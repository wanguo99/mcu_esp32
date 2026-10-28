# GPIO PWM 测试固件

舵机循环模式支持配置第一路和第二路 GPIO（默认仍为 GPIO13、GPIO14），以及循环次数（默认 `0`，持续循环）。设置非零次数后，完成整轮角度序列才计数，最后一档保持结束后关闭所有测试通道的 PWM，并将信号拉低。D25/D26 双 MG90S 对照测试可设置：`GPIO_PWM_TEST_SERVO_GPIO=25`、`GPIO_PWM_TEST_SECOND_SERVO_GPIO=26`、启用第二路及 45°/135°序列、每档保持 2000ms、`GPIO_PWM_TEST_SERVO_CYCLES=2`。这份独立固件使用 LEDC，不经过卫星应用和舵机组件；测试时网页离线，结束后需重新烧录卫星固件。

`gpio_pwm_test` 是独立的 IO 测试应用，默认在所选 GPIO 输出同相位的 **1kHz、50% 占空比**硬件 PWM。默认方波模式用于示波器验证 IO，测试前断开被测引脚上的舵机和其他外设信号线。另有 GPIO13 慢速电平模式和单/双路舵机循环模式，可在 `GPIO PWM test` 菜单选择。此应用不启动网页、Wi-Fi、摄像头或 SD 卡。

## 普通 ESP32 核心开发板

默认板卡为 `ESP32_DEVKIT`，目标芯片是原版 **ESP32**，默认按4MB Flash、DIO、40MHz配置。GPIO号对应芯片编号，以开发板丝印的 IO/GPIO 标识为准。

上传资料的30针排针图、丝印对照及原始PDF见 [ESP32-WROOM-32 / CH340开发板资料](ESP32_DEVKIT/README.md)。当前13路均在该图中引出；GPIO1/3为TX0/RX0，保留给CH340串口。

默认同时输出的13个引脚：

```text
GPIO4、13、14、18、19、21、22、23、25、26、27、32、33
```

- GPIO1/3 保留给 UART0 日志，波特率115200。
- GPIO6–11 保留给 Flash；GPIO34–39 仅支持输入。
- GPIO0/2/5/12/15 为启动绑带引脚，默认不参与测试。
- GPIO16/17 在 WROVER 等模块上用于 PSRAM，默认不参与测试；确认无 PSRAM 且引脚空闲后，可在 `menuconfig → GPIO PWM test` 中启用。

构建与烧录，端口按实际 USB 转串口设备调整：

```bash
bash tools/idf.sh --app gpio_pwm_test build
bash tools/idf.sh --app gpio_pwm_test -p /dev/ttyUSB0 flash
```

合并烧录文件可通过以下命令生成，完整镜像的烧录起始地址为 **0x0**：

```bash
bash tools/idf.sh --app gpio_pwm_test merge-bin
```

`build/gpio_pwm_test/ESP32_DEVKIT/gpio_pwm_test.bin` 只是应用文件，需要写入 **0x10000** 并配套 bootloader 和分区表；使用 `flash` 或合并镜像可避免遗漏。

## GPIO13 每秒切换电平，确认接线

普通 ESP32 版本可临时切换为慢速电平测试，方便用万用表直流电压档识别 D13：

```bash
bash tools/idf.sh --app gpio_pwm_test menuconfig
```

进入 `GPIO PWM test → Test mode`，选择 `GPIO13 LOW/HIGH every second (temporary wiring test, no PWM)`，保存后重新构建并烧录。此模式默认不启用；开启时仅控制 GPIO13，不启动 PWM、Wi-Fi 或网页。

先断开舵机信号线，红表笔接开发板丝印 D13（GPIO13），黑表笔接同一开发板的 GND。直流电压应循环保持约 0V 一秒、约 3.3V 一秒。串口每秒记录 `GPIO13 LOW, pad=0` 或 `GPIO13 HIGH, pad=1`；内部读回仅确认数字电平，实际电压以表笔测量为准。

选择 `Multiple GPIOs with 50% square wave` 并重新构建、烧录可恢复本应用的多路 1kHz PWM 测试。恢复网页舵机控制则须重新烧录 `satellite` 应用。

## GPIO13 舵机角度循环测试

普通 ESP32 版本在 `GPIO PWM test → Test mode` 中选择 `GPIO13 SG90 servo angle sweep`，然后重新构建、烧录：

```bash
bash tools/idf.sh --app gpio_pwm_test build
bash tools/idf.sh --app gpio_pwm_test -p /dev/ttyUSB0 flash
```

本模式只在 D13（GPIO13）输出 50Hz、14位分辨率的硬件 PWM。每个目标角度保持1秒，180°之后直接返回0°，每5秒重复一次：

| 目标角度 | 高电平脉宽 | 占空比 |
| --- | --- | --- |
| 0° | 500µs | 2.5% |
| 45° | 1000µs | 5% |
| 90° | 1500µs | 7.5% |
| 135° | 2000µs | 10% |
| 180° | 2500µs | 12.5% |

上表为默认五档模式。也可在同一菜单中启用 `Alternate only 45 and 135 degrees`，只在45°与135°之间切换；`Hold time per target angle (ms)`设置每档保持时间，默认1000ms。

