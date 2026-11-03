#include "receiver_app.h"
#include "wireless_demo.h"
#include "wired_control.h"
#include "gd32e23x_fwdgt.h"


int main(void)
{
    /* 先设置执行端 GPIO 的安全电平，再初始化串口、定时器和无线芯片。
     * GPIO 和外设时钟由现有初始化过程开启，同时关联维护和日志接口。
     */
    ReceiverApp_Init();

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
        /* 1. 同步当前无线命令，过期或异常时按停止处理。 */
        ReceiverApp_SyncRemote();

        /* 2. 采集方向回路输入，更新继电器和运行许可。
         *    先处理一轮执行控制，再进行串口和无线操作。
         */
        WiredControl_Process();

        /* 3. 根据执行状态决定是否允许重试无线初始化。
         *    有线运行期间暂缓重试。
         */
        WirelessDemo_SetRetryAllowed(WiredControl_RadioRetryAllowed());

        /* 4. 处理串口配置、无线收包校验、接收命令和超时。 */
        WirelessDemo_Process();

        /* 5. 将本轮新收到的命令或超时停止同步给执行端。 */
        ReceiverApp_SyncRemote();

        /* 6. 按最新命令更新执行控制，保留原有换向、换速等待时序。 */
        WiredControl_Process();

        /* 7. 每隔 1 秒打印更新后的动作和无线状态。 */
        WirelessDemo_ReportProcess();

        /* 8. 完整处理一轮接收和执行控制后喂狗 */
        fwdgt_counter_reload();
    }
}
