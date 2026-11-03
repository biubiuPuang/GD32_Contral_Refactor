/* Hardware model shared by the existing sender regression and sleep tests. */
static int lp_auto_pump, lp_timer_running, lp_uart_enabled, lp_rx_irq, lp_tc_ready;
static int lp_clock_source, lp_rtc_fail, lp_osc_fail, lp_clock_switch_fail;
static int lp_radio_sleep_fail, lp_wake_stby_fail, lp_short_key;
static unsigned lp_sleep_calls, lp_rtc_events, lp_feed_calls, lp_system_resets;
static unsigned lp_rtc_before_key, lp_spurious_events, lp_drain_key_after;
static unsigned lp_drain_uart_after;
static uint32_t lp_exti_pending, lp_exti_enabled, lp_exti_falling, lp_exti_rising;
static uint32_t lp_nvic_enabled, lp_wake_pin;
static uint32_t lp_boundary_pin;
static uint8_t lp_exti_ports[16];
static int lp_rtc_alarm_flag, lp_watchdog_check;
static unsigned lp_tc_delay_reads, lp_tc_low_observed;
static int lp_tc_stall_after_entry;
static uint64_t lp_elapsed_us, lp_last_feed_us;
uint32_t SystemCoreClock = 72000000U;

static void lp_dispatch_pending(void)
{
#if WIRELESS_DEMO_TX
    if(irq_mask) return;
    if(lp_exti_pending & EXTI_17) RTC_IRQHandler();
    if(lp_exti_pending & EXTI_3) EXTI2_3_IRQHandler();
    if(lp_exti_pending & (EXTI_4|EXTI_5|EXTI_6|EXTI_8|EXTI_9|EXTI_10))
        EXTI4_15_IRQHandler();
#endif
}

static void lp_drain_key_event(void)
{
    if(lp_drain_uart_after && output_length >= lp_drain_uart_after) {
        lp_drain_uart_after=0;
        uart_byte='G';uart_flags[USART_FLAG_RBNE]=1;USART1_IRQHandler();
    }
    if(lp_drain_key_after && output_length >= lp_drain_key_after) {
        lp_drain_key_after=0;
        lp_exti_pending |= lp_wake_pin;
        lp_dispatch_pending();
    }
}

static void lp_reset(void)
{
    lp_auto_pump=lp_timer_running=lp_uart_enabled=lp_rx_irq=0;
    lp_tc_ready=1;lp_clock_source=RCU_SCSS_PLL;
    lp_rtc_fail=lp_osc_fail=lp_clock_switch_fail=lp_radio_sleep_fail=0;
    lp_wake_stby_fail=lp_short_key=lp_rtc_alarm_flag=lp_watchdog_check=0;
    lp_sleep_calls=lp_rtc_events=lp_feed_calls=lp_system_resets=0;
    lp_rtc_before_key=3;lp_spurious_events=lp_drain_key_after=lp_drain_uart_after=0;
    lp_exti_pending=lp_exti_enabled=lp_exti_falling=lp_exti_rising=lp_nvic_enabled=0;
    lp_wake_pin=GPIO_PIN_9;lp_boundary_pin=0;memset(lp_exti_ports,0xFF,sizeof(lp_exti_ports));
    lp_elapsed_us=10000U;lp_last_feed_us=0;SystemCoreClock=72000000U;
    lp_tc_delay_reads=lp_tc_low_observed=0;lp_tc_stall_after_entry=0;
}

