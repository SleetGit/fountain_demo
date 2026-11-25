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
// 【要改时间就改这一行】字段顺序：年, 月, 日, 星期, 时, 分, 秒
//   · 当 DS1302_SET_TIME_ON_BOOT = 1 时，它就是"要写进芯片的时间"；
//   · 平时它是读回来之后的当前时间快照（OLED 显示的就是它）。
DS1302_Time_t now_time = {2026, 11, 24, 2, 23, 44, 0};
volatile uint8_t flag_1s = 0;                          // 1 秒到点标志（中断里置位，主循环清零）
uint8_t rtc_status = DS1302_OK;                        // DS1302_OK / DS1302_ERR_UNSTABLE / DS1302_ERR_BAD_BCD

/* ---------------- 【改时间就用这里，两步走】 ----------------
 *  1) 把下面宏改成 1，并把文件上方 now_time 那一行的初值改成你要的时间，烧录一次；
 *  2) 把宏改回 0，再烧录一次。
 *  之后就再也不写时间了，DS1302 自己走：
 *     · 板子不断电 -> 一直走；
 *     · 断电后   -> 靠纽扣电池保留，没电池就归零。
 *  平时保持 0 即可。
 * ------------------------------------------------------- */
#define DS1302_SET_TIME_ON_BOOT 0


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
    if (flag_1s == 1) {          // 每秒同步一次 RTC（降速后读一次约 6ms，不适合放中断里）
        flag_1s = 0;
        DS1302_Time_t fresh;
        uint8_t st = DS1302_GetTime(&fresh);
        rtc_status = st;
        if (st == DS1302_OK) {
            now_time = fresh;    // 只有读稳了才更新；读到坏数据就保持上一次的好值，屏上显示 --
        }
    }
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
    OLED_Updata_Date();
    OLED_Updata_Time();
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
/**
 * @brief  显示日期行：Date:26-11-24
 * @note   MEDIUM 字体 8 像素宽，"Date:" 占 x=0~39，
 *         后面三组两位数字落在 48 / 72 / 96，分隔符在 64 / 88，总宽 112 < 128
 */
void OLED_Updata_Date(void)
{
    OLED_DrawStr(0, 32, (uint8_t *)"Date:", MEDIUM, 0);
    OLED_DrawNum0(48, 32, now_time.year % 100, MEDIUM, 0);
    OLED_DrawChar(64, 32, '-', MEDIUM, 0);
    OLED_DrawNum0(72, 32, now_time.month, MEDIUM, 0);
    OLED_DrawChar(88, 32, '-', MEDIUM, 0);
    OLED_DrawNum0(96, 32, now_time.day, MEDIUM, 0);
}
/**
 * @brief  显示时间行：Time:18:05:30
 * @note   秒位同时兼任状态指示：
 *           正常        -> 显示秒数
 *           -- (不稳定) -> 连读三次都不一致，总线接触不良
 *           ## (非法BCD) -> 芯片里是坏 BCD，计数器已经失效
 */
void OLED_Updata_Time(void)
{
    OLED_DrawStr(0, 48, (uint8_t *)"Time:", MEDIUM, 0);
    OLED_DrawNum0(48, 48, now_time.hour, MEDIUM, 0);
    OLED_DrawChar(64, 48, ':', MEDIUM, 0);
    OLED_DrawNum0(72, 48, now_time.minute, MEDIUM, 0);
    OLED_DrawChar(88, 48, ':', MEDIUM, 0);

    if (rtc_status == DS1302_OK) {
        OLED_DrawNum0(96, 48, now_time.second, MEDIUM, 0);
    } else if (rtc_status == DS1302_ERR_UNSTABLE) {
        OLED_DrawChar(96, 48, '-', MEDIUM, 0);
        OLED_DrawChar(104, 48, '-', MEDIUM, 0);
    } else {
        OLED_DrawChar(96, 48, '#', MEDIUM, 0);
        OLED_DrawChar(104, 48, '#', MEDIUM, 0);
    }
}

void user_main_init()
{
    HAL_TIM_Base_Start_IT(&htim4);
    OLED_Init();
    delay_init(72);              // DS1302 的位操作延时依赖它，必须排在 DS1302_Init() 前面

    // ---------------- DS1302 实时时钟 ----------------
    DS1302_Init();

#if DS1302_SET_TIME_ON_BOOT
    // 【写时间】把上面 now_time 的初值写进芯片。
    // 烧完把开头那个宏改回 0，再烧一次就完事。
    // 注意顺序：必须先写、后读 —— 下面的 GetTime 会把 now_time 覆盖成芯片里的真实时间。
    (void)DS1302_SetTime(&now_time);   // 内部会读回校验；写不进去屏上秒位会显示 ##
#endif

    rtc_status = DS1302_GetTime(&now_time);   // 先读一次，避免开机第一秒显示空值

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
            // 通知主循环：该去读一次 RTC 了（读 DS1302 要 1ms 左右，放中断里不合适）
            flag_1s = 1;
        }

        // 计数器每 2 秒归零：既防止一直累加，也保证 % 取模的循环点固定
        if (TIM4_tick_ms >= 2000) {
            TIM4_tick_ms = 0;
        }
    }
}
