#ifndef __CMT_SPI3_H
#define __CMT_SPI3_H

#include "gpio.h"

__inline void cmt_spi3_delay(void);
__inline void cmt_spi3_delay_us(void);

void cmt_spi3_init(void);

void cmt_spi3_send(u8 data8);
u8 cmt_spi3_recv(void);

void cmt_spi3_write(u8 addr, u8 dat);
void cmt_spi3_read(u8 addr, u8* p_dat);

void cmt_spi3_write_fifo(const u8* p_buf, u16 len);
void cmt_spi3_read_fifo(u8* p_buf, u16 len);


void SQ(void);
void csb_out(void);//csb--PB10
void fcsb_out(void);//fcsb--PA5
void sclk_out(void);//SCLK--PB1
void sdio_out(void);//SDIO ‰≥ˆ--PC5
void sdio_in(void);//SDIO ‰»Î--PC5
void set_gpio1(void);
void set_gpio2(void);
void set_gpio3(void);


#endif
