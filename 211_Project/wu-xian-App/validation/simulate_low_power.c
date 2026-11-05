/* Test real sender code, retaining the complete pre-existing RF/key regression. */
#define main sender_regression_main
#include "simulate_multi.c"
#undef main

static const u32 lp_keys[] = {
    GPIO_PIN_3, GPIO_PIN_4,
    GPIO_PIN_8, GPIO_PIN_9, GPIO_PIN_10
};

static unsigned lp_occurrences(const char *needle)
{
    unsigned count=0;
    const char *at=output;
    while((at=strstr(at,needle))!=NULL) { ++count;at+=strlen(needle); }
    return count;
}

static void lp_clear_output(void) { flush_output();output_length=0;output[0]=0; }

static void lp_prepare(void)
{
    reset(1);configure(17,49);advance(101U);drain_stops();flush_output();
    assert(g_demo.ready && !g_demo.busy && !g_demo.stops_left && g_rtc_ready);
    assert(g_demo.estop_ready && !g_demo.button_command && !Demo_AnyKeyPressed());
    g_idle_ready=TRUE;
    lp_clear_output();lp_auto_pump=1;lp_watchdog_check=1;
    lp_last_feed_us=lp_elapsed_us-10000U;
}

static void lp_advance_idle(u32 ticks)
{
    while(ticks) {
        u16 step=(u16)(ticks>10000U?10000U:ticks);
        counter=(u16)(counter+step);ticks-=step;Demo_ButtonProcess();
    }
}

static void check_idle_timing_and_all_inputs(void)
{
    reset(1);lp_advance_idle(100U); /* Confirm startup E-stop state. */
    g_idle_since=Demo_Now();
    lp_advance_idle(DEMO_IDLE_TICKS-1U);assert(!g_idle_ready);
    lp_advance_idle(1U);assert(g_idle_ready);
    for(unsigned i=0;i<sizeof(lp_keys)/sizeof(lp_keys[0]);++i) {
        pins&=~lp_keys[i];Demo_ButtonProcess();assert(!g_idle_ready);
        lp_advance_idle(DEMO_IDLE_TICKS+100U);assert(!g_idle_ready);
        pins|=lp_keys[i];Demo_ButtonProcess();lp_advance_idle(100U);
        assert(!g_demo.button_bits && !g_demo.estop_pressed);
        lp_advance_idle(DEMO_IDLE_TICKS-1U);assert(!g_idle_ready);
        lp_advance_idle(1U);assert(g_idle_ready);
    }
    /* Time subtraction must also work across the software 32-bit wrap. */
    g_demo.ticks=UINT32_MAX-50000U;g_idle_since=g_demo.ticks;g_idle_ready=FALSE;
    lp_advance_idle(DEMO_IDLE_TICKS-1U);assert(!g_idle_ready);
    lp_advance_idle(1U);assert(g_idle_ready);
    /* A completed short pulse is still activity even if GPIO is high now. */
    lp_exti_pending|=EXTI_3;lp_dispatch_pending();assert(g_key_wakeup);
    Demo_ButtonProcess();assert(!g_idle_ready && !g_key_wakeup);
    /* Removed PB5/PB6 inputs neither block sleep nor restart the idle timer. */
    pins&=~(GPIO_PIN_5|GPIO_PIN_6);Demo_ButtonProcess();
    assert(!Demo_AnyKeyPressed() && !g_idle_ready);
    lp_advance_idle(DEMO_IDLE_TICKS);assert(g_idle_ready);
    puts("PASS: 180s threshold, all five held/released inputs, ignored PB5/PB6, wrap and latched short pulse");
}

