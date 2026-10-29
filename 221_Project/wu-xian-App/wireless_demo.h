#ifndef WIRELESS_DEMO_H
#define WIRELESS_DEMO_H

/* Project application functions. Call Init once, then Process in a tight loop.
 * Init configures the existing SysTick delay BSP, USART1 and TIMER2;
 * the TX board also configures active-low PA9/PA10 inputs with pull-ups.
 * Application owns USART1_IRQHandler for buffered RX and nonblocking log TX.
 * TIMER2 is reserved; no timer/SysTick IRQ handler is required.
 * Persistent settings use 0x0800F800..0x0800FFFF (last two 1KB pages).
 * First boot/default: unpaired. Provision BOTH boards with SET,<ID>,<CH>.
 * Call only from main context. Do not add a 300 ms delay around Process.
 */
void WirelessDemo_Init(void);
void WirelessDemo_Process(void);

#endif
