# 风扇与串口继电器

无需修改源码即可试用：见 [组件调试应用](component_demo.md)，通过 menuconfig 配置接线，再使用网页控制和查看状态。

两个模块均为可复用驱动，可以单独编入，也可和网页、摄像头、存储及舵机组合。默认应用组合不变；应用选择模块后，需要在自身组件依赖中加入相应模块，并创建实例。仅编入模块不会自动启动风扇或操作继电器，当前未增加网页控制页面。

```bash
bash tools/idf.sh --app satellite --modules fan,relay_uart build
bash tools/idf.sh --app camera_monitor --modules network,web,camera,storage,recording,fan,relay_uart,servo build
```

应用组件按构建选择引用驱动，例如在 `idf_component_register` 后使用 `idf_component_optional_requires(PRIVATE fan relay_uart)`，源码通过 `CONFIG_MCU_ENABLE_FAN` / `CONFIG_MCU_ENABLE_RELAY_UART` 包含对应头文件和调用。网页系统状态的 `modules` 会显示已编入的两个模块。

## 风扇

[fan.h](../components/fan/include/fan.h) 提供 PWM 占空比设置、停止 PWM 和周期 RPM 反馈。每个实例占一个低速 LEDC 通道/定时器；配置测速时额外占一个 PCNT 单元和一个采样定时器。创建时为逻辑 0% 占空比。

```c
#include "fan.h"

// APP_* 参数由应用根据实物接线定义，均为 ESP32 GPIO 编号，不是丝印 D 编号。
fan_config_t config = {
    .pwm_gpio = APP_FAN_PWM_GPIO,
    .tach_gpio = APP_FAN_TACH_GPIO,
    .timer = 1, .channel = 1,
    .pwm_frequency_hz = 25000,
    .sample_period_ms = 1000,
    .pulses_per_revolution = 2,
    .pwm_inverted = false,
};
fan_handle_t fan;
ESP_ERROR_CHECK(fan_create(&config, &fan));
ESP_ERROR_CHECK(fan_set_duty(fan, 60));

// 首个采样周期后读取，后续可定期读取；不要使用延时阻塞 HTTP 处理函数。
fan_status_t status;
ESP_ERROR_CHECK(fan_get_status(fan, &status));
if (status.rpm_valid) {
    // status.rpm 是实测值；status.duty_percent 是输出占空比。
}
// 业务结束后：fan_stop(fan); fan_destroy(fan);
```

- 占空比范围 0～100，默认 PWM 频率 25 kHz，默认采样周期 1000 ms，可配置 100～5000 ms。频率需符合风扇/驱动器要求；硬件不支持的频率与分辨率组合会创建失败。
- 测速按下降沿计数，`RPM = 脉冲数 × 60 / (实际采样秒数 × 每转脉冲数)`。每转脉冲数必须来自风扇资料，不能假定所有风扇都是 2。设置 `tach_gpio=-1` 可只调速，此时 `tach_available=false`、`rpm_valid=false`。
- 未完成首次采样或样本超过三倍采样周期时 `rpm_valid=false`；`sample_age_ms` 表示样本年龄。0 RPM 表示没有收到脉冲，无法区分停转、堵转和测速线断开。低转速测量可增加采样周期。
- 调速是开环占空比控制，不是恒定目标 RPM。`fan_stop` 设置 0% 后继续测量惯性旋转；某些风扇在 0% 时仍保持最低转速，不能以此代替断电。
- 四线风扇可使用独立 PWM 输入与测速输出；三线风扇需按资料通过外部驱动器调节供电，并确认供电 PWM 下的测速有效性；两线风扇没有直接测速反馈。GPIO 不能给风扇供电。
- 测速输入电平必须适合 ESP32 的 3.3 V GPIO，开集电极测速通常需要外接到 3.3 V 的上拉。PWM 线是否需要开漏/开集电极驱动取决于风扇规格；外部反相驱动可设置 `pwm_inverted=true`。电源和接口由实际型号决定。

## 串口继电器

[relay_uart.h](../components/relay_uart/include/relay_uart.h) 支持多实例、多通道；每个实例独占一个 UART。支持 8 数据位、无/奇/偶校验、1 或 2 停止位，波特率 1200～1000000，默认应答超时 500 ms（上限 10000 ms）。保留日志使用的 UART；当前默认 UART0 输出日志，继电器通常选择 UART1 或 UART2。

