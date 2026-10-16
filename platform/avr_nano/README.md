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

这同时回答了 05 篇"目标代码体积待在真实平台上实测"的待决问题的第一个数据点:在 8 位 AVR 上,完整内核(v0.5.2)+ 平台适配 + demo 约 8.7 KB。

demo 把 `SOS_CFG_HEALTH_MIN_INTERVAL` 设为 250 ms(默认 10 ms):监控检查是空闲时的唤醒源之一,默认值下空闲约 107 次/秒唤醒,调整后约 8 次/秒。看门狗超时 2 s,250 ms 的喂狗间隔仍有 8 倍余量。

串口在 macOS 上是 `/dev/cu.wchusbserial*`(CH340)或 `/dev/cu.usbserial-*`;找不到时 `PORT=/dev/cu.xxx`。

demo 接线:按钮(或套件 KY-004 的 S 脚)接 D2 与 GND,已开内部上拉。

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
