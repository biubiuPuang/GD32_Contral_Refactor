"""Recalculate the supplied AN1971 workbook's AFC formulas, without Excel.

AN1971 cells: C8=C4/92*1000; C11=MAX(L:L);
C12=INT(C11*C9*2^20/C4/1000/256);
G=F+C8/1000, H=C9, J=INT(G*H/C4),
M=C4*J/H, N=C4*(J+1)/H, O=M-C8/1000, P=N-C8/1000;
Q=MIN(ABS(F-O)*1000,ABS(F-P)*1000); R=0.95*Q;
S=MIN(INT(R*C9*2^20/C4/1000/256),C12).
L=F*(RX_ppm+TX_ppm)/1000.
The BIFF stream was extracted read-only with installed 7-Zip.
"""
from pathlib import Path
import hashlib, json, math, struct, subprocess

HERE=Path(__file__).resolve().parent
def check():
    # Verify the two critical formula token streams against original BIFF8.
    source=HERE.parents[2]/'资料/CMT2119B及CMT2219B芯片手册在这/AN1971-CMT2300A-CMT2219B 跳频计算表-V1.0(AN197配套).xls'
    data=subprocess.run([r'C:\Program Files\7-Zip\7z.exe','e','-so',str(source),'Workbook'],
                        check=True,capture_output=True).stdout
    (HERE/'frequency_workbook.bin').write_bytes(data)
    expected={
        (11,2): '440a0002c044080002c0051e02001e1400070544030002c0061ee803061e000106411900',
        (4,18): '44040011c04408000200051e02001e140007054403000200061ee803061e000106411900240b00020042020600',
    }
    seen={}; offset=0
    while offset+4<=len(data):
        kind,length=struct.unpack_from('<HH',data,offset)
        payload=data[offset+4:offset+4+length];offset+=4+length
        if kind==6 and len(payload)>=22:
            cell=struct.unpack_from('<HH',payload)
            if cell in expected and cell not in seen: seen[cell]=payload[22:].hex()
    assert seen==expected, 'Workbook formulas changed; recalculate/review AFC table'
    xtal,divider,ppm=26.,4.,80.
    frequencies=[433.920+.150*i for i in range(6)]
    initial=math.floor(max(frequencies)*ppm/1000*divider*2**20/xtal/1000/256)
    rows=[]
    for i,frequency in enumerate(frequencies):
        intermediate=xtal/92
        integer=math.floor((frequency+intermediate)*divider/xtal)
        lo=xtal*integer/divider-intermediate
        hi=xtal*(integer+1)/divider-intermediate
        distance=min(abs(frequency-lo),abs(frequency-hi))*1000
        threshold=min(math.floor(.95*distance*divider*2**20/xtal/1000/256),initial)
        assert threshold==21
        rows.append(dict(channel=i,mhz=round(frequency,3),afc_ovf_th=threshold))
    report=dict(crystal_mhz=xtal,tx_ppm=40,rx_ppm=40,initial_afc=initial,
                source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                workbook_stream_sha256=hashlib.sha256(data).hexdigest(),channels=rows)
    (HERE/'frequency_check.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print('AN1971 formulas checked: CH0..5, 150kHz spacing, all RX AFC thresholds = 21.',flush=True)
if __name__=='__main__': check()
