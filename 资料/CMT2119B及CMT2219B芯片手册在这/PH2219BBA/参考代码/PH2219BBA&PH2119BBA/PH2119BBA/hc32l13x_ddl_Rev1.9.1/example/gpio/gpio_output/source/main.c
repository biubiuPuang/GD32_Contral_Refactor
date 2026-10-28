
#include "gpio.h"

#include <stdio.h>
#include <string.h>


#include "radio.h"
#include "SQ.h"

#define RF_PACKET_SIZE   10               /* Define the payload size here */

static u8 g_txBuffer[RF_PACKET_SIZE];   /* RF Tx buffer */

char str[32];
u32 g_nRecvCount=0,g_nSendCount=0;




void App_LedInit(void)
{
    stc_gpio_cfg_t stcGpioCfg;
    
    ///< 打开GPIO外设时钟门控
    Sysctrl_SetPeripheralGate(SysctrlPeripheralGpio, TRUE); 
    
    ///< 端口方向配置->输出(其它参数与以上（输入）配置参数一致)
    stcGpioCfg.enDir = GpioDirOut;
    ///< 端口上下拉配置->下拉
    stcGpioCfg.enPu = GpioPuDisable;
    stcGpioCfg.enPd = GpioPdEnable;
    
    ///< LED关闭
    Gpio_ClrIO(STK_LED_PORT, STK_LED_PIN);
    
    ///< GPIO IO LED端口初始化
    Gpio_Init(STK_LED_PORT, STK_LED_PIN, &stcGpioCfg);
    

}		










void Mcu_Init(void)
{
//    /* system init */
//    SystemInit();
//    GPIO_Config();
//    NVIC_Config();
//    SystemTimerDelay_Config();
//    Timer5_Config();
//    lcd12864_init();
//    buzzer_init();   
//    lcd12864_led_on();
	   SQ(); 
	App_LedInit();
}

u8 Radio_Send_FixedLen(const u8 pBuf[], u8 len )
{

	u32 delay;
	CMT2300A_GoStby();
	CMT2300A_ClearInterruptFlags();
	CMT2300A_EnableWriteFifo();
	CMT2300A_ClearTxFifo();
	CMT2300A_SetPayloadLength(len);
	CMT2300A_WriteFifo(pBuf, len); // 写 TX_FIFO
	CMT2300A_GoTx(); // 启动发送
	delay = 3000;
	while(1)
	{
		if(CMT2300A_ReadGpio3()) // TX_DONE
		{
			CMT2300A_GoStby();
			CMT2300A_ClearInterruptFlags();	
			CMT2300A_GoSleep();
			
			return 1; // 
		}	
		if(delay==0) //超时溢出
		{
			CMT2300A_ClearInterruptFlags();
			CMT2300A_GoSleep();
			return 0; // 发送超时
		}
		
		delay100us (2);
		delay--;
	}

	
}


/* Main application entry point */
int main(void)  //单发例程
{
    int i;
	int ledflag=0;
	  g_nRecvCount=0;

	

	
	
    for(i=0; i<RF_PACKET_SIZE; i++)
        g_txBuffer[i] = i;
    
    Mcu_Init();
    RF_Init();
	
	CMT2300A_IsExist();
	
		CMT2300A_ConfigInterrupt(
	CMT2300A_INT_SEL_PKT_OK,/* Config INT1 */
	CMT2300A_INT_SEL_TX_DONE/* Config INT2 */
	);
	
	  while(1) 
		{
				Radio_Send_FixedLen(g_txBuffer,RF_PACKET_SIZE);
											ledflag=!ledflag;
			Gpio_WriteOutputIO(GpioPortD, GpioPin5,ledflag);	
		}
	  
}






