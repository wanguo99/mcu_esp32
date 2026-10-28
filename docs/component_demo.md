# 组件调试应用

`component_demo` 把现有外设组件接到统一网页，适合确认接线、调试参数，也可作为新项目的应用层示例。驱动仍独立于网页；选中模块不等于启用外设，接线在 menuconfig 中配置，不需要修改 C 源码。

## 第一次使用

在仓库根目录执行。下面使用 XIAO ESP32-S3 Sense；使用 YD 板时，将三个命令的板卡名都改为 `YD_ESP32_S3`。

```bash
bash tools/idf.sh --app component_demo --board XIAO_ESP32_S3_SENSE menuconfig
bash tools/idf.sh --app component_demo --board XIAO_ESP32_S3_SENSE build
bash tools/idf.sh --app component_demo --board XIAO_ESP32_S3_SENSE -p /dev/ttyACM0 flash monitor
```

1. 在 `Component demo wiring` 中只勾选已经连接的外设，并核对 GPIO、协议和器件参数。全部默认不启用，首次也可以仅测试网页。
2. 编译烧录，连接 AP **ESP32-Components**，密码 **esp32setup**。
3. 打开 `http://192.168.4.1/`，点击“组件调试”，或直接打开 `/components/`。
4. 页面区分“未配置”“初始化失败”“已就绪”。一个外设初始化失败不阻止其他外设和网页启动。状态每秒刷新，控制需要手动点击。

所有 GPIO 都是芯片 GPIO 编号，不是排针序号或 XIAO 的 D 编号。配置中的数字只是候选值，不代表已经完成接线确认。XIAO 与 YD 的引出脚不同，应以各自 [开发板资料](README.md) 为准。先完成断电接线和供电检查，再启用对应配置。

## 配置与操作

| 外设 | menuconfig 参数 | 网页操作及状态 |
| --- | --- | --- |
| 舵机 | PWM GPIO、最小/最大脉宽、最大角度 | 设置起始角度、平滑/相对移动、记录初始位置、复位、最大角度限位、往返扫描、保持当前指令、停止 PWM；显示指令角度和后台错误 |
| 风扇 | PWM、测速 GPIO、每圈脉冲数、PWM 频率、反相 | 占空比控制、0% PWM；显示 RPM、样本年龄和是否有效 |
| 串口继电器 | UART、TX/RX、RTS、波特率、校验、停止位及协议参数 | 通道开关；Modbus 线圈读回；区分发送成功与收到应答 |
| MPU6050 | I²C 控制器、SDA/SCL、AD0 地址、内部上拉 | 六轴数据、芯片温度、休眠/唤醒 |
| WS2812 | 数据 GPIO、灯珠数、GRB/RGB 顺序 | 颜色、亮度、常亮/呼吸/流水/闪烁、周期、拖尾、反向、熄灯 |

- 舵机使用 LEDC timer 1 / channel 1，风扇使用 timer 2 / channel 2，避开摄像头使用的 timer 0 / channel 0。舵机通电初始角度未知，必须先设置起始角度，再提交平滑运动；首次设置本身是立即输出，不能保证机械缓慢归位。校准方法见 [舵机组件](servo.md)。
- 风扇无测速线时将 Tach GPIO 设为 `-1`。0% PWM 不等于切断供电，也不保证所有风扇型号停转；详见 [风扇与继电器](peripherals.md)。
- Modbus 模式必须匹配设备从站地址、线圈起始地址和串口参数。固定帧模式目前在调试应用中提供一路开/关及可选应答，格式为 `A0 01 ...` 这样的空格分隔双位十六进制字节，必须填写设备实际协议。驱动 API 本身支持多通道固定帧。无应答设备不显示为“已确认状态”，读回线圈也不是机械触点反馈。
- MPU6050 默认 I²C0、地址 0x68；AD0 高时为 0x69。使用外部上拉，确认总线电平；详见 [传感器说明](mpu6050.md)。休眠后页面停止展示旧采样。
- WS2812 不会在初始化时自动播放，点击应用灯效才开始；详见 [灯带供电和电平说明](ws2812.md)。

