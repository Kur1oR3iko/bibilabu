# Native PS3 client 1.4.0

## Scope

The user chose a new native interface inspired by PS3 XMB, replacing the goal of porting wiliwili/Borealis views. The upstream application remains available in the repository, but the PS3 executable uses `native_ui.c`, a standalone C compositor and the native media backend. The TrueType rasterizer is vendored under `third_party/stb`; the PS3 UI no longer includes a Borealis header.

The initial target is progressive, 8-bit 4:2:0 H.264/AVC up to 1280×720 at 30 fps with stereo AAC-LC. Quality availability is determined by actual API responses and credentials. There is no unconditional HD entitlement after login. No source-control commit or push is part of this work.

## Findings in 1.3.1

Code inspection identified these defects, not a single proven explanation for every reported hardware freeze:

1. `recycle_command_arena` remained defined but was never called; the custom arena disabled SDK rollover. Continuous flips/transfers eventually exhaust it.
2. The UI repainted every frame, rasterized/blended text directly in VRAM and used fixed 1920×1080 positions even on other output sizes.
3. `open_log` refused files larger than 1 MiB, without rotation. Every log line synchronously fsynced up to three files, from different threads. The inspected USB log was only 7,927 bytes, so the size cap alone does **not** establish why that particular latest launch stopped logging.
4. Image cache lookup did not reserve in-flight URLs. Failures could remain uncached; URL buffers were shorter than feed URLs. Cache decode had no small-thumbnail dimension guard.
5. The main UI accessed feed results while the feed worker modified them. Requests and results had no generation ownership, allowing stale responses to replace a new tab.
6. The prefetch reader could still wait 15 seconds on the render thread; setup and QR HTTP calls were synchronous there too.
7. Stop could free video work memory despite an undrained/unclosed decoder. Frame pointers were not reset, making repeated stop unsafe. Each replay retained an additional mapped frame allocation.
8. Shutdown destroyed network/CURL state before joining HTTP workers.
9. Stream selection still excluded resolutions above 480p even though the requested qn defaulted to 64. The UI advertised 1080p despite a backend maximum of 720p.

## Ownership and scheduling

| Component | Owner / priority (lower number runs first) | Rule |
|---|---|---|
| Input, sysutil callbacks, GPU commands, decoder pump | Main PPU / 1001 | Never does media HTTP; ticks at ~1 ms during playback, ~8 ms in menus |
| Audio output | PPU / 450 | Retains PCM until hardware consumption; 48 kHz, 256 samples/block |
| VDEC firmware workers | PPU / 720; SPU priority 200, four SPUs requested | Allocations derive from firmware query; callbacks publish counters only |
| ADEC firmware workers | PPU / 450; SPU priority 250 | Query and validate runtime imports before invocation |
| Download and MP4 packet production | PPU / 1000, 256 KiB stack | Sole owner of the two MP4 parsers and CURL Range handles |
| Metadata/session/QR request | PPU / 1050, 512 KiB stack | One outstanding job; main joins before accepting a result |
| Feed | PPU / 1100, 256 KiB stack | Private job data; generation-checked mailbox, main-thread publication |
| Cover download/decode | PPU / 1150, 256 KiB stack | Reserved cache entries, bounded images; paused during playback |
| Logging | PPU / 1200, 32 KiB stack | Producers only enqueue; one storage writer, bounded queue |

These are explicit priorities, not claims of core affinity or full utilization of all six available SPUs. Firmware decides decoder scheduling. This version does not introduce a custom SPU decoder or assume RSX is a video bitstream decoder.

## Memory and rendering

Main memory and RSX local memory are separate budgets; 256 MiB VRAM cannot be treated as extra general-purpose RAM.

| Main-memory allocation | Bound |
|---|---:|
| Ten 720p ARGB output slots, individually 1 MiB-aligned strides | 40 MiB |
| Ready compressed video packets | 32 × 512 KiB = 16 MiB |
| Video input owned until AUDONE | 8 × 512 KiB = 4 MiB |
| Ready AAC packets + ADEC input | 64 × 8 KiB + 8 × 8 KiB |
| Stereo float PCM ring | 1 MiB |
| UI transfer surface + cached background | 4 MiB + ~3.52 MiB |
| Cover cache | 16 entries, each at most 512×512×4 bytes |
| VDEC work memory | Firmware query, accepted up to 64 MiB |
| ADEC work memory | Firmware query, accepted up to 16 MiB |
| Font, glyphs, metadata, HTTP cache, TLS, stacks | Additional bounded or library-managed allocations; not included above |

The allocation ceiling is not a measured working set. The runtime records fixed player allocations plus separate decoder queries. Allocation failure leads to a recoverable player error rather than silently expanding a pool.

RSX uses two local display buffers (~7 MiB total at 720p, ~16 MiB at 1080p output). The menu is composed in cached main memory only on changes; RSX scales it to the output mode. Video outputs share one mapped arena. A displayed slot remains pinned until a later frame takes its place, after the previous transfer fence has completed. No additional full-frame memcpy is required. Decoder output uses BT.709 when signalled or for unspecified 720p; BT.601 otherwise. Cropped pictures use the coded stride and adjusted source address.

The current render path uses hardware transfer/scale, not a programmable textured-quad renderer. Player OSD is opaque strips. A static cached wave and soft focus highlighting provide the visual language without continuously recomputing an animated background. More elaborate animation is deferred until real frame-time measurements.

## Backpressure and stopping

The packet worker performs blocking Range reads away from the main thread. A bounded SPSC queue hands complete AUs to the decoder pump. Empty queues return “not ready” immediately; full queues pause production. Audio packets get priority when their queue runs low. The audio output clock follows actual hardware consumption; final decoded sample count replaces estimated duration at EOF.

Stop joins the packet and audio workers, drains codec callbacks, closes codecs, unmaps the RSX frame arena, then frees allocations. It is idempotent after success. If any resource remains owned by firmware/worker or a GPU fence times out, do not reuse/reset/free it: exit the process and let LV2 tear it down. All HTTP workers must stop before CURL/network cleanup. Decoder callback OPD32 conversion, repaired CRT constructors, and validated AAC imports remain mandatory.

## Verification

`tools/build_platform_probe.py --bili-play` builds the actual client and runs host checks. `check_native_runtime.py` verifies duplicate thumbnail suppression, negative cache, pause, stale feed result rejection and actual UI rendering under ASan/UBSan. `check_720p.py` generates 90 frames of local synthetic 720p30 High-profile BT.709 H.264, runs the actual MP4/AU conversion and decodes the resulting stream with host FFmpeg. Existing API/TLS, timestamp, resampling, PCM ring, startup/exit and firmware import checks remain enabled.

No host check proves PS3 performance or firmware compatibility. Hardware must verify 720p frame rate, direct mapped frame transfers, natural EOF, repeated replay, memory reclamation and system-menu exit. `NATIVE_140_TEST.md` contains the user-facing test sequence.
