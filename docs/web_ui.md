# 网页控制台与无头预览

网页使用本地静态 HTML、CSS、JavaScript 和 SVG，无 CDN、外部字体、图片服务或图表库。默认深色航天控制台风格，手机自动改为单列操作。网页仍是可选适配层，未选择 `web` 时不会编译卫星页面资源。

| 地址 | 入口 |
| --- | --- |
| `/` | 设备概览、运行时间、堆内存、固件版本和已注册功能页面 |
| `/satellite/` | 卫星三轴控制、能源遥测、扫描曲线、CSV导出和辅助设备 |
| `/components/` | `component_demo` 的独立外设调试 |
| `/camera/` | 选择摄像头与网页模块后的监控页面 |

首页只展示当前应用实际注册的页面。卫星页面需选择 `web`，例如：

```bash
bash tools/idf.sh --app satellite --modules web,servo,voltage_sensor,mpu6050,ws2812,fan,relay_uart build
```

连接设备热点后访问 `http://192.168.4.1/`。服务器无需桌面环境；实际控制页面由电脑或手机浏览器渲染。

## 交互规则

- 打开页面和调整滑块不会发送动作命令，点击操作按钮后才提交。
- 指令角度与机械位置反馈明确区分；角度范围由设备状态决定。
- 未配置、未就绪、低电量、姿态异常、待机或断线时按设备能力禁用操作。停止/保持不受无关输入框的校验影响。
- 未获得的数据使用 `—`，不会自动替换成零。太阳能总输出要求左右功率都有效。
- 断线后显示数据已过期并禁用控制，保留上次遥测供查看；恢复连接后重新启用符合条件的操作。
- 扫描曲线通过“读取数据”获取最近扫描结果，切换轴会清空旧曲线，避免误读。CSV单位随天线dBm或太阳翼W变化。
- 操作日志只记录当前浏览器会话，设备接受指令不表示机械已经到位。

## 无头服务器验证

安装独立的浏览器测试工具，无需安装桌面，不改变固件依赖：

```bash
npm install --prefix /tmp/mcu-ui-review playwright
/tmp/mcu-ui-review/node_modules/.bin/playwright install chromium
PLAYWRIGHT_MODULE=/tmp/mcu-ui-review/node_modules/playwright node --test tests/browser_console.test.cjs
```

测试使用临时回环 HTTP 服务和测试夹具，不连接真实 MCU，不对硬件发送命令。无头 Chromium 禁用 GPU VSync 等待，避免没有显示设备时页面无法产生渲染帧。测试结束自动关闭服务和浏览器。

如果服务器上的 Chromium 仍出现动画帧或点击等待超时，可切换到无头 Firefox：

```bash
/tmp/mcu-ui-review/node_modules/.bin/playwright install firefox
PLAYWRIGHT_MODULE=/tmp/mcu-ui-review/node_modules/playwright PLAYWRIGHT_BROWSER=firefox node --test tests/browser_console.test.cjs
```

截图输出到 `build/ui-preview/`（Git忽略），包含首页、卫星、外设和摄像头四个页面，各有1440px桌面、1024px平板和390px手机版本。**截图中的遥测为测试夹具，用于展示布局，不是实物测量结果。** 固件页面只使用设备接口返回的数据。

未安装 Playwright 时，常规 `node --test tests/*.test.cjs` 会跳过浏览器用例，继续执行无需浏览器的回归测试；安装并设置上述环境变量后可完整执行。浏览器测试覆盖无自动动作、相对角度提交、输入保留、错误/断线恢复、缺失数据、扫描导出、移动端溢出及外部资源请求检查。
