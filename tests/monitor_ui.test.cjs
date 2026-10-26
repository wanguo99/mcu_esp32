const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const path = require('node:path');
const source = fs.readFileSync(path.join(__dirname, '../components/monitor_web/web/monitor.js'), 'utf8');

function element() {
  const classes = new Set();
  return {
    handlers: {}, hidden: false, disabled: false, checked: false, textContent: '', value: '',
    classList: { add: n => classes.add(n), remove: n => classes.delete(n),
      contains: n => classes.has(n), toggle: (n, on) => on ? classes.add(n) : classes.delete(n) },
    addEventListener(name, handler) { this.handlers[name] = handler; },
    setAttribute(name, value) { this[name] = value; },
    removeAttribute(name) { delete this[name]; },
    replaceChildren() {}, append() {}, querySelectorAll() { return []; },
  };
}

function consoleHarness({ storage = false, recording = false, enabled = false, failChange = false } = {}) {
  const nodes = new Map();
  const node = id => { if (!nodes.has(id)) nodes.set(id, element()); return nodes.get(id); };
  const features = { storage: [element()], recording: [element()] };
  const requests = [];
  let pending = 0, requested = enabled, lastError = 0;
  async function api(url, options = {}) {
    requests.push([url, options.method || 'GET']);
    if (url.startsWith('/api/camera/enabled?')) {
      requested = url.endsWith('=1'); pending = 3;
      return { message: 'change requested' };
    }
    if (url === '/api/camera/status') {
      if (pending && --pending === 0) {
        if (failChange) lastError = 0x107;
        else enabled = requested;
      }
      return {
        storage_available: storage, recording_available: recording,
        camera_enabled: enabled, camera_change_pending: pending > 0, camera_change_error: lastError,
        device_name: 'Test Camera', framesize: 11, stream_port: 81, ip: '192.168.4.1', ssid: 'test', sensor: 'test',
      };
    }
    if (url === '/api/recording/status' && recording) return {
      active: false, storage_busy: false, camera_enabled: enabled, state: 'idle',
      segment_minutes: 5, sd_ready: true, sd_free_bytes: 4096,
    };
    if (url === '/api/storage/status' && storage) return { sd_ready: true, busy: false };
    if (url === '/api/storage/files' && storage) return { sd_ready: true, files: [], total_bytes: 8192, free_bytes: 4096 };
    throw new Error(`Unavailable module endpoint: ${url}`);
  }
  const context = {
    window: { mcuWeb: { api, isDevice: true, streamUrl: () => 'http://device:81/stream' }, confirm: () => true },
    document: {
      getElementById: node, querySelector: node,
      querySelectorAll: selector => features[selector.includes('storage') ? 'storage' : 'recording'],
      addEventListener() {}, createElement: element, body: element(),
    },
    setTimeout(callback, delay) { if (delay === 300 || delay === 0) queueMicrotask(callback); return 1; },
    clearTimeout() {}, Date, console,
    URL: { createObjectURL: () => 'blob:test-photo', revokeObjectURL() {} },
    fetch: async url => {
      requests.push([url, 'GET']);
      return { ok: true, headers: { get: () => null }, blob: async () => ({ type: 'image/jpeg' }) };
    },
  };
  vm.runInNewContext(source, context);
  return { node, requests, features };
}

const flush = () => new Promise(resolve => setImmediate(resolve));

test('preview-only build hides absent modules and does not request their endpoints', async () => {
  const app = consoleHarness();
  await flush();
  assert.equal(app.features.storage[0].hidden, true);
  assert.equal(app.features.recording[0].hidden, true);
  assert.ok(app.requests.length > 0);
  assert.ok(app.requests.every(([url]) => url === '/api/camera/status'));
});

test('photo storage works without the recording module', async () => {
  const app = consoleHarness({ storage: true });
  await flush();
  assert.equal(app.features.storage[0].hidden, false);
  assert.equal(app.features.recording[0].hidden, true);
  assert.ok(app.requests.some(([url]) => url === '/api/storage/files'));
  assert.ok(app.requests.some(([url]) => url === '/api/storage/status'));
  assert.ok(app.requests.every(([url]) => !url.startsWith('/api/recording/')));
});

test('camera switch waits for asynchronous completion and displays the final state', async () => {
  const app = consoleHarness();
  await flush();
  const toggle = app.node('camera-toggle');
  toggle.checked = true;
  await toggle.handlers.change();
  assert.equal(toggle.checked, true);
  assert.equal(toggle.disabled, false);
  assert.ok(app.requests.some(([url, method]) => url === '/api/camera/enabled?enabled=1' && method === 'POST'));
  assert.match(app.node('status').textContent, /摄像头已开启/);
});

test('failed camera change restores the actual switch state and reports failure', async () => {
  const app = consoleHarness({ failChange: true });
  await flush();
  const toggle = app.node('camera-toggle');
  toggle.checked = true;
  await toggle.handlers.change();
  assert.equal(toggle.checked, false);
  assert.match(app.node('status').textContent, /摄像头切换失败/);
});

test('a photo can be opened in the browser without SD storage', async () => {
  const app = consoleHarness({ enabled: true });
  await flush();
  await app.node('capture').handlers.click();
  assert.equal(app.node('photo-preview').href, 'blob:test-photo');
  assert.equal(app.node('photo-preview').hidden, false);
  assert.match(app.node('status').textContent, /照片已生成/);
});
