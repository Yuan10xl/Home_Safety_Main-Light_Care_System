const els = {
  connectionDot: document.getElementById("connectionDot"),
  connectionText: document.getElementById("connectionText"),
  motionLevel: document.getElementById("motionLevel"),
  motionMeter: document.getElementById("motionMeter"),
  finalResult: document.getElementById("finalResult"),
  resultHint: document.getElementById("resultHint"),
  resultCard: document.getElementById("resultCard"),
  serviceId: document.getElementById("serviceId"),
  sampleCount: document.getElementById("sampleCount"),
  lastUpdate: document.getElementById("lastUpdate"),
  sourceState: document.getElementById("sourceState"),
  cloudState: document.getElementById("cloudState"),
  careState: document.getElementById("careState"),
  staticTime: document.getElementById("staticTime"),
  fallResult: document.getElementById("fallResult"),
  convulsionResult: document.getElementById("convulsionResult"),
  lowObserve: document.getElementById("lowObserve"),
  radarOnline: document.getElementById("radarOnline"),
  lastTopic: document.getElementById("lastTopic"),
  messageAge: document.getElementById("messageAge"),
  lowBar: document.getElementById("lowBar"),
  midBar: document.getElementById("midBar"),
  highBar: document.getElementById("highBar"),
  lowCount: document.getElementById("lowCount"),
  midCount: document.getElementById("midCount"),
  highCount: document.getElementById("highCount"),
  eventList: document.getElementById("eventList"),
  trendCanvas: document.getElementById("trendCanvas"),
  wsUrl: document.getElementById("wsUrl"),
  wsButton: document.getElementById("wsButton"),
  httpUrl: document.getElementById("httpUrl"),
  pollInterval: document.getElementById("pollInterval"),
  pollButton: document.getElementById("pollButton"),
  jsonInput: document.getElementById("jsonInput"),
  parseButton: document.getElementById("parseButton"),
  demoButton: document.getElementById("demoButton"),
  exportButton: document.getElementById("exportButton"),
  clearButton: document.getElementById("clearButton"),
  rawMessage: document.getElementById("rawMessage"),
};

const state = {
  ws: null,
  pollTimer: null,
  demoTimer: null,
  samples: [],
  lastMessageAt: 0,
  distribution: { low: 0, mid: 0, high: 0 },
};

const trend = els.trendCanvas.getContext("2d");

function timeText(value = new Date()) {
  return value.toLocaleTimeString("zh-CN", { hour12: false });
}

function setConnection(kind, text) {
  els.connectionDot.className = `dot ${kind || ""}`.trim();
  els.connectionText.textContent = text;
}

function addEvent(text, level = "") {
  const item = document.createElement("li");
  item.className = level;
  item.textContent = `${timeText()}  ${text}`;
  els.eventList.prepend(item);
  while (els.eventList.children.length > 80) {
    els.eventList.lastElementChild.remove();
  }
}

function safeString(value, fallback = "--") {
  if (value === undefined || value === null || value === "") return fallback;
  return String(value);
}

function numericFlag(value, fallback = null) {
  if (value === undefined || value === null || value === "") return fallback;
  const number = Number(value);
  return Number.isFinite(number) ? number : fallback;
}

function yesNo(value, positive = "是", negative = "否") {
  const number = numericFlag(value);
  if (number === null) return "--";
  return number ? positive : negative;
}

function formatDurationMs(value) {
  const number = numericFlag(value);
  if (number === null) return "--";
  if (number < 1000) return `${number} ms`;
  return `${(number / 1000).toFixed(1)} 秒`;
}

function normalizeResult(value) {
  const result = safeString(value, "unknown");
  const lowered = result.toLowerCase();
  if (["normal", "ok", "none", "0"].includes(lowered)) {
    return { text: "normal", label: "正常", level: "good", hint: "状态正常" };
  }
  if (lowered.includes("low_activity") || lowered.includes("observe")) {
    return { text: result, label: "低活动观察中", level: "warn", hint: "低活动持续，建议关注" };
  }
  if (lowered.includes("fall")) {
    return { text: result, label: "疑似跌倒", level: "alert", hint: "请家属确认" };
  }
  if (lowered.includes("convulsion") || lowered.includes("abnormal")) {
    return { text: result, label: "疑似异常活动波动", level: "alert", hint: "请家属确认" };
  }
  if (lowered.includes("confirm")) {
    return { text: result, label: "请家属确认", level: "alert", hint: "请家属确认" };
  }
  return { text: result, label: result, level: "warn", hint: result };
}

