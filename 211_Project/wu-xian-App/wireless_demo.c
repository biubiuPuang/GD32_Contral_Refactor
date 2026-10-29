#define WIRELESS_DEMO_TX 1

/* Project application over existing GD32 BSP and CMOSTEK radio drivers.
 * The other board owns an independent copy; only WIRELESS_DEMO_TX differs.
 */
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
#include <stdarg.h>
#include <string.h>

#define DEMO_PAYLOAD_SIZE 8U
#define DEMO_NODE_SIZE 4U
#define DEMO_DATA_SIZE 4U
#define DEMO_CHANNEL_COUNT 6U
#define DEMO_CHANNEL_STEP 60U /* 150 kHz, hardware step unit 2.5 kHz */
#define DEMO_DEBOUNCE_TICKS 100U /* 10 ms, both edges */
#define DEMO_RX_TIMEOUT 850U /* 85 ms from latest valid control packet */
#define DEMO_TX_TIMEOUT 700U
#define DEMO_RETRY_TICKS 10000U
#define DEMO_PRINT_TICKS 3000U
#define DEMO_TAG 0xA1U
#define DEMO_FLASH_FIRST 0x0800F800UL
#define DEMO_FLASH_PAGE 1024U
#define DEMO_FLASH_END 0x08010000UL
#define DEMO_RECORD_SIZE 32U
#define DEMO_MAGIC 0x57434632UL
#define DEMO_COMMIT 0x43464721UL
#define DEMO_UART_SIZE 128U
#define DEMO_UART_TX_SIZE 512U

typedef struct { u32 id, channel, configured; } Demo_Config;
static Demo_Config g_config;
static struct {
    u16 previous_timer, sequence;
    u32 ticks, report_at, retry_at, started_at, last_valid;
    u32 total, window, errors, next_at, random, button_since, log_drops;
    u32 saved_address, saved_generation;
    u8 pending[8], latest[8], line[64];
    u8 line_size, discard, button_raw, button_down, action, stops_left;
    BOOL ready, busy, have_latest, storage_ok;
    const char *error;
} g_demo;
static volatile u8 g_uart[DEMO_UART_SIZE];
static volatile u16 g_uart_head, g_uart_tail;
static volatile u8 g_uart_fault;
static volatile u8 g_uart_tx[DEMO_UART_TX_SIZE];
static volatile u16 g_uart_tx_head, g_uart_tx_tail;

/* Queue a complete line without waiting for serial transmission. In normal
 * use a log takes <12 ms on the wire; radio and debounce keep running during it.
 * A flooded serial output drops whole log lines rather than stalling control.
 */
static void Demo_Print(const char *format, ...)
{
    char text[192];
    va_list args;
    int length, i;
    u16 head = g_uart_tx_head;
    u16 free_bytes = (u16)((g_uart_tx_tail - head - 1U) & (DEMO_UART_TX_SIZE - 1U));
    va_start(args, format);
    length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if(length < 0 || length >= (int)sizeof(text) || (u16)length > free_bytes) {
        ++g_demo.log_drops;
        return;
    }
    for(i = 0; i < length; ++i) {
        g_uart_tx[head] = (u8)text[i];
        head = (u16)((head + 1U) & (DEMO_UART_TX_SIZE - 1U));
    }
    g_uart_tx_head = head;
    usart_interrupt_enable(BSP_USART, USART_INT_TBE);
}

#ifdef DEMO_HOST_TEST
static u32 Demo_ReadWord(u32 address);
static u32 Demo_ImageLimit(void);
#else
/* Include RW load data in the overlap guard. No configuration erase/program
 * is allowed if the linked firmware reaches the last two Flash pages.
 */
extern const unsigned char ImageLimit[] __asm("Load$$LR$$LR_IROM1$$Limit");
static u32 Demo_ImageLimit(void) { return (u32)ImageLimit; }
static u32 Demo_ReadWord(u32 address) { return *(volatile const u32 *)address; }
#endif

