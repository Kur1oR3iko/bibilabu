# 早期开发记录

以下记录保留开发过程中的判断和测试结果，部分结论已被后续实现取代。当前功能以 [README.md](README.md) 为准。记录中提到的原始实机日志和交付清单保存在开发者本地，不属于公开仓库内容。

> **bibilabu PS3 1.0.0（2026-11-07）**：当前客户端包版本已重置为 1.0.0。项目基于 wiliwili，采用独立原生 XMB 风格界面、Cell AVC/AAC 播放与 RSX YUV 输出。完整功能进度仍以各阶段说明和最新 [1.0.0 实机检查](NATIVE_100_TEST.md) 为准；发布包可复现构建，手机观看历史同步、横竖屏 720p 与高帧率播放候选已加入，需继续实机确认。公开仓库中的硬件日志已将本地 IP 替换为文档地址。

> **2.1.0：** 详情页和UP主页共享XMB壁纸，去掉灰底填充；视频固定左上，标题/UP操作/评论在左下独立滚动，右侧保留推荐或UP投稿。见 [2.1.0说明](NATIVE_210_TEST.md)。新GPU背景路径仍待实机验证。

> **2.0.0：** 最高720p；增加同账号会话内栏目/分页缓存、0～4弹幕密度、中文系统输入法，以及视频详情小窗、UP主页、相关推荐、评论与确认后提交操作。见 [2.0.0 使用与实机检查](NATIVE_200_TEST.md)。新页面和弹幕仍待PS3验证。

> **1.9.2：** 基于实机日志拆分音视频下载，16 MiB 变长视频包缓存扩至 128 帧描述符，加入约 2.5 秒预取与音画联合缓冲，缓存 Aero 静态层减少播放主线程停顿。见 [1.9.2 说明](NATIVE_192_TEST.md)。实际 Wi-Fi 流畅度待复测。

> **1.9.1：** 修复 SPS VUI 字段读取与 1080p 固定帧率误判，按 MP4 实际帧时间限制 30fps；加入重复帧缓存翻转、低开销提示音、批量日志、有限等待退出和原创银白 B/播放 Logo。性能与兼容性仍待实机反馈，见 [1.9.1 说明](NATIVE_191_TEST.md)。

> **1.8.0：** 原生 AVC 输出切换到 YUV420P，由 RSX 转色；新增半透明轻量 Aero 播放控制层和 GPU 淡入淡出。1.7.0 功能一并保留，见 [测试说明](NATIVE_180_TEST.md) 和 [渲染设计](YUV_AERO_DESIGN.md)。新 GPU 路径需要实机验证，1.7.0 包保留作对照。

> **1.7.0：** 加入离线/DNS恢复与账号备份、索引±10秒跳转、退出确认、动态清晰度菜单；扩大有界视频详情容量并改进裁切高度处理，列表左上微移。见 [测试说明](NATIVE_170_TEST.md) 与 [弹幕/1080p评估](DANMAKU_1080_RESEARCH.md)。新路径仍待实机验证，本次只在电脑生成包。

> **1.6.0：** 根据新日志调整画面状态处理与 DTS，接入真实收藏夹，增加搜索键盘兜底、放大的选中缩略图和 180 ms 浏览过渡。见 [测试说明](NATIVE_160_TEST.md) 与 [分析证据](NATIVE_160_FINDINGS.md)。新播放处理、键盘和动画仍待实机确认。

> **2026-11-06 / 1.5.0：** 根据实机日志修正 Level 5.1 解码初始化、系统菜单持续 flip，加入系统搜索键盘、历史游标分页、原生 XMB 十字布局与静态系统壁纸读取。先看 [1.5.0 测试步骤](NATIVE_150_TEST.md) 和 [日志分析](NATIVE_150_FINDINGS.md)。本版仍待实机验证。

> **2026-11-06 / 1.4.0：** 界面方向改为原生 XMB 风格，不继续迁移 Borealis。已实现推荐/热门/历史、扫码登录、原生 AVC/AAC 播放与 720p 选流，当前版本仍待实机验证。请先看 [实机测试步骤](NATIVE_140_TEST.md) 和 [线程、内存与渲染设计](NATIVE_RUNTIME_DESIGN.md)。下面较早的阶段记录仅作历史参考。