static void check_complete_irq_configuration(void)
{
    reset(1);
    u32 mask=EXTI_3|EXTI_4|EXTI_8|EXTI_9|EXTI_10;
    assert(lp_exti_enabled==(mask|EXTI_17));
    assert(lp_exti_falling==mask && lp_exti_rising==EXTI_17);
    for(unsigned i=0;i<sizeof(lp_keys)/sizeof(lp_keys[0]);++i) {
        unsigned line=0;while((1U<<line)!=lp_keys[i])++line;
        assert(lp_exti_ports[line]==(line<8U?EXTI_SOURCE_GPIOB:EXTI_SOURCE_GPIOA));
        lp_exti_pending|=lp_keys[i];lp_dispatch_pending();
        assert(g_key_wakeup && !(lp_exti_pending&lp_keys[i]));g_key_wakeup=0;
    }
    EXTI2_3_IRQHandler();EXTI4_15_IRQHandler();assert(!g_key_wakeup);
    assert((lp_nvic_enabled&((1U<<RTC_IRQn)|(1U<<EXTI2_3_IRQn)|(1U<<EXTI4_15_IRQn)))!=0);
    puts("PASS: every key mapping/IRQ and RTC EXTI17 configuration");
}

static void check_entry_guards(void)
{
    lp_prepare();g_idle_ready=FALSE;Demo_LowPowerProcess();assert(!lp_sleep_calls);
    g_idle_ready=TRUE;g_rtc_ready=FALSE;Demo_LowPowerProcess();assert(!lp_sleep_calls);
    g_rtc_ready=TRUE;g_demo.stops_left=1;Demo_LowPowerProcess();assert(!lp_sleep_calls);
    g_demo.stops_left=0;g_demo.busy=TRUE;Demo_LowPowerProcess();assert(!lp_sleep_calls);
    g_demo.busy=FALSE;g_demo.button_command=WIRELESS_CMD_ESTOP;
    Demo_LowPowerProcess();assert(!lp_sleep_calls);g_demo.button_command=0;
    for(unsigned i=0;i<sizeof(lp_keys)/sizeof(lp_keys[0]);++i) {
        pins&=~lp_keys[i];Demo_LowPowerProcess();assert(!lp_sleep_calls);pins|=lp_keys[i];
    }
    g_key_wakeup=1;Demo_LowPowerProcess();assert(!lp_sleep_calls);g_key_wakeup=0;
    g_demo.line_size=1;Demo_LowPowerProcess();assert(!lp_sleep_calls);g_demo.line_size=0;
    lp_tc_ready=0;Demo_LowPowerProcess();assert(!lp_sleep_calls);lp_tc_ready=1;
    g_uart_head=1;Demo_LowPowerProcess();assert(!lp_sleep_calls);g_uart_head=0;
    g_demo.ready=FALSE;Demo_LowPowerProcess();assert(!lp_sleep_calls);g_demo.ready=TRUE;
    irq_mask=1;Demo_LowPowerProcess();assert(!lp_sleep_calls && irq_mask);irq_mask=0;
    assert(!lp_occurrences("input sleep model\r\n"));
    puts("PASS: no sleep with held key, active TX/STOP, RTC/radio fault, UART command or masked IRQ");
}

static void check_sleep_and_each_wake_input(void)
{
    for(unsigned i=0;i<sizeof(lp_keys)/sizeof(lp_keys[0]);++i) {
        lp_prepare();lp_wake_pin=lp_keys[i];
        u16 saved_counter=counter;
        unsigned saved_packets=fifo_writes;
        unsigned saved_toggles=led_toggles;
        Demo_LowPowerProcess();flush_output();
        assert(!led_high && led_toggles==saved_toggles);
        assert(lp_sleep_calls==4 && lp_rtc_events==3 && lp_feed_calls==4);
        assert(lp_occurrences("input sleep model\r\n")==1);
        assert(lp_occurrences("exit sleep model\r\n")==1);
        assert(!strstr(output,"no key press model\r\nno key press model"));
        assert(SystemCoreClock==72000000U && lp_timer_running && lp_uart_enabled && lp_rx_irq);
        assert(status==CMT2300A_STA_STBY && !g_idle_ready && !lp_system_resets);
        assert(g_config.id==17U && g_config.channel==49U && g_config.configured);
        assert((u16)(counter-saved_counter)<200U && fifo_writes==saved_packets);
        assert(g_key_wakeup);Demo_ButtonProcess();
        counter=(u16)(counter+101U);Demo_ButtonProcess();
        u8 expected=lp_keys[i]==GPIO_PIN_8?WIRELESS_CMD_ESTOP:
            (lp_keys[i]&(GPIO_PIN_3|GPIO_PIN_4))?WIRELESS_CMD_UP:WIRELESS_CMD_DOWN;
        assert(g_demo.button_command==expected);
        Demo_LedProcess();
        assert(led_high && g_demo.led_mode==
               (expected==WIRELESS_CMD_ESTOP?DEMO_LED_ALERT:DEMO_LED_TX));
        Demo_TxProcess();assert(g_demo.busy && packet[5]==expected);
    }
    puts("PASS: all five inputs wake, only one entry/exit log, restored clocks/UART/radio and correct command");
}

