# fountain_demo —— 喷泉灯 + 实时时钟

> **硬件**：STM32F103C8T6（Blue Pill）
> **外设**：WS2812 灯带 · ADC 电压采集 · 光强输入 · SSD1306 OLED · DS1302 实时时钟
> **工具链**：CLion + CMake + arm-none-eabi-gcc；配置：STM32CubeMX（`fountain_demo.ioc`，FW_F1 V1.8.7）
> **最后更新**：2026-11-25

---

## 1. 功能一览

| # | 功能 | 实现方式 | 状态 |
|:--:|---|---|:--:|
| 1 | **实时时钟** | DS1302 三线位操作（CE/SCLK/DAT），主循环每秒读一次，掉电靠纽扣电池走时 | ✅ |
| 2 | **电压采集** | PA0 / ADC1_IN0，64 点循环 DMA，每秒取窗口内峰值换算成 mV | ✅ |
| 3 | **光强输入** | PB9 数字量，每 50 ms 采样一次 | ✅ |
| 4 | **OLED 显示** | SSD1306 128×64，I²C1 400 kHz，四行界面 | ✅ |
| 5 | **运行指示** | PC13 板载 LED，每 200 ms 翻转一次 | ✅ |
| 6 | **WS2812 灯带** | TIM1_CH1 PWM + DMA 合成 800 kHz 单总线时序，30 颗 GRB | ⚠️ 驱动就绪，主循环未调用 |
| 7 | **水泵控制** | TB6612FNG + TIM3_CH1(PA6) 20 kHz PWM，单方向启停与调速 | ✅ 代码就绪，待接线 |

> 第 6 项：`WS2812_Updata()` 已实现但 `run()` 里没有调用，所以灯带一直停在上次锁存的颜色。
> 要启用只要在 `run()` 开头加一行 `WS2812_Updata();`（注意每次执行约 1 ms，别丢进主循环里反复发，只发一次就挪进 `user_main_init()`）。

### 数据流

```
PA0 ──ADC1_IN0──▶ 64点循环DMA ──TIM4(10ms)扫峰值──▶ max ──TIM4(1000ms)换算──▶ voltage_mv ─┐
PB9 ──GPIO输入─────────────────TIM4(50ms)──────────────────────▶ light ──────────────────┤
PB0/PB1/PB10 ──DS1302──▶ 每秒读一次 ──▶ now_time + rtc_status ───────────────────────────┤
                                                                                          ▼
                                                                            run() ──▶ OLED_Updata()
                                                                                          │
PA8 ──TIM1_CH1 PWM──▶ DMA1_CH2 ──▶ WS2812 ×30 ◀── WS2812_Updata()（当前未被调用）
```

---

## 2. OLED 界面说明

128×64 分四行，字体 `MEDIUM`（8×16），行高 16 px：

| 行 | y 坐标 | 内容 | 显示示例 |
|:--:|:--:|---|---|
| 1 | 0 | 电压（整数 + 两位小数） | `Voltage:3.30` |
| 2 | 16 | 光强（0 / 1） | `Light:1` |
| 3 | 32 | 日期 `年-月-日`（年份只显示后两位） | `Date:26-11-24` |
| 4 | 48 | 时间 `时:分:秒` | `Time:18:05:30` |

### 秒位兼任 RTC 状态指示

第 4 行的秒位不只是数字，它同时告诉你 DS1302 读得怎么样：

| 显示 | 对应状态码 | 含义 |
|---|:--:|---|
| `Time:18:05:30` | `DS1302_OK` | 读到稳定且合法的数据 |
| `Time:18:05:--` | `DS1302_ERR_UNSTABLE` | 连读多次都不一致 → 总线接触不良、杜邦线太长、干扰大 |
| `Time:18:05:##` | `DS1302_ERR_BAD_BCD` | 芯片里是非法 BCD → 内部计数器已失效（多见模块晶振不起振或假货） |

读到坏数据时，`now_time` **保持上一次的好值**不清零，所以屏幕不会乱跳，只是秒位变成 `--` / `##`。

### I²C 抗干扰（电机噪声防护）

水泵启停/换向会在 I²C 上引入干扰。驱动侧做了三件事，让一次干扰最多丢一帧，而不是"屏幕冻结几十秒、LED 还在闪"：

