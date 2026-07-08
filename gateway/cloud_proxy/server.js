const http = require("http");
const https = require("https");
const crypto = require("crypto");
const fs = require("fs");
const path = require("path");
const { BasicCredentials } = require("@huaweicloud/huaweicloud-sdk-core");
const {
  IoTDAClient,
  ShowDeviceShadowRequest,
} = require("@huaweicloud/huaweicloud-sdk-iotda");

const CONFIG_PATH = path.join(__dirname, "config.json");

function readJson(file) {
  const config = JSON.parse(fs.readFileSync(file, "utf8"));
  if (process.env.HUAWEI_IOTDA_ENDPOINT) config.iotdaApiEndpoint = process.env.HUAWEI_IOTDA_ENDPOINT;
  if (process.env.HUAWEI_PROJECT_ID) config.projectId = process.env.HUAWEI_PROJECT_ID;
  if (process.env.HUAWEI_REGION_ID) config.regionId = process.env.HUAWEI_REGION_ID;
  if (process.env.HUAWEI_DEVICE_ID) config.deviceId = process.env.HUAWEI_DEVICE_ID;
  if (process.env.HUAWEI_SERVICE_ID) config.serviceId = process.env.HUAWEI_SERVICE_ID;
  if (process.env.HUAWEI_INSTANCE_ID) config.instanceId = process.env.HUAWEI_INSTANCE_ID;
  if (process.env.HUAWEI_NO_INSTANCE_ID === "1") delete config.instanceId;
  return config;
}

function parseCsvLine(line) {
  const cells = [];
  let current = "";
  let quoted = false;

  for (let i = 0; i < line.length; i += 1) {
    const ch = line[i];
    if (ch === "\"") {
      if (quoted && line[i + 1] === "\"") {
        current += "\"";
        i += 1;
      } else {
        quoted = !quoted;
      }
    } else if (ch === "," && !quoted) {
      cells.push(current);
      current = "";
    } else {
      current += ch;
    }
  }
  cells.push(current);
  return cells;
}

function readCredentials(csvPath) {
  if (process.env.HUAWEI_AK && process.env.HUAWEI_SK) {
    return {
      ak: process.env.HUAWEI_AK,
      sk: process.env.HUAWEI_SK,
    };
  }

  const lines = fs.readFileSync(csvPath, "utf8")
    .split(/\r?\n/)
    .filter(Boolean);
  if (lines.length < 2) {
    throw new Error(`credential csv has no data row: ${csvPath}`);
  }

  const headers = parseCsvLine(lines[0]).map((item) => item.trim());
  const row = parseCsvLine(lines[1]);
  const record = Object.fromEntries(headers.map((header, index) => [header, row[index]]));
  const ak = record["Access Key Id"];
  const sk = record["Secret Access Key"];
  if (!ak || !sk) {
    throw new Error("credential csv must contain Access Key Id and Secret Access Key");
  }
  return { ak, sk };
}

function sha256Hex(value) {
  return crypto.createHash("sha256").update(value).digest("hex");
}

function hmacHex(key, value) {
  return crypto.createHmac("sha256", key).update(value).digest("hex");
}

function sdkDate(date = new Date()) {
  const pad = (num) => String(num).padStart(2, "0");
  return `${date.getUTCFullYear()}${pad(date.getUTCMonth() + 1)}${pad(date.getUTCDate())}` +
    `T${pad(date.getUTCHours())}${pad(date.getUTCMinutes())}${pad(date.getUTCSeconds())}Z`;
}

