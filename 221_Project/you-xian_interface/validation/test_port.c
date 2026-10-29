#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "gd32e23x.h"
#include "../wired_port.c"

static uint16_t latch[2], modes[2], pins_in, timer_count;
static uint8_t clocks[3], serial_ready, inject_bad_latch;
static uint32_t apb_hz, ahb_hz, serial_bytes, report_lines, up_lines;
static char serial_line[512];
static size_t serial_length;

uint32_t rcu_clock_freq_get(uint32_t clock) { return clock == CK_APB1 ? apb_hz : ahb_hz; }
void rcu_periph_clock_enable(uint32_t clock) { assert(clock < 3U); clocks[clock] = 1U; }
void timer_deinit(uint32_t timer) { assert(timer == TIMER13 && clocks[RCU_TIMER13]); }
void timer_struct_para_init(timer_parameter_struct *timer) { memset(timer, 0, sizeof(*timer)); }
void timer_init(uint32_t timer, timer_parameter_struct *config)
{
    uint32_t hz = apb_hz == ahb_hz ? apb_hz : 2U * apb_hz;
    assert(timer == TIMER13 && config->period == 65535U);
    assert(hz / ((uint32_t)config->prescaler + 1U) == 2000U);
}
void timer_counter_value_config(uint32_t timer, uint16_t value)
{ assert(timer == TIMER13); timer_count = value; }
void timer_enable(uint32_t timer) { assert(timer == TIMER13); }
uint16_t timer_counter_read(uint32_t timer) { assert(timer == TIMER13); return timer_count; }

static void check_physical(void)
{
    uint16_t pa = latch[GPIOA], pb = latch[GPIOB];
    if(modes[GPIOA] & WIRED_DIRECTION_PINS) {
        assert((pa & (GPIO_PIN_8 | GPIO_PIN_9)) != 0U);
        if(!(pb & GPIO_PIN_6)) {
            assert(!(pb & GPIO_PIN_11));
            assert((pa & (GPIO_PIN_8 | GPIO_PIN_9)) != (GPIO_PIN_8 | GPIO_PIN_9));
        }
    }
    if(modes[GPIOB] & WIRED_REQUEST_PINS) assert(!(pb & WIRED_REQUEST_PINS));
}

static void write_pins(uint32_t port, uint32_t pins, uint8_t value)
{
    uint16_t before = latch[port];
    assert(port < 2U && clocks[port]);
    if(value) latch[port] |= (uint16_t)pins;
    else latch[port] &= (uint16_t)~pins;
    if(port == GPIOA && ((before ^ latch[port]) & modes[port] & WIRED_DIRECTION_PINS))
        assert(latch[GPIOB] & GPIO_PIN_6); /* KM must already be off. */
    check_physical();
}
void gpio_bit_set(uint32_t port, uint32_t pins) { write_pins(port, pins, 1U); }
void gpio_bit_reset(uint32_t port, uint32_t pins) { write_pins(port, pins, 0U); }
void gpio_bit_write(uint32_t port, uint32_t pins, bit_status value) { write_pins(port, pins, value != RESET); }
void gpio_output_options_set(uint32_t port, uint32_t type, uint32_t speed, uint32_t pins)
{ (void)type; (void)speed; (void)pins; assert(clocks[port]); }
void gpio_mode_set(uint32_t port, uint32_t mode, uint32_t pupd, uint32_t pins)
{
    assert(clocks[port]);
    if(mode == GPIO_MODE_OUTPUT) {
        if(port == GPIOB) {
            assert((latch[port] & (GPIO_PIN_6 | GPIO_PIN_11)) == (GPIO_PIN_6 | GPIO_PIN_11));
            assert(!(latch[port] & WIRED_REQUEST_PINS));
        } else assert((latch[port] & WIRED_DIRECTION_PINS) == WIRED_DIRECTION_PINS);
        modes[port] |= (uint16_t)pins;
    } else assert(port == GPIOB && pins == WIRED_INPUT_PINS && pupd == GPIO_PUPD_PULLUP);
    check_physical();
}
uint16_t gpio_input_port_get(uint32_t port) { assert(port == GPIOB); return pins_in; }
uint16_t gpio_output_port_get(uint32_t port)
{ return (uint16_t)(latch[port] ^ ((inject_bad_latch && port == GPIOA) ? GPIO_PIN_9 : 0U)); }
void usart_init(uint32_t baudrate) { assert(baudrate == 115200U); }
FlagStatus usart_flag_get(uint32_t uart, uint32_t flag)
{ assert(uart == BSP_USART && flag == USART_FLAG_TBE); return serial_ready ? SET : RESET; }
void usart_data_transmit(uint32_t uart, uint8_t value)
{
    assert(uart == BSP_USART && serial_ready);
    ++serial_bytes;
    assert(serial_length + 1U < sizeof(serial_line));
    serial_line[serial_length++] = (char)value;
    if(value == '\n') {
        unsigned long timestamp;
        serial_line[serial_length] = '\0';
        assert(sscanf(serial_line, "t=%lu", &timestamp) == 1);
        assert(timestamp % 500U == 0U);
        assert(strstr(serial_line, "RF=OFF"));
        if(strstr(serial_line, "you xian model up")) ++up_lines;
        ++report_lines; serial_length = 0U;
    }
}

