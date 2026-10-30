# 211 VS Code 调试（ST-Link）

在 VS Code 中打开 `D:\_Project\GD32_Contral_Refactor\211_Project\Project`，
在“运行和调试”中选择 `211 ST-Link: Keil build and debug`，按 F5。

F5 会先调用本机 Keil UV4 编译 `Target 1`，成功后通过 Cortex-Debug、pyOCD 和 ST-Link
下载当前 HEX，并软件复位、停在 `main`。F10 单步跳过，F11 单步进入，F5 继续，Shift+F5 结束调试。
仅编译可按 Ctrl+Shift+B；编译日志为 `.vscode/keil-debug-build.log`。

## 接线和工具

- 当前使用 SWDIO、SWCLK、GND 和供电，未连接 NRST。
- 明确采用 `reset_type=sysresetreq` 软件系统复位，连接模式为 `halt`，SWD 时钟初值 1MHz。
  软件复位需要 SWD 仍可访问；没有 NRST 接线时，不能使用硬件复位连接来恢复不可访问的目标。
- 绑定 ST-Link ID：`68004200040000515953574E`。更换调试器后，运行 `pyocd list`，
  更新 `launch.json` 的 `--uid` 参数，保留 `stlink:` 前缀。
- Keil：`D:\_Keil5\UV4\UV4.exe`。
- pyOCD：`D:\1_Sofrware\Python_3.14.6\Scripts\pyocd.exe`，本机 0.46.0，目标 `gd32e230c8` 已安装。
- ARM GDB 工具目录：`C:\Users\admin\.eide\tools\gcc_arm\bin`。
- 本机已安装 Cortex-Debug 1.12.1。该版本的 `boardId` 会生成旧的 `--board` 参数，
  所以配置使用 `serverArgs` 的 `--uid` 选择探针，以兼容当前 pyOCD。

## 镜像与调试行为

- 从 `Objects/GD32E230C8T6.axf` 读取符号，从同次构建的 HEX 下载程序。
- 编译任务检查 HEX 数据只位于 `0x08000000..0x0800F7FF`，保护末两页配置区。
  pyOCD 使用扇区擦除并关闭自动解锁，避免自动整片擦除配置。
- 保留原 Keil 工程优化设置，部分变量或源码行可能被优化，影响逐行显示。
- 断点和单步会改变按键、无线和定时处理的时序，不用于证明正常运行时的响应时间。
- 编译失败应停止调试，不要选择忽略失败并继续下载旧程序。

本次已做配置离线检查和命令行编译；尚未实机验证断点、单步、软件复位和下载流程。
