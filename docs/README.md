# 项目资料索引

| 文档 | 内容 |
| --- | --- |
| [组件调试快速上手](component_demo.md) | 配置接线、网页控制、状态诊断与可运行示例 |
| [舵机组件](servo.md) | 平滑运动、限速、序列、扫描、校准和状态 |
| [GPIO PWM 测试固件](gpio_pwm_test.md) | 普通 ESP32 / XIAO 的独立1kHz、50%方波与示波器验证 |
| [网页与无头预览](web_ui.md) | 控制台入口、交互规则、手机布局与无头浏览器截图 |
| [卫星 APP](satellite.md) | 串口/可选HTTP、天线与太阳翼寻优、电压功率与供电策略 |
| [模块使用说明](modules.md) | 模块选择、API、依赖和新增应用/板卡方式 |
| [风扇与串口继电器](peripherals.md) | PWM 调速/测速、固定指令和 Modbus RTU |
| [MPU6050 六轴传感器](mpu6050.md) | I²C 接线、量程、六轴采样与休眠 |
| [WS2812 RGB 灯带](ws2812.md) | 常亮、呼吸、流水、闪烁及后台动画 |
| [项目需求 V1.0](requirements-v1.md) | 北斗卫星桌面控制器的硬件范围、软件结构和开发路线 |

## 开发板资料

每块开发板使用独立目录，目录内的 `README.md` 作为说明和资料入口，原理图、尺寸图和图片放在同一目录。后续板卡在 `docs/` 下并列新增目录，并在下表登记。

| 目录 | 内容 |
| --- | --- |
| [XIAO_ESP32_S3_SENSE](XIAO_ESP32_S3_SENSE/README.md) | 摄像头、SD 卡、存储器配置与独立监控构建方式 |
| [YD_ESP32_S3](YD_ESP32_S3/README.md) | YD-ESP32-S3 型号、USB 接口、完整排针表、供电、尺寸、接线候选方案及 6 份原始硬件资料 |
| [ESP32_DEVKIT](ESP32_DEVKIT/README.md) | ESP32-WROOM-32 / CH340，30针排针表、PWM引脚分组和4份原始资料 |

## 开发工具

本项目统一使用 ESP-IDF，环境安装、编译和烧录命令见 [项目 README](../README.md)。
