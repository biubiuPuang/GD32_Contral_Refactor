#include "wired_control.h"
#include "wired_config.h"
#include "gd32e23x.h"
#include "usart.h"
#include <stdio.h>

#define WIRED_DIRECTION_PINS (GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10)
#define WIRED_REQUEST_PINS   (GPIO_PIN_12 | GPIO_PIN_13)
#define WIRED_INPUT_PINS     (GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5)

static WiredLogic g_wired;
static uint16_t g_last_timer;
static uint8_t g_half_ms;
static uint32_t g_now_ms, g_report_at, g_log_drops;
static uint8_t g_software_stop;
static uint8_t g_output_fault;
static char g_log[256];
static uint16_t g_log_size, g_log_sent;

/* Use TIMER13 at 2 kHz (72 MHz / 36000 fits its 16-bit prescaler).
 * TIMER2 belongs to the existing wireless Demo.
 * Process must run continuously (counter wraps every 32.768 seconds).
 * No SysTick reconfiguration and no additional interrupt handlers. */
static void WiredPort_TimerInit(void)
{
    timer_parameter_struct timer;
    uint32_t clock = rcu_clock_freq_get(CK_APB1);
    if(clock != rcu_clock_freq_get(CK_AHB)) clock *= 2U;
    rcu_periph_clock_enable(RCU_TIMER13);
    timer_deinit(TIMER13);
    timer_struct_para_init(&timer);
    timer.prescaler = (uint16_t)(clock / 2000U - 1U);
    timer.alignedmode = TIMER_COUNTER_EDGE;
    timer.counterdirection = TIMER_COUNTER_UP;
    timer.period = 0xFFFFU;
    timer.clockdivision = TIMER_CKDIV_DIV1;
    timer.repetitioncounter = 0U;
    timer_init(TIMER13, &timer);
    timer_counter_value_config(TIMER13, 0U);
    timer_enable(TIMER13);
    g_last_timer = 0U;
    g_half_ms = 0U;
    g_now_ms = 0U;
}

static uint32_t WiredPort_Now(void)
{
    uint16_t current = (uint16_t)timer_counter_read(TIMER13);
    uint32_t half_ticks = (uint16_t)(current - g_last_timer) + (uint32_t)g_half_ms;
    g_now_ms += half_ticks / 2U;
    g_half_ms = (uint8_t)(half_ticks % 2U);
    g_last_timer = current;
    return g_now_ms;
}

static void WiredPort_GpioInit(void)
{
    /* Enable both clocks, preload safe output latches BEFORE output mode. */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    gpio_bit_set(GPIOB, GPIO_PIN_6 | GPIO_PIN_11);
    gpio_bit_reset(GPIOB, WIRED_REQUEST_PINS);
    gpio_bit_set(GPIOA, WIRED_DIRECTION_PINS);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ,
                            GPIO_PIN_6 | GPIO_PIN_11 | WIRED_REQUEST_PINS);
    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ, WIRED_DIRECTION_PINS);
    gpio_mode_set(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                  GPIO_PIN_6 | GPIO_PIN_11 | WIRED_REQUEST_PINS);
    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, WIRED_DIRECTION_PINS);
    gpio_mode_set(GPIOB, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, WIRED_INPUT_PINS);
}

static WiredInput WiredPort_ReadInput(void)
{
    /* One port read avoids mixing two direction samples from different times. */
    uint16_t pins = gpio_input_port_get(GPIOB);
    WiredInput input;
    input.up = (uint8_t)((pins & GPIO_PIN_3) == 0U);
    input.down = (uint8_t)((pins & GPIO_PIN_4) == 0U);
    input.fast = (uint8_t)((pins & GPIO_PIN_5) == 0U);
    input.software_stop = g_software_stop;
    return input;
}

static uint8_t WiredPort_Apply(const WiredOutput *output)
{
    uint32_t active = (output->up ? GPIO_PIN_9 : 0U) |
                      (output->down ? GPIO_PIN_8 : 0U) |
                      (output->fast ? GPIO_PIN_10 : 0U);
    uint16_t expected_a, expected_b;
    if((output->up && output->down) ||
       (output->fast && !output->up && !output->down) ||
       (output->allow && (output->inhibit || (!output->up && !output->down)))) {
        gpio_bit_set(GPIOB, GPIO_PIN_6 | GPIO_PIN_11);
        gpio_bit_reset(GPIOB, WIRED_REQUEST_PINS);
        gpio_bit_set(GPIOA, WIRED_DIRECTION_PINS);
        return 0U;
    }

    /* Do not pulse PB6 high on every RUN iteration. Remove permission FIRST
     * only when stopping; restore it LAST after all other outputs are valid. */
    if(!output->allow) gpio_bit_set(GPIOB, GPIO_PIN_6);
    gpio_bit_write(GPIOB, GPIO_PIN_11, output->inhibit ? SET : RESET);
    gpio_bit_reset(GPIOB, WIRED_REQUEST_PINS); /* Wired-only build. */
    gpio_bit_set(GPIOA, WIRED_DIRECTION_PINS & ~active);
    gpio_bit_reset(GPIOA, active);
    if(output->allow) gpio_bit_reset(GPIOB, GPIO_PIN_6);

    /* Output latch readback is NOT contact/motor feedback. */
    expected_a = (uint16_t)(WIRED_DIRECTION_PINS & ~active);
    expected_b = (uint16_t)((output->allow ? 0U : GPIO_PIN_6) |
                            (output->inhibit ? GPIO_PIN_11 : 0U));
    return (uint8_t)((gpio_output_port_get(GPIOA) & WIRED_DIRECTION_PINS) == expected_a &&
                     (gpio_output_port_get(GPIOB) &
                      (GPIO_PIN_6 | GPIO_PIN_11 | WIRED_REQUEST_PINS)) == expected_b);
}