static void check_short_wake_and_races(void)
{
    for(unsigned i=0;i<sizeof(lp_keys)/sizeof(lp_keys[0]);++i) {
        lp_prepare();lp_wake_pin=lp_keys[i];lp_short_key=1;
        Demo_LowPowerProcess();flush_output();
        assert(!Demo_AnyKeyPressed() && g_key_wakeup && !g_idle_ready);
        assert(lp_occurrences("exit sleep model\r\n")==1);
        Demo_ButtonProcess();assert(!g_idle_ready && !g_demo.button_command);
    }
    lp_prepare();lp_short_key=1;lp_boundary_pin=EXTI_10;
    Demo_LowPowerProcess();flush_output();
    assert(lp_sleep_calls==1 && !lp_rtc_events && g_key_wakeup);
    assert(lp_occurrences("exit sleep model\r\n")==1);
    lp_prepare();lp_drain_key_after=4;
    Demo_LowPowerProcess();flush_output();
    assert(!lp_sleep_calls && g_key_wakeup && !g_idle_ready);
    assert(lp_occurrences("input sleep model\r\n")==1 && lp_occurrences("exit sleep model\r\n")==1);
    assert(lp_uart_enabled && lp_timer_running && status==CMT2300A_STA_STBY);
    lp_prepare();lp_drain_uart_after=4;
    Demo_LowPowerProcess();flush_output();
    assert(!lp_sleep_calls && g_uart_head!=g_uart_tail && lp_uart_enabled);
    Demo_UartProcess();assert(g_demo.line_size==1U);
    uart_char('E');uart_char('T');uart_char('\r');Demo_UartProcess();flush_output();
    assert(strstr(output,"CFG ID=17 CH=49 FREQ=434.7175MHz"));
    puts("PASS: released short press, pending edge at WFI boundary, key during entry-log drain");
}

static void check_uart_completion_and_repeated_sessions(void)
{
    lp_prepare();lp_tc_delay_reads=25;
    Demo_LowPowerProcess();flush_output();
    assert(lp_tc_low_observed==25 && lp_sleep_calls==4 && lp_uart_enabled);
    lp_prepare();lp_tc_stall_after_entry=1;
    Demo_LowPowerProcess();flush_output();
    assert(!lp_sleep_calls && !g_idle_ready && lp_timer_running && lp_uart_enabled);
    assert(lp_occurrences("input sleep model\r\n")==1 && lp_occurrences("exit sleep model\r\n")==1);
    lp_prepare();lp_short_key=1;
    unsigned saved_flash=flash_writes;
    for(unsigned i=0;i<3U;++i) {
        lp_clear_output();g_idle_ready=TRUE;g_key_wakeup=0U;
        lp_sleep_calls=lp_rtc_events=0;
        Demo_LowPowerProcess();flush_output();
        assert(lp_sleep_calls==4 && !g_idle_ready && !lp_system_resets);
        assert(lp_occurrences("input sleep model\r\n")==1 && lp_occurrences("exit sleep model\r\n")==1);
    }
    command("GET");
    assert(strstr(output,"CFG ID=17 CH=49 FREQ=434.7175MHz"));
    assert(flash_writes==saved_flash);
    puts("PASS: UART TC/stop-bit wait and timeout, repeated sleep sessions, pairing/GET after wake");
}

