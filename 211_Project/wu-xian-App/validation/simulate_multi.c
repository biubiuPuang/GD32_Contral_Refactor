/* Behavioral tests: real application, mocked BSP/radio/Flash. No board access. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
static char output[262144];
static size_t output_length;
#define DEMO_HOST_TEST 1
#include APP_SOURCE

static uint16_t counter;
static uint8_t registers[128],packet[8],flags,status;
static unsigned bank_bytes,fifo_reads,fifo_writes,rx_starts,init_calls;
static int timer_clock_enabled,gpio_clock,systick_ready,available,corrupt_readback;
static int rx_fail,overflow,done,fifo_selected,fifo_cleared,stby_fail;
static uint32_t pins,irq_mask,uart_flags[4],uart_byte;
static uint32_t flash[512],image_limit;
static unsigned flash_writes,flash_erases,program_calls;
static int flash_unlocked,fail_program,fail_erase;
static int tx_irq;

static u32 Demo_ReadWord(u32 address)
{
    assert(address>=DEMO_FLASH_FIRST && address<DEMO_FLASH_END && !(address&3U));
    return flash[(address-DEMO_FLASH_FIRST)/4U];
}
static u32 Demo_ImageLimit(void) { return image_limit; }
uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask=1U; }
void __enable_irq(void) { irq_mask=0U; }
void fmc_unlock(void) { assert(irq_mask); flash_unlocked=1; }
void fmc_lock(void) { flash_unlocked=0; }
void fmc_flag_clear(uint32_t f) { (void)f; assert(flash_unlocked); }
uint32_t fmc_page_erase(uint32_t address)
{
    assert(flash_unlocked && irq_mask && address>=DEMO_FLASH_FIRST && address<DEMO_FLASH_END);
    assert(!(address&(DEMO_FLASH_PAGE-1U)));
    if(g_demo.saved_address) assert(address!=(g_demo.saved_address&~(DEMO_FLASH_PAGE-1U)));
    if(fail_erase) return 1U;
    memset(flash+(address-DEMO_FLASH_FIRST)/4U,0xFF,DEMO_FLASH_PAGE);
    ++flash_erases;return FMC_READY;
}
uint32_t fmc_word_program(uint32_t address,uint32_t value)
{
    uint32_t *word;
    assert(flash_unlocked && irq_mask && address>=DEMO_FLASH_FIRST && address<DEMO_FLASH_END && !(address&3U));
    ++program_calls;
    if(fail_program && program_calls==(unsigned)fail_program) return 1U;
    word=flash+(address-DEMO_FLASH_FIRST)/4U;
    assert((*word&value)==value);*word&=value;++flash_writes;return FMC_READY;
}
uint32_t rcu_clock_freq_get(uint32_t c) { return c==CK_APB1?36000000U:72000000U; }
void rcu_periph_clock_enable(uint32_t c)
{
    if(c==RCU_TIMER2)timer_clock_enabled=1;
    else { assert(c==RCU_GPIOA);gpio_clock=1; }
}
void timer_deinit(uint32_t t) { assert(t==TIMER2 && timer_clock_enabled); }
void timer_struct_para_init(timer_parameter_struct *p) { memset(p,0,sizeof(*p)); }
void timer_init(uint32_t t,timer_parameter_struct *p)
{ assert(t==TIMER2 && timer_clock_enabled && p->prescaler==7199U && p->period==65535U); }
void timer_counter_value_config(uint32_t t,uint16_t v) { assert(t==TIMER2);counter=v; }
void timer_enable(uint32_t t) { assert(t==TIMER2 && timer_clock_enabled); }
uint32_t timer_counter_read(uint32_t t) { assert(t==TIMER2);return counter; }
void gpio_mode_set(uint32_t port,uint32_t mode,uint32_t pull,uint32_t p)
{ assert(gpio_clock && port==GPIOA && mode==GPIO_MODE_INPUT && pull==GPIO_PUPD_PULLUP && p==(GPIO_PIN_9|GPIO_PIN_10)); }
uint32_t gpio_input_bit_get(uint32_t port,uint32_t p) { assert(port==GPIOA);return pins&p?SET:RESET; }
void systick_config(void) { systick_ready=1; }
void usart_init(uint32_t baud) { assert(baud==115200U && systick_ready); }
uint32_t usart_flag_get(uint32_t port,uint32_t f) { assert(port==BSP_USART && f<4);return uart_flags[f]; }
void usart_flag_clear(uint32_t port,uint32_t f) { assert(port==BSP_USART && f<4);uart_flags[f]=0; }
uint16_t usart_data_receive(uint32_t port) { assert(port==BSP_USART);uart_flags[USART_FLAG_RBNE]=0;return (uint16_t)uart_byte; }
void usart_interrupt_enable(uint32_t port,uint32_t f)
{ assert(port==BSP_USART);if(f==USART_INT_TBE)tx_irq=1;else assert(f==USART_INT_RBNE); }
void usart_interrupt_disable(uint32_t port,uint32_t f)
{ assert(port==BSP_USART && f==USART_INT_TBE);tx_irq=0; }
uint32_t usart_interrupt_flag_get(uint32_t port,uint32_t f)
{ assert(port==BSP_USART && f==USART_INT_FLAG_TBE);return (uint32_t)tx_irq; }
void usart_data_transmit(uint32_t port,uint16_t value)
{ assert(port==BSP_USART && output_length+1<sizeof(output));output[output_length++]=(char)value;output[output_length]=0; }
void nvic_irq_enable(uint32_t irq,uint32_t priority) { assert(irq==USART1_IRQn && priority==1U); }
BOOL CMT2119B_Init(void) { ++init_calls;return (BOOL)available; }
BOOL CMT2219B_Init(void) { ++init_calls;return (BOOL)available; }
void CMT2119B_ConfigPolling(void) { flags=0; }
void CMT2219B_ConfigPolling(void) { flags=0; }
BOOL CMT2119B_TxDone(void) { return (BOOL)done; }
BOOL CMT2219B_FifoOverflow(void) { return (BOOL)overflow; }
BOOL CMT2300A_ConfigRegBank(u8 address,const u8 bank[],u8 length)
{
#if !WIRELESS_DEMO_TX
    assert(address!=0x55U);
#endif
    memcpy(registers+address,bank,length);bank_bytes+=length;return TRUE;
}
u8 CMT2300A_ReadReg(u8 address)
{ return (u8)(registers[address]^((corrupt_readback && address==0x18U)?1U:0U)); }
void CMT2300A_WriteReg(u8 address,u8 value) { registers[address]=value; }
void CMT2300A_SetFrequencyChannel(u8 channel) { registers[CMT2300A_CUS_FREQ_CHNL]=channel; }
void CMT2300A_SetFrequencyStep(u8 step) { registers[CMT2300A_CUS_FREQ_OFS]=step; }
BOOL CMT2300A_GoSleep(void) { status=CMT2300A_STA_SLEEP;return TRUE; }
BOOL CMT2300A_GoStby(void) { if(stby_fail)return FALSE;status=CMT2300A_STA_STBY;return TRUE; }
BOOL CMT2300A_GoTx(void) { status=CMT2300A_STA_TX;return TRUE; }
BOOL CMT2300A_GoRx(void)
{ assert(fifo_selected && fifo_cleared);++rx_starts;status=CMT2300A_STA_RX;return rx_fail?FALSE:TRUE; }
void CMT2300A_EnableFifoMerge(BOOL enabled) { assert(!enabled); }
void CMT2300A_EnableRxFifoAutoClear(BOOL enabled) { assert(enabled); }
void CMT2300A_EnableInterrupt(u8 f) { assert(f==(CMT2300A_MASK_PKT_DONE_EN|CMT2300A_MASK_CRC_OK_EN)); }
void CMT2300A_EnableWriteFifo(void) { fifo_selected=1;fifo_cleared=0; }
void CMT2300A_EnableReadFifo(void) { fifo_selected=1; }
u8 CMT2300A_ClearTxFifo(void) { assert(fifo_selected);fifo_cleared=1;return 0; }
u8 CMT2300A_ClearRxFifo(void) { assert(fifo_selected);fifo_cleared=1;overflow=0;return 0; }
u8 CMT2300A_ClearInterruptFlags(void) { flags=0;done=0;return 0; }
void CMT2300A_WriteFifo(const u8 bytes[],u16 length)
{
    assert(fifo_selected && fifo_cleared && length==4U);
    packet[0]=registers[0x4BU];packet[1]=registers[0x4AU];
    packet[2]=registers[0x49U];packet[3]=registers[0x48U];
    memcpy(packet+4,bytes,4);++fifo_writes;
}
void CMT2300A_ReadFifo(u8 bytes[],u16 length)
{ assert(status==CMT2300A_STA_STBY && length==8 && (flags&CMT2300A_MASK_CRC_OK_FLG));memcpy(bytes,packet,8);++fifo_reads; }
u8 CMT2300A_ReadPollingFlags(u8 address) { assert(address==CMT2300A_CUS_INT_FLAG);return flags; }
u8 CMT2300A_GetChipStatus(void) { return status; }

static void flush_output(void) { while(tx_irq)USART1_IRQHandler(); }
static void service(void) { WirelessDemo_Process();flush_output(); }
#define WirelessDemo_Process service

static void reset(int clear_flash)
{
    memset(registers,0,sizeof(registers));memset(packet,0,sizeof(packet));memset(uart_flags,0,sizeof(uart_flags));
    if(clear_flash)memset(flash,0xFF,sizeof(flash));
    counter=0;output_length=0;output[0]=0;
    bank_bytes=fifo_reads=fifo_writes=rx_starts=init_calls=0;
    timer_clock_enabled=gpio_clock=systick_ready=corrupt_readback=0;
    rx_fail=overflow=done=fifo_selected=fifo_cleared=stby_fail=0;
    available=1;flags=0;status=CMT2300A_STA_STBY;
    pins=GPIO_PIN_9|GPIO_PIN_10;irq_mask=0;
    flash_writes=flash_erases=program_calls=0;flash_unlocked=fail_program=fail_erase=tx_irq=0;
    image_limit=0x08006000U;
    WirelessDemo_Init();flush_output();
}
static void advance(u16 ticks) { counter=(u16)(counter+ticks);WirelessDemo_Process(); }
static void uart_char(u8 c) { assert(!irq_mask);uart_byte=c;uart_flags[USART_FLAG_RBNE]=1;USART1_IRQHandler(); }
static void command(const char *text)
{
    while(*text)uart_char((u8)*text++);
    uart_char('\r');uart_char('\n');
    WirelessDemo_Process();WirelessDemo_Process();
}
static void configure(u32 id,u32 ch)
{
    char line[48];snprintf(line,sizeof(line),"SET,%lu,%lu",(unsigned long)id,(unsigned long)ch);command(line);
}
static void check_configuration(void)
{
    unsigned before;
    reset(1);assert(!g_config.configured && !g_demo.ready && !init_calls);
    configure(0x12345678U,5U);
    assert(g_config.configured && g_config.id==0x12345678U && g_config.channel==5U && g_demo.ready);
    assert(registers[0x48]==0x78 && registers[0x49]==0x56 && registers[0x4A]==0x34 && registers[0x4B]==0x12);
    assert(registers[0x46]==7 && registers[0x47]==0x0D && registers[0x63]==5 && registers[0x64]==60);
#if !WIRELESS_DEMO_TX
    assert(registers[0x27]==21);
#endif
    before=flash_writes;configure(0x12345678U,5U);assert(flash_writes==before);
    reset(0);assert(g_config.id==0x12345678U && g_config.channel==5 && g_demo.ready);
    command("GET");assert(strstr(output,"FREQ=434.670MHz PAIRED=1"));
    command("DEFAULT");assert(!g_config.configured && !g_demo.ready);
    reset(0);assert(!g_config.configured && !g_demo.ready);
    configure(UINT32_MAX,0);assert(g_config.id==UINT32_MAX && g_demo.ready);
    for(u32 id=1;id<=100;++id){configure(id,id%6);assert(g_config.id==id && g_config.channel==id%6);}
    assert(flash_erases>=2);
    before=flash_writes;
    command("SET,0,0");command("SET,1,6");command("SET,4294967296,0");
    command("SET,-1,0");command("SET,1,0junk");command("SET,,0");command("SET,1,");
    assert(flash_writes==before && g_config.id==100);
    stby_fail=1;configure(200,1);assert(g_config.id==100 && flash_writes==before);stby_fail=0;
    reset(1);image_limit=DEMO_FLASH_FIRST+1;WirelessDemo_Init();configure(1,0);
    assert(!g_demo.storage_ok && !flash_writes && !g_demo.ready);
}
static void check_power_loss(void)
{
    uint32_t snapshot[512];
    reset(1);configure(7,1);memcpy(snapshot,flash,sizeof(flash));
    for(int fail=1;fail<=8;++fail){
        memcpy(flash,snapshot,sizeof(flash));reset(0);fail_program=fail;
        configure(8,2);assert(g_config.id==7);
        reset(0);assert(g_config.id==7 && g_config.channel==1);
    }
    reset(1);
    for(u32 id=1;id<=32;++id)configure(id,0);
    assert(g_demo.saved_address==DEMO_FLASH_FIRST+31*DEMO_RECORD_SIZE);
    fail_erase=1;configure(33,1);assert(g_config.id==32);
    reset(0);assert(g_config.id==32);
    configure(33,1);assert(g_config.id==33 && g_demo.saved_address==DEMO_FLASH_FIRST+DEMO_FLASH_PAGE);
    flash[(g_demo.saved_address-DEMO_FLASH_FIRST)/4+3]^=1U;
    reset(0);assert(g_config.id==32); /* Bad CRC falls back to previous committed record. */
}
static void check_uart_and_faults(void)
{
    unsigned writes;
    reset(1);configure(1,0);writes=flash_writes;
    for(unsigned i=0;i<64;++i)uart_char('X');
    uart_char('\n');WirelessDemo_Process();WirelessDemo_Process();
    assert(flash_writes==writes);
    for(unsigned i=0;i<150;++i)uart_char('X');
    WirelessDemo_Process();uart_char('\n');WirelessDemo_Process();
    command("GET");assert(!g_demo.discard && !g_uart_fault);
    uart_flags[USART_FLAG_ORERR]=1;USART1_IRQHandler();WirelessDemo_Process();
    uart_char('\n');WirelessDemo_Process();configure(2,1);assert(g_config.id==2);
    available=0;configure(3,2);assert(g_config.id==3 && !g_demo.ready);
    available=1;advance(10000);assert(g_demo.ready);
    corrupt_readback=1;configure(4,3);assert(!g_demo.ready);
    corrupt_readback=0;advance(10000);assert(g_demo.ready);
    for(unsigned i=0;i<50;++i)Demo_Print("A log line that must not block application control\r\n");
    assert(g_demo.log_drops>0 && g_demo.ready);flush_output();
    counter=65530U;g_demo.previous_timer=65520U;g_demo.ticks=UINT32_MAX-5U;
    assert(Demo_Now()==4U);counter=20U;assert(Demo_Now()==30U);
}
#if WIRELESS_DEMO_TX
static void finish_tx(void) { done=1;status=CMT2300A_STA_STBY;advance(190); }
static void drain_stops(void)
{
    for(unsigned i=0;i<12 && (g_demo.busy||g_demo.stops_left);++i){
        if(g_demo.busy)finish_tx();else advance(350);
    }
    assert(!g_demo.busy && !g_demo.stops_left);
}
static void check_button(void)
{
    unsigned before;
    u32 first_seed, other_seed;
    reset(1);configure(1,0);configure(1,0);first_seed=g_demo.random;
    reset(1);configure(7,0);configure(7,0);other_seed=g_demo.random;
    assert(first_seed!=other_seed); /* Repeated factory SET must retain ID diversity. */
    reset(1);configure(1,0);drain_stops();before=fifo_writes;
    advance(600);assert(fifo_writes==before); /* Released devices do not flood RF. */
    pins&=~GPIO_PIN_9;advance(1);advance(90);assert(!g_demo.button_down);
    pins|=GPIO_PIN_9;advance(1);pins&=~GPIO_PIN_9;advance(1);advance(99);
    assert(!g_demo.button_down && fifo_writes==before);
    advance(1);assert(g_demo.button_down && g_demo.busy && packet[4]==DEMO_TAG && packet[5]==1);
    assert(memcmp(packet,"\0\0\0\1",4)==0);finish_tx();
    pins&=~GPIO_PIN_10;advance(100);assert(g_demo.button_down); /* Stage 2: still RUN. */
    pins|=GPIO_PIN_10;advance(100);assert(g_demo.button_down); /* Back to stage 1. */
    pins|=GPIO_PIN_9;advance(1);advance(99);assert(g_demo.button_down);
    pins&=~GPIO_PIN_9;advance(1);pins|=GPIO_PIN_9;advance(1);advance(100);
    assert(!g_demo.button_down && g_demo.busy && packet[5]==0);
    drain_stops();before=fifo_writes;advance(1000);assert(fifo_writes==before);
    pins&=~GPIO_PIN_9;advance(1);advance(100);assert(g_demo.busy);
    advance(DEMO_TX_TIMEOUT);assert(!g_demo.ready && strcmp(g_demo.error,"TX_TIMEOUT")==0);
}
#else
static void inject(u32 id,u8 action,u8 tag,u8 event_flags)
{
    packet[0]=(u8)(id>>24);packet[1]=(u8)(id>>16);packet[2]=(u8)(id>>8);packet[3]=(u8)id;
    packet[4]=tag;packet[5]=action;packet[6]=0;packet[7]=42;
    flags=event_flags;status=CMT2300A_STA_STBY;WirelessDemo_Process();
}
static void check_receive(void)
{
    u8 valid=CMT2300A_MASK_PKT_OK_FLG|CMT2300A_MASK_CRC_OK_FLG;
    reset(1);configure(1,0);
    inject(1,1,DEMO_TAG,valid);assert(g_demo.action==1 && g_demo.total==1 && status==CMT2300A_STA_RX);
    advance(800);inject(2,1,DEMO_TAG,valid);advance(49);assert(g_demo.action==1);
    advance(1);assert(!g_demo.action && strstr(output,"EVENT STOP TIMEOUT"));
    inject(1,1,DEMO_TAG,valid);advance(600);inject(1,1,DEMO_TAG,valid);advance(600);assert(g_demo.action);
    inject(1,0,DEMO_TAG,valid);assert(!g_demo.action);
    inject(1,1,DEMO_TAG,CMT2300A_MASK_PKT_OK_FLG);assert(!g_demo.action);
    inject(1,1,0xA0,valid);inject(1,2,DEMO_TAG,valid);assert(!g_demo.action);
    overflow=1;WirelessDemo_Process();assert(!overflow && status==CMT2300A_STA_RX);
    inject(1,1,DEMO_TAG,valid);rx_fail=1;status=CMT2300A_STA_STBY;WirelessDemo_Process();
    assert(!g_demo.ready && !g_demo.action);
    rx_fail=0;advance(10000);assert(g_demo.ready && status==CMT2300A_STA_RX);
    inject(1,1,DEMO_TAG,valid);command("DEFAULT");assert(!g_demo.action && !g_config.configured);
}
#endif
int main(void)
{
    check_configuration();check_power_loss();check_uart_and_faults();
#if WIRELESS_DEMO_TX
    check_button();
#else
    check_receive();
#endif
    puts(WIRELESS_DEMO_TX?"211 multi-device behavior tests PASS":"221 multi-device behavior tests PASS");return 0;
}
