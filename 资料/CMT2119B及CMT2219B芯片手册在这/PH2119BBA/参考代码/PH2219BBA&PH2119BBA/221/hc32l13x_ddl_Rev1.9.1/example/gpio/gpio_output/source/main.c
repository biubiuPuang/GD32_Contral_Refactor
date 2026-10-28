
#include "gpio.h"

#include <stdio.h>
#include <string.h>


#include "radio.h"
#include "SQ.h"

#define RF_PACKET_SIZE   11               /* Define the payload size here */
static u8 g_rxBuffer[RF_PACKET_SIZE];   /* RF Rx buffer */


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

	   SQ(); 
	App_LedInit();
}


u8 Radio_Recv_FixedLen(u8 pBuf[],u8 len)
{

		if(CMT2300A_ReadGpio3()) //  PKT_OK
		{
			  CMT2300A_GoStby();
			  CMT2300A_ReadFifo(pBuf,1);
			  CMT2300A_ReadFifo(&pBuf[1],pBuf[0]);			  
			  CMT2300A_GoSleep();		
				CMT2300A_GoStby();		
				/* Must clear FIFO after enable SPI to read or write the FIFO */
				CMT2300A_EnableReadFifo();
				CMT2300A_ClearInterruptFlags();
				CMT2300A_ClearRxFifo();
				CMT2300A_GoRx();
			
			  return 1;
		}

		return 0;
}


/* Main application entry point */
int main(void) //单收例程
{
		int ledflag=0;

    Mcu_Init();
    RF_Init();	
	  if(FALSE==CMT2300A_IsExist()) 
		{

        while(1);
    }
		
			CMT2300A_ConfigInterrupt(
			CMT2300A_INT_SEL_TX_DONE,/* Config INT1 */
			CMT2300A_INT_SEL_PKT_OK/* Config INT2 */
			);	

	  CMT2300A_GoStby();		
		/* Must clear FIFO after enable SPI to read or write the FIFO */
    CMT2300A_EnableReadFifo();
		CMT2300A_ClearInterruptFlags();
		CMT2300A_ClearRxFifo();
    CMT2300A_GoRx();
	  while(1)
		{
		   if(Radio_Recv_FixedLen(g_rxBuffer,RF_PACKET_SIZE))
			 {
															ledflag=!ledflag;
			Gpio_WriteOutputIO(GpioPortD, GpioPin5,ledflag);		 
			 }

		}	
	
}