# wiliwili PS3 移植工作区

主项目保留 wiliwili 的业务和手柄界面；PS3 媒体后端参考 Yo! Player 的系统 H.264/AAC、
分离音视频流和音频时钟同步设计。上游版本与许可见 `UPSTREAM.json`。

## 当前代码与边界

- 已在 wiliwili 的 `ps3/port` 分支建立移植工作区。
- 已实现独立 C++17 DASH 选流策略：首版筛选 H.264 + AAC-LC，限制 720p/30fps 与带宽，
  排除不支持的编码，并保留后续 SPS 检查要求。未知帧率需要在媒体探测阶段补查。
- `bootstrap/` 是无图形、无 C 运行时初始化、无模块导入的 PS3 启动检查程序，
  显式提供进程参数，以直接系统调用写入日志。
- 开源 PS3DEV 工具链可校验下载和重现构建，部署在 WSL 的 `/opt/wiliwili-ps3`。
- **启动包不是客户端**，不包含 Borealis 界面、B站请求或视频播放。
- **Yo 的完整音视频后端尚未移植**。已新增独立的开放 SDK 基础 vdec 无声短片测试，AAC 音频仍待适配。
- 0.2 的程序入口、三处文件写入、等待和自动退回 XMB 已有实机日志与用户反馈确认。
- 0.3.2 实机日志定位到文件路径构造函数中的 abort；0.3.3 修正分配器锁与文件路径的初始化顺序，实机确认画面、按键、摇杆和退出正常，日志中内存自检通过。
  它使用 CPU 绘制像素、RSX 扫描输出，并非最终 NanoVG 硬件渲染实现。

## 构建

在 WSL、项目根目录运行：

```sh
python3 ports/ps3/tools/bootstrap_toolchain.py
python3 ports/ps3/tools/build_boot_probe.py
python3 ports/ps3/tools/build_platform_probe.py
```

启动包：`ports/ps3/dist/wiliwili-PS3-boot-probe-0.2.pkg`。
U 盘包与 `BOOT_TEST.md` 在同一目录。按 [`BOOT_TEST.md`](BOOT_TEST.md) 操作。

0.1 的实机日志证明入口和文件接口已执行，但退出编号写错为 getpid，随后进入死循环。
该版本已标记失败。0.2 修正退出路径后，已由用户确认正常自动退回 XMB。

**当前上机包：** `dist/wiliwili-PS3-video-test-0.7.1.pkg`，说明见 [`VIDEO_TEST.md`](VIDEO_TEST.md)。
0.4 日志为 `WILI_EARLY.LOG` 与 `WILI_VIDEO.LOG`。启动修复依据见 [`STARTUP_FIX.md`](STARTUP_FIX.md)。当前没有完整客户端安装包。

主机选流模块检查：

```sh
cmake -S ports/ps3 -B ports/ps3/build/host -DPS3_PORT_BUILD_TESTS=ON
cmake --build ports/ps3/build/host
ctest --test-dir ports/ps3/build/host --output-on-failure
```

PPU 交叉编译选流静态库：

```sh
export PS3DEV=/opt/wiliwili-ps3/ps3dev
export PSL1GHT=$PS3DEV
cmake -S ports/ps3 -B ports/ps3/build/ppu -DCMAKE_TOOLCHAIN_FILE=cmake/ps3dev.cmake
cmake --build ports/ps3/build/ppu
```

这只构建平台策略静态库，不是完整 wiliwili 可执行文件。当前不提供假装已完整支持的 `PLATFORM_PS3` 开关。

## 接下来的真实关口

1. 已通过：最小 SELF/PKG、入口、日志与自动退出。
2. 已通过：0.3.3 启动修复、内存自检、显示和手柄输入。Borealis/NanoVG 的 PS3 图形后端尚未实现。
3. 已通过：0.4 基础 vdec 接口解码并显示 80 帧无声短片，正常关闭并释放资源。AAC 接口仍需要兼容封装。
4. 先用本地已知 H.264/AAC 样片验证尺寸、帧缓冲、回调和同步，再接 HTTPS 分离流。
5. 接入 wiliwili API 数据和播放器事件，最后连接完整界面。

