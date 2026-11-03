#ifndef WIRELESS_DEMO_H
#define WIRELESS_DEMO_H

#include "typedefs.h"

/* Project application functions. Call Init once, then run the explicit steps
 * below in a tight loop, or use the compatible WirelessDemo_Process wrapper.
 * Init configures the existing SysTick delay BSP, USART1 and TIMER2;
 * the TX board configures active-low PA9/PA10 (DOWN), PB3/PB4 (UP),
 * PB5/PB6 (FAST), PA8 (active-low wireless E-stop) inputs with pull-ups.
 * Either direction/speed key stage means that key is held.
 * Application owns USART1_IRQHandler for buffered RX and nonblocking log TX,
 * plus RTC/EXTI17 and key EXTI3/4/5/6/8/9/10 for LP180 deep sleep.
 * TIMER2 is reserved; no timer/SysTick IRQ handler is required.
 * Persistent settings use 0x0800F800..0x0800FFFF (last two 1KB pages).
 * First boot/default: unpaired. Provision BOTH boards with SET,<ID>,<CH>.
 * Call only from main context. Enable FWDGT as in User/main.c before processing
 * low power, and call Demo_WatchdogFeed after a complete main-loop iteration.
 * Do not add blocking delays while awake. Low power waits for a key and feeds
 * FWDGT on RTC wakes without returning to normal application processing.
 */

/* On-air payload: ID[4, MSB first], TAG, COMMAND, SEQ_H, SEQ_L.
 * TAG changed from A1 (old generic RUN/STOP) to A2. Old receivers must not
 * interpret the new direction commands as the old generic RUN command.
 * Valid COMMAND values ONLY: 00, 01, 02, 03, 04, 05, 06, 08.
 * E-stop is a complete state, never combined with direction/FAST bits.
 * 03=direction conflict, 04=FAST alone: status only, NEVER motor commands.
 * TX FIFO still receives only the last four bytes.
 */
#define WIRELESS_CONTROL_TAG       0xA2U
#define WIRELESS_CMD_STOP          0x00U
#define WIRELESS_CMD_UP            0x01U
#define WIRELESS_CMD_DOWN          0x02U
#define WIRELESS_CMD_CONFLICT      0x03U
#define WIRELESS_CMD_FAST          0x04U
#define WIRELESS_CMD_ESTOP         0x08U
#define WIRELESS_CMD_UP_FAST       (WIRELESS_CMD_UP | WIRELESS_CMD_FAST)
#define WIRELESS_CMD_DOWN_FAST     (WIRELESS_CMD_DOWN | WIRELESS_CMD_FAST)

void WirelessDemo_Init(void);

/* Main-loop steps: UART -> keys -> radio check/TX -> periodic report.
 * RadioCheckAndRetry returns FALSE during recovery; TX resumes next loop.
 * Internal configuration and runtime state remain private to the application.
 */
void Demo_UartProcess(void);
void Demo_ButtonProcess(void);
BOOL Demo_RadioCheckAndRetry(void);
void Demo_TxProcess(void);
void Demo_ReportIfDue(void);
/* Call LowPowerProcess after reporting and before feeding the enabled FWDGT.
 * It stays in deep sleep until any of the seven key inputs wakes it.
 */
void Demo_LowPowerProcess(void);
void Demo_WatchdogFeed(void);
BOOL Demo_ClockResume(void);

/* Compatibility entry point for existing examples and validation. */
void WirelessDemo_Process(void);

#endif
