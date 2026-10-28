# 舵机组件

组件提供多实例硬件PWM、校准角度/脉宽、后台线性或平滑移动、速度/加速度限制、动作序列、往返扫描和状态查询。原版ESP32使用MCPWM，ESP32-S3继续使用LEDC。无需网页或应用自己循环更新PWM。网页试用入口见 [组件调试应用](component_demo.md)。

## 创建与初始位置

应用选择 `servo`，在 main 的依赖中加入 `servo`。以下 GPIO1、timer1、channel1 仅为示例，先确认板卡资源及实际舵机规格。

```c
#include "servo.h"

servo_handle_t servo;
servo_config_t config = SERVO_CONFIG_DEFAULT(1, 1, 1);
// 默认 50 Hz、500–2500 us、0–180°；必须根据舵机和机构校准。
ESP_ERROR_CHECK(servo_create(&config, &servo));
// 创建后无 PWM 输出，没有自动回中。
ESP_ERROR_CHECK(servo_set_angle(servo, 90));
ESP_ERROR_CHECK(servo_move_to(servo, 120, 1000, SERVO_CURVE_SMOOTH));
```

角度范围由 `min_angle/max_angle` 和 `min_pulse_us/max_pulse_us` 线性映射；越界、NaN、无穷值被拒绝。也可调用 `servo_set_pulse_us`，但脉宽仍受校准范围约束。500–2500 us 并非所有舵机的通用安全范围，机构限位需要单独校准。

`servo_config_t.reversed`默认`false`。设为`true`可翻转逻辑角度方向：默认校准下0°对应2500µs、90°对应1500µs、180°对应500µs。相对旋转、平滑运动、初始位置和限位均使用同一逻辑角度；`servo_set_pulse_us`仍表示实际高电平脉宽，会反向换算出逻辑角度。PWM保持50Hz、短高电平脉冲，电平极性不变。

没有位置反馈时，启动软件无法知道机械角度。首次 `set_angle` 立即输出对应 PWM，后续轨迹从已发送的指令角度开始。显式调用`servo_stop`后再次运动须先设置起始角度。`move_to` 的时间为 0 时等同立即设置。

### 动作后自动关闭PWM

`servo_config_t.auto_release_ms`控制动作结束后的目标脉宽保持时间；组件默认`0`，表示持续输出。创建前设为`1000`，可在直接设置角度后保持1秒，或在平滑轨迹/有限序列及其hold结束后再保持1秒，然后将PWM信号拉低。不会在运动或序列hold途中关闭；新指令重新计时。该计时由组件工作任务负责，不依赖网页或卫星应用的轮询。

自动关闭后，状态为`auto_released=true`、`output_enabled=false`，保留最后指令角度，后续绝对/相对移动与复位可自动恢复PWM；记录初始位置不会重新开启PWM。显式`servo_stop`清除自动关闭标志，需要新的`set_angle`才能继续运动。`servo_cancel_motion`取消轨迹后仍遵循配置的自动关闭计时。无限往返序列会持续运行，须主动取消或停止。

卫星应用默认启用1000ms自动关闭，可通过`Satellite hardware and policies → Release servo PWM after idle target hold`调整；组件调试应用保持组件的默认持续输出。关闭PWM后不保证舵机的保持力矩；没有位置反馈时，这只是按时间释放，不能确认机械轴已经到位或释放后未被外力移动。

## 后台运动

```c
// 限制平滑轨迹的最大指令速度 60°/s、最大指令加速度 120°/s²。
ESP_ERROR_CHECK(servo_move_with_limits(servo, 60, 60, 120));
// 如需等待，调用者任务可阻塞等待；动作任务独立运行。
esp_err_t result = servo_wait_until_idle(servo, 5000);

servo_step_t steps[] = {
    {.angle=30, .duration_ms=1000, .hold_ms=500},
    {.angle=150, .duration_ms=1500, .hold_ms=500},
};
// 复制数组，执行 3 轮；repeat_count=0 表示无限循环。
ESP_ERROR_CHECK(servo_play_sequence(servo, steps, 2, 3, SERVO_CURVE_SMOOTH));
// 替换当前动作，每段移动 2 秒，端点停留 300 ms，持续往返。
ESP_ERROR_CHECK(servo_sweep(servo, 30, 150, 2000, 300));
```

