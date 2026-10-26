# 功能模块使用说明

项目选择应用、板卡和功能模块后构建独立固件。构建时选择决定固件包含的能力，运行时接口控制已编入的能力。新增代码后需重新编译和烧录。

## 组件职责与依赖

| 可选模块 | 作用 | 依赖 |
| --- | --- | --- |
| `network` | Wi-Fi AP、网络状态、mDNS | 配置与 ESP-IDF 网络栈 |
| `web` | 公共首页、系统状态、模块页面登记、HTTP 工具、整机重启入口 | 自动选择 `network` |
| `camera` | 摄像头启停、图像获取/归还、分辨率、帧占用管理 | 板卡摄像头配置、esp32-camera；不依赖网页或 SD |
| `storage` | SD 挂载、容量检查、照片写入、文件列举/下载/删除、共享 IO 锁 | 板卡 SD 配置；不依赖摄像头或录像 |
| `recording` | AVI 编码封装、分段录制、临时文件收尾和历史索引修复 | 自动选择 `camera`、`storage`；使用共享 NVS 设置 |
| `servo` | 多实例 PWM、角度到校准脉宽换算、输出停止与销毁 | LEDC、板卡资源检查；不依赖网页 |

基础组件 `board`、`app_config`、`settings`、`runtime` 在各应用中共用。`runtime` 按选择的模块启动服务，应用入口负责产品业务。`camera` 与 `web` 同时选中时自动加入 `monitor_web`，该适配组件负责监控页面、HTTP API、MJPEG 传输及异步开关流程。

模块内部不初始化第二套网络或第二个控制端口。80 端口由 `web` 统一持有，视频流使用独立 81 端口；存储模块启用时，文件下载使用 82 端口。

## 配置与依赖边界

- [共享默认值](../config/sdkconfig.defaults)：CPU、串口和日志。
- [YD 板卡配置](../boards/YD_ESP32_S3/) 与 [XIAO 板卡配置](../boards/XIAO_ESP32_S3_SENSE/)：引脚、Flash / PSRAM 和分区表。
- [卫星应用](../apps/satellite/) 与 [监控应用](../apps/camera_monitor/)：应用入口、默认板卡、默认模块列表及产品名称。
- [构建配置](../cmake/mcu_project.cmake)：展开依赖，只发现所选组件，生成每个构建组合的功能标志，检查板卡能力。

修改运行参数使用 `menuconfig`；选择模块使用 `--modules` 或 CMake 的 `MCU_MODULES`。功能标志由选择结果生成，不在 `menuconfig` 中单独切换，以免代码选择和运行标志不一致。

例如 `--modules recording` 会自动带入摄像头与存储，可以实现不带 Wi-Fi 或网页的录像应用；应用仍需显式调用摄像头开启和录像开始接口，选择模块不会自动录像。

## 对外接口

| 模块 | 头文件 | 主要接口 |
| --- | --- | --- |
| 板卡 | [board.h](../components/board/include/board.h) | `board_get_config` |
| 网络 | [wifi_manager.h](../components/network/include/wifi_manager.h) | `wifi_manager_init`、`wifi_manager_get_status` |
| 网页 | [web_server.h](../components/web/include/web_server.h) | `web_server_start`、`web_server_handle`、`web_server_register_page` |
| 摄像头 | [camera_service.h](../components/camera/include/camera_service.h) | `camera_service_set_enabled`、`get_frame`、`return_frame` |
| 存储 | [storage_service.h](../components/storage/include/storage_service.h) | 初始化、存图、列举/下载/删除、状态、会话与 IO 锁 |
| 录像 | [recording_service.h](../components/recording/include/recording_service.h) | `start`、`stop`、`stop_and_wait`、状态与分段设置 |
| 舵机 | [servo.h](../components/servo/include/servo.h) | `servo_create`、`servo_set_angle`、`servo_stop`、`servo_destroy` |
| 设置 | [settings.h](../components/settings/include/settings.h) | `settings_init`、`settings_get_u16`、`settings_set_u16` |

摄像头帧必须成对获取和归还。关闭核心摄像头前应先停止录像；监控网页已封装这一顺序。直接使用底层 API 的应用自行安排业务顺序。

取帧等待期间不持有共享帧锁，避免双缓冲被占满时阻止其他使用者归还图像。引用计数同时包含等待取帧的调用和已借出的图像；关闭摄像头会等待它们退出，分辨率切换期间则暂停新取帧与录像启动，排空已有使用者后再配置传感器。

存储的录制会话允许同步拍照，但阻止并发下载、删除和 SD 检查。录像持有会话直到 AVI 收尾结束，图像与录像写入共用 IO 锁。

