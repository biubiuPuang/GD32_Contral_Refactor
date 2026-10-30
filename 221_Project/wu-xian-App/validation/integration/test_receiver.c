/* Real RX service + real GPIO adapter + real execution logic + real main-loop
 * integration. Radio/board/Flash peripherals are simulated, never hardware. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#undef assert
#define assert(c) do { if(!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
#define DEMO_HOST_TEST 1
#include "../../wireless_demo.c"
#include "../../../you-xian_interface/wired_port.c"
#include "../../receiver_app.c"

static uint16_t counter2, counter13, latch[16], modes[16];
static uint32_t sim_ms, clocks, irq_mask, uart_flags[8], uart_byte;
static uint32_t flash[512], image_limit;
static unsigned flash_writes, flash_erases, program_calls, init_calls, fifo_reads;
static int flash_unlocked, fail_program, fail_erase, tx_irq, uart_ready;
static int available, corrupt_readback, stby_fail, rx_fail, overflow, output_fault;
static uint8_t registers[128], packet[8], flags, status;
static uint8_t wired_up, wired_down, wired_fast, physical_estop, limit_up, limit_down, no_feedback;
static uint32_t rf_up_on, rf_down_on, rf_up_off, rf_down_off;
static uint8_t rf_had_up, rf_had_down;
static char output[1048576];
static size_t output_length;
static unsigned serial_lines;

static u32 Demo_ReadWord(u32 address)
{
    assert(address >= DEMO_FLASH_FIRST && address < DEMO_FLASH_END && !(address & 3U));
    return flash[(address - DEMO_FLASH_FIRST) / 4U];
}
static u32 Demo_ImageLimit(void) { return image_limit; }
uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask = 1U; }
void __enable_irq(void) { irq_mask = 0U; }
void fmc_unlock(void) { assert(irq_mask); flash_unlocked = 1; }
void fmc_lock(void) { flash_unlocked = 0; }
void fmc_flag_clear(uint32_t f) { (void)f; assert(flash_unlocked); }
static void flash_safe(void)
{
    assert(flash_unlocked && irq_mask);
    assert(latch[GPIOB] & GPIO_PIN_6);
    assert(latch[GPIOB] & GPIO_PIN_11);
    assert(!(latch[GPIOB] & (GPIO_PIN_12 | GPIO_PIN_13)));
}
uint32_t fmc_page_erase(uint32_t address)
{
    flash_safe();
    assert(address >= DEMO_FLASH_FIRST && address < DEMO_FLASH_END);
    assert(!(address & (DEMO_FLASH_PAGE - 1U)));
    if(g_demo.saved_address) assert(address != (g_demo.saved_address & ~(DEMO_FLASH_PAGE - 1U)));
    if(fail_erase) return 1U;
    memset(flash + (address - DEMO_FLASH_FIRST) / 4U, 0xFF, DEMO_FLASH_PAGE);
    ++flash_erases; return FMC_READY;
}
uint32_t fmc_word_program(uint32_t address, uint32_t value)
{
    uint32_t *word;
    flash_safe();
    assert(address >= DEMO_FLASH_FIRST && address < DEMO_FLASH_END && !(address & 3U));
    if(++program_calls == (unsigned)fail_program) return 1U;
    word = flash + (address - DEMO_FLASH_FIRST) / 4U;
    assert((*word & value) == value); *word &= value; ++flash_writes;
    return FMC_READY;
}

uint32_t rcu_clock_freq_get(uint32_t c) { return c == CK_APB1 ? 36000000U : 72000000U; }
void rcu_periph_clock_enable(uint32_t c) { assert(c < 32U); clocks |= 1UL << c; }
void timer_deinit(uint32_t t)
{ assert((t == TIMER2 && (clocks & (1UL << RCU_TIMER2))) || (t == TIMER13 && (clocks & (1UL << RCU_TIMER13)))); }
void timer_struct_para_init(timer_parameter_struct *p) { memset(p, 0, sizeof(*p)); }
void timer_init(uint32_t t, timer_parameter_struct *p)
{ assert((t == TIMER2 && p->prescaler == 7199U) || (t == TIMER13 && p->prescaler == 35999U)); assert(p->period == 65535U); }
void timer_counter_value_config(uint32_t t, uint16_t value)
{ if(t == TIMER2) counter2 = value; else { assert(t == TIMER13); counter13 = value; } }
void timer_enable(uint32_t t) { assert(t == TIMER2 || t == TIMER13); }
uint32_t timer_counter_read(uint32_t t)
{ if(t == TIMER2) return counter2; assert(t == TIMER13); return counter13; }
void systick_config(void) { }

static void check_outputs(void)
{
    uint16_t pa = latch[GPIOA], pb = latch[GPIOB];
    if(modes[GPIOA] & WIRED_DIRECTION_PINS) {
        assert((pa & (GPIO_PIN_8 | GPIO_PIN_9)) != 0U);
        if(!(pb & GPIO_PIN_6)) {
            assert(!(pb & GPIO_PIN_11));
            assert((pa & (GPIO_PIN_8 | GPIO_PIN_9)) != (GPIO_PIN_8 | GPIO_PIN_9));
        }
    }
    assert((pb & WIRED_REQUEST_PINS) != WIRED_REQUEST_PINS);
}
static void write_pins(uint32_t port, uint32_t pins, uint8_t value)
{
    uint16_t before = latch[port];
    assert((port == GPIOA && (clocks & (1UL << RCU_GPIOA))) ||
           (port == GPIOB && (clocks & (1UL << RCU_GPIOB))));
    if(value) latch[port] |= (uint16_t)pins;
    else latch[port] &= (uint16_t)~pins;
    if(port == GPIOA && ((before ^ latch[port]) & modes[port] & WIRED_DIRECTION_PINS))
        assert(latch[GPIOB] & GPIO_PIN_6);
    if(port == GPIOB && ((before ^ latch[port]) & WIRED_REQUEST_PINS)) {
        assert(latch[GPIOB] & GPIO_PIN_6); /* RF path changes with KM disabled. */
        if((before ^ latch[port]) & GPIO_PIN_12) {
            if(latch[port] & GPIO_PIN_12) { rf_up_on = sim_ms; rf_had_up = 1U; }
            else rf_up_off = sim_ms;
        }
        if((before ^ latch[port]) & GPIO_PIN_13) {
            if(latch[port] & GPIO_PIN_13) { rf_down_on = sim_ms; rf_had_down = 1U; }
            else rf_down_off = sim_ms;
        }
    }
    check_outputs();
}
void gpio_bit_set(uint32_t p, uint32_t pins) { write_pins(p, pins, 1U); }
void gpio_bit_reset(uint32_t p, uint32_t pins) { write_pins(p, pins, 0U); }
void gpio_bit_write(uint32_t p, uint32_t pins, bit_status v) { write_pins(p, pins, (uint8_t)(v != RESET)); }
void gpio_output_options_set(uint32_t p, uint32_t type, uint32_t speed, uint32_t pins)
{ (void)p; (void)type; (void)speed; (void)pins; }
void gpio_mode_set(uint32_t p, uint32_t mode, uint32_t pull, uint32_t pins)
{
    if(mode == GPIO_MODE_OUTPUT) {
        if(p == GPIOB) {
            assert((latch[p] & (GPIO_PIN_6 | GPIO_PIN_11)) == (GPIO_PIN_6 | GPIO_PIN_11));
            assert(!(latch[p] & WIRED_REQUEST_PINS));
        } else assert((latch[p] & WIRED_DIRECTION_PINS) == WIRED_DIRECTION_PINS);
        modes[p] |= (uint16_t)pins;
    } else assert(p == GPIOB && pins == WIRED_INPUT_PINS && pull == GPIO_PUPD_PULLUP);
    check_outputs();
}
uint16_t gpio_input_port_get(uint32_t p)
{
    uint16_t result = 0xFFFFU;
    uint8_t rf_up, rf_down;
    assert(p == GPIOB);
    if((latch[GPIOB] & GPIO_PIN_11) || physical_estop) return result;
    rf_up = (uint8_t)(!no_feedback && rf_had_up &&
        (((latch[GPIOB] & GPIO_PIN_12) && (uint32_t)(sim_ms - rf_up_on) >= 8U) ||
         (!(latch[GPIOB] & GPIO_PIN_12) && (uint32_t)(sim_ms - rf_up_off) < 12U)));
    rf_down = (uint8_t)(!no_feedback && rf_had_down &&
        (((latch[GPIOB] & GPIO_PIN_13) && (uint32_t)(sim_ms - rf_down_on) >= 8U) ||
         (!(latch[GPIOB] & GPIO_PIN_13) && (uint32_t)(sim_ms - rf_down_off) < 12U)));
    if((wired_up || rf_up) && !limit_up) result &= (uint16_t)~GPIO_PIN_3;
    if((wired_down || rf_down) && !limit_down) result &= (uint16_t)~GPIO_PIN_4;
    if(wired_fast) result &= (uint16_t)~GPIO_PIN_5;
    return result;
}
uint16_t gpio_output_port_get(uint32_t p)
{ return (uint16_t)(latch[p] ^ (output_fault && p == GPIOA ? GPIO_PIN_9 : 0U)); }
uint32_t gpio_input_bit_get(uint32_t p, uint32_t pin) { return gpio_input_port_get(p) & pin ? SET : RESET; }

