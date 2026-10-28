#include "gd32e23x.h"
#include "systick.h"
#include <stdio.h>
#include "main.h"
#include "usart.h"

int main(void)
{
	systick_config();	//	初始化滴答定时器
	
	rcu_periph_clock_enable(BSP_USART_RCU); // 开启串口时钟
	rcu_periph_clock_enable(BSP_USART_TX_RCU); // 开启端口时钟
	rcu_periph_clock_enable(BSP_USART_RX_RCU); // 开启端口时钟
	
	
		/* 配置复用功能 */
	gpio_af_set(BSP_USART_TX_PORT,BSP_USART_AF,BSP_USART_TX_PIN); 
	gpio_af_set(BSP_USART_RX_PORT,BSP_USART_AF,BSP_USART_RX_PIN); 
	
		/* 配置TX为复用模式 上拉模式 */
	gpio_mode_set(BSP_USART_TX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_USART_TX_PIN);
		/* 配置RX为复用模式 上拉模式 */
	gpio_mode_set(BSP_USART_RX_PORT, GPIO_MODE_AF, GPIO_PUPD_PULLUP, BSP_USART_RX_PIN); 
	
	
		/* 配置TX为推挽输出 50MHZ */
	gpio_output_options_set(BSP_USART_TX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_USART_TX_PIN);
		/* 配置RX为推挽输出 50MHZ */
	gpio_output_options_set(BSP_USART_RX_PORT, GPIO_OTYPE_PP, GPIO_OSPEED_50MHZ, BSP_USART_RX_PIN);
	
		/* 串口配置*/    
	usart_deinit(BSP_USART); 							// 复位串口    
	usart_baudrate_set(BSP_USART,115200); 				// 设置波特率    
	usart_parity_config(BSP_USART,USART_PM_NONE); 		// 没有校验位    
	usart_word_length_set(BSP_USART,USART_WL_8BIT); 	// 8位数据位    
	usart_stop_bit_set(BSP_USART,USART_STB_1BIT); 		// 1位停止位
	usart_receive_config(BSP_USART, USART_RECEIVE_ENABLE);		// 使能串口接收
    usart_transmit_config(BSP_USART, USART_TRANSMIT_ENABLE);	// 使能串口发送
	
	usart_enable(BSP_USART);	// 使能串口
	
	float number = 0;
	printf("Start!\n");
	while(1)
	{
		number += 0.11;
		printf("Number: %0.2f\n",number);
		delay_1ms(1000); // 延时一秒
		delay_1ms(1000);
	}
}
