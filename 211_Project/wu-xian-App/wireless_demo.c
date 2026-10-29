/* Application demo over the existing GD32 BSP and CMOSTEK driver.
 * Reference: official CMT2300A STM32 Tx/Rx examples and AN143.
 * RF bytes are exported for the actual CMT2119B / CMT2219B, never CMT2300A.
 */
#define WIRELESS_DEMO_TX 1

#include "wireless_demo.h"
#include "gd32e23x.h"
#include "systick.h"
#include "usart.h"
#if WIRELESS_DEMO_TX
#include "cmt2119b.h"
#else
#include "cmt2219b.h"
#endif
#include "wireless_demo_params.h"
#include <stdio.h>
#include <string.h>

#define DEMO_PAYLOAD_SIZE     8U
#define DEMO_NODE_SIZE        4U
#define DEMO_DATA_SIZE        (DEMO_PAYLOAD_SIZE - DEMO_NODE_SIZE)
#define DEMO_TICK_HZ          10000U
#define DEMO_PRINT_TICKS      3000U   /* 300 ms */
#define DEMO_TX_TIMEOUT       1000U   /* 100 ms; 22 on-air bytes take ~18.3 ms */
#define DEMO_RETRY_TICKS      10000U  /* 1 s */
#define DEMO_CHANNEL          0U

static const u8 g_node[4] = {0x00U, 0x00U, 0x00U, 0x01U};
static struct {
    u16 previous_timer;
    u32 ticks;
    u32 report_at;
    u32 retry_at;
    u32 started_at;
    u32 sequence;
    u32 total;
    u32 window;
    u32 errors;
    u8 pending[DEMO_PAYLOAD_SIZE];
    u8 latest[DEMO_PAYLOAD_SIZE];
    BOOL ready;
    BOOL busy;
    BOOL have_latest;
    const char *error;
} g_demo;

/* Project application helper: free-running TIMER2, independent of BSP SysTick. */
static void Demo_TimerInit(void)
{
    timer_parameter_struct timer;
    u32 timer_clock = rcu_clock_freq_get(CK_APB1);
    if(timer_clock != rcu_clock_freq_get(CK_AHB))
        timer_clock *= 2U;

    rcu_periph_clock_enable(RCU_TIMER2);
    timer_deinit(TIMER2);
    timer_struct_para_init(&timer);
    timer.prescaler = (u16)(timer_clock / DEMO_TICK_HZ - 1U);
    timer.alignedmode = TIMER_COUNTER_EDGE;
    timer.counterdirection = TIMER_COUNTER_UP;
    timer.period = 0xFFFFU;
    timer.clockdivision = TIMER_CKDIV_DIV1;
    timer.repetitioncounter = 0U;
    timer_init(TIMER2, &timer);
    timer_counter_value_config(TIMER2, 0U);
    timer_enable(TIMER2);
}

/* Call at least once every 6.55 s; all demo operations are bounded below that. */
static u32 Demo_Now(void)
{
    u16 current = (u16)timer_counter_read(TIMER2);
    g_demo.ticks += (u16)(current - g_demo.previous_timer);
    g_demo.previous_timer = current;
    return g_demo.ticks;
}

static BOOL Demo_Fail(const char *reason)
{
    g_demo.error = reason;
    g_demo.ready = FALSE;
    g_demo.busy = FALSE;
    g_demo.retry_at = Demo_Now();
    ++g_demo.errors;
    return FALSE;
}

/* ConfigRegBank returns TRUE without verification; check actual SPI readback. */
static BOOL Demo_LoadBank(u8 address, u8 size)
{
    u8 i;
    if(!CMT2300A_ConfigRegBank(address, &g_demo_rf_registers[address], size))
        return Demo_Fail("CONFIG_WRITE");
    for(i = 0U; i < size; ++i) {
        if(CMT2300A_ReadReg((u8)(address + i)) != g_demo_rf_registers[address + i])
            return Demo_Fail("CONFIG_READBACK");
    }
    return TRUE;
}

#if !WIRELESS_DEMO_TX
/* Stop before clearing flags/FIFO. Re-enter RX before printing any report. */
static BOOL Demo_StartRx(void)
{
    if(!CMT2300A_GoStby())
        return Demo_Fail("RX_STBY");
    CMT2300A_EnableReadFifo();
    CMT2300A_ClearRxFifo();
    CMT2300A_ClearInterruptFlags();
    if(!CMT2300A_GoRx())
        return Demo_Fail("GO_RX");
    return TRUE;
}
#endif

