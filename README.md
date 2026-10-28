# ESP32 可组合固件工程

基于 ESP-IDF v6.1 和 ESP32-S3，将共享功能、板卡配置和应用组合分别维护。当前提供北斗卫星基础工程、XIAO 摄像头监控及外设组件调试应用。

| 应用 | 默认板卡 | 默认功能 |
| --- | --- | --- |
| `satellite` | `YD_ESP32_S3`，16 MB Flash / 8 MB PSRAM | 串口控制、三路舵机、Wi-Fi RSSI、太阳能/电池采样、角度寻优及策略；硬件默认未配置 |
| `component_demo` | `YD_ESP32_S3`，可切换 XIAO | AP、网页及五种外设调试；接线通过 menuconfig 启用 |
| `camera_monitor` | `XIAO_ESP32_S3_SENSE`，8 MB Flash / 8 MB PSRAM | AP 网络、网页、摄像头、拍照存卡、分段录像、文件管理 |

模块选择、接口及扩展方式见 [模块使用说明](docs/modules.md)，卫星业务需求见 [需求文档](docs/requirements-v1.md)，开发板资料见 [资料索引](docs/README.md)。

## 目录

```text
apps/          # 各应用的入口、默认板卡和产品设置
boards/        # 各板卡的引脚、存储器默认值和分区表
components/    # 可复用功能组件及网页适配组件
cmake/         # 模块选择、依赖展开和板卡检查
config/        # 共享 ESP-IDF 默认值
build/         # 按应用、板卡及模块组合隔离的构建结果，Git 忽略
docs/          # 需求、模块文档和板卡资料
tools/         # SDK 安装、环境激活和应用构建入口
tests/         # 无需开发板的网页、摄像头、存储及外设回归测试
```

## 开发环境

统一使用 **ESP-IDF v6.1**。SDK 和工具链共用 `~/EPS`，安装或补齐环境：

```bash
bash tools/setup.sh
```

依赖 Git、Python 3.10+、CMake 3.22+ 和 Ninja。环境脚本支持 Bash / Zsh；可通过 `ESP_SDK_ROOT` 改变 SDK 位置。

## 编译和模块选择

在仓库根目录执行，默认构建卫星基础应用：

```bash
bash tools/idf.sh build
bash tools/idf.sh --app camera_monitor build
```

显式选择模块，例如只保留实时画面和拍照，不包含 SD 卡与录像：

```bash
bash tools/idf.sh --app camera_monitor --modules network,web,camera build
```

拍照存卡但不带录像：

```bash
bash tools/idf.sh --app camera_monitor --modules network,web,camera,storage build
```

卫星应用选择网页与舵机驱动：

```bash
bash tools/idf.sh --app satellite --modules network,web,servo build
```

`servo` 提供驱动 API，选择它不会自动产生 PWM；需要应用传入实物引脚与校准参数并调用接口。雷达扫描、展翼调度和电源采样业务尚未实现。

风扇调速/测速和串口继电器可分别选择，也可与舵机组合：

```bash
bash tools/idf.sh --app satellite --modules fan build
bash tools/idf.sh --app satellite --modules relay_uart build
bash tools/idf.sh --app satellite --modules fan,servo,relay_uart build
```

`fan` 提供 PWM 占空比控制与 RPM 反馈；`relay_uart` 提供固定指令帧和 Modbus RTU 两种配置。应用负责创建实例并传入实际接线和协议参数，使用说明见 [风扇与串口继电器](docs/peripherals.md)。

六轴传感器与 RGB 灯带可分别选择或组合使用：

```bash
bash tools/idf.sh --app satellite --modules mpu6050 build
bash tools/idf.sh --app satellite --modules ws2812 build
bash tools/idf.sh --app satellite --modules mpu6050,ws2812 build
```

`mpu6050` 读取三轴加速度、三轴角速度与温度；`ws2812` 提供常亮、呼吸、流水和闪烁，支持颜色、亮度、周期及反向流水。两者均由应用配置实际引脚并调用 API，说明见 [MPU6050](docs/mpu6050.md) 与 [WS2812](docs/ws2812.md)。

应用选择与板卡选择相互独立。例如可使用 XIAO 为卫星应用编入摄像头能力：

```bash
bash tools/idf.sh --app satellite --board XIAO_ESP32_S3_SENSE --modules network,web,camera,servo build
```

YD 默认板卡没有摄像头和 SD 接线配置，选择对应模块会在配置阶段报错；使用外接摄像头时，先增加匹配实际接线的板卡配置。

`--app`、`--board`、`--modules` 放在 IDF 参数之前；后续可使用 `size`、`menuconfig`、`flash`、`monitor` 等命令。省略 `--modules` 使用应用默认组合，`--modules ""` 仅构建基础服务。默认和自选组合分别使用独立目录，例如：

```text
build/satellite/YD_ESP32_S3/
build/camera_monitor/XIAO_ESP32_S3_SENSE/
build/camera_monitor/XIAO_ESP32_S3_SENSE-network_web_camera/
```

生成的 `sdkconfig`、依赖锁文件和固件位于相应构建目录；第三方源码缓存位于各应用的 `managed_components/`，均由 Git 忽略。直接依赖版本固定在组件的 `idf_component.yml` 中。

## 烧录和串口

先查询端口，再为同一应用及模块组合烧录或查看日志：

```bash
ls -l /dev/serial/by-id/
bash tools/idf.sh -p /dev/ttyACM0 flash monitor
bash tools/idf.sh --app camera_monitor -p /dev/ttyACM0 flash monitor
```

自选组合的烧录命令也需携带相同的 `--modules`。监视波特率为 115200，按 `Ctrl+]` 退出。默认 UART0 主控制台加原生 USB Serial/JTAG 辅助输出。手动进入下载模式：按住 BOOT，点按 RST，再松开 BOOT。

