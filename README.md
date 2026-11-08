# bibilabu PS3 · 1.0.0

基于 PS3 的 Bilibili 原生客户端实验项目。

## 灵感与实现

- 灵感来自 wiliwili，参考其部分 Bilibili 接口和数据格式，并复用字体等资源。
- 播放页和信息流界面按 PS3 XMB 风格设计，面板使用透明玻璃效果。
- PS3 硬件解码限制参考 Movian 的 VDEC 实现。CellVdec 拒绝竖屏 720p 参数后，提供 FFmpeg/AltiVec 软件解码实验路径。

## 当前播放能力

目前横屏 720p 和竖屏 480p 可正常播放。竖屏 720p 软解能显示画面，但性能不稳定，容易卡顿或缓冲。

1080p拼尽全力我还是搞不定，性能有瓶颈而且视频高度和参考帧也有限制；当前软解运行在 PPU/AltiVec，没有 SPE 并行解码后端。

## 操作

首页：方向键切换栏目/选择视频，叉键进入，圆圈返回，三角刷新，R2 下一页。

播放页：三角暂停，L1/R1 跳转前后 10 秒，方块切换弹幕档位，SELECT 全屏，START 退出确认。清晰度菜单用上下键选择。详情页右侧视频列表用 R2/L2 翻页。

## 构建

在 WSL/Linux x86-64、Python 3.12+、GCC、make、pkg-config、OpenSSL、libcurl/json-c 开发包环境中，从仓库根目录运行：

```sh
python3 tools/bootstrap_toolchain.py
python3 tools/bootstrap_software_avc.py
python3 tools/bootstrap_software_avc.py --host
python3 tools/build_platform_probe.py --bili-play --release
```

正式构建生成 `dist/bibilabu-PS3-1.0.0.pkg`。软件解码依赖来自 FFmpeg 7.1.1，下载脚本会校验 SHA-256。依赖和许可见 [UPSTREAM.json](UPSTREAM.json)。

## 本地数据

扫码登录凭据保存在 PS3 硬盘。弹幕首次默认为 6/6；手动修改后保存在 `/dev_hdd0/tmp/bibilabu_settings.cfg`，换视频、重启应用和退出登录后仍保留。

## 实机状态

横屏 720p 和竖屏 480p 有实机正常播放记录。竖屏 720p 可通过软件解码显示，但仍卡顿，建议竖屏480p播放。


