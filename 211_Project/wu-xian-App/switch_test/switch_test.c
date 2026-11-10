#include "gd32e23x.h"
#include "systick.h"
#include "usart.h"

#define SWITCH_PORT          GPIOB
#define SWITCH_SENSE_PIN     GPIO_PIN_7
#define SWITCH_LOW_PIN       GPIO_PIN_8
#define SWITCH_LED_PIN       GPIO_PIN_9
#define SWITCH_DEBOUNCE_MS    20U

/* 项目自定义函数：只初始化本测试使用的 PB7、PB8、PB9。
 * PB7 始终是内部上拉输入，PB8 始终是开漏拉低输出。
 * 闭合时由 MCU 内部上拉电阻限流，不使用两个相反电平的推挽输出。
 */
static void SwitchTest_GpioInit(void)
{
    rcu_periph_clock_enable(RCU_GPIOB);

    /* 先将开关两端设为输入，避免配置期间主动驱动高电平。 */
    gpio_mode_set(SWITCH_PORT, GPIO_MODE_INPUT, GPIO_PUPD_NONE,
                  SWITCH_SENSE_PIN | SWITCH_LOW_PIN);
    gpio_mode_set(SWITCH_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP,
                  SWITCH_SENSE_PIN);

    /* 先预置输出低、配置开漏，最后才使能 PB8 输出模式。 */
    gpio_bit_reset(SWITCH_PORT, SWITCH_LOW_PIN);
    gpio_output_options_set(SWITCH_PORT, GPIO_OTYPE_OD, GPIO_OSPEED_2MHZ,
                            SWITCH_LOW_PIN);
    gpio_mode_set(SWITCH_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                  SWITCH_LOW_PIN);

    /* 板载 PB9 LED 低有效：先预置高电平灭灯，再开启输出。 */
    gpio_bit_set(SWITCH_PORT, SWITCH_LED_PIN);
    gpio_output_options_set(SWITCH_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_2MHZ,
                            SWITCH_LED_PIN);
    gpio_mode_set(SWITCH_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE,
                  SWITCH_LED_PIN);
}

/* 项目自定义函数：PB7 为低表示开关导通。 */
static uint8_t SwitchTest_IsClosed(void)
{
    return (uint8_t)(gpio_input_bit_get(SWITCH_PORT, SWITCH_SENSE_PIN) == RESET);
}

/* 项目自定义函数：同步更新 LED 和串口，只有稳定状态变化才调用。 */
static void SwitchTest_Report(uint8_t closed)
{
    if(closed != 0U) {
        gpio_bit_reset(SWITCH_PORT, SWITCH_LED_PIN);
        printf("SWITCH CLOSED, PB7=0, LED ON\r\n");
    } else {
        gpio_bit_set(SWITCH_PORT, SWITCH_LED_PIN);
        printf("SWITCH OPEN, PB7=1, LED OFF\r\n");
    }
}

int main(void)
{
    uint8_t candidate, sample;
    uint8_t stable = 2U; /* 未确认：上电后也打印一次实际状态。 */
    uint8_t stable_ms = 0U;

    /* 复用工程现有延时接口和 BSP 串口初始化，包含串口/GPIOA 时钟。
     * 启动文件和 system_gd32e23x.c 沿用现有内部晶振 72MHz 配置。
     */
    systick_config();
    SwitchTest_GpioInit();
    usart_init(115200U);
    printf("\r\nSWITCH TEST: PB7 INPUT PULLUP, PB8 OPEN-DRAIN LOW, PB9 LED\r\n");

    candidate = SwitchTest_IsClosed();
    while(1) {
        delay_1ms(1U);
        sample = SwitchTest_IsClosed();
        if(sample != candidate) {
            candidate = sample;
            stable_ms = 0U;
        } else if(stable_ms < SWITCH_DEBOUNCE_MS) {
            ++stable_ms;
        }

        if(stable_ms >= SWITCH_DEBOUNCE_MS && stable != candidate) {
            stable = candidate;
            SwitchTest_Report(stable);
        }
    }
}
