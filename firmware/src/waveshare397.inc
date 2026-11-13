/*****************************************************************************
* | File      	:   EPD_3IN97.c
* | Author      :   Waveshare team
* | Function    :   1.54inch e-paper V2
* | Info        :
*----------------
* |	This version:   V1.0
* | Date        :   2019-06-11
* | Info        :
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documnetation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to  whom the Software is
# furished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.
#
******************************************************************************/

// Waveshare ESP32-S3-ePaper-3.97 Arduino display example, adapted for InkSight.
#include <SPI.h>
using UBYTE = uint8_t;
using UWORD = uint16_t;
using UDOUBLE = uint32_t;
#define EPD_3IN97_WIDTH W
#define EPD_3IN97_HEIGHT H
#define EPD_RST_PIN PIN_EPD_RST
#define EPD_DC_PIN PIN_EPD_DC
#define EPD_CS_PIN PIN_EPD_CS
#define EPD_BUSY_PIN PIN_EPD_BUSY
#define DEV_Digital_Write digitalWrite
#define DEV_Digital_Read digitalRead
#define DEV_Delay_ms delay
#define DEV_SPI_WriteByte(value) SPI.transfer(value)
#define Debug(message) Serial.print(message)
static void EPD_3IN97_Reset(void)
{
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
    DEV_Digital_Write(EPD_RST_PIN, 0);
    DEV_Delay_ms(2);
    DEV_Digital_Write(EPD_RST_PIN, 1);
    DEV_Delay_ms(20);
}

static void EPD_3IN97_SendCommand(UBYTE Reg)
{
    DEV_Digital_Write(EPD_DC_PIN, 0);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Reg);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

static void EPD_3IN97_SendData(UBYTE Data)
{
    DEV_Digital_Write(EPD_DC_PIN, 1);
    DEV_Digital_Write(EPD_CS_PIN, 0);
    DEV_SPI_WriteByte(Data);
    DEV_Digital_Write(EPD_CS_PIN, 1);
}

// BUSY 为高时屏幕仍在工作，等它变低后再发送后续指令。
static void EPD_3IN97_ReadBusy(void)
{
    Debug("e-Paper busy\r\n");
    DEV_Delay_ms(100);
    while(DEV_Digital_Read(EPD_BUSY_PIN) == 1) {      //LOW: idle, HIGH: busy
        DEV_Delay_ms(10);
    }
    Debug("e-Paper busy release\r\n");
}

// 下面三种刷新都先用 0x22 设置刷新方式，再用 0x20 启动：F7 全刷、D7 快刷、FF 局刷。
static void EPD_3IN97_TurnOnDisplay(void)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(0xF7);
	EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ReadBusy();
}

static void EPD_3IN97_TurnOnDisplay_Fast(void)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(0xD7);
	EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ReadBusy();
}

static void EPD_3IN97_TurnOnDisplay_Part(void)
{
    EPD_3IN97_SendCommand(0x22);
    EPD_3IN97_SendData(0xFF);
    EPD_3IN97_SendCommand(0x20);
    EPD_3IN97_ReadBusy();
}

// 初始化时设定扫描范围和图像写入起点。800 × 480 屏的最后一个像素坐标为 (799, 479)。
void EPD_3IN97_Init(void)
{
    EPD_3IN97_Reset();

    EPD_3IN97_ReadBusy();
    EPD_3IN97_SendCommand(0x12);  //SWRESET
    EPD_3IN97_ReadBusy();

    EPD_3IN97_SendCommand(0x18);
    EPD_3IN97_SendData(0x80);

    EPD_3IN97_SendCommand(0x0C);
	EPD_3IN97_SendData(0xAE);
	EPD_3IN97_SendData(0xC7);
	EPD_3IN97_SendData(0xC3);
	EPD_3IN97_SendData(0xC0);
	EPD_3IN97_SendData(0x80);

    EPD_3IN97_SendCommand(0x01); //Driver output control
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);
    EPD_3IN97_SendData(0x02);

    EPD_3IN97_SendCommand(0x3C); //BorderWavefrom
    EPD_3IN97_SendData(0x01);

    EPD_3IN97_SendCommand(0x11); //data entry mode       
	EPD_3IN97_SendData(0x01);

	EPD_3IN97_SendCommand(0x44); //set Ram-X address start/end position   
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)%256);    
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)/256);

	EPD_3IN97_SendCommand(0x45); //set Ram-Y address start/end position    
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);    
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);  
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);

    EPD_3IN97_SendCommand(0x4E);   // set RAM x address count to 0;
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendCommand(0x4F);   // set RAM y address count to 0X199;    
	EPD_3IN97_SendData((H-1)&255);
    EPD_3IN97_SendData((H-1)>>8);
    EPD_3IN97_ReadBusy();

}

