/* Host tests for application state transitions; no hardware is accessed.
 * The real application and real driver declarations are used, with mocked APIs.
 */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static char output[65536];
static size_t output_length;
static int capture_printf(const char *format, ...)
{
    va_list args;
    int count;
    va_start(args, format);
    count = vsnprintf(output + output_length, sizeof(output) - output_length, format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(output) - output_length);
    output_length += (size_t)count;
    return count;
}

#define printf capture_printf
#include APP_SOURCE
#undef printf

static uint16_t counter;
static uint8_t registers[128], packet[8], flags, status;
static unsigned bank_bytes, fifo_reads, fifo_writes, rx_starts, init_calls;
static int timer_clock_enabled, systick_ready, available, corrupt_readback;
static int rx_fail, overflow, done, fifo_selected, fifo_cleared;

uint32_t rcu_clock_freq_get(uint32_t clock) { return clock == CK_APB1 ? 36000000U : 72000000U; }
void rcu_periph_clock_enable(uint32_t clock) { assert(clock == RCU_TIMER2); timer_clock_enabled = 1; }
void timer_deinit(uint32_t timer) { assert(timer == TIMER2 && timer_clock_enabled); }
void timer_struct_para_init(timer_parameter_struct *p) { memset(p, 0, sizeof(*p)); }
void timer_init(uint32_t timer, timer_parameter_struct *p)
{
    assert(timer == TIMER2 && timer_clock_enabled);
    assert(p->prescaler == 7199U && p->period == 65535U);
}
void timer_counter_value_config(uint32_t timer, uint16_t value) { assert(timer == TIMER2); counter = value; }
void timer_enable(uint32_t timer) { assert(timer == TIMER2 && timer_clock_enabled); }
uint32_t timer_counter_read(uint32_t timer) { assert(timer == TIMER2); return counter; }
void systick_config(void) { systick_ready = 1; }
void usart_init(uint32_t baud) { assert(baud == 115200U && systick_ready); }

BOOL CMT2119B_Init(void) { assert(systick_ready); ++init_calls; return (BOOL)available; }
BOOL CMT2219B_Init(void) { assert(systick_ready); ++init_calls; return (BOOL)available; }
void CMT2119B_ConfigPolling(void) { flags = 0U; }
void CMT2219B_ConfigPolling(void) { flags = 0U; }
BOOL CMT2119B_TxDone(void) { return (BOOL)done; }
BOOL CMT2219B_FifoOverflow(void) { return (BOOL)overflow; }
BOOL CMT2300A_ConfigRegBank(u8 address, const u8 bank[], u8 length)
{
#if !WIRELESS_DEMO_TX
    assert(address != 0x55U); /* No Tx bank writes to the receive-only chip. */
#endif
    memcpy(registers + address, bank, length);
    bank_bytes += length;
    return TRUE;
}
u8 CMT2300A_ReadReg(u8 address)
{
    if(corrupt_readback && address == 0x18U)
        return (u8)(registers[address] ^ 1U);
    return registers[address];
}
void CMT2300A_SetFrequencyChannel(u8 channel) { registers[CMT2300A_CUS_FREQ_CHNL] = channel; }
BOOL CMT2300A_GoSleep(void) { status = CMT2300A_STA_SLEEP; return TRUE; }
BOOL CMT2300A_GoStby(void) { status = CMT2300A_STA_STBY; return TRUE; }
BOOL CMT2300A_GoTx(void) { status = CMT2300A_STA_TX; return TRUE; }
BOOL CMT2300A_GoRx(void)
{
    assert(fifo_selected && fifo_cleared);
    ++rx_starts;
    status = CMT2300A_STA_RX;
    return rx_fail ? FALSE : TRUE;
}
void CMT2300A_EnableFifoMerge(BOOL enable) { assert(!enable); }
void CMT2300A_EnableRxFifoAutoClear(BOOL enable) { assert(enable); }
void CMT2300A_EnableInterrupt(u8 enable)
{
    assert(enable == (CMT2300A_MASK_CRC_OK_EN | CMT2300A_MASK_PKT_DONE_EN));
}
void CMT2300A_EnableWriteFifo(void) { fifo_selected = 1; fifo_cleared = 0; }
void CMT2300A_EnableReadFifo(void) { fifo_selected = 1; }
u8 CMT2300A_ClearTxFifo(void) { assert(fifo_selected); fifo_cleared = 1; return 0U; }
u8 CMT2300A_ClearRxFifo(void) { assert(fifo_selected); fifo_cleared = 1; overflow = 0; return 0U; }
u8 CMT2300A_ClearInterruptFlags(void) { flags = 0U; done = 0; return 0U; }
void CMT2300A_WriteFifo(const u8 bytes[], u16 length)
{
    /* Actual board behavior: mode 1 emits the configured Node ID first, then
     * consumes only the four DATA bytes from TX FIFO for an 8-byte payload.
     * The previous mock omitted this packet-handler behavior and missed the bug.
     */
    assert(fifo_selected && fifo_cleared && length == 4U);
    packet[0] = registers[0x4BU];
    packet[1] = registers[0x4AU];
    packet[2] = registers[0x49U];
    packet[3] = registers[0x48U];
    memcpy(packet + 4U, bytes, length);
    ++fifo_writes;
}
void CMT2300A_ReadFifo(u8 bytes[], u16 length)
{
    assert(status == CMT2300A_STA_STBY && length == 8U);
    assert(flags & CMT2300A_MASK_CRC_OK_FLG);
    memcpy(bytes, packet, length);
    ++fifo_reads;
}
u8 CMT2300A_ReadPollingFlags(u8 address) { assert(address == CMT2300A_CUS_INT_FLAG); return flags; }
u8 CMT2300A_GetChipStatus(void) { return status; }

