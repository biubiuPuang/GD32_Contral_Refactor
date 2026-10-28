
#include "SQ.h"
#include "gpio.h"

void SQ(void)
{
	                  
	Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio,TRUE);//使能GPIO模块时钟
	stc_gpio_cfg_t stcGpioCfg;//IO 配置结构体指针
	DDL_ZERO_STRUCT(stcGpioCfg);//将里面的数据清空
	stcGpioCfg.enDir=GpioDirOut;//输出
	Gpio_Init(GpioPortB, GpioPin3, &stcGpioCfg);//SClK
	Gpio_Init(GpioPortD, GpioPin2, &stcGpioCfg);//csb
	Gpio_Init(GpioPortA, GpioPin4, &stcGpioCfg);//fcsb
	Gpio_Init(GpioPortB, GpioPin4, &stcGpioCfg);//SDIO
	stcGpioCfg.enDir=GpioDirIn;//输入
	Gpio_Init(GpioPortB, GpioPin5, &stcGpioCfg);//GPIO1
	Gpio_Init(GpioPortB, GpioPin6, &stcGpioCfg);//GPIO2
	Gpio_Init(GpioPortB, GpioPin7, &stcGpioCfg);//GPIO3
	
}


