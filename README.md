# ESP32 可组合固件工程

基于 ESP-IDF v6.1 和 ESP32-S3，将共享功能、板卡配置和应用组合分别维护。当前提供北斗卫星基础工程与 XIAO 摄像头监控应用。

| 应用 | 默认板卡 | 默认功能 |
| --- | --- | --- |
| `satellite` | `YD_ESP32_S3`，16 MB Flash / 8 MB PSRAM | NVS、芯片和内存自检；每 5 秒输出状态 |
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
tests/         # 无需开发板的监控网页行为测试
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

## 监控应用

默认启动 AP：SSID `CMCC-ESP`，密码 `66668888`；可通过 `menuconfig` 修改网络配置。连接后打开 `http://192.168.4.1/`，点击“摄像头监控”进入 `/camera/`。

摄像头默认关闭。开启后提供 MJPEG 实时画面和拍照；选中存储模块时支持照片保存和文件管理，选中录像模块时支持分段 MJPEG AVI 录像。当前默认 SVGA、单路预览、录像约 5 FPS。摄像头关闭请求在后台完成录像收尾与视频流退出，再释放摄像头驱动和帧缓冲；网络与网页保留，便于再次开启。

XIAO 配置沿用原工程的 OV3660 和 SDMMC 1-bit 接线，SD 引脚 CLK=7、CMD=9、D0=8。当前网络模块支持 AP，卫星需求中的 STA 配网、重连与其他业务后续实现。

## 验证

本次拆分执行 ESP-IDF 编译和模块组合检查；无板网页测试：

```bash
node --test tests/monitor_ui.test.cjs
```

测试覆盖无 SD/无录像的页面、异步摄像头切换、切换失败恢复和无卡拍照预览。摄像头成像、SD 实物接线、供电和舵机动作等待上板验证；未执行烧录。