static void tick(unsigned ms)
{
    while(ms--) {
        unsigned j;
        timer_count = (uint16_t)(timer_count + 2U);
        /* Control is called many times per ms; UART can make progress. */
        for(j = 0U; j < 16U; ++j) WiredControl_Process();
    }
}

static void board_reset(uint32_t apb, uint32_t ahb)
{
    memset(clocks, 0, sizeof(clocks)); memset(modes, 0, sizeof(modes));
    memset(latch, 0, sizeof(latch)); pins_in = 0xFFFFU;
    serial_ready = 1U; inject_bad_latch = 0U;
    apb_hz = apb; ahb_hz = ahb;
    serial_bytes = 0U; serial_length = 0U; report_lines = 0U; up_lines = 0U;
    WiredControl_Init();
    assert(latch[GPIOB] & GPIO_PIN_6);
    assert(latch[GPIOB] & GPIO_PIN_11);
    assert((latch[GPIOA] & WIRED_DIRECTION_PINS) == WIRED_DIRECTION_PINS);
}

int main(void)
{
    uint32_t before;
    /* Both actual AHB/APB1=72 MHz and prescaled APB timer doubling. */
    board_reset(72000000U, 72000000U); tick(110U);
    pins_in &= (uint16_t)~GPIO_PIN_3; tick(41U);
    assert(!(latch[GPIOA] & GPIO_PIN_9) && !(latch[GPIOB] & GPIO_PIN_6));
    tick(1000U);
    assert(up_lines == 2U && report_lines == 3U);
    /* A 0.5-ms sample must not count as a whole millisecond. */
    before = g_now_ms; ++timer_count; WiredControl_Process();
    assert(g_now_ms == before);
    ++timer_count; WiredControl_Process(); assert(g_now_ms == before + 1U);
    /* UART not ready must never stall release/reversal. */
    serial_ready = 0U; before = serial_bytes;
    tick(600U); assert(serial_bytes == before);
    pins_in |= GPIO_PIN_3; pins_in &= (uint16_t)~GPIO_PIN_4;
    WiredControl_Process(); assert(latch[GPIOB] & GPIO_PIN_6);
    tick(112U);
    assert(!(latch[GPIOA] & GPIO_PIN_8) && !(latch[GPIOB] & GPIO_PIN_6));
    tick(1000U); assert(g_log_drops > 0U);
    /* Stop request then immediate cancel before Process still removes KM and
     * cannot bypass the recorded stop/release sequence. */
    WiredControl_SetSoftwareStop(1U);
    assert(latch[GPIOB] & GPIO_PIN_6); assert(latch[GPIOB] & GPIO_PIN_11);
    WiredControl_SetSoftwareStop(0U); WiredControl_Process();
    tick(49U); assert(latch[GPIOB] & GPIO_PIN_6);
    tick(113U); assert(!(latch[GPIOB] & GPIO_PIN_6));
    /* Readback failure latches a software stop until board reinitialization. */
    inject_bad_latch = 1U; WiredControl_Process();
    assert(g_output_fault && (latch[GPIOB] & GPIO_PIN_6) && (latch[GPIOB] & GPIO_PIN_11));
    inject_bad_latch = 0U; WiredControl_SetSoftwareStop(0U); tick(200U);
    assert(latch[GPIOB] & GPIO_PIN_6); assert(latch[GPIOB] & GPIO_PIN_11);

    board_reset(36000000U, 72000000U); tick(40000U);
    assert(g_now_ms == 40000U); /* Includes 16-bit timer rollover. */
    assert(report_lines == 80U); /* t=0 plus 500..39500, 40000 still queued. */
    assert(!up_lines);
    puts("PASS: actual port code, safe GPIO preload/order, 72MHz prescaler, timer doubling/rollover/fraction, 500ms logs, UART backpressure, brief software stop, output readback fault.");
    return 0;
}
