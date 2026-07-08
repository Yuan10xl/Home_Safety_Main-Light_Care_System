const els = {
  lastSeen: document.getElementById("lastSeen"),
  liveDot: document.getElementById("liveDot"),
  fallHero: document.getElementById("fallHero"),
  fallHeroValue: document.getElementById("fallHeroValue"),
  fallHeroHint: document.getElementById("fallHeroHint"),
  fallHeroSource: document.getElementById("fallHeroSource"),
  fallHeroReason: document.getElementById("fallHeroReason"),
  fallFeatureText: document.getElementById("fallFeatureText"),
  waveHero: document.getElementById("waveHero"),
  waveHeroValue: document.getElementById("waveHeroValue"),
  waveHeroHint: document.getElementById("waveHeroHint"),
  waveHeroReason: document.getElementById("waveHeroReason"),
  radarWaveText: document.getElementById("radarWaveText"),
  waveMetricText: document.getElementById("waveMetricText"),
  motionLevel: document.getElementById("motionLevel"),
  motionBar: document.getElementById("motionBar"),
  staticDuration: document.getElementById("staticDuration"),
  packetSeq: document.getElementById("packetSeq"),
  packetRate: document.getElementById("packetRate"),
  sampleSeqText: document.getElementById("sampleSeqText"),
  newSampleText: document.getElementById("newSampleText"),
  serialState: document.getElementById("serialState"),
  scanState: document.getElementById("scanState"),
  sleState: document.getElementById("sleState"),
  notifyState: document.getElementById("notifyState"),
  radarState: document.getElementById("radarState"),
  cloudState: document.getElementById("cloudState"),
  fallState: document.getElementById("fallState"),
  officialFall: document.getElementById("officialFall"),
  localFall: document.getElementById("localFall"),
  compositeFall: document.getElementById("compositeFall"),
  fallFeature: document.getElementById("fallFeature"),
  fallReason: document.getElementById("fallReason"),
  lowObserve: document.getElementById("lowObserve"),
  convulsionState: document.getElementById("convulsionState"),
  convulsionReason: document.getElementById("convulsionReason"),
  convulsionMetrics: document.getElementById("convulsionMetrics"),
  highRatio: document.getElementById("highRatio"),
  strongRun: document.getElementById("strongRun"),
  radarWaveHint: document.getElementById("radarWaveHint"),
  convFeature: document.getElementById("convFeature"),
  newSample: document.getElementById("newSample"),
  sampleSeqLo: document.getElementById("sampleSeqLo"),
  sensorFlags: document.getElementById("sensorFlags"),
  lowPosture: document.getElementById("lowPosture"),
  fallCandidate: document.getElementById("fallCandidate"),
  radarOnline: document.getElementById("radarOnline"),
  eventList: document.getElementById("eventList"),
  motionChart: document.getElementById("motionChart"),
  rawLog: document.getElementById("rawLog"),
  rawInput: document.getElementById("rawInput"),
  applyButton: document.getElementById("applyButton"),
  demoButton: document.getElementById("demoButton"),
  clearButton: document.getElementById("clearButton"),
  exportCsvButton: document.getElementById("exportCsvButton"),
  exportLogButton: document.getElementById("exportLogButton"),
  copyLogButton: document.getElementById("copyLogButton"),
  pauseLogButton: document.getElementById("pauseLogButton"),
  wsUrl: document.getElementById("wsUrl"),
  connectWsButton: document.getElementById("connectWsButton"),
  serialButton: document.getElementById("serialButton"),
  disconnectSerialButton: document.getElementById("disconnectSerialButton"),
  baudRate: document.getElementById("baudRate"),
};

const CSV_COLUMNS = [
  "time_iso",
  "source",
  "packet_seq",
  "motion_level",
  "static_time_ms",
  "fall_result",
  "fall_reason",
  "fall_feature_hex",
  "fall_source",
  "official_fall",
  "local_fall",
  "composite_fall",
  "low_observe",
  "convulsion_result",
  "convulsion_reason",
  "amp",
  "cross",
  "peak",
  "strong",
  "high_ratio",
  "max_high_run",
  "conv_feature_hex",
  "conv_risk_context",
  "conv_low_band",
  "conv_high_band",
  "conv_radar_wave_hint",
  "conv_low_posture",
  "conv_fall_candidate",
  "new_sample",
  "radar_wave",
  "low_posture",
  "fall_candidate",
  "sample_seq_lo",
  "sensor_flags_hex",
  "radar_online",
  "serial_state",
  "scan_state",
  "sle_state",
  "notify_state",
  "radar_state",
  "cloud_state",
  "event_type",
  "raw_packet_line",
  "raw_fall_line",
  "raw_convulsion_line",
  "raw_cloud_line",
  "raw_event_line",
];

const RAW_LOG_VISIBLE_LIMIT = 1200;
const FALL_OFFICIAL_BIT = 0x01;
const FALL_LOCAL_BIT = 0x02;
const FALL_COMPOSITE_BIT = 0x04;
const CONV_RISK_BIT = 0x01;
const CONV_LOW_BIT = 0x02;
const CONV_HIGH_BIT = 0x04;
const CONV_RADAR_WAVE_BIT = 0x08;
const CONV_LOW_POSTURE_BIT = 0x10;
const CONV_FALL_CANDIDATE_BIT = 0x20;
const CONV_NEW_SAMPLE_BIT = 0x80;