static u32 Demo_Now(void)
{
    u16 current = (u16)timer_counter_read(TIMER2);
    g_demo.ticks += (u16)(current - g_demo.previous_timer);
    g_demo.previous_timer = current;
    return g_demo.ticks;
}
static void Demo_TimerInit(void)
{
    timer_parameter_struct timer;
    u32 clock = rcu_clock_freq_get(CK_APB1);
    if(clock != rcu_clock_freq_get(CK_AHB)) clock *= 2U;
    rcu_periph_clock_enable(RCU_TIMER2);
    timer_deinit(TIMER2);
    timer_struct_para_init(&timer);
    timer.prescaler = (u16)(clock / 10000U - 1U);
    timer.alignedmode = TIMER_COUNTER_EDGE;
    timer.counterdirection = TIMER_COUNTER_UP;
    timer.period = 0xFFFFU;
    timer.clockdivision = TIMER_CKDIV_DIV1;
    timer.repetitioncounter = 0U;
    timer_init(TIMER2, &timer);
    timer_counter_value_config(TIMER2, 0U);
    timer_enable(TIMER2);
}

/* USART1 is initialized by existing BSP, including GPIO/peripheral clocks.
 * ISR only buffers input. Parsing and Flash writes run in main context.
 */
void USART1_IRQHandler(void)
{
    u16 next;
    u8 value;
    if(usart_flag_get(BSP_USART, USART_FLAG_ORERR) ||
       usart_flag_get(BSP_USART, USART_FLAG_FERR) ||
       usart_flag_get(BSP_USART, USART_FLAG_NERR)) {
        (void)usart_data_receive(BSP_USART);
        usart_flag_clear(BSP_USART, USART_FLAG_ORERR);
        usart_flag_clear(BSP_USART, USART_FLAG_FERR);
        usart_flag_clear(BSP_USART, USART_FLAG_NERR);
        g_uart_fault = 1U;
    }
    if(usart_flag_get(BSP_USART, USART_FLAG_RBNE)) {
        value = (u8)usart_data_receive(BSP_USART);
        next = (u16)((g_uart_head + 1U) & (DEMO_UART_SIZE - 1U));
        if(next == g_uart_tail) g_uart_fault = 1U;
        else if(!g_uart_fault) { g_uart[g_uart_head] = value; g_uart_head = next; }
    }
    if(usart_interrupt_flag_get(BSP_USART, USART_INT_FLAG_TBE)) {
        if(g_uart_tx_tail == g_uart_tx_head) usart_interrupt_disable(BSP_USART, USART_INT_TBE);
        else {
            usart_data_transmit(BSP_USART, g_uart_tx[g_uart_tx_tail]);
            g_uart_tx_tail = (u16)((g_uart_tx_tail + 1U) & (DEMO_UART_TX_SIZE - 1U));
        }
    }
}

static u32 Demo_Checksum(const u32 words[], u8 count)
{
    u32 crc = 0xFFFFFFFFUL, word;
    u8 i, j;
    for(i = 0U; i < count; ++i) {
        word = words[i];
        for(j = 0U; j < 32U; ++j) {
            u32 bit = (crc ^ word) & 1U;
            crc >>= 1;
            if(bit) crc ^= 0xEDB88320UL;
            word >>= 1;
        }
    }
    return ~crc;
}
static BOOL Demo_ReadRecord(u32 address, u32 words[8])
{
    u8 i;
    for(i = 0U; i < 8U; ++i) words[i] = Demo_ReadWord(address + 4U * i);
    return (BOOL)(words[0] == DEMO_MAGIC && words[1] == 1U &&
           words[7] == DEMO_COMMIT && words[6] == Demo_Checksum(words, 6U) &&
           words[3] != 0U && words[4] < DEMO_CHANNEL_COUNT && words[5] <= 1U);
}
static void Demo_ConfigLoad(void)
{
    u32 address, words[8];
    g_config.id = 1U; g_config.channel = 0U; g_config.configured = 0U;
    g_demo.storage_ok = (BOOL)(Demo_ImageLimit() <= DEMO_FLASH_FIRST);
    if(!g_demo.storage_ok) return;
    for(address = DEMO_FLASH_FIRST; address < DEMO_FLASH_END; address += DEMO_RECORD_SIZE) {
        if(Demo_ReadRecord(address, words) &&
           (!g_demo.saved_address || (int32_t)(words[2] - g_demo.saved_generation) > 0)) {
            g_demo.saved_address = address; g_demo.saved_generation = words[2];
            g_config.id = words[3]; g_config.channel = words[4]; g_config.configured = words[5];
        }
    }
}
/* Append-only two-page journal: CRC and last-written commit reject torn writes.
 * If one page fills, erase the other; the last valid record survives power loss.
 */
