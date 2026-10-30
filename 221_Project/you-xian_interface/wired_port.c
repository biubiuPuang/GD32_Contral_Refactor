#include "wired_control.h"
#include "wired_config.h"
#include "gd32e23x.h"
#include "usart.h"
#include <stdio.h>

#define WIRED_DIRECTION_PINS (GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10)
#define WIRED_REQUEST_PINS   (GPIO_PIN_12 | GPIO_PIN_13)
#define WIRED_INPUT_PINS     (GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5)

static WiredLogic g_wired;
static WiredInput g_physical_input;
static uint16_t g_last_timer;
static uint8_t g_half_ms;
static uint32_t g_now_ms, g_report_at, g_log_drops;
static uint8_t g_software_stop;
static uint8_t g_remote_estop;
static uint8_t g_output_fault;
static uint8_t g_remote_status; /* 03/04 are display states, never execution targets. */
static uint8_t g_remote_target, g_remote_applied, g_remote_transition, g_remote_settling;
static uint32_t g_remote_at;
static uint8_t (*g_log_sink)(const char *line);
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
    input.software_stop = (uint8_t)(g_software_stop || g_remote_estop);
    return input;
}

static uint8_t WiredPort_Apply(const WiredOutput *output)
{
    uint32_t active = (output->up ? GPIO_PIN_9 : 0U) |
                      (output->down ? GPIO_PIN_8 : 0U) |
                      (output->fast ? GPIO_PIN_10 : 0U);
    uint16_t expected_a, expected_b;
    uint32_t requests;
    if(output->inhibit) {
        g_remote_applied = 0U;
        if(g_remote_target) { g_remote_transition = 1U; g_remote_settling = 0U; }
    }
    requests = (g_remote_applied & 1U ? GPIO_PIN_12 : 0U) |
               (g_remote_applied & 2U ? GPIO_PIN_13 : 0U);
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
    gpio_bit_reset(GPIOB, WIRED_REQUEST_PINS & ~requests);
    gpio_bit_set(GPIOB, requests);
    gpio_bit_set(GPIOA, WIRED_DIRECTION_PINS & ~active);
    gpio_bit_reset(GPIOA, active);
    if(output->allow) gpio_bit_reset(GPIOB, GPIO_PIN_6);

    /* Output latch readback is NOT contact/motor feedback. */
    expected_a = (uint16_t)(WIRED_DIRECTION_PINS & ~active);
    expected_b = (uint16_t)((output->allow ? 0U : GPIO_PIN_6) |
                            (output->inhibit ? GPIO_PIN_11 : 0U) | requests);
    return (uint8_t)((gpio_output_port_get(GPIOA) & WIRED_DIRECTION_PINS) == expected_a &&
                     (gpio_output_port_get(GPIOB) &
                      (GPIO_PIN_6 | GPIO_PIN_11 | WIRED_REQUEST_PINS)) == expected_b);
}

/* Nonblocking polled transmit: one byte when USART is ready, no printf/fputc
 * wait inside the control loop. Under serial backpressure drop a whole NEW
 * report, preserve the old line, and continue sampling/stopping normally. */
static void WiredPort_LogProcess(void)
{
    if(g_log_sink) return; /* One UART writer: integrated radio TX queue. */
    if(g_log_sent < g_log_size && usart_flag_get(BSP_USART, USART_FLAG_TBE))
        usart_data_transmit(BSP_USART, (uint8_t)g_log[g_log_sent++]);
}

const char *WiredControl_Error(void)
{
    if(g_output_fault) return "OUTPUT_MISMATCH";
    return g_wired.fault ? "INTERNAL_STATE" : NULL;
}