function unwrapPayload(input) {
  if (typeof input === "string") {
    const trimmed = input.trim();
    if (!trimmed) return null;
    return JSON.parse(trimmed);
  }
  if (!input || typeof input !== "object") return null;
  if (typeof input.payload === "string") return unwrapPayload(input.payload);
  if (input.payload && typeof input.payload === "object") return input.payload;
  return input;
}

function getPropertiesFromService(service) {
  if (!service || typeof service !== "object") return null;
  return service.properties || service.paras || null;
}

function findCloudRecord(data) {
  const topic = data.topic || data.mqttTopic || data.resource || "";
  if (Array.isArray(data.services)) {
    for (const service of data.services) {
      const props = getPropertiesFromService(service);
      if (props && (props.motion_level !== undefined || props.final_result !== undefined)) {
        return {
          topic,
          serviceId: service.service_id || service.serviceId || "GatewayCare",
          properties: props,
        };
      }
    }
  }

  if (Array.isArray(data.shadow)) {
    for (const item of data.shadow) {
      const reported = item.reported || {};
      const props = reported.properties || item.properties;
      if (props) {
        return {
          topic,
          serviceId: item.service_id || item.serviceId || reported.service_id || "GatewayCare",
          properties: props,
        };
      }
    }
  }

  if (data.properties || data.paras) {
    return {
      topic,
      serviceId: data.service_id || data.serviceId || "GatewayCare",
      properties: data.properties || data.paras,
    };
  }

  if (data.motion_level !== undefined || data.final_result !== undefined) {
    return {
      topic,
      serviceId: data.service_id || data.serviceId || "GatewayCare",
      properties: data,
    };
  }

  return null;
}

function pushSample(record, source, raw) {
  const props = record.properties;
  const motion = Number(props.motion_level ?? props.motion ?? props.activity_strength);
  const resultInfo = normalizeResult(props.final_result ?? props.result ?? props.care_result);
  const staticTime = props.static_time ?? props.static_duration_ms ?? props.static_time_ms;
  const fallResult = props.fall_result ?? props.fall ?? props.is_suspected_fall;
  const convulsionResult = props.convulsion_result ?? props.convulsion ?? props.is_suspected_convulsion;
  const lowObserve = props.low_observe ?? props.low_activity_observe;
  const radarOnline = props.radar_online;
  const now = Date.now();

  if (!Number.isFinite(motion)) {
    addEvent("收到云端消息，但没有 motion_level", "warn");
    return;
  }

  state.lastMessageAt = now;
  state.samples.push({
    time: now,
    motion,
    result: resultInfo.text,
    staticTime: numericFlag(staticTime, 0),
    fallResult: numericFlag(fallResult, 0),
    convulsionResult: numericFlag(convulsionResult, 0),
    lowObserve: numericFlag(lowObserve, 0),
    radarOnline: numericFlag(radarOnline, 1),
    source,
    serviceId: record.serviceId,
  });
  if (state.samples.length > 120) state.samples.shift();

  state.distribution.low = state.samples.filter((s) => s.motion < 30).length;
  state.distribution.mid = state.samples.filter((s) => s.motion >= 30 && s.motion < 70).length;
  state.distribution.high = state.samples.filter((s) => s.motion >= 70).length;

  els.motionLevel.textContent = motion;
  els.motionMeter.style.width = `${Math.max(0, Math.min(100, motion))}%`;
  els.finalResult.textContent = resultInfo.label;
  els.resultHint.textContent = resultInfo.hint;
  els.resultCard.style.borderColor = resultInfo.level === "alert"
    ? "rgba(206, 50, 77, 0.65)"
    : resultInfo.level === "warn"
      ? "rgba(199, 133, 16, 0.65)"
      : "var(--line)";
  els.serviceId.textContent = record.serviceId;
  els.sampleCount.textContent = state.samples.length;
  els.lastUpdate.textContent = `更新于 ${timeText(new Date(now))}`;
  els.sourceState.textContent = source;
  els.cloudState.textContent = "已接收";
  els.careState.textContent = resultInfo.label;
  els.staticTime.textContent = formatDurationMs(staticTime);
  els.fallResult.textContent = yesNo(fallResult, "疑似跌倒", "无");
  els.convulsionResult.textContent = yesNo(convulsionResult, "有风险特征", "无");
  els.lowObserve.textContent = yesNo(lowObserve, "观察中", "否");
  els.radarOnline.textContent = yesNo(radarOnline, "在线", "离线");
  els.lastTopic.textContent = safeString(record.topic);
  els.messageAge.textContent = "刚刚";
  els.rawMessage.textContent = JSON.stringify(raw, null, 2);
  setConnection(resultInfo.level === "alert" ? "alert" : "online", "云端数据在线");

  if (resultInfo.level === "alert") {
    addEvent(`告警：${resultInfo.label}，活动强度 ${motion}`, "alert");
  } else if (resultInfo.level === "warn") {
    addEvent(`观察：${resultInfo.label}，低活动持续 ${formatDurationMs(staticTime)}`, "warn");
  } else {
    addEvent(`收到属性上报：motion_level=${motion} static_time=${safeString(staticTime, 0)}`, "good");
  }

  renderDistribution();
  drawTrend();
}