function encodePathSegment(segment) {
  return encodeURIComponent(segment).replace(/[!'()*]/g, (ch) =>
    `%${ch.charCodeAt(0).toString(16).toUpperCase()}`);
}

function canonicalUri(pathname) {
  const uri = pathname.split("/").map((segment) => encodePathSegment(segment)).join("/");
  return uri.endsWith("/") ? uri : `${uri}/`;
}

function canonicalQuery(searchParams) {
  const pairs = [];
  for (const [key, value] of searchParams.entries()) {
    pairs.push([encodePathSegment(key), encodePathSegment(value)]);
  }
  return pairs
    .sort((a, b) => (a[0] === b[0] ? a[1].localeCompare(b[1]) : a[0].localeCompare(b[0])))
    .map(([key, value]) => `${key}=${value}`)
    .join("&");
}

function signRequest({ method, url, body = "", ak, sk }) {
  const date = sdkDate();
  const payloadHash = sha256Hex(body);
  const signedHeaders = "host;x-sdk-content-sha256;x-sdk-date";
  const canonicalHeaders = `host:${url.host}\nx-sdk-content-sha256:${payloadHash}\nx-sdk-date:${date}\n`;
  const canonicalRequest = [
    method.toUpperCase(),
    canonicalUri(url.pathname),
    canonicalQuery(url.searchParams),
    canonicalHeaders,
    signedHeaders,
    payloadHash,
  ].join("\n");
  const stringToSign = [
    "SDK-HMAC-SHA256",
    date,
    sha256Hex(canonicalRequest),
  ].join("\n");
  const signature = hmacHex(sk, stringToSign);

  return {
    "Host": url.host,
    "X-Sdk-Content-Sha256": payloadHash,
    "X-Sdk-Date": date,
    "Authorization": `SDK-HMAC-SHA256 Access=${ak}, SignedHeaders=${signedHeaders}, Signature=${signature}`,
    "Content-Type": "application/json",
  };
}

function requestJson({ method, url, body, ak, sk }) {
  return new Promise((resolve, reject) => {
    const payload = body ? JSON.stringify(body) : "";
    const headers = signRequest({ method, url, body: payload, ak, sk });
    const req = https.request({
      method,
      hostname: url.hostname,
      port: url.port || 443,
      family: 4,
      path: `${url.pathname}${url.search}`,
      headers,
    }, (res) => {
      let chunks = "";
      res.setEncoding("utf8");
      res.on("data", (chunk) => { chunks += chunk; });
      res.on("end", () => {
        let parsed = null;
        try {
          parsed = chunks ? JSON.parse(chunks) : null;
        } catch (error) {
          parsed = { raw: chunks };
        }
        if (res.statusCode >= 200 && res.statusCode < 300) {
          resolve({ statusCode: res.statusCode, data: parsed });
        } else {
          const err = new Error(`Huawei API HTTP ${res.statusCode}`);
          err.statusCode = res.statusCode;
          err.data = parsed;
          reject(err);
        }
      });
    });
    req.on("error", reject);
    req.setTimeout(15000, () => {
      req.destroy(new Error(`Huawei API timeout: ${url.href}`));
    });
    if (payload) req.write(payload);
    req.end();
  });
}

function normalizeEndpoint(value) {
  if (!value) throw new Error("iotdaApiEndpoint is required");
  return value.startsWith("http://") || value.startsWith("https://") ? value : `https://${value}`;
}

function buildShadowUrl(config) {
  const endpoint = normalizeEndpoint(config.iotdaApiEndpoint).replace(/\/+$/, "");
  return new URL(`/v5/iot/${config.projectId}/devices/${encodeURIComponent(config.deviceId)}/shadow`, endpoint);
}

function createSdkClient(config, credentials) {
  const auth = new BasicCredentials()
    .withAk(credentials.ak)
    .withSk(credentials.sk)
    .withProjectId(config.projectId);
  const derivedAuthServiceName = process.env.HUAWEI_DERIVED_AUTH_SERVICE_NAME || config.derivedAuthServiceName;
  if (derivedAuthServiceName) {
    auth.processDerivedAuthParams(derivedAuthServiceName, config.regionId || "cn-east-3");
    auth.withDerivedPredicate(() => true);
  }
  return IoTDAClient.newBuilder()
    .withCredential(auth)
    .withEndpoint(normalizeEndpoint(config.iotdaApiEndpoint))
    .build();
}

function extractProperties(shadow, serviceId) {
  if (!shadow || !Array.isArray(shadow.shadow)) return null;
  const item = shadow.shadow.find((entry) => entry.service_id === serviceId || entry.serviceId === serviceId);
  if (!item) return null;
  const reported = item.reported || {};
  const properties = reported.properties || item.properties || null;
  if (!properties) return null;
  return {
    topic: `device-shadow:${serviceId}`,
    services: [{
      service_id: serviceId,
      properties,
    }],
    shadow,
    received_at: new Date().toISOString(),
  };
}

class WebSocketHub {
  constructor() {
    this.clients = new Set();
  }

  add(req, socket) {
    const key = req.headers["sec-websocket-key"];
    if (!key) {
      socket.destroy();
      return;
    }
    const accept = crypto
      .createHash("sha1")
      .update(`${key}258EAFA5-E914-47DA-95CA-C5AB0DC85B11`)
      .digest("base64");
    socket.write([
      "HTTP/1.1 101 Switching Protocols",
      "Upgrade: websocket",
      "Connection: Upgrade",
      `Sec-WebSocket-Accept: ${accept}`,
      "",
      "",
    ].join("\r\n"));
    socket.on("close", () => this.clients.delete(socket));
    socket.on("error", () => this.clients.delete(socket));
    this.clients.add(socket);
  }

  frame(data) {
    const payload = Buffer.from(data);
    if (payload.length < 126) {
      return Buffer.concat([Buffer.from([0x81, payload.length]), payload]);
    }
    if (payload.length < 65536) {
      const header = Buffer.alloc(4);
      header[0] = 0x81;
      header[1] = 126;
      header.writeUInt16BE(payload.length, 2);
      return Buffer.concat([header, payload]);
    }
    const header = Buffer.alloc(10);
    header[0] = 0x81;
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(payload.length), 2);
    return Buffer.concat([header, payload]);
  }

  broadcast(message) {
    const data = this.frame(JSON.stringify(message));
    for (const socket of [...this.clients]) {
      if (socket.destroyed) {
        this.clients.delete(socket);
      } else {
        socket.write(data);
      }
    }
  }
}