const char *WiredControl_StatusLine(void)
{
    if(WiredControl_Error()) return "wu-xian not connect\r\n";
    if(g_remote_estop || g_software_stop) return "stop model\r\n";
    if((g_physical_input.up && g_physical_input.down) ||
       g_wired.reason == WIRED_REASON_CONFLICT) return "error model\r\n";
    /* Do not claim an action while KM/relays are being changed or settling. */
    if(g_remote_transition || (g_wired.phase != WIRED_RUN && g_wired.phase != WIRED_IDLE))
        return NULL;
    if(g_wired.phase == WIRED_RUN && g_wired.output.allow && !g_wired.output.inhibit) {
        if(g_remote_applied) {
            if(g_wired.output.up) return g_wired.output.fast ? "wu-xian fast up\r\n" : "wu-xian up\r\n";
            return g_wired.output.fast ? "wu-xian fast dowm\r\n" : "wu-xian dowm\r\n";
        }
        if(g_wired.output.up) return g_wired.output.fast ? "you-xian fast up\r\n" : "you-xian up\r\n";
        return g_wired.output.fast ? "you-xian fast dowm\r\n" : "you-xian dowm\r\n";
    }
    if(g_remote_status == 3U) return "error model\r\n";
    if(g_physical_input.fast && !g_physical_input.up && !g_physical_input.down)
        return "you-xian fast\r\n";
    if(g_remote_status == 4U) return "wu-xian fast\r\n";
    /* No independent limit/E-stop input: 'cant' means RF request not passed,
     * including limit, physical E-stop or other hardware gating. It does not
     * identify the cause. Wired raw direction keys are not observable either.
     * Mixed-source reporting retains the documented arbitration limitation. */
    if(g_remote_target & 1U) return "cant up model\r\n";
    if(g_remote_target & 2U) return "cant dowm model\r\n";
    return "sleep model\r\n";
}

static void WiredPort_Report(uint32_t now)
{
    int length;
    const char *line = WiredControl_StatusLine();
    const char *error = WiredControl_Error();
    (void)now;
    if(!line) return;
    if(g_log_sent < g_log_size) { ++g_log_drops; return; }
    length = snprintf(g_log, sizeof(g_log), "%s%s%s%s", line,
                      error ? "ERR EXEC " : "", error ? error : "", error ? "\r\n" : "");
    if(length < 0 || length >= (int)sizeof(g_log)) { ++g_log_drops; return; }
    g_log_sent = 0U; g_log_size = (uint16_t)length;
}

void WiredControl_Init(void)
{
    /* Safety GPIOs precede UART/timer setup; do not call WirelessDemo_Init. */
    WiredPort_GpioInit();
    usart_init(115200U); /* Existing BSP enables UART and GPIO clocks. */
    WiredPort_TimerInit();
    g_software_stop = 0U;
    g_remote_estop = 0U;
    g_output_fault = 0U;
    g_remote_status = 0U; g_remote_target = 0U; g_remote_applied = 0U;
    g_remote_transition = 0U; g_remote_settling = 0U; g_remote_at = 0U;
    g_log_sink = NULL;
    g_physical_input = WiredPort_ReadInput();
    g_log_size = 0U; g_log_sent = 0U; g_log_drops = 0U;
    g_report_at = 0U;
    WiredLogic_Init(&g_wired, 0U);
    if(!WiredPort_Apply(&g_wired.output)) {
        g_output_fault = 1U;
        WiredControl_SetSoftwareStop(1U);
    }
}

static void WiredPort_AssertStop(void)
{
    WiredInput input;
    /* Physical inhibit immediately; record it even if cleared again before
     * Process. PB11 removes circuit power, not MCU/radio power: keep polling. */
    gpio_bit_set(GPIOB, GPIO_PIN_6 | GPIO_PIN_11);
    input = WiredPort_ReadInput();
    WiredLogic_Step(&g_wired, input, WiredPort_Now());
    (void)WiredPort_Apply(&g_wired.output);
}

void WiredControl_SetSoftwareStop(uint8_t active)
{
    /* Main context. Hardware E-stop acts independently of these reasons. */
    g_software_stop = (uint8_t)(active != 0U || g_output_fault);
    if(g_software_stop) WiredPort_AssertStop();
}

void WiredControl_SetRemoteEmergencyStop(uint8_t active)
{
    active = (uint8_t)(active != 0U);
    if(active == g_remote_estop) return;
    g_remote_estop = active;
    if(active) WiredPort_AssertStop();
    /* Clear only RF E-stop. Process releases PB11 and observes circuit settle;
     * do not wait for PB3/PB4 while PB11 is still cutting their circuit power. */
}