const state = {
  samples: [],
  records: [],
  rawLogs: [],
  packetTimes: [],
  ws: null,
  serialPort: null,
  serialReader: null,
  serialBuffer: "",
  latestRecordIndex: -1,
  logPaused: false,
  lastFallFeature: 0,
  lastRadarWave: 0,
  lastFallResult: undefined,
  lastWaveResult: undefined,
  lastStrongMotion: false,
  packet: {
    fallFeature: 0,
    convFeature: 0,
    newSample: "",
    radarWave: 0,
    lowPosture: 0,
    fallCandidate: 0,
    sampleSeqLo: "",
    sensorFlags: "",
  },
  fall: {
    result: undefined,
    reason: "--",
    lowObserve: "",
  },
  wave: {
    result: undefined,
    reason: "--",
    amp: "",
    cross: "",
    peak: "",
    strong: "",
    highRatio: "",
    maxHighRun: "",
  },
};

const chart = els.motionChart.getContext("2d");

function nowText() {
  return new Date().toLocaleTimeString();
}

function isoText(time = Date.now()) {
  return new Date(time).toISOString();
}

function setLive() {
  els.lastSeen.textContent = `最后数据 ${nowText()}`;
  els.liveDot.classList.add("live");
}

function setText(el, value) {
  if (!el) return;
  if (value !== undefined && value !== null && value !== "") {
    el.textContent = String(value);
  }
}

function setPanelState(panel, stateName) {
  if (!panel) return;
  panel.classList.remove("ok", "warn", "danger");
  panel.classList.add(stateName);
}

function addEvent(text, level = "") {
  const li = document.createElement("li");
  li.className = level;
  li.textContent = `${nowText()}  ${text}`;
  els.eventList.prepend(li);
  while (els.eventList.children.length > 160) {
    els.eventList.lastElementChild.remove();
  }
}