void usart_init(uint32_t baud) { assert(baud == 115200U); }
uint32_t usart_flag_get(uint32_t p, uint32_t f) { assert(p == BSP_USART && f < 8U); return uart_flags[f]; }
void usart_flag_clear(uint32_t p, uint32_t f) { assert(p == BSP_USART && f < 8U); uart_flags[f] = 0U; }
uint16_t usart_data_receive(uint32_t p)
{ assert(p == BSP_USART); uart_flags[USART_FLAG_RBNE] = 0U; return (uint16_t)uart_byte; }
void usart_interrupt_enable(uint32_t p, uint32_t f)
{ assert(p == BSP_USART); if(f == USART_INT_TBE) tx_irq = 1; else assert(f == USART_INT_RBNE); }
void usart_interrupt_disable(uint32_t p, uint32_t f) { assert(p == BSP_USART && f == USART_INT_TBE); tx_irq = 0; }
uint32_t usart_interrupt_flag_get(uint32_t p, uint32_t f)
{ assert(p == BSP_USART && f == USART_INT_FLAG_TBE); return (uint32_t)(tx_irq && uart_ready); }
void usart_data_transmit(uint32_t p, uint16_t v)
{
    assert(p == BSP_USART && output_length + 1U < sizeof(output));
    output[output_length++] = (char)v; output[output_length] = '\0';
    if(v == '\n') ++serial_lines;
}
void nvic_irq_enable(uint32_t irq, uint32_t priority) { assert(irq == USART1_IRQn && priority == 1U); }

