(() => {
  const isDevice = location.protocol !== "file:" && !["localhost", "127.0.0.1"].includes(location.hostname);

  let previewCameraEnabled = false;
  function mockApi(path) {
    if (path === "/api/camera/status") return Promise.resolve({ ip: "192.168.4.1", sensor: "disabled", storage_available: false, recording_available: false, camera_change_pending: false, camera_change_error: 0, camera_ready: previewCameraEnabled, camera_enabled: previewCameraEnabled, framesize: -1, ap_active: true, ssid: "CMCC-ESP", hostname: "xiao-s3-camera.local", stream_port: 81 });
    if (path === "/api/recording/status") return Promise.resolve({ state: "idle", active: false, storage_busy: false, camera_enabled: false, sd_ready: true, sd_total_bytes: 0, sd_free_bytes: 0, sd_error: "", segment_minutes: 5, frames: 0, path: "", last_error: "" });
    if (path.includes("/api/camera/enabled")) { previewCameraEnabled = path.includes("enabled=1"); return Promise.resolve({ message: previewCameraEnabled ? "预览：摄像头已开启" : "预览：摄像头已关闭" }); }
    if (path.includes("record/settings")) return Promise.resolve({ message: "预览：录制周期已设置" });
    if (path.includes("record/start")) return Promise.resolve({ message: "预览：录像已开始" });
    if (path.includes("record/stop")) return Promise.resolve({ message: "预览：录像正在停止" });
    if (path.includes("record/sd-check")) return Promise.resolve({ message: "预览：SD 卡检查通过" });
    if (path === "/api/storage/files") return Promise.resolve({ sd_ready: true, total_bytes: 0, free_bytes: 0, recording_active: false, files: [], truncated: false });
    return Promise.resolve({ message: "预览：操作已完成" });
  }

  async function api(path, options) {
    if (!isDevice) return mockApi(path, options);
    const response = await fetch(path, options);
    let payload;
    try {
      payload = await response.json();
    } catch (error) {
      throw new Error(`设备返回了无效响应 (${response.status})`);
    }
    if (!response.ok) {
      const error = new Error(payload.message || payload.error || `请求失败 (${response.status})`);
      error.status = response.status;
      error.payload = payload;
      throw error;
    }
    return payload;
  }

  function streamUrl(port = 81) {
    if (!isDevice) return "";
    return `http://${location.hostname}:${port}/stream?_=${Date.now()}`;
  }

  window.mcuWeb = { api, isDevice, streamUrl };
})();
