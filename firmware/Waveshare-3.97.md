# 微雪 ESP32-S3-ePaper-3.97

适用于带 ESP32-S3 的 3.97 英寸一体板。单独的 3.97 英寸 HAT+ 不带主控，不能通过 USB 单独烧录这份固件。

在 Mac 上，用 VS Code 的 PlatformIO 扩展打开本仓库的 `firmware` 目录。用可传输数据的 USB 线连接开发板，选择 `epd_397_waveshare_s3`，点击 Upload。首次写入会替换原厂演示程序。首次配网时，按 InkSight 的提示填写 Wi-Fi 和自己的 InkSight 服务地址。请勿通过在线更新安装其他机型的固件。

本配置使用 800 × 480 黑白图像，包含全屏、快速和局部刷新流程。没有开启四级灰度、语音和电量上报。与 4.26 英寸型号分开配置；即便控制器相同，也不能仅按芯片型号选择固件。

[微雪一体板官方示例](https://github.com/waveshareteam/ESP32-S3-ePaper-3.97)
