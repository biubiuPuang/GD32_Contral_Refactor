#include "gd32e23x.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

#define BSP_USART_RCU             RCU_USART1
#define BSP_USART_TX_RCU          RCU_GPIOA
#define BSP_USART_RX_RCU          RCU_GPIOA

#define BSP_USART    USART1


#define BSP_USART_TX_PORT      	GPIOA
#define BSP_USART_TX_PIN        GPIO_PIN_2
#define BSP_USART_RX_PORT       GPIOA
#define BSP_USART_RX_PIN        GPIO_PIN_3
#define BSP_USART_AF        	GPIO_AF_1  // 串口是引脚复用功能3

// 设置串口波特率
void usart_init(uint32_t btl);

void usart_send_data(uint8_t ucch);
	
void usart_send_String(uint8_t *ucstr);