执行器使用匹配的外部供电/驱动电路并与 MCU 共地，GPIO 不直接为舵机、风扇、继电器线圈或灯带供电。

## 选择模块

只测试风扇和灯带：

```bash
bash tools/idf.sh --app component_demo --board XIAO_ESP32_S3_SENSE --modules web,fan,ws2812 menuconfig
bash tools/idf.sh --app component_demo --board XIAO_ESP32_S3_SENSE --modules web,fan,ws2812 build
```

`web` 自动带入 `network`。未选模块不显示；选中但未配置接线的模块显示禁用提示。编译、menuconfig、烧录必须使用相同的 app/board/modules 参数，每个组合有独立 sdkconfig。

合并摄像头可选 `--modules web,camera,storage,recording,servo,fan,relay_uart,mpu6050,ws2812` 并使用 XIAO 板卡配置；摄像头页面保留在 `/camera/`。这仅表示软件组合受支持，不保证一块板上引出的 GPIO 足以同时连接全部外设。摄像头 SCCB 默认占 I²C1，SD 占 GPIO7/9/8，调试应用的候选引脚必须相应调整。资源冲突会拒绝初始化并输出串口日志；关闭摄像头不释放静态预留资源。

无网页组合如 `--modules servo,mpu6050` 也可编译，但交互控制需自行在应用入口调用驱动 API；网页调试请选择 `web`。

## HTTP 与重启

| 方法 | 地址 | 用途 |
| --- | --- | --- |
| GET | `/api/components/status` | 已编入模块、配置/初始化状态、实时数据 |
| POST | `/api/components/command?module=...&action=...` | 外设控制 |

例如舵机先 `module=servo&action=set&angle=90`，再 `module=servo&action=move&angle=120&duration_ms=1000`。停止 PWM 为 `module=servo&action=stop`，不依赖角度参数。风扇调速为 `module=fan&action=set&duty=40`。参数格式或范围错误返回 400，设备未就绪/驱动错误返回 409，准备重启时返回 503；错误体含具体错误代码。

应用通过 `runtime_register_shutdown_hook` 注册收尾：网页重启先停止舵机 PWM、设风扇 0% PWM、熄灯、让 MPU6050 休眠，再进行摄像头/录像收尾。继电器保持原状态。收尾失败取消本次重启，网页恢复可操作，用户可以查看错误并重试。

这里提供的是应用示例，不会把网页依赖加入外设驱动。新产品可以参考 `apps/component_demo/main/console.c` 的配置、初始化与收尾，只复用需要的组件。

## 验证范围

本机回归覆盖实际驱动的后台动作、错误恢复、资源冲突、输入解析，以及网页状态与命令交互；ESP-IDF 构建验证默认、已启用外设、固定帧、精简和摄像头组合。模拟测试与编译不能替代实际接线验证，外设的电平、供电、转速、运动范围及长时间运行仍需上板确认。

### 舵机新增操作

`servo` 的相对转动、记录、复位和限位接口可独立使用，网页仅为调试适配层。

| action | 参数 | 含义 |
| --- | --- | --- |
| `relative` | `delta=-20`、`duration_ms=1000` | 从最近输出的指令角度继续转动 −20° |
| `record_home` | 无 | 空闲时记录当前指令角度为初始位置 |
| `home` | `duration_ms=1000` | 平滑返回已记录初始位置 |
| `limits` | `max=150` | 设置运行范围 0–150°，不改变脉宽映射 |

以上请求都带 `module=servo`。网页限位及初始位置只在本次运行有效，重启后恢复创建时范围且初始位置未记录。menuconfig 的最大校准角度随固件保存。停止 PWM 后需重新建立起始指令才能相对转动或复位；修改限位时运动须已结束，当前指令及已记录初始位置须位于新范围内。
