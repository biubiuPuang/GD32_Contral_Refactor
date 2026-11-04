#include "wireless_demo.h"
#include "gd32e23x_fwdgt.h"

int main(void)
{
    /* 初始化时基、串口、定时器、按键、Flash 配置和无线芯片。
     * GPIO 和外设时钟由现有初始化过程开启。
     */
    WirelessDemo_Init();

    /* 开启看门狗使用的内部 40kHz 时钟 */
    rcu_osci_on(RCU_IRC40K);
    /* 等待看门狗时钟稳定，失败则停在这里 */
    if (SUCCESS != rcu_osci_stab_wait(RCU_IRC40K))
    {
        while (1)
        {
        }
    }
    /* 配置看门狗：64分频，重装载值1250，超时约2秒 */
    if (SUCCESS != fwdgt_config(1250U, FWDGT_PSC_DIV64))
    {
        /* 配置失败，停止继续运行 */
        while (1)
        {
        }
    }
    /* 启动看门狗，只执行一次 */
    fwdgt_enable();

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

        /* 5. 更新PB9指示灯：正常发送500ms翻转，异常200ms翻转。 */
        Demo_LedProcess();

        /* 6. 空闲180秒后进入低功耗，灭灯；按键唤醒后恢复指示。 */
        Demo_LowPowerProcess();

        /* 7. 主循环完整执行一轮后，由应用限制喂狗频率。 */
        Demo_WatchdogFeed();
    }
}