BOOL CMT2219B_Init(void) { ++init_calls; return (BOOL)available; }
void CMT2219B_ConfigPolling(void) { flags = 0U; }
BOOL CMT2219B_FifoOverflow(void) { return (BOOL)overflow; }
BOOL CMT2300A_ConfigRegBank(u8 address, const u8 bank[], u8 length)
{ assert(address != 0x55U); memcpy(registers + address, bank, length); return TRUE; }
u8 CMT2300A_ReadReg(u8 address)
{ return (u8)(registers[address] ^ ((corrupt_readback && address == 0x18U) ? 1U : 0U)); }
void CMT2300A_WriteReg(u8 address, u8 v) { registers[address] = v; }
void CMT2300A_SetFrequencyChannel(u8 ch) { registers[CMT2300A_CUS_FREQ_CHNL] = ch; }
void CMT2300A_SetFrequencyStep(u8 step) { registers[CMT2300A_CUS_FREQ_OFS] = step; }
BOOL CMT2300A_GoSleep(void) { status = CMT2300A_STA_SLEEP; return TRUE; }
BOOL CMT2300A_GoStby(void) { if(stby_fail) return FALSE; status = CMT2300A_STA_STBY; return TRUE; }
BOOL CMT2300A_GoRx(void) { status = CMT2300A_STA_RX; return (BOOL)!rx_fail; }
void CMT2300A_EnableFifoMerge(BOOL value) { assert(!value); }
void CMT2300A_EnableRxFifoAutoClear(BOOL value) { assert(value); }
void CMT2300A_EnableInterrupt(u8 f) { assert(f == (CMT2300A_MASK_PKT_DONE_EN | CMT2300A_MASK_CRC_OK_EN)); }
void CMT2300A_EnableReadFifo(void) { }
u8 CMT2300A_ClearRxFifo(void) { overflow = 0; return 0U; }
u8 CMT2300A_ClearInterruptFlags(void) { flags = 0U; return 0U; }
void CMT2300A_ReadFifo(u8 bytes[], u16 length)
{ assert(status == CMT2300A_STA_STBY && length == 8U && (flags & CMT2300A_MASK_CRC_OK_FLG)); memcpy(bytes, packet, 8U); ++fifo_reads; }
u8 CMT2300A_ReadPollingFlags(u8 address) { assert(address == CMT2300A_CUS_INT_FLAG); return flags; }
u8 CMT2300A_GetChipStatus(void) { return status; }

