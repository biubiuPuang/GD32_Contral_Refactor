"""2026-11-2: automate COM2(TX)/COM18(RX); no button presses or IDE required.

Usage: python run_test.py --count 10000
Requires the dedicated test firmware on both boards. Does NOT flash boards.
"""
from pathlib import Path
import argparse
from datetime import datetime, timezone, timedelta
import json
import re
import secrets
import time
import serial

HERE = Path(__file__).resolve().parent
TZ = timezone(timedelta(hours=8))


def fields(line):
    return dict(re.findall(r"([A-Z_0-9]+)=([^\s]+)", line))


class Boards:
    def __init__(self, tx, rx, output):
        self.ports, self.buffers = {}, {}
        self.events = []
        self.output = output
        self.log = (output / "serial.jsonl").open("w", encoding="utf-8")
        try:
            for role, name in [("TX", tx), ("RX", rx)]:
                port = serial.Serial(port=None, baudrate=115200, timeout=0, write_timeout=2)
                port.dtr = False
                port.rts = False
                port.port = name
                port.open()
                port.reset_input_buffer()
                self.ports[role] = port
                self.buffers[role] = b""
        except BaseException:
            self.close()
            raise

    def send(self, role, command):
        self.ports[role].write((command + "\r\n").encode("ascii"))
        self.log.write(json.dumps({"time": datetime.now(TZ).isoformat(), "role": role,
                                   "direction": "host_to_board", "line": command}) + "\n")
        self.log.flush()

    def poll(self, seconds=.05):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for role, port in self.ports.items():
                data = port.read(port.in_waiting or 1)
                self.buffers[role] += data
                while b"\n" in self.buffers[role]:
                    raw, self.buffers[role] = self.buffers[role].split(b"\n", 1)
                    line = raw.decode("ascii", errors="replace").strip()
                    event = {"time": datetime.now(TZ).isoformat(), "role": role,
                             "direction": "board_to_host", "line": line}
                    self.events.append(event)
                    self.log.write(json.dumps(event) + "\n")
                if len(self.buffers[role]) > 4096:
                    raise RuntimeError(f"{role}: invalid serial stream")
            self.log.flush()
            time.sleep(.005)

    def wait(self, role, prefix, after=0, timeout=4):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for event in self.events[after:]:
                if event["line"].startswith("PLT ERROR"):
                    raise RuntimeError(event["role"] + ": " + event["line"])
                if event["role"] == role and event["line"].startswith(prefix):
                    return fields(event["line"])
            self.poll()
        raise TimeoutError(f"{role}: no {prefix}")

    def command(self, role, command, prefix):
        after = len(self.events)
        self.send(role, command)
        return self.wait(role, prefix, after=after)

    def close(self):
        for port in self.ports.values():
            port.close()
        self.log.close()


