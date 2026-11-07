# 1.0.0-r4 竖屏 720p 软件解码后备路径

r3 实机确认：扩展函数已解析，但 QueryAttrEx 拒绝 720×1280、七参考帧/八缓冲配置。因此不能再仅靠硬件参数修改保证该片源播放。

r4：当这种竖屏片源无法初始化硬件解码器时，自动使用本机 FFmpeg H.264 软件解码（PPU/AltiVec），保留原始 720×1280 分辨率、参考帧和 YUV 输出。音频仍用现有 AAC 路径。视频线程与界面独立，按解码后的画面余量控制音频缓冲。软件解码能否实时达到源帧率尚待 PS3 验证，可能出现缓冲或较低帧率。

重点实测：BV1DKew61E2p 或 BV1RG4666EwD 选择720p；确认出现正确画面，再测暂停/恢复、L1/R1跳转、返回列表、另一条竖屏及横屏视频。日志应出现 SOFTWARE_AVC_FALLBACK、SOFTWARE_AVC_FIRST_FRAME_READY、SOFTWARE_AVC_PROGRESS 和 VIDEO_CLOCK。若首次加载长时间没画面，保留日志。

弹幕默认6/6且保存用户设置的功能继续保留。仅本地测试，不发布 GitHub、不写 U 盘。

## 可重现依赖
在 WSL 运行 tools/bootstrap_software_avc.py 构建 PPU 库；运行同工具加 --host 构建主机检查库，再运行 tools/check_software_avc.py。依赖为官方 FFmpeg 7.1.1，SHA-256 固定在脚本内；仅启用 H.264 解码器和 avutil，无编码器、网络或外部协议。脚本补齐该版本仅启用 H.264 时缺失的 aom_film_grain.o 依赖。

静态库采用 LGPL-2.1-or-later，源下载地址和修改均在构建脚本中，许可随 PKG 放入 USRDIR/licenses。整个应用保持原 GPL-3.0-or-later 许可。
