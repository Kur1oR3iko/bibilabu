# 0.8 局域网 HTTP Range 播放验证

本版只从 WILI_NET.INI 指定的 HTTP/HTTPS URL 读两条测试轨，不回退到U盘媒体。
复用此前实测通过的视频/AAC/RSX/PCM环，网络适配尚未在PS3实测。

## 准备

PS3与电脑连接同一路由器。电脑需运行 tools/media_range_server.py，服务器只提供两份合成测试MP4。
U盘保留 wiliwili-PS3-network-test-0.8.pkg 和 WILI_NET.INI。
启用HEN覆盖安装，名称 wiliwili PS3 Network Test 0.8，版本00.80。
电脑测试服务保持运行，PS3启动后应通过HTTP播放30秒/900帧，并有每秒短音和白色闪烁。
×暂停/继续，○或Options退出，主循环正常运行时约90秒自动退出。

配置格式：

```
video=http://电脑的局域网IPv4:8765/WILI_VIDEO.MP4
audio=http://电脑的局域网IPv4:8765/WILI_AUDIO.MP4
```

两份MP4无需放在U盘，电脑端使用与构建匹配的素材。
若电脑IP改变，修改配置中的两个地址后再测试。

## 传输行为

每轨256KB固定HTTP缓存，支持跨块读取和重定向。
必须收到206以及匹配的Content-Range/总长度/实际字节数，拒绝忽略Range、压缩编码、截断、变化的强ETag。
强ETag通过If-Range保持对象一致，避免把不同版本的字节拼接。
本地媒体CRC扫描不用于网络来源；已知测试文件大小和解码配置仍受检查。

使用SDK libcurl 7.64.1和mbedTLS 2.28.10，HTTPS保留CA/主机名验证、SNI和TLS1.2，CA随PKG放入USRDIR。
本轮默认LAN HTTP；原生HTTPS与B站CDN尚未实测。HTTP首次测试使用数字IP以减少DNS变量。
连接超时2.5秒，每个Range请求超时6秒；网络等待期间可检查系统退出及按键取消。
底层系统调用本身仍不构成绝对超时保证。

## 日志与定位

返回 WILI_EARLY.LOG 与新日志 WILI_NETWORK.LOG。
重点：NETCTL_STATE、NETWORK_IP、NETWORK_CLIENT、HTTP_RANGE_OK/FAILED、HTTP_MP4_SOURCE_READY、播放计数及NETWORK_SHUTDOWN_DONE。
电脑服务端JSONL日志能核对来自PS3的IP、范围和传输字节。
如果PS3连接失败，先检查电脑服务是否运行、配置IP是否正确、路由器设备隔离和Windows入站访问。

## 构建与运行电脑服务

```
python3 ports/ps3/tools/build_platform_probe.py --network
python ports/ps3/tools/media_range_server.py --root ports/ps3/dist --host 0.0.0.0 --port 8765 --log ports/ps3/build/network/server.jsonl
```

主机测试使用同一C Range客户端解封装全部900帧/1408个AAC样本，并覆盖错误范围、短响应、压缩头、过长头、ETag变更、重定向和取消。
这些检查不代表PS3联网已通过；目前没有B站API、登录或完整前端。
