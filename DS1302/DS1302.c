/**
  * @file DS1302.c
  * @brief DS1302 三线实时时钟驱动（CE / SCLK / DAT 位操作）
  * @author SleetGit
  * @date 2026/11/24
  *
  * @note 这个驱动里踩过的坑，都写在对应函数上方：
  *       1) 连续访问之间 CE 要留 tCWH(4us)，否则后面的写会被芯片忽略；
  *       2) 读 7 个寄存器要读两遍比对，否则跨进位时会读到"分新秒旧"的组合；
  *       3) 判数据合法性必须看原始字节的半字节，不能看转换后的十进制。
  */

#include "DS1302.h"
#include "delay.h"
#include <string.h>

// ---------------- 电平操作 ----------------
#define DS1302_CE_H()      HAL_GPIO_WritePin(DS1302_CE_PORT,   DS1302_CE_PIN,   GPIO_PIN_SET)
#define DS1302_CE_L()      HAL_GPIO_WritePin(DS1302_CE_PORT,   DS1302_CE_PIN,   GPIO_PIN_RESET)
#define DS1302_SCLK_H()    HAL_GPIO_WritePin(DS1302_SCLK_PORT, DS1302_SCLK_PIN, GPIO_PIN_SET)
#define DS1302_SCLK_L()    HAL_GPIO_WritePin(DS1302_SCLK_PORT, DS1302_SCLK_PIN, GPIO_PIN_RESET)
#define DS1302_DAT_H()     HAL_GPIO_WritePin(DS1302_DAT_PORT,  DS1302_DAT_PIN,  GPIO_PIN_SET)
#define DS1302_DAT_L()     HAL_GPIO_WritePin(DS1302_DAT_PORT,  DS1302_DAT_PIN,  GPIO_PIN_RESET)
#define DS1302_DAT_READ()  HAL_GPIO_ReadPin(DS1302_DAT_PORT,   DS1302_DAT_PIN)

// SCLK 每半个周期保持多久(us)：这个值是抗干扰的关键。
//   DS1302 本身是静态器件，没有最低时钟频率要求，所以可以放心降速。
//     1  -> 约 250kHz，杜邦线短(<10cm)时没问题，长线容易乱码
//     10 -> 约 25kHz，长杜邦线/接触一般时的推荐值（当前）
//     50 -> 约 5kHz，线特别烂/接触不良时的保底值
#define DS1302_HALF_US     10

// DS1302 要求的三个传输间隔(us)
#define DS1302_T_CC_US     4    // CE 拉高 -> 第一个 SCLK
#define DS1302_T_CCH_US    4    // 最后一个 SCLK -> CE 拉低
#define DS1302_T_CWH_US    4    // CE 拉低 -> 下一次 CE 拉高

// 上一次成功读时间时用的原始寄存器值（秒/分/时/日/月/星期/年）
// 调试时直接拿这份，保证和 now_time 是同一时刻的数据，不会出现"两行不同步"
static uint8_t s_last_raw[7];

// ---------------- 内部函数声明 ----------------
static void    DS1302_DatOut(void);
static void    DS1302_DatIn(void);
static void    DS1302_WriteByte(uint8_t dat);
static uint8_t DS1302_ReadByte(void);
static void    DS1302_ReadRaw(uint8_t raw[7]);
static void    DS1302_RawToTime(const uint8_t raw[7], DS1302_Time_t *time);
static uint8_t DS1302_IsBcd(uint8_t val);
static uint8_t DS1302_IsRawValid(const uint8_t raw[7]);
static uint8_t DS1302_BcdToDec(uint8_t bcd);
static uint8_t DS1302_DecToBcd(uint8_t dec);

/**
  * @brief 把 DAT 切成推挽输出（写数据时用）
  */
static void DS1302_DatOut(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin   = DS1302_DAT_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DS1302_DAT_PORT, &GPIO_InitStruct);
}

/**
  * @brief 把 DAT 切成输入（读数据时用）
  */
