//#include "gpio_defs.h"
//#include "common.h"



#include "cmt_spi3.h"
#include "gpio.h"

//#include "gpio_defs.h"
//#include "gpio.h"
/* ************************************************************************
*  The following need to be modified by user
*  ************************************************************************ */

void csb_out(void)//csb--PD2
{
	stc_gpio_cfg_t stcGpioCfg_csb;//结构体变量
	stcGpioCfg_csb.enDir=GpioDirOut;//设置为输出
	Gpio_Init(GpioPortD, GpioPin2, &stcGpioCfg_csb);// GPIO 初始化
}

void fcsb_out(void)//fcsb--PA4
{
	stc_gpio_cfg_t stcGpioCfg_fcsb;
	stcGpioCfg_fcsb.enDir=GpioDirOut;
	Gpio_Init(GpioPortA, GpioPin4, &stcGpioCfg_fcsb);
}

void sclk_out(void)//SCLK--PB3
{
	stc_gpio_cfg_t stcGpioCfg_sclk;
	stcGpioCfg_sclk.enDir=GpioDirOut;
	Gpio_Init(GpioPortB, GpioPin3, &stcGpioCfg_sclk);
}

void sdio_out(void)//SDIO输出--PB4
{
	stc_gpio_cfg_t stcGpioCfg_sdio_out;
	stcGpioCfg_sdio_out.enDir=GpioDirOut;
	Gpio_Init(GpioPortB, GpioPin4, &stcGpioCfg_sdio_out);
}

void sdio_in(void)//SDIO输入--PB4
{
	stc_gpio_cfg_t stcGpioCfg_sdio_in;
	stcGpioCfg_sdio_in.enDir=GpioDirIn;
	Gpio_Init(GpioPortB, GpioPin4, &stcGpioCfg_sdio_in);
}

void set_gpio1(void)//GPIO1--PB5
{
	stc_gpio_cfg_t stcGpioCfg_gpio1;
	stcGpioCfg_gpio1.enDir=GpioDirIn;
	Gpio_Init(GpioPortB, GpioPin5, &stcGpioCfg_gpio1);
}
void set_gpio2(void)//GPIO2--P86
{
	stc_gpio_cfg_t stcGpioCfg_gpio2;
	stcGpioCfg_gpio2.enDir=GpioDirIn;
	Gpio_Init(GpioPortB, GpioPin6, &stcGpioCfg_gpio2);
}
void set_gpio3(void)//GPIO3--PB7
{
	stc_gpio_cfg_t stcGpioCfg_gpio3;
	stcGpioCfg_gpio3.enDir=GpioDirIn;
	Gpio_Init(GpioPortB, GpioPin7, &stcGpioCfg_gpio3);
}



#define cmt_spi3_csb_out()    	csb_out() //访问寄存器的片选
#define cmt_spi3_fcsb_out()     fcsb_out()//访问FIFO的片选
#define cmt_spi3_sclk_out()    	sclk_out()
#define cmt_spi3_sdio_out()     sdio_out()
#define cmt_spi3_sdio_in()      sdio_in()


//电平变化
#define cmt_spi3_csb_1()        Gpio_WriteOutputIO(GpioPortD,GpioPin2,TRUE)
#define cmt_spi3_csb_0()        Gpio_WriteOutputIO(GpioPortD,GpioPin2,FALSE)

#define cmt_spi3_fcsb_1()       Gpio_WriteOutputIO(GpioPortA,GpioPin4,TRUE)
#define cmt_spi3_fcsb_0()       Gpio_WriteOutputIO(GpioPortA,GpioPin4,FALSE)
    
#define cmt_spi3_sclk_1()       Gpio_WriteOutputIO(GpioPortB,GpioPin3,TRUE)
#define cmt_spi3_sclk_0()       Gpio_WriteOutputIO(GpioPortB,GpioPin3,FALSE)

#define cmt_spi3_sdio_1()       Gpio_WriteOutputIO(GpioPortB,GpioPin4,TRUE)
#define cmt_spi3_sdio_0()       Gpio_WriteOutputIO(GpioPortB,GpioPin4,FALSE)
#define cmt_spi3_sdio_read()    Gpio_GetInputIO(GpioPortB,GpioPin4)


/* ************************************************************************ */
    
void cmt_spi3_delay(void)
{
    u32 n = 7;
    while(n--);
}

void cmt_spi3_delay_us(void)
{
    u16 n = 8;
    while(n--);
}

void cmt_spi3_init(void)
{
    cmt_spi3_csb_1();
    cmt_spi3_csb_out();
    cmt_spi3_csb_1();   /* CSB has an internal pull-up resistor */
    
    cmt_spi3_sclk_0();
    cmt_spi3_sclk_out();
    cmt_spi3_sclk_0();   /* SCLK has an internal pull-down resistor */
    
    cmt_spi3_sdio_1();
    cmt_spi3_sdio_out();
    cmt_spi3_sdio_1();
    
    cmt_spi3_fcsb_1();
    cmt_spi3_fcsb_out();
    cmt_spi3_fcsb_1();  /* FCSB has an internal pull-up resistor */

    cmt_spi3_delay();
}

