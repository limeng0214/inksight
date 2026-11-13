# 家庭小智语音网关

ESP32 在中按后录音，再次中按或满 8 秒发送。家庭网关将 16 kHz 单声道录音转换为 Opus，通过小智 WebSocket v1 转交给后端，再把回答解码回开发板播放。

1. 在家庭服务器依照 [小智开源服务端部署说明](https://github.com/xinnan-tech/xiaozhi-esp32-server/blob/main/docs/Deployment.md) 启动服务，并配置本地语音识别、语言模型和语音合成。使用本地模型可免 API 费用，模型下载与运行需要主机资源。
2. 在网关主机安装 libopus（Mac 可用 brew install opus），建立 Python 虚拟环境并运行 pip install -r requirements.txt。
3. 在本目录运行 python gateway.py。默认后端是 ws://127.0.0.1:8000/xiaozhi/v1/。后端不在同一主机时，通过 XIAOZHI_WS 指定实际地址。后端要求令牌时设置 XIAOZHI_TOKEN。
4. 用手机打开墨水屏留言页上的家庭 IP，在顶部填写网关主机的局域网 IPv4。网关监听端口 8765，仅在可信家庭网络使用。
5. 板上进入第六页，中按开始录音，再按一次发送。离开该页停止本地录音或播放。网关与后端需在使用期间运行。
