# 1.6.0 investigation

## Confirmed 1.5.0 observations

Raw console logs are preserved as `evidence/hardware/native-1.5.0-bibi_play.log` and `native-1.5.0-wili_early.log`.

- System wallpaper was read from a matched user directory and `WALLPAPER_APPLIED` was logged.
- PS menu events 0x0121/0x0131 were followed by continued frame submission, EXIT 0x0101, completed cleanup and process exit. Preserve this path.
- VDEC query/open/start succeeded at level 4.1 and required 57,299,581 bytes. AAC PCM and the first video picture were retrieved. The next output picture (userdata 4) had status `0x80610106`, normal attr and expected dimensions, then the application's blanket status check stopped decoding. This reproduced at 720p and 480p.
- System keyboard container creation succeeded, but OSK load returned `0x8002b504` (PARAM). Search did not reach the API.
- Favorites were still an explicit placeholder in the previous source.

## Playback changes and evidence limits

The user-selected failing video's first 72 AUs were fetched through the actual session and MP4 code, with credentials supplied only through stdin to a local diagnostic executable. The normalized AVC prefix decoded successfully under FFmpeg `-xerror`. Its first AU alone contained SPS/PPS/IDR; subsequent AUs had correct non-sync flags and no repeated configuration injection. This ruled out a proposed duplicate-SPS explanation for this sample. Decoding order differed from presentation order, as expected with B-frames.

Reference: [Movian PS3 video backend](https://github.com/andoma/movian/blob/master/src/arch/ps3/ps3_vdec.c), `picture_out`, deliberately does not discard solely because `pi->status` is nonzero. Our change is narrower: only the observed `VDEC_ERROR_PIC` is tolerated, while structural checks and GetPicture success remain mandatory. The analogous callback status is counted; other callback errors remain fatal. This is not a claim that every warned picture is visually correct. Logs retain warning counts; hardware must check concealment, artifacts and continuity.

Decode timestamps now come from MP4 DTS rather than being marked invalid. PTS remains separately mapped by userdata for presentation order. No stream transcode, firmware patch or game executable modification is introduced.

## Search

The setup now follows the [PSL1GHT working OSK sample](https://github.com/ps3dev/PSL1GHT/blob/master/samples/sys/osk/source/main.cpp): full-key panel, centered layout, system-default language. Shared input buffers/structures are aligned. There is a bounded alphabet-only panel fallback on PARAM, then an application keyboard so ordinary text search remains accessible even if system OSK still fails. The exact rejected parameter in the old optional setup is not established by the single error code.

## Favorites and motion

Favorites use `x/v3/fav/folder/created/list-all` with the authenticated UID, then `x/v3/fav/resource/list` with the chosen 64-bit media_id. Folder entries are distinguished from playable videos; page/refresh/back requests carry the folder context through the existing generation-checked mailbox. Host live checks confirmed both endpoints without saving account credentials or titles in evidence.

UI motion is an interrupted cubic ease-out over 180 ms. The selected thumbnail grows to 136 px width, category indices wrap across six categories, and selection position resets for a changed dataset/page/folder. The compositor runs during transitions, returns to cached presentation afterward, and does not animate the playback view. Offline preview is from the same compositor, not a separate mockup; it is not a PS3 performance benchmark.
