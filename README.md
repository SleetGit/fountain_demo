# fountain_demo —— 喷泉灯 Demo

> STM32F103C8T6（Blue Pill）+ WS2812 灯带 + ADC 电压采集 + 光强输入 + SSD1306 OLED
> 工具链：Clion + CMake + arm-none-eabi-gcc；配置：CubeMX（`fountain_demo.ioc`，FW_F1 V1.8.7）
> 最后修改：2026-11-22

---

## 1. 这个工程在做什么

一块 Blue Pill 同时干三件事：

1. **驱动 WS2812 灯带**（30 颗，GRB，TIM1_CH1 PWM + DMA 波形合成）——"喷泉"的灯效底座；
2. **采集模拟电压**（PA0 / ADC1_IN0，DMA 循环搬运 64 点，每秒取窗口内最大值换算成 mV）；
3. **在 0.96" OLED 上显示** 电压（如 `3.30`）和一路光强开关量（`Light: 0/1`）。

同时在 TIM4 的 1 ms 中断里做软件节拍：每 50 ms 采样一次光强输入（PB9），每 200 ms 翻转板载 LED（PC13），每 1000 ms 刷新一次电压值。

一句话数据流：

```
PA0 ──ADC1_IN0──▶ 64 点 DMA 循环缓冲 ──TIM4(10ms)取最大──▶ max_adc_2s ──TIM4(1000ms)换算──▶ voltage_mv ─┐
PB9 ──GPIO 输入──────────────────────────TIM4(50ms)──────▶ light ────────────────────────────────────┤
                                                                                                      ▼
                                                                                            run() → OLED_Updata()
                                                                                                      │
PA8 ──TIM1_CH1 PWM──▶ DMA1_CH2 ──▶ WS2812 30 颗灯 ◀── WS2812_Updata()（当前未被调用，见 #1）
```

---

## 2. 硬件接线

| 引脚 | 外设/模式 | 用途 | 备注 |
|---|---|---|---|
| PA0 | ADC1_IN0，模拟输入 | 电压采样 | 0–3.3 V，12 位，满量程 4095 |
| PA8 | TIM1_CH1，AF 推挽 | WS2812 `DIN` | 800 kHz PWM 合成单总线时序 |
| PB6 / PB7 | I²C1 SCL / SDA，开漏 | SSD1306 OLED | 400 kHz，7 位地址 `0x78` |
| PB9 | GPIO 输入（无上下拉） | 光强/开关量输入 | 代码里当"Light"用 |
| PC13 | GPIO 推挽输出 | 板载 LED（低电平点亮） | 200 ms 翻转一次 |
| PA13 / PA14 | SWD | ST-Link / DAP-Link | |
| PD0 / PD1 | HSE 8 MHz 晶振 | 系统时钟源 | PLL×9 = 72 MHz |

> ⚠️ PB9 配的是 `GPIO_NOPULL`，如果外接的是光敏模块的 DO（推挽输出）没问题；如果是机械触点/裸光敏，建议改 `GPIO_PULLUP`，否则悬空时读数随机。

---

## 3. 软件结构

```
fountain_demo/
├─ Core/                     CubeMX 生成，别手改（除非在 USER CODE 段内）
│  ├─ Inc/
│  │  ├─ main.h              ★ 额外 include 了 user_main.h / delay.h / tim.h /
│  │  │                        driver_ssd1306.h / adc.h / WS2812.h（在 USER CODE 段内）
│  │  ├─ tim.h adc.h i2c.h gpio.h dma.h
│  │  └─ stm32f1xx_it.h
│  └─ Src/
│     ├─ main.c              main() → HAL_Init → SystemClock_Config → 各 MX_xxx_Init → user_main()
│     ├─ tim.c               TIM1(PWM, ARR=89) / TIM4(1ms 时基) + DMA1_CH2
│     ├─ adc.c               ADC1 连续转换 + DMA1_CH1 循环
│     ├─ i2c.c               I2C1 400 kHz 主机
│     ├─ gpio.c              PC13 输出 / PB9 输入
│     └─ stm32f1xx_it.c      中断向量，转发给 HAL_xxx_IRQHandler
├─ user_main/                ★ 你的业务代码都在这
│  ├─ user_main.c            初始化、run() 主循环、显示绘制、TIM4 回调
│  └─ user_main.h
├─ WS2812/
│  ├─ WS2812.c               SetColor 拼 PWM 占空比数组 / Show 启 DMA / 完成后停 DMA
│  └─ WS2812.h               时序常量 Hight_Data=64、Low_Data=36、Reste_Data=80、Led_Num=30
├─ OLED/
│  ├─ Inc/driver_ssd1306.h   128×64 字符/数字/汉字/图片 API
│  ├─ Inc/oledfont.h         8×6 / 16×8 / 32×16 三套字库
│  ├─ Src/driver_ssd1306.c   GRAM 渲染 + I²C 整屏刷新
│  └─ Src/driver_i2c.c       软件 I²C（已从 CMakeLists 里注释掉，不再编译）
├─ delay/                    正点原子 SysTick 延时（delay_init / delay_ms / delay_us）
├─ CMakeLists.txt            模板生成 + 手工追加 target_sources
└─ fountain_demo.ioc         CubeMX 工程文件
```

