#define WIRELESS_DEMO_TX 1

/* TX application over existing GD32 BSP and CMOSTEK radio drivers.
 * Three two-stage keys plus PA8 E-stop, protocol A2. Both current firmware
 * images are required for wireless E-stop; old receivers reject command 08.
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
#include "wireless_frequency_plan.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#define DEMO_PAYLOAD_SIZE 8U
#define DEMO_NODE_SIZE 4U
#define DEMO_DATA_SIZE 4U
#define DEMO_DEBOUNCE_TICKS 100U /* 10 ms, both edges */
#define DEMO_RX_TIMEOUT 850U /* 85 ms from latest valid control packet */
#define DEMO_TX_TIMEOUT 700U
#define DEMO_RETRY_TICKS 10000U
#define DEMO_PRINT_TICKS 10000U /* 1000ms, TIMER2=10kHz */
#define DEMO_LED_TX_TICKS 5000U /* 500ms翻转一次 */
#define DEMO_LED_ALERT_TICKS 2000U /* 200ms翻转一次 */
/*--------------低功耗开始-----------------*/
// 计算方法：等待秒数 × 10000。
#define DEMO_IDLE_TICKS 1800000UL /* 180秒，计时频率10kHz */
#define DEMO_UART_DRAIN_TICKS 1000U /* 最多等待串口100ms */
#define DEMO_WATCHDOG_FEED_TICKS 100U /* 正常运行时每10ms喂狗 */
/*--------------低功耗结束-----------------*/
#if WIRELESS_DEMO_TX
#define DEMO_TAG WIRELESS_CONTROL_TAG
#else
#define DEMO_TAG 0xA1U /* Legacy, inactive receiver branch. */
#endif
#define DEMO_FLASH_FIRST 0x0800F800UL
#define DEMO_FLASH_PAGE 1024U
#define DEMO_FLASH_END 0x08010000UL
#define DEMO_RECORD_SIZE 32U
#define DEMO_MAGIC 0x57434632UL
#define DEMO_COMMIT 0x43464721UL
#define DEMO_UART_SIZE 128U
#define DEMO_UART_TX_SIZE 512U

typedef struct { u32 id, channel, configured; } Demo_Config;
typedef enum {
    DEMO_LED_IDLE, DEMO_LED_TX, DEMO_LED_ALERT, DEMO_LED_SLEEP
} Demo_LedMode;
static Demo_Config g_config;
static struct {
    u16 previous_timer, sequence;
    u32 ticks, report_at, retry_at, started_at, last_valid;
    u32 total, window, errors, next_at, random, button_since[3], conflict_since, log_drops;
    u32 estop_since;
    u32 led_at;
    Demo_LedMode led_mode;
    u32 saved_address, saved_generation;
    u8 pending[8], latest[8], line[64];
    u8 line_size, discard, button_raw, button_bits, button_command, button_conflict, action, stops_left;
    u8 estop_raw, estop_pressed, estop_ready;
    u8 restart_wait, restart_idle_tracking;
    u32 restart_idle_at;
    BOOL ready, busy, have_latest, storage_ok;
    const char *error;
} g_demo;
static volatile u8 g_uart[DEMO_UART_SIZE];
static volatile u16 g_uart_head, g_uart_tail;
static volatile u8 g_uart_fault;
static volatile u8 g_uart_tx[DEMO_UART_TX_SIZE];
static volatile u16 g_uart_tx_head, g_uart_tx_tail;

/*------低功耗开始------*/
/* RTC是否产生过闹钟中断 */
static volatile u8 g_rtc_wakeup = 0U;
static volatile u8 g_key_wakeup = 0U;
/* RTC是否初始化成功，后续作为允许休眠的条件 */
static BOOL g_rtc_ready = FALSE;
static u32 g_idle_since = 0U;       /* 开始空闲时的时间 */
static BOOL g_idle_ready = FALSE;  /* 是否已经空闲满180秒 */
static BOOL g_idle_tracking = FALSE; /* 从所有按键消抖确认松开后开始计时 */
static BOOL g_radio_initialized = FALSE;
static u32 g_watchdog_at = 0U;
/*------低功耗结束------*/

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

/* 项目自定义函数：PB9低电平点亮，复用官方GPIO库和现有10kHz时基。
 * 使用按键命令保持发送灯效，不跟随每个无线包的busy状态闪烁。
 */