static void check_unpaired_sleep(void)
{
    reset(1);advance(101U);lp_clear_output();
    assert(!g_config.configured && !g_radio_initialized && !init_calls);
    g_idle_ready=TRUE;lp_auto_pump=1;lp_short_key=1;
    unsigned saved_toggles=led_toggles;
    Demo_LowPowerProcess();flush_output();
    assert(!led_high && led_toggles==saved_toggles+1U && g_demo.led_mode==DEMO_LED_ALERT);
    assert(lp_sleep_calls==4 && !g_config.configured && !g_demo.ready && !init_calls);
    assert(lp_occurrences("exit sleep model\r\n")==1);
    lp_prepare();command("DEFAULT");lp_clear_output();
    assert(!g_config.configured && g_radio_initialized);
    g_idle_ready=TRUE;lp_short_key=1;
    Demo_LowPowerProcess();flush_output();
    assert(lp_sleep_calls==4 && !g_config.configured && !g_demo.ready);
    assert(status==CMT2300A_STA_STBY);
    puts("PASS: unpaired startup and DEFAULT also sleep without transmitting control");
}

static void check_watchdog_and_failure_paths(void)
{
    lp_prepare();lp_rtc_before_key=3600;lp_spurious_events=1;
    Demo_LowPowerProcess();flush_output();
    assert(lp_rtc_events==3600 && lp_feed_calls==3601 && !lp_system_resets);
    assert(lp_occurrences("exit sleep model\r\n")==1);
    lp_prepare();lp_wake_stby_fail=1;Demo_LowPowerProcess();flush_output();
    assert(!g_demo.ready && !strcmp(g_demo.error,"WAKE_STBY") && lp_uart_enabled);
    lp_prepare();lp_radio_sleep_fail=1;Demo_LowPowerProcess();
    assert(!lp_sleep_calls && !g_demo.ready && !strcmp(g_demo.error,"IDLE_SLEEP"));
    lp_prepare();lp_osc_fail=RCU_PLL_CK;Demo_LowPowerProcess();assert(lp_system_resets==1);
    lp_prepare();lp_clock_switch_fail=1;Demo_LowPowerProcess();assert(lp_system_resets==1);
    lp_prepare();lp_rtc_fail=1;g_rtc_ready=Demo_RtcInit();Demo_LowPowerProcess();
    assert(!g_rtc_ready && !lp_sleep_calls);
    lp_prepare();lp_auto_pump=0;g_watchdog_at=Demo_Now();
    counter=(u16)(counter+99U);Demo_WatchdogFeed();assert(lp_feed_calls==0);
    counter=(u16)(counter+1U);lp_elapsed_us+=10000U;Demo_WatchdogFeed();assert(lp_feed_calls==1);
    Demo_WatchdogFeed();assert(lp_feed_calls==1);
    puts("PASS: virtual hour of RTC feeds, no feed on spurious wake, radio/RTC/PLL failures and reload spacing");
}

