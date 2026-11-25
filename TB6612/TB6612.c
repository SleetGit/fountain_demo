/**
  * @file TB6612.c
  * @brief TB6612FNG 直流电机底层驱动（水泵专用，单方向）
  * @author SleetGit
  * @date 2026/11/25
  */

#include "TB6612.h"

// ============================================================================
// 内部变量
// ============================================================================

/* PWM 定时器句柄：TIM3 在 CubeMX 里没有配置，由本驱动自己建 */
TIM_HandleTypeDef htim3_tb6612;

static uint8_t s_inited = 0;   // 1 = 已初始化
static int     s_duty   = 0;   // 当前占空比 0~100

// ============================================================================
// 内部函数
// ============================================================================

/**
  * @brief 占空比 -> PWM 比较值
  * @param dutyRatio 占空比 0~100
  * @return 比较值；参数非法返回 -1
  */
static int speed_convert(int dutyRatio)
{
    if ((dutyRatio < 0) || (dutyRatio > 100)) return -1;
    return (dutyRatio * (int) TB6612_PWM_RESOLUTION);
}

/**
  * @brief 设置方向脚为"水泵正转"这一组固定电平
  * @note  水泵只能单向抽水，所以方向是写死的；如果装反了，
  *        把 AO1/AO2 两根线对调，不要改代码
  */
static void dir_set_forward(void)
{
    HAL_GPIO_WritePin(TB6612_AIN1_PORT, TB6612_AIN1_PIN, TB6612_DIR_AIN1_SET);
    HAL_GPIO_WritePin(TB6612_AIN2_PORT, TB6612_AIN2_PIN, TB6612_DIR_AIN2_SET);
}

// ============================================================================
// 驱动函数实现
// ============================================================================

void TB6612_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    // ---------------- 1. 方向脚 + STBY ----------------
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitStruct.Pin   = TB6612_AIN1_PIN | TB6612_AIN2_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TB6612_AIN1_PORT, &GPIO_InitStruct);

#if TB6612_USE_STBY
    GPIO_InitStruct.Pin = TB6612_STBY_PIN;
    HAL_GPIO_Init(TB6612_STBY_PORT, &GPIO_InitStruct);
    // 先拉低进入待机，等 PWM 配好再使能，避免上电瞬间抽一下
    HAL_GPIO_WritePin(TB6612_STBY_PORT, TB6612_STBY_PIN, GPIO_PIN_RESET);
#endif

    // 初始：滑行停止
    HAL_GPIO_WritePin(TB6612_AIN1_PORT, TB6612_AIN1_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TB6612_AIN2_PORT, TB6612_AIN2_PIN, GPIO_PIN_RESET);

    // ---------------- 2. PWM 引脚 PA6 = TIM3_CH1 ----------------
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitStruct.Pin   = GPIO_PIN_6;
    GPIO_InitStruct.Mode  = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    // ---------------- 3. TIM3 时基 + PWM 通道 ----------------
    __HAL_RCC_TIM3_CLK_ENABLE();

    htim3_tb6612.Instance               = TIM3;
    htim3_tb6612.Init.Prescaler         = TB6612_PWM_PRESCALER;
    htim3_tb6612.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim3_tb6612.Init.Period            = TB6612_PWM_PERIOD;
    htim3_tb6612.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim3_tb6612.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim3_tb6612) != HAL_OK) {
        s_inited = 0;
        return;
    }

    TIM_OC_InitTypeDef sConfigOC = {0};
    sConfigOC.OCMode     = TIM_OCMODE_PWM1;
    sConfigOC.Pulse      = 0;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3_tb6612, &sConfigOC, TB6612_PWM_CHANNEL) != HAL_OK) {
        s_inited = 0;
        return;
    }

    HAL_TIM_PWM_Start(TB6612_PWM_TIMER, TB6612_PWM_CHANNEL);
    __HAL_TIM_SET_COMPARE(TB6612_PWM_TIMER, TB6612_PWM_CHANNEL, 0);

    // ---------------- 4. 使能输出 ----------------
#if TB6612_USE_STBY
    HAL_GPIO_WritePin(TB6612_STBY_PORT, TB6612_STBY_PIN, GPIO_PIN_SET);
#endif

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
