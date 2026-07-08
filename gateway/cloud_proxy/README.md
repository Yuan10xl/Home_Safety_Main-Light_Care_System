# 华为云 IoTDA 本地代理

这个代理在本机读取华为云 IoTDA 设备影子，并通过 HTTP / WebSocket 提供给前端看板。

## 配置

配置文件：`config.json`

```json
{
  "credentialCsvPath": "C:/Users/ecydm/Downloads/credentials.csv",
  "projectId": "34b18a5966e2477a948d239185d98140",
  "iotdaApiEndpoint": "https://e539e002e3.st1.iotda-app.cn-east-3.myhuaweicloud.com",
  "deviceId": "6a48c2ab18855b39c52bdc10_gateway001",
  "serviceId": "GatewayCare",
  "listenHost": "127.0.0.1",
  "listenPort": 8790,
  "pollIntervalMs": 1000
}
```

AK/SK 不写进代码，运行时从 `credentials.csv` 读取。

## 运行

双击：

```text
run_proxy.cmd
```

或者运行：

```powershell
cd C:\Users\ecydm\Downloads\fbb_ws63-master\src\application\samples\custom\gateway_huawei_cloud\cloud_proxy
C:\Users\ecydm\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe server.js
```

## 接口

- `http://127.0.0.1:8790/latest`
- `http://127.0.0.1:8790/refresh`
- `http://127.0.0.1:8790/raw`
- `http://127.0.0.1:8790/status`
- `ws://127.0.0.1:8790/ws`

前端看板默认已经指向这些地址。