## 本次已经核对的接口差异

Yo 使用 `cellVdecQueryAttrEx`、`cellVdecOpenEx` 等扩展接口。当前开源 SDK 的 `codec/vdec.h`
只声明基础的 `vdecQueryAttr`、`vdecOpen` 等接口；`codec/` 中没有可直接复用的 AAC 头文件。
因此不能简单地重命名函数后宣称硬解可用。

页面宽高是显示尺寸，不一定是 H.264 的 coded size。最终传给解码器的尺寸、位深和参考帧数必须来自有效 SPS。
本轮没有启用或复制上游的 SPS 改写策略，也没有向 PS3 发送未经探测的视频流。

## 磁盘与清理边界

项目位于 F 盘。WSL Ubuntu 虚拟磁盘已确认位于 `F:\WSL\Ubuntu\ext4.vhdx`。
工具链、下载包和可能的后续工具依赖集中在 `/opt/wiliwili-ps3`，方便将来单独清理。
本次安装使用官方工具链发布包，SHA-256 已核对；没有取得或安装 Cell SDK。

原生视频诊断包（自动生成合成片并执行主机解析测试）：

```sh
python3 ports/ps3/tools/build_platform_probe.py --video
```

0.4 已由实机和日志确认通过，只支持随包的 WILI_TEST.H264，不是通用 MP4 播放器。
日志显示渲染循环约 8 fps，80 帧约 11 秒取完；尚未达到目标 10 fps，不能据此推断解码器独立吞吐。

0.5 新增 848x480/30fps/180 帧测试包：RSX 主存到显存缩放，缓存静态界面，取消 100ms 循环休眠，记录平均取帧速率。
0.5 尚未实机验证，音频仍未接入；对应短片文件为 WILI_480.H264。

0.5 日志确认 180 帧/约 29.99fps，但后续冻结，退出不通过。0.5.1 减少空闲 GPU 提交、去掉多余 GPU wait-flip、保护命令缓冲余量并补齐退出诊断，等待实测。

0.5.1 实机确认默认命令段只有 7167 字，触发保护并提前结束，日志到达退出请求。
0.5.2 新增自管命令缓冲和有界 GET/PUT 回收，强制在短片中多次回收，尚待实机验证。

0.5.2 实机通过：848x480、180 帧，平均取帧 29.998fps，命令缓冲成功回收 3 次，按键退出及各项关闭返回正常。
本次日志总运行约 15 秒，未覆盖 35 秒空闲；不据此宣称长期稳定性。下一关为 AAC 与音频输出/同步。

0.6 新增原生AAC ADTS解码、48kHz双声道音频端口、独立PPU输出线程和音频消费时钟。
当前先预解码固定6秒音频，尚非长视频流式后端；需要 WILI_AV.H264 与 WILI_AV.AAC 两份新素材。
编译/参考音频/导入表检查通过不代表PS3发声已验证，0.6 等待实机测试。

0.6 实机在 ADEC_QUERY_BEGIN 后无返回，音频未通过。0.6.1 增加运行时绑定校验、双候选导出库和 ADEC 基础模块加载，先关闭 AAC 再打开视频解码器，等待实机验证。

0.6.1 实机音视频通过：cellAdec 8项解析，libadec 8项均为0；AAC 283 AU、288000样本、视频180帧完成，运行至约43秒后清理全返回0并发出退出请求。暂停/恢复和精确音画偏移尚未验证；固定素材预解码不能代表长视频流式支持。

0.7 新增本地单轨分离 fMP4：逐 moof 读取索引，AVCC 转 Annex B、AAC 转 ADTS；视频8×256KB包池、音频8×8KB包池、256KB PCM环。
不再按文件大小或整段音频时长分配媒体缓冲，暂停保留未消费数据，缺数据时重新缓冲并冻结媒体时钟。
新测试为 WILI_VIDEO.MP4 / WILI_AUDIO.MP4，30秒/900帧。主机解封装、变异、参考解码、PCM环测试通过，原生并行流式播放等待实机。