static BOOL Demo_ConfigSave(const Demo_Config *config)
{
    u32 address, start, words[8], verify[8], irq;
    u8 i;
    BOOL empty, ok = TRUE;
    if(!g_demo.storage_ok) return FALSE;
    if(g_demo.saved_address && memcmp(config, &g_config, sizeof(*config)) == 0) return TRUE;
    start = g_demo.saved_address ? (g_demo.saved_address & ~(DEMO_FLASH_PAGE - 1U)) : DEMO_FLASH_FIRST;
    for(address = start; address < start + DEMO_FLASH_PAGE; address += DEMO_RECORD_SIZE) {
        empty = TRUE;
        for(i = 0U; i < 8U; ++i)
            if(Demo_ReadWord(address + 4U * i) != 0xFFFFFFFFUL) empty = FALSE;
        if(empty) break;
    }
    words[0] = DEMO_MAGIC; words[1] = 1U; words[2] = g_demo.saved_generation + 1U;
    words[3] = config->id; words[4] = config->channel; words[5] = config->configured;
    words[6] = Demo_Checksum(words, 6U); words[7] = DEMO_COMMIT;
    irq = __get_PRIMASK(); __disable_irq();
    fmc_unlock();
    fmc_flag_clear(FMC_FLAG_END | FMC_FLAG_PGERR | FMC_FLAG_PGAERR | FMC_FLAG_WPERR);
    if(address == start + DEMO_FLASH_PAGE) {
        address = start == DEMO_FLASH_FIRST ? start + DEMO_FLASH_PAGE : DEMO_FLASH_FIRST;
        if(fmc_page_erase(address) != FMC_READY) ok = FALSE;
    }
    for(i = 0U; ok && i < 8U; ++i)
        if(fmc_word_program(address + 4U * i, words[i]) != FMC_READY ||
           Demo_ReadWord(address + 4U * i) != words[i]) ok = FALSE;
    fmc_lock();
    if(!irq) __enable_irq();
    if(ok && Demo_ReadRecord(address, verify)) {
        g_demo.saved_address = address; g_demo.saved_generation = words[2]; return TRUE;
    }
    return FALSE;
}

