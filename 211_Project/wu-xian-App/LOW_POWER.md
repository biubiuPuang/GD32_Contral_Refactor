# 211低功耗：V5 F50 LP180

主控GD32E230C8T6，无线芯片CMT2119B。保留A2协议、配对ID和50频道参数。

## 已接入的行为

- 所有按键消抖确认松开后，连续空闲180秒才允许休眠。短按中断也会重新计时。
- 上升PB3/PB4、下降PA9/PA10、急停PA8共五个输入支持下降沿唤醒；方向轻按普通速度、深按快速。PB5/PB6不参与控制、空闲计时或唤醒。
- 任意按键按住、发送进行中或3个STOP未发完时，不休眠。
- RTC初始化失败或已配对无线芯片发生故障时，保持主循环处理和原有恢复流程。
- 串口完整命令、输出队列处理完后，打印一次`input sleep model\r\n`，等待最后一个停止位发完。
- 复用现有`CMT2300A_GoSleep()`让无线芯片休眠，再调用GD32官方深度睡眠接口。
- 深睡期间暂停TIMER2和USART1。RTC使用IRC40K，名义每秒唤醒一次，只在主循环上下文喂狗，不打印进入/退出日志。
- PB9低有效LED在暂停TIMER2前输出高电平灭灯，整个深睡和RTC喂狗期间保持灭灯；按键唤醒、时钟及无线恢复后更新清醒灯效。详见[LED指示](README.md#pb9-led-指示)。
- 任意按键唤醒后恢复内部晶振PLL 72MHz、BSP延时、TIMER2、USART1和无线STBY，打印一次`exit sleep model\r\n`。
- 后续按原有10ms消抖和急停优先规则发送当前状态；短于消抖时间的按键可以唤醒，但不保证形成控制命令。
- 休眠后串口配置需要先按键唤醒，再发送GET/SET等命令。
- 若在进入日志打印期间收到按键或串口数据，会取消此次休眠并输出对应退出日志，继续处理输入。
- `no key press model`仍是正常工作时每秒输出的无按键快照。真正休眠期间停止快照输出。教学阶段的`IDLE 180S`已由实际休眠流程取代。

## 初始化与调用

`WirelessDemo_Init()`开启GPIOA、GPIOB、SYSCFG、PMU、RTC等时钟并初始化按键唤醒和RTC。
原main.c继续启用约2秒FWDGT，在主循环末尾依次调用`Demo_LedProcess()`、`Demo_LowPowerProcess()`和`Demo_WatchdogFeed()`。
正常运行每10ms喂狗一次；休眠中每次喂狗前后各等1ms，满足低速时钟域同步要求。
`Demo_LowPowerProcess()`必须从中断已开启、FWDGT已启用的主循环调用。
低功耗应用逻辑放在wu-xian-App，原无线BSP和官方库保持原样。

对应项目资料`lerning/GD32E23x_User_Manual_Rev2.6_CN.pdf`：58页深睡及8MHz唤醒，96～97页EXTI，172～174页看门狗，184页RTC分频，192页RTC唤醒，207页每秒闹钟。
进入WFI前的临界区遵循[Arm Cortex-M23官方用户指南](https://documentation-service.arm.com/static/63dbc61253e02459952db9cb)：使用PRIMASK防止检查与休眠之间丢失中断；待处理异常仍可唤醒WFI，随后开启中断运行ISR。

## 命令行验证

在工程根目录执行：

```powershell
python 211_Project/wu-xian-App/validation/check_low_power.py
```

复用本机GCC和Keil，测试真实应用源码；涵盖已有配对、Flash、50频道、两级速度切换、急停和串口回归，以及180秒边界、计时回绕、五个唤醒输入、弃用PB5/PB6、短按、WFI边界竞态、UART发送完成/超时、反复休眠、模拟1小时RTC喂狗和异常恢复。
编译原Keil工程，检查三个唤醒入口及HEX未覆盖末两页配对Flash。
验证期间对150个既有BSP、官方库、接收端及工程文件做前后哈希比较；不会访问串口或烧录硬件。
结果在`validation/low_power_summary.json`，编译日志在`validation/low_power_build.log`。
旧`check_sender.py`使用历史整项目保护基线，低功耗版本请使用上述新入口；不更新其他既有工程的历史基线。

## 上板验证

1. 烧录211固件，确认版本包含`V5 F50 LP180`，并出现`RTC INIT OK`。
2. 全部松开超过3分钟，看到一次`input sleep model`，之后不再每秒刷屏。
3. 继续静置至少10秒，确认没有重复启动横幅或反复进入/退出信息。
4. 分别测试PB3/PB4、PA9/PA10及PA8，任一触点按下都应出现一次`exit sleep model`，并恢复相应按键状态和无线发送。分别检查上升/下降轻按普通速度、深按快速、退回轻按普通速度。PB5/PB6不得唤醒。每次重新休眠需再次松开并等待3分钟。
5. 持续按住任意按键超过3分钟，应始终正常工作；松开后重新计时。
6. 唤醒后发送GET，核对原ID/频道保留、串口无乱码。

以上软件验证不等于实板唤醒、电流或机械动作已经验证。最终测电流应确认供电接线，使用单一供电路径；ST-Link和CH340的供电/信号连接可能影响实测电流。进入/退出日志只能证明软件流程，节电效果需要电流测量确认。