0.7 实机已通过 MP4 初始化、AAC 预缓冲及两个解码器同时打开，但第一张视频帧被校验拒绝。0.7.1 规范 AUD 首位、做输入缓存写回并补齐图像元数据日志；根因仍需实机确认。

0.7.1 实机部分通过：至少540帧、音频消费约18.43秒、PCM保持≤32768样本、0次重新缓冲，4次暂停/3次恢复并在暂停中正常清理退出。用户提前退出，尚未验证900帧/30秒自然EOF。

用户明确确认本地播放正常并要求推进，后续不再重复本地900帧验证关口。
0.8 新增HTTP/HTTPS Range媒体源，每轨256KB缓存、严格206/Content-Range/ETag校验、证书与主机名验证、请求超时和取消；局域网只读素材服务已准备。
构建命令 python3 ports/ps3/tools/build_platform_probe.py --network；当前网络包 dist/wiliwili-PS3-network-test-0.8.pkg，配置 WILI_NET.INI，说明 NETWORK_TEST.md。
主机完整HTTP读取/错误情形通过，PS3联网和原生HTTPS仍待实机；尚无B站API。

0.8 局域网HTTP实机通过：PS3 192.0.2.215 的20次视频Range及1次音频Range与电脑服务端日志吻合，900帧、1440000音频样本完整播放，约29.82fps，网络释放/音视频关闭/退出正常。原生HTTPS和B站API是下一阶段。

0.9 新增公网HTTPS/B站API结果页：匿名nav/WBI、视频信息、签名playurl、AVC/AAC元数据筛选、两个CDN的4KB HTTPS Range探测。
电脑端同一C请求/解析代码实测成功；默认BV17x411w7KC当前匿名返回512x288/30216，目标qn32按实际返回显示。PS3原生TLS/API尚待实测。
命令 python3 ports/ps3/tools/build_platform_probe.py --bili，安装包 dist/wiliwili-PS3-api-test-0.9.pkg，配置 WILI_BILI.INI，日志 WILI_API.LOG，说明 API_TEST.md。本模式不运行媒体解码器。

交接当前结论（2026-11-05）：0.9 的 PS3 原生 HTTPS、WBI、VIEW、PLAYURL 和两条 CDN 4KB 探测均经用户和 WILI_API.LOG 确认通过，详见 evidence/api-0.9-hardware.json。真实 B站媒体仍未送入原生播放器。
用户要求最终复用原版 wiliwili 界面/业务，PS3 专用播放底层；Borealis/RSX 图形平台尚未完成。用户明确要求不提交 Git，交接期间没有暂存、提交或推送。

## 1.0 真实 B站播放（本轮新增，实机待测）

先按实测格式补齐适配，再把两条已通的路接起来。真实媒体参数逐项记录在
[`evidence/REAL_MEDIA_1.0.md`](evidence/REAL_MEDIA_1.0.md) 与
`evidence/real-media-BV17x411w7KC.json`；**不是普通 stbl MP4**，而是单文件 DASH
分片 MP4（`moov/mvex/trex/elst` + `sidx` + 40 组 `moof/trun/mdat`）：

- 视频 `avc1.64001E` High@L3.0、512×288、8-bit 4:2:0、6 参考帧、SAR 1105:1104、
  `mdhd` 16000、B 帧 cto（最大 10672 ticks ≈ 10 帧）、`elst` 起始 3200 ticks + 67ms 空编辑。
- 音频 ASC `1210` → AAC-LC **44100Hz** 立体声；PS3 端口是 48kHz，需精确重采样。
- 匿名权限实际返回 qn=16 / 512×288；480p 仍是目标，按实际权限显示。
- `view.data.duration=2412` 是多 P 字段，播放时长由 sidx 实测 **199.33s** 驱动。

新增/改动：

