# 211 / 221 无线连通性 Demo

本目录是 221 接收应用；211 发送应用位于另一个工程同名目录。两端共用相同的调用方式，内部按芯片方向分别实现。

## 使用

1. 打开各自 `Project/GD32E230C8T6.uvprojx`。已添加 `wu-xian-App` 分组、`wireless_demo.c` 和头文件搜索路径。
2. 将本目录 `main_example.c` 的内容完整复制到对应工程 `User/main.c`，保存后重新编译、烧录。示例文件未加入正式工程，避免出现两个 `main()`。
3. 串口助手设为 **115200、8 数据位、无校验、1 停止位**。沿用现有 USART1：PA2 为 TX，PA3 为 RX。
4. 两块板均上电。211 持续逐包发送，221 持续接收；两端各自约每 300ms 打印一次本窗口最新数据。

主程序两端相同：

```c
#include "wireless_demo.h"

int main(void)
{
    WirelessDemo_Init();
    while(1) {
        WirelessDemo_Process();
    }
}
```

`WirelessDemo_Init()`、`WirelessDemo_Process()` 是本项目自定义应用函数。初始化函数复用现有 `systick_config()`、`usart_init()` 和芯片 BSP；后续寄存器、FIFO 和状态切换复用厂家 `CMT2300A_*` 接口。

初始化内部先开启所需时钟，再配置 USART、GPIO 和定时器。独占 **TIMER2**，按 10kHz 自由计数，轮询计时，不增加中断函数；现有 SysTick 继续供底层 SPI 延时使用。主循环不要加 `delay_1ms(300)`，也不要从中断调用应用函数。当前两端 MCU 时钟配置均使用内部 IRC8M 的 PLL。

## 两端测试参数

| 项目 | 211 发送端 | 221 接收端 |
| --- | --- | --- |
| 导出源文件 | `211_config.exp` | `221_config.exp` |
| 频率 / 频道 | 433.920MHz / 0 | 433.920MHz / 0 |
| 无线芯片晶振 | 26MHz | 26MHz |
| 调制 / 数据率 / 频偏 | GFSK / 9.6kbps / 10kHz | 同左 |
| 功率 | +13dBm | 不适用 |
| 前导码 | 8 字节 0xAA | 检测 2 字节 0xAA |
| 同步字 | 0x2DD4A7B3 | 同左 |
| Node ID / 模式 | 0x00000001 / 1 | 同左 |
| 包格式 | 定长，8 字节有效载荷 | 同左 |
| 有效载荷内容 | 4 字节 Node ID + 4 字节递增序号 | 同左 |
| CRC | CCITT-16，Seed 0xFFFF，整个 Payload | 同左 |
| 退出状态 | STBY | STBY |
| 完成判断 | SPI 查询 TX_DONE | SPI 查询 PKT_OK，并要求 CRC_OK |

`wireless_demo_params.h` 按实际 `.exp` 的 0x00..0x5F 共 96 字节生成，头部记录源文件 SHA256。代码不修改 `.exp`，不沿用厂家 CMT2300A 示例参数。221 的 0x55..0x5E 为保留区，只保留导出记录，不写入芯片；0x5F LBD 单独加载。

8 字节有效载荷不含前导码、同步字和 CRC，不带长度字节。包长寄存器保留导出值 7。 **TX FIFO 仅写入 4 字节序号，4 字节 Node ID 由发送芯片根据 NODE_VALUE 自动插入；RX FIFO 则读出 Node ID + 序号，共 8 字节。** 两端 FIFO 的读写字节数不同，不能将完整 8 字节日志缓冲区原样写入 TX FIFO，否则会实际发出 ID + ID。序号从 1 开始，高字节在前：首个测试包为 `00 00 00 01 00 00 00 01`。211 仅在查到 TX_DONE 后累计发送成功和推进序号；这不能证明接收端收到，因为系统没有回传。

## 串口判断

```text
INIT OK
TX seq=12 data=00 00 00 01 00 00 00 0C total=12 new=12 err=0
RX seq=11 data=00 00 00 01 00 00 00 0B total=10 new=10 err=0
RX NO_NEW_DATA total=10 err=0 state=OK
```