function csvEscape(value) {
  if (value === undefined || value === null) return "";
  const text = String(value);
  if (/[",\r\n]/.test(text)) return `"${text.replace(/"/g, "\"\"")}"`;
  return text;
}

function hexByte(value) {
  if (value === undefined || value === null || value === "") return "";
  const num = Number(value);
  if (!Number.isFinite(num)) return String(value);
  return `0x${num.toString(16).padStart(2, "0")}`;
}

function formatDuration(ms) {
  const value = Number(ms);
  if (!Number.isFinite(value)) return "--";
  if (value >= 1000) {
    const seconds = value / 1000;
    return `${Number.isInteger(seconds) ? seconds : seconds.toFixed(1)} s`;
  }
  return `${value} ms`;
}

function decodeFallSource(mask) {
  const value = Number(mask) || 0;
  const parts = [];
  if (value & FALL_OFFICIAL_BIT) parts.push("官方83_81");
  if (value & FALL_LOCAL_BIT) parts.push("本地低位规则");
  if (value & FALL_COMPOSITE_BIT) parts.push("综合保持");
  return parts.length ? parts.join(" + ") : "--";
}

function decodeConvFeature(mask) {
  const value = Number(mask) || 0;
  const parts = [];
  if (value & CONV_RISK_BIT) parts.push("risk");
  if (value & CONV_LOW_BIT) parts.push("low");
  if (value & CONV_HIGH_BIT) parts.push("high");
  if (value & CONV_RADAR_WAVE_BIT) parts.push("radar_wave");
  if (value & CONV_LOW_POSTURE_BIT) parts.push("low_posture");
  if (value & CONV_FALL_CANDIDATE_BIT) parts.push("fall_candidate");
  if (value & CONV_NEW_SAMPLE_BIT) parts.push("new_sample");
  return parts.length ? parts.join(" / ") : "--";
}

function makeRecord(fields = {}) {
  const time = fields.time || Date.now();
  const record = {
    time,
    time_iso: isoText(time),
    source: "",
    packet_seq: "",
    motion_level: "",
    static_time_ms: "",
    fall_result: "",
    fall_reason: "",
    fall_feature_hex: "",
    fall_source: "",
    official_fall: "",
    local_fall: "",
    composite_fall: "",
    low_observe: "",
    convulsion_result: "",
    convulsion_reason: "",
    amp: "",
    cross: "",
    peak: "",
    strong: "",
    high_ratio: "",
    max_high_run: "",
    conv_feature_hex: "",
    conv_risk_context: "",
    conv_low_band: "",
    conv_high_band: "",
    conv_radar_wave_hint: "",
    conv_low_posture: "",
    conv_fall_candidate: "",
    new_sample: "",
    radar_wave: "",
    low_posture: "",
    fall_candidate: "",
    sample_seq_lo: "",
    sensor_flags_hex: "",
    radar_online: "",
    serial_state: els.serialState.textContent,
    scan_state: els.scanState.textContent,
    sle_state: els.sleState.textContent,
    notify_state: els.notifyState.textContent,
    radar_state: els.radarState.textContent,
    cloud_state: els.cloudState.textContent,
    event_type: "",
    raw_packet_line: "",
    raw_fall_line: "",
    raw_convulsion_line: "",
    raw_cloud_line: "",
    raw_event_line: "",
    ...fields,
  };
  state.records.push(record);
  state.latestRecordIndex = state.records.length - 1;
  return record;
}

function latestRecord() {
  if (state.latestRecordIndex < 0) return null;
  return state.records[state.latestRecordIndex] || null;
}

function updateLatestRecord(fields = {}) {
  const record = latestRecord();
  if (!record) return makeRecord(fields);
  Object.assign(record, fields);
  return record;
}

function classifyRawLine(raw) {
  const lower = raw.toLowerCase();
  if (lower.includes("fall_result=1") || lower.includes("convulsion_result=1") || lower.includes("suspected")) {
    return "bad";
  }
  if (
    lower.includes("failed") ||
    lower.includes("not ready") ||
    lower.includes("disconnected") ||
    lower.includes("no_risk") ||
    lower.includes("feature_not_met") ||
    lower.includes("duplicate_sample") ||
    lower.includes("cooldown") ||
    lower.includes("warmup")
  ) {
    return "warn";
  }
  if (
    lower.includes("connected") ||
    lower.includes("notify subscribed") ||
    lower.includes("crc ok") ||
    lower.includes("packet seq") ||
    lower.includes("mqtt connected")
  ) {
    return "good";
  }
  return "";
}

function shouldKeepRawLine(raw) {
  return (
    raw.includes("[GATEWAY]") ||
    raw.includes("[Connected]") ||
    raw.includes("[Disconnected]") ||
    raw.includes("CONNACK") ||
    raw.includes("properties/report") ||
    raw.includes("APP|STA") ||
    raw.includes("getaddrinfo") ||
    raw.toLowerCase().includes("mqtt")
  );
}

function appendRawLog(raw) {
  if (!shouldKeepRawLine(raw)) return;
  state.rawLogs.push({
    time: Date.now(),
    line: raw,
    level: classifyRawLine(raw),
  });
  renderRawLog();
}

function renderRawLog() {
  if (state.logPaused || !els.rawLog) return;
  const visibleLogs = state.rawLogs.slice(-RAW_LOG_VISIBLE_LIMIT);
  const header =
    state.rawLogs.length > RAW_LOG_VISIBLE_LIMIT
      ? `显示最近 ${RAW_LOG_VISIBLE_LIMIT} / ${state.rawLogs.length} 行。导出日志会保存全部。\n`
      : `显示 ${state.rawLogs.length} 行。导出日志会保存全部。\n`;
  els.rawLog.textContent = header + visibleLogs
    .map((entry) => `${new Date(entry.time).toLocaleTimeString()} ${entry.level ? `[${entry.level}] ` : ""}${entry.line}`)
    .join("\n");
  els.rawLog.scrollTop = els.rawLog.scrollHeight;
}

function downloadText(filename, text, type = "text/plain") {
  const blob = new Blob([text], { type });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  a.click();
  URL.revokeObjectURL(a.href);
}

function updateFallHero() {
  const feature = Number(state.packet.fallFeature) || 0;
  const source = decodeFallSource(feature);
  setText(els.fallHeroSource, source);
  setText(els.fallHeroReason, state.fall.reason || "--");
  setText(els.fallFeatureText, feature ? `${hexByte(feature)} ${source}` : "--");

  if (state.fall.result === 1) {
    setPanelState(els.fallHero, "danger");
    setText(els.fallHeroValue, "已触发");
    setText(els.fallHeroHint, "网关最终输出：疑似跌倒提示");
    return;
  }

  if (feature) {
    setPanelState(els.fallHero, "warn");
    setText(els.fallHeroValue, "确认中");
    setText(els.fallHeroHint, "雷达端已上送跌倒特征，等待网关连续确认");
    return;
  }

  setPanelState(els.fallHero, "ok");
  setText(els.fallHeroValue, "未触发");
  setText(els.fallHeroHint, "暂无跌倒特征或网关报警");
}

function updateWaveHero() {
  const convFeature = Number(state.packet.convFeature) || 0;
  const radarWave = Number(state.packet.radarWave) || ((convFeature & CONV_RADAR_WAVE_BIT) ? 1 : 0);
  const riskContext = (convFeature & CONV_RISK_BIT) ? 1 : 0;
  const metricText =
    state.wave.amp !== "" || state.wave.cross !== "" || state.wave.peak !== ""
      ? `amp=${state.wave.amp || "--"} cross=${state.wave.cross || "--"} peak=${state.wave.peak || "--"} strong=${state.wave.strong || "--"}`
      : "--";

  setText(els.waveHeroReason, state.wave.reason || "--");
  setText(els.radarWaveText, radarWave ? "1，雷达端已发现波动特征" : "--");
  setText(els.waveMetricText, metricText);

  if (state.wave.result === 1) {
    setPanelState(els.waveHero, "danger");
    setText(els.waveHeroValue, "已触发");
    setText(els.waveHeroHint, "网关最终输出：疑似异常活动波动提示");
    return;
  }

  if (radarWave) {
    setPanelState(els.waveHero, "warn");
    setText(els.waveHeroValue, "雷达提示");
    setText(els.waveHeroHint, "雷达端已有 wave_hint，等待网关确认");
    return;
  }

  if (riskContext) {
    setPanelState(els.waveHero, "warn");
    setText(els.waveHeroValue, "观察中");
    setText(els.waveHeroHint, "处于风险观察窗口，尚未满足波动事件条件");
    return;
  }

  setPanelState(els.waveHero, "ok");
  setText(els.waveHeroValue, "未触发");
  setText(els.waveHeroHint, "暂无异常活动波动提示");
}

function updateMotion(value, sampleTime = Date.now()) {
  const motion = Number(value);
  if (!Number.isFinite(motion)) return;
  setLive();
  els.motionLevel.textContent = motion;
  els.motionBar.style.width = `${Math.max(0, Math.min(100, motion))}%`;
  state.samples.push({ time: sampleTime, motion });
  if (state.samples.length > 160) state.samples.shift();
  drawChart();

  const isStrong = motion > 40;
  if (isStrong && !state.lastStrongMotion) addEvent(`强活动 motion=${motion}`, "warn");
  state.lastStrongMotion = isStrong;
}

function updatePacketFeatures(features = {}) {
  if (features.fallFeature !== undefined) state.packet.fallFeature = features.fallFeature;
  if (features.convFeature !== undefined) state.packet.convFeature = features.convFeature;
  if (features.newSample !== undefined) state.packet.newSample = features.newSample;
  if (features.radarWave !== undefined) state.packet.radarWave = features.radarWave;
  if (features.lowPosture !== undefined) state.packet.lowPosture = features.lowPosture;
  if (features.fallCandidate !== undefined) state.packet.fallCandidate = features.fallCandidate;
  if (features.sampleSeqLo !== undefined) state.packet.sampleSeqLo = features.sampleSeqLo;
  if (features.sensorFlags !== undefined) state.packet.sensorFlags = features.sensorFlags;

  const fallFeature = Number(state.packet.fallFeature) || 0;
  const convFeature = Number(state.packet.convFeature) || 0;
  const radarWave = Number(state.packet.radarWave) || ((convFeature & CONV_RADAR_WAVE_BIT) ? 1 : 0);

  setText(els.fallFeature, fallFeature ? hexByte(fallFeature) : "--");
  setText(els.officialFall, fallFeature & FALL_OFFICIAL_BIT ? "1" : "0");
  setText(els.localFall, fallFeature & FALL_LOCAL_BIT ? "1" : "0");
  setText(els.compositeFall, fallFeature & FALL_COMPOSITE_BIT ? "1" : "0");
  setText(els.convFeature, convFeature ? `${hexByte(convFeature)} ${decodeConvFeature(convFeature)}` : "--");
  setText(els.radarWaveHint, radarWave ? "1" : "0");
  setText(els.newSample, state.packet.newSample === "" ? "--" : state.packet.newSample);
  setText(els.sampleSeqLo, state.packet.sampleSeqLo === "" ? "--" : state.packet.sampleSeqLo);
  setText(els.sensorFlags, state.packet.sensorFlags === "" ? "--" : hexByte(state.packet.sensorFlags));
  setText(els.lowPosture, state.packet.lowPosture === "" ? "--" : state.packet.lowPosture);
  setText(els.fallCandidate, state.packet.fallCandidate === "" ? "--" : state.packet.fallCandidate);
  setText(els.sampleSeqText, state.packet.sampleSeqLo === "" ? "--" : state.packet.sampleSeqLo);
  setText(
    els.newSampleText,
    state.packet.newSample === "" ? "--" : `new_sample=${state.packet.newSample}${Number(state.packet.newSample) ? " 有效新样本" : " 重复包"}`
  );

  if (fallFeature && state.lastFallFeature === 0) {
    addEvent(`雷达端跌倒特征 ${hexByte(fallFeature)} ${decodeFallSource(fallFeature)}`, "warn");
  }
  if (radarWave && !state.lastRadarWave) {
    addEvent("雷达端异常活动波动 hint", "warn");
  }
  state.lastFallFeature = fallFeature;
  state.lastRadarWave = radarWave;
  updateFallHero();
  updateWaveHero();
}

function mergeRawLine(oldLine, rawLine) {
  if (!oldLine) return rawLine;
  if (!rawLine || oldLine.includes(rawLine)) return oldLine;
  return `${oldLine} | ${rawLine}`;
}

function updatePacket(seq, motion, staticDuration, rawLine, features = {}) {
  const time = Date.now();
  const last = latestRecord();
  const samePacket = last && last.source === "packet" && Number(last.packet_seq) === Number(seq);
  const fallFeature = features.fallFeature;
  const convFeature = features.convFeature;
  const fields = {
    time: samePacket ? last.time : time,
    time_iso: samePacket ? last.time_iso : isoText(time),
    source: "packet",
    packet_seq: seq,
    motion_level: motion,
    static_time_ms: staticDuration,
    fall_feature_hex: fallFeature !== undefined ? hexByte(fallFeature) : last?.fall_feature_hex || "",
    fall_source: fallFeature !== undefined ? decodeFallSource(fallFeature) : last?.fall_source || "",
    official_fall: fallFeature !== undefined ? ((fallFeature & FALL_OFFICIAL_BIT) ? 1 : 0) : last?.official_fall || "",
    local_fall: fallFeature !== undefined ? ((fallFeature & FALL_LOCAL_BIT) ? 1 : 0) : last?.local_fall || "",
    composite_fall: fallFeature !== undefined ? ((fallFeature & FALL_COMPOSITE_BIT) ? 1 : 0) : last?.composite_fall || "",
    conv_feature_hex: convFeature !== undefined ? hexByte(convFeature) : last?.conv_feature_hex || "",
    conv_risk_context: convFeature !== undefined ? ((convFeature & CONV_RISK_BIT) ? 1 : 0) : last?.conv_risk_context || "",
    conv_low_band: convFeature !== undefined ? ((convFeature & CONV_LOW_BIT) ? 1 : 0) : last?.conv_low_band || "",
    conv_high_band: convFeature !== undefined ? ((convFeature & CONV_HIGH_BIT) ? 1 : 0) : last?.conv_high_band || "",
    conv_radar_wave_hint: convFeature !== undefined ? ((convFeature & CONV_RADAR_WAVE_BIT) ? 1 : 0) : last?.conv_radar_wave_hint || "",
    conv_low_posture: convFeature !== undefined ? ((convFeature & CONV_LOW_POSTURE_BIT) ? 1 : 0) : last?.conv_low_posture || "",
    conv_fall_candidate: convFeature !== undefined ? ((convFeature & CONV_FALL_CANDIDATE_BIT) ? 1 : 0) : last?.conv_fall_candidate || "",
    new_sample: features.newSample ?? last?.new_sample ?? "",
    radar_wave: features.radarWave ?? last?.radar_wave ?? "",
    low_posture: features.lowPosture ?? last?.low_posture ?? "",
    fall_candidate: features.fallCandidate ?? last?.fall_candidate ?? "",
    sample_seq_lo: features.sampleSeqLo ?? last?.sample_seq_lo ?? "",
    sensor_flags_hex: features.sensorFlags !== undefined ? hexByte(features.sensorFlags) : last?.sensor_flags_hex || "",
    raw_packet_line: samePacket ? mergeRawLine(last.raw_packet_line, rawLine) : rawLine,
  };

  if (samePacket) {
    Object.assign(last, fields);
  } else {
    makeRecord(fields);
    updateMotion(motion, time);
    state.packetTimes.push(time);
  }

  setText(els.packetSeq, seq);
  if (staticDuration !== undefined) setText(els.staticDuration, formatDuration(staticDuration));
  updatePacketFeatures(features);

  const cutoff = Date.now() - 60000;
  state.packetTimes = state.packetTimes.filter((item) => item >= cutoff);
  setText(els.packetRate, `${state.packetTimes.length} packets/min`);
}

function drawChart() {
  const { width, height } = els.motionChart;
  chart.clearRect(0, 0, width, height);
  chart.fillStyle = "#ffffff";
  chart.fillRect(0, 0, width, height);

  const plotTop = 10;
  const plotHeight = height - 20;
  const yFor = (value) => height - plotTop - (value / 100) * plotHeight;

  chart.fillStyle = "rgba(15, 159, 110, 0.09)";
  chart.fillRect(0, yFor(10), width, yFor(0) - yFor(10));
  chart.fillStyle = "rgba(197, 122, 20, 0.11)";
  chart.fillRect(0, yFor(40), width, yFor(10) - yFor(40));
  chart.fillStyle = "rgba(215, 55, 74, 0.10)";
  chart.fillRect(0, yFor(100), width, yFor(40) - yFor(100));

  chart.strokeStyle = "#dfe5ee";
  chart.lineWidth = 1;
  [20, 40, 60, 80, 100].forEach((value) => {
    const y = yFor(value);
    chart.beginPath();
    chart.moveTo(0, y);
    chart.lineTo(width, y);
    chart.stroke();
  });

  if (state.samples.length < 2) return;

  const step = width / Math.max(1, state.samples.length - 1);
  chart.strokeStyle = "#2563eb";
  chart.lineWidth = 3;
  chart.beginPath();
  state.samples.forEach((sample, index) => {
    const x = index * step;
    const y = yFor(Math.max(0, Math.min(100, sample.motion)));
    if (index === 0) chart.moveTo(x, y);
    else chart.lineTo(x, y);
  });
  chart.stroke();
}

function readNumber(line, key) {
  const match = line.match(new RegExp(`${key}=(-?\\d+)`));
  return match ? Number(match[1]) : undefined;
}

function readText(line, key) {
  const match = line.match(new RegExp(`${key}=([^\\s,]+)`));
  return match ? match[1] : undefined;
}

function parseProperties(properties, rawLine = "") {
  if (!properties || typeof properties !== "object") return;
  const motion = properties.motion_level ?? properties.motion ?? properties.activity_strength;
  const staticDuration =
    properties.static_duration ??
    properties.static_time ??
    properties.static_duration_ms ??
    properties.static_time_ms;
  if (motion !== undefined && !latestRecord()) {
    makeRecord({ source: "cloud", motion_level: motion, raw_cloud_line: rawLine });
  }
  updateLatestRecord({
    source: latestRecord()?.source || "cloud",
    motion_level: motion ?? latestRecord()?.motion_level ?? "",
    static_time_ms: staticDuration ?? latestRecord()?.static_time_ms ?? "",
    fall_result: properties.fall_result ?? latestRecord()?.fall_result ?? "",
    convulsion_result: properties.convulsion_result ?? latestRecord()?.convulsion_result ?? "",
    high_ratio: properties.high_ratio ?? latestRecord()?.high_ratio ?? "",
    radar_online: properties.radar_online ?? latestRecord()?.radar_online ?? "",
    raw_cloud_line: rawLine || latestRecord()?.raw_cloud_line || "",
  });
  if (motion !== undefined) updateMotion(motion);
  if (staticDuration !== undefined) setText(els.staticDuration, formatDuration(staticDuration));
  if (properties.fall_result !== undefined) {
    state.fall.result = Number(properties.fall_result);
    setText(els.fallState, state.fall.result);
    updateFallHero();
  }
  if (properties.convulsion_result !== undefined) {
    state.wave.result = Number(properties.convulsion_result);
    setText(els.convulsionState, state.wave.result);
    updateWaveHero();
  }
  if (properties.high_ratio !== undefined) setText(els.highRatio, properties.high_ratio);
  if (properties.low_observe !== undefined) setText(els.lowObserve, properties.low_observe);
  if (properties.radar_online !== undefined) setText(els.radarOnline, properties.radar_online);
  setLive();
}

function parseJsonObject(obj, rawLine = "") {
  if (!obj || typeof obj !== "object") return false;
  if (Array.isArray(obj.services)) {
    obj.services.forEach((service) => parseProperties(service.properties || service.paras, rawLine));
    setText(els.cloudState, "Payload parsed");
    updateLatestRecord({ cloud_state: els.cloudState.textContent, raw_cloud_line: rawLine });
    addEvent("华为云 payload 已解析", "good");
    return true;
  }
  if (obj.properties || obj.paras) {
    parseProperties(obj.properties || obj.paras, rawLine);
    return true;
  }
  parseProperties(obj, rawLine);
  return true;
}

function extractJsonBlocks(text) {
  const blocks = [];
  let depth = 0;
  let start = -1;
  let inString = false;
  let escaped = false;

  for (let i = 0; i < text.length; i += 1) {
    const ch = text[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch === "\\") escaped = true;
      else if (ch === "\"") inString = false;
      continue;
    }
    if (ch === "\"") inString = true;
    else if (ch === "{") {
      if (depth === 0) start = i;
      depth += 1;
    } else if (ch === "}") {
      depth -= 1;
      if (depth === 0 && start >= 0) {
        blocks.push(text.slice(start, i + 1));
        start = -1;
      }
    }
  }
  return blocks;
}

function parseJsonText(text) {
  let parsed = false;
  extractJsonBlocks(text).forEach((block) => {
    try {
      parsed = parseJsonObject(JSON.parse(block), block) || parsed;
    } catch (error) {
      // 串口日志中可能有非 JSON 的大括号输出。
    }
  });
  return parsed;
}

function updateLinkRecord() {
  updateLatestRecord({
    serial_state: els.serialState.textContent,
    scan_state: els.scanState.textContent,
    sle_state: els.sleState.textContent,
    notify_state: els.notifyState.textContent,
    radar_state: els.radarState.textContent,
    cloud_state: els.cloudState.textContent,
  });
}

function parsePacketLine(raw) {
  const rich = raw.match(
    /packet\s+seq=(\d+)\s+motion=(\d+)\s+static=(\d+)\s+fall_feature=0x([0-9a-f]+)\s+conv_feature=0x([0-9a-f]+)\s+new_sample=(\d+)\s+radar_wave=(\d+)\s+low_posture=(\d+)\s+fall_candidate=(\d+)\s+sample_seq_lo=(\d+)\s+sensor_flags=0x([0-9a-f]+)/i
  );
  if (rich) {
    updatePacket(Number(rich[1]), Number(rich[2]), Number(rich[3]), raw, {
      fallFeature: parseInt(rich[4], 16),
      convFeature: parseInt(rich[5], 16),
      newSample: Number(rich[6]),
      radarWave: Number(rich[7]),
      lowPosture: Number(rich[8]),
      fallCandidate: Number(rich[9]),
      sampleSeqLo: Number(rich[10]),
      sensorFlags: parseInt(rich[11], 16),
    });
    return true;
  }

  const shortPacket = raw.match(/packet\s+seq=(\d+)\s+motion=(\d+)\s+static=(\d+)/i);
  if (shortPacket) {
    updatePacket(Number(shortPacket[1]), Number(shortPacket[2]), Number(shortPacket[3]), raw);
    return true;
  }
  return false;
}

function parseFallLine(raw) {
  if (!raw.includes("fall_result=")) return false;
  const fallResult = readNumber(raw, "fall_result");
  const lowObserve = readNumber(raw, "low_observe");
  const reason = readText(raw, "reason") || "--";
  state.fall.result = fallResult;
  state.fall.reason = reason;
  state.fall.lowObserve = lowObserve ?? "";
  setText(els.fallState, fallResult);
  setText(els.fallReason, reason);
  setText(els.lowObserve, lowObserve);
  updateLatestRecord({
    fall_result: fallResult,
    low_observe: lowObserve,
    fall_reason: reason,
    raw_fall_line: raw,
  });
  if (fallResult === 1 && state.lastFallResult !== 1) {
    addEvent(`疑似跌倒提示 reason=${reason}`, "bad");
  }
  if (fallResult === 0 && state.lastFallResult === 1) {
    addEvent("疑似跌倒提示解除", "warn");
  }
  state.lastFallResult = fallResult;
  updateFallHero();
  return true;
}

function parseConvulsionLine(raw) {
  if (!raw.includes("convulsion_result=")) return false;
  const convulsionResult = readNumber(raw, "convulsion_result");
  const reason = readText(raw, "reason") || "--";
  const amp = readNumber(raw, "amp");
  const cross = readNumber(raw, "cross");
  const peak = readNumber(raw, "peak");
  const strong = readNumber(raw, "strong");
  const highRatio = readNumber(raw, "high_ratio");
  const maxHighRun = readNumber(raw, "max_high_run");
  state.wave = {
    result: convulsionResult,
    reason,
    amp: amp ?? "",
    cross: cross ?? "",
    peak: peak ?? "",
    strong: strong ?? "",
    highRatio: highRatio ?? "",
    maxHighRun: maxHighRun ?? "",
  };
  setText(els.convulsionState, convulsionResult);
  setText(els.convulsionReason, reason);
  if (amp !== undefined || cross !== undefined || peak !== undefined) {
    setText(els.convulsionMetrics, `${amp ?? "--"} / ${cross ?? "--"} / ${peak ?? "--"}`);
  }
  if (strong !== undefined || maxHighRun !== undefined) {
    setText(els.strongRun, `${strong ?? "--"} / ${maxHighRun ?? "--"}`);
  }
  setText(els.highRatio, highRatio);
  updateLatestRecord({
    convulsion_result: convulsionResult,
    convulsion_reason: reason,
    amp,
    cross,
    peak,
    strong,
    high_ratio: highRatio,
    max_high_run: maxHighRun,
    raw_convulsion_line: raw,
  });
  if (convulsionResult === 1 && state.lastWaveResult !== 1) {
    addEvent(`疑似异常活动波动 reason=${reason}`, "bad");
  }
  if (convulsionResult === 0 && state.lastWaveResult === 1) {
    addEvent("异常活动波动提示解除", "warn");
  }
  state.lastWaveResult = convulsionResult;
  updateWaveHero();
  return true;
}

function parseLogLine(line) {
  const raw = line.trim();
  if (!raw) return;
  appendRawLog(raw);

  if (raw.includes("[GATEWAY] scan start") || raw.includes("APP|Start Scan")) {
    setText(els.scanState, "扫描中");
  }
  if (raw.includes("set seek param ret=0x0") || raw.includes("start seek ret=0x0")) {
    setText(els.scanState, "扫描中");
  }
  if (raw.includes("uuid_0xabcd=1") || raw.includes("found RADAR_NODE_01") || raw.includes("found radar service")) {
    setText(els.scanState, "发现雷达");
    addEvent("发现雷达广播/服务", "good");
  }
  if (raw.includes("[GATEWAY] connected") || raw.includes("[Connected]")) {
    setText(els.sleState, "已连接");
    addEvent("SLE 已连接", "good");
  }
  if (raw.includes("[GATEWAY] conn state=2") || raw.includes("[Disconnected]")) {
    setText(els.sleState, "已断开");
    addEvent("SLE 已断开", "warn");
  }
  if (raw.includes("characteristic found")) {
    setText(els.notifyState, "找到特征");
  }
  if (raw.includes("notify subscribe req")) {
    setText(els.notifyState, "订阅中");
  }
  if (raw.includes("notify subscribed status=0x0")) {
    setText(els.notifyState, "已订阅");
    addEvent("Notify 已订阅", "good");
  }
  if (raw.includes("rx len=27")) {
    setText(els.notifyState, "接收中");
    setText(els.radarState, "接收中");
  }
  if (raw.includes("crc ok")) {
    setText(els.radarState, "CRC OK");
  }
  if (raw.includes("huawei mqtt connected") || raw.includes("CONNACK rc: 0")) {
    setText(els.cloudState, "已连接");
    addEvent("华为云 MQTT 已连接", "good");
  }
  if (raw.includes("huawei mqtt not ready") || raw.includes("mqtt not ready")) {
    setText(els.cloudState, "未就绪");
  }
  if (raw.includes("publish") || raw.includes("properties/report")) {
    setText(els.cloudState, "上报中");
  }

  parsePacketLine(raw);
  parseFallLine(raw);
  parseConvulsionLine(raw);

  if (raw.includes("event=")) {
    const eventType = readText(raw, "event") || "";
    updateLatestRecord({ event_type: eventType, raw_event_line: raw });
    if (eventType.includes("FALL")) {
      addEvent(`CARE_EVENT ${eventType}`, "bad");
    } else if (eventType.includes("CONVULSION") || eventType.includes("WAVE")) {
      addEvent(`CARE_EVENT ${eventType}`, "bad");
    } else {
      addEvent(`Gateway event ${eventType || raw}`, eventType ? "warn" : "");
    }
  }

  updateLinkRecord();
}

function parseText(text) {
  const parsedJson = parseJsonText(text);
  text.split(/\r?\n/).forEach(parseLogLine);
  if (!parsedJson) addEvent("文本已解析");
}

async function connectSerial() {
  if (!("serial" in navigator)) {
    addEvent("当前浏览器不支持 Web Serial，请使用 Chrome 或 Edge。", "bad");
    return;
  }
  if (!window.isSecureContext) {
    addEvent("串口需要安全上下文。建议用 localhost 打开，或确认浏览器允许 file 页面串口。", "bad");
    return;
  }
  try {
    state.serialPort = await navigator.serial.requestPort();
    await state.serialPort.open({ baudRate: Number(els.baudRate.value) });
    setText(els.serialState, "已连接");
    addEvent(`串口已连接 ${els.baudRate.value}`, "good");
    const decoder = new TextDecoder();
    state.serialReader = state.serialPort.readable.getReader();
    while (state.serialReader) {
      const { value, done } = await state.serialReader.read();
      if (done) break;
      if (value) processSerialChunk(decoder.decode(value, { stream: true }));
    }
  } catch (error) {
    addEvent(`串口错误：${error.message}`, "bad");
  } finally {
    await disconnectSerial(false);
  }
}

function processSerialChunk(chunk) {
  state.serialBuffer += chunk;
  const lines = state.serialBuffer.split(/\r?\n/);
  state.serialBuffer = lines.pop() || "";
  lines.forEach(parseLogLine);
  parseJsonText(chunk);
}

async function disconnectSerial(showEvent = true) {
  try {
    if (state.serialReader) {
      await state.serialReader.cancel();
      state.serialReader.releaseLock();
    }
  } catch (error) {
    // reader 可能已经关闭。
  }
  state.serialReader = null;
  try {
    if (state.serialPort) await state.serialPort.close();
  } catch (error) {
    // port 可能已经关闭。
  }
  state.serialPort = null;
  setText(els.serialState, "未连接");
  if (showEvent) addEvent("串口已断开");
}

function connectWs() {
  if (state.ws) {
    state.ws.close();
    state.ws = null;
    els.connectWsButton.textContent = "连接 WS";
    return;
  }
  const url = els.wsUrl.value.trim();
  if (!url) return;
  state.ws = new WebSocket(url);
  state.ws.addEventListener("open", () => {
    addEvent("WebSocket 已连接", "good");
    els.connectWsButton.textContent = "断开 WS";
  });
  state.ws.addEventListener("message", (event) => parseText(String(event.data)));
  state.ws.addEventListener("close", () => {
    addEvent("WebSocket 已关闭", "warn");
    state.ws = null;
    els.connectWsButton.textContent = "连接 WS";
  });
  state.ws.addEventListener("error", () => addEvent("WebSocket 错误", "bad"));
}

function exportCsv() {
  if (state.records.length === 0) {
    addEvent("没有可导出的记录", "warn");
    return;
  }
  const rows = [CSV_COLUMNS.join(",")];
  state.records.forEach((record) => {
    rows.push(CSV_COLUMNS.map((column) => csvEscape(record[column])).join(","));
  });
  downloadText(`radar_gateway_full_${new Date().toISOString().replace(/[:.]/g, "-")}.csv`, rows.join("\n"), "text/csv");
  addEvent(`已导出 ${state.records.length} 条完整记录`, "good");
}

function exportLogs() {
  if (state.rawLogs.length === 0) {
    addEvent("没有可导出的原始日志", "warn");
    return;
  }
  const text = state.rawLogs
    .map((entry) => `${new Date(entry.time).toISOString()} ${entry.line}`)
    .join("\n");
  downloadText(`radar_gateway_raw_logs_${new Date().toISOString().replace(/[:.]/g, "-")}.txt`, text);
  addEvent(`已导出 ${state.rawLogs.length} 行原始日志`, "good");
}

async function copyLogs() {
  const text = state.rawLogs
    .map((entry) => `${new Date(entry.time).toISOString()} ${entry.line}`)
    .join("\n");
  if (!text) {
    addEvent("没有可复制的原始日志", "warn");
    return;
  }
  try {
    await navigator.clipboard.writeText(text);
    addEvent("原始日志已复制", "good");
  } catch (error) {
    addEvent(`复制失败：${error.message}`, "bad");
  }
}

function clearAll() {
  els.eventList.innerHTML = "";
  if (els.rawLog) els.rawLog.textContent = "";
  state.samples = [];
  state.records = [];
  state.rawLogs = [];
  state.packetTimes = [];
  state.latestRecordIndex = -1;
  state.lastFallFeature = 0;
  state.lastRadarWave = 0;
  state.lastFallResult = undefined;
  state.lastWaveResult = undefined;
  state.lastStrongMotion = false;
  state.packet = {
    fallFeature: 0,
    convFeature: 0,
    newSample: "",
    radarWave: 0,
    lowPosture: 0,
    fallCandidate: 0,
    sampleSeqLo: "",
    sensorFlags: "",
  };
  state.fall = { result: undefined, reason: "--", lowObserve: "" };
  state.wave = { result: undefined, reason: "--", amp: "", cross: "", peak: "", strong: "", highRatio: "", maxHighRun: "" };
  setText(els.motionLevel, "--");
  setText(els.motionBar, "");
  els.motionBar.style.width = "0";
  setText(els.staticDuration, "--");
  setText(els.packetSeq, "--");
  setText(els.packetRate, "0 packets/min");
  updatePacketFeatures({});
  updateFallHero();
  updateWaveHero();
  drawChart();
}

function runDemo() {
  const seq = state.samples.length + 1;
  const motion = [2, 3, 52, 2, 45, 1, 60, 2][seq % 8];
  const fallFeature = seq > 4 ? 0x05 : 0x00;
  const convFeature = seq > 2 ? 0x88 : 0x82;
  const radarWave = seq > 3 ? 1 : 0;
  const fallLine = seq > 6 ? "[GATEWAY] fall_result=1 low_observe=0 reason=radar_fall_feature_confirmed" : "[GATEWAY] fall_result=0 low_observe=0 reason=none";
  const waveLine = seq > 5 ? "[GATEWAY] convulsion_result=1 amp=50 cross=4 peak=2 strong=2 high_ratio=16 max_high_run=1 reason=abnormal_motion_wave" : "[GATEWAY] convulsion_result=0 amp=28 cross=3 peak=1 strong=1 high_ratio=25 max_high_run=1 reason=collecting_window";
  parseText(`[GATEWAY] notify subscribed status=0x0
[GATEWAY] rx len=27
[GATEWAY] crc ok
[GATEWAY] packet seq=${seq} motion=${motion} static=${seq * 200}
[GATEWAY] packet seq=${seq} motion=${motion} static=${seq * 200} fall_feature=${hexByte(fallFeature)} conv_feature=${hexByte(convFeature)} new_sample=1 radar_wave=${radarWave} low_posture=0 fall_candidate=0 sample_seq_lo=${seq & 0xff} sensor_flags=0x0d
${fallLine}
${waveLine}`);
}

els.applyButton.addEventListener("click", () => parseText(els.rawInput.value));
els.demoButton.addEventListener("click", runDemo);
els.clearButton.addEventListener("click", clearAll);
els.exportCsvButton.addEventListener("click", exportCsv);
els.exportLogButton.addEventListener("click", exportLogs);
els.copyLogButton.addEventListener("click", copyLogs);
els.pauseLogButton.addEventListener("click", () => {
  state.logPaused = !state.logPaused;
  els.pauseLogButton.textContent = state.logPaused ? "恢复滚动" : "暂停滚动";
  if (!state.logPaused) renderRawLog();
});
els.connectWsButton.addEventListener("click", connectWs);
els.serialButton.addEventListener("click", connectSerial);
els.disconnectSerialButton.addEventListener("click", () => disconnectSerial(true));

drawChart();
updateFallHero();
updateWaveHero();
addEvent("前端已就绪");
