/* GD32E230 board adaptation. Uses the project's GD32 library and SysTick BSP. */
#include "cmt_port.h"
#include "systick.h"

void CMT_PortInit(void)
{
    /* Software SPI uses GPIO clocks; the hardware SPI peripheral is not used. */
    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);

    /* Set idle output levels before enabling the output drivers. */
    gpio_bit_set(CMT_CSB_PORT, CMT_CSB_PIN | CMT_SDIO_PIN);
    gpio_bit_set(CMT_FCSB_PORT, CMT_FCSB_PIN);
    gpio_bit_reset(CMT_SCLK_PORT, CMT_SCLK_PIN);

    gpio_output_options_set(GPIOA, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ,
                            CMT_CSB_PIN | CMT_SCLK_PIN | CMT_SDIO_PIN);
    gpio_output_options_set(GPIOB, GPIO_OTYPE_PP, GPIO_OSPEED_10MHZ, CMT_FCSB_PIN);
    gpio_mode_set(GPIOA, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, CMT_CSB_PIN | CMT_SCLK_PIN);
    gpio_mode_set(GPIOB, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, CMT_FCSB_PIN);
    CMT_PortSdioInput();
}

void CMT_PortSdioInput(void)
{
    /* Release the bidirectional data line before the chip starts driving it. */
    gpio_mode_set(CMT_SDIO_PORT, GPIO_MODE_INPUT, GPIO_PUPD_PULLUP, CMT_SDIO_PIN);
}

void CMT_PortSdioOutput(void)
{
    gpio_mode_set(CMT_SDIO_PORT, GPIO_MODE_OUTPUT, GPIO_PUPD_NONE, CMT_SDIO_PIN);
}

void CMT_PortDelayUs(uint32_t us)
{
    /* Short chunks keep the existing 24-bit SysTick LOAD in range. */
    while(us >= 1000U) {
        delay_1us(1000U);
        us -= 1000U;
    }
    if(us != 0U)
        delay_1us(us);
}

void CMT_PortDelayMs(uint32_t ms)
{
    while(ms != 0U) {
        delay_1ms(1U);
        --ms;
    }
}