`SERVO_CURVE_LINEAR` 为匀速指令插值；`SERVO_CURVE_SMOOTH` 使用三次曲线 `3t²−2t³`，起止指令速度为零。限速接口按距离计算满足该曲线速度与加速度上限的时间；限制的是连续规划的指令轨迹，实际 PWM 离散更新和机械速度由刷新周期、舵机、供电与负载决定，不是闭环运动控制。

每实例首次提交动作时创建一个 FreeRTOS 任务（3072 字节栈、优先级 4）；启用自动关闭时，直接设置角度也会创建该任务。默认每 20 ms 更新，空闲阻塞等待通知或自动关闭期限。`refresh_interval_ms=0` 使用默认值，可设 5–1000 ms。序列最多 32 步，每步移动 1–600000 ms、保持 0–600000 ms。下一步在上一步被任务确认完成后开始，因此整体耗时包含调度误差，不适用于精密多轴同步。

## 状态、停止与销毁

```c
servo_status_t state;
ESP_ERROR_CHECK(servo_get_status(servo, &state));
ESP_ERROR_CHECK(servo_cancel_motion(servo)); // 取消序列，按配置保持或定时关闭最新指令 PWM。
ESP_ERROR_CHECK(servo_stop(servo));          // 取消序列并停止 PWM。
ESP_ERROR_CHECK(servo_destroy(servo));       // 等后台任务退出，释放资源。
```

状态包含是否输出、是否已设置角度、是否运动、当前/目标指令角度、当前步骤及该步骤剩余时间、最近驱动错误。角度是软件指令，不是测量结果；保持 PWM 不承诺机械位置不变，停止 PWM 也不等于切断舵机电源。

任务创建失败返回 `ESP_ERR_NO_MEM`；PWM 更新失败终止当前动作并记录错误，`wait_until_idle` 返回该错误。排除故障后重新 `set_angle` 可恢复。等待超时返回 `ESP_ERR_TIMEOUT`，不会自动取消动作。有效的新动作替换当前序列，非法参数不会替换原动作。

同一实例的控制接口内部串行化；销毁前应用须停止其他调用者，禁止继续使用已释放句柄。GPIO冲突由`peripheral_resources`拒绝。原版ESP32由MCPWM驱动在两组中动态分配定时器及操作器，最多6实例，不占用LEDC定时器或通道；`servo_config_t.timer/channel`在此后端保留接口兼容性。ESP32-S3的多个LEDC实例可共享相同频率和分辨率的定时器，通道及GPIO须独立。

PWM更新均有实例锁保护。原版ESP32采用1MHz计时、周期边界更新的MCPWM比较器，避免ESP-IDF 6.1中LEDC更新等待前一次占空比操作完成导致的粗步进。ESP32-S3使用`ledc_set_duty` / `ledc_update_duty`，无需应用安装全局LEDC fade服务。创建MCPWM实例时输出强制为低电平，明确设置角度后释放；停止PWM则重新强制低电平。

`servo_get_pwm_status`返回后端、时基、比较值及输出使能。LEDC支持硬件占空比读回，`hardware_readback=true`；MCPWM返回最后成功下发的比较器指令，`hardware_readback=false`，不能将其当作实际轴角度或引脚电压测量。

## 指定角度与定位精度

API 的角度参数为浮点数，调试网页也允许小数角度，例如 `45.5°`。`set_angle`、`move_to` 和动作序列中的角度均是校准坐标系内的绝对目标；不是相对于当前位置再旋转的增量。

原版ESP32的MCPWM以1µs为一档，在默认500–2500µs映射0–180°时约为0.09°/档。ESP32-S3继续使用14位LEDC，在默认50Hz下一档约`20000 / 16384 = 1.221µs`，约0.110°/档。这些是指令分辨率，不是实物定位精度；改变配置或增加小数位不会保证实际角度精度。