照片以排他创建方式分配编号，防止覆盖已有文件。仅文件名冲突可重试，最多 32 个候选；卡满、只读或其他创建失败直接返回并记录 `errno`。卡满时保留挂载以便删除旧文件释放空间，物理 I/O 错误则卸载；写入失败会移除本次未完成的照片并保留首个错误原因。

舵机实例由应用持有并销毁，不在系统启动时创建。调用 `servo_set_angle` 才开始输出，超出校准角度会返回错误；`servo_stop` 停止 PWM，并不承诺机械保持位置。接口不提供位置反馈或平滑轨迹，这些由雷达/展翼业务层后续实现。

当前 ESP32-S3 舵机驱动检查通道、定时器、GPIO 重复占用；当摄像头或 SD 被选中时检查相应固定资源冲突。仍需结合完整板卡丝印及外设接线确认引脚，禁止把候选引脚当作已完成接线。

## HTTP 接口

| 方法 | 地址 | 条件 |
| --- | --- | --- |
| GET | `/` | 网页模块 |
| GET | `/api/status`、`/api/modules` | 网页模块；状态中的 `modules` 表示构建选择 |
| POST | `/api/system/restart` | 网页模块；先完成注册的应用收尾，再重启整机 |
| GET | `/camera/`、`/api/camera/status` | 摄像头与网页 |
| POST | `/api/camera/enabled?enabled=0或1` | 异步启停，返回 202；轮询状态确认完成与错误 |
| GET | `/api/camera/capture` | JPEG 拍照；无存储模块时仍可在浏览器查看 |
| GET | `/api/camera/framesize?framesize=N` | 设置分辨率，录像期间拒绝更改 |
| GET | `http://设备:81/stream` | 单客户端 MJPEG 视频流 |
| GET | `/api/storage/status`、`/api/storage/files` | 监控网页适配与存储模块 |
| POST | `/api/storage/check` | 检查 SD 卡 |
| GET / DELETE | `/api/storage/file?name=...` | 下载跳转至 82 端口；删除由 80 端口处理 |
| POST | `/api/recording/start`、`/api/recording/stop` | 录像模块与监控网页适配 |
| GET | `/api/recording/status` | 录像状态 |
| POST | `/api/recording/settings?minutes=N` | 1～60 分钟分段时长 |

监控页面按设备返回的 `storage_available` 和 `recording_available` 隐藏未选功能，不请求相应接口。旧监控工程的首页现移到 `/camera/`；旧 `/api/camera`、`/api/record/*`、`/api/sd/*`、`/api/reset` 分别改为上表中的接口。

关闭摄像头会在后台等待录像收尾、请求视频流退出、等待帧归还并反初始化驱动。失败会保留可重试状态并返回状态错误；不会强制重启整机。摄像头关闭后网络和管理网页仍然运行，不等于整机断电或深度休眠。

## 增加功能模块

1. 在 `components/<模块名>/` 实现功能，公开头文件放入 `include/`，通过 `CMakeLists.txt` 声明明确的组件依赖；需要第三方库时增加固定版本的 `idf_component.yml`。
2. 在 `cmake/mcu_project.cmake` 的 `known_modules` 登记可选名称；如需自动选择依赖，在该处加入依赖展开规则。对应 `CONFIG_MCU_ENABLE_<大写模块名>` 会自动生成。
3. 在 `runtime` 中加入可选依赖和必要的初始化/收尾，或由应用入口调用模块 API。不要在组件加载或静态初始化时自动操作外设。
4. 网页适配单独调用业务接口，通过公共 HTTP 句柄注册带模块前缀的路由，再用 `web_server_register_page` 加入首页。长时间操作交给业务任务处理。
5. 用至少一个包含该模块和一个不包含该模块的应用组合验证构建与依赖裁剪。

不要为尚未实现的雷达、展翼、电源或配网功能创建空实现。它们在实现时再加入组件和应用组合。

## 增加应用与板卡

新增应用在 `apps/<名称>/` 放置 `CMakeLists.txt`、`board.default`、`sdkconfig.defaults` 和 `main/`。参考两个现有应用，定义 `MCU_APP`、默认模块和 `MCU_APP_DIR`，再包含共享 `cmake/mcu_project.cmake`。工具脚本根据目录发现应用，无需修改应用白名单。

新增板卡在 `boards/<名称>/` 提供 `board.cmake`、`sdkconfig.defaults`、`partitions.csv` 和 `include/board_definition.h`。能力标志与实际引脚必须一致；只提供有接线依据的摄像头和 SD 映射，资料放到对应 `docs/<板卡名>/`。

原 XIAO 工程保留在原目录，本仓库迁入其摄像头、网页和录像代码后完成拆分。硬件运行、录像性能、供电和外设校准由上板验证确认。