static void Demo_SetAction(u8 action, const char *reason)
{
    if(action != g_demo.action) {
        g_demo.action = action;
        Demo_Print("EVENT %s %s\r\n", action ? "RUN" : "STOP", reason);
    }
}
static BOOL Demo_Fail(const char *reason)
{
    Demo_SetAction(0U, "RADIO_FAULT");
    g_demo.error = reason; g_demo.ready = FALSE; g_demo.busy = FALSE;
    g_demo.retry_at = Demo_Now(); ++g_demo.errors;
    return FALSE;
}
static BOOL Demo_LoadBank(u8 address, u8 length)
{
    u8 i;
    if(!CMT2300A_ConfigRegBank(address, &g_demo_rf_registers[address], length))
        return Demo_Fail("CONFIG_WRITE");
    for(i = 0U; i < length; ++i)
        if(CMT2300A_ReadReg(address + i) != g_demo_rf_registers[address + i])
            return Demo_Fail("CONFIG_READBACK");
    return TRUE;
}
#if !WIRELESS_DEMO_TX
static BOOL Demo_StartRx(void)
{
    if(!CMT2300A_GoStby()) return Demo_Fail("RX_STBY");
    CMT2300A_EnableReadFifo(); CMT2300A_ClearRxFifo(); CMT2300A_ClearInterruptFlags();
    if(!CMT2300A_GoRx()) return Demo_Fail("GO_RX");
    return TRUE;
}
#endif
static BOOL Demo_RadioInit(void)
{
    u8 i, value;
#if WIRELESS_DEMO_TX
    if(!CMT2119B_Init())
#else
    if(!CMT2219B_Init())
#endif
        return Demo_Fail("SPI_OR_STBY");
    if(!Demo_LoadBank(0x00U, 12U) || !Demo_LoadBank(0x0CU, 12U) ||
       !Demo_LoadBank(0x18U, 8U) || !Demo_LoadBank(0x20U, 24U) ||
       !Demo_LoadBank(0x38U, 29U)) return FALSE;
#if WIRELESS_DEMO_TX
    if(!Demo_LoadBank(0x55U, 11U)) return FALSE;
#else
    if(!Demo_LoadBank(0x5FU, 1U)) return FALSE;
#endif
    for(i = 0U; i < 4U; ++i) {
        value = (u8)(g_config.id >> (8U * i));
        CMT2300A_WriteReg((u8)(0x48U + i), value);
        if(CMT2300A_ReadReg((u8)(0x48U + i)) != value) return Demo_Fail("ID_READBACK");
    }
    CMT2300A_SetFrequencyStep(DEMO_CHANNEL_STEP);
    CMT2300A_SetFrequencyChannel((u8)g_config.channel);
    if(CMT2300A_ReadReg(CMT2300A_CUS_FREQ_OFS) != DEMO_CHANNEL_STEP ||
       CMT2300A_ReadReg(CMT2300A_CUS_FREQ_CHNL) != g_config.channel)
        return Demo_Fail("CHANNEL_READBACK");
#if !WIRELESS_DEMO_TX
    /* AN1971: 26 MHz, TX/RX 40 ppm each, 433.920..434.670 MHz.
     * All six channels need AFC_OVF_TH=21 (0x15); the original .exp value
     * is 10. See validation/check_frequencies.py for the workbook formulas.
     */
    CMT2300A_WriteReg(CMT2300A_CUS_FSK4, 0x15U);
    if(CMT2300A_ReadReg(CMT2300A_CUS_FSK4) != 0x15U) return Demo_Fail("AFC_READBACK");
#endif
    if(!CMT2300A_GoSleep()) return Demo_Fail("CONFIG_SLEEP");
    if(!CMT2300A_GoStby()) return Demo_Fail("CONFIG_STBY");
    CMT2300A_EnableFifoMerge(FALSE);
#if WIRELESS_DEMO_TX
    CMT2119B_ConfigPolling(); CMT2300A_EnableWriteFifo(); CMT2300A_ClearTxFifo();
    g_demo.stops_left = 3U; g_demo.next_at = Demo_Now();
#else
    CMT2219B_ConfigPolling();
    CMT2300A_EnableInterrupt(CMT2300A_MASK_PKT_DONE_EN | CMT2300A_MASK_CRC_OK_EN);
    CMT2300A_EnableRxFifoAutoClear(TRUE);
    if(!Demo_StartRx()) return FALSE;
#endif
    g_demo.busy = FALSE; g_demo.ready = TRUE; g_demo.error = "OK";
    return TRUE;
}

