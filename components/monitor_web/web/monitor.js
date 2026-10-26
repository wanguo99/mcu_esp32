(() => {
  const { api, isDevice, streamUrl } = window.mcuWeb;
  const status = document.getElementById("status");
  const stream = document.getElementById("stream");
  const streamStage = document.getElementById("stream-stage");
  const reconnectButton = document.getElementById("reconnect");
  const fullscreenButton = document.getElementById("fullscreen");
  const size = document.getElementById("size");
  const period = document.getElementById("period");
  const startButton = document.getElementById("record-start");
  const stopButton = document.getElementById("record-stop");
  const sdCheckButton = document.getElementById("sd-check");
  const captureButton = document.getElementById("capture");
  const photoPreview = document.getElementById("photo-preview");
  let photoObjectUrl;
  const resetButton = document.getElementById("reset");
  const cameraToggle = document.getElementById("camera-toggle");
  const refreshFilesButton = document.getElementById("sd-refresh");
  const fileList = document.getElementById("file-list");
  const storageSummary = document.getElementById("storage-summary");
  const statusDot = document.querySelector(".status-dot");
  let streamPort = 81;
  let deviceSummary = "连接中...";
  let recordingSummary = "";
  let reconnectTimer;
  let statusTimer;
  let statusPolling = true;
  let statusRequest;
  let fileRequest;
  let recordingActive;
  let storageBusy = false;
  let cameraEnabled = false;
  let storageAvailable = false;
  let recordingAvailable = false;

  function applyCapabilities(result) {
    storageAvailable = Boolean(result.storage_available);
    recordingAvailable = Boolean(result.recording_available);
    document.querySelectorAll('[data-feature="storage"]').forEach(el => { el.hidden = !storageAvailable; });
    document.querySelectorAll('[data-feature="recording"]').forEach(el => { el.hidden = !recordingAvailable; });
    document.getElementById("device-name").textContent = result.device_name || "摄像头监控";
  }

  async function waitForCameraChange(enabled) {
    const deadline = Date.now() + 45000;
    while (Date.now() < deadline) {
      const result = await api("/api/camera/status");
      if (!result.camera_change_pending) {
        if (result.camera_change_error || Boolean(result.camera_enabled) !== enabled)
          throw new Error("设备未能完成摄像头切换，请查看状态和串口日志");
        return;
      }
      await new Promise(resolve => setTimeout(resolve, 300));
    }
    throw new Error("摄像头切换超时，请刷新状态");
  }
  let suppressStreamReconnect = false;
  const recordingFps = 5;

  function updateCameraUi(enabled) {
    cameraEnabled = Boolean(enabled);
    cameraToggle.checked = cameraEnabled;
    streamStage.classList.toggle("camera-disabled", !cameraEnabled);
    reconnectButton.disabled = !cameraEnabled;
    fullscreenButton.disabled = !cameraEnabled;
    if (!cameraEnabled) {
      stream.removeAttribute("src");
      clearTimeout(reconnectTimer);
    }
    captureButton.disabled = !cameraEnabled || (storageBusy && !recordingActive);
    size.disabled = !cameraEnabled || recordingActive;
    startButton.disabled = !cameraEnabled || storageBusy;
    if (!cameraEnabled) recordingSummary = "摄像头已关闭，等待启用";
  }

  const formatStorage = bytes => {
    if (!Number.isFinite(bytes) || bytes < 0) return "";
    const units = ["B", "KiB", "MiB", "GiB"];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024;
      unit += 1;
    }
    return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
  };

  const formatRecordingDuration = frames => {
    const totalSeconds = Math.max(0, Math.floor(Number(frames || 0) / recordingFps));
    const minutes = Math.floor(totalSeconds / 60);
    const seconds = String(totalSeconds % 60).padStart(2, "0");
    return `${minutes}分: ${seconds}秒`;
  };

  const recordingFileName = path => {
    const normalizedPath = String(path || "").replace(/\\/g, "/");
    return normalizedPath.split("/").pop() || "文件准备中";
  };

  const renderStatus = () => {
    status.textContent = recordingSummary ? `${deviceSummary} | ${recordingSummary}` : deviceSummary;
  };

  const setConnectionState = state => {
    statusDot.classList.toggle("is-online", state === "online");
    statusDot.classList.toggle("is-error", state === "error");
  };

  const nativeFullscreenElement = () => document.fullscreenElement || document.webkitFullscreenElement;

  function updateFullscreenButton() {
    const active = nativeFullscreenElement() === streamStage || streamStage.classList.contains("is-fullscreen-fallback");
    const label = active ? "退出全屏" : "全屏显示";
    fullscreenButton.setAttribute("aria-label", label);
    fullscreenButton.setAttribute("aria-pressed", String(active));
    fullscreenButton.title = label;
  }

  function enterFallbackFullscreen() {
    streamStage.classList.add("is-fullscreen-fallback");
    document.body.classList.add("fullscreen-fallback");
    updateFullscreenButton();
  }

  function exitFallbackFullscreen() {
    streamStage.classList.remove("is-fullscreen-fallback");
    document.body.classList.remove("fullscreen-fallback");
    updateFullscreenButton();
  }

  function toggleFullscreen() {
    if (nativeFullscreenElement() === streamStage) {
      const exitFullscreen = document.exitFullscreen || document.webkitExitFullscreen || document.webkitCancelFullScreen;
      if (exitFullscreen) exitFullscreen.call(document);
      return;
    }
    if (streamStage.classList.contains("is-fullscreen-fallback")) {
      exitFallbackFullscreen();
      return;
    }

    const requestFullscreen = streamStage.requestFullscreen || streamStage.webkitRequestFullscreen || streamStage.webkitRequestFullScreen;
    if (!requestFullscreen) {
      enterFallbackFullscreen();
      return;
    }
    try {
      const request = requestFullscreen.call(streamStage);
      if (request && typeof request.catch === "function") request.catch(enterFallbackFullscreen);
    } catch (error) {
      enterFallbackFullscreen();
    }
  }

  const reconnect = () => {
    clearTimeout(reconnectTimer);
    reconnectTimer = undefined;
    if (!cameraEnabled) return;
    if (!isDevice) {
      document.body.classList.add("preview");
      return;
    }
    stream.removeAttribute("src");
    setTimeout(() => { stream.src = streamUrl(streamPort); }, 0);
  };

  function refreshRecordingStatus() {
    if (statusRequest) return statusRequest;
    const request = (async () => {
      try {
        const camera = await api("/api/camera/status");
        applyCapabilities(camera);
        const wasEnabled = cameraEnabled;
        updateCameraUi(camera.camera_enabled);
        cameraToggle.disabled = Boolean(camera.camera_change_pending);
        if (!wasEnabled && cameraEnabled) reconnect();
        if (!recordingAvailable) {
          if (storageAvailable) {
            const storage = await api("/api/storage/status");
            storageBusy = Boolean(storage.busy);
            updateCameraUi(camera.camera_enabled);
            sdCheckButton.disabled = storageBusy;
            refreshFilesButton.disabled = storageBusy;
            updateFileActions(storageBusy, "传输中");
          }
          recordingSummary = camera.camera_change_pending ? "摄像头切换中" :
            camera.camera_change_error ? "摄像头切换失败" :
            cameraEnabled ? "摄像头已开启" : "摄像头已关闭，等待启用";
          renderStatus();
          return;
        }
        const result = await api("/api/recording/status");
        period.value = String(result.segment_minutes);
        storageBusy = Boolean(result.storage_busy || result.active);
        updateCameraUi(result.camera_enabled);
        period.disabled = storageBusy;
        size.disabled = !cameraEnabled || result.active;
        startButton.disabled = !cameraEnabled || storageBusy;
        stopButton.disabled = !result.active;
        cameraToggle.disabled = Boolean(camera.camera_change_pending);
        sdCheckButton.disabled = storageBusy;
        refreshFilesButton.disabled = storageBusy;
        if (result.state === "active") {
          recordingSummary = `录像中 ${recordingFileName(result.path)}文件 ${formatRecordingDuration(result.frames)}`;
        }
        else if (result.state === "starting") recordingSummary = "录像启动中";
        else if (result.state === "stopping") recordingSummary = "录像停止中";
        else if (result.storage_busy) recordingSummary = "SD 卡文件传输中";
        else if (!cameraEnabled) recordingSummary = "摄像头已关闭，等待启用";
        else if (result.last_error) recordingSummary = `录像错误：${result.last_error}`;
        else if (!result.sd_ready) recordingSummary = `SD 卡不可用：${result.sd_error || "未检测到"}`;
        else recordingSummary = `SD 卡正常：${formatStorage(result.sd_free_bytes)} 可用`;
        updateFileActions(storageBusy, result.active ? "录像中" : "传输中");
        if (recordingActive !== result.active) {
          recordingActive = result.active;
          if (!recordingActive) refreshFiles();
        }
        renderStatus();
      } catch (error) {
        recordingSummary = "录像状态不可用";
        renderStatus();
      }
    })();
    let wrapped;
    wrapped = request.finally(() => {
      if (statusRequest === wrapped) statusRequest = undefined;
    });
    statusRequest = wrapped;
    return wrapped;
  }

  async function pollRecordingStatus() {
    await refreshRecordingStatus();
    if (statusPolling) statusTimer = setTimeout(pollRecordingStatus, 2000);
  }

  function renderFiles(result) {
    const files = Array.isArray(result.files) ? result.files.slice().sort((left, right) =>
      String(right.name).localeCompare(String(left.name), undefined, { numeric: true })) : [];
    const total = formatStorage(result.total_bytes);
    const free = formatStorage(result.free_bytes);
    storageSummary.textContent = result.sd_ready
      ? `${files.length}${result.truncated ? "+" : ""} 个文件 · 总容量 ${total || "未知"} · 可用 ${free || "未知"}`
      : "SD 卡不可用，请检查卡片或重新执行检查";
    fileList.replaceChildren();
    if (!result.sd_ready) {
      const empty = document.createElement("div");
      empty.className = "empty-state";
      empty.textContent = "无法读取 SD 卡文件";
      fileList.append(empty);
      return;
    }
    if (files.length === 0) {
      const empty = document.createElement("div");
      empty.className = "empty-state";
      empty.textContent = "SD 卡内暂无文件";
      fileList.append(empty);
      return;
    }
    for (const file of files) {
      const row = document.createElement("div");
      row.className = "file-row";
      const name = document.createElement("span");
      name.className = "file-name";
      name.textContent = file.name;
      const meta = document.createElement("span");
      meta.className = "file-meta";
      meta.textContent = formatStorage(file.size_bytes) || "0 B";
      const actions = document.createElement("span");
      actions.className = "file-actions";
      const base = `/api/storage/file?name=${encodeURIComponent(file.name)}`;
      const download = document.createElement("a");
      download.className = "file-action";
      download.dataset.label = "下载";
      download.textContent = "下载";
      download.dataset.href = base;
      download.href = download.dataset.href;
      const remove = document.createElement("button");
      remove.className = "file-action file-delete";
      remove.dataset.label = "删除";
      remove.type = "button";
      remove.textContent = "删除";
      remove.dataset.name = file.name;
      actions.append(download, remove);
      row.append(name, meta, actions);
      fileList.append(row);
    }
    updateFileActions(Boolean(result.recording_active || recordingActive), "录像中");
  }

  function updateFileActions(disabled, disabledLabel) {
    for (const action of fileList.querySelectorAll(".file-action")) {
      action.classList.toggle("is-disabled", disabled);
      action.setAttribute("aria-disabled", String(disabled));
      const label = action.dataset.label || action.textContent;
      if (action.tagName === "BUTTON") {
        action.textContent = disabled ? `${label}(${disabledLabel}不可用)` : label;
        action.disabled = disabled;
      } else if (disabled) {
        action.textContent = `${label}(${disabledLabel}不可用)`;
        action.removeAttribute("href");
        action.removeAttribute("target");
        action.removeAttribute("rel");
      } else if (action.tagName === "A") {
        action.textContent = label;
        action.href = action.dataset.href;
      }
    }
  }

  function refreshFiles() {
    if (!storageAvailable) return Promise.resolve();
    if (fileRequest) return fileRequest;
    const request = api("/api/storage/files").then(renderFiles).catch(error => {
      if (error.status === 409) {
        storageSummary.textContent = "SD 卡正在使用，录像或传输结束后自动刷新";
        updateFileActions(true, recordingActive ? "录像中" : "传输中");
        return;
      }
      storageSummary.textContent = `文件列表不可用：${error.message}`;
      fileList.replaceChildren();
      const empty = document.createElement("div");
      empty.className = "empty-state";
      empty.textContent = "无法读取 SD 卡文件";
      fileList.append(empty);
    });
    let wrapped;
    wrapped = request.finally(() => {
      if (fileRequest === wrapped) fileRequest = undefined;
    });
    fileRequest = wrapped;
    return wrapped;
  }

  fileList.addEventListener("click", async event => {
    const button = event.target.closest(".file-delete");
    if (!button || button.disabled) return;
    const name = button.dataset.name;
    if (!window.confirm(`确定删除文件 ${name}？此操作不可恢复。`)) return;
    button.disabled = true;
    try {
      await api(`/api/storage/file?name=${encodeURIComponent(name)}`, { method: "DELETE" });
      recordingSummary = `已删除：${name}`;
      renderStatus();
      await refreshFiles();
    } catch (error) {
      recordingSummary = `删除失败：${error.message}`;
      renderStatus();
      button.disabled = false;
    }
  });

  fullscreenButton.addEventListener("click", toggleFullscreen);
  document.addEventListener("fullscreenchange", updateFullscreenButton);
  document.addEventListener("webkitfullscreenchange", updateFullscreenButton);
  document.addEventListener("fullscreenerror", enterFallbackFullscreen);
  document.addEventListener("webkitfullscreenerror", enterFallbackFullscreen);
  document.addEventListener("keydown", event => {
    if (event.key === "Escape" && streamStage.classList.contains("is-fullscreen-fallback")) exitFallbackFullscreen();
  });
  stream.addEventListener("error", () => {
    if (!cameraEnabled || suppressStreamReconnect) return;
    setConnectionState("error");
    deviceSummary = "视频流已断开，正在重连...";
    renderStatus();
    clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(reconnect, 1500);
  });
  stream.addEventListener("load", () => {
    setConnectionState("online");
    if (deviceSummary.startsWith("视频流已断开")) {
      deviceSummary = "视频流已连接";
      renderStatus();
    }
  });
  reconnectButton.addEventListener("click", reconnect);
  cameraToggle.addEventListener("change", async () => {
    const requested = cameraToggle.checked;
    cameraToggle.disabled = true;
    try {
      const result = await api(`/api/camera/enabled?enabled=${requested ? 1 : 0}`, { method: "POST" });
      await waitForCameraChange(requested);
      updateCameraUi(requested);
      deviceSummary = result.message;
      recordingSummary = requested ? "摄像头已开启" : "摄像头已关闭，等待启用";
      renderStatus();
      if (requested) reconnect();
      await refreshRecordingStatus();
    } catch (error) {
      cameraToggle.checked = cameraEnabled;
      recordingSummary = `摄像头切换失败：${error.message}`;
      renderStatus();
    } finally {
      cameraToggle.disabled = false;
    }
  });
  captureButton.addEventListener("click", async () => {
    if (!isDevice) {
      deviceSummary = "预览模式：抓拍由开发板提供";
      renderStatus();
      return;
    }

    captureButton.disabled = true;
    try {
      const response = await fetch("/api/camera/capture", { cache: "no-store" });
      if (!response.ok) throw new Error(`抓拍失败 (${response.status})`);
      const savedPath = response.headers.get("X-Saved-Path");
      const saveError = response.headers.get("X-Save-Error");
      const photo = await response.blob();
      if (photoObjectUrl) URL.revokeObjectURL(photoObjectUrl);
      photoObjectUrl = URL.createObjectURL(photo);
      photoPreview.href = photoObjectUrl;
      photoPreview.hidden = false;
      deviceSummary = savedPath ? `照片已保存：${savedPath}` :
        storageAvailable ? `照片未保存：${saveError || "SD 卡不可用"}` : "照片已生成，可点击查看照片";
      renderStatus();
      if (!storageBusy) await refreshFiles();
    } catch (error) {
      deviceSummary = error.message;
      setConnectionState("error");
      renderStatus();
    } finally {
      captureButton.disabled = false;
    }
  });
  size.addEventListener("change", async () => {
    size.disabled = true;
    suppressStreamReconnect = true;
    clearTimeout(reconnectTimer);
    stream.removeAttribute("src");
    try {
      await api(`/api/camera/framesize?framesize=${size.value}`);
      reconnect();
    } catch (error) {
      deviceSummary = `无法切换分辨率：${error.message}`;
      renderStatus();
    } finally {
      suppressStreamReconnect = false;
      await refreshRecordingStatus();
    }
  });
  period.addEventListener("change", async () => {
    period.disabled = true;
    try {
      const result = await api(`/api/recording/settings?minutes=${period.value}`, { method: "POST" });
      recordingSummary = result.message;
    } catch (error) {
      recordingSummary = error.message;
    }
    await refreshRecordingStatus();
  });
  startButton.addEventListener("click", async () => {
    startButton.disabled = true;
    try {
      await api("/api/recording/start", { method: "POST" });
    } catch (error) {
      recordingSummary = error.message;
      renderStatus();
    }
    await refreshRecordingStatus();
  });
  stopButton.addEventListener("click", async () => {
    stopButton.disabled = true;
    try {
      await api("/api/recording/stop", { method: "POST" });
    } catch (error) {
      recordingSummary = error.message;
      renderStatus();
    }
    await refreshRecordingStatus();
  });
  sdCheckButton.addEventListener("click", async () => {
    sdCheckButton.disabled = true;
    try {
      const result = await api("/api/storage/check", { method: "POST" });
      recordingSummary = result.message;
    } catch (error) {
      recordingSummary = `SD 卡检查失败：${error.message}`;
    }
    await refreshRecordingStatus();
    if (!storageBusy) await refreshFiles();
    sdCheckButton.disabled = storageBusy;
  });
  refreshFilesButton.addEventListener("click", async () => {
    refreshFilesButton.disabled = true;
    await refreshFiles();
    refreshFilesButton.disabled = storageBusy;
  });
  resetButton.addEventListener("click", async () => {
    if (!window.confirm("确定要复位 MCU？正在录像时会先停止并保存当前文件。")) return;
    resetButton.disabled = true;
    try {
      await api("/api/system/restart", { method: "POST" });
      clearTimeout(reconnectTimer);
      statusPolling = false;
      clearTimeout(statusTimer);
      stream.removeAttribute("src");
      setConnectionState("resetting");
      deviceSummary = "MCU 正在复位，请稍后重新连接";
      recordingSummary = "";
      renderStatus();
    } catch (error) {
      deviceSummary = `复位失败：${error.message}`;
      resetButton.disabled = false;
      renderStatus();
    }
  });

  api("/api/camera/status").then(result => {
    applyCapabilities(result);
    refreshFiles();
    streamPort = result.stream_port || 81;
    updateCameraUi(result.camera_enabled);
    if (Number.isInteger(result.framesize) && result.framesize >= 6 && result.framesize <= 15) {
      size.value = String(result.framesize);
    }
    deviceSummary = `IP: ${result.ip} | ${result.sensor} | AP: ${result.ssid}`;
    setConnectionState("online");
    renderStatus();
    if (cameraEnabled) reconnect();
  }).catch(() => {
    deviceSummary = "设备状态不可用";
    setConnectionState("error");
    renderStatus();
    reconnect();
  });
  pollRecordingStatus();
  refreshFiles();
})();