需要保留日志时，运行 `bash tools/serial_log.sh --port /dev/ttyACM0 --output logs/serial/satellite.log`，按时间戳追加保存并自动重连。查看历史及后台运行方式见 [串口日志说明](docs/satellite.md#日志保存与回溯)；烧录前先停止记录器。

## 卫星 APP

普通 ESP32 核心开发板可先用独立的 [GPIO PWM 测试固件](docs/gpio_pwm_test.md) 验证引脚：`bash tools/idf.sh --app gpio_pwm_test build`，默认13路输出1kHz、50%方波，目标为原版 ESP32。

卫星业务已接入，默认不选网页。通过 `menuconfig → Satellite hardware and policies` 配置实际接线，编译烧录后在原生 USB 主控制台输入 `sat help` 或 `sat status`。支持天线/太阳翼的手动与自动调向、分压电压采样、可选模拟电流采样、功率/Wh统计、低电量与姿态策略，以及灯光、风扇和继电器控制。

选择 `web` 后可从设备首页进入 `/satellite/` 图形工作台，查看角度仪表、电源遥测与扫描曲线。首页、卫星、外设调试和摄像头页面使用统一的深色控制台样式，支持手机布局，所有资源由设备本地提供。

XIAO ESP32S3 Sense 的卫星默认接线已将天线舵机设为 **D0 / GPIO1**。选择 `--board XIAO_ESP32_S3_SENSE --modules web,servo` 后，可在“通信天线”卡片输入角度并点击“设置角度”；上电及打开网页不会自动设置角度。已有 sdkconfig 需确认天线 GPIO 为 `1`，验证步骤见 [GPIO1 舵机网页验证](docs/satellite.md#xiao-gpio1-舵机网页验证)。

普通 ESP32-WROOM-32 / CH340 使用 `--board ESP32_DEVKIT --modules web,servo`，当前左太阳翼为 **D25 / GPIO25**，右太阳翼为 **D26 / GPIO26**，雷达/天线轴暂时禁用，D14不再输出PWM。UART0保留日志与串口控制；上电后等待角度指令。连接 `ESP32-Satellite` 热点后可使用同一个卫星网页，步骤见 [太阳翼网页控制](docs/satellite.md#普通-esp32-太阳翼网页控制)。

完整命令、硬件参数、可选HTTP接口和验证范围见 [卫星 APP 说明](docs/satellite.md)。未配置通道明确返回不可用；无电流采样时不提供功率寻优。默认软件参数需按实际器件校准，本轮功能尚未上板验证。

## 外设开箱使用

使用 `component_demo`，无需修改源码即可配置接线并用网页测试舵机、风扇、串口继电器、MPU6050 和 WS2812：

```bash
bash tools/idf.sh --app component_demo --board XIAO_ESP32_S3_SENSE menuconfig
bash tools/idf.sh --app component_demo --board XIAO_ESP32_S3_SENSE build
bash tools/idf.sh --app component_demo --board XIAO_ESP32_S3_SENSE -p /dev/ttyACM0 flash monitor
```

在 `Component demo wiring` 中启用实际连接的外设并填写参数。连接 AP `ESP32-Components`（密码 `esp32setup`），访问 `http://192.168.4.1/components/`。默认不启用外设输出；GPIO 候选值需要根据实际板卡确认。详细步骤和模块组合见 [组件调试快速上手](docs/component_demo.md)，平滑移动、动作序列和校准见 [舵机说明](docs/servo.md)。

## 监控应用

默认启动 AP：SSID `CMCC-ESP`，密码 `66668888`；可通过 `menuconfig` 修改网络配置。连接后打开 `http://192.168.4.1/`，点击“摄像头监控”进入 `/camera/`。

摄像头默认关闭。开启后提供 MJPEG 实时画面和拍照；选中存储模块时支持照片保存和文件管理，选中录像模块时支持分段 MJPEG AVI 录像。当前默认 SVGA、单路预览、录像约 5 FPS。摄像头关闭请求在后台完成录像收尾与视频流退出，再释放摄像头驱动和帧缓冲；网络与网页保留，便于再次开启。

XIAO 配置沿用原工程的 OV3660 和 SDMMC 1-bit 接线，SD 引脚 CLK=7、CMD=9、D0=8。网络模块支持 AP，以及通过 menuconfig 配置的可选 STA 和断线重连；运行时配网尚未实现。

## 验证

模块拆分及后续修复执行 ESP-IDF 编译和模块组合检查；无板回归测试：

```bash
node --test tests/*.test.cjs
python3 -m unittest discover -s tests -p 'test_native_*.py' -v
```

网页测试覆盖无 SD/无录像的页面、异步摄像头切换、切换失败恢复和无卡拍照预览。本机 C 回归测试需要 `cc` 和 POSIX 线程支持，编译实际摄像头、照片文件与外设驱动代码，覆盖双缓冲并发、关闭/分辨率切换时的使用者排空、卡满、I/O 错误、名称冲突、编号溢出、风扇 RPM、PWM、资源冲突、继电器指令/应答、串口异常、MPU6050 量程与 I²C 错误，以及 WS2812 灯效和后台任务退出。

2026-10-26 模块拆分基线已在 XIAO 实板验证启动、PSRAM 自检、SD 卡读写检查、OV3660 预览、录像、拍照、摄像头开关与文件删除。后续并发与存储修复已烧录并通过启动、PSRAM 和 SD 卡检查；并发边界、卡满及 I/O 错误通过本机回归与错误注入验证，未在实物 SD 卡上填满容量或制造故障。舵机动作、供电边界和长时间压力测试仍需按实际应用验证。
