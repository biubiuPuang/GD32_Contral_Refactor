#ifndef WIRED_MOCK_GD32_H
#define WIRED_MOCK_GD32_H
#include <stdint.h>
typedef enum { RESET = 0, SET = 1 } bit_status;
typedef bit_status FlagStatus;
typedef struct {
    uint16_t prescaler, period, repetitioncounter;
    uint32_t alignedmode, counterdirection, clockdivision;
} timer_parameter_struct;
#define GPIOA 0U
#define GPIOB 1U
#define GPIO_PIN_3 (1U << 3)
#define GPIO_PIN_4 (1U << 4)
#define GPIO_PIN_5 (1U << 5)
#define GPIO_PIN_6 (1U << 6)
#define GPIO_PIN_8 (1U << 8)
#define GPIO_PIN_9 (1U << 9)
#define GPIO_PIN_10 (1U << 10)
#define GPIO_PIN_11 (1U << 11)
#define GPIO_PIN_12 (1U << 12)
#define GPIO_PIN_13 (1U << 13)
#define RCU_GPIOA 0U
#define RCU_GPIOB 1U
#define RCU_TIMER13 2U
#define TIMER13 13U
#define CK_APB1 1U
#define CK_AHB 2U
#define TIMER_COUNTER_EDGE 0U
#define TIMER_COUNTER_UP 0U
#define TIMER_CKDIV_DIV1 0U
#define GPIO_OTYPE_PP 0U
#define GPIO_OSPEED_2MHZ 0U
#define GPIO_PUPD_NONE 0U
#define GPIO_PUPD_PULLUP 1U
#define GPIO_MODE_OUTPUT 1U
#define GPIO_MODE_INPUT 0U
#define USART_FLAG_TBE 1U
uint32_t rcu_clock_freq_get(uint32_t clock);
void rcu_periph_clock_enable(uint32_t clock);
void timer_deinit(uint32_t timer);
void timer_struct_para_init(timer_parameter_struct *timer);
void timer_init(uint32_t timer, timer_parameter_struct *config);
void timer_counter_value_config(uint32_t timer, uint16_t value);
void timer_enable(uint32_t timer);
uint16_t timer_counter_read(uint32_t timer);
void gpio_bit_set(uint32_t port, uint32_t pins);
void gpio_bit_reset(uint32_t port, uint32_t pins);
void gpio_bit_write(uint32_t port, uint32_t pins, bit_status value);
void gpio_output_options_set(uint32_t port, uint32_t type, uint32_t speed, uint32_t pins);
void gpio_mode_set(uint32_t port, uint32_t mode, uint32_t pupd, uint32_t pins);
uint16_t gpio_input_port_get(uint32_t port);
uint16_t gpio_output_port_get(uint32_t port);
FlagStatus usart_flag_get(uint32_t uart, uint32_t flag);
void usart_data_transmit(uint32_t uart, uint8_t value);
#endif
