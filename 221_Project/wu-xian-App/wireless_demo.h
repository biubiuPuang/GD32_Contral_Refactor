#ifndef WIRELESS_DEMO_H
#define WIRELESS_DEMO_H

/* Project application functions. Call Init once, then Process in a tight loop.
 * Init configures the existing SysTick delay BSP, USART1 and TIMER2.
 * TIMER2 is reserved by this demo; no timer/SysTick IRQ handler is required.
 * Call only from main context. Do not add a 300 ms delay around Process.
 */
void WirelessDemo_Init(void);
void WirelessDemo_Process(void);

#endif