轨迹结束后按`auto_release_ms`配置持续保持或定时关闭目标PWM，`servo_wait_until_idle`只确认软件轨迹结束，不等待自动关闭。要验证“实际到达目标角度且稳定保持”，需要提供实际角度反馈，并定义允许误差、连续稳定时间和到位超时。当前组件没有外部角度反馈，不提供“机械已到位”的判断，也不承诺 ±0.5° 或其他实物误差指标。

实际方案需先确定舵机型号、工作范围、供电及负载，并测量多个目标角度及两个转动方向的重复定位误差。现有线性映射支持端点校准；如果实测需要非线性补偿或外部闭环，再依据反馈接口增加对应支持，不能把软件指令角度作为测量反馈使用。

## 相对转动、初始位置和复位

以下功能全部由 `servo` 组件实现，不依赖 `web`、`network`、HTTP 或调试应用。应用任务、按键、串口命令等都可直接调用同一套 C API。

```c
// 前提：已按实际器件校准并创建 servo，示例校准范围为 0–180°。
ESP_ERROR_CHECK(servo_set_limits(servo, 0, 150)); // 软件限位：最大 150°。
ESP_ERROR_CHECK(servo_set_angle(servo, 60));     // 建立初始指令，机械稳定后再记录。
ESP_ERROR_CHECK(servo_record_home(servo));      // 保存 60° 为初始位置。
ESP_ERROR_CHECK(servo_move_by(servo, 30, 1000, SERVO_CURVE_SMOOTH)); // 60° → 90°。
ESP_ERROR_CHECK(servo_wait_until_idle(servo, 2000));
ESP_ERROR_CHECK(servo_move_by(servo, -20, 1000, SERVO_CURVE_SMOOTH)); // 90° → 70°。
ESP_ERROR_CHECK(servo_wait_until_idle(servo, 2000));
ESP_ERROR_CHECK(servo_return_home(servo, 1000, SERVO_CURVE_SMOOTH)); // 返回保存的 60°。
```

相对转动以调用时最近一次成功输出的指令角度为基准，不以尚未完成的目标角度或外部手动转动后的机械位置为基准。运动中调用会替换旧轨迹，不排队；如果需要连续完成多个增量，等待前一个动作完成后再提交，或使用动作序列。正数增大校准角度，负数减小校准角度，实际顺/逆时针取决于安装方向。不会绕回角度或静默截断超限请求。

`servo_record_home`仅在已有起始指令、轨迹空闲、无驱动错误，且PWM开启或已自动关闭时成功；记录本身不发出新角度指令，也不把坐标重新归零。再次调用会覆盖旧初始位置。未记录时复位返回`ESP_ERR_INVALID_STATE`。`servo_return_home`默认按指定时长运动；时长0为立即设置。自动关闭后可以继续相对移动或平滑复位；显式停止PWM后，先确认机械状态并重新设置起始角度，再继续运动。

初始位置和运行时限位保存在实例内存中，销毁或 MCU 重启后清除。它们不自动写入 NVS，也不要求任何存储组件。若产品需要掉电保存，由应用决定保存策略；上电仍须重新建立可靠的起始角度，不能把上次断电前的指令当成实际位置。

## 最大角度与校准范围分离

- `servo_config_t.min_angle/max_angle`：创建时的校准范围，与最小/最大脉宽一起定义角度映射。根据真实舵机规格设置，不能靠扩大该数值让舵机获得更大的物理行程。
- `servo_set_limits(servo, min, max)`：运行时允许的运动范围，必须位于校准范围内，不改变角度与脉宽的映射。只限制最大角度时，保留原最小角度即可。
- 所有角度入口都受限位约束，包括直接设置、脉宽设置、绝对/相对移动、复位、限速、扫描和序列。
- 修改限位要求轨迹空闲，当前指令和已记录初始位置均在新范围内；否则报错并保留旧限位，不自动推动舵机。应先移动到新范围内，必要时重新记录初始位置，再应用限位。
- `servo_get_status` 提供 `min_angle`、`max_angle`、`home_known`、`home_angle`；`commanded_angle` 仍是指令估计而不是机械反馈。

本项目将 ±1° 作为实物验证目标，尚未作精度达标声明。需要按实际供电、负载、安装方向校准，并测量正反两个方向的多个目标点及重复定位误差。仅调整输入的小数位数或设定软件容差无法保证机械精度。