| 文件 | 作用 |
|---|---|
| `tools/probe_real_media.c`、`tools/analyze_real_media.py` | 复用 0.9 的 HTTPS/WBI 代码取新鲜 URL，有界 Range 读取真实初始化/索引，输出参数记录 |
| `include/avc_sps.h`、`src/avc_sps.c` | 通用 SPS 解析（Main/High、位深/色度、裁剪、VUI/SAR、参考帧、reorder），有界位读取 |
| `include/mp4_dash.h`、`src/mp4_dash.c` | 真实 DASH fMP4：sidx/moof/trun 逐样本游标（不再受 128 样本上限）、avcC High 扩展、NAL 长度、AUD/SPS/PPS 注入、B 帧 PTS、elst、ASC→ADTS |
| `include/resampler.h`、`src/resampler.c` | 44100→48000 精确有理数线性重采样，单样本 carry，无累计漂移 |
| `src/bili_session.c/h`、`include/bili_play.h` | 运行时会话：API→CDN 总长→MP4 配置，412/403 记录后停止 |
| `platform/native_bili_player.c` | 1.0 原生播放后端：动态 coded 尺寸/level、16 帧重排队列、输入包池、PCM 环、音频消费时钟、暂停/退出 |
| `platform/platform_probe.c`、`platform/probe_identity.h` | 1.0 页面与启动流程（日志 `WILI_PLAY.LOG`） |
| `tools/build_platform_probe.py --bili-play` | 新构建模式，含主机回归、静态 ABI、SELF/PKG 校验 |
| `tests/mp4_dash_tests.c`、`tools/make_dash_fixtures.py` | 用去敏真实元数据生成单分片回归样本（不含媒体字节/签名 URL） |

主机侧已验证：`tests/mp4_dash_tests.c`（ASan/UBSan）覆盖 High SPS、avcC 扩展、
sidx/trun 75/215 样本、B 帧 cto、ASC 44100→ADTS、重采样；
`tools/verify_real_stream.c` 对**新鲜完整流**全量解析通过：
视频 40 分片 / 2990 样本（VCL=2990、AUD 首位=2990、注入 SPS=40、B 帧样本=2631，
DTS 末端 3189328 == sidx 时长）、音频 40 分片 / 8584 AU（ADTS 全 44100 立体声，
DTS 末端 8789987 == sidx 时长），见 `evidence/real-media-full-verify.json`。

构建：`python3 ports/ps3/tools/build_platform_probe.py --bili-play`
产物：`dist/wiliwili-PS3-bili-play-test-1.0.pkg` 与同名 `-USB.zip`（含 `WILI_BILI.INI`），
说明见 [`BILI_PLAY_TEST.md`](BILI_PLAY_TEST.md)。

**证据分级**：以上为主机测试 + 静态 ABI/包结构校验；`evidence/bili-play-1.0-static.json`
明确 `ps3_hardware_tested=false`、`borealis_ported=false`。播放器接口化与 RSX/NanoVG 后端路线见
[`PLAYER_INTERFACE_AND_UI_PLAN.md`](PLAYER_INTERFACE_AND_UI_PLAN.md)（仅为计划，未实现）。

## 1.0 实机结果与 1.0.1 修复

用户回传的 `G:\WILI_PLAY.LOG` / `WILI_EARLY.LOG` 已原样保存到
`evidence/hardware/bili-play-1.0-*.log`，根因记录见
`evidence/hardware/bili-play-1.0-hardware.json`。

**通过的阶段**（实机证明，不是推断）：pre-CRT 构造函数顺序、分配器自检、RSX 1920×1080
扫描输出与 64KB 自管命令区、手柄、网络初始化（curl 7.64.1 / mbedTLS 2.28.10）、
匿名 HTTPS nav（HTTP200 verify0）、view（cid=279786、`page_duration=199`、
`view_duration=2412`）、WBI playurl（qn=16、512×288、`avc1.64001E`）、
首个视频 CDN `HTTPS 206 bytes 0-4095/5644323`（verify0），以及退出清理。

