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
    WirelessDemo_SetStatusProvider(WiredControl_StatusLine, WiredControl_Error);
}

static void ReceiverApp_SyncRemote(void)
{
    uint8_t command = WirelessDemo_GetCommand();
    /* Valid RF E-stop overrides wired as well as RF motion. The RX service
     * returns STOP after 85 ms/fault/unpairing, so RF loss releases ONLY this
     * inhibit reason and restores wired control. No persistent E-stop latch. */
    WiredControl_SetRemoteEmergencyStop((uint8_t)(command == WIRELESS_CMD_ESTOP));
    WiredControl_SetRemoteCommand(command == WIRELESS_CMD_ESTOP ? WIRELESS_CMD_STOP : command);
}

void ReceiverApp_Process(void)
{
    /* Service motion before and after SPI/UART work. Radio initialization
     * retries wait for idle so a missing radio does not periodically interrupt
     * an already-running wired operation. */
    ReceiverApp_SyncRemote();
    WiredControl_Process();
    WirelessDemo_SetRetryAllowed(WiredControl_RadioRetryAllowed());
    WirelessDemo_Process();
    ReceiverApp_SyncRemote();
    WiredControl_Process();
    WirelessDemo_ReportProcess();
}