static void flush_output(void) { while(tx_irq && uart_ready) USART1_IRQHandler(); }
static void service(void) { ReceiverApp_Process(); flush_output(); }
static void tick(unsigned ms)
{
    while(ms--) { ++sim_ms; counter2 = (u16)(counter2 + 10U); counter13 = (u16)(counter13 + 2U); service(); }
}
static void reset(int clear_flash)
{
    if(clear_flash) memset(flash, 0xFF, sizeof(flash));
    memset(latch, 0, sizeof(latch)); memset(modes, 0, sizeof(modes));
    memset(registers, 0, sizeof(registers)); memset(uart_flags, 0, sizeof(uart_flags));
    clocks = 0U; irq_mask = 0U; sim_ms = 0U; counter2 = 0U; counter13 = 0U;
    wired_up = wired_down = wired_fast = physical_estop = limit_up = limit_down = no_feedback = 0U;
    rf_up_on = rf_down_on = rf_up_off = rf_down_off = 0U; rf_had_up = rf_had_down = 0U;
    flash_writes = flash_erases = program_calls = init_calls = fifo_reads = serial_lines = 0U;
    flash_unlocked = fail_program = fail_erase = tx_irq = 0; uart_ready = 1;
    available = 1; corrupt_readback = stby_fail = rx_fail = overflow = output_fault = 0;
    flags = 0U; status = CMT2300A_STA_STBY; output_length = 0U; output[0] = '\0';
    image_limit = 0x08006000U;
    ReceiverApp_Init(); flush_output();
}
static void uart_char(u8 c)
{ assert(!irq_mask); uart_byte = c; uart_flags[USART_FLAG_RBNE] = 1U; USART1_IRQHandler(); }
static void command(const char *text)
{ while(*text) uart_char((u8)*text++); uart_char('\r'); uart_char('\n'); service(); service(); }
static void configure(u32 id, u32 ch)
{ char line[48]; snprintf(line, sizeof(line), "SET,%lu,%lu", (unsigned long)id, (unsigned long)ch); command(line); }
static void inject(u32 id, u8 cmd, u8 tag, u8 event_flags)
{
    packet[0] = (u8)(id >> 24); packet[1] = (u8)(id >> 16); packet[2] = (u8)(id >> 8); packet[3] = (u8)id;
    packet[4] = tag; packet[5] = cmd; packet[6] = 0U; packet[7] = (u8)g_demo.total;
    flags = event_flags; status = CMT2300A_STA_STBY; service();
}
static void valid(u8 cmd)
{ inject(g_config.id, cmd, WIRELESS_CONTROL_TAG, CMT2300A_MASK_PKT_OK_FLG | CMT2300A_MASK_CRC_OK_FLG); }
static void held(u8 cmd, unsigned duration)
{ for(unsigned i = 0U; i < duration; ++i) { if(i % 40U == 0U) valid(cmd); tick(1U); } }
static void expect_run(u8 cmd)
{
    assert(g_wired.phase == WIRED_RUN && g_wired.output.allow && !g_wired.output.inhibit);
    assert(g_wired.output.up == !!(cmd & 1U)); assert(g_wired.output.down == !!(cmd & 2U));
    assert(g_wired.output.fast == !!(cmd & 4U));
    assert(!(latch[GPIOB] & GPIO_PIN_6));
}