function sendJson(res, statusCode, data) {
  const body = JSON.stringify(data, null, 2);
  res.writeHead(statusCode, {
    "Content-Type": "application/json; charset=utf-8",
    "Access-Control-Allow-Origin": "*",
    "Cache-Control": "no-store",
  });
  res.end(body);
}

async function createProxy(config, options = {}) {
  const credentials = readCredentials(config.credentialCsvPath);
  const sdkClient = createSdkClient(config, credentials);
  const hub = new WebSocketHub();
  const state = {
    latest: null,
    rawShadow: null,
    error: null,
    lastFetchAt: null,
    fetchCount: 0,
  };

  async function fetchShadow() {
    const request = new ShowDeviceShadowRequest(config.deviceId);
    if (config.instanceId) {
      request.withInstanceId(config.instanceId);
    }
    const response = await sdkClient.showDeviceShadow(request);
    state.rawShadow = response;
    state.lastFetchAt = new Date().toISOString();
    state.fetchCount += 1;
    const normalized = extractProperties(response, config.serviceId);
    if (normalized) {
      state.latest = normalized;
      state.error = null;
      hub.broadcast(normalized);
    } else {
      state.error = `No reported properties for service ${config.serviceId}`;
    }
    return normalized;
  }

  const server = http.createServer(async (req, res) => {
    if (req.method === "OPTIONS") {
      res.writeHead(204, {
        "Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Methods": "GET, OPTIONS",
        "Access-Control-Allow-Headers": "Content-Type",
      });
      res.end();
      return;
    }

    const url = new URL(req.url, `http://${req.headers.host}`);
    try {
      if (url.pathname === "/latest") {
        if (!state.latest) await fetchShadow();
        sendJson(res, 200, state.latest || { error: state.error || "No data yet" });
      } else if (url.pathname === "/refresh") {
        const latest = await fetchShadow();
        sendJson(res, 200, latest || { error: state.error });
      } else if (url.pathname === "/raw") {
        if (!state.rawShadow) await fetchShadow();
        sendJson(res, 200, state.rawShadow || { error: state.error || "No data yet" });
      } else if (url.pathname === "/status") {
        sendJson(res, 200, {
          ok: !state.error,
          deviceId: config.deviceId,
          serviceId: config.serviceId,
          endpoint: normalizeEndpoint(config.iotdaApiEndpoint),
          lastFetchAt: state.lastFetchAt,
          fetchCount: state.fetchCount,
          websocketClients: hub.clients.size,
          error: state.error,
        });
      } else {
        sendJson(res, 404, { error: "Not found", routes: ["/latest", "/refresh", "/raw", "/status", "/ws"] });
      }
    } catch (error) {
      state.error = error.message;
      sendJson(res, error.statusCode || 500, {
        error: error.message,
        details: error.data,
      });
    }
  });

  server.on("upgrade", (req, socket) => {
    const url = new URL(req.url, `http://${req.headers.host}`);
    if (url.pathname !== "/ws") {
      socket.destroy();
      return;
    }
    hub.add(req, socket);
    if (state.latest) {
      hub.broadcast(state.latest);
    }
  });

  let busy = false;
  if (options.polling !== false) {
    setInterval(async () => {
      if (busy) return;
      busy = true;
      try {
        await fetchShadow();
      } catch (error) {
        state.error = error.message;
        console.error("[cloud-proxy] fetch failed:", error.message, error.data ? JSON.stringify(error.data) : "");
      } finally {
        busy = false;
      }
    }, config.pollIntervalMs || 1000);
  }

  return { server, fetchShadow, state };
}

async function main() {
  const config = readJson(CONFIG_PATH);

  if (process.argv.includes("--once")) {
    const proxy = await createProxy(config, { polling: false });
    const latest = await proxy.fetchShadow();
    console.log(JSON.stringify(latest || proxy.state, null, 2));
    process.exit(0);
    return;
  }

  const proxy = await createProxy(config);
  proxy.server.listen(config.listenPort, config.listenHost, () => {
    console.log(`[cloud-proxy] listening on http://${config.listenHost}:${config.listenPort}`);
    console.log(`[cloud-proxy] websocket ws://${config.listenHost}:${config.listenPort}/ws`);
  });
}

main().catch((error) => {
  console.error("[cloud-proxy] fatal:", error.message);
  if (error.data) console.error(JSON.stringify(error.data, null, 2));
  process.exitCode = 1;
});
