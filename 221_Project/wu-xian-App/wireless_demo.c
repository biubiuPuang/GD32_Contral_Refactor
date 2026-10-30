#define WIRELESS_DEMO_TX 0

/* RX-only A2 protocol service. Execution GPIOs belong to wired_port.c.
 * ReceiverApp connects the service to the shared wired execution state machine.
 */
#include "wireless_demo.h"
#include "gd32e23x.h"
#include "systick.h"
#include "usart.h"
#include "cmt2219b.h"
#include "wireless_demo_params.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define DEMO_PAYLOAD_SIZE 8U
#define DEMO_CHANNEL_COUNT 6U
#define DEMO_CHANNEL_STEP 60U /* 150 kHz, hardware step unit 2.5 kHz */
#define DEMO_RX_TIMEOUT 850U /* 85 ms from latest valid control packet */
#define DEMO_RETRY_TICKS 10000U
#define DEMO_PRINT_TICKS 10000U /* 1000ms, TIMER2=10kHz */
#define DEMO_TAG WIRELESS_CONTROL_TAG
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
    u16 previous_timer;
    u32 ticks, report_at, retry_at, last_valid;
    u32 total, window, errors, log_drops;
    u32 saved_address, saved_generation;
    u8 latest[8], line[64];
    u8 line_size, discard, action;
    BOOL ready, have_latest, storage_ok;
    const char *error;
} g_demo;
static volatile u8 g_uart[DEMO_UART_SIZE];
static volatile u16 g_uart_head, g_uart_tail;
static volatile u8 g_uart_fault;
static volatile u8 g_uart_tx[DEMO_UART_TX_SIZE];
static volatile u16 g_uart_tx_head, g_uart_tx_tail;
static u8 g_retry_allowed;
static const char *(*g_status_provider)(void);
static const char *(*g_error_provider)(void);
static void (*g_maintenance_hook)(uint8_t active);

static BOOL Demo_ValidCommand(u8 command)
{
    return (BOOL)(command == WIRELESS_CMD_STOP || command == WIRELESS_CMD_UP ||
                  command == WIRELESS_CMD_DOWN || command == WIRELESS_CMD_UP_FAST ||
                  command == WIRELESS_CMD_DOWN_FAST || command == WIRELESS_CMD_ESTOP ||
                  command == WIRELESS_CMD_FAST || command == WIRELESS_CMD_CONFLICT);
}

static u8 Demo_Queue(const char *text, u16 length)
{
    u16 i, head = g_uart_tx_head;
    u16 free_bytes = (u16)((g_uart_tx_tail - head - 1U) & (DEMO_UART_TX_SIZE - 1U));
    if(length > free_bytes) { ++g_demo.log_drops; return 0U; }
    for(i = 0U; i < length; ++i) {
        g_uart_tx[head] = (u8)text[i];
        head = (u16)((head + 1U) & (DEMO_UART_TX_SIZE - 1U));
    }
    g_uart_tx_head = head;
    usart_interrupt_enable(BSP_USART, USART_INT_TBE);
    return 1U;
}

uint8_t WirelessDemo_Log(const char *line)
{
    size_t length = strlen(line);
    if(length >= DEMO_UART_TX_SIZE) { ++g_demo.log_drops; return 0U; }
    return Demo_Queue(line, (u16)length);
}

/* Queue a complete line without waiting for serial transmission. In normal
 * use a log takes <12 ms on the wire; radio and debounce keep running during it.
 * A flooded serial output drops whole log lines rather than stalling control.
 */
