/**
  * @file DS1302.h
  * @brief DS1302 实时时钟驱动（三线位操作：CE / SCLK / DAT）
  * @author SleetGit
  * @date 2026/11/24
  *
  * @note 接线：CE -> PB0，SCLK -> PB1，DAT(I/O) -> PB5
  *       硬件还需要：32.768kHz 晶振 + 纽扣电池（否则断电后时间不保留）
  *
  *       为什么 DAT 不用 PB2：Blue Pill 上 PB2 就是 BOOT1，板载 10k 下拉还接着启动跳线，
  *       拿它做双向数据线会被下拉拖住。
  */

#ifndef __DS1302_H
#define __DS1302_H

#include "main.h"

// ---------------- 引脚定义（要换口只改这里） ----------------
#define DS1302_CE_PORT       GPIOB
#define DS1302_CE_PIN        GPIO_PIN_0

#define DS1302_SCLK_PORT     GPIOB
#define DS1302_SCLK_PIN      GPIO_PIN_1

#define DS1302_DAT_PORT      GPIOB
#define DS1302_DAT_PIN       GPIO_PIN_5

// ---------------- 寄存器地址（bit0 = 1 表示读，驱动内部自动处理） ----------------
#define DS1302_REG_SEC       0x80   // 秒   bit7 = CH（时钟停振位）
#define DS1302_REG_MIN       0x82   // 分
#define DS1302_REG_HOUR      0x84   // 时   bit7 = 12/24 制选择
#define DS1302_REG_DATE      0x86   // 日
#define DS1302_REG_MONTH     0x88   // 月
#define DS1302_REG_WEEK      0x8A   // 星期
#define DS1302_REG_YEAR      0x8C   // 年（00 ~ 99 代表 2000 ~ 2099）
#define DS1302_REG_WP        0x8E   // 写保护 bit7：1 = 保护

// ---------------- 读时间的状态码 ----------------
#define DS1302_OK            0      // 读到稳定且合法的数据
#define DS1302_ERR_UNSTABLE  1      // 连读多次都不一致（总线接触不良）
#define DS1302_ERR_BAD_BCD   2      // 数据是非法 BCD（芯片内部计数器已失效）

// ---------------- 时间结构体 ----------------
typedef struct {
    uint16_t year;      // 2000 ~ 2099
    uint8_t  month;     // 1 ~ 12
    uint8_t  day;       // 1 ~ 31
    uint8_t  week;      // 1 ~ 7（编号习惯由使用者自己定）
    uint8_t  hour;      // 0 ~ 23
    uint8_t  minute;    // 0 ~ 59
    uint8_t  second;    // 0 ~ 59
} DS1302_Time_t;

// ---------------- 对外接口 ----------------
void    DS1302_Init(void);                            // 初始化 GPIO（不需要 CubeMX 配置）
uint8_t DS1302_SetTime(const DS1302_Time_t *time);    // 写时间并读回校验；0 = 成功
uint8_t DS1302_GetTime(DS1302_Time_t *time);          // 读时间；返回上面的状态码
uint8_t DS1302_IsHalted(void);                        // 1 = 时钟停振（掉电/换电池后）
void    DS1302_GetLastRaw(uint8_t raw[7]);            // 取上次成功读数用的原始字节（调试用，与 now_time 同源）
void    DS1302_WriteReg(uint8_t addr, uint8_t dat);   // 写单个寄存器（BCD 原始值）
uint8_t DS1302_ReadReg(uint8_t addr);                 // 读单个寄存器（BCD 原始值）

#endif