/* Nonblocking polled transmit: one byte when USART is ready, no printf/fputc
 * wait inside the control loop. Under serial backpressure drop a whole NEW
 * report, preserve the old line, and continue sampling/stopping normally. */
static void WiredPort_LogProcess(void)
{
    if(g_log_sent < g_log_size && usart_flag_get(BSP_USART, USART_FLAG_TBE))
        usart_data_transmit(BSP_USART, (uint8_t)g_log[g_log_sent++]);
}

static void WiredPort_Report(uint32_t now)
{
    int length;
    if(g_log_sent < g_log_size) { ++g_log_drops; return; }
    length = snprintf(g_log, sizeof(g_log),
        "t=%lu %s speed=%s phase=%s reason=%s "
        "PB3/4/5=%u%u%u PB6/11=%u%u PA9/8/10=%u%u%u RF=OFF drop=%lu\r\n",
        (unsigned long)now, WiredLogic_ModeName(&g_wired),
        g_wired.output.fast ? "fast" : "slow",
        WiredLogic_PhaseName(g_wired.phase),
        g_output_fault ? "OUTPUT_MISMATCH" : WiredLogic_ReasonName(g_wired.reason),
        (unsigned)!g_wired.input.up, (unsigned)!g_wired.input.down,
        (unsigned)!g_wired.input.fast, (unsigned)!g_wired.output.allow,
        (unsigned)g_wired.output.inhibit, (unsigned)!g_wired.output.up,
        (unsigned)!g_wired.output.down, (unsigned)!g_wired.output.fast,
        (unsigned long)g_log_drops);
    if(length < 0 || length >= (int)sizeof(g_log)) {
        ++g_log_drops; g_log_sent = 0U; g_log_size = 0U; return;
    }
    g_log_sent = 0U;
    g_log_size = (uint16_t)length;
}

void WiredControl_Init(void)
{
    /* Safety GPIOs precede UART/timer setup; do not call WirelessDemo_Init. */
    WiredPort_GpioInit();
    usart_init(115200U); /* Existing BSP enables UART and GPIO clocks. */
    WiredPort_TimerInit();
    g_software_stop = 0U;
    g_output_fault = 0U;
    g_log_size = 0U; g_log_sent = 0U; g_log_drops = 0U;
    g_report_at = 0U;
    WiredLogic_Init(&g_wired, 0U);
    if(!WiredPort_Apply(&g_wired.output)) {
        g_output_fault = 1U;
        WiredControl_SetSoftwareStop(1U);
    }
    WiredPort_Report(0U);
}

void WiredControl_SetSoftwareStop(uint8_t active)
{
    /* Call in main context. A real hardware E-stop acts independently of MCU. */
    g_software_stop = (uint8_t)(active != 0U || g_output_fault);
    if(g_software_stop) {
        WiredInput input;
        /* Physical inhibit immediately; record the stop even if the caller
         * clears it again before the next Process iteration. */
        gpio_bit_set(GPIOB, GPIO_PIN_6 | GPIO_PIN_11);
        input = WiredPort_ReadInput();
        WiredLogic_Step(&g_wired, input, WiredPort_Now());
        (void)WiredPort_Apply(&g_wired.output);
    }
}

void WiredControl_Process(void)
{
    uint32_t now = WiredPort_Now();
    WiredInput input = WiredPort_ReadInput();
    WiredLogic_Step(&g_wired, input, now);
    if(!WiredPort_Apply(&g_wired.output)) {
        /* No RUN log is queued after detecting an output latch mismatch. */
        g_output_fault = 1U;
        WiredControl_SetSoftwareStop(1U);
        input.software_stop = 1U;
        WiredLogic_Step(&g_wired, input, now);
        (void)WiredPort_Apply(&g_wired.output);
    }
    if((uint32_t)(now - g_report_at) >= WIRED_REPORT_MS) {
        g_report_at = now;
        WiredPort_Report(now);
    }
    WiredPort_LogProcess();
}