**失败点**：`BILI_SESSION_CDN_FAILED track=Video curl=0 http=206 tls=1 range=1 total=5644323`。
`src/bili_session.c` 的 4KB 探测守卫写成 `!r.range_start`，而 `0-4095` 的
`range_start==0` 是**预期值**，于是把完全合法的响应当成失败；session 在打开
HTTP Range / 解析 MP4 / 打开任何解码器之前就返回 -1。与 TLS、CDN、API、412/403、
固件解码无关（日志已逐项排除）。

**1.0.1 改动**：
- 抽出纯函数 `wili_bili_cdn_probe_ok()`，正确要求 `range_start==0`、
  `206`、`tls_verified`、`has_range`、Content-Range 与 body 长度一致、无 overflow；
  失败日志补齐 start/end/total/bytes/overflow 便于下次定位。
- 新增 `tests/bili_session_tests.c` 主机回归（含 1.0 实机那条响应的精确断言 +
  curl/TLS/status/range/length/overflow 拒绝用例），已接入 `--bili-play` 构建。
- `platform_probe.c` 在 session 打开失败时调用 `wili_bili_session_close()`，
  释放可能已打开的 Range handle。
- 新包名 `wiliwili-PS3-bili-play-test-1.0.1.pkg`（APP_VER `01.01`）、
  新日志 `WILI_PLAY101.LOG`、启动标记 `WILIWILI_PS3_BILI_PLAY_101`，避免误装旧包或串日志。

## 1.0.1 实机：真实播放约 8 秒 → 1.0.2 修复

用户回传的 `WILI_PLAY101.LOG` 已原样保存到
`evidence/hardware/bili-play-1.0.1-*.log`，结论见
`evidence/hardware/bili-play-1.0.1-hardware.json`。

**实机证实（不再是推断）**：两条 CDN 严格 206 + 强 ETag、真实 MP4 配置解析、
cellAdec 8 项导入解析 / QUERY / OPEN / START、VDEC QUERY（内存 25,538,941 / queue 4）/
OPEN / START、AAC 预缓冲、`FIRST_NATIVE_AAC_PCM_RETRIEVED`、
`FIRST_NATIVE_PICTURE_RETRIEVED`、`FIRST_REAL_VIDEO_FRAME_PRESENTED`、
RSX 首次缩放 blit、48000Hz 音频时钟启动、命令区回收 3 次。
稳态 `VIDEO_PROGRESS fed=130 decoded=120 presented=104 audio_samples=333312`：
104 帧 / ~15fps ≈ 6.93s 与音频 333312/48000 ≈ 6.94s **音画同步**；
全程无 `VDEC_PICTURE_REJECTED`，说明 High@L3.0 / 6 参考帧 / B 帧重排 / 16 帧队列
和 44100Hz→48000Hz 重采样都被 4.93 HEN 固件接受。

**冻结原因**：`PCM_RING_WRITE_REJECTED` → `audio_pump()` 返回 -1 →
`wili_video_tick` 把播放器置 `failed` 并暂停音频，画面停在 238 帧。
用日志里的真实计数在主机复现（`build/media-probe/pcm_repro.c`）：
`held=30955` 时容量余量 1813 > n=1115，**容量不是原因**；
单个 NaN 或 >1 / <-1 的浮点样本才会让 `wili_pcm_ring_write` 拒绝整批。
即真实 cellAdec 输出含 NaN 或轻微超界峰值。次要问题：用户退出时
`VDEC_DRAIN_INCOMPLETE`（stop 用 `pull_picture()`，重排槽满时不消费 item）。

**1.0.2 改动**：
- `src/pcm_ring.c` 新增纯函数 `wili_pcm_sanitize()`（NaN→0、超界钳位、记录首个异常位模式），
  `native_bili_player.c` 在写环前调用并打印 `PCM_SANITIZED`；
  新增 `tests/pcm_sanitize_tests.c` 主机回归（含 1.0.1 实机计数）。
- 取 cellAdec PCM item **之前**做容量预检（不足则把 item 留在解码器队列，等端口消费），
  `PCM_RING_WRITE_REJECTED` 现在打印 produced/consumed/held/n/bad/room，并加
  `BILI_PLAY_AUDIO_FATAL` 标记。