当前双路对照测试启用两档模式，并将保持时间设为 **2000ms**：D13、D14同步输出45°（1000µs、5%）保持2秒，再输出135°（2000µs、10%）保持2秒，每4秒循环一次。使用下面的双路选项启用D14。该测试使用独立固件，不启动Wi-Fi和网页。

脉宽经硬件计数量化，会有约1µs以内的偏差；实际机械角度取决于舵机的校准。SG90信号线接D13，使用5V电源，电源负极与开发板GND共地。此模式不启动Wi-Fi或网页。

串口输出目标角度、脉宽、硬件频率及占空比读回，并在每次切换后采样60ms记录引脚高低电平数量和上升沿数，便于确认PWM持续输出。可用以下命令保存日志：

```bash
bash tools/serial_log.sh --port /dev/ttyUSB0 --output logs/serial/esp32-gpio13-servo-sweep.log
```

### 同时测试 SG90 和 MG90S

保持上述舵机循环模式，在同一菜单中启用 `Also test MG90S on GPIO14 with a separate PWM channel`，重新构建并烧录。该选项默认关闭。启用后同时输出两路：

| 舵机 | 开发板丝印 | GPIO | LEDC 通道 | 定时器 |
| --- | --- | --- | --- | --- |
| SG90 180° | D13 | GPIO13 | 1 | 1 |
| MG90S 180° | D14 | GPIO14 | 2 | 1 |

两个独立通道共享50Hz时基，同步循环所选角度，并使用相同的保持时间；两路信号均持续输出，只在切换角度时更新脉宽。D14和D13之间的D12不用于本测试。两只舵机信号线分别接对应IO，红线接稳定5V电源，棕/黑线接电源负极，电源负极还须接ESP32的GND。不要将舵机电源接3V3。

串口分别记录GPIO13、GPIO14的频率、占空比和引脚采样。双路日志建议保存为：

```bash
bash tools/serial_log.sh --port /dev/ttyUSB0 --output logs/serial/esp32-dual-servo.log
```

### 与上传舵机资料的对照

- [SG90说明](sg90_180/使用说明.txt)与[MG90S说明](mg90s/使用说明.txt)均给出上表的0.5–2.5ms脉宽映射。两目录内的`舵机控制例程.rar`经SHA256校验完全相同，都是`dba58e898359fe601d95ba975d1fc2de915fa1cac6808d85820518e949be4748`。
- 压缩包内STM32 `PWM.c`将72MHz时钟分频到1MHz，再计数20000次形成20ms周期；`servo.c`用`Angle/180*2000+500`换算高电平微秒数，与本应用一致。另一个STM32示例每秒切换0°/90°/180°，与本应用的保持方式相同。
- Arduino示例持续输出`(angle*11)+500`微秒高电平，再输出`20000-pulsewidth`微秒低电平。它使用软件延时，本应用由LEDC硬件持续输出。每秒切换角度不代表每秒只发送一个脉冲。
- 厂家STM32示例注释把`20ms × 2.5%`写成`0.05ms`，正确值是`0.5ms`；实际例程没有使用这个错误值。Word资料中的角度数值也标为参考值，实际机械角度仍须校准。
- [MG90S封面](mg90s/封面图.jpg)标注180°、工作电压4.8V、死区5µs。[接线图](mg90s/舵机引脚图.png)标明红线为+5V、棕线为GND、黄线为PWM。资料未给出PWM输入的高电平门限，不能仅凭5V电源标注就断言信号也必须是5V。
- 50Hz、14位计数每档约1.221µs，当前五个目标的量化误差小于0.5µs，未发现会导致完全不转的脉宽计算错误。内部数字采样可确认引脚在切换，不能证明外接舵机端的电压幅值、供电或实际机械位置。

## 测量与日志

1. 探头接所选 GPIO，地夹接同一开发板的 GND，示波器使用 DC 耦合、1MΩ输入。可从0.2ms/div、1V/div开始调整，并以约1.5V上升沿触发。
2. 每路应看到约0–3.3V方波，周期1ms，高电平和低电平均为0.5ms。50%方波的平均电压约1.65V，单看平均电压不能判断有没有 PWM。
3. 固件每10秒打印一次硬件频率/占空比读回和各引脚的高低电平样本数。内部数字采样不能测量模拟电压幅值，幅值仍以示波器为准。
4. 修改频率可进入 `menuconfig → GPIO PWM test`；占空比保持50%。

烧录结束后可保存串口日志：

```bash
bash tools/serial_log.sh --port /dev/ttyUSB0 --output logs/serial/esp32-pwm.log
```

烧录前先停止占用该端口的日志记录器或串口工具。另一端口上的记录器可以继续运行。

## XIAO ESP32S3 Sense 版本

同一测试应用也提供 XIAO 映射，但须明确选择板卡：

```bash
bash tools/idf.sh --app gpio_pwm_test --board XIAO_ESP32_S3_SENSE build
```

该版本只输出 D0–D7，即 GPIO1、2、3、4、5、6、43、44，日志使用原生 USB Serial/JTAG。板载 SD、摄像头和麦克风连接不参与测试。GPIO3 在启动完成后才输出方波。恢复卫星应用时，使用原先相同的板卡、模块参数重新烧录 `satellite`。
