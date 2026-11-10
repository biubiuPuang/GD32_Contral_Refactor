# PB7 / PB8 拨动开关独立测试

打开本目录 `switch_test.uvprojx`，编译并烧录 **211 发送板**。
本测试拥有自己的 `main()`；原 `User/main.c`、无线应用和原 Keil 工程无需修改。
测试固件只运行开关检测、LED 和串口输出，不运行无线控制或低功耗。

## 接线与初始化

- 开关两端分别连接 PB7、PB8，中间不需要增加外部电阻。
- PB7：内部上拉输入。PB8：开漏输出，固定拉低。
- 电流路径：MCU 内部电源 → 内部上拉电阻 → PB7 → 闭合开关 → PB8 → GND。
- 初始化先开启 GPIOB 时钟，将开关两端设成输入；预置 PB8 低电平并配置开漏后，才切换为输出。代码不将 PB7 配置为输出，也不驱动 PB8 为高电平。
- 板载 LED 使用 PB9，低电平亮、高电平灭。
- 串口复用现有 BSP `usart_init(115200U)`，该函数开启 USART1 和 GPIOA 时钟。
- MCU 使用工程现有内部晶振 72MHz 配置。消抖复用 `systick_config()` 和 `delay_1ms()`，连续稳定约 20ms 后确认状态。

## 测试步骤

1. 串口助手设为 **115200、8 数据位、无校验、1 停止位**；电脑串口 RX 接板上 PA2（TX），并共地。
2. 烧录后复位。首先输出 `SWITCH TEST` 标识，随后打印一次开关实际状态。
3. 开关导通：PB9 LED 常亮，输出 `SWITCH CLOSED, PB7=0, LED ON`。
4. 开关断开：LED 熄灭，输出 `SWITCH OPEN, PB7=1, LED OFF`。
5. 多次拨动，检查灯和日志对应。只有上电及状态变化时打印，不持续刷屏。
6. 若需要再次查看上电日志，打开串口助手后按复位键。

测试 HEX：`Objects/switch_test.hex`。独立编译日志：`build.log`。
本测试不写配对配置 Flash；恢复正式功能时，重新烧录原 211 工程固件。
下载器是否整片擦除由下载设置决定，测试代码本身不会执行 Flash 擦写。

## 本机命令行编译

在工程根目录执行（本机已有 Keil 路径）：

```powershell
$demoDir = Join-Path (Get-Location) '211_Project\wu-xian-App\switch_test'
$buildArgs = @('-r', ('"' + (Join-Path $demoDir 'switch_test.uvprojx') + '"'), '-j0', '-o', ('"' + (Join-Path $demoDir 'build.log') + '"'))
$buildProcess = Start-Process -FilePath 'D:\_Keil5\UV4\UV4.exe' -ArgumentList $buildArgs -WorkingDirectory $demoDir -WindowStyle Hidden -Wait -PassThru
Get-Content -LiteralPath (Join-Path $demoDir 'build.log')
```

本目录工程复用现有启动文件、官方外设库及串口 BSP，编译输出在本目录中。编译不会自动烧录硬件。
