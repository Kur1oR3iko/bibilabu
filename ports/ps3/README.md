# bibilabu PS3 1.0.0

本目录是独立原生 PS3 Bilibili 客户端。项目最初从 wiliwili v1.6.0 派生。当前仓库保留 PS3 专用代码与必要字体资源；上游业务 API 仅作参考，不包含 wiliwili 主源码。界面、网络适配和播放后端为 PS3 专用实现。上游来源和许可证见 [UPSTREAM.json](UPSTREAM.json)。

## 播放路径和限制

- 720p AVC/H.264 + AAC/LC。横屏优先 CellVdec 硬解；扩展接口拒绝竖屏配置时，PPU/AltiVec 上的 FFmpeg H.264 软件解码器作为后备，YUV 由 RSX 显示。
- 竖屏 720p 已由 r4 软件后备解出画面，但出现卡顿。r5 调整了音频预缓冲、落后门限、解码线程优先级和短缓冲状态栏；实际改善幅度需实机确认。
- 当前软解不使用 SPE 并行。任何帧率都不会因菜单策略被隐藏，但 Cell 处理能力仍决定实际速度。
- 不提供 HEVC、AV1 或 1080p 播放选项。720p 仍须存在兼容的 AVC 轨道。

## 构建

需要 WSL/Linux x86-64、Python 3.12+、GCC、make、pkg-config、FFmpeg CLI、OpenSSL、libcurl/json-c/OpenSSL 开发文件，以及由工具脚本安装的 PS3DEV PPU 工具链。

从仓库根目录运行：

```sh
python3 ports/ps3/tools/bootstrap_toolchain.py
python3 ports/ps3/tools/bootstrap_software_avc.py
python3 ports/ps3/tools/bootstrap_software_avc.py --host
python3 ports/ps3/tools/build_platform_probe.py --bili-play
```

默认生成 `ports/ps3/dist/bibilabu-PS3-test-1.0.0-r5.pkg`。`--release-only` 跳过主机回归套件，但仍会交叉编译并校验安装包。

## 控制与本地数据

首页左右换栏目、上下选择、叉键进入、三角刷新、R2 下一页。播放页 SELECT 全屏、三角暂停、L1/R1 跳转 10 秒、方块切换弹幕、START 退出确认；相关推荐和 UP 投稿可用 R2/L2 翻页。

扫码登录和观看进度保存在本机硬盘。弹幕密度首次默认为 6，之后保存于 `/dev_hdd0/tmp/bibilabu_settings.cfg`，换视频、重启应用和退出登录后保持。

## 实测说明

[r5 卡顿与状态栏修复](NATIVE_100R5_TEST.md)、[r4 竖屏软解](NATIVE_100R4_TEST.md)、[r3 扩展 VDEC 接口](NATIVE_100R3_TEST.md)、[运行时设计](NATIVE_RUNTIME_DESIGN.md)。PS3 硬解和 PPU 软解性能均须以目标机器实测。

打开 GitHub 当前仓库可以看到完整 PS3 源码；构建结果和原始用户观看日志不纳入 Git。