static void DS1302_DatIn(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    GPIO_InitStruct.Pin  = DS1302_DAT_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    // 用内部上拉：读的时候虽然由 DS1302 主动驱动，但切换方向的那几微秒总线是浮空的，
    // 有上拉就能保证它是确定的"高"，不会浮出一个假的低电平被当成数据位收进来。
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(DS1302_DAT_PORT, &GPIO_InitStruct);
}

/**
  * @brief 发一个字节（低位先发，上升沿被 DS1302 采样）
  */
static void DS1302_WriteByte(uint8_t dat)
{
    for (uint8_t i = 0; i < 8; i++) {
        if (dat & 0x01) {
            DS1302_DAT_H();
        } else {
            DS1302_DAT_L();
        }
        delay_us(DS1302_HALF_US);

        DS1302_SCLK_H();                    // 上升沿：DS1302 采样
        delay_us(DS1302_HALF_US);
        DS1302_SCLK_L();
        delay_us(DS1302_HALF_US);

        dat >>= 1;                          // 低位先发
    }
}

/**
  * @brief 收一个字节（低位先收，下降沿后数据有效）
  */
static uint8_t DS1302_ReadByte(void)
{
    uint8_t dat = 0;

    for (uint8_t i = 0; i < 8; i++) {
            delay_us(DS1302_HALF_US);          // 下降沿之后数据才稳定

        if (DS1302_DAT_READ() == GPIO_PIN_SET) {
            dat |= 0x80;
        }

        DS1302_SCLK_H();                    // 给一个上升沿，DS1302 换下一位
        delay_us(DS1302_HALF_US);
        DS1302_SCLK_L();
        delay_us(DS1302_HALF_US);

        if (i < 7) {
            dat >>= 1;                      // 低位先收
        }
    }

    return dat;
}

/**
  * @brief BCD 转十进制
  */
static uint8_t DS1302_BcdToDec(uint8_t bcd)
{
    return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

/**
  * @brief 十进制转 BCD
  */
static uint8_t DS1302_DecToBcd(uint8_t dec)
{
    return (uint8_t)(((dec / 10) << 4) | (dec % 10));
}

/**
  * @brief 判断一个字节是不是合法 BCD（高、低半字节都必须 <= 9）
  */
static uint8_t DS1302_IsBcd(uint8_t val)
{
    return (((val >> 4) <= 9) && ((val & 0x0F) <= 9)) ? 1 : 0;
}

/**
  * @brief 检查 7 个原始字节是否合法
  * @note  关键点：不能"转成十进制再看范围"。0x1A 会被转成 20，看着完全合法，
  *        但它是非法 BCD —— 芯片内部的 BCD 进位逻辑此时已经失效，会一直在坏值上乱加。
  */
static uint8_t DS1302_IsRawValid(const uint8_t raw[7])
{
    if (raw == NULL) return 0;

    if (DS1302_IsBcd(raw[0]) == 0 || raw[0] > 0x59) return 0;                   // 秒 00~59
    if (DS1302_IsBcd(raw[1]) == 0 || raw[1] > 0x59) return 0;                   // 分 00~59
    if (DS1302_IsBcd(raw[2]) == 0 || raw[2] > 0x23) return 0;                   // 时 00~23
    if (DS1302_IsBcd(raw[3]) == 0 || raw[3] < 0x01 || raw[3] > 0x31) return 0;  // 日 01~31
    if (DS1302_IsBcd(raw[4]) == 0 || raw[4] < 0x01 || raw[4] > 0x12) return 0;  // 月 01~12
    if (DS1302_IsBcd(raw[6]) == 0) return 0;                                    // 年 00~99

    // 星期(1~7)不参与判断：各家编号习惯不一样，不值得为了它把整个时间重写

    return 1;
}

/**
  * @brief 原始字节 -> 时间结构体
  */
static void DS1302_RawToTime(const uint8_t raw[7], DS1302_Time_t *time)
{
    time->second = DS1302_BcdToDec(raw[0]);
    time->minute = DS1302_BcdToDec(raw[1]);
    time->hour   = DS1302_BcdToDec(raw[2]);
    time->day    = DS1302_BcdToDec(raw[3]);
    time->month  = DS1302_BcdToDec(raw[4]);
    time->week   = DS1302_BcdToDec(raw[5]);
    time->year   = (uint16_t)(2000 + DS1302_BcdToDec(raw[6]));
}

/**
  * @brief 一次性读出 7 个寄存器的原始值（不做任何校验）
  */
static void DS1302_ReadRaw(uint8_t raw[7])
{
    raw[0] = DS1302_ReadReg(DS1302_REG_SEC)   & 0x7F;
    raw[1] = DS1302_ReadReg(DS1302_REG_MIN)   & 0x7F;
    raw[2] = DS1302_ReadReg(DS1302_REG_HOUR)  & 0x3F;
    raw[3] = DS1302_ReadReg(DS1302_REG_DATE)  & 0x3F;
    raw[4] = DS1302_ReadReg(DS1302_REG_MONTH) & 0x1F;
    raw[5] = DS1302_ReadReg(DS1302_REG_WEEK)  & 0x07;
    raw[6] = DS1302_ReadReg(DS1302_REG_YEAR);
}

/**
  * @brief 初始化 DS1302 用到的三个 GPIO
  * @note  放在驱动内部初始化，CubeMX 重新生成代码也不会被冲掉
  */
void DS1302_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    // CE 和 SCLK 一直是输出
    GPIO_InitStruct.Pin   = DS1302_CE_PIN | DS1302_SCLK_PIN;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    DS1302_CE_L();
    DS1302_SCLK_L();

    DS1302_DatOut();
    DS1302_DAT_H();

    memset(s_last_raw, 0, sizeof(s_last_raw));
}