| 机制 | 说明 |
|---|---|
| **限时事务** | 单次 I²C 超时从 `0xFFFF`（≈65 秒）改成 `OLED_I2C_TIMEOUT_MS = 10` 毫秒 |
| **分页批量写** | `OLED_Refresh_Gram()` 从 1034 次单字节事务改为 8 页 × 2 次 = **16 次**，一帧约 25 ms |
| **自动总线恢复** | 连续失败 `OLED_I2C_ERR_LIMIT = 3` 次 → `OLED_I2C_Recover()`：关 I²C → SCL/SDA 切成开漏 GPIO → 打 9 个 SCL 时钟 + 手工 STOP → 重新 `MX_I2C1_Init()` |

传输失败时**当帧直接放弃**（不再重试同一字节），下一帧继续刷。

可用来观察总线健康状况：

```c
uint8_t  OLED_GetErrStreak(void);      // 当前连续失败次数
uint32_t OLED_GetErrTotal(void);       // 累计失败次数
uint32_t OLED_GetRecoverCount(void);   // 累计总线恢复次数
void     OLED_I2C_Recover(void);       // 手动触发一次恢复
```

`run()` 里的 OLED 刷新改成**由 TIM4 中断置标志位驱动**：中断每 100 ms 置一次 `flag_100ms`，主循环轮询到就刷一帧（10 Hz）。时间基准统一在 TIM4 一个地方，不用 `HAL_GetTick()` 算差值，也不会出现"主循环被阻塞后补刷好几帧"。

> ⚠️ 这些只是**软件兜底**。根因仍在硬件：泵的脉冲电流通过地线和长杜邦线耦合到 I²C。
> 必做的三件事：① 星形单点接地；② `VM`–`GND` 加 470 µF 以上储能电容；③ 泵线双绞并远离 I²C 走线。

---

## 3. 硬件接线

| 引脚 | 外设 / 模式 | 用途 | 备注 |
|---|---|---|---|
| **PA0** | ADC1_IN0，模拟输入 | 电压采样 | 0–3.3 V，12 位，满量程 4095 |
| **PA8** | TIM1_CH1，AF 推挽 | WS2812 `DIN` | 800 kHz PWM 合成单总线时序 |
| **PB0** | GPIO 推挽输出 | **DS1302 `CE`（RST）** | 复位/片选 |
| **PB1** | GPIO 推挽输出 | **DS1302 `SCLK`** | 约 30 kHz（每 bit 3 段 × `DS1302_HALF_US = 10 µs`） |
| **PB10** | GPIO 输出 / 输入（方向切换） | **DS1302 `DAT`（双向）** | 建议外接 4.7k–10k 上拉 |
| **PA6** | TIM3_CH1，AF 推挽 | **TB6612 `PWMA`** | 20 kHz PWM，占空比调速 |
| **PB11** | GPIO 推挽输出 | **TB6612 `AIN1`** | 单方向：固定输出高 |
| **PB12** | GPIO 推挽输出 | **TB6612 `AIN2`** | 单方向：固定输出低 |
| **PB13** | GPIO 推挽输出 | **TB6612 `STBY`** | 高电平使能，低电平待机 |

> 以上 4 个脚和 TIM3 全部在 **CubeMX 里配置**（`fountain_demo.ioc`），生成代码负责：
> `MX_TIM3_Init()` 配 PSC=0 / ARR=3600-1，`HAL_TIM3_MspInit()` 开 TIM3 时钟，
> `HAL_TIM_MspPostInit()` 把 PA6 配成 AF_PP，`MX_GPIO_Init()` 把 PB11/PB12/PB13 配成推挽输出且初值低电平。
| **PB6 / PB7** | I²C1 SCL / SDA，开漏 | SSD1306 OLED | 400 kHz，7 位地址 `0x78` |
| **PB9** | GPIO 输入（无上下拉） | 光强 / 开关量输入 | 见下方注意事项 |
| **PC13** | GPIO 推挽输出 | 板载 LED（低电平点亮） | 200 ms 翻转 |
| **PA13 / PA14** | SWD | ST-Link / DAP-Link | |
| **PD0 / PD1** | HSE 8 MHz 晶振 | 系统时钟源 | PLL×9 = 72 MHz |

### DS1302 的外部元件

| 元件 | 是否必需 | 说明 |
|---|:--:|---|
| 32.768 kHz 晶振 | 必需 | 时钟基准，模块一般自带 |
| 纽扣电池（CR2032） | 走时必须 | 没有电池则每次上电时间从零开始 |
| DAT 上拉电阻 4.7k–10k | 建议 | PB10 内部上拉偏弱，杜邦线较长时容易读到乱码 |
| 100 nF 去耦电容 | 建议 | 靠近 DS1302 电源脚 |