void cmt_spi3_send(u8 data8)
{
    u8 i;

    for(i=0; i<8; i++)
    {
        cmt_spi3_sclk_0();

        /* Send byte on the rising edge of SCLK */
        if(data8 & 0x80)
            cmt_spi3_sdio_1();
        else            
            cmt_spi3_sdio_0();

        cmt_spi3_delay();

        data8 <<= 1;
        cmt_spi3_sclk_1();
        cmt_spi3_delay();
    }
}

u8 cmt_spi3_recv(void)
{
    u8 i;
    u8 data8 = 0xFF;

    for(i=0; i<8; i++)
    {
        cmt_spi3_sclk_0();
        cmt_spi3_delay();
        data8 <<= 1;

        cmt_spi3_sclk_1();

        /* Read byte on the rising edge of SCLK */
        if(cmt_spi3_sdio_read())
            data8 |= 0x01;
        else
            data8 &= ~0x01;

        cmt_spi3_delay();
    }

    return data8;
}

void cmt_spi3_write(u8 addr, u8 dat)
{
    cmt_spi3_sdio_1();
    cmt_spi3_sdio_out();

    cmt_spi3_sclk_0();
    cmt_spi3_sclk_out();
    cmt_spi3_sclk_0(); 

    cmt_spi3_fcsb_1();
    cmt_spi3_fcsb_out();
    cmt_spi3_fcsb_1();

    cmt_spi3_csb_0();

    /* > 0.5 SCLK cycle */
    cmt_spi3_delay();
    cmt_spi3_delay();

    /* r/w = 0 */
    cmt_spi3_send(addr&0x7F);

    cmt_spi3_send(dat);

    cmt_spi3_sclk_0();

    /* > 0.5 SCLK cycle */
    cmt_spi3_delay();
    cmt_spi3_delay();

    cmt_spi3_csb_1();
    
    cmt_spi3_sdio_1();
    cmt_spi3_sdio_in();
    
    cmt_spi3_fcsb_1();    
}

void cmt_spi3_read(u8 addr, u8* p_dat)
{
    cmt_spi3_sdio_1();
    cmt_spi3_sdio_out();

    cmt_spi3_sclk_0();
    cmt_spi3_sclk_out();
    cmt_spi3_sclk_0(); 

    cmt_spi3_fcsb_1();
    cmt_spi3_fcsb_out();
    cmt_spi3_fcsb_1();

    cmt_spi3_csb_0();

    /* > 0.5 SCLK cycle */
    cmt_spi3_delay();
    cmt_spi3_delay();

    /* r/w = 1 */
    cmt_spi3_send(addr|0x80);

    /* Must set SDIO to input before the falling edge of SCLK */
    cmt_spi3_sdio_in();
    
    *p_dat = cmt_spi3_recv();

    cmt_spi3_sclk_0();

    /* > 0.5 SCLK cycle */
    cmt_spi3_delay();
    cmt_spi3_delay();

    cmt_spi3_csb_1();
    
    cmt_spi3_sdio_1();
    cmt_spi3_sdio_in();
    
    cmt_spi3_fcsb_1();
}

void cmt_spi3_write_fifo(const u8* p_buf, u16 len)
{
    u16 i;

    cmt_spi3_fcsb_1();
    cmt_spi3_fcsb_out();
    cmt_spi3_fcsb_1();

    cmt_spi3_csb_1();
    cmt_spi3_csb_out();
    cmt_spi3_csb_1();

    cmt_spi3_sclk_0();
    cmt_spi3_sclk_out();
    cmt_spi3_sclk_0();

    cmt_spi3_sdio_out();

    for(i=0; i<len; i++)
    {
        cmt_spi3_fcsb_0();

        /* > 1 SCLK cycle */
        cmt_spi3_delay();
        cmt_spi3_delay();

        cmt_spi3_send(p_buf[i]);

        cmt_spi3_sclk_0();

        /* > 2 us */
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();

        cmt_spi3_fcsb_1();

        /* > 4 us */
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
    }

    cmt_spi3_sdio_in();
    
    cmt_spi3_fcsb_1();
}

void cmt_spi3_read_fifo(u8* p_buf, u16 len)
{
    u16 i;

    cmt_spi3_fcsb_1();
    cmt_spi3_fcsb_out();
    cmt_spi3_fcsb_1();

    cmt_spi3_csb_1();
    cmt_spi3_csb_out();
    cmt_spi3_csb_1();

    cmt_spi3_sclk_0();
    cmt_spi3_sclk_out();
    cmt_spi3_sclk_0();

    cmt_spi3_sdio_in();

    for(i=0; i<len; i++)
    {
        cmt_spi3_fcsb_0();

        /* > 1 SCLK cycle */
        cmt_spi3_delay();
        cmt_spi3_delay();

        p_buf[i] = cmt_spi3_recv();

        cmt_spi3_sclk_0();

        /* > 2 us */
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();

        cmt_spi3_fcsb_1();

        /* > 4 us */
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
        cmt_spi3_delay_us();
    }

    cmt_spi3_sdio_in();
    
    cmt_spi3_fcsb_1();
}

