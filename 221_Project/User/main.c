#include "receiver_app.h"
#include "wireless_demo.h"
#include "wired_control.h"

int main(void)
{
    /* 先设置执行端 GPIO 的安全电平，再初始化串口、定时器和无线芯片。
     * GPIO 和外设时钟由现有初始化过程开启，同时关联维护和日志接口。
     */
    ReceiverApp_Init();

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
    }
}