static void Demo_Print(const char *format, ...)
{
    char text[192];
    va_list args;
    int length;
    va_start(args, format);
    length = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    if(length < 0 || length >= (int)sizeof(text)) {
        ++g_demo.log_drops;
        return;
    }
    (void)Demo_Queue(text, (u16)length);
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
    (void)reason; /* State edges are silent; 1000ms snapshot is the only auto log. */
    g_demo.action = action;
}
static BOOL Demo_Fail(const char *reason)
{
    Demo_SetAction(0U, "RADIO_FAULT");
    g_demo.error = reason; g_demo.ready = FALSE;
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
static BOOL Demo_StartRx(void)
{
    if(!CMT2300A_GoStby()) return Demo_Fail("RX_STBY");
    CMT2300A_EnableReadFifo(); CMT2300A_ClearRxFifo(); CMT2300A_ClearInterruptFlags();
    if(!CMT2300A_GoRx()) return Demo_Fail("GO_RX");
    return TRUE;
}
static BOOL Demo_RadioInit(void)
{
    u8 i, value;
    if(!CMT2219B_Init())
        return Demo_Fail("SPI_OR_STBY");
    if(!Demo_LoadBank(0x00U, 12U) || !Demo_LoadBank(0x0CU, 12U) ||
       !Demo_LoadBank(0x18U, 8U) || !Demo_LoadBank(0x20U, 24U) ||
       !Demo_LoadBank(0x38U, 29U)) return FALSE;
    if(!Demo_LoadBank(0x5FU, 1U)) return FALSE;
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
    /* AN1971: 26 MHz, TX/RX 40 ppm each, 433.920..434.670 MHz.
     * All six channels need AFC_OVF_TH=21 (0x15); the original .exp value
     * is 10. See validation/check_frequencies.py for the workbook formulas.
     */
    CMT2300A_WriteReg(CMT2300A_CUS_FSK4, 0x15U);
    if(CMT2300A_ReadReg(CMT2300A_CUS_FSK4) != 0x15U) return Demo_Fail("AFC_READBACK");
    if(!CMT2300A_GoSleep()) return Demo_Fail("CONFIG_SLEEP");
    if(!CMT2300A_GoStby()) return Demo_Fail("CONFIG_STBY");
    CMT2300A_EnableFifoMerge(FALSE);
    CMT2219B_ConfigPolling();
    CMT2300A_EnableInterrupt(CMT2300A_MASK_PKT_DONE_EN | CMT2300A_MASK_CRC_OK_EN);
    CMT2300A_EnableRxFifoAutoClear(TRUE);
    if(!Demo_StartRx()) return FALSE;
    g_demo.ready = TRUE; g_demo.error = "OK";
    return TRUE;
}

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
            if(id == g_config.id && packet[4] == DEMO_TAG && Demo_ValidCommand(packet[5])) {
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

static void Demo_ShowConfig(void)
{
    u32 frequency = 433920U + 150U * g_config.channel;
    Demo_Print("CFG ID=%lu CH=%lu FREQ=%lu.%03luMHz PAIRED=%lu SAVE=%s RADIO=%s LOGDROP=%lu PROTO=A2 ESTOP=08 STATUS=03/04\r\n",
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
    if(g_maintenance_hook) g_maintenance_hook(1U);
    Demo_SetAction(0U, "CONFIG");
    if(g_demo.ready && !CMT2300A_GoStby()) {
        (void)Demo_Fail("CONFIG_STBY"); Demo_Print("ERR APPLY radio not stopped\r\n");
        if(g_maintenance_hook) g_maintenance_hook(0U);
        return;
    }
    g_demo.ready = FALSE;
    if(!Demo_ConfigSave(&config)) {
        Demo_Print("ERR SAVE old configuration retained\r\n");
        if(g_config.configured && g_demo.storage_ok) (void)Demo_RadioInit();
        if(g_maintenance_hook) g_maintenance_hook(0U);
        return;
    }
    g_config = config; g_demo.have_latest = FALSE; g_demo.window = 0U;
    g_demo.error = "UNPAIRED";
    if(g_config.configured && !Demo_RadioInit()) Demo_Print("ERR APPLY saved; radio retry pending\r\n");
    else Demo_Print("OK SAVED APPLIED\r\n");
    Demo_ShowConfig();
    if(g_maintenance_hook) g_maintenance_hook(0U);
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
    const char *line = g_status_provider ? g_status_provider() : NULL;
    const char *error = g_error_provider ? g_error_provider() : NULL;
    u8 connected = (u8)(g_demo.ready && g_config.configured && g_demo.have_latest &&
                         (u32)(Demo_Now() - g_demo.last_valid) < DEMO_RX_TIMEOUT && !error);
    if(line) Demo_Print("%s", line);
    /* Known limitation, intentionally deferred: idle TX sends no heartbeat.
     * This is recent valid-packet status, NOT proof of a bidirectional link.
     * Idle and powered-off transmitters both become NOT CONNECT after 85ms. */
    Demo_Print("%s", connected ? "wu-xian connect ing\r\n" : "wu-xian not connect\r\n");
    if(!g_demo.ready) Demo_Print("ERR RADIO %s\r\n", g_demo.error);
    if(error) Demo_Print("ERR EXEC %s\r\n", error);
    g_demo.window = 0U;
}
void WirelessDemo_Init(void)
{
    memset(&g_demo, 0, sizeof(g_demo)); g_uart_head = g_uart_tail = 0U; g_uart_fault = 0U;
    g_uart_tx_head = g_uart_tx_tail = 0U;
    g_maintenance_hook = NULL; g_retry_allowed = 1U;
    g_status_provider = NULL; g_error_provider = NULL;
    systick_config(); usart_init(115200U); Demo_TimerInit();
    Demo_ConfigLoad();
    g_demo.error = g_demo.storage_ok ? "UNPAIRED" : "FLASH_OVERLAP";
    usart_interrupt_enable(BSP_USART, USART_INT_RBNE); nvic_irq_enable(USART1_IRQn, 1U);
    Demo_Print("\r\nRECEIVER V3 RX221 PROTO=A2 UART=115200 8N1 ESTOP=08 STATUS=03/04\r\n");
    if(g_config.configured && g_demo.storage_ok) (void)Demo_RadioInit();
    Demo_ShowConfig();
    Demo_Print("INIT %s\r\n", g_demo.ready ? "OK" : g_demo.error);
    g_demo.report_at = Demo_Now();
}
void WirelessDemo_Process(void)
{
    u32 now;
    Demo_UartProcess();
    now = Demo_Now();
    if(g_config.configured && g_demo.storage_ok) {
        if(!g_demo.ready) {
            if(g_retry_allowed && (u32)(now - g_demo.retry_at) >= DEMO_RETRY_TICKS) (void)Demo_RadioInit();
        } else {
            Demo_RxProcess();
        }
    }
    /* The integrated coordinator reports AFTER syncing the fresh command and
     * applying execution outputs, so the 1000ms snapshot is not one loop old. */
}

void WirelessDemo_ReportProcess(void)
{
    u32 now = Demo_Now();
    if((u32)(now - g_demo.report_at) >= DEMO_PRINT_TICKS) {
        g_demo.report_at = now;
        Demo_Report();
    }
}

uint8_t WirelessDemo_GetCommand(void)
{
    if(!g_demo.ready || !g_config.configured || !g_demo.storage_ok) return WIRELESS_CMD_STOP;
    if(g_demo.action && (u32)(Demo_Now() - g_demo.last_valid) >= DEMO_RX_TIMEOUT)
        Demo_SetAction(WIRELESS_CMD_STOP, "TIMEOUT");
    return g_demo.action;
}

void WirelessDemo_SetRetryAllowed(uint8_t allowed) { g_retry_allowed = (u8)(allowed != 0U); }
void WirelessDemo_SetMaintenanceHook(void (*hook)(uint8_t active)) { g_maintenance_hook = hook; }

void WirelessDemo_SetStatusProvider(const char *(*status)(void), const char *(*error)(void))
{
    g_status_provider = status;
    g_error_provider = error;
}
