/**
  * @file user_main.c
  * @brief 用户主程序的入口文件
  * @author SleetGit
  * @date 2026/11/21
  */

#include "user_main.h"

#define ADC_BUFFER_SIZE 64

uint32_t TIM4_tick_ms = 0;
uint32_t voltage_mv = 0; // 电压值，单位毫伏 (例如 3300 表示 3.300V)
uint16_t adc_dma_buffer[ADC_BUFFER_SIZE] = {0}; // DMA 接收缓冲区，存放 ADC 原始值
uint32_t max_adc_2s = 0; // 记录这 2 秒内的最大 ADC 原始值
uint32_t light = 0;


/**
  * @brief 主程序入口函数，初始化按键检测和运行逻辑
  * @return int
  */
int user_main()
{
    user_main_init();
    while (1) {
        // 运行主逻辑
        run();
    }

    return 0; // 死循环不会走到这里，仅用于消除 "control reaches end of non-void function" 警告
}

void run()
{
    OLED_Updata();
}
/**
 * @brief  更新 WS2812 灯带显示 (前30个灯点亮为绿色)
 * @note   为什么代码改了/注释了，灯还亮着？
 *         1. WS2812 内部有“锁存”功能，发完颜色只要不断电，就会一直保持。
 *         2. 重新烧录只是让 STM32 复位了，灯带的电没断，所以会“定格”在上次颜色。
 *         3. 要真正熄灭，必须用 SetColor 发全 0，再执行一次 Show()；
 */
void WS2812_Updata(void)
{
    for (uint8_t i = 0; i < Led_Num; i++) {//只有30个灯从0开始，要修改灯的数量请前往WS2812.h修改
        WS2812_SetColor(i, 0, 24, 0);
    }
    WS2812_Show();                 // 发射数据！
}
void OLED_Updata(void)
{
    OLED_Clear(0x00);
    OLED_Updata_Voltage();
    OLED_Updata_light();
    OLED_Refresh_Gram();
}
void OLED_Updata_Voltage(void)
{
    // 1. 拆分整数和小数（保留两位小数）
    uint32_t int_part = voltage_mv / 1000;
    uint32_t dec_part = (voltage_mv % 1000) / 10;

    OLED_DrawStr(0, 0, (uint8_t *)"Voltage:", MEDIUM, 0);
    OLED_DrawNum(63, 0, int_part, MEDIUM, 0);
    OLED_DrawChar(63 + 9, 0, '.', MEDIUM, 0);
    OLED_DrawNum0(63 + 14, 0, dec_part, MEDIUM, 0);
}
void OLED_Updata_light(void)
{
    OLED_DrawStr(0, 16, (uint8_t *)"Light:", MEDIUM, 0);
    OLED_DrawNum(48, 16, light, MEDIUM, 0);
}

void user_main_init()
{
    HAL_TIM_Base_Start_IT(&htim4);
    OLED_Init();
    delay_init(72);
    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *) adc_dma_buffer, ADC_BUFFER_SIZE);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim == &htim4) {
        // 判断中断源是否为定时器htim4
        TIM4_tick_ms++; // 每次中断时增加计数

        if (TIM4_tick_ms % 10 == 0) {
            for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
                if (adc_dma_buffer[i] > max_adc_2s) {
                    max_adc_2s = adc_dma_buffer[i]; // 不断刷新这 1 秒内的最大值
                }
            }
        }

        if (TIM4_tick_ms % 50 == 0) {
            if (HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_9) == GPIO_PIN_SET) {
                light = 1;
            } else {
                light = 0;
            }
        }

        if (TIM4_tick_ms % 200 == 0) {
            HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13); // 每200毫秒翻转LED状态
        }

        if (TIM4_tick_ms % 1000 == 0) {
            // 把这 1 秒内采集到的最大原始值，换算成电压 (12 位满量程 4095 对应 3300 mV)
            voltage_mv = (max_adc_2s * 3300) / 4095;
            // 清零，开始记录下一秒的最高电平
            max_adc_2s = 0;
        }

        // 计数器每 2 秒归零：既防止一直累加，也保证 % 取模的循环点固定
        if (TIM4_tick_ms >= 2000) {
            TIM4_tick_ms = 0;
        }
    }
}