### 两个硬件坑

- **不要用 PB2 接 DAT**：Blue Pill 上 PB2 就是 `BOOT1`，板载 10 kΩ 下拉还接着启动跳线，拿它做双向数据线会被下拉拖住。
- **PB9 配的是 `GPIO_NOPULL`**：接光敏模块的 DO（推挽输出）没问题；若接机械触点或裸光敏，建议改成 `GPIO_PULLUP`，否则悬空时读数随机。

### 水泵（TB6612FNG）接线

水泵是**单向**的，AIN1/AIN2 电平写死，只用 PWM 调速。

| TB6612 模块引脚 | 接到 | 说明 |
|---|---|---|
| `VM` | 水泵电源正极（按泵额定 5 V / 6 V / 12 V） | 电机功率电源 |
| `VCC` | **3.3 V** | 逻辑电源 |
| `GND` | **公共地**（STM32 的 GND 与泵电源负极接一起） | 必须共地 |
| `STBY` | **PB13**（或直接接 3.3 V） | 高电平工作 |
| `PWMA` | **PA6** | 20 kHz PWM |
| `AIN1` | **PB11** | 固定高 |
| `AIN2` | **PB12** | 固定低 |
| `AO1` / `AO2` | 水泵两根线 | 转向反了就把这两根对调，不要改代码 |
| `PWMB` / `BIN1` / `BIN2` / `BO1` / `BO2` | 不接 | 本工程只用 A 路 |

代码用法：

```c
TB6612_Init();        // 已在 user_main_init() 里调用，初始停机
TB6612_Start(70);     // 70% 转速抽水
TB6612_SetDuty(40);   // 运行中调速到 40%
TB6612_Stop();        // 停机（滑行）
TB6612_Brake();       // 短路刹车（水泵用不到，留给以后做轮子）
TB6612_Enable(0);     // STBY 拉低，整路待机
```

要验证接线，把 `user_main.c` 里的 `PUMP_SELF_TEST` 改成 `1`；转速和时长分别由 `PUMP_SELF_TEST_DUTY`（默认 100%）和 `PUMP_SELF_TEST_MS`（默认 5000 ms）控制。

> **自检是非阻塞的**：由 `flag_100ms` 驱动 `pump_100ms_tick()` 状态机推进，全程不用 `delay_ms`，
> 所以自检那 5 秒里 OLED 照常刷新、RTC 照常走。
> 以后给水泵加别的时序（定时启停、按液位动作）也在这个状态机里扩展——**任何电机动作都不要用 `delay_ms` 等**，
> 一旦阻塞主循环，屏幕就冻结了。

> ⚠️ 三个容易踩的坑：
> 1. **不要用 STM32 的 3.3 V 给水泵供电**。水泵启动电流通常是额定的 2–3 倍，必须单独供电，只共地。
> 2. **VM 与 GND 之间加去耦**：100–470 µF 电解 + 0.1 µF 陶瓷。泵启动瞬间会把电源拉低，不加大电容容易导致 STM32 复位（表现为 OLED 花屏、时钟乱走）。
> 3. **TB6612 单路持续 1.2 A、峰值 3.2 A**。超过就别硬撑，换大电流驱动板或 MOS 管方案。

---

## 4. 修改时间：两步烧录法

本工程不在运行时改时间，而是把时间"烧"进芯片，之后让 DS1302 自己走。

**第 1 步**：打开 `user_main/user_main.c`，改两处

```c
// ① 第 20 行附近，改成你要的时间。字段顺序：年, 月, 日, 星期, 时, 分, 秒
DS1302_Time_t now_time = {2026, 11, 24, 2, 23, 44, 0};

// ② 把开关置 1
#define DS1302_SET_TIME_ON_BOOT 1
```

烧录一次。`user_main_init()` 里的顺序是**先写后读**：

```c
(void)DS1302_SetTime(&now_time);   // 写进芯片，内部会读回校验
rtc_status = DS1302_GetTime(&now_time);   // 再把芯片里的真实时间读回来
```

**第 2 步**：把 `DS1302_SET_TIME_ON_BOOT` 改回 `0`，再烧录一次。

之后就不再写时间了：

- 板子不断电 → 一直走；
- 断电 → 靠纽扣电池继续走；
- 没装电池 → 断电后归零，秒位可能出现 `##`，需要重新走一次两步法。

> 写不进去时（`WP` 写保护没解开、或接触不良），屏上秒位会显示 `##`，据此可以判断。

