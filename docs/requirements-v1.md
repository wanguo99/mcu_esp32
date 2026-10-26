# 北斗卫星桌面控制器需求 V1.0

本文件整理用户提供的项目设计方案，作为开发依据。当前任务首先搭建共享开发环境和可编译基础工程；下列业务功能为后续实现范围。

## 硬件与范围

- ESP32-S3-N16R8：16MB Flash、8MB PSRAM；2.4GHz 外接天线。
- 3D 打印北斗卫星外壳；雷达盘固定约 45° 倾斜，由一个位置舵机水平旋转。
- 左右展翼各一个舵机，每侧两块 60×60mm 太阳能板安装在刚性支架上，绕根部转轴旋转，不折叠。
- 两节并联 18650 电池，标称 3.7V / 6000mAh；Type-C 充电稳压模块；12V 外部输入与 DC-DC；ESP32 与舵机分支供电。
- 第一阶段不包含摄像头、四路串口继电器和光源追踪。
- 后续保留 UART、I²C、GPIO 功能底座扩展能力。

## 软件结构

ESP-IDF + FreeRTOS。Web UI / REST / WebSocket → 业务控制层（状态机、指令队列、限位、调度）→ radar / solar / power → ESP-IDF 外设驱动。

所有硬件动作通过统一业务接口提交，Web 回调不直接操作 GPIO，不执行长时间扫描循环。通用舵机驱动为 `servo` 组件；雷达和展翼分别承担业务逻辑。

建议逐步建立以下目录，不用空实现伪装已完成模块：

```text
main/app_main.c
components/
  board/       # 引脚、板级配置
  servo/       # 通用 PWM 驱动和校准
  radar/       # 雷达平滑旋转、扫描、限位
  solar/       # 左右展翼控制
  power/       # 采样和低电量策略
  network/     # STA/AP、重连、配网、mDNS
  web/         # HTTP、WebSocket
  settings/    # NVS 配置
  controller/  # 指令、状态机、动作调度
webui/
  index.html
  app.js
  style.css
```

## 网络管理

- NVS 保存 SSID、密码、设备名称。
- 有已保存配置时连接 STA；无配置或连接持续失败时进入带独立密码的 AP 配网。
- AP 页面填写 Wi-Fi 信息，保存后连接。
- 自动重连采用退避策略；支持 mDNS，如 `beidou.local`。
- 网络故障不影响本地舵机调度与电源管理。

## 舵机业务

雷达支持指定角度、平滑移动、范围往返扫描、停止并保持当前目标。软件限位必须结合机械结构和天线馈线校准。没有反馈传感器时，界面只能报告目标值或软件轨迹值，不能宣称实际到位。

```c
esp_err_t radar_init(void);
esp_err_t radar_set_angle(float angle);
esp_err_t radar_start_scan(float min_angle, float max_angle, float speed);
esp_err_t radar_stop_scan(void);
float radar_get_target_angle(void);
```

左右展翼支持独立手动、同步调节、缓慢演示和停止。±60° 仅是交互示例，正式限位需校准支架、线束和舵机方向。

```c
esp_err_t solar_init(void);
esp_err_t solar_set_left_angle(float angle);
esp_err_t solar_set_right_angle(float angle);
esp_err_t solar_set_sync_angle(float angle);
esp_err_t solar_stop(void);
```

底层可用 LEDC 或 MCPWM；频率和脉宽范围按实际舵机规格与校准决定，约 50Hz 仅是普通舵机的起点。当前还需确认三路 GPIO、舵机型号、方向、脉宽及安全机械范围。

## 电源监测

计划通过 ADC 电阻分压监测电池电压，第一版按电压粗估电量。外部 12V 状态、舵机电源和太阳能充电状态取决于额外采样电路及充电模块是否提供可读状态信号。没有对应电路时显示未配置或未知，不生成模拟读数。

低电量时停止自动扫描、减少动作，并在 Web 控制台提示。还需确认 ADC 引脚、分压电阻值、校准参数、低电压阈值和充电模块型号。

## 任务调度

| 功能 | 周期或触发方式 | 职责 |
| --- | --- | --- |
| Control Task | 约 20ms | 舵机平滑轨迹、状态机和动作调度 |
| Power Task | 约 1s | 电压采样和低电量策略 |
| Network | 事件驱动 | Wi-Fi 连接、重连、配网 |
| Web Server | HTTP / WebSocket 事件 | 接收命令、状态推送 |
| Storage | 配置变更时 | NVS 持久化 |

不必为每个组件创建独立任务；网络回调只提交命令。

## 已明确的 REST API

| 方法 | 地址 | 用途 |
| --- | --- | --- |
| GET | `/api/status` | 系统状态 |
| GET | `/api/power` | 电源状态 |
| PUT | `/api/radar/angle` | 设置雷达目标角度 |
| POST | `/api/radar/scan` | 开始自动扫描 |
| POST | `/api/radar/stop` | 停止扫描 |

请求字段、响应格式、展翼及配网接口在相应功能实现时确定。远程管理先在局域网完成；后续增加 OTA，预留 MQTT 或 App 接入。

## 实现路线

1. 搭建 `~/EPS` 共享 ESP-IDF 环境；编译 NVS / 内存自检固件。
2. 接上开发板，验证烧录、串口、16MB Flash 与 8MB PSRAM。
3. 确认引脚和舵机参数，完成单路校准，再接入三路统一调度。
4. 增加 NVS 设置、STA/AP 配网、重连及 mDNS。
5. 增加 REST、WebSocket、轻量 Web 控制台。
6. 接入真实电源采样与低电量策略，完成 OTA 和持续运行验证。
