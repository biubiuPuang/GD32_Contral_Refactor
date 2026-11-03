/* Copy this file's contents into User/main.c manually.
 * This example is deliberately NOT added to the normal Keil target.
 */
#include "wireless_demo.h"
#include "gd32e23x_fwdgt.h"

int main(void)
{
    WirelessDemo_Init();
    rcu_osci_on(RCU_IRC40K);
    if(SUCCESS != rcu_osci_stab_wait(RCU_IRC40K) ||
       SUCCESS != fwdgt_config(1250U, FWDGT_PSC_DIV64)) {
        while(1) {}
    }
    fwdgt_enable();
    while(1) {
        WirelessDemo_Process();
        Demo_WatchdogFeed();
    }
}
