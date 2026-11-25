#ifndef WS2812_H
#define WS2812_H
#include "main.h"

#define Hight_Data    ( 64  )   // 1 码
#define Low_Data      ( 36  )   // 0 码
#define Reste_Data    ( 80  )   // 复位电平长度 (80*1.25us = 100us > 50us，足够)
#define Led_Num       (  30  )   // WS2812 灯个数
#define Led_Data_Len  ( 24  )   // 单个灯 24 位

// 全局缓存数组，必须在这里声明，并且大小包含复位信号
extern uint16_t RGB_buffur[Reste_Data + Led_Num * Led_Data_Len];

// 设置某个灯的颜色 (num 从 0 开始)
void WS2812_SetColor(uint8_t num, uint8_t r, uint8_t g, uint8_t b);

// 将缓存里的数据发送给 WS2812
void WS2812_Show(void);

#endif