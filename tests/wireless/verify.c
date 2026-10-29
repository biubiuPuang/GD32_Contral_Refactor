
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cmt2119b.h"
#include "cmt2219b.h"
#include "cmt_spi3.h"
static unsigned pins[2]={16|128,1}, clocks[2], output, cmd, byte, bits, readbit;
static unsigned csb=1,fcsb=1,sclk,fail_state,instant_tx,absent,writes,status_reads;
static unsigned txlen,rxpos,rxlen;
static uint64_t time_us;
static u8 regs[128],tx[256],rx[256];
static const unsigned allowed[2]={16|32|128,1};
static u8 chip_read(unsigned a) {
    u8 v=regs[a];
    if(a==CMT2300A_CUS_MODE_STA) ++status_reads;
    if((a==CMT2300A_CUS_INT_FLAG||a==CMT2300A_CUS_INT_CLR1)
       &&(regs[CMT2300A_CUS_INT1_CTL]&CMT2300A_MASK_INT_POLAR)) v=(u8)~v;
    return v;
}
static void chip_write(unsigned a,u8 v) {
    ++writes; if(absent) return;
    if(a==CMT2300A_CUS_INT_CLR1) {
        if(v&CMT2300A_MASK_TX_DONE_CLR) regs[a]&=(u8)~CMT2300A_MASK_TX_DONE_FLG;
        return;
    }
    if(a==CMT2300A_CUS_INT_CLR2) {
        if(v&CMT2300A_MASK_PKT_DONE_CLR) regs[CMT2300A_CUS_INT_FLAG]=0;
        return;
    }
    if(a==0x7f&&v==0xff) { memset(regs,0,sizeof(regs)); regs[CMT2300A_CUS_MODE_STA]=CMT2300A_STA_SLEEP; return; }
    regs[a]=v;
    if(a==CMT2300A_CUS_MODE_CTL&&!fail_state) {
        u8 st=0;
        switch(v) {
        case CMT2300A_GO_SLEEP: st=CMT2300A_STA_SLEEP; break;
        case CMT2300A_GO_STBY: st=CMT2300A_STA_STBY; break;
        case CMT2300A_GO_TX: st=CMT2300A_STA_TX; break;
        case CMT2300A_GO_RX: st=CMT2300A_STA_RX; break;
        case CMT2300A_GO_TFS: st=CMT2300A_STA_TFS; break;
        case CMT2300A_GO_RFS: st=CMT2300A_STA_RFS; break;
        }
        if(v==CMT2300A_GO_TX&&instant_tx) { st=CMT2300A_STA_STBY; regs[CMT2300A_CUS_INT_CLR1]|=CMT2300A_MASK_TX_DONE_FLG; }
        regs[CMT2300A_CUS_MODE_STA]=(regs[CMT2300A_CUS_MODE_STA]&0xf0)|st;
    }
}
void rcu_periph_clock_enable(uint32_t p) { assert(p<2); clocks[p]=1; }
static void gpio_update(uint32_t p,uint32_t mask,int high) {
    unsigned nc,nf,ns;
    assert(p<2&&clocks[p]&&!(mask&~allowed[p]));
    if(high) pins[p]|=mask; else pins[p]&=~mask;
    nc=!!(pins[0]&16); nf=!!(pins[1]&1); ns=!!(pins[0]&32);
    assert(nc||nf);
    if(csb&&!nc) bits=cmd=byte=readbit=0;
    if(fcsb&&!nf) bits=byte=readbit=0;
    if(!sclk&&ns&&output&&(!nc||!nf)) {
        byte=(byte<<1)|!!(pins[0]&128); ++bits;
        if(!nc&&bits==8) { cmd=byte; byte=0; }
        if(!nc&&bits==16) chip_write(cmd&0x7f,(u8)byte);
        if(!nf&&bits==8) { assert(txlen<256); tx[txlen++]=(u8)byte; }
    }
    if(!fcsb&&nf&&!output&&readbit==8) ++rxpos;
    csb=nc; fcsb=nf; sclk=ns;
}
void gpio_bit_set(uint32_t p,uint32_t m) { gpio_update(p,m,1); }
void gpio_bit_reset(uint32_t p,uint32_t m) { gpio_update(p,m,0); }
void gpio_mode_set(uint32_t p,uint32_t m,uint32_t pull,uint32_t pin) {
    (void)pull; assert(p<2&&clocks[p]&&!(pin&~allowed[p]));
    if(p==0&&(pin&128)) output=(m==GPIO_MODE_OUTPUT);
}
void gpio_output_options_set(uint32_t p,uint32_t t,uint32_t s,uint32_t pin) {
    (void)t; (void)s; assert(p<2&&clocks[p]&&!(pin&~allowed[p]));
}
FlagStatus gpio_input_bit_get(uint32_t p,uint32_t pin) {
    u8 v; static u8 latch;
    assert(p==0&&pin==128&&!output&&sclk&&readbit<8);
    if(absent) v=0xff;
    else if(!csb) { assert(bits==8&&(cmd&0x80)); if(!readbit) latch=chip_read(cmd&0x7f); v=latch; }
    else { assert(!fcsb&&rxpos<rxlen); v=rx[rxpos]; }
    return (v>>(7-readbit++))&1;
}
void delay_1us(uint32_t us) { assert(us&&us<=1000); time_us+=us; }
void delay_1ms(uint32_t ms) { assert(ms==1); time_us+=1000; }
static void idle(void) { assert(csb&&fcsb&&!sclk&&!output); }
int main(void) {
    const u8 pattern[]={0x00,0xff,0x81,0x5a,0xa5}; u8 result[5],v; unsigned prior;
    assert(CMT2119B_Init()); assert(CMT2300A_GetChipStatus()==CMT2300A_STA_STBY);
    assert(regs[CMT2300A_CUS_INT_EN]==CMT2300A_MASK_TX_DONE_EN); idle();
    cmt_spi3_write(0x50,0xa5); cmt_spi3_read(0x50,&v); assert(v==0xa5); idle();
    cmt_spi3_write_fifo(pattern,5); assert(txlen==5&&!memcmp(pattern,tx,5)); idle();
    memcpy(rx,pattern,5); rxlen=5; cmt_spi3_read_fifo(result,5); assert(rxpos==5&&!memcmp(pattern,result,5)); idle();
    cmt_spi3_write_fifo(pattern,0); cmt_spi3_read_fifo(result,0); idle();
    regs[CMT2300A_CUS_PKT17]=0x37; assert(CMT2300A_IsExist()&&regs[CMT2300A_CUS_PKT17]==0x37);
    assert(!CMT2119B_TxDone()); regs[CMT2300A_CUS_INT_CLR1]=CMT2300A_MASK_TX_DONE_FLG;
    assert(CMT2119B_TxDone()&&CMT2119B_TxDone()); CMT2300A_SetInterruptPolar(FALSE); assert(CMT2119B_TxDone());
    regs[CMT2300A_CUS_INT_CLR1]=0; assert(!CMT2119B_TxDone()); CMT2119B_ConfigPolling();
    instant_tx=1; assert(CMT2300A_GoTx()); instant_tx=0; assert(CMT2119B_TxDone());
    CMT2300A_ClearInterruptFlags(); assert(!CMT2119B_TxDone());
    fail_state=1; prior=status_reads; assert(!CMT2300A_GoRx()); assert(status_reads-prior==100); fail_state=0;
    prior=writes; assert(!CMT2300A_AutoSwitchStatus(0)); assert(prior==writes);
    assert(CMT2219B_Init()); assert(regs[CMT2300A_CUS_INT_EN]==CMT2300A_MASK_PKT_DONE_EN);
    assert(!CMT2219B_PacketReady()&&!CMT2219B_PacketError()&&!CMT2219B_FifoOverflow());
    regs[CMT2300A_CUS_INT_FLAG]=CMT2300A_MASK_PKT_OK_FLG; assert(CMT2219B_PacketReady()&&CMT2219B_PacketReady());
    CMT2300A_SetInterruptPolar(FALSE); assert(CMT2219B_PacketReady());
    regs[CMT2300A_CUS_INT_FLAG]=CMT2300A_MASK_PKT_ERR_FLG; assert(!CMT2219B_PacketReady()&&CMT2219B_PacketError());
    regs[CMT2300A_CUS_INT_FLAG]=CMT2300A_MASK_COL_ERR_FLG; assert(CMT2219B_PacketError());
    regs[CMT2300A_CUS_FIFO_FLAG]=CMT2300A_MASK_RX_FIFO_OVF_FLG; assert(CMT2219B_FifoOverflow());
    CMT2219B_ConfigPolling(); assert(!CMT2219B_PacketError());
    CMT_PortDelayUs(0); CMT_PortDelayUs(2501); CMT_PortDelayMs(0); CMT_PortDelayMs(3);
    absent=1; assert(!CMT2119B_Init()); assert(!CMT2219B_Init()); absent=0; idle();
    puts("PASS: GPIO clock/pin isolation; SPI register/FIFO byte order, direction, chip selects; zero-length FIFO; IsExist restore; TX/RX flags/polarity; instant TX; bounded timeout; invalid command; chip absent; delay bounds.");
    return 0;
}