/**
  * @brief 写单个寄存器
  * @param addr 寄存器地址（0x80 / 0x82 ... 这种写地址）
  * @param dat  要写入的原始值（BCD）
  */
void DS1302_WriteReg(uint8_t addr, uint8_t dat)
{
    DS1302_DatOut();
    DS1302_CE_H();
    delay_us(DS1302_T_CC_US);

    DS1302_WriteByte(addr);     // 命令字节，bit0 = 0 表示写
    DS1302_WriteByte(dat);      // 数据字节

    delay_us(DS1302_T_CCH_US);
    DS1302_CE_L();

    // 两次传输之间 CE 必须保持低电平至少 tCWH，否则下一次写会被芯片忽略
    delay_us(DS1302_T_CWH_US);
}

/**
  * @brief 读单个寄存器
  * @return 原始 BCD 值
  */
uint8_t DS1302_ReadReg(uint8_t addr)
{
    uint8_t dat;

    DS1302_DatOut();
    DS1302_CE_H();
    delay_us(DS1302_T_CC_US);

    DS1302_WriteByte((uint8_t)(addr | 0x01));   // 命令字节，bit0 = 1 表示读

    DS1302_DatIn();                             // 切成输入，把总线让给 DS1302
    dat = DS1302_ReadByte();

    delay_us(DS1302_T_CCH_US);
    DS1302_CE_L();
    DS1302_DatOut();

    delay_us(DS1302_T_CWH_US);

    return dat;
}

/**
  * @brief 读出完整时间（带撕裂读保护）
  * @param time 输出用的时间结构体指针
  * @return DS1302_OK / DS1302_ERR_UNSTABLE / DS1302_ERR_BAD_BCD
  *
  * @note 为什么要读两遍：7 个寄存器是逐个读的，整个读取要几百微秒。
  *       如果正好跨过"59 秒 -> 00 秒、同时分 +1"，就会读到"分是新值、秒是旧值"的组合，
  *       表现出来就是"分刚 +1 又退回去 -1"。读两遍、秒和分都一样才采用，就没这个问题。
  */
