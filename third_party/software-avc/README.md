# Software AVC dependency

FFmpeg 7.1.1, LGPL-2.1-or-later. Official source: https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz

SHA-256: 733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1

Run `python3 tools/bootstrap_software_avc.py` in WSL to build a static PPU/Cell library. Only H.264 decoder/parser and avutil are enabled. No protocols, encoders, programs, or external hardware libraries are enabled. AltiVec is enabled; VSX/POWER8 are disabled. The script also supports `--host` for checks using the same wrapper and decoder version.

The script adds `aom_film_grain.o` to the H264_SEI object list to satisfy the shared SEI cleanup reference in this minimal FFmpeg build. It supplies the C11 static_assert macro missing in the older newlib headers. Both adjustments are explicit in the reproducible script; no proprietary SDK sources are needed.

The application uses this decoder on a dedicated PPU worker when the extended firmware query/open path rejects portrait AVC. Source dimensions and references are preserved. PS3 runtime performance still requires hardware measurement.
