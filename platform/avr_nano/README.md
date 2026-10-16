# 平台适配:Arduino Nano(ATmega328P,16 MHz,裸机)

smartOs 的第一个真实硅片平台。按附录 A "通用裸机 MCU" 一列实现 05 篇的 7 个契约函数,不依赖 Arduino core。

## 契约映射

| 契约 | 实现 |
|---|---|
| `TICK_BITS` / `TICKS_PER_SEC` | 32 / 250 000(Timer1 clk/64,4 µs 分辨率;可比较间隔约 2.4 h) |
| `now` | Timer1 16 位计数 + 软件溢出计数拼成 32 位,自然回绕;关中断读取并补偿"已溢出但中断未处理"的情况 |
| `critical_enter / exit` | 最外层保存 SREG 并 `cli`,按深度嵌套,退出最外层时恢复 SREG |
| `wait_until` | 把 OCR1A 设到截止时刻附近(最远 0xF000 tick),IDLE 睡眠;任何中断唤醒,允许提前返回 |
| `notify` | 粘性标志(单字节,AVR 上原子) |
| `health_signal` | `wdt_reset()`(demo 中看门狗 2 s) |
| `halt` | 串口打印原因 → 看门狗 15 ms 复位 |

关中断临界区最长可达一个 Timer1 溢出周期(262 ms)而不丢时间。

## 文件

- `avr_platform.c/.h` — 契约实现 + 板级辅助(串口、LED、复位原因)
- `avr_contract.c` — 契约一致性套件的目标端桩,另加 4 项 AVR 专用检查
- `avr_demo.c` + `sos_cfg.h` — 真内核在 Nano 上跑:按钮中断事件、LED 周期任务、串口报告、看门狗
- `Makefile` — avr-gcc 构建、avrdude 烧录

## 使用

```sh
brew tap osx-cross/avr && brew install avr-gcc avrdude
cd platform/avr_nano
make                     # 两个固件 + 尺寸
make flash-contract      # 老 bootloader 克隆板默认 57600;Optiboot 用 UPLOAD_BAUD=115200
make monitor             # 115200,期待最后一行 CONTRACT PASS
make flash-demo
```

没有板子时可以用模拟器跑同一份固件:`brew install simavr`,然后 `make sim-contract` / `make sim-demo`。

## 实测(smartOs v0.5.2,simavr 1.6,ATmega328P @ 16 MHz,avr-gcc 7.3,-Os)

| 固件 | Flash | SRAM(静态) | 结果 |
|---|---|---|---|
| `contract` | 4716 B(14.4 %) | 831 B | 通用 8 项 ok、2 项 skipped,AVR 专用 4 项 ok → `CONTRACT PASS` |
| `demo`(完整内核,4 任务表,环形缓冲关) | 8860 B(27.0 %) | 632 B | 1 s 周期任务 15 s 无漂移,`errors=0`,2 s 看门狗未触发 |

真板验证(2026-10-16,Arduino Nano 克隆板):`contract` 结果与模拟器逐项一致,`CONTRACT PASS`;`demo` 周期任务、看门狗与 INT0 按键事件均正常;启动时 `MCUSR=0x07`(上电 + 外部复位 + 掉电标志累积;Optiboot 会清零 MCUSR,所以这块板很可能是老 bootloader)。

**实物板(2026-10-16)**:`contract` 输出 `CONTRACT PASS`;`demo` 的 LED 闪烁、每秒串口报告(`errors=0`)、D2 按钮中断事件与看门狗均正常,长时间运行未发生看门狗复位。

这同时回答了 05 篇"目标代码体积待在真实平台上实测"的待决问题的第一个数据点:在 8 位 AVR 上,完整内核(v0.5.2)+ 平台适配 + demo 约 8.7 KB。

demo 把 `SOS_CFG_HEALTH_MIN_INTERVAL` 设为 250 ms(默认 10 ms):监控检查是空闲时的唤醒源之一,默认值下空闲约 107 次/秒唤醒,调整后约 8 次/秒。看门狗超时 2 s,250 ms 的喂狗间隔仍有 8 倍余量。

