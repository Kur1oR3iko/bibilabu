# 1.5.0 evidence and implementation notes

## Hardware evidence from 1.4.0

Raw logs are preserved in `evidence/hardware/native-1.4.0-bibi_play.log` and `native-1.4.0-wili_early.log`.

- The session selected actual AVC 1280×720 and 1280×544 representations, both High profile with `level=51`, four references, stereo AAC 48 kHz. API, TLS, Range and audio initialization succeeded. Both runs failed at `VDEC_QUERY rc=0x80610101`; neither reached VDEC open or picture output.
- Both failed sessions could be stopped through the application: network join, ADEC close, audio quit and session cleanup completed. This narrows the reported PS-menu problem: it occurred later on the browsing screen.
- `SYSUTIL_MENU_OR_DRAW_OPEN` appears twice. ALIVE continued through 100 seconds, while draws remained 49, and no `SYSUTIL_EXIT_REQUEST` was recorded. The main loop expressly skipped drawing whenever `menu_open` was set. This is strong evidence for an overlay presentation stall, not proof that LV2 itself had halted.
- History tab fetch logged 16 items. That does not prove the displayed contents or complete history UX was correct. The replacement uses cursor pagination, explicit business errors and progress metadata.

## Codec handling

The public CellVdec level enum and query validation accept 10..42 (specific AVC level values), not 51. See [RPCS3 CellVdec definitions](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/cellVdec.h) and [query validation](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/Cell/Modules/cellVdec.cpp); these are supporting references, while the actual rejection is confirmed by this console's log.

`wili_avc_ps3_level` retains supported level declarations. For 5.0/5.1/5.2 declarations it only permits the bounded 720p progressive 8-bit 4:2:0 envelope, rejects HRD-bearing streams whose CPB constraints have not been checked, checks DPB macroblocks and available VUI timing, and uses level 4.1. API selection already restricts rate and reported bandwidth. Both avcC-derived injected SPS and any in-band SPS are normalized by the actual AU conversion path. Changes in coded dimensions/profile or an incompatible SPS fail closed.

This is a compatibility policy, not proof of full bitstream conformance or a software decoder. Host tests generate a valid 720p30 High-profile stream, change its declared level to 5.1, normalize it through the production parser, and verify all 90 resulting frames decode with FFmpeg. Hardware still must validate firmware decoding.

## Overlay and keyboard

Menu, system draw and OSK lifetime are independent states. Input is suppressed while a system overlay owns it. Cached frame presentation continues at approximately 60 Hz even when nothing changes; expensive UI composition remains dirty-driven. OSK buffers and its 4 MiB memory container persist until the asynchronous unload event. UTF-8 / UTF-16 conversion handles surrogate pairs and rejects truncated/invalid sequences. Shutdown aborts and drains an active keyboard with a bounded wait.

## Wallpaper

The saved wallpaper path is corroborated by [webMAN MOD resource handling](https://github.com/aldostools/webMAN-MOD/blob/master/include/feat/vsh_random_res.h). This client does not import VSH xsetting exports or hook VSH. It reads the current user name through sysutil, matches an eight-digit home directory's localusername, and checks the static image's mtime/size. The existing image worker performs bounded decode and scaling; main takes the completed bitmap through a locked mailbox. No system file is modified. Dynamic VSH theme rendering is outside this implementation.

## Search verification

A host request without client identifiers returned HTTP 412. A normal official `/x/frontend/finger/spi` initialization followed by search with its `buvid3`/`buvid4` cookies returned code 0 and 16 results. The client follows that initialization once per run; it does not rotate identifiers on rejection. Login credentials remain in memory/config as before and are not included in evidence or the package.