void WiredControl_SetLogSink(uint8_t (*sink)(const char *line))
{
    /* Call before normal processing to switch to a single shared UART queue. */
    g_log_sink = sink;
    g_log_size = 0U; g_log_sent = 0U;
}

uint8_t WiredControl_RadioRetryAllowed(void)
{
    return (uint8_t)((g_wired.phase == WIRED_IDLE || g_wired.phase == WIRED_BLOCKED) &&
                     !g_wired.output.allow && !g_wired.output.up && !g_wired.output.down);
}

void WiredControl_SetRemoteCommand(uint8_t command)
{
    WiredInput input;
    uint32_t now;
    /* Preserve status-only packets for display; never expose their direction
     * bits to execution GPIOs. In particular 03 must NOT become UP+DOWN. */
    g_remote_status = command;
    if(command != 0U && command != 1U && command != 2U && command != 5U && command != 6U)
        command = 0U;
    if(command == g_remote_target) return;
    now = WiredPort_Now();
    g_remote_target = command;
    g_remote_transition = 1U; g_remote_settling = 0U;
    /* Remove KM FIRST, then deassert both RF direction requests. Never
     * reinterpret a decaying RF feedback signal as a new wired command. */
    input.up = 0U; input.down = 0U; input.fast = 0U;
    input.software_stop = (uint8_t)(g_software_stop || g_remote_estop);
    WiredLogic_Step(&g_wired, input, now);
    gpio_bit_set(GPIOB, GPIO_PIN_6);
    g_remote_applied = 0U;
    if(!WiredPort_Apply(&g_wired.output)) {
        g_output_fault = 1U;
        WiredControl_SetSoftwareStop(1U);
    }
}

void WiredControl_Process(void)
{
    uint32_t now = WiredPort_Now();
    WiredInput input = WiredPort_ReadInput();
    g_physical_input = input;
    if(g_remote_transition && !g_software_stop && !g_remote_estop && !g_wired.output.inhibit &&
       g_wired.phase == WIRED_IDLE) {
        if(!g_remote_settling) {
            g_remote_applied = g_remote_target;
            g_remote_at = now;
            g_remote_settling = 1U;
        }
        if((uint32_t)(now - g_remote_at) >= WIRED_RF_SETTLE_MS) {
            g_remote_transition = 0U; g_remote_settling = 0U;
        }
    }
    if(g_remote_transition) {
        input.up = 0U; input.down = 0U; input.fast = 0U;
    } else if(g_remote_applied) {
        /* TODO: Known wired/RF arbitration bug, intentionally deferred.
         * PB3/PB4 are shared post-limit direction inputs, not raw wired keys;
         * WIRED_IDLE is not available to the MCU. A wired slow-UP takeover
         * during RF fast-UP can leave PB3 unchanged, so the RF FAST bit below
         * can incorrectly keep fast drive active for the wired request.
         * An opposite wired direction can also be filtered by the RF target.
         * Hardware request gating alone does not make this software adapter
         * implement wired priority. Mixed-source speed/source reporting is
         * not reliable. No periodic RF interruption/probing is implemented;
         * a future arbitration strategy must be agreed before fixing this.
         */
        /* RF intent never substitutes for actual post-limit direction input.
         * The common feedback must match the requested direction. */
        if(!(input.up && input.down)) {
            input.up = (uint8_t)(input.up && (g_remote_applied & 1U));
            input.down = (uint8_t)(input.down && (g_remote_applied & 2U));
        }
        input.fast = (uint8_t)((g_remote_applied & 4U) && (input.up != input.down));
    }
    WiredLogic_Step(&g_wired, input, now);
    if(!WiredPort_Apply(&g_wired.output)) {
        /* No RUN log is queued after detecting an output latch mismatch. */
        g_output_fault = 1U;
        WiredControl_SetSoftwareStop(1U);
        input.software_stop = 1U;
        WiredLogic_Step(&g_wired, input, now);
        (void)WiredPort_Apply(&g_wired.output);
    }
    if(!g_log_sink && (uint32_t)(now - g_report_at) >= WIRED_REPORT_MS) {
        g_report_at = now;
        WiredPort_Report(now);
    }
    WiredPort_LogProcess();
}