---

## 5. 软件结构

```
fountain_demo/
├─ Core/                      CubeMX 生成，别手改（除非在 USER CODE 段内）
│  ├─ Inc/  main.h tim.h adc.h i2c.h gpio.h dma.h ...
│  └─ Src/  main.c  tim.c  adc.c  i2c.c  gpio.c  stm32f1xx_it.c ...
├─ user_main/                 ★ 业务逻辑
│  ├─ user_main.c             初始化、run() 主循环、四行显示绘制、TIM4 回调
│  └─ user_main.h
├─ DS1302/                    ★ DS1302 实时时钟驱动
│  ├─ DS1302.c                三线位操作、BCD 转换、双读校验、写后回读
│  └─ DS1302.h                引脚宏、寄存器地址、状态码、DS1302_Time_t
├─ TB6612/                    ★ 水泵电机驱动（单方向）
│  ├─ TB6612.c                方向脚 + TIM3_CH1 PWM，启停/调速/刹车
│  └─ TB6612.h                引脚宏、20 kHz 参数、接口声明
├─ WS2812/
│  ├─ WS2812.c                SetColor 拼占空比数组 / Show 启 DMA
│  └─ WS2812.h                时序常量 Hight_Data=64、Low_Data=36、Reste_Data=80、Led_Num=30
├─ OLED/
│  ├─ Inc/driver_ssd1306.h    128×64 字符/数字/汉字/图片 API
│  ├─ Inc/oledfont.h          8×6 / 16×8 / 32×16 三套字库
│  └─ Src/driver_ssd1306.c    GRAM 渲染 + I²C 整屏刷新
├─ delay/                     正点原子 SysTick 延时（DS1302 的位延时依赖它）
├─ CMakeLists.txt             模板生成 + 手工追加 target_sources
└─ fountain_demo.ioc          CubeMX 工程文件
```

### 调用链

```
main()                          Core/Src/main.c
 └─ user_main()                 user_main/user_main.c:39
     ├─ user_main_init()        :143
     │   ├─ HAL_TIM_Base_Start_IT(&htim4)        启动 1 ms 节拍
     │   ├─ OLED_Init()                            I²C 初始化 + SSD1306 上电序列
     │   ├─ delay_init(72)                         ★ 必须排在 DS1302_Init() 前面
     │   ├─ DS1302_Init()                          自配 PB0/PB1/PB10，不依赖 CubeMX
     │   ├─ DS1302_GetTime(&now_time)              开机先读一次，避免首屏空值
     │   ├─ TB6612_Init()                          启动 TIM3_CH1 PWM，初始停机（外设由 CubeMX 配）
     │   ├─ HAL_ADCEx_Calibration_Start(&hadc1)    ADC 自校准
     │   └─ HAL_ADC_Start_DMA(&hadc1, buf, 64)     ADC 连续转换 + 循环 DMA
     └─ while(1) run()          :50
         ├─ flag_1s ? → DS1302_GetTime()          每秒轮询一次 RTC
         └─ OLED_Updata()       :78  清 GRAM → 画四行 → 整屏刷新
             ├─ OLED_Updata_Voltage()  :87   Voltage:3.30
             ├─ OLED_Updata_light()    :98   Light:1
             ├─ OLED_Updata_Date()     :108  Date:26-11-24
             └─ OLED_Updata_Time()     :124  Time:18:05:30（含状态码）
```

> `DS1302_Init()` 在驱动内部自己配置 GPIO，所以即使 CubeMX 里没有这三个脚，也能正常工作。
> 但**要换引脚必须两处同时改**：`DS1302.h` 的 `DS1302_xxx_PIN/PORT` 宏，以及 `fountain_demo.ioc`。
>
> 水泵这边反过来：**外设全部由 CubeMX 生成**（TIM3 + PA6/PB11/PB12/PB13），
> 驱动只启动 PWM 和控制电平。换引脚要改 `fountain_demo.ioc` 重新生成，再同步 `TB6612.h` 的宏名。

---

## 6. 关键参数

### 6.1 系统节拍（TIM4，1 ms）

| 计数时钟 | 中断周期 |
|---|---|
| 72 MHz / 72 = 1 MHz | (ARR 999 + 1) / 1 MHz = **1 ms** |

