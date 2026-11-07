# 1.0.4 真实 B站播放包（独立网络预取线程实测通过，实机连续稳定播放 130 秒）

> **历史实机结果（原始日志全部保留在 `evidence/hardware/`）**
> - 1.0：CDN 首包守卫 `!r.range_start` 误判（已修）。
> - 1.0.1：真实播放约 8 秒后 `PCM_RING_WRITE_REJECTED` 冻结（已修）。
> - 1.0.2：连续播放约 159 秒（用户主动退出），2139 AU 送入 / 2114 帧呈现、
>   29/40 分片、20 次 Range，`PCM_SANITIZED bad=2` 证实浮点容错生效。但每约 8 秒音频与画面停顿一下（20 次 `rebuffers`）。
> - 1.0.3：实机连播 52 秒（用户主动退出），639 AU 送入 / 630 帧解码 / 614 帧呈现，
>   `rebuffers=0` 全程为 0！证实 2.73s PCM 环彻底解决了音频断流欠载问题。
> - **1.0.4：实机连播 130 秒（用户主动退出）**，1780 AU 送入 / 1770 帧解码 / 1754 帧呈现，
>   **`rebuffers=0` 全程为 0**，**`v_buf` 常态保持 1.8MB~2.0MB 满水饱和**，
>   20 次 HTTPS Range 请求全部在后台工作线程无感完成，主线程零 I/O 阻塞，画面大卡顿彻底消除！退出清理完整（0 泄露）。

这不是最终客户端界面，仍是 PPU 位图诊断页 + RSX 视频缩放。最终外观仍要复用
原版 wiliwili/Borealis/NanoVG。

## 关于 720p（已用主机实测核实，不是推测）

`evidence/quality-probe-anonymous.json`：匿名状态下分别请求
`qn=16 / 32 / 64 / 80 / 112`，B站**一律**返回 `code=0`、`accept_quality=[16]`、
`returned_qn=16`、512×288，可选视频轨只有 HEVC `hev1.1.6.L120.90` 与 AVC `avc1.64001E`。

**所以改 qn 常量拿不到 720p**，必须走用户授权的正式登录流程取得凭证。
本项目约束是不搜集账号密码、不自动更换身份；目前也没有登录/凭证存储实现。
另外客户端当前还有硬限制：`wili_bili_parse_play` 只收 ≤854×480 且 ≤30fps，
`native_bili_player.c` 拒绝 coded >896×496 或输出缓冲 >2MB，
而 1280×720×4 = 3.7MB；16 帧重排队列在 720p 需要约 59MB。
720p 的 vdec level / SPU / 内存需求在 4.93 HEN 上也未验证，且超出原定
「480p / 最高 30fps」目标。**需要用户明确决定是否走登录 + 大改动这条路。**

## 与旧版本的区别

| | 0.9 | 1.0.3 | 1.0.4 |
|---|---|---|---|
| 能力 | API 结果页 | DASH 原生播放，音频不欠载 | 异步后台预取，彻底消除主线程网络卡顿 |
| 网络模式 | 同步 4KB | 主线程同步 256KB Range | 独立 `wili-bili-net` 线程异步预取 |
| 预取缓冲 | 无 | 仅 PCM 环（2.73s） | 视频 2MB（74s）+ 音频 1MB（165s）内存环 |
| 主线程阻塞 | 频繁阻塞 | 每次 Range 阻塞 0.5s~1.5s | 零网络阻塞，全速流畅渲染 |
| 日志 | `WILI_API.LOG` | `WILI_PLAY103.LOG` | `WILI_PLAY104.LOG` |

## 安装与运行

1. 启用 HEN，安装 `wiliwili-PS3-bili-play-test-1.0.4.pkg`（TITLE_ID `WILIPRB01`，覆盖同一测试槽）。
   名称 `wiliwili PS3 Bili Playback Test 1.0.4`，版本 `01.04`，固件需求 04.9300。
   **先删除 U 盘上 `WILI_PLAY*.LOG`**。
2. U 盘保留 `WILI_BILI.INI`，单行 `bvid=BV17x411w7KC`（可改其它公开 12 位 BV 号）。
3. PS3 需能访问公网；**不需要**局域网媒体服务或本地 MP4 素材。
4. `CROSS` 暂停/恢复，`CIRCLE` 或 `OPTIONS` 退出；最长 360 秒自动退出。
5. 交回 `WILI_PLAY104.LOG`、`WILI_EARLY.LOG`。

## 关键日志

```
PREFETCH_WORKER_STARTED v_slots=8 a_slots=4 block=256k
BILI_PLAY_CONFIG ... v_slots=8 a_slots=4
VIDEO_PROGRESS fed=... decoded=... presented=... v_buf=... a_buf=...
CLEANUP_BEGIN ... NET_WORKER_JOIN ... VDEC_CLOSE ... BILI_PLAY_RESOURCES_RELEASED
```

**验收点**：
1. 画面彻底平滑，不再出现每隔 8~10 秒定格卡顿一下。
2. `VIDEO_PROGRESS` 中的 `v_buf` 和 `a_buf` 维持在高水位（数兆字节缓冲）。
3. 退出时 `NET_WORKER_JOIN`、`AUDIO_WORKER_JOIN`、`VDEC_CLOSE` 干净释放。