### 调用链

```
main()                          Core/Src/main.c:68
 └─ user_main()                 user_main/user_main.c:23
     ├─ user_main_init()        :76
     │   ├─ HAL_TIM_Base_Start_IT(&htim4)        启动 1ms 节拍
     │   ├─ OLED_Init()                            I²C 初始化 + SSD1306 上电序列
     │   ├─ delay_init(72)                         SysTick 延时
     │   ├─ HAL_ADCEx_Calibration_Start(&hadc1)    ADC 自校准
     │   └─ HAL_ADC_Start_DMA(&hadc1, buf, 64)    ADC 连续转换 + 循环 DMA
     └─ while(1) run()           :34
         └─ OLED_Updata()        :52   清 GRAM → 画两行 → 整屏刷新
             ├─ OLED_Updata_Voltage()  :59   Voltage: 3.30
             └─ OLED_Updata_light()    :70   Light: 0
```

---

## 4. 关键时序与参数（自己算一遍，改参数时别踩坑）

### 4.1 WS2812 单总线时序合成

| 量 | 计算 | 结果 |
|---|---|---|
| TIM1 计数时钟 | 72 MHz / (PSC 0 + 1) | 72 MHz |
| PWM 周期 | ARR 89 + 1 = 90 → 72 MHz / 90 | **800 kHz，1.25 µs/位** |
| 逻辑 1 高电平 | CCR = `Hight_Data` = 64 → 64 / 72 MHz | **0.889 µs** |
| 逻辑 0 高电平 | CCR = `Low_Data` = 36 → 36 / 72 MHz | **0.500 µs** |
| 复位低电平 | `Reste_Data` = 80 位 × 1.25 µs | **100 µs**（要求 > 50 µs ✔） |
| DMA 缓冲区 | 80 + 30 × 24 | **800 个 `uint16_t` = 1600 B** |
| 一次整条刷新耗时 | 800 × 1.25 µs | **1 ms** |

`WS2812_SetColor(num, r, g, b)` 把 24 bit 按 **GRB** 顺序逐位转成 64/36 写入 `RGB_buffur`，高位先发；`WS2812_Show()` 一次性用 `HAL_TIM_PWM_Start_DMA()` 把 800 个半字打出去；`HAL_TIM_PWM_PulseFinishedCallback()`（WS2812.c:33）里 `Stop_DMA` 收尾。

> 💡 你注释里问的"为什么代码改了/注释了，灯还亮着"：WS2812 有锁存，只要不断电就保持最后一帧；**更关键的是本工程里 `WS2812_Updata()` 从来没被调用过**（见已知问题 #1），所以灯带一直是上一次上电时锁存的颜色。

### 4.2 系统节拍

| 项 | 计算 | 结果 |
|---|---|---|
| TIM4 计数时钟 | 72 MHz / 72 | 1 MHz |
| TIM4 中断周期 | ARR 999 + 1 = 1000 | **1 ms** |
| `TIM4_tick_ms % 10` | 10 ms | 扫一遍 64 点，刷新窗口最大值 |
| `TIM4_tick_ms % 50` | 50 ms | 读 PB9 → `light` |
| `TIM4_tick_ms % 200` | 200 ms | 翻转 PC13 |
| `TIM4_tick_ms % 1000` | **1000 ms** | `voltage_mv = max * 3300 / 4095`，清零 max |
| `TIM4_tick_ms >= 2000` | 2000 ms | 计数器归零（防溢出） |

> 📌 代码注释写的是"这两秒内采集到的最大值"，但实际 `% 1000` 是 **每 1 秒** 换算一次。已按实际逻辑修正注释。

