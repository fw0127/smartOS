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

CI(`.github/workflows/ci.yml` 的 avr 任务)每次推送都会编译两个固件,并用 `make ci-sim-contract` / `make ci-sim-demo` 在 simavr 中运行、检查输出。

通用套件的两项异步检查(`critical: excludes async`、`notify: async ends wait`)假设异步上下文是能并发长循环、等主流程叫停的线程。单核 MCU 只有运行到完成的中断,无法这样表达,所以这里报告为 skipped。对目标真正重要的性质由 `avr_contract.c` 用 Timer2 单次中断另行检查:

- `avr: isr notify ends wait` — 中断里的 notify 结束进行中的等待
- `avr: critical blocks isr` — 嵌套临界区挡住中断,退出后放行
- `avr: now across overflow` — 关中断跨越 Timer1 回绕时时间仍单调正确
- `avr: wait across counter wrap` — 在 Timer1 将要回绕时开始等待(比较值随之回绕),16 次中醒来都不晚于截止时刻 1 ms(v0.6 新增,回应"比较匹配可能晚一整个计数周期"的疑问)
- `avr: 3 s monotonic` — 多次溢出后仍单调

建议后续把套件的异步桩改成"单步、可由中断承载"的形式(例如由周期中断反复调用、每次只做一步),这样 MCU 平台也能跑同一组检查。

## 目标板测试(v0.6 测试派发单,2026-10-16)

完整报告:[docs/测试报告_v0.6_AVR_Nano.md](../../docs/测试报告_v0.6_AVR_Nano.md)。**除 T3 外数据全部来自 simavr**(指令时序确定,可信度较高);实物板复测待逻辑分析仪到位后进行。

| 用例 | 模拟器结果 |
|---|---|
| T1 等待/通知竞态(10 万次相位扫描) | PASS:最大延迟 104 µs;去掉粘滞通知的变异体出现 2096 次 ≥ 200 ms,测试有效 |
| T2 环形缓冲 1 / 5 / 10 kHz,各 600 s | PASS:无丢失、无重复、无溢出;10 kHz 时 CPU 饱和(见下"容量") |
| T3 代码与 RAM | 见 05 篇"代码规模指标" |
| T4 执行开销 | 见下表 |
| T5 故障恢复 | T5.1–T5.5 PASS;T5.6 看门狗部分 PASS(断电部分待实物) |
| T6 压力 | 模拟 1 h(含一次 tick 回绕)+ 自然运行 1.5 h:errors 0、无 miss、无复位 |

### 执行开销(模拟器,ATmega328P @ 16 MHz,smartOs `239ced2`)

| 路径 | 典型 / 最大 |
|---|---|
| 派发一步(空 `run`,返回 `AGAIN`),8 个任务 | 89.2 / 92.1 µs |
| 周期任务完成一步(含 `l_advance_release`) | 145.4 / 148.0 µs ※ |
| `sos_event_set` / `sos_event_set_async`(中断中) | 14.6 / 16.4 µs |
| 中断 → 任务第一条语句(空闲睡眠中 / 内核忙) | 83.0 / 110.0 µs · 96.8 / 146.8 µs |
| 环形缓冲 `put` / `get`(2 字节) | 28.1 / 21.3 µs |
| 监控检查一次(8 个任务) | 44.0 / 47.9 µs |
| `sos_time_to_ticks` / `sos_task_set_alarm` | 117.5 / 211.4 µs(64 位运算) |

※ 其中约 35 µs 是每步一次的 32 位除法。v0.6.0 已去掉无错过周期时的除法(F1),预计降到约 110 µs,**需复测**。

**`dispatch_overhead` 推荐值**:96 µs(派发单规则:8 任务派发最大值向上取整到 4 µs);有周期任务时,在 F1 修正版复测之前保守取 152 µs。注意:这个量级意味着 Nano 上每步的内核开销与很多任务的预算本身相当——`examples/layered_pipeline` 的预算是按快得多的目标写的,在 Nano 上加入 96 µs 后 ingress / worker 的分析结论都是 FAIL,heartbeat 的高优先级负载达到 1.43(生成器现在报告"发散")。所以示例描述文件**没有**写入这个值;它属于具体目标上的应用配置。

### 容量

单元素"中断 put + notify + 唤醒 + 派发 + get"约 2300–2500 周期,**Nano 上可持续约 6–7 kHz**。10 kHz 时数据仍完整,但消费者占满 CPU,低优先级任务全部错过周期。高频数据源应在中断里批量入队(一次放多个元素或用更大的元素),或降低通知频率。

## 已知注意

- **Optiboot**:bootloader 会清零 MCUSR,`avr_reset_cause()` 读到的恒为 0,无法区分看门狗复位与上电。当前测试板为老 bootloader,不受影响;换板时注意。
- **simavr 1.6 的两处偏差**(用模拟器复现时注意,非 smartOs 问题):写 `TIFR1 = _BV(OCF1A)` 会清掉整个 TIFR1(吃掉挂起的溢出标志,可能丢 262 ms);已运行的看门狗不会按 `wdt_enable` 的新超时重新计时(HALT 后 15 ms 复位在模拟器里表现为 2 s)。
- 就绪位图查找(de Bruijn,32 位乘法)与 `1u << id`(32 位变量移位)在 AVR 上合计约占派发的 21 %(F2)。可用 `SOS_CFG_FIND_FIRST_SET` 换成按字节查找;AVR 上常量表默认进 RAM,需要 PROGMEM,属于平台侧优化,待有实测手段后再做。

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