#if WIRELESS_DEMO_TX
static u32 Demo_Random(void)
{
    u32 x = g_demo.random;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    g_demo.random = x; return x;
}
static void Demo_ButtonProcess(void)
{
    u8 down = (u8)(gpio_input_bit_get(GPIOA, GPIO_PIN_9) == RESET ||
                   gpio_input_bit_get(GPIOA, GPIO_PIN_10) == RESET);
    u32 now = Demo_Now();
    if(down != g_demo.button_raw) { g_demo.button_raw = down; g_demo.button_since = now; }
    if(down != g_demo.button_down && (u32)(now - g_demo.button_since) >= DEMO_DEBOUNCE_TICKS) {
        g_demo.button_down = down;
        Demo_Print("BUTTON %s\r\n", down ? "DOWN" : "UP");
        /* Release preempts a previous-state packet, rather than waiting for it. */
        if(g_demo.busy) {
            if(!CMT2300A_GoStby()) { (void)Demo_Fail("BUTTON_STBY"); return; }
            CMT2300A_ClearInterruptFlags(); g_demo.busy = FALSE;
        }
        g_demo.stops_left = down ? 0U : 3U; g_demo.next_at = now;
    }
}
static void Demo_TxProcess(void)
{
    u32 now = Demo_Now();
    if(g_demo.busy) {
        if(CMT2119B_TxDone()) {
            memcpy(g_demo.latest, g_demo.pending, 8U); g_demo.have_latest = TRUE;
            ++g_demo.total; ++g_demo.window; ++g_demo.sequence;
            if(!g_demo.pending[5] && g_demo.stops_left) --g_demo.stops_left;
            CMT2300A_ClearInterruptFlags(); g_demo.busy = FALSE;
        } else if((u32)(now - g_demo.started_at) >= DEMO_TX_TIMEOUT) {
            (void)CMT2300A_GoStby(); (void)Demo_Fail("TX_TIMEOUT");
        }
        return;
    }
    if((!g_demo.button_down && !g_demo.stops_left) || (int32_t)(now - g_demo.next_at) < 0) return;
    g_demo.pending[0] = (u8)(g_config.id >> 24); g_demo.pending[1] = (u8)(g_config.id >> 16);
    g_demo.pending[2] = (u8)(g_config.id >> 8); g_demo.pending[3] = (u8)g_config.id;
    g_demo.pending[4] = DEMO_TAG; g_demo.pending[5] = g_demo.button_down;
    g_demo.pending[6] = (u8)(g_demo.sequence >> 8); g_demo.pending[7] = (u8)g_demo.sequence;
    if(!CMT2300A_GoStby()) { (void)Demo_Fail("TX_STBY"); return; }
    CMT2300A_ClearInterruptFlags(); CMT2300A_EnableWriteFifo(); CMT2300A_ClearTxFifo();
    /* Hardware inserts Node ID. TX FIFO receives only four DATA bytes. */
    CMT2300A_WriteFifo(g_demo.pending + DEMO_NODE_SIZE, DEMO_DATA_SIZE);
    now = Demo_Now();
    if(!CMT2300A_GoTx()) { (void)Demo_Fail("GO_TX"); return; }
    g_demo.started_at = now;
    /* Start-to-start: RUN 40..60 ms; repeated STOP 25..35 ms. No ACK/LBT. */
    g_demo.next_at = now + (g_demo.button_down ? 400U : 250U) + Demo_Random() % (g_demo.button_down ? 201U : 101U);
    g_demo.busy = TRUE;
}
#else
static void Demo_RxProcess(void)
{
    u8 packet[8], accepted = 0U;
    u32 id;
    u8 flags = CMT2300A_ReadPollingFlags(CMT2300A_CUS_INT_FLAG);
    if(CMT2219B_FifoOverflow() || (flags & (CMT2300A_MASK_PKT_ERR_FLG | CMT2300A_MASK_COL_ERR_FLG))) {
        ++g_demo.errors; (void)Demo_StartRx();
    } else if(flags & CMT2300A_MASK_PKT_OK_FLG) {
        if(!CMT2300A_GoStby()) { (void)Demo_Fail("READ_STBY"); return; }
        if(flags & CMT2300A_MASK_CRC_OK_FLG) {
            CMT2300A_ReadFifo(packet, DEMO_PAYLOAD_SIZE);
            id = ((u32)packet[0] << 24) | ((u32)packet[1] << 16) | ((u32)packet[2] << 8) | packet[3];
            if(id == g_config.id && packet[4] == DEMO_TAG && packet[5] <= 1U) {
                memcpy(g_demo.latest, packet, 8U); g_demo.have_latest = TRUE;
                ++g_demo.total; ++g_demo.window; g_demo.last_valid = Demo_Now(); accepted = 1U;
            } else ++g_demo.errors;
        } else ++g_demo.errors;
        if(!Demo_StartRx()) return; /* Resume RX before printing an event. */
        if(accepted) Demo_SetAction(packet[5], "PACKET");
    } else if(CMT2300A_GetChipStatus() != CMT2300A_STA_RX) (void)Demo_StartRx();
    if(g_demo.action && (u32)(Demo_Now() - g_demo.last_valid) >= DEMO_RX_TIMEOUT)
        Demo_SetAction(0U, "TIMEOUT");
}
#endif