function ingest(input, source = "手动") {
  let raw;
  try {
    raw = unwrapPayload(input);
  } catch (error) {
    addEvent(`JSON 解析失败：${error.message}`, "warn");
    return;
  }
  const record = findCloudRecord(raw || {});
  if (!record) {
    els.rawMessage.textContent = JSON.stringify(raw, null, 2);
    addEvent("消息中没有找到 GatewayCare 属性", "warn");
    return;
  }
  pushSample(record, source, raw);
}

function renderDistribution() {
  const total = Math.max(1, state.samples.length);
  const { low, mid, high } = state.distribution;
  els.lowCount.textContent = low;
  els.midCount.textContent = mid;
  els.highCount.textContent = high;
  els.lowBar.style.width = `${(low / total) * 100}%`;
  els.midBar.style.width = `${(mid / total) * 100}%`;
  els.highBar.style.width = `${(high / total) * 100}%`;
}

function drawTrend() {
  const { width, height } = els.trendCanvas;
  trend.clearRect(0, 0, width, height);
  trend.fillStyle = "#ffffff";
  trend.fillRect(0, 0, width, height);

  trend.strokeStyle = "#dce3ee";
  trend.lineWidth = 1;
  for (let i = 1; i < 5; i += 1) {
    const y = (height / 5) * i;
    trend.beginPath();
    trend.moveTo(0, y);
    trend.lineTo(width, y);
    trend.stroke();
  }

  if (state.samples.length < 2) return;
  const max = Math.max(100, ...state.samples.map((sample) => sample.motion));
  const gap = width / Math.max(1, state.samples.length - 1);

  trend.strokeStyle = "#2866d8";
  trend.lineWidth = 4;
  trend.beginPath();
  state.samples.forEach((sample, index) => {
    const x = index * gap;
    const y = height - (sample.motion / max) * (height - 28) - 14;
    if (index === 0) trend.moveTo(x, y);
    else trend.lineTo(x, y);
  });
  trend.stroke();

  const latest = state.samples[state.samples.length - 1];
  const x = (state.samples.length - 1) * gap;
  const y = height - (latest.motion / max) * (height - 28) - 14;
  trend.fillStyle = latest.motion >= 70 ? "#ce324d" : "#159a72";
  trend.beginPath();
  trend.arc(x, y, 6, 0, Math.PI * 2);
  trend.fill();
}

function connectWs() {
  if (state.ws) {
    state.ws.close();
    state.ws = null;
    return;
  }
  const url = els.wsUrl.value.trim();
  if (!url) return;
  state.ws = new WebSocket(url);
  els.wsButton.textContent = "断开 WebSocket";
  els.sourceState.textContent = "WebSocket";
  setConnection("warn", "正在连接云端代理");
  state.ws.addEventListener("open", () => {
    addEvent("WebSocket 已连接", "good");
    setConnection("online", "WebSocket 已连接");
  });
  state.ws.addEventListener("message", (event) => ingest(String(event.data), "WebSocket"));
  state.ws.addEventListener("close", () => {
    addEvent("WebSocket 已断开", "warn");
    state.ws = null;
    els.wsButton.textContent = "连接 WebSocket";
    setConnection("", "等待云端数据");
  });
  state.ws.addEventListener("error", () => addEvent("WebSocket 连接错误", "warn"));
}

