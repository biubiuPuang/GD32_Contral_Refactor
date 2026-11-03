#ifndef DEMO_MOCK_GD32_H
#define DEMO_MOCK_GD32_H
#include <stdint.h>
#define CK_APB1 1U
#define CK_AHB 2U
#define RCU_TIMER2 3U
#define TIMER2 4U
#define TIMER_COUNTER_EDGE 0U
#define TIMER_COUNTER_UP 0U
#define TIMER_CKDIV_DIV1 0U
typedef struct {
    uint16_t prescaler, alignedmode, counterdirection, clockdivision;
    uint32_t period;
    uint8_t repetitioncounter;
} timer_parameter_struct;
uint32_t rcu_clock_freq_get(uint32_t clock);
void rcu_periph_clock_enable(uint32_t clock);
void timer_deinit(uint32_t timer);
void timer_struct_para_init(timer_parameter_struct *parameters);
void timer_init(uint32_t timer, timer_parameter_struct *parameters);
void timer_counter_value_config(uint32_t timer, uint16_t value);
void timer_enable(uint32_t timer);
uint32_t timer_counter_read(uint32_t timer);
#define RESET 0U
#define SET 1U
#define RCU_GPIOA 5U
#define GPIOA 6U
#define RCU_GPIOB 7U
#define GPIOB 8U
#define GPIO_PIN_3 (1U << 3)
#define GPIO_PIN_4 (1U << 4)
#define GPIO_PIN_5 (1U << 5)
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_8 (1U << 8)
#define GPIO_PIN_9 (1U << 9)
#define GPIO_PIN_10 (1U << 10)
#define GPIO_MODE_INPUT 0U
#define GPIO_PUPD_PULLUP 1U
#define USART_FLAG_ORERR 0U
#define USART_FLAG_FERR 1U
#define USART_FLAG_NERR 2U
#define USART_FLAG_RBNE 3U
#define USART_INT_RBNE 4U
#define USART_INT_TBE 5U
#define USART_INT_FLAG_TBE 6U
#define USART1_IRQn 28U
#define FMC_FLAG_END 1U
#define FMC_FLAG_PGERR 2U
#define FMC_FLAG_PGAERR 4U
#define FMC_FLAG_WPERR 8U
#define FMC_READY 0U
#define USART_FLAG_TC 7U
#define RCU_CFGCMP 9U
#define RCU_PMU 10U
#define RCU_RTC 11U
#define RCU_IRC8M 12U
#define RCU_IRC40K 13U
#define RCU_PLL_CK 14U
#define RCU_SCSS_PLL 8U
#define RCU_CKSYSSRC_PLL 2U
#define RCU_PLLSRC_IRC8M_DIV2 0U
#define RCU_PLL_MUL18 18U
#define RCU_RTCSRC_IRC40K 2U
#define WS_WSCNT_2 2U
#define RTC_IRQn 2U
#define EXTI2_3_IRQn 6U
#define EXTI4_15_IRQn 7U
#define EXTI_SOURCE_GPIOA 0U
#define EXTI_SOURCE_GPIOB 1U
#define EXTI_SOURCE_PIN3 3U
#define EXTI_SOURCE_PIN4 4U
#define EXTI_SOURCE_PIN5 5U
#define EXTI_SOURCE_PIN6 6U
#define EXTI_SOURCE_PIN8 8U
#define EXTI_SOURCE_PIN9 9U
#define EXTI_SOURCE_PIN10 10U
#define EXTI_3 (1U << 3)
#define EXTI_4 (1U << 4)
#define EXTI_5 (1U << 5)
#define EXTI_6 (1U << 6)
#define EXTI_8 (1U << 8)
#define EXTI_9 (1U << 9)
#define EXTI_10 (1U << 10)
#define EXTI_17 (1U << 17)
#define EXTI_INTERRUPT 0U
#define EXTI_TRIG_RISING 0U
#define EXTI_TRIG_FALLING 1U
#define PMU_LDO_LOWPOWER 1U
#define WFI_CMD 0U
#define RTC_JAN 1U
#define RTC_SATURDAY 6U
#define RTC_24HOUR 0U
#define RTC_AM 0U
#define RTC_ALARM_ALL_MASK 0x80808080U
#define RTC_ALARM_DATE_SELECTED 0U
#define RTC_MASKSSC_0_14 0U
#define RTC_FLAG_ALARM0 1U
#define RTC_INT_ALARM 1U
typedef enum { ERROR = 0, SUCCESS = 1 } ErrStatus;
typedef uint32_t exti_line_enum;
typedef struct {
    uint8_t rtc_year, rtc_month, rtc_date, rtc_day_of_week;
    uint8_t rtc_hour, rtc_minute, rtc_second;
    uint16_t rtc_factor_asyn, rtc_factor_syn;
    uint32_t rtc_am_pm, rtc_display_format;
} rtc_parameter_struct;
typedef struct {
    uint32_t rtc_alarm_mask, rtc_weekday_or_date;
    uint8_t rtc_alarm_day, rtc_alarm_hour, rtc_alarm_minute, rtc_alarm_second;
    uint32_t rtc_am_pm;
} rtc_alarm_struct;
extern uint32_t SystemCoreClock;
void SystemCoreClockUpdate(void);
void __DSB(void);
void __ISB(void);
void NVIC_ClearPendingIRQ(uint32_t irq);
void NVIC_SystemReset(void);
void timer_disable(uint32_t timer);
void usart_enable(uint32_t port);
void usart_disable(uint32_t port);
void rcu_osci_on(uint32_t oscillator);
ErrStatus rcu_osci_stab_wait(uint32_t oscillator);
uint32_t rcu_system_clock_source_get(void);
void rcu_system_clock_source_config(uint32_t source);
void rcu_pll_config(uint32_t source, uint32_t multiplier);
void fmc_wscnt_set(uint8_t wait_states);
void fwdgt_counter_reload(void);
void pmu_backup_write_enable(void);
void pmu_to_deepsleepmode(uint32_t ldo, uint8_t command);
void rcu_bkp_reset_enable(void);
void rcu_bkp_reset_disable(void);
void rcu_rtc_clock_config(uint32_t source);
ErrStatus rtc_register_sync_wait(void);
ErrStatus rtc_init(rtc_parameter_struct *parameters);
ErrStatus rtc_alarm_disable(void);
void rtc_alarm_config(rtc_alarm_struct *parameters);
void rtc_alarm_subsecond_config(uint32_t mask, uint32_t subsecond);
void rtc_flag_clear(uint32_t flag);
uint32_t rtc_flag_get(uint32_t flag);
void rtc_interrupt_enable(uint32_t interrupt);
void rtc_alarm_enable(void);
void syscfg_exti_line_config(uint8_t port, uint8_t pin);
void exti_init(exti_line_enum line, uint32_t mode, uint32_t trigger);
uint32_t exti_interrupt_flag_get(exti_line_enum line);
void exti_interrupt_flag_clear(exti_line_enum line);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __enable_irq(void);
void nvic_irq_enable(uint32_t irq, uint32_t priority);
void gpio_mode_set(uint32_t port, uint32_t mode, uint32_t pull, uint32_t pins);
uint32_t gpio_input_bit_get(uint32_t port, uint32_t pin);
uint16_t gpio_input_port_get(uint32_t port);
uint32_t usart_flag_get(uint32_t port, uint32_t flag);
void usart_flag_clear(uint32_t port, uint32_t flag);
uint16_t usart_data_receive(uint32_t port);
void usart_interrupt_enable(uint32_t port, uint32_t interrupt);
void usart_interrupt_disable(uint32_t port, uint32_t interrupt);
uint32_t usart_interrupt_flag_get(uint32_t port, uint32_t flag);
void usart_data_transmit(uint32_t port, uint16_t value);
void fmc_unlock(void);
void fmc_lock(void);
void fmc_flag_clear(uint32_t flags);
uint32_t fmc_page_erase(uint32_t address);
uint32_t fmc_word_program(uint32_t address, uint32_t word);
#endif
