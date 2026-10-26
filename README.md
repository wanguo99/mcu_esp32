# 北斗卫星桌面控制器

基于 ESP32-S3-N16R8、ESP-IDF 和 FreeRTOS。需求与后续开发顺序见 [docs/requirements-v1.md](docs/requirements-v1.md)，开发板资料入口见 [docs/README.md](docs/README.md)。

## 开发环境

固定使用 **ESP-IDF v6.1**，目标为 `esp32s3`。SDK 和工具链放在 HOME 下的 `EPS` 目录，多个项目可以共用：

```text
~/EPS/
├── esp-idf/       # v6.1 源码，包含子模块
├── tools/        # 编译器、GDB、OpenOCD、Python 虚拟环境
├── cache/        # pip 等缓存
└── export.sh     # Bash / Zsh 通用激活入口
```

在仓库根目录安装或补齐环境：

```bash
bash tools/setup.sh
```

脚本使用乐鑫官方支持的 `install.sh` 安装方式，只安装 ESP32-S3 所需工具。依赖 Git、Python 3.10+、CMake 3.22+ 和 Ninja。最小 Debian 环境缺少 `ensurepip` 时，会通过现有 pip 为独立 Python 环境引导安装 pip，不修改系统 Python。下载的工具由 ESP-IDF 安装器校验。

默认位置为 `$HOME/EPS`；如需其他位置，在安装和激活前设置 `ESP_SDK_ROOT`。脚本会检查 SDK 版本，保留已有的其他版本或修改过的源码，不自动切换它们。

## 编译

在项目根目录运行：

```bash
source tools/export.sh
idf.py build
```

也可以直接运行封装命令，无需提前激活终端：

```bash
bash tools/idf.sh build
bash tools/idf.sh size
bash tools/idf.sh menuconfig
```

其他项目可运行 `source ~/EPS/export.sh` 共用此 SDK，再进入自己的项目目录执行 `idf.py`。工程默认目标已经是 `esp32s3`，首次编译无需额外运行 `set-target`。

## 板卡默认配置

`sdkconfig.defaults` 按 **ESP32-S3-WROOM-1 / WROOM-1U N16R8** 配置：16MB Quad Flash、8MB Octal PSRAM，时钟均为 80MHz，Flash 使用 DIO。若实际模组为 WROOM-2 或带 `V` 后缀的其他型号，应先按模组规格确认 Flash 模式。

YD-ESP32-S3 的 USB 接口、完整排针表、GPIO 限制、供电和尺寸见 [开发板使用说明](docs/YD_ESP32_S3/README.md)。

| 分区 | 大小 | 用途 |
| --- | --- | --- |
| nvs | 24 KiB | 后续保存配网和设备配置 |
| otadata | 8 KiB | OTA 启动选择数据 |
| phy_init | 4 KiB | 预留 PHY 数据 |
| ota_0 | 6 MiB | 默认应用槽 |
| ota_1 | 6 MiB | 后续 OTA 应用槽 |
| storage | 3968 KiB | 预留 SPIFFS 数据区 |

分区覆盖至 16MB 边界。OTA 升级流程、回滚自检和文件系统挂载尚未实现；当前只是预留空间。`sdkconfig` 和 `build/` 是本地生成文件，默认配置与分区表纳入版本管理。

## 烧录与串口

接上开发板后，先确定端口：

```bash
ls /dev/serial/by-id/
```

将下面的 `/dev/ttyACM0` 替换成实际端口（USB 转串口板可能是 `/dev/ttyUSB0`）：

```bash
bash tools/idf.sh -p /dev/ttyACM0 flash monitor
```

串口监视器波特率为 115200，按 `Ctrl+]` 退出。默认通过 UART0 输出日志，同时启用原生 USB Serial/JTAG 辅助控制台。首次不能进入下载模式时，可按住 BOOT，再点按 RESET 后松开 BOOT。

如果端口存在但提示权限不足，先用 `ls -l /dev/ttyACM0` 检查所属组；Debian 常用 `dialout` 组，需要让系统管理员授予相应访问权限。

## 当前固件行为

- 初始化 NVS；异常时输出错误，不自动擦除已有数据。
- 输出项目版本、ESP-IDF 版本、芯片核心数和 Flash / PSRAM 实际容量。
- 每 5 秒输出运行时间及可用内部内存、PSRAM。
- 尚未配置舵机 GPIO、ADC、Wi-Fi 或 Web 服务。

## 验证记录

2026-10-26 在 Debian 13 / x86_64 上，使用 Python 3.13.5、ESP-IDF v6.1、Xtensa GCC 15.2.0 完成以下验证：

- Bash 封装命令、Zsh 环境激活和 `idf.py --version` 均正常。
- `IDF_PY_BUILD_JOBS=4 bash tools/idf.sh build` 完整编译通过，生成应用、bootloader 和分区表。
- 官方分区校验通过；生成的配置与 ESP32-S3、16MB Flash、80MHz Octal PSRAM 默认值一致。
- `esptool image-info` 确认应用为 ESP32-S3、16MB / 80MHz / DIO，版本 0.1.0，镜像大小 193,968 字节，校验和及验证哈希有效。
- GDB 和 OpenOCD 可启动。

实际 PSRAM 容量、串口输出、下载及板上运行需要接上开发板后验证。

## 参考文档

- [ESP-IDF v6.1 Linux / macOS 命令行安装](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/linux-macos-setup-legacy.html)
- [ESP32-S3 Flash / PSRAM 配置](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-guides/flash_psram_config.html)
- [ESP-IDF v6.1 发布记录](https://github.com/espressif/esp-idf/releases/tag/v6.1)
