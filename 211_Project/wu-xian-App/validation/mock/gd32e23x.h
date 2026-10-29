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
#endif
