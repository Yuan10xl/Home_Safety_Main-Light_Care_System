# 华为云雷达数据看板

这是一个独立的华为云数据可视化前端，不直接保存设备密码、AK/SK 或华为云 Token。

## 数据入口

页面支持三种入口：

- WebSocket：默认 `ws://127.0.0.1:8790/ws`
- HTTP 轮询：默认 `http://127.0.0.1:8790/latest`
- 手动 JSON：粘贴华为云属性上报 JSON

后续代理服务只需要把华为云 IoTDA 收到的数据转成下面这种格式即可：

```json
{
  "services": [
    {
      "service_id": "GatewayCare",
      "properties": {
        "motion_level": 24,
        "final_result": "normal"
      }
    }
  ]
}
```

也支持带 `topic` 和 `payload` 的包装格式：

```json
{
  "topic": "$oc/devices/.../sys/properties/report",
  "payload": {
    "services": [
      {
        "service_id": "GatewayCare",
        "properties": {
          "motion_level": 24,
          "final_result": "normal"
        }
      }
    ]
  }
}
```

## 本地打开

建议通过 localhost 打开，方便后续接 WebSocket/HTTP 代理：

```powershell
cd C:\Users\ecydm\Downloads\fbb_ws63-master\src\application\samples\custom\gateway_huawei_cloud\cloud_dashboard
C:\Users\ecydm\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe -m http.server 8791 --bind 127.0.0.1
```

然后访问：

```text
http://127.0.0.1:8791
```

## 连接真实华为云数据

先启动本地代理：

```powershell
cd C:\Users\ecydm\Downloads\fbb_ws63-master\src\application\samples\custom\gateway_huawei_cloud\cloud_proxy
C:\Users\ecydm\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\bin\node.exe server.js
```

再打开本页面，点击 `连接 WebSocket`，或点击 `HTTP 轮询`。
