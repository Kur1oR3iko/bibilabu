# 0.9 公网 HTTPS 与 B站 API 验证

局域网 HTTP 播放已由PS3和电脑服务端日志确认通过。
0.9 显示独立的HTTPS/API结果页，验证公网证书、WBI签名、视频信息、播放地址和CDN初始化范围。
它尚不把B站实际流接入解码器；0.8局域网播放包保留。

## 安装与运行

启用HEN，安装 wiliwili-PS3-api-test-0.9.pkg，覆盖原测试程序。
名称 wiliwili PS3 API Test 0.9，版本00.90。
PS3保持可访问公网，U盘保持插着用于配置和日志。
本版直接访问公网API，电脑上的局域网素材服务不参与这些请求。

默认公开视频：BV17x411w7KC。U盘 WILI_BILI.INI 可改为另一个公开视频BV号：

```
bvid=BV17x411w7KC
```

只支持12位BV号、单行配置，无账号Cookie、不要求输入密码。
启动后应依次看到 HTTPS/WBI、视频信息、播放地址、视频CDN、音频CDN的结果。
完成或报错后按 ○/Options 退出，主循环正常运行时约90秒自动退出。

## 结果含义

- NAV未登录返回-101，但带有效wbi_img时可用于匿名签名，属于正常情况。
- 视频信息显示CID，UTF-8标题记录在日志。
- 播放地址请求目标qn=32（480p），实际清晰度按API返回及可用AVC/AAC条目显示。
- 当前电脑直连参考运行中，默认样本实际返回qn=16、512x288、AAC 30216。
- 两个CDN只读取0-4095范围的初始化字节，用于验证HTTPS及Range访问。
- 尚需实际SPS/ASC、B站MP4变体、帧率/裁剪/采样率处理，不能把接口成功当成已能播放B站视频。

## TLS与错误处理

保留CA与主机名验证、SNI及TLS1.2，不提供跳过证书验证的模式。
CA随PKG安装在USRDIR。PS3系统日期需处于证书有效期。
WBI时间优先取已验证HTTPS响应的Date，记录本机时间以诊断偏差。
连接超时4秒、单次请求12秒、最多3次HTTPS重定向、API解压后正文上限256KB。
DNS/固件调用本身仍需实机验证，不能以主机超时测试承诺所有平台调用都可中断。
服务返回412/403等状态时报告并停止相关阶段，不自动更换身份、Cookie或反复重试。

## 日志

返回 WILI_EARLY.LOG 与新日志 WILI_API.LOG。
关键：NETWORK_CLIENT、HTTPS_REQUEST_BEGIN/END、verify、tls_verified、BILI_NAV_OK、BILI_VIDEO_INFO、
BILI_WBI_CLOCK、BILI_DASH_SELECTED、BILI_CDN_RANGE_OK、BILI_API_AND_CDN_PROBE_COMPLETE、退出清理。
日志不保存账号Cookie或完整带签名媒体URL。

## 构建与验证

```
python3 ports/ps3/tools/build_platform_probe.py --bili
```

主机回归检查：严格JSON/UTF-8和字段类型、WBI固定向量、AVC/AAC选择与业务错误，
受控TLS1.2服务的可信CA、拒绝不可信CA/错误主机名、正文超限、拒绝降级重定向、Range和取消。
另有同一C请求/解析代码对真实B站接口和两个CDN成功的主机记录。
PS3原生TLS与公网API仍等待实测；主机结果不替代实机结果。
