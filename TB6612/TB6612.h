/**
  * @file TB6612.h
  * @brief TB6612FNG 直流电机底层驱动（水泵专用，单方向）
  * @author SleetGit
  * @date 2026/11/25
  *
  * @note 移植自 Automated_Sorting_System/l298n/l298n_driver.c，接口风格保持一致
  *       （宏配置 + Init/Start/Stop + 0~100 占空比 + 0/-1/-2 返回码）。
  *
  *       TB6612 与 L298N 的三点差别：
  *         1. TB6612 多一个 STBY 使能脚：低电平 = 待机，电机不输出
  *         2. TB6612 支持 20 kHz 以上 PWM，电机不啸叫（L298N 用 50 Hz 会抖）
  *         3. 真值表不同：
  *              IN1=1 IN2=0  → 正转
  *              IN1=0 IN2=1  → 反转
  *              IN1=0 IN2=0  → 滑行停止（输出高阻）
  *              IN1=1 IN2=1  → 短路刹车
  *
  *       本驱动只做单方向（水泵），方向脚固定 IN1=1 / IN2=0，
  *       调速只改 PWM 占空比。
  *
  *       ⚠️ 引脚由驱动自己初始化，不依赖 CubeMX（与 DS1302 驱动一致），
  *          所以 .ioc 里不用配 PA6/PB11/PB12/PB13。
  */

#ifndef TB6612_H
#define TB6612_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

// ============================================================================
// TB6612 硬件配置 - 使用预处理宏定义（要换引脚只改这里）
// ============================================================================

// AIN1 / AIN2 方向脚
#define TB6612_AIN1_PORT         GPIOB
#define TB6612_AIN1_PIN          GPIO_PIN_11

#define TB6612_AIN2_PORT         GPIOB
#define TB6612_AIN2_PIN          GPIO_PIN_12

// STBY 使能脚（高电平 = 正常工作，低电平 = 待机）
// 如果模块把 STBY 直接接到 3.3V，可以把 TB6612_USE_STBY 改成 0
#define TB6612_USE_STBY          1
#define TB6612_STBY_PORT         GPIOB
#define TB6612_STBY_PIN          GPIO_PIN_13

// PWM：TIM3_CH1 = PA6（TIM1 已被 WS2812 占用，TIM4 是 1 ms 节拍）
#define TB6612_PWM_TIMER         (&htim3_tb6612)
#define TB6612_PWM_CHANNEL       TIM_CHANNEL_1

// 计数时钟 72 MHz / (PSC+1) / (ARR+1) = 72M/(0+1)/(3599+1) = 20 kHz
#define TB6612_PWM_PRESCALER     0u
#define TB6612_PWM_PERIOD        3599u

// 占空比 100 对应 CCR = ARR+1，即常高
#define TB6612_PWM_MAX_VALUE     (TB6612_PWM_PERIOD + 1u)
#define TB6612_PWM_RESOLUTION    (TB6612_PWM_MAX_VALUE / 100u)   // 36

// 单方向：水泵只能往一个方向抽水，方向脚固定成这一组
#define TB6612_DIR_AIN1_SET      GPIO_PIN_SET
#define TB6612_DIR_AIN2_SET      GPIO_PIN_RESET

// ============================================================================
// 对外句柄（PWM 定时器由驱动内部创建并初始化）
// ============================================================================

extern TIM_HandleTypeDef htim3_tb6612;

// ============================================================================
// 驱动函数声明
// ============================================================================

/**
  * @brief  初始化 TB6612：配置 AIN1/AIN2/STBY 推挽输出 + TIM3_CH1 20 kHz PWM
  * @note   初始化完成后电机处于停止状态；重复调用是安全的
  */
void TB6612_Init(void);

/**
  * @brief  启动水泵并设定转速
  * @param  dutyRatio 占空比 0~100（%）
  * @return 0 成功；-1 驱动未初始化；-2 占空比超出范围
  */
int TB6612_Start(int dutyRatio);

/**
  * @brief  运行中调速，只改占空比、不改方向
  * @param  dutyRatio 占空比 0~100（%）
  * @return 0 成功；-1 驱动未初始化；-2 占空比超出范围
  */
int TB6612_SetDuty(int dutyRatio);

/**
  * @brief  停机（滑行停止：IN1=IN2=0，PWM=0），水泵自然减速
  * @return 0 成功；-1 驱动未初始化
  */
int TB6612_Stop(void);

/**
  * @brief  刹车（短路刹车：IN1=IN2=1，PWM=0），电机被短路快速停住
  * @note   水泵用不到，留给以后做轮子时用
  * @return 0 成功；-1 驱动未初始化
  */
int TB6612_Brake(void);

/**
  * @brief  控制 STBY 使能脚
  * @param  on 1 = 使能输出，0 = 进入待机（电机无输出）
  */
void TB6612_Enable(uint8_t on);

/**
  * @brief  读取当前设定的占空比
  * @return 0~100；驱动未初始化时返回 -1
  */
int TB6612_GetDuty(void);

#ifdef __cplusplus
}
#endif

#endif // TB6612_H
