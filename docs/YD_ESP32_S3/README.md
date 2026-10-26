# YD-ESP32-S3 开发板使用说明

本项目选用 ESP32-S3 N16R8，软件使用 ESP-IDF。板级连接按 [YD-ESP32-S3 V1.4 原理图](YD-ESP32-S3-SCH-V1.4.pdf) 整理；资料包同时介绍其他容量型号，实板版本、完整模组型号及天线形式仍需与实物核对。

## 型号和项目配置

资料中的三个容量版本如下。容量和存储器接口见乐鑫 [WROOM-1 / WROOM-1U 数据手册，第 1.2 节](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)。

| 模组后缀 | Flash | PSRAM | 本项目用途 |
| --- | --- | --- | --- |
| N8R2 | 8 MB，Quad SPI | 2 MB，Quad SPI | 资料包中的其他型号 |
| N8R8 | 8 MB，Quad SPI | 8 MB，Octal SPI | 资料包中的其他型号 |
| N16R8 | 16 MB，Quad SPI | 8 MB，Octal SPI | 本项目选型 |

[板卡默认配置](../../boards/YD_ESP32_S3/sdkconfig.defaults) 已按 N16R8 设置：Flash 使用 DIO / 80 MHz，启用 16 MB 容量；PSRAM 使用 Octal / 80 MHz，并在启动时进行内存检查。Quad SPI 描述 Flash 接口能力，当前固件选择的读取模式是 DIO。

天线形式由模组型号决定：**WROOM-1 使用 PCB 天线，WROOM-1U 使用外接天线连接器**。项目需求是外接 2.4 GHz 天线，选板时应核对是否为 WROOM-1U 或具有对应天线连接电路的板型，不能仅凭 N16R8 判断。见 [乐鑫数据手册，第 1.1 节](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)。

## 两个 USB 接口

以下左右位置均指：**正面元件朝向自己，天线朝上，两个 Type-C 接口朝下**。可对照 [中文硬件介绍图](ESP32-S3-0702.PNG)。

| 位置 | 原理图编号 | 连接 | 用途 |
| --- | --- | --- | --- |
| 左侧 Type-C | USB2 | GPIO19 = D−，GPIO20 = D+ | ESP32-S3 原生 USB，可用于 USB Serial/JTAG；USB OTG 功能取决于固件配置 |
| 右侧 Type-C | USB1 | CH343P → UART0 | USB 转串口，通过 UART0 烧录和查看日志；配有 DTR / RTS 自动下载电路 |

原生 USB Serial/JTAG 支持串口日志、烧录和 JTAG 调试；USB OTG 与 USB Serial/JTAG 是不同控制器，使用同一原生接口时须按固件配置选择功能。见 [ESP-IDF v6.1 USB Serial/JTAG 说明](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/usb-serial-jtag-console.html)。

2026-10-26 开发机识别到的连接是原生 USB Serial/JTAG：

```text
设备：Espressif USB JTAG/serial debug unit
USB ID：303a:1001
串口：/dev/ttyACM0
稳定路径：/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_E0:72:A1:F9:B4:BC-if00
```

换接口、重插或接入其他板卡后，设备节点可能变化。先查看 `ls -l /dev/serial/by-id/`，需要辨认接口时运行 `udevadm info --query=property --name=/dev/ttyACM0`；CH343P 的节点名称由主机驱动决定。

在仓库根目录查看日志：

```bash
bash tools/idf.sh -p /dev/ttyACM0 monitor
```

烧录当前项目并进入监视器：

```bash
bash tools/idf.sh -p /dev/ttyACM0 flash monitor
```

监视波特率为 **115200**，按 **Ctrl + ]** 退出。手动进入下载模式时，按住 BOOT，点按 RST，再松开 BOOT。

当前配置以 UART0 为主控制台，同时启用 `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG`，因此原生 USB 可以查看辅助日志。辅助控制台用于输出；若以后需要从原生 USB 输入交互命令，应调整主控制台配置。见 [ESP-IDF 控制台配置说明](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/usb-serial-jtag-console.html)。

## 板载器件和固定连接