void EPD_3IN97_Init_Fast(void)
{
	EPD_3IN97_Reset(); 
	
	EPD_3IN97_ReadBusy();   
	EPD_3IN97_SendCommand(0x12);  //SWRESET
	EPD_3IN97_ReadBusy();   
	
	EPD_3IN97_SendCommand(0x0C);
	EPD_3IN97_SendData(0xAE);
	EPD_3IN97_SendData(0xC7);
	EPD_3IN97_SendData(0xC3);
	EPD_3IN97_SendData(0xC0);
	EPD_3IN97_SendData(0x80);
	
	EPD_3IN97_SendCommand(0x01); //Driver output control      
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);   
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);
	EPD_3IN97_SendData(0x02);

	EPD_3IN97_SendCommand(0x11); //data entry mode       
	EPD_3IN97_SendData(0x01);

	EPD_3IN97_SendCommand(0x44); //set Ram-X address start/end position   
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)%256);    
	EPD_3IN97_SendData((EPD_3IN97_WIDTH-1)/256);

	EPD_3IN97_SendCommand(0x45); //set Ram-Y address start/end position    
    EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)%256);    
	EPD_3IN97_SendData((EPD_3IN97_HEIGHT-1)/256);  
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);


	EPD_3IN97_SendCommand(0x4E);   // set RAM x address count to 0;
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendData(0x00);
	EPD_3IN97_SendCommand(0x4F);   // set RAM y address count to 0X199;    
	EPD_3IN97_SendData((H-1)&255);
    EPD_3IN97_SendData((H-1)>>8);
    EPD_3IN97_ReadBusy();

	EPD_3IN97_SendCommand(0x3C); //BorderWavefrom
	EPD_3IN97_SendData(0x01);	
	
	EPD_3IN97_SendCommand(0x18);   
	EPD_3IN97_SendData(0x80); 
	//Fast(1.5s)
	EPD_3IN97_SendCommand(0x1A); 
	EPD_3IN97_SendData(0x6A);

}

// 每个字节表示 8 个黑白像素。整屏共 48000 字节，分别写入 0x24 和 0x26 两个图像存储区。
void EPD_3IN97_Display_Base(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
    Height = EPD_3IN97_HEIGHT;

    EPD_3IN97_SendCommand(0x24);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_3IN97_SendData(Image[i + j * Width]);
        }
        DEV_Delay_ms(1);
    }

    EPD_3IN97_SendCommand(0x26);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_3IN97_SendData(Image[i + j * Width]);
        }
        DEV_Delay_ms(1);
    }
    EPD_3IN97_TurnOnDisplay();
}

void EPD_3IN97_Display_Fast_Base(const UBYTE *Image)
{
    UWORD Width, Height;
    Width = (EPD_3IN97_WIDTH % 8 == 0)? (EPD_3IN97_WIDTH / 8 ): (EPD_3IN97_WIDTH / 8 + 1);
    Height = EPD_3IN97_HEIGHT;

    EPD_3IN97_SendCommand(0x24);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_3IN97_SendData(Image[i + j * Width]);
        }
        DEV_Delay_ms(1);
    }

    EPD_3IN97_SendCommand(0x26);
    for (UWORD j = 0; j < Height; j++) {
        for (UWORD i = 0; i < Width; i++) {
            EPD_3IN97_SendData(Image[i + j * Width]);
        }
        DEV_Delay_ms(1);
    }
    EPD_3IN97_TurnOnDisplay_Fast();
}