static void Demo_ShowConfig(void)
{
    u32 frequency = 433920U + 150U * g_config.channel;
    Demo_Print("CFG ID=%lu CH=%lu FREQ=%lu.%03luMHz PAIRED=%lu SAVE=%s RADIO=%s LOGDROP=%lu\r\n",
           (unsigned long)g_config.id, (unsigned long)g_config.channel,
           (unsigned long)(frequency / 1000U), (unsigned long)(frequency % 1000U),
           (unsigned long)g_config.configured, g_demo.storage_ok ? "OK" : "OVERLAP",
           g_demo.ready ? "OK" : g_demo.error, (unsigned long)g_demo.log_drops);
}
static BOOL Demo_Number(const u8 **text, u32 *value)
{
    u32 number = 0U, digit;
    const u8 *p = *text;
    if(*p < '0' || *p > '9') return FALSE;
    do {
        digit = (u32)(*p++ - '0');
        if(number > (0xFFFFFFFFUL - digit) / 10U) return FALSE;
        number = number * 10U + digit;
    } while(*p >= '0' && *p <= '9');
    *text = p; *value = number; return TRUE;
}
static void Demo_Command(const u8 *line)
{
    Demo_Config config;
    const u8 *p;
    if(strcmp((const char *)line, "GET") == 0) { Demo_ShowConfig(); return; }
    if(strcmp((const char *)line, "HELP") == 0) {
        Demo_Print("GET | SET,<ID:1..4294967295>,<CH:0..5> | DEFAULT | HELP\r\n"); return;
    }
    if(strcmp((const char *)line, "DEFAULT") == 0) {
        config.id = 1U; config.channel = 0U; config.configured = 0U;
    } else if(strncmp((const char *)line, "SET,", 4U) == 0) {
        p = line + 4U;
        if(!Demo_Number(&p, &config.id) || *p++ != ',' || !Demo_Number(&p, &config.channel) ||
           *p || !config.id || config.channel >= DEMO_CHANNEL_COUNT) {
            Demo_Print("ERR ARG use SET,<ID>,<CH>\r\n"); return;
        }
        config.configured = 1U;
    } else { Demo_Print("ERR COMMAND use HELP\r\n"); return; }
    Demo_SetAction(0U, "CONFIG");
    if((g_demo.ready || g_demo.busy) && !CMT2300A_GoStby()) {
        (void)Demo_Fail("CONFIG_STBY"); Demo_Print("ERR APPLY radio not stopped\r\n"); return;
    }
    g_demo.ready = FALSE; g_demo.busy = FALSE;
    if(!Demo_ConfigSave(&config)) {
        Demo_Print("ERR SAVE old configuration retained\r\n");
        if(g_config.configured && g_demo.storage_ok) (void)Demo_RadioInit();
        return;
    }
    g_config = config; g_demo.have_latest = FALSE; g_demo.window = 0U;
    /* Re-seed from the ID; XORing an existing ID-derived seed with that same
     * ID could cancel it and synchronize different devices after a repeated SET.
     */
    g_demo.random = (g_config.id * 0x9E3779B9UL) ^ Demo_Now() ^ 0x6D2B79F5UL;
    if(!g_demo.random) g_demo.random = 0x6D2B79F5UL;
    g_demo.error = "UNPAIRED";
    if(g_config.configured && !Demo_RadioInit()) Demo_Print("ERR APPLY saved; radio retry pending\r\n");
    else Demo_Print("OK SAVED APPLIED\r\n");
    Demo_ShowConfig();
}
static void Demo_UartProcess(void)
{
    u8 value, budget = 64U;
    u32 irq;
    if(g_uart_fault) {
        irq = __get_PRIMASK(); __disable_irq();
        g_uart_tail = g_uart_head; g_uart_fault = 0U;
        if(!irq) __enable_irq();
        g_demo.line_size = 0U; g_demo.discard = 1U;
        Demo_Print("ERR UART resend whole line after newline\r\n");
    }
    while(budget-- && g_uart_tail != g_uart_head) {
        value = g_uart[g_uart_tail];
        g_uart_tail = (u16)((g_uart_tail + 1U) & (DEMO_UART_SIZE - 1U));
        if(value == '\r' || value == '\n') {
            if(g_demo.discard) { g_demo.discard = 0U; g_demo.line_size = 0U; }
            else if(g_demo.line_size) {
                g_demo.line[g_demo.line_size] = 0U; Demo_Command(g_demo.line);
                g_demo.line_size = 0U; return;
            }
        } else if(!g_demo.discard) {
            if(value < 0x20U || value > 0x7EU || g_demo.line_size >= sizeof(g_demo.line) - 1U) {
                g_demo.discard = 1U; g_demo.line_size = 0U;
                Demo_Print("ERR LINE resend whole line\r\n");
            } else g_demo.line[g_demo.line_size++] = value;
        }
    }
}
static void Demo_Report(void)
{
#if WIRELESS_DEMO_TX
    const char *side = "TX";
    u8 action = g_demo.button_down;
#else
    const char *side = "RX";
    u8 action = g_demo.action;
#endif
    if(g_demo.window && g_demo.have_latest) {
        Demo_Print("%s seq=%u cmd=%s state=%s data=%02X %02X %02X %02X %02X %02X %02X %02X total=%lu new=%lu err=%lu\r\n",
               side, (unsigned int)(((u16)g_demo.latest[6] << 8) | g_demo.latest[7]),
               g_demo.latest[5] ? "RUN" : "STOP", action ? "RUN" : "STOP",
               (unsigned int)g_demo.latest[0], (unsigned int)g_demo.latest[1],
               (unsigned int)g_demo.latest[2], (unsigned int)g_demo.latest[3],
               (unsigned int)g_demo.latest[4], (unsigned int)g_demo.latest[5],
               (unsigned int)g_demo.latest[6], (unsigned int)g_demo.latest[7],
               (unsigned long)g_demo.total, (unsigned long)g_demo.window, (unsigned long)g_demo.errors);
    } else Demo_Print("%s IDLE state=%s total=%lu err=%lu\r\n", side,
                  g_demo.ready ? (action ? "RUN" : "STOP") : g_demo.error,
                  (unsigned long)g_demo.total, (unsigned long)g_demo.errors);
    g_demo.window = 0U;
}
void WirelessDemo_Init(void)
{
    memset(&g_demo, 0, sizeof(g_demo)); g_uart_head = g_uart_tail = 0U; g_uart_fault = 0U;
    g_uart_tx_head = g_uart_tx_tail = 0U;
    g_demo.sequence = 1U;
    systick_config(); usart_init(115200U); Demo_TimerInit();
#if WIRELESS_DEMO_TX
    rcu_periph_clock_enable(RCU_GPIOA);
    gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO_PIN_9 | GPIO_PIN_10);