- `seq`：最近一个成功发送 / 有效接收包的序号；持续变化可用于判断通信是否持续。
- `data`：该包的 8 字节内容，十六进制显示。
- `total`：从本板上电至今的成功包数；`new`：本次打印窗口内的成功包数。
- `err`：本板检测到的错误次数，包括初始化/状态失败、发送超时，或接收 CRC/包错误、FIFO 溢出、ID 不符。
- `NO_NEW_DATA`：本窗口没有新的成功包，不把旧数据当成新包打印。
- `state=SPI_OR_STBY`：初始化或寄存器访问失败；`CONFIG_READBACK`：导出参数写入后的读回校验失败；`GO_TX/GO_RX`：进入收发状态失败；`TX_TIMEOUT`：100ms 内未查到发送完成。
- `OFFLINE`：本窗口有过成功包，但随后进入故障恢复状态。故障时每 1 秒尝试重新初始化。

打印只抽样显示最新包，两端上电时间和打印时刻不同，序号不要求每行相等。串口打印是短暂的阻塞输出，接收端会先重新进入 RX 再打印；本 Demo 不作为零丢包率测试工具。

## 验证与范围

本目录 `validation` 保存使用 `main_example.c` 的独立验证工程和编译日志。正式工程中的 `User/main.c` 仍由用户手动替换，原有驱动和原 `.exp` 文件未修改。

从仓库根目录运行：

```powershell
python 211_Project/wu-xian-App/validation/check_demo.py
```

脚本使用本机 Keil 6.21 完整编译两端示例，并用本机 GCC 模拟应用接口。覆盖导出数组一致性、初始化失败与重试、写后读回失败、定时器回绕、TX_DONE 后计数、发送超时、CRC 错误拒收、Node ID 检查、FIFO 溢出和 RX 重启。模拟不访问开发板，不能证明实际射频连通。

没有改动有线控制、按键、执行器、配对或心跳逻辑。

参考：本地厂家 `CMT2300A_Demo_Tx/Rx` 示例，以及 [厂家 AN143 FIFO 和包格式使用指南](https://www.hoperf.cn/uploads/AN143_CMT2300A_FIFO%E5%92%8C%E5%8C%85%E6%A0%BC%E5%BC%8F%E4%BD%BF%E7%94%A8%E6%8C%87%E5%8D%97_V1.1_202511_1764551407.pdf)。


## 2026-10-29 实板调试记录

- 端口：COM2 为 211，COM18 为 221。
- 复现：发送序号持续增加，接收序号固定为 1。
- 定位：ST-Link 断点停在发送 FIFO 写入前，临时将 FIFO 前 4 字节改成 `12 34 56 78`，接收实际变成 `00 00 00 01 12 34 56 78`。证实发送芯片自动添加 Node ID，原程序重复把 ID 当作数据写入。
- 修复：发送 FIFO 只写 `pending[4..7]` 共 4 字节；日志仍显示完整有效载荷 8 字节。接收 FIFO 仍读 8 字节。底层驱动和 `.exp` 未修改。
- 仅通过 ST-Link 更新 211 固件，Flash 读回与修正版 HEX 一致；221 保持原固件即可接收。
- 连续采集 15 秒，两端各 50 条有效报告。TX seq=1013..1634，RX seq=1014..1635，两端报告 err=0，RX 序号持续递增、数据内序号解码一致。
- 两端累计 total 会因启动、烧录、调试暂停而不同；不能直接用两个 total 的差计算丢包率。该次检查证实序号传输正确，不代表所有距离、干扰场景或零丢包保证。
- 记录保存在 211 应用目录 `validation/hardware_debug`：`node_probe_verified.log`、`COM2_after_fix.log`、`COM18_after_fix.log`、`after_fix_summary.json`、`flash_readback.json`。旧固件备份为 `sender_before_debug.bin`。
- 调试结束已移除硬件断点并恢复发送，命令行已释放 COM2/COM18，可重新在 XCOM 打开串口观察。
