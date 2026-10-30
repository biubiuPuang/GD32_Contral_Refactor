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
