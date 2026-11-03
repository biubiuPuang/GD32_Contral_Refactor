#include "receiver_app.h"
#include "wireless_demo.h"
#include "wired_control.h"
#include "gd32e23x.h"

static uint8_t g_restart_rf_wait;

static uint8_t ReceiverApp_ResetCause(void)
{
    uint8_t cause = 0U;
    if(rcu_flag_get(RCU_FLAG_FWDGTRST) != RESET) cause |= 1U;
    if(rcu_flag_get(RCU_FLAG_WWDGTRST) != RESET) cause |= 2U;
    if(rcu_flag_get(RCU_FLAG_SWRST) != RESET) cause |= 4U;
    /* Reset flags are sticky. Consume them on EVERY boot, including power-on. */
    rcu_all_reset_flag_clear();
    return cause;
}

static void ReceiverApp_Maintenance(uint8_t active)
{
    /* Explicit configuration is maintenance: stop execution BEFORE Flash
     * blocks the MCU. Release afterward; fresh RF data is required to resume. */
    if(active) WiredControl_SetRemoteCommand(WIRELESS_CMD_STOP);
    WiredControl_SetSoftwareStop(active);
}

void ReceiverApp_Init(void)
{
    uint8_t cause = ReceiverApp_ResetCause();
    /* Safe execution GPIOs first. The radio SPI init must not precede them. */
    WiredControl_Init();
    g_restart_rf_wait = (uint8_t)(cause != 0U);
    if(cause) WiredControl_RequireRestartRelease();
    WirelessDemo_Init();
    WiredControl_SetLogSink(WirelessDemo_Log);
    WirelessDemo_SetMaintenanceHook(ReceiverApp_Maintenance);
    WirelessDemo_SetStatusProvider(WiredControl_StatusLine, WiredControl_Error);
    if(cause & 1U) (void)WirelessDemo_Log("RESET FWDGT WAIT RELEASE\r\n");
    if(cause & 2U) (void)WirelessDemo_Log("RESET WWDGT WAIT RELEASE\r\n");
    if(cause & 4U) (void)WirelessDemo_Log("RESET SOFTWARE WAIT RELEASE\r\n");
}

void ReceiverApp_SyncRemote(void)
{
    uint8_t command = WirelessDemo_GetCommand();
    /* Valid RF E-stop overrides wired as well as RF motion. The RX service
     * returns STOP after 85 ms/fault/unpairing, so RF loss releases ONLY this
     * inhibit reason and restores wired control. No persistent E-stop latch. */
    WiredControl_SetRemoteEmergencyStop((uint8_t)(command == WIRELESS_CMD_ESTOP));
    if(g_restart_rf_wait) {
        /* An expired command/radio failure also returns STOP: that is NOT
         * evidence of key release. Require an actually received valid 00. */
        if(WirelessDemo_HasFreshStop()) {
            g_restart_rf_wait = 0U;
            (void)WirelessDemo_Log("RESTART RF RELEASED\r\n");
        }
        command = WIRELESS_CMD_STOP;
    }
    WiredControl_SetRemoteCommand(command == WIRELESS_CMD_ESTOP ? WIRELESS_CMD_STOP : command);
}

/* Compatibility entry point; main.c now runs these steps explicitly. */
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