static BOOL Demo_RadioInit(void)
{
#if WIRELESS_DEMO_TX
    if(!CMT2119B_Init())
#else
    if(!CMT2219B_Init())
#endif
        return Demo_Fail("SPI_OR_STBY");

    /* Keep the exact RFPDK banks and fixed-length encoding (8 bytes -> 7).
     * The vendor SetPayloadLength helper takes the encoded value, not 8.
     */
    if(!Demo_LoadBank(0x00U, 12U) || !Demo_LoadBank(0x0CU, 12U) ||
       !Demo_LoadBank(0x18U, 8U) || !Demo_LoadBank(0x20U, 24U) ||
       !Demo_LoadBank(0x38U, 29U))
        return FALSE;
#if WIRELESS_DEMO_TX
    if(!Demo_LoadBank(0x55U, 11U))
        return FALSE;
#else
    /* CMT2219B 0x55..0x5E is reserved: do not copy the Tx demo's TX bank write. */
    if(!Demo_LoadBank(0x5FU, 1U))
        return FALSE;
#endif

    CMT2300A_SetFrequencyChannel(DEMO_CHANNEL);
    if(CMT2300A_ReadReg(CMT2300A_CUS_FREQ_CHNL) != DEMO_CHANNEL)
        return Demo_Fail("CHANNEL_READBACK");
    if(!CMT2300A_GoSleep())
        return Demo_Fail("CONFIG_SLEEP");
    if(!CMT2300A_GoStby())
        return Demo_Fail("CONFIG_STBY");

    CMT2300A_EnableFifoMerge(FALSE);
#if WIRELESS_DEMO_TX
    CMT2119B_ConfigPolling();
    CMT2300A_EnableWriteFifo();
    CMT2300A_ClearTxFifo();
#else
    CMT2219B_ConfigPolling();
    /* PKT_OK alone does not establish CRC validity. Enable and require CRC_OK. */
    CMT2300A_EnableInterrupt(CMT2300A_MASK_PKT_DONE_EN | CMT2300A_MASK_CRC_OK_EN);
    CMT2300A_EnableRxFifoAutoClear(TRUE);
    if(!Demo_StartRx())
        return FALSE;
#endif
    g_demo.busy = FALSE;
    g_demo.ready = TRUE;
    g_demo.error = "OK";
    return TRUE;
}

#if WIRELESS_DEMO_TX
static void Demo_TxProcess(void)
{
    u32 now;
    if(g_demo.busy) {
        if(CMT2119B_TxDone()) {
            memcpy(g_demo.latest, g_demo.pending, DEMO_PAYLOAD_SIZE);
            g_demo.have_latest = TRUE;
            ++g_demo.total;
            ++g_demo.window;
            ++g_demo.sequence;
            CMT2300A_ClearInterruptFlags();
            g_demo.busy = FALSE;
        } else if((u32)(Demo_Now() - g_demo.started_at) >= DEMO_TX_TIMEOUT) {
            /* Stop the RF operation; full reinitialization is retried after 1 s. */
            (void)CMT2300A_GoStby();
            (void)Demo_Fail("TX_TIMEOUT");
        }
        return;
    }

    memcpy(g_demo.pending, g_node, sizeof(g_node));
    g_demo.pending[4] = (u8)(g_demo.sequence >> 24);
    g_demo.pending[5] = (u8)(g_demo.sequence >> 16);
    g_demo.pending[6] = (u8)(g_demo.sequence >> 8);
    g_demo.pending[7] = (u8)g_demo.sequence;

    if(!CMT2300A_GoStby()) {
        (void)Demo_Fail("TX_STBY");
        return;
    }
    CMT2300A_ClearInterruptFlags();
    /* Required order: select FIFO access first, then clear the selected FIFO. */
    CMT2300A_EnableWriteFifo();
    CMT2300A_ClearTxFifo();
    /* Node mode 1 inserts NODE_VALUE in hardware. TX FIFO contains DATA only;
     * pending retains ID + DATA solely to print the complete on-air payload.
     * Writing pending[0..7] here would transmit ID + ID instead of ID + sequence.
     */
    CMT2300A_WriteFifo(&g_demo.pending[DEMO_NODE_SIZE], DEMO_DATA_SIZE);
    now = Demo_Now();
    if(!CMT2300A_GoTx()) {
        (void)CMT2300A_GoStby();
        (void)Demo_Fail("GO_TX");
        return;
    }
    g_demo.started_at = now;
    g_demo.busy = TRUE;
}
#else
static void Demo_RxProcess(void)
{
    u8 packet[DEMO_PAYLOAD_SIZE];
    u8 flags = CMT2300A_ReadPollingFlags(CMT2300A_CUS_INT_FLAG);
    if(CMT2219B_FifoOverflow() ||
       (flags & (CMT2300A_MASK_PKT_ERR_FLG | CMT2300A_MASK_COL_ERR_FLG))) {
        ++g_demo.errors;
        (void)Demo_StartRx();
        return;
    }
    if(flags & CMT2300A_MASK_PKT_OK_FLG) {
        if(!CMT2300A_GoStby()) {
            (void)Demo_Fail("READ_STBY");
            return;
        }
        if(flags & CMT2300A_MASK_CRC_OK_FLG) {
            /* RX FIFO includes Node ID + DATA (verified on the actual boards).
             * No length byte, preamble, sync or CRC bytes are read here.
             */
            CMT2300A_ReadFifo(packet, DEMO_PAYLOAD_SIZE);
            if(memcmp(packet, g_node, sizeof(g_node)) == 0) {
                memcpy(g_demo.latest, packet, DEMO_PAYLOAD_SIZE);
                g_demo.have_latest = TRUE;
                ++g_demo.total;
                ++g_demo.window;
            } else {
                ++g_demo.errors;
            }
        } else {
            ++g_demo.errors;
        }
        (void)Demo_StartRx();
    } else if(CMT2300A_GetChipStatus() != CMT2300A_STA_RX) {
        /* Auto-exit or an unexpected state must not leave the receiver stopped. */
        (void)Demo_StartRx();
    }
}
#endif

