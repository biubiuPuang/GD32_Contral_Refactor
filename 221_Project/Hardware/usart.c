#include "usart.h"

void usart_send_data(uint8_t ucch)
{
    usart_data_transmit(BSP_USART, (uint8_t)ucch);  
    while(RESET == usart_flag_get(BSP_USART, USART_FLAG_TBE)); // 等待发送数据缓冲区标志置位
}

void usart_send_String(uint8_t *ucstr)
{   
      while(ucstr && *ucstr)  // 地址为空或者值为空跳出   
      {     
        usart_send_data(*ucstr++);    
      }
}


/* retarget the C library printf function to the USART */
int fputc(int ch, FILE *f)
{
    usart_data_transmit(BSP_USART, (uint8_t) ch);
    while(RESET == usart_flag_get(BSP_USART, USART_FLAG_TBE));
    return ch;
}
