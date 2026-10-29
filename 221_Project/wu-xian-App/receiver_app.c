#include "receiver_app.h"
#include "wireless_demo.h"
#include "wired_control.h"

static void ReceiverApp_Maintenance(uint8_t active)
{
    /* Explicit configuration is maintenance: stop execution BEFORE Flash
     * blocks the MCU. Release afterward; fresh RF data is required to resume. */
    if(active) WiredControl_SetRemoteCommand(WIRELESS_CMD_STOP);
    WiredControl_SetSoftwareStop(active);
}

void ReceiverApp_Init(void)
{
    /* Safe execution GPIOs first. The radio SPI init must not precede them. */
    WiredControl_Init();
    WirelessDemo_Init();
    WiredControl_SetLogSink(WirelessDemo_Log);
    WirelessDemo_SetMaintenanceHook(ReceiverApp_Maintenance);
}

void ReceiverApp_Process(void)
{
    /* Service motion before and after SPI/UART work. Radio initialization
     * retries wait for idle so a missing radio does not periodically interrupt
     * an already-running wired operation. */
    WiredControl_SetRemoteCommand(WirelessDemo_GetCommand());
    WiredControl_Process();
    WirelessDemo_SetRetryAllowed(WiredControl_RadioRetryAllowed());
    WirelessDemo_Process();
    WiredControl_SetRemoteCommand(WirelessDemo_GetCommand());
    WiredControl_Process();
}
