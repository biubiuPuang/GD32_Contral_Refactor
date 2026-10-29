
#ifndef MOCK_GD32_H
#define MOCK_GD32_H
#include <stdint.h>
#define GPIOA 0
#define GPIOB 1
#define GPIO_PIN_0 1U
#define GPIO_PIN_4 16U
#define GPIO_PIN_5 32U
#define GPIO_PIN_7 128U
#define RCU_GPIOA 0
#define RCU_GPIOB 1
#define GPIO_MODE_INPUT 0
#define GPIO_MODE_OUTPUT 1
#define GPIO_PUPD_NONE 0
#define GPIO_PUPD_PULLUP 1
#define GPIO_OTYPE_PP 0
#define GPIO_OSPEED_10MHZ 1
#define RESET 0
typedef int FlagStatus;
void rcu_periph_clock_enable(uint32_t p);
void gpio_bit_set(uint32_t p,uint32_t pin);
void gpio_bit_reset(uint32_t p,uint32_t pin);
void gpio_mode_set(uint32_t p,uint32_t m,uint32_t pull,uint32_t pin);
void gpio_output_options_set(uint32_t p,uint32_t t,uint32_t s,uint32_t pin);
FlagStatus gpio_input_bit_get(uint32_t p,uint32_t pin);
#endif
