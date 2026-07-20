# WS63E 主灯异常看护系统

本项目是2026嵌入式大赛-海思赛道参赛作品，组员：郑力源、胡凌瑞、孙启航 from UESTC

本项目是面向独居癫痫环患者而开发的基于WS63E的主灯异常看护系统

按两块板子分为两个目录：

- `demo/`：雷达板代码。负责 SEN0623 毫米波雷达数据读取、雷达轻量特征提取、灯光/安全照明控制、星闪发送。
- `gateway/`：网关板代码。负责星闪接收、协议解析、跌倒与异常活动波动的二次确认、事件输出、华为云上报、前端/App 展示。

## demo 雷达板包含内容

- `radar_sensor.c/.h`：SEN0623 雷达 UART 解析、motion_level/static_time/fall/abnormal wave 轻量特征。
- `radar_lamp_switch.c/.h`：本地开关、普通照明、安全照明模式控制。
- `radar_node_app.c`：雷达板应用入口。
- `radar_node/`：雷达特征封装、SLE server/notify 发送。
- `common/radar_protocol/`：雷达板和网关板共用的数据包协议、CRC、字段定义。
- `RADAR_SENSOR_INTEGRATION.md`：雷达接口和主控集成说明。

## gateway 网关板包含内容

- `gateway/`：网关主逻辑、SLE 接收、雷达包解析、跌倒判断、异常活动波动判断、云端上报。
- `common/radar_protocol/`：与雷达板一致的协议代码。
- `sle/`、`wifi/`：SLE/WiFi 支撑代码。
- `frontend/`：串口调试前端，用于观察雷达包、跌倒/异常波动事件和日志。
- `cloud_dashboard/`、`cloud_proxy/`：云端/网页展示相关代码。
- `android_app/`：App 源码，不包含 Android SDK、build 输出和 APK。

## 未包含内容

为控制体积并避免泄露无关文件，本包未包含：

- SDK 全量源码、编译 output、临时 build 产物；
- Android SDK / Gradle 缓存 / node_modules；
- 已生成的 APK、bin、elf、map、object 文件；
- 真实 WiFi 密码、华为云设备密钥等敏感配置。

如需在真实工程中编译，请将对应目录文件放回 WS63 工程的 `src/application/samples/custom/` 结构中，并按实际环境填写 `gateway/gateway_cloud_config.h` 中的 WiFi 与华为云参数。

## 表述边界

本项目用于居家安全看护演示，只输出“疑似跌倒风险提示”和“疑似异常活动波动提示”，不作为医疗诊断或癫痫发作确认。