`tx_gpio` / `rx_gpio` 明确接线，`rts_gpio=-1` 使用 TTL/全双工；配置有效 RTS GPIO 时使用 RS485 半双工控制外部收发器的 DE/~RE。TTL UART、RS232 和 RS485 的电气接口不同；RS232/RS485 需要匹配的外部收发器，不能把它们的总线直接接到 GPIO。

### Modbus RTU

支持功能码 05 写单线圈、01 读单线圈，校验地址、功能码、写回显和 CRC16；异常帧、错误应答和超时均返回错误，不自动重发。部分厂商使用寄存器功能码 06/10，不能当作本实现的线圈设备使用，需按实物协议扩展。

```c
#include "relay_uart.h"

relay_uart_config_t config = {
    .uart_port = 1,
    .tx_gpio = APP_RELAY_TX_GPIO, .rx_gpio = APP_RELAY_RX_GPIO,
    .rts_gpio = -1, // RS485 时改为实际 DE/~RE GPIO。
    .baud_rate = 9600, .parity = UART_PARITY_EVEN,
    .stop_bits = UART_STOP_BITS_1, .response_timeout_ms = 500,
    .protocol = RELAY_UART_MODBUS_RTU,
    .address = 1, .channel_count = 4, .first_coil = 0,
};
relay_uart_handle_t relay;
ESP_ERROR_CHECK(relay_uart_create(&config, &relay));
ESP_ERROR_CHECK(relay_uart_set(relay, 0, true)); // 第一路开。
bool on;
ESP_ERROR_CHECK(relay_uart_read(relay, 0, &on));
ESP_ERROR_CHECK(relay_uart_set(relay, 0, false));
// 业务结束后：relay_uart_destroy(relay);
```

通道从 0 开始，最多 32 路。设备地址范围 1～247，不支持广播。`first_coil` 是报文中的零基线圈地址，例如设备资料中的“00001”可能对应报文地址 0，必须以协议说明为准。参数示例并不代表所有模块都使用 9600、偶校验或相同地址。

### 固定十六进制指令

每路提供开、关两条指令和可选的精确应答，每帧最多 64 字节；创建时复制帧内容。帧包括设备要求的地址和校验，驱动不会为未知厂商自动拼装校验。以下字节仅演示配置格式，需替换成实物协议：

```c
relay_uart_channel_frames_t frames[] = {
    {.on = {.data = {0xA0, 0x01, 0x01, 0xA2}, .length = 4},
     .off = {.data = {0xA0, 0x01, 0x00, 0xA1}, .length = 4}},
};
relay_uart_config_t config = {
    .uart_port = 1, .tx_gpio = APP_RELAY_TX_GPIO,
    .rx_gpio = -1, .rts_gpio = -1,
    .baud_rate = 9600, .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
    .protocol = RELAY_UART_FIXED_FRAMES, .channel_count = 1, .frames = frames,
};
```

`ack_on` / `ack_off` 长度为 0 表示只发不等应答。此时 `relay_uart_set` 成功仅表示指令已发送，`relay_uart_get_command_status` 返回 `command_sent=true`、`acknowledged=false`。配置应答后需提供 RX GPIO，只有收到匹配应答才标记确认。固定帧模式的 `relay_uart_read` 返回 `ESP_ERR_NOT_SUPPORTED`，不会用上次命令冒充读取结果。

所有应答均只代表设备协议层确认，Modbus 线圈读回也不是机械触点反馈。没有辅助触点输入时，无法验证机械继电器是否实际动作。创建和销毁实例均不发送开关命令；应用需要在业务结束或整机重启前显式设置期望状态并等待完成。销毁前需停止并发调用者。

## 资源与验证

内部的 `peripheral_resources` 自动管理共享 GPIO 与 LEDC 资源。风扇使用 10 位 PWM、舵机使用 14 位 PWM，因此必须选择不同定时器和通道；启用摄像头时避开其 LEDC 定时器 0、通道 0。测速、UART TX/RX/RTS 也参加 GPIO 互斥检查。静态保留的是构建时选中的摄像头和 SD 引脚，即使运行时摄像头关闭，也不将它们借给其他外设。

宿主机测试编译实际驱动并替代底层硬件，覆盖 PWM 边界、RPM/滑行/过期样本、创建失败清理、跨模块资源冲突、Modbus CRC/读写/异常/超时、固定指令和 UART 独占。不能代替实际风扇的转速、驱动电路和继电器型号验证。

驱动接口依据本地 ESP-IDF v6.1；外设背景见 Espressif 的 [PCNT 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/pcnt.html) 和 [UART 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/peripherals/uart.html)。