void __DSB(void)
{ if(lp_boundary_pin) { lp_exti_pending|=lp_boundary_pin;lp_boundary_pin=0; } }
void __ISB(void) {}
void NVIC_ClearPendingIRQ(uint32_t irq) { assert(irq==USART1_IRQn); }
void NVIC_SystemReset(void) { ++lp_system_resets; }
void timer_disable(uint32_t timer) { assert(timer==TIMER2);lp_timer_running=0; }
void usart_enable(uint32_t port) { assert(port==BSP_USART);lp_uart_enabled=1; }
void usart_disable(uint32_t port) { assert(port==BSP_USART);lp_uart_enabled=0; }
void rcu_osci_on(uint32_t oscillator)
{ assert(oscillator==RCU_IRC8M || oscillator==RCU_IRC40K || oscillator==RCU_PLL_CK); }
ErrStatus rcu_osci_stab_wait(uint32_t oscillator)
{ return lp_osc_fail==(int)oscillator?ERROR:SUCCESS; }
uint32_t rcu_system_clock_source_get(void) { return (uint32_t)lp_clock_source; }
void rcu_system_clock_source_config(uint32_t source)
{ assert(source==RCU_CKSYSSRC_PLL);if(!lp_clock_switch_fail)lp_clock_source=RCU_SCSS_PLL; }
void rcu_pll_config(uint32_t source,uint32_t multiplier)
{ assert(lp_clock_source!=RCU_SCSS_PLL && source==RCU_PLLSRC_IRC8M_DIV2 && multiplier==RCU_PLL_MUL18); }
void fmc_wscnt_set(uint8_t states) { assert(states==WS_WSCNT_2); }
void SystemCoreClockUpdate(void)
{ SystemCoreClock=lp_clock_source==RCU_SCSS_PLL?72000000U:8000000U; }
void delay_1ms(uint32_t ms)
{
    assert(SystemCoreClock==(lp_clock_source==RCU_SCSS_PLL?72000000U:8000000U));
    lp_elapsed_us += 1000U*ms;
    if(lp_timer_running)counter=(uint16_t)(counter+10U*ms);
}
void fwdgt_counter_reload(void)
{
    if(lp_watchdog_check) {
        assert(lp_elapsed_us-lp_last_feed_us>=500U);
        assert(lp_elapsed_us-lp_last_feed_us<2000000U);
    }
    lp_last_feed_us=lp_elapsed_us;++lp_feed_calls;
}
void pmu_backup_write_enable(void) {}
void rcu_bkp_reset_enable(void) {}
void rcu_bkp_reset_disable(void) {}
void rcu_rtc_clock_config(uint32_t source) { assert(source==RCU_RTCSRC_IRC40K); }
ErrStatus rtc_register_sync_wait(void) { return lp_rtc_fail?ERROR:SUCCESS; }
ErrStatus rtc_init(rtc_parameter_struct *p)
{ assert(p->rtc_factor_asyn==124U && p->rtc_factor_syn==319U && p->rtc_date==1U);return SUCCESS; }
ErrStatus rtc_alarm_disable(void) { return SUCCESS; }
void rtc_alarm_config(rtc_alarm_struct *p)
{ assert(p->rtc_alarm_mask==RTC_ALARM_ALL_MASK && p->rtc_weekday_or_date==RTC_ALARM_DATE_SELECTED); }
void rtc_alarm_subsecond_config(uint32_t mask,uint32_t subsecond)
{ assert(mask==RTC_MASKSSC_0_14 && subsecond==0U); }
void rtc_flag_clear(uint32_t flag) { assert(flag==RTC_FLAG_ALARM0);lp_rtc_alarm_flag=0; }
uint32_t rtc_flag_get(uint32_t flag) { assert(flag==RTC_FLAG_ALARM0);return (uint32_t)lp_rtc_alarm_flag; }
void rtc_interrupt_enable(uint32_t interrupt) { assert(interrupt==RTC_INT_ALARM); }
void rtc_alarm_enable(void) {}
void syscfg_exti_line_config(uint8_t port,uint8_t pin)
{ assert(pin<16 && port<=EXTI_SOURCE_GPIOB);lp_exti_ports[pin]=port; }
void exti_init(exti_line_enum line,uint32_t mode,uint32_t trigger)
{
    assert(mode==EXTI_INTERRUPT);lp_exti_enabled|=line;
    if(trigger==EXTI_TRIG_FALLING)lp_exti_falling|=line;
    else { assert(trigger==EXTI_TRIG_RISING);lp_exti_rising|=line; }
}
uint32_t exti_interrupt_flag_get(exti_line_enum line) { return (lp_exti_pending&line)?SET:RESET; }
void exti_interrupt_flag_clear(exti_line_enum line) { lp_exti_pending&=~line; }
void pmu_to_deepsleepmode(uint32_t ldo,uint8_t command)
{
    assert(ldo==PMU_LDO_LOWPOWER && command==WFI_CMD && irq_mask);
    assert(!lp_timer_running && !lp_uart_enabled && !lp_rx_irq && !tx_irq);
    assert(!lp_tc_delay_reads);
#if WIRELESS_DEMO_TX
    assert(!g_radio_initialized || status==CMT2300A_STA_SLEEP);
#endif
    assert(strstr(output,"input sleep model\r\n"));
    assert(!strstr(output,"exit sleep model\r\n"));
    ++lp_sleep_calls;
    if(lp_exti_pending) return; /* WFI在已有待处理中断时立即返回。 */
    lp_clock_source=0;
    if(lp_spurious_events) { --lp_spurious_events;lp_elapsed_us+=100U;return; }
    lp_elapsed_us+=1000000U;
    if(lp_watchdog_check)assert(lp_elapsed_us-lp_last_feed_us<2000000U);
    if(lp_rtc_events<lp_rtc_before_key) {
        ++lp_rtc_events;lp_rtc_alarm_flag=1;lp_exti_pending|=EXTI_17;
    } else {
        if(!lp_short_key)pins&=~lp_wake_pin;
        lp_exti_pending|=lp_wake_pin;
        if(lp_wake_stby_fail)stby_fail=1;
    }
}
