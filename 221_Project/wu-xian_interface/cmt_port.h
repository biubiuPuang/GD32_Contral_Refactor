#ifndef CMT_PORT_H
#define CMT_PORT_H

#include "gd32e23x.h"

/* Board wiring. Radio GPIO1/2/3 are deliberately not mapped to MCU pins. */
#define CMT_CSB_PORT  GPIOA
#define CMT_CSB_PIN   GPIO_PIN_4
#define CMT_FCSB_PORT GPIOB
#define CMT_FCSB_PIN  GPIO_PIN_0
#define CMT_SCLK_PORT GPIOA
#define CMT_SCLK_PIN  GPIO_PIN_5
#define CMT_SDIO_PORT GPIOA
#define CMT_SDIO_PIN  GPIO_PIN_7

#ifdef __cplusplus
extern "C" {
#endif

/* Call the existing systick_config() before using any driver function. */
void CMT_PortInit(void);
void CMT_PortSdioInput(void);
void CMT_PortSdioOutput(void);
void CMT_PortDelayUs(uint32_t us);
void CMT_PortDelayMs(uint32_t ms);

#ifdef __cplusplus
}
#endif
#endif