static void test_wired_and_radio_absence(void)
{
    unsigned before;
    reset(1); assert(!g_config.configured && !g_demo.ready);
    tick(110U); wired_up = 1U; tick(41U); expect_run(1U);
    tick(500U); assert(strstr(output, "you xian model up"));
    assert(!(latch[GPIOB] & WIRED_REQUEST_PINS));
    wired_fast = 1U; tick(125U); expect_run(5U);
    wired_up = 0U; service(); assert(latch[GPIOB] & GPIO_PIN_6);
    tick(100U); assert(!g_wired.output.allow && !g_wired.output.fast);
    wired_fast = 0U; available = 0; configure(1U, 0U);
    tick(150U); wired_down = 1U; tick(41U); expect_run(2U);
    before = init_calls; tick(3000U); expect_run(2U);
    assert(init_calls == before); /* No blocking radio reset during wired RUN. */
    available = 1; wired_down = 0U; tick(110U); assert(g_demo.ready);
    assert(registers[0x27] == 21U && registers[0x46] == 7U);
    assert(strstr(output, "PROTO=A2"));
}

static void test_motion_and_stops(void)
{
    static const u8 commands[] = {1U, 2U, 5U, 6U};
    for(unsigned i = 0U; i < sizeof(commands); ++i) {
        u8 cmd = commands[i];
        reset(1); configure(1U, 0U); tick(110U);
        held(cmd, 240U); expect_run(cmd);
        assert(latch[GPIOB] & ((cmd & 1U) ? GPIO_PIN_12 : GPIO_PIN_13));
        held(cmd, 510U);
        assert(strstr(output, (cmd & 1U) ? "wu xian model up" : "wu xian model down"));
        if(cmd & 4U) {
            assert(g_physical_input.fast == 0U); /* RF FAST is not physical PB5. */
            assert(strstr(output, (cmd & 1U) ? "PB3/4/5=011" : "PB3/4/5=101"));
        }
        valid(0U); assert(latch[GPIOB] & GPIO_PIN_6);
        assert(!(latch[GPIOB] & WIRED_REQUEST_PINS));
        tick(300U); assert(!g_wired.output.allow && !g_wired.output.up && !g_wired.output.down);
        /* 12-ms decaying RF feedback must not be reclassified as wired RUN. */
        assert(!g_remote_applied && !g_remote_transition);
        wired_down = 1U; tick(41U); expect_run(2U);
    }
    /* Reversal and fast changes use the same release/preparation execution. */
    reset(1); configure(1U, 0U); tick(110U); held(1U, 240U); expect_run(1U);
    valid(6U); assert(!g_wired.output.allow && !(latch[GPIOB] & WIRED_REQUEST_PINS));
    held(6U, 49U); assert(g_wired.output.up && !g_wired.output.down);
    held(6U, 150U); expect_run(6U);
    valid(2U); assert(!g_wired.output.allow); held(2U, 210U); expect_run(2U);
}