### 4.3 ADC

ADC 时钟 = PCLK2(72 MHz) / 6 = 12 MHz；采样时间 55.5 周期 + 转换 12.5 周期 = 68 周期 ≈ **5.7 µs/次**。
连续转换 + 循环 DMA 时，64 点缓冲区约 **363 µs** 就被整体刷新一次。

所以 `% 10` 那一段的实际行为是：**每 10 ms 扫一次缓冲区，每次只看到最近 363 µs 的 64 个样本**。
一秒下来相当于抽检了约 3.6% 的时间——对"缓慢变化的电源电压 + 纹波"够用（能抓到较宽的峰），
但很窄的尖峰会被漏掉。想更稳就加大 `ADC_BUFFER_SIZE`（比如 256）或提高扫描频率。

---

## 5. 构建与烧录

```powershell
cd D:\STM32xmu\CLION\STM32F103C8T6\fountain_demo
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug -j
```

产物：`cmake-build-debug/fountain_demo.elf / .hex / .bin`。

- **Clion**：直接选 `fountain_demo` 配置，Run/Debug 走 OpenOCD + `stm32f1xx.cfg`。
- **命令行烧录**（OpenOCD）：
  ```powershell
  openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "program cmake-build-debug/fountain_demo.elf verify reset exit"
  ```

---

## 6. 已知问题与改进建议

按"影响大小"排序。标 🔴 的建议尽快处理。

### 🔴 #1 `WS2812_Updata()` 定义了但没人调用

`user_main.c:43-49` 写好了点灯函数，但 `run()`（`:32-35`）里只调了 `OLED_Updata()`。
→ 灯带永远停在上一帧（甚至从没收到过数据）。

```c
void run()
{
    WS2812_Updata();   // ← 补上这一行
    OLED_Updata();
}
```
若只想点一次，就把它挪进 `user_main_init()` 末尾，别放在主循环里反复发（每次 1 ms）。

### 🔴 #2 主循环没有节流，OLED 每轮全屏刷新，把 CPU 占满

`OLED_Refresh_Gram()`（`driver_ssd1306.c:146`）对每个字节单独调一次 `HAL_I2C_Mem_Write`：
8 页 × 128 列 = **1024 次独立 I²C 事务**。按 400 kHz、每次事务 START+地址+0x40+数据+STOP 约 70–100 µs 估算，
**一次整屏刷新要 70–110 ms**，而 `run()` 里没有任何延时，于是主循环只有 10 Hz 左右，几乎全部时间在刷屏。

两种改法（任选，推荐先做 A）：

**A. 加节流（最小改动，行为不变）**
```c
void run()
{
    static uint32_t t_last = 0;
    if (HAL_GetTick() - t_last >= 100) {   // 10 Hz 刷屏足够
        t_last = HAL_GetTick();
        OLED_Updata();
    }
}
```

**B. 按页整块发（治本，提速约 8~30 倍）**
给 `driver_ssd1306.c` 加一个"一页 128 字节一次性发送"的函数，`OLED_Refresh_Gram()` 改成调 8 次：
```c
static void OLED_WritePage(uint8_t page, const uint8_t *buf, uint16_t len)
{
    uint8_t cmd[2] = {0xB0 + page, 0x00};      // 页地址 + 列低地址
    HAL_I2C_Mem_Write(&WHICH_I2C, OLED0561_ADD, I2C_CMD, I2C_MEMADD_SIZE_8BIT, cmd, 2, 100);
    HAL_I2C_Mem_Write(&WHICH_I2C, OLED0561_ADD, I2C_DATA, I2C_MEMADD_SIZE_8BIT, (uint8_t*)buf, len, 100);
}
```
> 注意：SSD1306 一帧 128 字节没问题，但部分模块对 `0x40` 连续写的长度敏感，先用 32/64 字节分块试。

### 🟠 #3 主循环与中断共享的变量没有 `volatile`

`voltage_mv`、`light` 在 TIM4 中断里写、在 `run()` 里读；`max_adc_2s` 只在中断里用。
编译器（尤其 `-Og`/`-O2`）没有义务每次都重新从内存读 → 可能读到旧值。

```c
volatile uint32_t voltage_mv = 0;
volatile uint32_t light = 0;
```
（`adc_dma_buffer` 是 DMA 写的，也建议标 `volatile`，虽然这里靠"每次都重新扫数组"通常不会出事。）

