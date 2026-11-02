/* 2026-11-2 日注释：以下完整保留原主程序。
 * 原本功能：遥控按键采集、无线升降/急停状态发送。
 * 现在新功能：自动无线丢包测试；无需手动按键，测试数据不执行升降控制。
 * 恢复方法见 test-diu-bao-lv 中说明文档及 backup/main.original.c。
 */
// //#include "gd32e23x.h"
// //#include "systick.h"
// //#include <stdio.h>
// //#include "main.h"
// //#include "usart.h"
// 
// //int main(void)
// //{
// //	//	初始化滴答定时器
// //	systick_config();	
// //	
// //	// 初始化串口
// //	usart_init(115200);
// 
// //	int number = 0;
// //	printf("Start!\n");
// //	while(1)
// //	{
// //		number += 1;
// //		printf("test data: %d\r\n",number);
// //		delay_1ms(1000); // 延时一秒
// //		delay_1ms(1000);
// //	}
// //}
// 
// #include "wireless_demo.h"
// 
// int main(void)
// {
//     WirelessDemo_Init();
// 
//     while (1)
//     {
//         WirelessDemo_Process();
//     }
// }

/* 2026-11-2 新主程序：仅运行项目自定义的丢包测试函数。 */
#include "packet_loss_test.h"

int main(void)
{
    PacketLossTest_Init();
    while (1)
    {
        PacketLossTest_Process();
    }
}