| 器件或功能 | 连接 | 使用要点 |
| --- | --- | --- |
| RGB LED | GPIO48，经 `RGB` 0 Ω 连接至 WS2812B 数据输入 | 使用 WS2812 时序驱动；普通 GPIO 高低电平不能直接设置颜色 |
| BOOT 按键 | GPIO0，按下接地 | 下载模式按键，同时属于启动配置引脚 |
| RST 按键 / 排针 RST | CHIP_PU / EN，按下接地 | 芯片复位信号，排针 RST 不是普通 GPIO |
| UART0 TX | GPIO43，排针标 TX | 已连接 CH343P 和 TX 指示灯 |
| UART0 RX | GPIO44，排针标 RX | 已连接 CH343P 和 RX 指示灯 |
| PWR 指示灯 | 3.3 V 电源轨 | 电源指示，不作为可编程状态灯 |
| 3.3 V 稳压 | V1.4 原理图标注 CJ6107A33GW | 由板上 5 V 电源轨供电 |

以上连接见 [V1.4 原理图](YD-ESP32-S3-SCH-V1.4.pdf)。

## 完整排针表

正面朝向与 USB 接口一节相同。左排为 **J1**，右排为 **J2**，各 22 针；每排从天线端向 USB 端编号 1～22。这里的编号是开发板排针编号，与模组焊盘号不同。

| 排针序号 | 左侧 J1 | 右侧 J2 |
| --- | --- | --- |
| 1 | 3V3 | GND |
| 2 | 3V3 | TX / GPIO43 |
| 3 | RST / EN | RX / GPIO44 |
| 4 | GPIO4 | GPIO1 |
| 5 | GPIO5 | GPIO2 |
| 6 | GPIO6 | GPIO42 |
| 7 | GPIO7 | GPIO41 |
| 8 | GPIO15 | GPIO40 |
| 9 | GPIO16 | GPIO39 |
| 10 | GPIO17 | GPIO38 |
| 11 | GPIO18 | GPIO37，N16R8 上保留给 PSRAM |
| 12 | GPIO8 | GPIO36，N16R8 上保留给 PSRAM |
| 13 | GPIO3 | GPIO35，N16R8 上保留给 PSRAM |
| 14 | GPIO46 | GPIO0 / BOOT |
| 15 | GPIO9 | GPIO45 |
| 16 | GPIO10 | GPIO48 / 板载 RGB |
| 17 | GPIO11 | GPIO47 |
| 18 | GPIO12 | GPIO21 |
| 19 | GPIO13 | GPIO20 / USB D+ |
| 20 | GPIO14 | GPIO19 / USB D− |
| 21 | 5Vin | GND |
| 22 | GND | GND |

排针连接依据 [V1.4 原理图中的 J1 / J2](YD-ESP32-S3-SCH-V1.4.pdf)，位置可对照 [毫米尺寸图](ESP32-S3-Metric.pdf)。

## GPIO 使用限制

| 引脚 | 本项目处理方式 | 原因 |
| --- | --- | --- |
| GPIO35、36、37 | 不分配给外设 | N16R8 的 Octal PSRAM 占用这些引脚，即使排针引出也不能另用 |
| GPIO19、20 | 保留给原生 USB | 改作其他 GPIO 会使对应 USB 功能失效 |
| GPIO43、44 | 保留给 UART0 日志和烧录 | 板上已连接 CH343P |
| GPIO48 | 默认保留给 RGB | 已连接板载 WS2812B |
| GPIO0、3、45、46 | 第一版不分配给外设 | 启动配置引脚，外接电路可能改变复位时的采样电平 |
| GPIO39～42 | 需要外部四线 JTAG 时保留 | 可复用为芯片的 JTAG 信号 |

