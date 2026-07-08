# 雷达照护 Android App

这个 App 直接通过手机 WiFi/移动网络访问华为云 IoTDA 设备影子接口，不依赖电脑上的 `cloud_proxy`。

## 打开和编译

1. 用 Android Studio 打开本文件夹：
   `src/application/samples/custom/gateway_huawei_cloud/android_app`
2. 等 Gradle 同步完成。
3. 连接安卓手机或启动模拟器。
4. 点击 Run。

## 配置

云端参数集中在：

`app/src/main/java/com/example/radargateway/CloudConfig.java`

默认读取：

- `GatewayCare.motion_level`
- `GatewayCare.final_result`

如果换设备、服务 ID 或 AK/SK，只改这个文件。