static void test_timeout_filter_and_feedback(void)
{
    u8 good = CMT2300A_MASK_PKT_OK_FLG | CMT2300A_MASK_CRC_OK_FLG;
    reset(1); configure(1U, 0U); tick(110U); held(1U, 240U); expect_run(1U);
    valid(1U); tick(84U); assert(g_wired.output.allow);
    inject(2U, 1U, 0xA2U, good); inject(1U, 1U, 0xA1U, good);
    inject(1U, 1U, 0xA2U, CMT2300A_MASK_PKT_OK_FLG);
    for(unsigned cmd = 0; cmd < 256U; ++cmd)
        if(cmd != 0U && cmd != 1U && cmd != 2U && cmd != 5U && cmd != 6U && cmd != 8U)
            inject(1U, (u8)cmd, 0xA2U, good);
    tick(1U);
    assert(!g_wired.output.allow && !(latch[GPIOB] & WIRED_REQUEST_PINS));
    assert(WirelessDemo_GetCommand() == 0U && strstr(output, "EVENT STOP TIMEOUT"));
    tick(300U); assert(!g_wired.output.allow);

    /* Valid RF intention must never synthesize direction/limit feedback. */
    reset(1); configure(1U, 0U); tick(110U); no_feedback = 1U;
    held(5U, 400U); assert(!g_wired.output.allow && !g_wired.output.up && !g_wired.output.fast);
    no_feedback = 0U; held(5U, 120U); expect_run(5U);
    limit_up = 1U; service(); assert(!g_wired.output.allow);
    held(5U, 200U); assert(!g_wired.output.allow);
    limit_up = 0U; held(5U, 120U); expect_run(5U);
    physical_estop = 1U; service(); assert(!g_wired.output.allow);
    held(5U, 200U); assert(!g_wired.output.allow);
    physical_estop = 0U; held(5U, 120U); expect_run(5U);
    /* No fresh RF data at restore: remain stopped. */
    physical_estop = 1U; service(); tick(100U); physical_estop = 0U; tick(250U);
    assert(!g_wired.output.allow && !(latch[GPIOB] & WIRED_REQUEST_PINS));
}

static void test_faults_maintenance_and_uart(void)
{
    unsigned before;
    uint32_t saved[512];
    reset(1); configure(7U, 1U); tick(110U); held(6U, 240U); expect_run(6U);
    rx_fail = 1; status = CMT2300A_STA_STBY; service();
    assert(!g_demo.ready && !g_wired.output.allow && !(latch[GPIOB] & WIRED_REQUEST_PINS));
    rx_fail = 0; tick(1100U); assert(g_demo.ready); held(1U, 200U); expect_run(1U);
    /* Stop execution before ALL config Flash writes; old journal survives failure. */
    memcpy(saved, flash, sizeof(saved));
    for(int f = 1; f <= 8; ++f) {
        memcpy(flash, saved, sizeof(saved)); reset(0); tick(110U);
        held(5U, 220U); expect_run(5U); fail_program = f;
        configure(8U, 2U); assert(g_config.id == 7U && !g_wired.output.allow);
        reset(0); assert(g_config.id == 7U && g_config.channel == 1U);
    }
    configure(9U, 2U); tick(150U); before = flash_writes;
    configure(9U, 2U); assert(flash_writes == before);
    held(2U, 240U); expect_run(2U);
    command("DEFAULT"); assert(!g_config.configured && !g_wired.output.allow);
    tick(150U); wired_up = 1U; tick(41U); expect_run(1U);
    /* Receiving serial junk does not change settings, motion or pair state. */
    before = flash_writes; command("SET,0,0"); command("SET,1,6"); command("SET,-1,0");
    command("SET,4294967296,0"); command("SET,1,0junk"); assert(flash_writes == before);
    for(unsigned i = 0U; i < 150U; ++i) uart_char('X');
    service(); uart_char('\n'); service(); command("GET"); expect_run(1U);
    assert(strstr(output, "PAIRED=0") && strstr(output, "ERR ARG"));
    /* Shared log queue backpressure cannot stall a hardware-input release. */
    uart_ready = 0; for(unsigned i = 0; i < 30U; ++i) (void)WirelessDemo_Log("A complete queued diagnostic line\r\n");
    assert(g_demo.log_drops); wired_up = 0U; service(); assert(!g_wired.output.allow);
    tick(100U); uart_ready = 1; flush_output();
    wired_down = 1U; tick(41U); expect_run(2U);
    output_fault = 1; service(); assert(g_output_fault && !g_wired.output.allow);
    assert(latch[GPIOB] & GPIO_PIN_11); assert(!(latch[GPIOB] & WIRED_REQUEST_PINS));
    output_fault = 0; WiredControl_SetSoftwareStop(0U); tick(200U);
    assert(!g_wired.output.allow && (latch[GPIOB] & GPIO_PIN_11));
}