// 局刷只传入选中矩形的图像。调用方传入的结束坐标不含在矩形内，写入前按字节换算横向范围。
void EPD_3IN97_Display_Partial(const UBYTE *Image, UWORD Xstart, UWORD Ystart, UWORD Xend, UWORD Yend)
{
	if((Xstart % 8 + Xend % 8 == 8 && Xstart % 8 > Xend % 8) || Xstart % 8 + Xend % 8 == 0 || (Xend - Xstart)%8 == 0)
    {
        Xstart = Xstart / 8 ;
        Xend = Xend / 8;
    }
    else
    {
        Xstart = Xstart / 8 ;
        Xend = Xend % 8 == 0 ? Xend / 8 : Xend / 8 + 1;
    }
    

    uint32_t i; UWORD Width;
	Width = Xend -  Xstart;
	UDOUBLE IMAGE_COUNTER = Width * (Yend-Ystart);

    Xend -= 1;
	Yend -= 1;	

    // Match the full frame: horizontal bytes ascend, controller rows descend.
    Ystart = H-1-Ystart; Yend = H-1-Yend;
    EPD_3IN97_Reset();
    EPD_3IN97_SendCommand(0x11); EPD_3IN97_SendData(0x01);

    EPD_3IN97_SendCommand(0x18);
    EPD_3IN97_SendData(0x80);

    EPD_3IN97_SendCommand(0x3C);
    EPD_3IN97_SendData(0x80);

	EPD_3IN97_SendCommand(0x44);
    EPD_3IN97_SendData((Xstart*8) & 0xFF);
    EPD_3IN97_SendData(((Xstart*8) >> 8) & 0xFF);
    EPD_3IN97_SendData((Xend*8) & 0xFF);
    EPD_3IN97_SendData(((Xend*8) >> 8) & 0xFF);
	
    EPD_3IN97_SendCommand(0x45);
    EPD_3IN97_SendData(Ystart & 0xFF);
    EPD_3IN97_SendData((Ystart >> 8) & 0xFF);
    EPD_3IN97_SendData(Yend & 0xFF);
    EPD_3IN97_SendData((Yend >> 8) & 0xFF);
    
    EPD_3IN97_SendCommand(0x4E); 
    EPD_3IN97_SendData((Xstart*8) & 0xFF);
    EPD_3IN97_SendData(((Xstart*8) >> 8) & 0xFF);
	
    EPD_3IN97_SendCommand(0x4F);
    EPD_3IN97_SendData(Ystart & 0xFF);
    EPD_3IN97_SendData((Ystart >> 8) & 0xFF);

	EPD_3IN97_SendCommand(0x24);   //Write Black and White image to RAM
    for (i = 0; i < IMAGE_COUNTER; i++) {
	    EPD_3IN97_SendData(Image[i]);
	}

	EPD_3IN97_TurnOnDisplay_Part();
    // Save the visible rectangle as the previous image for the next differential refresh.
    EPD_3IN97_SendCommand(0x4E);
    EPD_3IN97_SendData((Xstart*8)&255);
    EPD_3IN97_SendData((Xstart*8)>>8);
    EPD_3IN97_SendCommand(0x4F);
    EPD_3IN97_SendData(Ystart&255);
    EPD_3IN97_SendData(Ystart>>8);
    EPD_3IN97_SendCommand(0x26);
    for(i=0;i<IMAGE_COUNTER;i++)EPD_3IN97_SendData(Image[i]);
}

// 刷新完成后让屏幕控制器休眠，墨水屏仍会保留已有画面。
void EPD_3IN97_Sleep(void)
{
    EPD_3IN97_SendCommand(0x10); //enter deep sleep
    EPD_3IN97_SendData(0x01);
    DEV_Delay_ms(100);
}
void gpioInit() {
    pinMode(PIN_CFG_BTN, INPUT_PULLUP);
    pinMode(PIN_EPD_BUSY, INPUT);
    pinMode(PIN_EPD_DC, OUTPUT);
    pinMode(PIN_EPD_RST, OUTPUT);
    pinMode(PIN_EPD_CS, OUTPUT);
    digitalWrite(PIN_EPD_CS, HIGH);
    SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, -1);
    SPI.setFrequency(4000000);
    SPI.setDataMode(SPI_MODE0);
}
void epdInit() { EPD_3IN97_Init(); }
void epdInitFast() { EPD_3IN97_Init_Fast(); }
void epdDisplay(const uint8_t *image) {
    epdInit();
    EPD_3IN97_Display_Base(image);
}
void epdDisplayDeepClear(const uint8_t *image) { epdDisplay(image); }
void epdDisplayFast(const uint8_t *image) {
    epdInitFast();
    EPD_3IN97_Display_Fast_Base(image);
}
bool epdSupportsPartialRefresh() { return true; }
void epdPartialDisplay(uint8_t *data, int x0, int y0, int x1, int y1) {
    if (!data || x0 < 0 || y0 < 0 || x1 > W || y1 > H || x0 >= x1 || y0 >= y1) return;
    EPD_3IN97_Display_Partial(data, x0 & ~7, y0, (x1 + 7) & ~7, y1);
}
void epdPartialDisplayWithOld(uint8_t *data, const uint8_t *, int x0, int y0, int x1, int y1) {
    epdPartialDisplay(data, x0, y0, x1, y1);
}
void epdSleep() { EPD_3IN97_Sleep(); }
// InkSight sends monochrome frames for this profile.
void epdDisplay2bpp(const uint8_t *) {}
