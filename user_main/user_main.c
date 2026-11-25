/**
  * @file user_main.c
  * @brief 用户主程序的入口文件
  * @author SleetGit
  * @date 2026/11/21
  */

#include "user_main.h"
#include "delay.h"
#include "tim.h"
#include "adc.h"
#include "driver_ssd1306.h"
#include "WS2812.h"
#include "DS1302.h"
#include "TB6612.h"

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
volatile uint8_t flag_100ms = 0;                       // 100 ms 到点标志（中断里置位，主循环清零）
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

/* ---------------- 水泵循环演示 ----------------
 *  PUMP_CYCLE_DEMO=1 时，水泵按“工作 PUMP_ON_MS → 停 PUMP_OFF_MS”无限循环。
 *  状态机由 flag_100ms 推进，不要改成 delay_ms（主循环一停，屏幕就冻结了）。
 * ------------------------------------------------------- */
#define PUMP_CYCLE_DEMO       0          // 1=开启循环演示  0=关闭
#define PUMP_CYCLE_DUTY       100        // 工作时的转速 0~100（%）
#define PUMP_ON_MS            5000u      // 每次工作多久（毫秒）
#define PUMP_OFF_MS           5000u      // 每次停止多久（毫秒）


/**
  * @brief 主程序入口函数，初始化按键检测和运行逻辑
  * @return int
  */
int user_main()
{
    user_main_init();
    while (1) {
        run();
    }

    return 0; // 死循环不会走到这里，仅用于消除 "control reaches end of non-void function" 警告
}

/**
  * @brief 水泵循环服务：工作 → 停止 → 工作 ……（非阻塞）
  * @note  由 run() 在 flag_100ms 到点时调用，用 100 ms 拍数计时；
  *        不使用 delay_ms，所以抽水期间 OLED 照常刷新。
  */
static void pump_100ms_tick(void)
{
#if PUMP_CYCLE_DEMO
    static uint8_t  state   = 0;    // 0=待启动 1=工作中 2=停止中
    static uint32_t elapsed = 0;    // 当前状态已持续的 100 ms 拍数

    switch (state) {
    case 0:
        TB6612_Start(PUMP_CYCLE_DUTY);
        elapsed = 0;
        state   = 1;
        break;

    case 1:                         // 工作中 → 停止
        elapsed++;
        if (elapsed >= PUMP_ON_MS / 100u) {
            TB6612_Stop();
            elapsed = 0;
            state   = 2;
        }
        break;

    case 2:                         // 停止中 → 工作
        elapsed++;
        if (elapsed >= PUMP_OFF_MS / 100u) {
            TB6612_Start(PUMP_CYCLE_DUTY);
            elapsed = 0;
            state   = 1;
        }
        break;

    default:
        state   = 0;
        elapsed = 0;
        break;
    }
#endif
}

void run()
{
    /* RTC：每秒同步一次（读一次约 6 ms，放中断里不合适） */
    if (flag_1s == 1) {
        flag_1s = 0;
        DS1302_Time_t fresh;
        uint8_t st = DS1302_GetTime(&fresh);
        rtc_status = st;
        if (st == DS1302_OK) {
            now_time = fresh;    // 读到坏数据就保持上次好值，屏上秒位显示 --
        }
    }

    /* 屏幕：TIM4 每 100 ms 置一次 flag_100ms，主循环轮询到就刷一帧。 */
    if (flag_100ms == 1) {
        flag_100ms = 0;
        OLED_Updata();          // 先刷屏，再动电机
        pump_100ms_tick();
    }
}
/**
 * @brief  更新 WS2812 灯带显示（前 30 颗点亮为绿色）
 * @note   灯带内部锁存，不断电就一直保持最后一帧；
 *         要真正熄灭必须发全 0 再 Show() 一次。
 */
void WS2812_Updata(void)
{
    for (uint8_t i = 0; i < Led_Num; i++) {   // 灯数改 WS2812.h 的 Led_Num
        WS2812_SetColor(i, 0, 24, 0);
    }
    WS2812_Show();
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

    OLED_DrawStr(0, 0, "Voltage:", MEDIUM, 0);
    OLED_DrawNum(63, 0, int_part, MEDIUM, 0);
    OLED_DrawChar(63 + 9, 0, '.', MEDIUM, 0);
    OLED_DrawNum0(63 + 14, 0, dec_part, MEDIUM, 0);
}
void OLED_Updata_light(void)
{
    OLED_DrawStr(0, 16, "Light:", MEDIUM, 0);
    OLED_DrawNum(48, 16, light, MEDIUM, 0);
}
/**
 * @brief  显示日期行：Date:26-11-24
 * @note   MEDIUM 字体 8 像素宽，"Date:" 占 x=0~39，
 *         后面三组两位数字落在 48 / 72 / 96，分隔符在 64 / 88，总宽 112 < 128
 */
void OLED_Updata_Date(void)
{
    OLED_DrawStr(0, 32, "Date:", MEDIUM, 0);
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
    OLED_DrawStr(0, 48, "Time:", MEDIUM, 0);
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
    delay_init(72);              // DS1302 位操作延时依赖它，必须排在 DS1302_Init() 之前

    // ---------------- DS1302 实时时钟 ----------------
    DS1302_Init();

#if DS1302_SET_TIME_ON_BOOT
    /* 必须先写后读：下面的 GetTime 会把 now_time 覆盖掉 */
    (void)DS1302_SetTime(&now_time);   // 内部读回校验，写不进去屏上秒位显示 ##
#endif

    rtc_status = DS1302_GetTime(&now_time);   // 开机先读一次，避免首屏空值

    // ---------------- 水泵（TB6612，单方向） ----------------
    TB6612_Init();                 // 引脚与 TIM3 由 CubeMX 生成，这里只启 PWM 并保持停机

    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *) adc_dma_buffer, ADC_BUFFER_SIZE);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim == &htim4) {
        TIM4_tick_ms++;

        if (TIM4_tick_ms % 10 == 0) {
            for (int i = 0; i < ADC_BUFFER_SIZE; i++) {
                if (adc_dma_buffer[i] > max_adc_2s) {
                    max_adc_2s = adc_dma_buffer[i];
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

        if (TIM4_tick_ms % 100 == 0) {
            flag_100ms = 1;        // 刷屏要 ~25 ms 阻塞 I2C，交给主循环做
        }

        if (TIM4_tick_ms % 200 == 0) {
            HAL_GPIO_TogglePin(GPIOC,GPIO_PIN_13);
        }

        if (TIM4_tick_ms % 1000 == 0) {
            voltage_mv = (max_adc_2s * 3300) / 4095;   // 1 秒峰值 → mV
            max_adc_2s = 0;
            flag_1s = 1;                               // 通知主循环去读 RTC
        }

        // 每 2 秒归零：防止累加溢出，也固定取模循环点
        if (TIM4_tick_ms >= 2000) {
            TIM4_tick_ms = 0;
        }
    }
}