static void expect_estop(void)
{
    assert(g_remote_estop && !g_wired.output.allow && g_wired.output.inhibit);
    assert((latch[GPIOB] & (GPIO_PIN_6 | GPIO_PIN_11)) == (GPIO_PIN_6 | GPIO_PIN_11));
    assert(!(latch[GPIOB] & WIRED_REQUEST_PINS));
}

static void test_wireless_estop(void)
{
    const u8 good = CMT2300A_MASK_PKT_OK_FLG | CMT2300A_MASK_CRC_OK_FLG;
    static const u8 motion[] = {1U, 2U, 5U, 6U};
    /* Global priority over every RF direction/speed, sustained by fresh packets. */
    for(unsigned i = 0U; i < sizeof(motion); ++i) {
        reset(1); configure(1U, 0U); tick(110U);
        held(motion[i], 240U); expect_run(motion[i]);
        valid(WIRELESS_CMD_ESTOP); expect_estop();
        held(WIRELESS_CMD_ESTOP, 620U); expect_estop();
        assert(!g_wired.output.up && !g_wired.output.down && !g_wired.output.fast);
        assert(strstr(output, "EVENT ESTOP PACKET") && strstr(output, "RF=ESTOP"));
        held(motion[i], 300U); expect_run(motion[i]); /* Current valid state resumes. */
    }
    /* The same valid E-stop also stops wired RUN; STOP releases only RF E-stop. */
    reset(1); configure(1U, 0U); tick(150U); wired_up = wired_fast = 1U;
    tick(41U); expect_run(5U);
    valid(WIRELESS_CMD_ESTOP); expect_estop(); held(WIRELESS_CMD_ESTOP, 300U);
    valid(0U); tick(300U); expect_run(5U);
    /* 84 ms retains E-stop. Bad CRC/ID/tag/command cannot release or renew it. */
    valid(WIRELESS_CMD_ESTOP); tick(84U); expect_estop();
    inject(2U, 0U, 0xA2U, good); inject(1U, 0U, 0xA1U, good);
    inject(1U, 0U, 0xA2U, CMT2300A_MASK_PKT_OK_FLG);
    for(unsigned cmd = 0U; cmd < 256U; ++cmd)
        if(cmd != 0U && cmd != 1U && cmd != 2U && cmd != 5U && cmd != 6U && cmd != 8U)
            inject(1U, (u8)cmd, 0xA2U, good);
    expect_estop(); tick(1U);
    assert(!g_remote_estop && WirelessDemo_GetCommand() == 0U);
    tick(300U); expect_run(5U); /* Loss restores wired without a release packet. */
    /* No wired request on loss means idle; stale RF direction never restarts. */
    wired_up = wired_fast = 0U; tick(100U);
    held(6U, 240U); expect_run(6U); valid(WIRELESS_CMD_ESTOP); tick(400U);
    assert(!g_remote_estop && !g_wired.output.allow && !g_wired.output.fast);
    assert(!(latch[GPIOB] & WIRED_REQUEST_PINS));
    /* Physical E-stop is independent; RF expiry cannot override that circuit. */
    wired_down = 1U; tick(41U); expect_run(2U);
    valid(WIRELESS_CMD_ESTOP); physical_estop = 1U; tick(400U);
    assert(!g_remote_estop && !g_wired.output.allow);
    assert(!(latch[GPIOB] & GPIO_PIN_11)); /* Software released, hardware still blocks. */
    physical_estop = 0U; tick(150U); expect_run(2U);
    /* Radio faults also return to wired, rather than leaving a stale inhibit. */
    valid(WIRELESS_CMD_ESTOP); rx_fail = 1; status = CMT2300A_STA_STBY; service();
    assert(!g_demo.ready && !g_remote_estop); tick(300U); expect_run(2U);
    rx_fail = 0; wired_down = 0U; tick(1300U); assert(g_demo.ready);
    /* E-stop renewal must remain nonblocking even with a full UART queue. */
    uart_ready = 0;
    for(unsigned i = 0U; i < 30U; ++i)
        (void)WirelessDemo_Log("A complete queued diagnostic line\r\n");
    held(WIRELESS_CMD_ESTOP, 1200U); expect_estop();
    assert(g_demo.log_drops); uart_ready = 1; flush_output();
    /* Clearing RF E-stop must never clear a separate maintenance inhibit. */
    WiredControl_SetSoftwareStop(1U); tick(100U);
    assert(!g_remote_estop && g_software_stop && (latch[GPIOB] & GPIO_PIN_11));
    valid(WIRELESS_CMD_ESTOP); expect_estop(); valid(0U); tick(200U);
    assert(g_software_stop && (latch[GPIOB] & GPIO_PIN_11));
    WiredControl_SetSoftwareStop(0U); tick(150U);
    /* Explicit SET/DEFAULT maintenance and a sticky output fault stay separate. */
    valid(WIRELESS_CMD_ESTOP); configure(1U, 0U); tick(200U);
    assert(!g_remote_estop && !(latch[GPIOB] & GPIO_PIN_11));
    valid(WIRELESS_CMD_ESTOP); output_fault = 1; service();
    output_fault = 0; tick(200U);
    assert(!g_remote_estop && g_output_fault && (latch[GPIOB] & GPIO_PIN_11));
    /* Receiver restart forgets old RF state; a new packet determines E-stop. */
    reset(0); tick(110U); assert(!g_remote_estop && WirelessDemo_GetCommand() == 0U);
    wired_up = 1U; tick(41U); expect_run(1U);
    valid(WIRELESS_CMD_ESTOP); expect_estop(); reset(0); tick(110U);
    valid(0U); assert(!g_remote_estop); wired_up = 1U; tick(41U); expect_run(1U);
    /* E-stop expiry remains correct across the 32-bit radio time wrap. */
    g_demo.ticks = UINT32_MAX - 200U; valid(WIRELESS_CMD_ESTOP);
    tick(84U); expect_estop(); tick(1U); assert(!g_remote_estop);
    tick(300U); expect_run(1U);
}