static void Demo_Report(void)
{
#if WIRELESS_DEMO_TX
    const char *side = "TX";
#else
    const char *side = "RX";
#endif
    u32 sequence;
    u8 i;
    if(g_demo.window != 0U && g_demo.have_latest) {
        sequence = ((u32)g_demo.latest[4] << 24) | ((u32)g_demo.latest[5] << 16) |
                   ((u32)g_demo.latest[6] << 8) | (u32)g_demo.latest[7];
        printf("%s seq=%lu data=", side, (unsigned long)sequence);
        for(i = 0U; i < DEMO_PAYLOAD_SIZE; ++i)
            printf("%02X%s", (unsigned int)g_demo.latest[i],
                   (i + 1U == DEMO_PAYLOAD_SIZE) ? "" : " ");
        printf(" total=%lu new=%lu err=%lu%s\r\n", (unsigned long)g_demo.total,
               (unsigned long)g_demo.window, (unsigned long)g_demo.errors,
               g_demo.ready ? "" : " OFFLINE");
    } else {
        printf("%s NO_NEW_DATA total=%lu err=%lu state=%s\r\n", side,
               (unsigned long)g_demo.total, (unsigned long)g_demo.errors,
               g_demo.ready ? "OK" : g_demo.error);
    }
    g_demo.window = 0U;
}

void WirelessDemo_Init(void)
{
    memset(&g_demo, 0, sizeof(g_demo));
    g_demo.sequence = 1U;
    g_demo.error = "NOT_INITIALIZED";
    systick_config();
    usart_init(115200U);  /* Existing BSP enables USART and GPIO clocks itself. */
    Demo_TimerInit();
#if WIRELESS_DEMO_TX
    printf("\r\nCMT2119B TX DEMO 433.920MHz CH=0 NODE=00000001 9.6kbps\r\n");
#else
    printf("\r\nCMT2219B RX DEMO 433.920MHz CH=0 NODE=00000001 9.6kbps\r\n");
#endif
    (void)Demo_RadioInit();
    printf("INIT %s\r\n", g_demo.ready ? "OK" : g_demo.error);
    g_demo.report_at = Demo_Now();
}

void WirelessDemo_Process(void)
{
    u32 now = Demo_Now();
    if(!g_demo.ready) {
        if((u32)(now - g_demo.retry_at) >= DEMO_RETRY_TICKS)
            (void)Demo_RadioInit();
    } else {
#if WIRELESS_DEMO_TX
        Demo_TxProcess();
#else
        Demo_RxProcess();
#endif
    }
    now = Demo_Now();
    if((u32)(now - g_demo.report_at) >= DEMO_PRINT_TICKS) {
        g_demo.report_at = now;
        Demo_Report();
    }
}