uint8_t DS1302_GetTime(DS1302_Time_t *time)
{
    uint8_t r1[7] = {0};
    uint8_t r2[7] = {0};

    if (time == NULL) return DS1302_ERR_UNSTABLE;

    for (uint8_t retry = 0; retry < 5; retry++) {
        DS1302_ReadRaw(r1);
        DS1302_ReadRaw(r2);

        // 两次读数之间没有发生进位，说明拿到的是稳定快照
        if (r1[0] == r2[0] && r1[1] == r2[1]) {
            DS1302_RawToTime(r2, time);
            memcpy(s_last_raw, r2, sizeof(s_last_raw));
            return DS1302_IsRawValid(r2) ? DS1302_OK : DS1302_ERR_BAD_BCD;
        }
    }

    // 三次都没读稳：先用最后一次，让显示不要卡死
    DS1302_RawToTime(r2, time);
    memcpy(s_last_raw, r2, sizeof(s_last_raw));
    return DS1302_ERR_UNSTABLE;
}

/**
  * @brief 写入完整时间，并读回校验
  * @param time 时间结构体（年份填 2000 ~ 2099）
  * @return 0 = 写入并校验通过；1 = 校验失败（写不进去，多半是接线问题）
  */
uint8_t DS1302_SetTime(const DS1302_Time_t *time)
{
    uint8_t raw[7] = {0};
    uint8_t sec_bcd;

    if (time == NULL) return 1;

    sec_bcd = (uint8_t)(DS1302_DecToBcd(time->second) & 0x7F);   // bit7 = CH，写 0 才起振

    DS1302_WriteReg(DS1302_REG_WP, 0x00);                        // 解除写保护

    DS1302_WriteReg(DS1302_REG_SEC,   sec_bcd);
    DS1302_WriteReg(DS1302_REG_MIN,   DS1302_DecToBcd(time->minute));
    DS1302_WriteReg(DS1302_REG_HOUR,  DS1302_DecToBcd(time->hour));   // bit7 = 0 -> 24 小时制
    DS1302_WriteReg(DS1302_REG_DATE,  DS1302_DecToBcd(time->day));
    DS1302_WriteReg(DS1302_REG_MONTH, DS1302_DecToBcd(time->month));
    DS1302_WriteReg(DS1302_REG_WEEK,  DS1302_DecToBcd(time->week));
    DS1302_WriteReg(DS1302_REG_YEAR,  DS1302_DecToBcd((uint8_t)(time->year % 100)));

    DS1302_WriteReg(DS1302_REG_WP, 0x80);                        // 重新上写保护

    // 读回校验：写不进去的话（比如 CE 虚焊），这里能立刻发现
    DS1302_ReadRaw(raw);

    if (DS1302_IsRawValid(raw) == 0) return 1;
    if (raw[0] != sec_bcd)                        return 1;
    if (raw[1] != DS1302_DecToBcd(time->minute))  return 1;
    if (raw[2] != DS1302_DecToBcd(time->hour))    return 1;

    return 0;
}

/**
  * @brief 判断时钟是否停振
  * @return 1 = 停振（刚上电 / 换过电池 / 没装电池，时间不可信）；0 = 正常走时
  */
uint8_t DS1302_IsHalted(void)
{
    return (DS1302_ReadReg(DS1302_REG_SEC) & 0x80) ? 1 : 0;
}

/**
  * @brief 取上一次成功读时间时用的原始字节（调试用）
  * @param raw raw[0]=秒 raw[1]=分 raw[2]=时 raw[3]=日 raw[4]=月 raw[5]=星期 raw[6]=年
  * @note  这份数据和 now_time 来自同一次读取，所以调试行和显示行永远对得上
  */
void DS1302_GetLastRaw(uint8_t raw[7])
{
    if (raw == NULL) return;
    memcpy(raw, s_last_raw, sizeof(s_last_raw));
}