static void test_storage_and_rollover(void)
{
    reset(1); for(u32 id = 1U; id <= 33U; ++id) configure(id, id % 6U);
    assert(g_config.id == 33U && flash_erases >= 1U);
    reset(0); assert(g_config.id == 33U && g_config.channel == 3U);
    flash[(g_demo.saved_address - DEMO_FLASH_FIRST) / 4U + 3U] ^= 1U;
    reset(0); assert(g_config.id == 32U);
    command("SET,4294967295,5"); assert(g_config.id == UINT32_MAX);
    tick(150U); held(6U, 40000U); expect_run(6U);
    assert(g_now_ms == sim_ms); /* TIMER2 and TIMER13 both wrapped. */
    valid(0U); tick(150U);
    g_now_ms = UINT32_MAX - 20U; g_demo.ticks = UINT32_MAX - 200U;
    held(1U, 250U); expect_run(1U);
    valid(1U); tick(85U); assert(!g_wired.output.allow);
    reset(1); image_limit = DEMO_FLASH_FIRST + 1U; ReceiverApp_Init();
    configure(1U, 0U); assert(!g_demo.storage_ok && !flash_writes && !g_demo.ready);
    tick(110U); wired_up = 1U; tick(41U); expect_run(1U);
}

int main(void)
{
    test_wired_and_radio_absence(); test_motion_and_stops(); test_timeout_filter_and_feedback();
    test_faults_maintenance_and_uart(); test_wireless_estop(); test_storage_and_rollover();
    puts("PASS integrated real RX + GPIO + execution: A2 motion/STOP/wireless E-stop, global E-stop priority, 85ms E-stop expiry restores wired, physical E-stop independent, release/reboot/fault/maintenance isolation, wired regression/unpaired/radio absent, feedback gating, no stale RF restart, reversal/fast timing, CRC/ID/tag/all invalid commands, journal power loss, UART backpressure, timer rollover.");
    return 0;
}