void Demo_LedProcess(void)
{
    Demo_LedMode mode;
    u32 interval, now = Demo_Now();
    if(!g_config.configured || !g_demo.storage_ok || !g_demo.ready ||
       g_demo.estop_pressed || g_demo.button_conflict ||
       g_demo.button_command == WIRELESS_CMD_CONFLICT) {
        mode = DEMO_LED_ALERT;
        interval = DEMO_LED_ALERT_TICKS;
    } else if(g_demo.button_command != WIRELESS_CMD_STOP) {
        mode = DEMO_LED_TX;
        interval = DEMO_LED_TX_TICKS;
    } else {
        /* 无按键时常亮，松键后的三个STOP尾包不延长发送灯效。 */
        gpio_bit_reset(GPIOB, GPIO_PIN_9);
        g_demo.led_mode = DEMO_LED_IDLE;
        g_demo.led_at = now;
        return;
    }
    /* 进入发送/异常状态立即翻转，之后按各自周期非阻塞翻转。 */
    if(g_demo.led_mode != mode || (u32)(now - g_demo.led_at) >= interval) {
        gpio_bit_toggle(GPIOB, GPIO_PIN_9);
        g_demo.led_at = now;
    }
    g_demo.led_mode = mode;
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
    (void)reason;
    g_demo.action = action;
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
    const u8 *bank = &g_demo_rf_registers[address];
    if(address == CMT2300A_CUS_RF1 && length == 8U)
        bank = g_demo_frequency_registers;
    if(!CMT2300A_ConfigRegBank(address, bank, length))
        return Demo_Fail("CONFIG_WRITE");
    for(i = 0U; i < length; ++i)
        if(CMT2300A_ReadReg(address + i) != bank[i])
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
    g_radio_initialized = TRUE;
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
    /* AN1971: 26 MHz, TX/RX 40 ppm each, all 50 F50 channels.
     * Keep the worksheet-derived AFC value in the application profile.
     */
    CMT2300A_WriteReg(CMT2300A_CUS_FSK4, DEMO_AFC_OVF_TH);
    if(CMT2300A_ReadReg(CMT2300A_CUS_FSK4) != DEMO_AFC_OVF_TH) return Demo_Fail("AFC_READBACK");
#endif
    if(!CMT2300A_GoSleep()) return Demo_Fail("CONFIG_SLEEP");
    if(!CMT2300A_GoStby()) return Demo_Fail("CONFIG_STBY");
    CMT2300A_EnableFifoMerge(FALSE);
#if WIRELESS_DEMO_TX
    CMT2119B_ConfigPolling(); CMT2300A_EnableWriteFifo(); CMT2300A_ClearTxFifo();
    g_demo.stops_left = g_demo.button_command ? 0U : 3U; g_demo.next_at = Demo_Now();
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
void Demo_ButtonProcess(void)
{
    u8 raw = 0U, i, command, key_activity;
    u16 pa = gpio_input_port_get(GPIOA), pb = gpio_input_port_get(GPIOB);
    u32 now = Demo_Now();
    u8 estop = (u8)((pa & GPIO_PIN_8) == 0U);
    u32 irq = __get_PRIMASK();
    /* 即使按键很快松开，中断留下的记录也会重新开始空闲计时。 */
    __disable_irq();
    key_activity = g_key_wakeup;
    g_key_wakeup = 0U;
    if(!irq) __enable_irq();
    /* Independent, nonblocking 10-ms confirmation on both edges. Do not send
     * a startup STOP before the current E-stop input has been qualified. */
    if(estop != g_demo.estop_raw) {
        g_demo.estop_raw = estop;
        g_demo.estop_since = now;
    }
    if((u32)(now - g_demo.estop_since) >= DEMO_DEBOUNCE_TICKS) {
        g_demo.estop_pressed = estop;
        g_demo.estop_ready = 1U;
    }
    if((pb & (GPIO_PIN_3 | GPIO_PIN_4)) != (GPIO_PIN_3 | GPIO_PIN_4)) raw |= WIRELESS_CMD_UP;
    if((pa & (GPIO_PIN_9 | GPIO_PIN_10)) != (GPIO_PIN_9 | GPIO_PIN_10)) raw |= WIRELESS_CMD_DOWN;
    if((pb & (GPIO_PIN_5 | GPIO_PIN_6)) != (GPIO_PIN_5 | GPIO_PIN_6)) raw |= WIRELESS_CMD_FAST;

    /* Independent debounce of each complete key: stage 1 <-> stage 2 keeps
     * it held; both contacts high for 10 ms is the final release. */
    for(i = 0U; i < 3U; ++i) {
        u8 bit = (u8)(1U << i);
        if((raw ^ g_demo.button_raw) & bit) g_demo.button_since[i] = now;
        if(((raw ^ g_demo.button_bits) & bit) &&
           (u32)(now - g_demo.button_since[i]) >= DEMO_DEBOUNCE_TICKS) {
            if(raw & bit) g_demo.button_bits |= bit;
            else g_demo.button_bits &= (u8)~bit;
        }
    }
    g_demo.button_raw = raw;
    
    /*-------------------低功耗开始-----------------------*/
    /* 任意按键按住，或者按键状态尚未确认，都不能算空闲 */
    if(key_activity != 0U || raw != 0U || estop != 0U ||
    g_demo.button_bits != 0U || g_demo.estop_pressed != 0U ||
    !g_demo.estop_ready) {
        g_idle_since = now;
        g_idle_ready = FALSE;
        g_idle_tracking = FALSE;
    } else if(!g_idle_tracking) {
        g_idle_since = now;
        g_idle_ready = FALSE;
        g_idle_tracking = TRUE;
    } else if(!g_idle_ready &&
            (u32)(now - g_idle_since) >= DEMO_IDLE_TICKS) {
        g_idle_ready = TRUE;
    }
    /*-------------------低功耗结束-----------------------*/

    /* Raw direction conflict stops immediately; require 10 ms without it
     * before resuming, so a bouncing opposite key cannot resume old motion. */
    if((raw & (WIRELESS_CMD_UP | WIRELESS_CMD_DOWN)) == (WIRELESS_CMD_UP | WIRELESS_CMD_DOWN)) {
        g_demo.button_conflict = 1U; g_demo.conflict_since = now;
    } else if(g_demo.button_conflict && (u32)(now - g_demo.conflict_since) >= DEMO_DEBOUNCE_TICKS) {
        g_demo.button_conflict = 0U;
    }

    command = (u8)(g_demo.button_bits & (WIRELESS_CMD_UP | WIRELESS_CMD_DOWN));
    if(g_demo.button_conflict || command == (WIRELESS_CMD_UP | WIRELESS_CMD_DOWN))
        command = WIRELESS_CMD_CONFLICT;
    else if(g_demo.button_bits & WIRELESS_CMD_FAST) command |= WIRELESS_CMD_FAST;
    if(g_demo.restart_wait) {
        if(raw || g_demo.button_bits) {
            g_demo.restart_idle_tracking = 0U;
        } else if(!g_demo.restart_idle_tracking) {
            g_demo.restart_idle_tracking = 1U;
            g_demo.restart_idle_at = now;
        } else if((u32)(now - g_demo.restart_idle_at) >= DEMO_DEBOUNCE_TICKS) {
            g_demo.restart_wait = 0U;
            Demo_Print("RESTART KEYS RELEASED\r\n");
        }
        command = WIRELESS_CMD_STOP;
    }
    /* Emergency stop remains available even while waiting for key release. */
    if(g_demo.estop_pressed) command = WIRELESS_CMD_ESTOP;

    if(command != g_demo.button_command) {
        g_demo.button_command = command;
        /* Any command change preempts an old direction/speed packet. */
        if(g_demo.busy) {
            if(!CMT2300A_GoStby()) { (void)Demo_Fail("BUTTON_STBY"); return; }
            CMT2300A_ClearInterruptFlags(); g_demo.busy = FALSE;
        }
        g_demo.stops_left = command ? 0U : 3U; g_demo.next_at = now;
    }
}
void Demo_TxProcess(void)
{
    u32 now = Demo_Now();
    if(!g_demo.estop_ready) return;
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
    if((!g_demo.button_command && !g_demo.stops_left) || (int32_t)(now - g_demo.next_at) < 0) return;
    g_demo.pending[0] = (u8)(g_config.id >> 24); g_demo.pending[1] = (u8)(g_config.id >> 16);
    g_demo.pending[2] = (u8)(g_config.id >> 8); g_demo.pending[3] = (u8)g_config.id;
    g_demo.pending[4] = DEMO_TAG; g_demo.pending[5] = g_demo.button_command;
    g_demo.pending[6] = (u8)(g_demo.sequence >> 8); g_demo.pending[7] = (u8)g_demo.sequence;
    if(!CMT2300A_GoStby()) { (void)Demo_Fail("TX_STBY"); return; }
    CMT2300A_ClearInterruptFlags(); CMT2300A_EnableWriteFifo(); CMT2300A_ClearTxFifo();
    /* Hardware inserts Node ID. TX FIFO receives only four DATA bytes. */
    CMT2300A_WriteFifo(g_demo.pending + DEMO_NODE_SIZE, DEMO_DATA_SIZE);
    now = Demo_Now();
    if(!CMT2300A_GoTx()) { (void)Demo_Fail("GO_TX"); return; }
    g_demo.started_at = now;
    /* Start-to-start: motion/held E-stop 40..60 ms; STOP 25..35 ms x3.
     * E-stop must keep renewing RX validity, even with no direction held. */
    g_demo.next_at = now + (g_demo.button_command ? 400U : 250U) + Demo_Random() % (g_demo.button_command ? 201U : 101U);
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
    u32 frequency = DEMO_BASE_FREQUENCY_HZ + DEMO_CHANNEL_SPACING_HZ * g_config.channel;
    Demo_Print("CFG ID=%lu CH=%lu FREQ=%lu.%04luMHz PAIRED=%lu SAVE=%s RADIO=%s LOGDROP=%lu PROTO=%02X ESTOP=08 STATUS=03/04\r\n",
           (unsigned long)g_config.id, (unsigned long)g_config.channel,
           (unsigned long)(frequency / 1000000U), (unsigned long)((frequency % 1000000U) / 100U),
           (unsigned long)g_config.configured, g_demo.storage_ok ? "OK" : "OVERLAP",
           g_demo.ready ? "OK" : g_demo.error, (unsigned long)g_demo.log_drops,
           (unsigned)DEMO_TAG);
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
        Demo_Print("GET | SET,<ID:1..4294967295>,<CH:0..49> | DEFAULT | HELP\r\n"); return;
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
void Demo_UartProcess(void)
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
/* Snapshot only: never log key edges or individual packets. Idle has no
 * heartbeat by user policy; reporting must not change RF scheduling. */
static void Demo_Report(void)
{
    const char *line;
    switch(g_demo.button_command) {
    case WIRELESS_CMD_UP: line = "up\r\n"; break;
    case WIRELESS_CMD_DOWN: line = "dowm\r\n"; break;
    case WIRELESS_CMD_UP_FAST: line = "fast up\r\n"; break;
    case WIRELESS_CMD_DOWN_FAST: line = "fast dowm\r\n"; break;
    case WIRELESS_CMD_FAST: line = "wu-xian fast\r\n"; break;
    case WIRELESS_CMD_CONFLICT: line = "error model\r\n"; break;
    case WIRELESS_CMD_ESTOP: line = "stop model\r\n"; break;
    default: line = "no key press model\r\n"; break;
    }
    Demo_Print("%s", line);
    if(!g_demo.ready) {
        Demo_Print("ERR RADIO %s\r\n", g_demo.error);
    }
    g_demo.window = 0U;
}

/*--------------------------------低功耗开始-----------------------------*/
/* 项目自定义函数：恢复深度睡眠前的内部晶振72MHz配置。 */
BOOL Demo_ClockResume(void)
{
    u32 timeout = 100000U;
    if(RCU_SCSS_PLL != rcu_system_clock_source_get()) {
        rcu_osci_on(RCU_IRC8M);
        if(SUCCESS != rcu_osci_stab_wait(RCU_IRC8M)) return FALSE;
        fmc_wscnt_set(WS_WSCNT_2);
        rcu_pll_config(RCU_PLLSRC_IRC8M_DIV2, RCU_PLL_MUL18);
        rcu_osci_on(RCU_PLL_CK);
        if(SUCCESS != rcu_osci_stab_wait(RCU_PLL_CK)) return FALSE;
        rcu_system_clock_source_config(RCU_CKSYSSRC_PLL);
        while(RCU_SCSS_PLL != rcu_system_clock_source_get()) {
            if(--timeout == 0U) return FALSE;
        }
    }
    SystemCoreClockUpdate();
    systick_config();
    return TRUE;
}

static BOOL Demo_AnyKeyPressed(void)
{
    const u16 pa_keys = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10;
    const u16 pb_keys = GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6;
    return (BOOL)((gpio_input_port_get(GPIOA) & pa_keys) != pa_keys ||
                  (gpio_input_port_get(GPIOB) & pb_keys) != pb_keys);
}

/* 主循环完成一轮后调用，限制喂狗频率，满足连续reload的最小间隔。 */
void Demo_WatchdogFeed(void)
{
    u32 now = Demo_Now();
    if((u32)(now - g_watchdog_at) >= DEMO_WATCHDOG_FEED_TICKS) {
        fwdgt_counter_reload();
        g_watchdog_at = now;
    }
}

static BOOL Demo_UartDrain(void)
{
    u32 started = Demo_Now();
    while(g_uart_tx_head != g_uart_tx_tail ||
          RESET == usart_flag_get(BSP_USART, USART_FLAG_TC)) {
        if(g_key_wakeup || Demo_AnyKeyPressed()) return FALSE;
        if((u32)(Demo_Now() - started) >= DEMO_UART_DRAIN_TICKS) return FALSE;
    }
    return TRUE;
}

/* RTC唤醒后仍为8MHz，先更新BSP延时参数；喂狗前后均留出同步时间。
 * 参照Rev2.6用户手册174页：连续reload及reload到深睡均需等待低速时钟同步。
 */
static void Demo_SleepFeed(void)
{
    SystemCoreClockUpdate();
    systick_config();
    delay_1ms(1U);
    fwdgt_counter_reload();
    delay_1ms(1U);
}

void Demo_LowPowerProcess(void)
{
    BOOL radio_asleep = FALSE, timer_stopped = FALSE;
    u32 now;

    /* 只在主循环、全部按键松开、停止包发完、串口命令处理完后进入。 */
    if(__get_PRIMASK() || !g_rtc_ready || !g_idle_ready ||
       !g_demo.estop_ready || g_demo.button_command ||
       g_demo.busy || g_demo.stops_left || g_key_wakeup || Demo_AnyKeyPressed() ||
       g_uart_head != g_uart_tail || g_uart_fault || g_demo.line_size || g_demo.discard)
        return;
    /* 无线故障时保留原有重试流程；未配对设备也允许在空闲后休眠。 */
    if(g_config.configured && !g_demo.ready) return;
    if(g_uart_tx_head != g_uart_tx_tail ||
       RESET == usart_flag_get(BSP_USART, USART_FLAG_TC)) return;

    if(g_radio_initialized) {
        if(!CMT2300A_GoSleep()) { (void)Demo_Fail("IDLE_SLEEP"); return; }
        radio_asleep = TRUE;
    }

    /* 队列已空，可保证整条进入日志入队。等待最后一个停止位发完。 */
    Demo_Print("input sleep model\r\n");
    if(!Demo_UartDrain()) goto resume;

    __disable_irq();
    /* 处理打印期间到来的按键和串口命令，避免漏掉短按或截断配置命令。 */
    if(g_key_wakeup || Demo_AnyKeyPressed() || g_uart_head != g_uart_tail ||
       g_uart_fault || usart_flag_get(BSP_USART, USART_FLAG_RBNE)) {
        __enable_irq();
        goto resume;
    }
    usart_interrupt_disable(BSP_USART, USART_INT_RBNE);
    usart_interrupt_disable(BSP_USART, USART_INT_TBE);
    usart_disable(BSP_USART);
    NVIC_ClearPendingIRQ(USART1_IRQn);
    /* 深睡前灭灯；RTC仅喂狗，不改变PB9电平。 */
    gpio_bit_set(GPIOB, GPIO_PIN_9);
    g_demo.led_mode = DEMO_LED_SLEEP;
    (void)Demo_Now();
    timer_disable(TIMER2);
    timer_stopped = TRUE;
    g_rtc_wakeup = 0U;
    __enable_irq();

    Demo_SleepFeed();
    for(;;) {
        __disable_irq();
        if(g_key_wakeup || Demo_AnyKeyPressed()) {
            __enable_irq();
            break;
        }
        if(g_rtc_wakeup) {
            g_rtc_wakeup = 0U;
            __enable_irq();
            Demo_SleepFeed();
            continue;
        }
        /* 关中断检查到WFI之间不留空隙。待处理中断仍可唤醒WFI，
         * 开中断后运行ISR，因此短按不会丢失。复用GD32官方深睡接口。
         */
        __DSB();
        pmu_to_deepsleepmode(PMU_LDO_LOWPOWER, WFI_CMD);
        __enable_irq();
        __ISB();
    }

resume:
    if(timer_stopped) {
        /* 时钟恢复失败时复位恢复，不能以错误波特率/时基继续控制。 */
        if(!Demo_ClockResume()) { NVIC_SystemReset(); return; }
        g_demo.previous_timer = (u16)timer_counter_read(TIMER2);
        timer_enable(TIMER2);
        usart_enable(BSP_USART);
        usart_interrupt_enable(BSP_USART, USART_INT_RBNE);
    }
    if(radio_asleep && !CMT2300A_GoStby()) (void)Demo_Fail("WAKE_STBY");
    Demo_LedProcess();
    now = Demo_Now();
    g_idle_since = now;
    g_idle_ready = FALSE;
    g_idle_tracking = FALSE;
    g_demo.report_at = now;
    g_watchdog_at = now;
    Demo_Print("exit sleep model\r\n");
}

static BOOL Demo_RtcInit(void)
{
    rtc_parameter_struct rtc = {0};
    rtc_alarm_struct alarm = {0};

    /* 1. 开启PMU时钟，允许写入备份域 */
    rcu_periph_clock_enable(RCU_PMU);
    pmu_backup_write_enable();

    /* 2. 复位备份域，重新建立RTC配置 */
    rcu_bkp_reset_enable();
    rcu_bkp_reset_disable();

    /* 3. 开启内部40kHz时钟，并等待稳定 */
    rcu_osci_on(RCU_IRC40K);
    if(SUCCESS != rcu_osci_stab_wait(RCU_IRC40K)) {
        return FALSE;
    }

    /* 4. 选择RTC时钟源，并开启RTC时钟 */
    rcu_rtc_clock_config(RCU_RTCSRC_IRC40K);
    rcu_periph_clock_enable(RCU_RTC);

    if(SUCCESS != rtc_register_sync_wait()) {
        return FALSE;
    }

    /* 5. 分频得到约1Hz，设置初始日历 */
    rtc.rtc_factor_asyn = 124U;
    rtc.rtc_factor_syn = 319U;
    rtc.rtc_year = 0x00U;
    rtc.rtc_month = RTC_JAN;
    rtc.rtc_date = 0x01U;
    rtc.rtc_day_of_week = RTC_SATURDAY;
    rtc.rtc_display_format = RTC_24HOUR;
    rtc.rtc_am_pm = RTC_AM;

    if(SUCCESS != rtc_init(&rtc)) {
        return FALSE;
    }

    /* 6. 配置闹钟前，先关闭闹钟并等待允许写入 */
    if(SUCCESS != rtc_alarm_disable()) {
        return FALSE;
    }

    /* 7. 屏蔽日期、时、分、秒的比较，每秒产生闹钟 */
    alarm.rtc_alarm_mask = RTC_ALARM_ALL_MASK;
    alarm.rtc_weekday_or_date = RTC_ALARM_DATE_SELECTED;
    alarm.rtc_alarm_day = 0x01U;
    alarm.rtc_am_pm = RTC_AM;
    rtc_alarm_config(&alarm);
    rtc_alarm_subsecond_config(RTC_MASKSSC_0_14, 0U);

    /* 8. RTC闹钟通过EXTI17传递，使用上升沿 */
    rtc_flag_clear(RTC_FLAG_ALARM0);
    exti_init(EXTI_17, EXTI_INTERRUPT, EXTI_TRIG_RISING);
    exti_interrupt_flag_clear(EXTI_17);
    g_rtc_wakeup = 0U;

    /* 9. 开启RTC闹钟中断及闹钟 */
    rtc_interrupt_enable(RTC_INT_ALARM);
    nvic_irq_enable(RTC_IRQn, 2U);
    rtc_alarm_enable();

    return TRUE;
}
/*--------------------------------低功耗结束-----------------------------*/

void WirelessDemo_Init(void) 
{
    u8 restart_cause = 0U;
    if(rcu_flag_get(RCU_FLAG_FWDGTRST) != RESET) restart_cause |= 1U;
    if(rcu_flag_get(RCU_FLAG_WWDGTRST) != RESET) restart_cause |= 2U;
    if(rcu_flag_get(RCU_FLAG_SWRST) != RESET) restart_cause |= 4U;
    rcu_all_reset_flag_clear();
    memset(&g_demo, 0, sizeof(g_demo)); g_uart_head = g_uart_tail = 0U; g_uart_fault = 0U;
    g_demo.restart_wait = (u8)(restart_cause != 0U);
    g_uart_tx_head = g_uart_tx_tail = 0U;
    g_demo.sequence = 1U;
    g_key_wakeup = 0U;
    g_rtc_wakeup = 0U;
    g_rtc_ready = FALSE;
    g_radio_initialized = FALSE;
    systick_config(); usart_init(115200U); Demo_TimerInit();
#if WIRELESS_DEMO_TX
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    /* 先开启GPIOB时钟、预置高电平灭灯，再配置PB9推挽输出。 */
    gpio_bit_set(GPIOB, GPIO_PIN_9);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, GPIO_PIN_9);
    gpio_mode_set(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, GPIO_PIN_9);
    gpio_mode_set(GPIOA, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10);
    gpio_mode_set(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6);
    g_demo.estop_raw = (u8)(gpio_input_bit_get(GPIOA, GPIO_PIN_8) == RESET);

    /* ===== 低功耗相关代码-引脚初始化-新增开始 ===== */
    /* 先开启SYSCFG时钟 */
    rcu_periph_clock_enable(RCU_CFGCMP);
    /* 上升键：PB3、PB4对应EXTI3、EXTI4 */
    syscfg_exti_line_config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN3);
    syscfg_exti_line_config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN4);
    /* 下降键：PA9、PA10对应EXTI9、EXTI10 */
    syscfg_exti_line_config(EXTI_SOURCE_GPIOA, EXTI_SOURCE_PIN9);
    syscfg_exti_line_config(EXTI_SOURCE_GPIOA, EXTI_SOURCE_PIN10);
    /* 快速键：PB5、PB6对应EXTI5、EXTI6 */
    syscfg_exti_line_config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN5);
    syscfg_exti_line_config(EXTI_SOURCE_GPIOB, EXTI_SOURCE_PIN6);
    /* 急停键：PA8对应EXTI8 */
    syscfg_exti_line_config(EXTI_SOURCE_GPIOA, EXTI_SOURCE_PIN8);

    /* 配置全部按键：下降沿触发中断 */
    exti_init(EXTI_3,  EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_init(EXTI_4,  EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_init(EXTI_5,  EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_init(EXTI_6,  EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_init(EXTI_8,  EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_init(EXTI_9,  EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    exti_init(EXTI_10, EXTI_INTERRUPT, EXTI_TRIG_FALLING);
    /* 清除可能存在的中断挂起标志 */
    exti_interrupt_flag_clear(EXTI_3);
    exti_interrupt_flag_clear(EXTI_4);
    exti_interrupt_flag_clear(EXTI_5);
    exti_interrupt_flag_clear(EXTI_6);
    exti_interrupt_flag_clear(EXTI_8);
    exti_interrupt_flag_clear(EXTI_9);
    exti_interrupt_flag_clear(EXTI_10);
    /* 开启两组中断入口，优先级设为2 */
    nvic_irq_enable(EXTI2_3_IRQn, 2U);
    nvic_irq_enable(EXTI4_15_IRQn, 2U);
    /* ===== 低功耗相关代码-引脚初始化-新增结束 ===== */

    g_demo.estop_since = Demo_Now();
#endif
    Demo_ConfigLoad();
    g_demo.random = (g_config.id * 0x9E3779B9UL) ^ Demo_Now() ^ 0x6D2B79F5UL;
    if(!g_demo.random) g_demo.random = 1U;
    g_demo.error = g_demo.storage_ok ? "UNPAIRED" : "FLASH_OVERLAP";
    usart_interrupt_enable(BSP_USART, USART_INT_RBNE); nvic_irq_enable(USART1_IRQn, 1U);
    if(restart_cause & 1U) Demo_Print("RESET FWDGT WAIT RELEASE\r\n");
    if(restart_cause & 2U) Demo_Print("RESET WWDGT WAIT RELEASE\r\n");
    if(restart_cause & 4U) Demo_Print("RESET SOFTWARE WAIT RELEASE\r\n");
#if WIRELESS_DEMO_TX
    Demo_Print("\r\nWIRELESS V5 F50 LP180 TX211 PROTO=A2 UART=115200 8N1 ESTOP=08 STATUS=03/04\r\n");
#else
    Demo_Print("\r\nWIRELESS V2 RX221 UART=115200 8N1\r\n");
#endif
    if(g_config.configured && g_demo.storage_ok) (void)Demo_RadioInit();
    Demo_ShowConfig();
    Demo_Print("INIT %s\r\n", g_demo.ready ? "OK" : g_demo.error);
    /*----------低功耗开始----------------*/
    g_rtc_ready = Demo_RtcInit();
    Demo_Print("RTC INIT %s\r\n", g_rtc_ready ? "OK" : "FAIL");
    /*----------低功耗结束----------------*/
    
    g_demo.report_at = Demo_Now();
    
    /*----------低功耗开始----------------*/
    g_idle_since = g_demo.report_at;
    g_idle_ready = FALSE;
    g_idle_tracking = TRUE;
    g_watchdog_at = g_demo.report_at;
    /*----------低功耗结束----------------*/
    Demo_LedProcess();
}
/* Check configuration and recover the radio without delaying the main loop. */
BOOL Demo_RadioCheckAndRetry(void)
{
    u32 now;

    if(!g_config.configured || !g_demo.storage_ok) return FALSE;
    if(g_demo.ready) return TRUE;

    now = Demo_Now();
    if((u32)(now - g_demo.retry_at) >= DEMO_RETRY_TICKS) {
        (void)Demo_RadioInit();
    }

    /* Preserve the original recovery order: transmit from the next loop. */
    return FALSE;
}

/* Print a snapshot once per second without blocking radio processing. */
void Demo_ReportIfDue(void)
{
    u32 now = Demo_Now();

    if((u32)(now - g_demo.report_at) < DEMO_PRINT_TICKS) return;

    g_demo.report_at = now;
    Demo_Report();
}

void WirelessDemo_Process(void)
{
    Demo_UartProcess();
#if WIRELESS_DEMO_TX
    Demo_ButtonProcess();
#endif
    if(Demo_RadioCheckAndRetry()) {
#if WIRELESS_DEMO_TX
        Demo_TxProcess();
#else
        Demo_RxProcess();
#endif
    }
    Demo_ReportIfDue();
    Demo_LedProcess();
    Demo_LowPowerProcess();
}
/*-----------------------低功耗开始----------------------*/
static void Demo_KeyInterrupt(exti_line_enum line)
{
    if(SET == exti_interrupt_flag_get(line)) {
        g_key_wakeup = 1U;
        exti_interrupt_flag_clear(line);
    }
}

/* EXTI2、EXTI3共用入口：本项目使用EXTI3 */
void EXTI2_3_IRQHandler(void)
{
    Demo_KeyInterrupt(EXTI_3);
}

/* EXTI4～EXTI15共用入口：清除本项目使用的六条线 */
void EXTI4_15_IRQHandler(void)
{
    Demo_KeyInterrupt(EXTI_4);
    Demo_KeyInterrupt(EXTI_5);
    Demo_KeyInterrupt(EXTI_6);
    Demo_KeyInterrupt(EXTI_8);
    Demo_KeyInterrupt(EXTI_9);
    Demo_KeyInterrupt(EXTI_10);
}

void RTC_IRQHandler(void)
{
    if(SET == rtc_flag_get(RTC_FLAG_ALARM0)) {
        /* 清除RTC内部标志和EXTI标志 */
        rtc_flag_clear(RTC_FLAG_ALARM0);  
        exti_interrupt_flag_clear(EXTI_17);
        
        /* 记录本次唤醒来自RTC */
        g_rtc_wakeup = 1U;
    }
}
/*-----------------------低功耗结束----------------------*/
