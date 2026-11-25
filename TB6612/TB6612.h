/**
  * @file TB6612.h
  * @brief TB6612FNG 直流电机驱动（水泵专用，单方向）
  * @author SleetGit
  * @date 2026/11/25
  *
  * @note 接口移植自 Automated_Sorting_System/l298n/l298n_driver.c
  *       （宏配置 + Init/Start/Stop + 0~100 占空比 + 0/-1/-2 返回码）。
  *
  *       真值表：IN1=1 IN2=0 正转 ｜ IN1=0 IN2=1 反转
  *               IN1=0 IN2=0 滑行停止 ｜ IN1=1 IN2=1 短路刹车
  *       本驱动只做单向（水泵），方向固定 IN1=1 / IN2=0，只用 PWM 调速。
  *
  *       ★ TIM3 与 PA6/PB11/PB12/PB13 全部由 CubeMX 生成，本驱动只负责启动 PWM、
  *         控制方向脚与 STBY、换算占空比。换引脚或改频率要改 fountain_demo.ioc
  *         重新生成，再同步下面的宏名。
  */

#ifndef TB6612_H
#define TB6612_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

// ============================================================================
// TB6612 硬件配置 - 必须与 fountain_demo.ioc 中的配置一致
// ============================================================================

// AIN1 / AIN2 方向脚（ioc: PB11 / PB12 → GPIO_Output）
#define TB6612_AIN1_PORT         GPIOB
#define TB6612_AIN1_PIN          GPIO_PIN_11

#define TB6612_AIN2_PORT         GPIOB
#define TB6612_AIN2_PIN          GPIO_PIN_12

// STBY 使能脚（ioc: PB13 → GPIO_Output）
// 如果模块把 STBY 直接接到 3.3V，可以把 TB6612_USE_STBY 改成 0
#define TB6612_USE_STBY          1
#define TB6612_STBY_PORT         GPIOB
#define TB6612_STBY_PIN          GPIO_PIN_13

// PWM：TIM3_CH1 = PA6（ioc: TIM3 通道1，PSC=0，ARR=3600-1 → 20 kHz）
// htim3 由 CubeMX 在 Core/Src/tim.c 中定义，这里直接引用
#define TB6612_PWM_TIMER         (&htim3)
#define TB6612_PWM_CHANNEL       TIM_CHANNEL_1
#define TB6612_PWM_PERIOD        3599u                              // 与 ioc 的 3600-1 对应

// 占空比 100 对应 CCR = ARR+1，即常高
#define TB6612_PWM_MAX_VALUE     (TB6612_PWM_PERIOD + 1u)
#define TB6612_PWM_RESOLUTION    (TB6612_PWM_MAX_VALUE / 100u)      // 36

// 单方向：水泵只能往一个方向抽水，方向脚固定成这一组
#define TB6612_DIR_AIN1_SET      GPIO_PIN_SET
#define TB6612_DIR_AIN2_SET      GPIO_PIN_RESET

// ============================================================================
// 驱动函数声明
// ============================================================================

/**
  * @brief  启动 PWM 并把电机置为停止状态
  * @note   引脚的 GPIO 方向与 TIM3 时基已由 CubeMX 的 MX_GPIO_Init / MX_TIM3_Init 完成，
  *         本函数只启动 PWM 输出、把方向脚与 PWMA 清零。重复调用安全。
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