| 条件 | 动作 |
|---|---|
| `tick % 10 == 0` | 扫一遍 64 点缓冲区，刷新窗口峰值 `max_adc_2s` |
| `tick % 50 == 0` | 读 PB9 → `light` |
| `tick % 200 == 0` | 翻转 PC13 |
| `tick % 1000 == 0` | `voltage_mv = max × 3300 / 4095`，清零峰值，置 `flag_1s` |
| `tick >= 2000` | 计数归零（防溢出，同时固定取模循环点） |

### 6.2 ADC

| 项 | 计算 | 结果 |
|---|---|---|
| ADC 时钟 | PCLK2 72 MHz / 6 | 12 MHz |
| 单次转换 | 55.5（采样）+ 12.5（转换）= 68 周期 | ≈ 5.7 µs |
| 64 点缓冲区刷新 | 64 × 5.7 µs | ≈ 363 µs |

所以 `% 10` 那段实际是：每 10 ms 扫一次缓冲，每次只看到最近 363 µs 的 64 个样本，一秒下来约抽检 3.6% 的时间。对缓慢变化的电源电压够用，很窄的尖峰会漏。

### 6.3 WS2812 时序合成

| 量 | 计算 | 结果 |
|---|---|---|
| 计数时钟 | 72 MHz / (PSC 0 + 1) | 72 MHz |
| PWM 周期 | (ARR 89 + 1) → 72 MHz / 90 | **800 kHz，1.25 µs/位** |
| 逻辑 1 高电平 | CCR 64 / 72 MHz | 0.889 µs |
| 逻辑 0 高电平 | CCR 36 / 72 MHz | 0.500 µs |
| 复位低电平 | 80 位 × 1.25 µs | 100 µs（要求 > 50 µs ✔） |
| DMA 缓冲区 | 80 + 30 × 24 | 800 × `uint16_t` = 1600 B |
| 整条刷新耗时 | 800 × 1.25 µs | 1 ms |

`WS2812_SetColor(num, r, g, b)` 把 24 bit 按 **GRB** 顺序逐位转成 64/36 写入 `RGB_buffur`，高位先发；`WS2812_Show()` 一次性用 `HAL_TIM_PWM_Start_DMA()` 打出去；`HAL_TIM_PWM_PulseFinishedCallback()` 里 `Stop_DMA` 收尾。

---

## 7. 已知问题

按影响排序，🔴 建议优先处理。

| 级别 | 问题 | 说明 |
|:--:|---|---|
| 🔴 | `WS2812_Updata()` 未被调用 | 灯带永远停在上次锁存的颜色，从没收到过新数据 |
| 🟠 | `voltage_mv` / `light` / `max_adc_2s` 缺 `volatile` | 中断里写、主循环里读，`-O2` 下可能读到旧值（`flag_1s` 已加） |
| 🟠 | WS2812 用循环 DMA + 回调里 `Stop_DMA` | 建议 `tim.c` 改成 `DMA_NORMAL`，"发完即止"语义更清楚 |
| 🟡 | OLED 坐标硬编码 | `63`、`63+9`、`63+14` 与 MEDIUM 字宽绑死，换字号或改文案就会重叠 |
| 🟡 | `main.h` 与 `user_main.h` 互相包含 | 改任何模块头都要重编整个工程 |
| 🟢 | `OLED_DrawStr` 形参是 `uint8_t *` | 调用处靠强转消警告，驱动本身应改成 `const char *` |
| 🟢 | 工程卫生 | `WS2812.h` 的 include guard 叫 `__RGB_H`；`driver_ssd1306.h` 还留着 SPI 模式宏；`CMakeLists.txt` 顶部注释与实际不符 |

---

## 8. 构建与烧录

```powershell
cd fountain_demo
cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug -j
```

产物：`cmake-build-debug/fountain_demo.elf / .hex / .bin`

- **CLion**：直接选 `fountain_demo` 配置，Run/Debug 走 OpenOCD + `stm32f1xx.cfg`
- **命令行烧录**（OpenOCD）：

```powershell
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg `
  -c "program cmake-build-debug/fountain_demo.elf verify reset exit"
```

---

## 9. 后续可加的功能

- [ ] 灯带做成真正的"喷泉"效果：呼吸 / 流水 / 随电压亮度映射
- [ ] ADC 采两路（电压 + 电流）做功率显示
- [ ] 光强改用 ADC 读模拟值，而不是 PB9 的 0/1
- [ ] `voltage_mv` 的"1 秒峰值"改成滑动平均，显示更稳
- [ ] OLED 加显示运行时间 / 帧率
- [ ] 按键或编码器，支持运行时改时间，不用两步烧录