串口在 macOS 上是 `/dev/cu.wchusbserial*`(CH340)或 `/dev/cu.usbserial-*`;找不到时 `PORT=/dev/cu.xxx`。

demo 接线:普通按钮接 D2 与 GND(已开内部上拉);也可以先用一根杜邦线从 D2 碰一下 GND 来验证。
套件的 KY-004 模块板上自带 10 kΩ 电阻,不同批次有上拉也有下拉接法,三个脚要全接:S→D2,中间→5V,−→GND。只接 S 和 −、中间悬空时,若是下拉接法,D2 永远是低电平,不会触发中断。

## 异步上下文与契约套件

通用套件的两项异步检查(`critical: excludes async`、`notify: async ends wait`)假设异步上下文是能并发长循环、等主流程叫停的线程。单核 MCU 只有运行到完成的中断,无法这样表达,所以这里报告为 skipped。对目标真正重要的性质由 `avr_contract.c` 用 Timer2 单次中断另行检查:

- `avr: isr notify ends wait` — 中断里的 notify 结束进行中的等待
- `avr: critical blocks isr` — 嵌套临界区挡住中断,退出后放行
- `avr: now across overflow` — 关中断跨越 Timer1 回绕时时间仍单调正确
- `avr: 3 s monotonic` — 多次溢出后仍单调

建议后续把套件的异步桩改成"单步、可由中断承载"的形式(例如由周期中断反复调用、每次只做一步),这样 MCU 平台也能跑同一组检查。

## 已知注意

- 老 bootloader 的克隆板在看门狗复位后可能进入复位循环;本适配在 `.init3` 中尽早关闭看门狗,但若 bootloader 本身先超时仍可能循环。遇到时断电重启,或换 Optiboot。
- `sos_time_to_ticks` 等使用 64 位乘除,在 AVR 上较慢(软件实现),目前只在初始化和设闹钟时调用。

## demo 2:4x4 键盘 + LCD1602(`keypad_lcd/`)

三个任务共用一块屏幕的影子缓冲(运行到完成,无需加锁):

| 任务 | 优先级 | 触发 | 做什么 |
|---|---|---|---|
| `keypad` | 0 | 周期 10 ms,预算 1 ms | 扫描矩阵,连续 3 次相同才算按下(30 ms 消抖),编辑第 2 行 |
| `lcd` | 1 | 事件 `EV_DIRTY`,预算 8 ms,`again_limit` 4 | 每步最多把 4 个变化的字符推到 I2C,还有剩余就返回 `AGAIN` |
| `clock` | 2 | 周期 1 s | 第 1 行显示 `up HH:MM:SS k NNN`,串口打印各任务最长单步、超预算次数、错误数 |

按键:数字和 B C D 输入,`*` 删除,`#` 清空,`A` 开关背光。

接线:

| 器件 | 引脚 | Nano |
|---|---|---|
| 键盘排线 1..8(按键面朝自己,从左到右) | R1 R2 R3 R4 C1 C2 C3 C4 | D4 D5 D6 D7 D8 D9 D10 D11 |
| LCD I2C 背板 | GND / VCC / SDA / SCL | GND / 5V / A4 / A5 |

```sh
make flash-kpdlcd && make monitor
```

启动时串口打印 `LCD at I2C address 0x00000027`(或 `0x3F`)。找不到屏时继续运行,只用串口输出。屏亮但看不到字时,调背板上的蓝色电位器(对比度)。

模拟验证(simavr 1.6,测试台模拟 PCF8574 + HD44780 与 4x4 键盘,脚本按 28 次键,含前后各 3 次接触抖动):开机画面、输入、删除、清空、16 字符滚动、背光开关、时钟与计数全部正确,`overruns 0`,`errors 0`。simavr 1.6 对已应答的 SLA+W 报 0x28(真芯片是 0x18),模拟构建需加 `-DTWI_SIMAVR_QUIRK`;正式固件不加。模拟器里 I2C 几乎不耗时,真板上 LCD 单步约 2-4 ms。