存储器限制见 [乐鑫模组数据手册，第 3.2 节注释 b](https://www.espressif.com/sites/default/files/documentation/esp32-s3-wroom-1_wroom-1u_datasheet_en.pdf)；启动配置和 USB 引脚限制见 [ESP-IDF v6.1 GPIO 说明](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/peripherals/gpio.html)。

## 本项目的接线候选方案

以下是后续接线的候选分配，**尚未配置到固件，也未完成实物接线验证**。选取的三路舵机信号位于同一排相邻位置，便于布线。

| 功能 | 候选 GPIO | 排针位置 | 说明 |
| --- | --- | --- | --- |
| 雷达舵机信号 | GPIO15 | J1-8 | 后续由 LEDC 或 MCPWM 输出 |
| 左展翼舵机信号 | GPIO16 | J1-9 | 独立 PWM 通道 |
| 右展翼舵机信号 | GPIO17 | J1-10 | 独立 PWM 通道 |
| 电池电压采样 | GPIO4 | J1-4 | ADC1_CH3，经外部分压后接入 |
| 预留 I²C SDA / SCL | GPIO8 / GPIO9 | J1-12 / J1-15 | 传感器或扩展模块；拉升至 3.3 V |
| 预留扩展 UART TX / RX | GPIO18 / GPIO21 | J1-11 / J2-18 | 后续通过 GPIO 矩阵配置，与 UART0 控制台分开 |

ADC 通道和 GPIO 路由能力见 [ESP-IDF v6.1 GPIO 表](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/peripherals/gpio.html)。舵机频率、脉宽、方向和机械限位，以及电池分压电阻和 ADC 量程，在具体器件确定后再设定，参见 [项目需求](../requirements-v1.md)。

## 供电连接

V1.4 原理图由 USB / 5Vin 接入板上 5 V 电源轨，再经 LDO 产生 3.3 V；图中没有 12 V 降压或锂电池充电电路。

- 开发板按 5 V 供电；项目的 12 V 外部输入先经过独立降压模块。
- 舵机按实际型号使用独立电源支路，信号地与开发板 GND 共地；不通过开发板 3V3 引脚给舵机供电。
- GPIO 使用 3.3 V 逻辑。电池电压采样先分压，分压比和 ADC 量程按电池最高电压设计。
- `IN-OUT` 和 `USB-OTG` 的 0 Ω 连接会改变供电隔离或 VBUS 路径，同时接 USB 和外部电源前需核对实板焊接状态。`USB-JTAG` 连接关联 GPIO3 上拉，不是通用电源跳线。

电源支路、D1 / D2 / D3、LDO 和相关连接见 [V1.4 原理图](YD-ESP32-S3-SCH-V1.4.pdf)。

## 外形和安装尺寸

以下数值来自 [ESP32-S3-Metric.pdf](ESP32-S3-Metric.pdf) 第 1 页，适用于图示板型。

| 项目 | 尺寸 |
| --- | --- |
| 底板 PCB 长 × 宽 | 57.15 × 27.94 mm |
| 含图示 PCB 天线伸出的总长 | 63.39 mm |
| 两排排针中心距 | 25.40 mm |
| 同排相邻针中心距 | 2.54 mm，由 22 针首末间距计算 |
| 同排首针至末针中心距 | 53.34 mm |
| 首末针中心至对应 PCB 短边距离 | 图中均标注约 1.91 mm |

3D 打印外壳按实际所选板型复核，WROOM-1U 的天线端外形可能与图示 WROOM-1 不同。外壳还需为 Type-C 插头、BOOT / RST 操作、排针和天线馈线留出空间。

## 上板时待确认

1. 实板 PCB 版本、完整模组型号和外接天线接口，与项目需求一致。
2. 运行项目自检固件，启动日志实际输出 `Flash: 16 MB, PSRAM: 8 MB`；这是预期结果，当前端口识别不能证明容量。
3. 三个舵机的型号、供电、方向、脉宽和机械限位，以及最终信号线接法。
4. 电池分压电阻、充电稳压模块型号和可读取的电源状态信号。

## 原始硬件资料

| 文件 | 内容与适用范围 |
| --- | --- |
| [YD-ESP32-S3-SCH-V1.4.pdf](YD-ESP32-S3-SCH-V1.4.pdf) | V1.4 原理图，图纸日期 2022-09-23；查询排针连接、USB、RGB、按键和供电电路 |
| [ESP32-S3-Metric.pdf](ESP32-S3-Metric.pdf) | 毫米尺寸图；设计底座、排针座和外壳时优先使用 |
| [ESP32-S3-inch.pdf](ESP32-S3-inch.pdf) | 英寸尺寸图 |
| [ESP32-S3-0702.PNG](ESP32-S3-0702.PNG) | 中文硬件介绍，覆盖 N8R2、N8R8、N16R8 型号 |
| [S3-V1.2.PNG](S3-V1.2.PNG) | 英文硬件介绍，文件名标注 V1.2；与 V1.4 原理图区分使用 |
| [YD-ESP32-S3.PNG](YD-ESP32-S3.PNG) | 开发板正面照片，用于辨认元件和接口位置 |

接线采用 V1.4 原理图中的连接关系，并核对实板版本和丝印；介绍图片展示了不同容量的样板，容量以实板完整型号和运行自检为准。

厂商资料包中的 Windows 工具、下载缓存和日志、旧 MicroPython 固件及例程已移除。环境安装、编译和烧录命令见 [项目 README](../../README.md)，其他项目文档见 [资料索引](../README.md)。
