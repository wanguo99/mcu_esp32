# XIAO ESP32S3 Sense 板卡配置

本仓库沿用原 `xiao_esp32_s3_sense` 工程的 OV3660 摄像头和 SDMMC 1-bit 配置，用于独立监控或带摄像头能力的其他应用。代码与 ESP-IDF v6.1 完成编译检查，实际摄像头及扩展板版本仍需上机核对。

| 项目 | 配置 |
| --- | --- |
| 板卡标识 | `XIAO_ESP32_S3_SENSE` |
| Flash | 8 MB，QIO / 80 MHz |
| PSRAM | 8 MB，Octal / 80 MHz |
| 应用分区 | 2 MiB factory，沿用原监控工程，未配置 OTA |
| 摄像头 | 默认关闭；JPEG，两个 PSRAM 帧缓冲；运行默认 SVGA |
| SD 模式 | SDMMC 1-bit，CLK=GPIO7、CMD=GPIO9、D0=GPIO8 |
| 摄像头时钟 | GPIO10，20 MHz，LEDC Timer 0 / Channel 0 |
| 摄像头 SCCB | SDA=GPIO40、SCL=GPIO39 |
| 摄像头 D0～D7 | GPIO15、17、18、16、14、12、11、48 |
| VSYNC / HREF / PCLK | GPIO38 / 47 / 13 |

配置源文件见 [board_definition.h](../../boards/XIAO_ESP32_S3_SENSE/include/board_definition.h)、[存储器默认值](../../boards/XIAO_ESP32_S3_SENSE/sdkconfig.defaults)和[分区表](../../boards/XIAO_ESP32_S3_SENSE/partitions.csv)。GPIO35～37 保留给 Octal PSRAM。

在仓库根目录运行：

```bash
bash tools/idf.sh --app camera_monitor build
bash tools/idf.sh --app camera_monitor -p /dev/ttyACM0 flash monitor
```

预览和拍照无需 SD 模块；照片存卡可单独启用存储；录像额外选择 `recording`。模块组合与新应用方式见 [模块说明](../modules.md)。

板上扩展排针与 YD 不同，不能沿用 YD 的舵机候选引脚。选择摄像头和 SD 后，舵机应使用剩余引脚及其他 LEDC 资源，并按实物接线校准。摄像头软件关闭不会切断整板供电。

厂商资料入口：[Seeed XIAO ESP32-S3 系列说明](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/)、[Sense 摄像头说明](https://wiki.seeedstudio.com/xiao_esp32s3_camera_usage/)。