- 退出改用 `discard_picture()`（NULL 目标）排空 vdec picture items，修 `VDEC_DRAIN_INCOMPLETE`；
  即使排空不净也继续释放会话与 CPU 缓冲，日志会显示 `BILI_PLAY_RESOURCES_RELEASED`。
- `VIDEO_PROGRESS` 增加已解析分片数与 `max_cto`，便于观察 B 帧与流式进度。
- 新包名 `wiliwili-PS3-bili-play-test-1.0.2.pkg`（APP_VER `01.02`）、
  日志 `WILI_PLAY102.LOG`、启动标记 `WILIWILI_PS3_BILI_PLAY_102`。

## 1.0.2 实机：连播 159 秒 → 1.0.3 消除周期性卡顿；以及 720p 结论

用户回传 `WILI_PLAY102.LOG` 已存
`evidence/hardware/bili-play-1.0.2-*.log`，结论见
`evidence/hardware/bili-play-1.0.2-hardware.json`。

**1.0.2 连播约 159 秒**（用户主动退出）：`fed=2139 decoded=2130 presented=2114`、
29/40 分片、6101 个 AAC AU、20 次 Range、69 次 RSX 命令区回收；
`PCM_SANITIZED bad=2 first_bits=3f803875` 证实 cellAdec 浮点样本 ≈1.0039 越界并被钳位；
清理完整（`ADEC_CLOSE`/`VDEC_CLOSE`/`AUDIO_QUIT`/`BILI_SESSION_CLOSED`/
`BILI_PLAY_RESOURCES_RELEASED`/`DISPLAY_DRAIN` 全 rc=0），1.0.1 的冻结与
`VDEC_DRAIN_INCOMPLETE` 都不再出现。

**周期性卡顿根因**：`rebuffers=20`，与 20 次 `HTTP_RANGE_OK offset=N*262144` 一一对应。
视频轨每跨一个 256KB 媒体缓存块就在主线程同步发一次 HTTPS 206（约 0.5s），
`audio_pump` 一并被阻塞；音频端口把只有 32768 样本（0.68s）的 PCM 环抽空，
`write_block` 失败 → `close_port` + rebuffer → 音频时钟冻结 → 画面卡一下。
日志模式：`HTTP_RANGE_OK` 后 `held` 从 ~31000 掉到 ~8000。

**1.0.3**：`PCM_RING` 32768→**131072**（2.73s）、`PREFILL` 16384→**65536**（1.37s），
足以吸收一次同步 Range 停顿；端口重开等待改为 `REOPEN_MIN=16384`；
`AUDIO_STREAM_BUFFER` 增加 `capacity/room`，`BILI_PLAY_CONFIG` 增加 `pcm_ring/prefill`。
新包名 `wiliwili-PS3-bili-play-test-1.0.3.pkg`（APP_VER `01.03`）、
日志 `WILI_PLAY103.LOG`、启动标记 `WILIWILI_PS3_BILI_PLAY_103`。验收点是
`rebuffers` 接近 0，而 `HTTP_RANGE_OK` 仍会有约 20 次。

**720p 结论（主机匿名实测，`evidence/quality-probe-anonymous.json`）**：
分别请求 `qn=16/32/64/80/112`，B站一律返回 `accept_quality=[16]`、
`returned_qn=16`、512×288，只有 HEVC `hev1.1.6.L120.90` 与 AVC `avc1.64001E` 两条轨。
**改 qn 常量拿不到 720p**，必须用户授权的正式登录凭证；本项目不搜集账号密码、
目前也无登录/凭证存储实现。此外客户端还硬限制 ≤854×480 / ≤30fps、
coded >896×496 拒绝、输出 RSX 映射只有 2MB（720p ARGB 需 3.7MB）、
16 帧重排队列在 720p 需约 59MB，且 720p 的 vdec level/SPU 需求在 4.93 HEN 未验证，
也超出原定「480p / 最高 30fps」目标。是否走登录 + 大改动需用户明确决定。

1.0.3 仍**未上机**；修复只在主机侧与静态检查层面验证，不能用编译通过代替"PS3 可播"。
