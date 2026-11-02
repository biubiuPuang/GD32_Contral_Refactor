//#include "gd32e23x.h"
//#include "systick.h"
//#include <stdio.h>
//#include "main.h"
//#include "usart.h"

//int main(void)
//{
//	//	初始化滴答定时器
//	systick_config();	
//	
//	// 初始化串口
//	usart_init(115200);

//	int number = 0;
//	printf("Start!\n");
//	while(1)
//	{
//		number += 1;
//		printf("test data: %d\r\n",number);
//		delay_1ms(1000); // 延时一秒
//		delay_1ms(1000);
//	}
//}

#include "wireless_demo.h"

int main(void)
{
    WirelessDemo_Init();

    while (1)
    {
        WirelessDemo_Process();
    }
}
