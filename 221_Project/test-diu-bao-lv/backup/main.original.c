#include "receiver_app.h"

int main(void)
{
    ReceiverApp_Init();
    while (1)
    {
        ReceiverApp_Process();
    }
}
