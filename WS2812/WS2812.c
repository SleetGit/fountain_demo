#include "WS2812.h"
#include "tim.h"

// 全局数组，放在静态区，DMA 搬数据时绝对安全
uint16_t RGB_buffur[Reste_Data + Led_Num * Led_Data_Len] = { 0 };

// 设置第 num 个灯的颜色 (不会点亮，只是把数据写进内存)
void WS2812_SetColor(uint8_t num, uint8_t r, uint8_t g, uint8_t b)
{
    if (num >= Led_Num) return; // 防越界

    // WS2812 是 GRB 顺序
    uint32_t color = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;

    // 指针偏移，跳过开头的复位电平
    uint16_t* p = RGB_buffur + Reste_Data + (num * Led_Data_Len);

    // 高位先发，将 24 位数据转换为 PWM 占空比
    for (uint8_t i = 0; i < 24; i++) {
        // 从最高位 (bit 23) 开始判断
        p[i] = (color & (0x800000 >> i)) ? Hight_Data : Low_Data;
    }
}

// 真正的“发射”函数，调用后灯才会亮
void WS2812_Show(void)
{
    // 启动 PWM DMA 发送整个数组
    // 数组总长度 = 复位电平长度 + (灯数量 * 24)
    HAL_TIM_PWM_Start_DMA(&htim1, TIM_CHANNEL_1, (uint32_t *)RGB_buffur, Reste_Data + Led_Num * Led_Data_Len);
}

// 传输完成回调：DMA 配的是 NORMAL 模式（发完即止），这里把 PWM-DMA 关掉收尾
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if(htim == &htim1) {
        HAL_TIM_PWM_Stop_DMA(&htim1, TIM_CHANNEL_1);
    }
}