static void reset(void)
{
    memset(registers, 0, sizeof(registers));
    memset(packet, 0, sizeof(packet));
    output_length = 0U;
    output[0] = '\0';
    counter = 0U;
    bank_bytes = fifo_reads = fifo_writes = rx_starts = init_calls = 0U;
    timer_clock_enabled = systick_ready = corrupt_readback = 0;
    rx_fail = overflow = done = fifo_selected = fifo_cleared = 0;
    available = 1;
    flags = 0U;
    status = CMT2300A_STA_STBY;
}

static void check_initialization(void)
{
    reset();
    WirelessDemo_Init();
    assert(g_demo.ready && registers[0x46U] == 7U && registers[0x47U] == 0x0DU);
    assert(registers[CMT2300A_CUS_FREQ_CHNL] == 0U);
    assert(bank_bytes == (WIRELESS_DEMO_TX ? 96U : 86U));
    reset();
    corrupt_readback = 1;
    WirelessDemo_Init();
    assert(!g_demo.ready && strcmp(g_demo.error, "CONFIG_READBACK") == 0);
    assert(fifo_writes == 0U && rx_starts == 0U);

    reset();
    available = 0;
    WirelessDemo_Init();
    assert(!g_demo.ready && init_calls == 1U);
    counter = 2999U;
    WirelessDemo_Process();
    assert(!strstr(output, "NO_NEW_DATA"));
    counter = 3000U;
    WirelessDemo_Process();
    assert(strstr(output, "NO_NEW_DATA") && strstr(output, "SPI_OR_STBY"));
    available = 1;
    counter = 10000U;
    WirelessDemo_Process();
    assert(g_demo.ready && init_calls == 2U);
}

static void check_timer_wrap(void)
{
    reset();
    WirelessDemo_Init();
    counter = 65530U;
    assert(Demo_Now() == 65530U);
    counter = 20U;
    assert(Demo_Now() == 65556U);
    g_demo.ticks = UINT32_MAX - 15U;
    counter = 52U;
    assert(Demo_Now() == 16U);
}

#if WIRELESS_DEMO_TX
static void check_radio(void)
{
    reset();
    WirelessDemo_Init();
    WirelessDemo_Process();
    assert(g_demo.busy && fifo_writes == 1U && g_demo.total == 0U);
    assert(memcmp(packet, "\0\0\0\1\0\0\0\1", 8U) == 0);
    counter = 180U;
    done = 1;
    status = CMT2300A_STA_STBY;
    WirelessDemo_Process();
    assert(g_demo.total == 1U && g_demo.sequence == 2U && !g_demo.busy);
    counter = 3000U;
    WirelessDemo_Process();
    assert(strstr(output, "TX seq=1 data=00 00 00 01 00 00 00 01"));
    assert(g_demo.busy && packet[7] == 2U);
    counter = 4001U;
    WirelessDemo_Process();
    assert(!g_demo.ready && g_demo.total == 1U && strcmp(g_demo.error, "TX_TIMEOUT") == 0);
    counter = 14001U;
    WirelessDemo_Process();
    assert(g_demo.ready && init_calls == 2U);
    WirelessDemo_Process();
    assert(g_demo.busy && packet[7] == 2U); /* Retry same unconfirmed sequence. */
}
#else
static void inject(u8 event_flags, u8 first_node_byte)
{
    memcpy(packet, "\0\0\0\1\0\0\0\52", 8U);
    packet[0] = first_node_byte;
    flags = event_flags;
    status = CMT2300A_STA_STBY;
    WirelessDemo_Process();
}
static void check_radio(void)
{
    reset();
    WirelessDemo_Init();
    inject(CMT2300A_MASK_PKT_OK_FLG, 0U);
    assert(g_demo.total == 0U && fifo_reads == 0U && status == CMT2300A_STA_RX);
    inject(CMT2300A_MASK_PKT_OK_FLG | CMT2300A_MASK_CRC_OK_FLG, 0U);
    assert(g_demo.total == 1U && fifo_reads == 1U && status == CMT2300A_STA_RX);
    counter = 3000U;
    WirelessDemo_Process();
    assert(strstr(output, "RX seq=42 data=00 00 00 01 00 00 00 2A"));
    counter = 6000U;
    WirelessDemo_Process();
    assert(strstr(output, "RX NO_NEW_DATA total=1"));
    inject(CMT2300A_MASK_PKT_OK_FLG | CMT2300A_MASK_CRC_OK_FLG, 9U);
    assert(g_demo.total == 1U && fifo_reads == 2U);
    inject(CMT2300A_MASK_PKT_ERR_FLG | CMT2300A_MASK_PKT_OK_FLG | CMT2300A_MASK_CRC_OK_FLG, 0U);
    assert(g_demo.total == 1U && fifo_reads == 2U && status == CMT2300A_STA_RX);
    overflow = 1;
    WirelessDemo_Process();
    assert(!overflow && status == CMT2300A_STA_RX);
    status = CMT2300A_STA_STBY;
    rx_fail = 1;
    WirelessDemo_Process();
    assert(!g_demo.ready && strcmp(g_demo.error, "GO_RX") == 0);
    rx_fail = 0;
    counter = 16000U;
    WirelessDemo_Process();
    assert(g_demo.ready && status == CMT2300A_STA_RX);
}
#endif

int main(void)
{
    check_initialization();
    check_timer_wrap();
    check_radio();
    puts(WIRELESS_DEMO_TX ? "211 application simulation PASS" : "221 application simulation PASS");
    return 0;
}
