# CMT2119B 官方资料与驱动移植说明

## 结论

截至 2026-07-25，华普微/HOPERF 的 CMT2119B 官方产品页和官方资料中心没有公开一套文件名明确为
“CMT2119B STM32 Demo/Driver”的独立模板工程。产品页公开的是 CMT2119B 数据手册、RFPDK 配置工具
以及若干应用笔记。

官网资料中心公开了同一寄存器式平台的 `CMT2300A_Demo(STM32)V1.0.1`。其中的
`CMT2300A_Demo_Tx` 是完整的 STM32F103VC + Keil 发射工程，包含 CMOSTEK 官方编写的：

- 3 线 SPI 寄存器读写
- FCSB FIFO 写入
- 芯片状态切换
- TX FIFO、发送启动和 TX_DONE 判断
- RFPDK 参数表加载

这套工程适合作为 CMT2119B 的官方移植底稿，但不能把其中针对 CMT2300A 导出的寄存器参数原样烧进
CMT2119B。必须用本目录中的 RFPDK 选择 CMT2119B，按实际频率/晶振/调制/速率/功率重新导出参数，
替换工程中的参数文件，并按自己的主控板修改 GPIO。

## 文件说明

- `CMT2300A_Demo_STM32_V1.0.1.zip`
  - 官网原始完整包，包含 Rx 和 Tx 两个 Keil 工程。
  - CMT2119B 移植请从 `CMT2300A_Demo_Tx` 开始。
- `CMT2300A_DemoEasy_v1.2.zip`
  - 官网原始通用示例，便于拆出底层驱动。
- `RFPDK_V1.63_Setup.rar`
  - 官网当前提供的射频参数配置/导出工具。
- `CMT2119B_Datasheet_CN_V0.6.pdf`
  - CMT2119B 中文数据手册。
- `CMT2119B_Datasheet_EN_V0.8.pdf`
  - CMT2119B 较新的英文数据手册。

两个 ZIP 已在同名目录中解压，便于直接查看。

## 推荐入口

Keil 工程：

`CMT2300A_Demo_STM32_V1.0.1/CMT2300A_Demo(STM32)V1.0.1/CMT2300A_Demo_Tx/MDK-ARM/CMT2300A_DemoEasy.uvprojx`

发送示例：

`CMT2300A_Demo_STM32_V1.0.1/CMT2300A_Demo(STM32)V1.0.1/CMT2300A_Demo_Tx/USER/platform/main.c`

3 线 SPI/FIFO 底层：

`CMT2300A_Demo_STM32_V1.0.1/CMT2300A_Demo(STM32)V1.0.1/CMT2300A_Demo_Tx/USER/periph/cmt_spi3.c`

驱动与状态控制：

`CMT2300A_Demo_STM32_V1.0.1/CMT2300A_Demo(STM32)V1.0.1/CMT2300A_Demo_Tx/USER/radio/cmt2300a.c`

参数表：

`CMT2300A_Demo_STM32_V1.0.1/CMT2300A_Demo(STM32)V1.0.1/CMT2300A_Demo_Tx/USER/radio/cmt2300a_params.h`

引脚配置：

`CMT2300A_Demo_STM32_V1.0.1/CMT2300A_Demo(STM32)V1.0.1/CMT2300A_Demo_Tx/USER/periph/gpio_defs.h`

## 移植时必须改的内容

1. 用 RFPDK 选择 **CMT2119B**，重新生成频率、数据率、基带和 TX 参数。
2. 用新参数替换 `cmt2300a_params.h` 中的参数数组；不要沿用文件里标注
   `Part Number = CMT2300A` 的现成参数。
3. 在 `gpio_defs.h` 中修改 CSB、FCSB、SCLK、SDIO 和 TX_DONE/INT 引脚。
4. 如果换了 MCU/主频，重新校准 `cmt_spi3_delay()` 和 `cmt_spi3_delay_us()`，满足数据手册时序。
5. 只保留 TX 使用路径；CMT2119B 没有接收功能。
6. 根据工作频段、天线匹配和供电能力配置发射功率。+20 dBm 时电流明显高于 +13 dBm。

## 官方来源

- CMT2119B 产品页：
  https://www.hoperf.cn/ic/rf_transmitter/CMT2119B.html
- 官方资料中心：
  https://www.hoperf.cn/service/information/tool/
- 官方 CMT2300A STM32 示例下载：
  https://www.hoperf.cn/uploads/CMT2300A_Demo(STM32)V1.0.1_1692927844.zip
- 官方 CMT2300A DemoEasy 下载：
  https://www.hoperf.cn/uploads/CMT2300A_DemoEasy_v1.2_1692927591.zip

## SHA-256

```text
406BEB736B30E9256FF40BBC21C842AF5359B47DE316716A9925BDEAD8CF1196  CMT2119B_Datasheet_CN_V0.6.pdf
C5CE4B22FDF67675869BF3825F6C990B99880D1FCFD88F130C8FD089F8240902  CMT2119B_Datasheet_EN_V0.8.pdf
17AC15141060E3D45CD47E649EE75936DF2C171F28FBFC2CE69ECB9188552BE3  CMT2300A_DemoEasy_v1.2.zip
A58D40D38799A046A992FC88F549AC6BE60B7D05073BE94C3329C7E5CA5C3BA2  CMT2300A_Demo_STM32_V1.0.1.zip
D93B34927C3F0C6D2C7C5940132A371CE79688254A54D3F81216AA1793653A4A  RFPDK_V1.63_Setup.rar
```