def measure(count=10000, tx="COM2", rx="COM18"):
    if not 1 <= count <= 10000:
        raise ValueError("count must be 1..10000")
    output = HERE / "results" / datetime.now(TZ).strftime("%Y%m%d_%H%M%S")
    output.mkdir(parents=True, exist_ok=False)
    result = {"started_at": datetime.now(TZ).isoformat(), "requested": count,
              "tx_port": tx, "rx_port": rx, "valid": False,
              "scope": "one physical pair, current placement; not 50-pair concurrency or range qualification"}
    boards = None
    armed = False
    try:
        boards = Boards(tx, rx, output)
        info = {}
        for role in ["TX", "RX"]:
            info[role] = boards.command(role, "GET", "PLT INFO")
            if info[role].get("ROLE") != role or info[role].get("RADIO") != "OK":
                raise RuntimeError(f"Wrong firmware/role or radio failure: {info[role]}")
        if any(info["TX"][key] != info["RX"][key] for key in ["ID", "CH", "FREQ_KHZ", "RATE", "TAG"]):
            raise RuntimeError("TX/RX radio configuration mismatch")
        if info["RX"].get("SAFE") != "1":
            raise RuntimeError("Receiver output inhibition missing")
        result["info"] = info
        for role in ["TX", "RX"]:
            boards.command(role, "STOP", "PLT STOPPED")
        session = secrets.randbelow(255) + 1
        result["session"] = session
        armed = True  # also clean up a receiver that armed before a TX setup failure
        # Prepare receiver first. Wired USB replies synchronize setup; there is no radio ACK.
        for role in ["RX", "TX"]:
            reply = boards.command(role, f"ARM,{session},{count}", "PLT ARMED")
            if reply != {"SESSION": str(session), "TARGET": str(count)}:
                raise RuntimeError("ARM acknowledgement mismatch")
        boards.command("TX", "RUN", "PLT RUNNING")
        deadline = time.monotonic() + count * .065 + 20
        next_report = time.monotonic()
        progress = []
        while time.monotonic() < deadline:
            boards.poll(.15)
            for event in boards.events:
                if event["line"].startswith("PLT ERROR"):
                    raise RuntimeError(event["role"] + ": " + event["line"])
            done = any(e["role"] == "TX" and e["line"].startswith("PLT DONE")
                       and fields(e["line"]).get("SESSION") == str(session) for e in boards.events)
            if done:
                break
            if time.monotonic() >= next_report:
                snapshot = {}
                for role in ["TX", "RX"]:
                    snapshot[role] = boards.command(role, "STAT", "PLT STAT")
                    if snapshot[role].get("SESSION") != str(session) or snapshot[role].get("STATE") == "5":
                        raise RuntimeError("Board reset, wrong session, or board fault")
                    if any(int(snapshot[role][key]) != 0 for key in ["UARTERR", "LOGDROP", "TXFAIL"]):
                        raise RuntimeError(f"{role}: instrumentation or local transmit failure")
                progress.append({"time": datetime.now(TZ).isoformat(), **snapshot})
                print(f"TX={snapshot['TX']['SENT']}/{count} RX={snapshot['RX']['RX']} "
                      f"RX_HWERR={snapshot['RX']['HWERR']} MAXGAP={int(snapshot['RX']['MAXGAP_TICKS'])/10:.1f}ms", flush=True)
                next_report = time.monotonic() + 5
        else:
            raise TimeoutError("Automatic test did not finish")
        boards.poll(.5)  # include the final air packet and final UART reply
        final = {role: boards.command(role, "STAT", "PLT STAT") for role in ["TX", "RX"]}
        audits = {role: boards.command(role, "AUDIT", "PLT AUDIT") for role in ["TX", "RX"]}
        result["memory_audits"] = audits
        result["final"] = final
        result["progress"] = progress
        sent, received = int(final["TX"]["SENT"]), int(final["RX"]["RX"])
        valid = (sent == count and int(final["TX"]["ATTEMPT"]) == count and 0 <= received <= sent
                 and final["TX"]["STATE"] == "3" and final["RX"]["STATE"] == "1"
                 and final["RX"]["SAFE"] == "1"
                 and all(int(final[role][key]) == 0 for role in ["TX", "RX"]
                         for key in ["TXFAIL", "LOGDROP", "UARTERR"])
                 and all(final[role]["SESSION"] == str(session) for role in ["TX", "RX"]))
        valid = (valid and all(audit.get("GUARD") == "1" and audit.get("MATCH") == "1"
                              for audit in audits.values())
                 and int(audits["RX"]["BITS"]) == received
                 and not any(e["line"].startswith("PLT ERROR") for e in boards.events))
        result.update(valid=valid, sent=sent, unique_received=received,
                      lost=sent-received if valid else None,
                      loss_percent=100*(sent-received)/sent if valid else None,
                      max_receive_gap_ms=int(final["RX"]["MAXGAP_TICKS"])/10,
                      receive_gaps_at_least_85ms=int(final["RX"]["GAP85"]))
        if not valid:
            raise RuntimeError("Incomplete or instrument-faulted run; loss percentage is not valid")
    except BaseException as error:
        result["error"] = f"{type(error).__name__}: {error}"
        raise
    finally:
        if boards is not None:
            if armed:
                for role in ["TX", "RX"]:
                    try:
                        boards.command(role, "STOP", "PLT STOPPED")
                    except Exception as error:
                        result.setdefault("cleanup_errors", []).append(f"{role}: {error}")
            boards.close()
        if result.get("cleanup_errors"):
            result.update(valid=False, lost=None, loss_percent=None,
                          error="Stop/cleanup verification failed; inspect cleanup_errors")
        result["finished_at"] = datetime.now(TZ).isoformat()
        (output / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
        if result["valid"]:
            text = (f"# 无线丢包测试实测结果\n\n时间：{result['started_at']}\n\n"
                    f"发送完成 {result['sent']} 包，接收有效且去重后 {result['unique_received']} 包，"
                    f"丢失 {result['lost']} 包，丢包率 **{result['loss_percent']:.4f}%**。\n\n"
                    f"接收有效包之间最大间隔 {result['max_receive_gap_ms']:.1f}ms；"
                    f"达到85ms的间隔 {result['receive_gaps_at_least_85ms']} 次。\n\n"
                    "这是当前摆放环境中一套真实硬件的结果，不代表50套并发、100米或工业现场验证。\n\n"
                    "丢包率=(本地TX_DONE成功发送数−接收端去重有效包数)/本地TX_DONE成功发送数。"
                    "本地发送超时、串口计数不完整、复位或会话不一致会将该轮标为无效。"
                    "CRCERR/HWERR为接收异常事件计数，并不等于丢包数；LOGDROP为日志丢弃，不是无线丢包。\n")
        else:
            text = f"# 无线丢包测试未形成有效结果\n\n{result.get('error', 'Unknown error')}\n\n不能据此报告丢包率。\n"
        (output / "result.md").write_text(text, encoding="utf-8")
        print("RESULT_DIRECTORY", output, flush=True)
    print(json.dumps({k: v for k, v in result.items() if k != "progress"}, ensure_ascii=False, indent=2), flush=True)
    if not result["valid"]:
        raise RuntimeError(result.get("error", "Invalid run"))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--count", type=int, default=10000)
    parser.add_argument("--tx", default="COM2")
    parser.add_argument("--rx", default="COM18")
    args = parser.parse_args()
    measure(args.count, args.tx, args.rx)
