#include "wired_control.h"

int main(void)
{
    WiredControl_Init();
    while (1)
    {
        WiredControl_Process();
    }
}
