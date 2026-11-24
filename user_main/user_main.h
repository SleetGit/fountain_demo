
#ifndef LJY_USER_MAIN_H
#define LJY_USER_MAIN_H

#ifdef __cplusplus
extern "C"
{
#endif
#include "main.h"
#include "stm32f1xx_hal.h"

int user_main();
void user_main_init();
void run();
void OLED_Updata_Voltage(void);
void OLED_Updata_light(void);
void OLED_Updata_Date(void);
void OLED_Updata_Time(void);
void OLED_Updata(void);
void WS2812_Updata(void);

#ifdef __cplusplus
}
#endif

#endif //LJY_USER_MAIN_H