async function pollOnce() {
  const response = await fetch(els.httpUrl.value.trim(), { cache: "no-store" });
  if (!response.ok) throw new Error(`HTTP ${response.status}`);
  ingest(await response.text(), "HTTP");
}

function togglePolling() {
  if (state.pollTimer) {
    clearInterval(state.pollTimer);
    state.pollTimer = null;
    els.pollButton.textContent = "HTTP 轮询";
    addEvent("HTTP 轮询已停止");
    return;
  }
  const interval = Number(els.pollInterval.value);
  pollOnce().catch((error) => addEvent(`HTTP 轮询失败：${error.message}`, "warn"));
  state.pollTimer = setInterval(() => {
    pollOnce().catch((error) => addEvent(`HTTP 轮询失败：${error.message}`, "warn"));
  }, interval);
  els.pollButton.textContent = "停止轮询";
  els.sourceState.textContent = "HTTP";
  setConnection("warn", "HTTP 轮询中");
}

function demoPayload() {
  const motion = Math.round(Math.random() * 92);
  const abnormal = motion > 78 && Math.random() > 0.35;
  return {
    topic: "$oc/devices/6a48c2ab18855b39c52bdc10_gateway001/sys/properties/report",
    services: [{
      service_id: "GatewayCare",
      properties: {
        motion_level: motion,
        final_result: abnormal ? "suspected_abnormal_activity_wave" : "normal",
        static_time: Math.round(Math.random() * 18000),
        fall_result: 0,
        convulsion_result: abnormal ? 1 : 0,
        low_observe: 0,
        radar_online: 1,
      },
    }],
  };
}

function toggleDemo() {
  if (state.demoTimer) {
    clearInterval(state.demoTimer);
    state.demoTimer = null;
    els.demoButton.textContent = "模拟云端数据";
    addEvent("模拟数据已停止");
    return;
  }
  ingest(demoPayload(), "模拟");
  state.demoTimer = setInterval(() => ingest(demoPayload(), "模拟"), 1200);
  els.demoButton.textContent = "停止模拟";
}

function exportCsv() {
  const rows = ["time,motion_level,final_result,static_time,fall_result,convulsion_result,low_observe,radar_online,source,service_id"];
  state.samples.forEach((sample) => {
    rows.push(`${new Date(sample.time).toISOString()},${sample.motion},${sample.result},${sample.staticTime},${sample.fallResult},${sample.convulsionResult},${sample.lowObserve},${sample.radarOnline},${sample.source},${sample.serviceId}`);
  });
  const blob = new Blob([rows.join("\n")], { type: "text/csv;charset=utf-8" });
  const link = document.createElement("a");
  link.href = URL.createObjectURL(blob);
  link.download = "huawei_cloud_radar_data.csv";
  link.click();
  URL.revokeObjectURL(link.href);
}

function clearData() {
  state.samples = [];
  state.distribution = { low: 0, mid: 0, high: 0 };
  els.eventList.innerHTML = "";
  els.motionLevel.textContent = "--";
  els.finalResult.textContent = "--";
  els.resultHint.textContent = "尚未收到属性上报";
  els.serviceId.textContent = "--";
  els.staticTime.textContent = "--";
  els.fallResult.textContent = "--";
  els.convulsionResult.textContent = "--";
  els.lowObserve.textContent = "--";
  els.radarOnline.textContent = "--";
  els.sampleCount.textContent = "0";
  els.lastUpdate.textContent = "未更新";
  els.rawMessage.textContent = "暂无数据";
  renderDistribution();
  drawTrend();
}

setInterval(() => {
  if (!state.lastMessageAt) return;
  const seconds = Math.floor((Date.now() - state.lastMessageAt) / 1000);
  els.messageAge.textContent = `${seconds} 秒前`;
  if (seconds > 15) setConnection("warn", "云端数据延迟");
}, 1000);

els.wsButton.addEventListener("click", connectWs);
els.pollButton.addEventListener("click", togglePolling);
els.parseButton.addEventListener("click", () => ingest(els.jsonInput.value, "手动"));
els.demoButton.addEventListener("click", toggleDemo);
els.exportButton.addEventListener("click", exportCsv);
els.clearButton.addEventListener("click", clearData);

if ("serviceWorker" in navigator && location.protocol.startsWith("http")) {
  navigator.serviceWorker.register("./sw.js").catch(() => {});
}

drawTrend();
renderDistribution();
addEvent("看板已就绪");
