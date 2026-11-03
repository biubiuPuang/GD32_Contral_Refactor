#include "wireless_demo.h"

int main(void)
{
    /* 初始化时基、串口、定时器、按键、Flash 配置和无线芯片。
     * GPIO 和外设时钟由现有初始化过程开启。
     */
    WirelessDemo_Init();

    while (1)
    {
        /* 1. 处理串口配置：查询、设置设备 ID/信道、恢复默认。 */
        Demo_UartProcess();

        /* 2. 扫描按键并消抖，判断方向、快速和急停命令。
         *    命令变化时，中止旧包，安排发送新命令。
         */
        Demo_ButtonProcess();

        /* 3. 检查配对和无线状态，异常时每隔 1 秒尝试恢复。 */
        if (Demo_RadioCheckAndRetry())
        {
            /* 检查发送完成或超时；到时间后组包并发送。
             * 停止包成功发满 3 次后，停止发送。
             */
            Demo_TxProcess();
        }

        /* 4. 每隔 1 秒打印当前状态，不阻塞主循环。 */
        Demo_ReportIfDue();
    }
}
