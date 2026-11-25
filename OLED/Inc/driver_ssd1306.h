

#ifndef __OLED_H
#define __OLED_H


#include "stm32f1xx_hal.h"




// 本工程只用 I²C 方式驱动 SSD1306。原来那组 SPI 模式的
// OLED_DC/CS/SCK/MOSI 宏（PA12/PB12/PB13/PB15）已经删掉——
// 其中 PB12/PB13 现在是 TB6612 的方向/使能脚，留着容易误用。
#define WHICH_I2C hi2c1          // 按实际连接的 I²C 改
#define Max_Column 128
#define Max_Row    64

// 定义字体枚举，值为字体的高度
enum Font_Size
{
    SMALL = 6 ,
    MEDIUM = 12 ,
    BIG = 16
};
#define OLED0561_ADD	0x78  // OLED I2C地址（禁止修改）
#define I2C_CMD				0x00  // OLED I2C指令（禁止修改）
#define I2C_DATA 			0x40  // OLED I2C数据（禁止修改）

#define Write_CMD  0	//写指令
#define Write_DATA 1	//写数据

//-----------------I2C 抗干扰参数----------------
// 单次事务超时（毫秒）。原值是 0xffff ≈ 65 秒，被电机干扰打断后主循环会卡死几十秒，
// 表现为“屏幕冻结但 LED 还在闪”。失败就当帧作废，下一帧再刷。
#define OLED_I2C_TIMEOUT_MS   10
// 连续失败多少次认为总线被从机拉死，触发一次总线恢复
#define OLED_I2C_ERR_LIMIT    3

uint8_t  OLED_GetErrStreak(void);      // 当前连续失败次数
uint32_t OLED_GetErrTotal(void);       // 累计失败次数
uint32_t OLED_GetRecoverCount(void);   // 累计总线恢复次数
void     OLED_I2C_Recover(void);       // 手动触发一次总线恢复

//使用以下函数这是更新了缓冲区的数据，要刷新屏幕需要跟上OLED_Refresh_Gram()函数
void OLED_Text();
void OLED_Write_Byte(unsigned char dat, unsigned char cmd);  //发送1字节

void OLED_Init(void);  //oled初始化
void OLED_Clear(uint32_t backColor);  // 清屏
void OLED_Fill(uint32_t fill_Data);  //全屏填充
void OLED_Refresh_Gram(void);  //更新显存到oled(要刷新屏幕内容，就要执行一次)
	
void OLED_DisPlay_On(void);  //开启显示
void OLED_DisPlay_Off(void);  //关闭显示
void OLED_ColorTurn(uint8_t i);  //反显函数
void OLED_DisplayTurn(uint8_t i);  //屏幕旋转180度

int16_t OLED_DrawPoint(uint16_t x, uint16_t y, uint32_t color);  // 画点 x:0~127 y:0~63 color:1 or 0
void OLED_ClearPoint(uint8_t x,uint8_t y);  //清除一个点 x:0~127  y:0~63
void OLED_DrawLine(uint8_t x1,uint8_t y1,uint8_t x2,uint8_t y2);  //画线
void OLED_DrawCircle(uint8_t centerX, uint8_t centerY, uint8_t radius);  //画圆
void OLED_DrawRectangle(uint8_t x1, uint8_t y1, uint8_t x2, uint8_t y2);  //画矩形

void OLED_DrawChar(uint8_t x, uint8_t y, uint8_t ch, enum Font_Size fontSize,uint8_t Color_Turn);  //画字符
void OLED_DrawStr(uint8_t x, uint8_t y, const char * ch, enum Font_Size fontSize, uint16_t Color_Turn);  //画字符串
uint16_t Ten_Exponent(uint8_t exp);
void OLED_DrawNum(uint8_t x, uint8_t y, uint16_t num, enum Font_Size fontSize, uint16_t Color_Turn);  //画数字
void OLED_DrawNum0(uint8_t x, uint8_t y, uint16_t num, enum Font_Size fontSize, uint16_t Color_Turn);//一位数的时候强制两位01,02,...,09,10
void OLED_DrawCN(uint8_t x, uint8_t y, uint16_t N, uint16_t Color_Turn);  //画汉字
void OLED_DrawBMP(uint8_t x, uint8_t y, uint8_t x1, uint8_t y1, unsigned char *BMP);  //画图片


#endif