#endif
    Demo_ConfigLoad();
    g_demo.random = (g_config.id * 0x9E3779B9UL) ^ Demo_Now() ^ 0x6D2B79F5UL;
    if(!g_demo.random) g_demo.random = 1U;
    g_demo.error = g_demo.storage_ok ? "UNPAIRED" : "FLASH_OVERLAP";
    usart_interrupt_enable(BSP_USART, USART_INT_RBNE); nvic_irq_enable(USART1_IRQn, 1U);
    Demo_Print("\r\nWIRELESS V2 %s UART=115200 8N1\r\n", WIRELESS_DEMO_TX ? "TX211" : "RX221");
    if(g_config.configured && g_demo.storage_ok) (void)Demo_RadioInit();
    Demo_ShowConfig(); g_demo.report_at = Demo_Now();
}
void WirelessDemo_Process(void)
{
    u32 now;
    Demo_UartProcess();
#if WIRELESS_DEMO_TX
    Demo_ButtonProcess();
#endif
    now = Demo_Now();
    if(g_config.configured && g_demo.storage_ok) {
        if(!g_demo.ready) {
            if((u32)(now - g_demo.retry_at) >= DEMO_RETRY_TICKS) (void)Demo_RadioInit();
        } else {
#if WIRELESS_DEMO_TX
            Demo_TxProcess();
#else
            Demo_RxProcess();
#endif
        }
    }
    now = Demo_Now();
    if((u32)(now - g_demo.report_at) >= DEMO_PRINT_TICKS) { g_demo.report_at = now; Demo_Report(); }
}