static void check_led_states_and_timing(void)
{
    /* Fresh/unconfigured board alerts even with every key released. */
    reset(1);assert(led_output_ready && !led_high);
    unsigned toggles=led_toggles;
    log_ticks(1999U);assert(!led_high && led_toggles==toggles);
    log_ticks(1U);assert(led_high && led_toggles==toggles+1U);
    log_ticks(2000U);assert(!led_high && led_toggles==toggles+2U);

    configure(1U,0U);drain_stops();assert(!led_high);
    toggles=led_toggles;log_ticks(10000U);
    assert(!led_high && led_toggles==toggles); /* Idle stays ON. */
    keys(GPIO_PIN_3);assert(led_high && led_toggles==toggles+1U);
    toggles=led_toggles;
    log_ticks(4999U);assert(led_high && led_toggles==toggles);
    log_ticks(1U);assert(!led_high && led_toggles==toggles+1U);
    log_ticks(5000U);assert(led_high && led_toggles==toggles+2U);
    keys(0);assert(!led_high && g_demo.stops_left); /* Release overrides STOP tail. */
    drain_stops();
    keys(GPIO_PIN_3|GPIO_PIN_4);assert(led_high && g_demo.led_mode==DEMO_LED_TX);
    keys(0);drain_stops(); /* Deep press uses normal sending indication. */
    keys(GPIO_PIN_5|GPIO_PIN_6);assert(!led_high && g_demo.led_mode==DEMO_LED_IDLE);

    /* E-stop wins over motion and changes to the 200ms period immediately. */
    keys(GPIO_PIN_3);assert(led_high);
    toggles=led_toggles;keys(GPIO_PIN_3|GPIO_PIN_8);
    assert(!led_high && led_toggles==toggles+1U);
    toggles=led_toggles;
    log_ticks(1999U);assert(!led_high && led_toggles==toggles);
    log_ticks(1U);assert(led_high && led_toggles==toggles+1U);
    keys(GPIO_PIN_3);assert(!led_high && g_demo.led_mode==DEMO_LED_TX);
    keys(0);drain_stops();

    /* Direction conflict is detected on the first raw sample. */
    keys(GPIO_PIN_3);assert(led_high);
    toggles=led_toggles;pins&=~GPIO_PIN_9;advance(1U);
    assert(!led_high && led_toggles==toggles+1U);
    toggles=led_toggles;
    log_ticks(1999U);assert(!led_high && led_toggles==toggles);
    log_ticks(1U);assert(led_high && led_toggles==toggles+1U);
    keys(0);drain_stops();assert(!led_high);

    /* A real TX timeout alerts, including after all keys are released. */
    keys(GPIO_PIN_3);available=0;advance(700U);
    assert(!g_demo.ready && !strcmp(g_demo.error,"TX_TIMEOUT"));
    keys(0);assert(g_demo.led_mode==DEMO_LED_ALERT);
    int level=led_high;toggles=led_toggles;
    log_ticks(2000U);assert(led_high!=level && led_toggles==toggles+1U);
    available=1;log_ticks(10000U);
    assert(g_demo.ready && !led_high && g_demo.led_mode==DEMO_LED_IDLE);
    command("DEFAULT");assert(g_demo.led_mode==DEMO_LED_ALERT);
    puts("PASS LED: active-low PB9, clock/preload/output setup, idle/STOP tail, immediate send, 500ms/200ms boundaries, E-stop/conflict/fault/unconfigured priority and recovery");
}

static void check_led_wrap_and_sleep_abort(void)
{
    reset(1);configure(1U,0U);drain_stops();
    counter=65520U;g_demo.previous_timer=counter;g_demo.ticks=UINT32_MAX-1000U;
    keys(GPIO_PIN_3);assert(led_high);
    log_ticks(4999U);assert(led_high);
    log_ticks(1U);assert(!led_high);
    /* Alert period itself crosses the 32-bit software clock wrap. */
    reset(1);g_demo.ticks=UINT32_MAX-1000U;
    g_idle_since=g_demo.ticks;
    g_demo.previous_timer=counter;g_demo.led_mode=DEMO_LED_IDLE;
    Demo_LedProcess();int level=led_high;
    log_ticks(1999U);assert(led_high==level);
    log_ticks(1U);assert(led_high!=level);

    lp_prepare();lp_drain_key_after=4;
    Demo_LowPowerProcess();assert(!lp_sleep_calls && !led_high);
    lp_prepare();lp_wake_stby_fail=1;
    Demo_LowPowerProcess();assert(!g_demo.ready && g_demo.led_mode==DEMO_LED_ALERT && !led_high);
    puts("PASS LED: hardware/software timer wrap, cancelled sleep keeps indication, wake radio fault restores alert; all sleep/RTC entries keep LED OFF");
}

int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    puts("RUN: channel regression");check_fifty_channels();
    puts("RUN: pairing regression");check_configuration();
    puts("RUN: Flash regression");check_power_loss();
    puts("RUN: UART regression");check_uart_and_faults();
    puts("RUN: key regression");check_button();
    puts("RUN: two-stage speed regression");check_two_stage_speed();
    puts("RUN: E-stop regression");check_estop();
    puts("RUN: restart release regression");check_restart_release();
    puts("RUN: log regression");check_snapshot_logs();
    check_led_states_and_timing();check_led_wrap_and_sleep_abort();
    check_complete_irq_configuration();check_idle_timing_and_all_inputs();
    check_entry_guards();check_sleep_and_each_wake_input();
    check_short_wake_and_races();check_uart_completion_and_repeated_sessions();
    check_unpaired_sleep();check_watchdog_and_failure_paths();
    puts("LOW POWER sender tests PASS; no hardware accessed.");
    return 0;
}
