/**
  * @file TB6612.c
  * @brief TB6612FNG 直流电机底层驱动（水泵专用，单方向）
  * @author SleetGit
  * @date 2026/11/25
  *
  * @note 外设初始化由 CubeMX 生成的代码负责（见 TB6612.h 顶部说明），
  *       本文件只做：启动 PWM、方向与使能脚控制、占空比换算。
  */

#include "TB6612.h"

static uint8_t s_inited = 0;   // 1 = PWM 已启动
static int     s_duty   = 0;   // 当前占空比 0~100

/**
  * @brief 占空比 -> PWM 比较值
  * @return 比较值；参数非法返回 -1
  */
static int speed_convert(int dutyRatio)
{
    if ((dutyRatio < 0) || (dutyRatio > 100)) return -1;
    return (dutyRatio * (int) TB6612_PWM_RESOLUTION);
}

/**
  * @brief 设置方向脚为“水泵正转”这一组固定电平
  * @note  单向抽水，方向写死；装反了就把 AO1/AO2 两根线对调，不要改代码
  */
static void dir_set_forward(void)
{
    HAL_GPIO_WritePin(TB6612_AIN1_PORT, TB6612_AIN1_PIN, TB6612_DIR_AIN1_SET);
    HAL_GPIO_WritePin(TB6612_AIN2_PORT, TB6612_AIN2_PIN, TB6612_DIR_AIN2_SET);
}

void TB6612_Init(void)
{
    // PB11/PB12/PB13 已在 MX_GPIO_Init() 配成推挽输出且初值低电平（STBY 低 = 待机），
    // 所以上电瞬间电机不会动；这里只启动 PWM 并置成停止态。
    HAL_TIM_PWM_Start(TB6612_PWM_TIMER, TB6612_PWM_CHANNEL);
    __HAL_TIM_SET_COMPARE(TB6612_PWM_TIMER, TB6612_PWM_CHANNEL, 0);

    HAL_GPIO_WritePin(TB6612_AIN1_PORT, TB6612_AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_AIN2_PORT, TB6612_AIN2_PIN, GPIO_PIN_RESET);

    TB6612_Enable(1);

    s_duty   = 0;
    s_inited = 1;
}

int TB6612_Start(int dutyRatio)
{
    int ccr;

    if (!s_inited) return -1;
    ccr = speed_convert(dutyRatio);
    if (ccr < 0) return -2;

    if (dutyRatio == 0) {
        return TB6612_Stop();          // 0% 等同于停止
    }

    dir_set_forward();
    __HAL_TIM_SET_COMPARE(TB6612_PWM_TIMER, TB6612_PWM_CHANNEL, (uint32_t) ccr);
    s_duty = dutyRatio;

    return 0;
}

int TB6612_SetDuty(int dutyRatio)
{
    int ccr;

    if (!s_inited) return -1;
    ccr = speed_convert(dutyRatio);
    if (ccr < 0) return -2;

    if (dutyRatio == 0) {
        return TB6612_Stop();
    }

    // 只在没转的时候补一次方向（先 Stop 再 SetDuty 的情况）
    if (s_duty == 0) {
        dir_set_forward();
    }

    __HAL_TIM_SET_COMPARE(TB6612_PWM_TIMER, TB6612_PWM_CHANNEL, (uint32_t) ccr);
    s_duty = dutyRatio;

    return 0;
}

int TB6612_Stop(void)
{
    if (!s_inited) return -1;

    // 滑行停止：PWM 清零 + IN1=IN2=0（输出高阻，电机自由转）
    __HAL_TIM_SET_COMPARE(TB6612_PWM_TIMER, TB6612_PWM_CHANNEL, 0);
    HAL_GPIO_WritePin(TB6612_AIN1_PORT, TB6612_AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_AIN2_PORT, TB6612_AIN2_PIN, GPIO_PIN_RESET);
    s_duty = 0;

    return 0;
}

int TB6612_Brake(void)
{
    if (!s_inited) return -1;

    // 短路刹车：IN1=IN2=1，电机绕组被短接，快速停住
    __HAL_TIM_SET_COMPARE(TB6612_PWM_TIMER, TB6612_PWM_CHANNEL, 0);
    HAL_GPIO_WritePin(TB6612_AIN1_PORT, TB6612_AIN1_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(TB6612_AIN2_PORT, TB6612_AIN2_PIN, GPIO_PIN_SET);
    s_duty = 0;

    return 0;
}

void TB6612_Enable(uint8_t on)
{
#if TB6612_USE_STBY
    HAL_GPIO_WritePin(TB6612_STBY_PORT, TB6612_STBY_PIN,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    (void) on;   // STBY 直接接 3.3V 时无可控制
#endif
}

int TB6612_GetDuty(void)
{
    if (!s_inited) return -1;
    return s_duty;
}
