/* Copy this file's contents into User/main.c manually.
 * This example is deliberately NOT added to the normal Keil target.
 */
#include "wireless_demo.h"

int main(void)
{
    WirelessDemo_Init();
    while(1) {
        WirelessDemo_Process();
    }
}
