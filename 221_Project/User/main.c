/* 2026-11-2 日注释：以下完整保留原主程序。
 * 原本功能：有线/无线升降控制协调、方向回路检查及继电器执行。
 * 现在新功能：自动无线丢包测试；无需手动按键，测试数据不执行升降控制。
 * 恢复方法见 test-diu-bao-lv 中说明文档及 backup/main.original.c。
 */
// #include "receiver_app.h"
// 
// int main(void)
// {
//     ReceiverApp_Init();
//     while (1)
//     {
//         ReceiverApp_Process();
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