### 🟠 #4 没有做 ADC 自校准

F1 的 ADC 上电后偏差可达几十个 LSB。建议在 `HAL_ADC_Start_DMA()` 之前加：
```c
HAL_ADCEx_Calibration_Start(&hadc1);
```

### 🟠 #5 WS2812 用的是循环 DMA，靠回调里 `Stop_DMA` 收尾

`tim.c:151` 配的是 `DMA_CIRCULAR`，而 `WS2812.c:33` 在 `PulseFinished` 回调里停 DMA。
能用，但语义别扭：正常模式（`DMA_NORMAL`）才是"发完即止"。若 `Show()` 在上一帧还没发完时被再次调用，会打断上一帧的时序。
建议把 `tim.c` 里 `hdma_tim1_ch1.Init.Mode` 改成 `DMA_NORMAL`（CubeMX 里 DMA Mode 选 Normal 即可）。

### 🟡 #6 灯数 `30` 写了两处

`user_main.c:45` 的 `for (i = 0; i < 30; i++)` 和 `WS2812.h:8` 的 `Led_Num (30)` 重复，改数量时容易漏。
→ 循环直接用 `Led_Num`。

### 🟡 #7 OLED 坐标是硬编码，和字宽绑死

`user_main.c:64-66`：`OLED_DrawNum(63, ...)`、`OLED_DrawChar(63+9, ...)`、`OLED_DrawNum0(63+14, ...)`。
MEDIUM 是 8×16 字模，"Voltage:" 8 个字符正好占 x=0–63，所以数字从 63 开始会压住冒号最后一列；
小数点画在 72、小数位画在 77，两者也有 3 px 重叠。

建议改成"右对齐"写法，换字号/改文案都不炸：
```c
// 例：把 "3.30" 画成整体，右端固定在 x=120
OLED_DrawNum(120 - 24, 0, int_part, MEDIUM, 0);
```

### 🟡 #8 头文件互相包含，改一处全量重编

`main.h` include 了 `user_main.h`，`user_main.h` 又 include `main.h`（靠 include guard 顶住）。
目前能跑，但 `main.h` 是 CubeMX 生成文件，所有模块都 include 它 → 改任何模块头都要重编整个工程。
建议：业务头文件只 include `stm32f1xx_hal.h`，把 `main.h` 的 include 收敛到 `.c` 里。

### 🟢 #9 编译警告：`OLED_DrawStr` 的形参是 `uint8_t *`，传字符串字面量会报 `-Wpointer-sign`

```
user_main.c:65:24: warning: pointer targets in passing argument 3 of 'OLED_DrawStr' differ in signedness
```
`driver_ssd1306.h:68` 声明为 `uint8_t *ch`，而 `"Voltage:"` 是 `char[]`。
最干净的做法是把驱动改成 `const char *`（顺带加 `const` 更安全）：
```c
void OLED_DrawStr(uint8_t x, uint8_t y, const char *ch, enum Font_Size fontSize, uint16_t Color_Turn);
```
（`driver_i2c.c` 若还在编译，同步改一下即可。）

### 🟢 #10 工程卫生

- `WS2812.h` 的 include guard 叫 `__RGB_H`，与实际文件名不符（`__` 开头还是保留标识符）。
- `driver_ssd1306.h:16-26` 还留着 SPI 版的 `OLED_DC/CS/SCK/MOSI` 宏（PA12/PB12/PB13/PB15），I²C 模式下用不到，容易和别的工程混淆。
- `CMakeLists.txt:1` 写着"此文件从模板自动生成! 请勿更改!"，但下面已经手工追加了 `target_sources` —— 文案与实际不符，建议改成"模板生成 + 手工追加"。
- `CMakeLists_template.txt`、`stm32f1xx.cfg`、`.settings/` 属于工具链附属文件，保留即可。

---

## 7. 下一步可以加的功能

- [ ] 灯带做成"喷泉"效果：呼吸 / 流水 / 音量或电压驱动的亮度映射（`WS2812_SetColor` + `HAL_Delay` 足够做）
- [ ] ADC 采 2 路（电压 + 电流）做功率显示
- [ ] 光强用 ADC 读模拟值，而不是 PB9 的 0/1
- [ ] 把 `voltage_mv` 的"1 秒峰值"改成滑动平均，显示更稳
- [ ] OLED 加第三行显示运行时间/帧率
