# Native YUV and Aero renderer

## Ownership

The media worker still owns the MP4 readers and Range handles. CellVdec output is now planar YUV420. Each output slot records its actual reported dimensions and an aligned plane layout. The current frame remains pinned until a newer frame replaces it after the prior GPU fence. Teardown closes the codec and unmaps the entire arena before freeing it; a GPU timeout retains buffers for process teardown.

Plane bases are aligned to 128 bytes. The decoder supplies tightly packed planes; when padding is required, V moves first, then U, so overlapping moves cannot destroy unread chroma. Most ordinary macroblock-aligned frames require no move. The pool uses 128-byte slot strides and one 1 MiB-aligned mapping, rather than rounding every slot to 1 MiB.

The native renderer binds three B8 textures in mapped main memory and draws a single quad. The dimensions and crop determine texture coordinates; explicit SAR values determine output aspect ratio. Extremely implausible aspect ratios fall back to square pixels. The original ARGB CPU-frame accessor returns no frame in this backend; consumers use `wili_video_surface`.

## Shader build

`cgcomp -a` bypasses the missing proprietary Cg frontend. `shaders/quad.vp` and `shaders/overlay.fp` are our own RSX assembly; the four YUV fragments are generated from standard Kr/Kb matrices in `build_rsx_shaders.py`.

The assembler can silently omit unsupported texture-unit syntax, so successful exit alone is insufficient. The build validates exact code bounds and counts, vertex input mask, and the presence/order of sampler instructions 0/1/2. Runtime also validates the embedded program headers and code bounds before loading GPU memory. No downloaded shader binary or Cg runtime is shipped in the PKG.

Matrices cover BT.601/BT.709 and limited/full range. Chroma center is 128/255, limited luma starts at 16/255. When the source leaves the matrix unspecified, HD defaults to BT.709 and SD to BT.601. HDR transfer/gamut conversion is not implemented by these shaders.

## Aero layer

The compositor produces a transparent, premultiplied ARGB layer. Font coverage and panel pixels obey RGB <= alpha. RSX uses ONE / ONE_MINUS_SRC_ALPHA for the layer; vertex color multiplies all four components to fade the whole layer without rebuilding it on every opacity step.

Rounded translucent panels, cached highlights and small shadows create the Aero appearance. No live blur or refraction is involved. The browsing compositor keeps its previous opaque RGB behavior; alpha-target state is explicitly scoped to the player and its dialog.

The native RSX renderer resets render target, viewport, masks, depth/stencil/culling/alpha state, vertex and fragment programs and texture state for each draw. A valid VRAM depth surface is allocated even though depth testing/writing is disabled. All video and overlay draws precede the existing backend fence and flip protocol. The command arena reserves 4096 words before a frame and uses the existing bounded recycling procedure.

## Verification and remaining work

`yuv_frame_tests.c` checks crop-sized and padded frame layouts, overlapping U/V repacking, bounds and pool sizes. `check_yuv.py` compares matrix output with FFmpeg. The ordinary compositor preview now blends the actual premultiplied layer over a synthetic video frame and verifies transparent regions.

Host checks do not execute RSX. Hardware must verify the B8 texture interpretation, main-memory sampling, actual CellVdec plane layout, matrix/range behavior, crop, alpha blending, system-menu interoperation and frame-time cost. The 1.7.0 package and source snapshot remain available as the prior renderer baseline.

## 2.0 detail and danmaku composition

The same native YUV draw now fits either the full screen, an 800x450 detail viewport, or a 320x180 creator-page preview. The CPU detail layer leaves that rectangle transparent. Eight premultiplied text strips occupy a bounded 2 MiB mapped atlas; new comments are rasterized once and clipped RSX quads move with the media clock before the detail/OSD overlay. Feed responses are session-cached; static detail text is cached, image arrivals refresh only thumbnail regions, and prefix measurement is linear in text length. These additions require PS3 validation.

## 2.1 clear XMB pages and pinned video

The shared wallpaper is mapped separately and drawn with blending disabled before the video. The clear detail layer has no gray panel fill. Both detail and creator views retain the 800x450 viewport at (32,48). A cached 1280x720 CPU content canvas supplies only the visible 800x172 metadata/comment strip beneath it; 180ms interruptible scrolling never changes video or right-column geometry. Tests compare those fixed regions pixel-for-pixel before and after scrolling. Background memory is flushed after wallpaper updates and unmapped after the last fenced draw on exit.
