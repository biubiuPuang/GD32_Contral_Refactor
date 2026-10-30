#ifndef WIRELESS_DEMO_H
#define WIRELESS_DEMO_H

#include <stdint.h>

/* RX service only. Use ReceiverApp_Init/Process in main for physical control.
 * Owns USART1_IRQHandler, TIMER2, radio SPI and the last two Flash pages.
 * Existing GPIO/USART/SysTick BSPs are reused. All APIs run in main context.
 * Raw valid packets do NOT bypass the shared execution/limit feedback checks.
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
/* STOP if fault, unpaired or >=85ms old; E-stop also expires (not latched). */
uint8_t WirelessDemo_GetCommand(void);
uint8_t WirelessDemo_Log(const char *line); /* Complete nonblocking UART line. */
void WirelessDemo_SetRetryAllowed(uint8_t allowed);
/* Pause physical execution around explicit SET/DEFAULT maintenance, including
 * Flash operations. NULL is permitted for service-only host tests. */
void WirelessDemo_SetMaintenanceHook(void (*hook)(uint8_t active));

#endif
