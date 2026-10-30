#ifndef WIRELESS_DEMO_H
#define WIRELESS_DEMO_H

/* Project application functions. Call Init once, then Process in a tight loop.
 * Init configures the existing SysTick delay BSP, USART1 and TIMER2;
 * the TX board configures active-low PA9/PA10 (DOWN), PB3/PB4 (UP),
 * PB5/PB6 (FAST), PA8 (active-low wireless E-stop) inputs with pull-ups.
 * Either direction/speed key stage means that key is held.
 * Application owns USART1_IRQHandler for buffered RX and nonblocking log TX.
 * TIMER2 is reserved; no timer/SysTick IRQ handler is required.
 * Persistent settings use 0x0800F800..0x0800FFFF (last two 1KB pages).
 * First boot/default: unpaired. Provision BOTH boards with SET,<ID>,<CH>.
 * Call only from main context. Do not add a 300 ms delay around Process.
 */

/* On-air payload: ID[4, MSB first], TAG, COMMAND, SEQ_H, SEQ_L.
 * TAG changed from A1 (old generic RUN/STOP) to A2. Old receivers must not
 * interpret the new direction commands as the old generic RUN command.
 * Valid COMMAND values ONLY: 00, 01, 02, 05, 06, 08 (wireless E-stop).
 * E-stop is a complete state, never combined with direction/FAST bits.
 * FAST alone/both directions are not control commands.
 * TX FIFO still receives only the last four bytes.
 */
#define WIRELESS_CONTROL_TAG       0xA2U
#define WIRELESS_CMD_STOP          0x00U
#define WIRELESS_CMD_UP            0x01U
#define WIRELESS_CMD_DOWN          0x02U
#define WIRELESS_CMD_FAST          0x04U
#define WIRELESS_CMD_ESTOP         0x08U
#define WIRELESS_CMD_UP_FAST       (WIRELESS_CMD_UP | WIRELESS_CMD_FAST)
#define WIRELESS_CMD_DOWN_FAST     (WIRELESS_CMD_DOWN | WIRELESS_CMD_FAST)

void WirelessDemo_Init(void);
void WirelessDemo_Process(void);

#endif
