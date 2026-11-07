# 0.7.1 分片 MP4 流式播放验证

0.6.1 已实机通过固定六秒音视频，但音频会整段预解码。
0.7 改为逐分片读取 MP4、有限输入包池和 PCM 环形缓冲，尚未实机验证。

## 安装与操作

U 盘根目录保留以下三个新文件：

```
wiliwili-PS3-video-test-0.7.1.pkg
WILI_VIDEO.MP4
WILI_AUDIO.MP4
```

启用 HEN，覆盖安装，名称 **wiliwili PS3 Stream Test 0.7.1**，版本 **00.71**。
旧的 WILI_AV.H264 / WILI_AV.AAC 不用于本版。

本次素材 **30 秒，848x480/30fps，共900帧**。每秒白色闪烁伴随短音。
请在中途按 × 暂停约2秒，再按 × 继续；观察音视频能否一起恢复。
预期最终 900/900、Audio 30.00s DONE，再按 ○/Options 退出。
主循环运行时启动约90秒自动退出。

## 改变

- 支持测试所用的单轨分片 MP4：moov/avcC/esds 初始化配置，moof/tfhd/tfdt/trun 样本与时间戳。
- 视频样本转 Annex B，在 IDR 的 AUD 后补 SPS/PPS；AAC 样本转 ADTS。
- 每个读取器只保留一个分片的最多128项样本索引，元数据盒上限64KB。
- 视频输入池固定8×256KB，AAC输入池8×8KB，回调 AUDONE 后才复用相应输入存储。
- PCM 固定32768个双声道样本帧（256KB，约0.68秒），不再按整段时长分配和预解码。
- 启动预缓冲至少16384个样本；播放中解码与输出交替，环满时停止继续取PCM/送入压缩样本。
- 输出仍由独立线程服务音频端口，视频由已消费的音频样本驱动。
- PCM 直到硬件消费后才回收，暂停关闭端口时保留未消费数据，恢复后从消费位置重新排队。
- 若数据不足，输出进入重新缓冲并冻结媒体时钟，不把填入的静音误计为已播放内容。

应用输入/PCM缓冲大小固定；固件解码工作内存仍按查询结果分配，显示缓冲仍在显存。
启动时会按64KB块扫描已知测试文件并核对CRC，不会整文件载入内存；这一完整校验步骤不是未来未知长度网络流的策略。
AAC 与视频解码器现在同时保持打开，这是本次新增的实机验证点。

## 当前边界

只接受随包的两个固定文件，不支持替换自己的MP4。
本次为 DASH 风格的分离单轨 fMP4，不支持普通 stbl MP4、多轨交错、加密、B帧、动态参数变更、网络或完整B站界面。
AAC编码延迟裁剪1024样本仍仅对应这份合成素材；任意文件的编辑列表/priming尚未实现。
这一包是验证有界内存的数据通路，不代表长视频、任意编码或联网已全面支持。

## 日志

保留 WILI_EARLY.LOG、WILI_VIDEO.LOG，测试后将U盘插回电脑。
新启动段以 WILIWILI_PS3_STREAM_071 区分。

关键阶段：

```
MP4_SOURCE_READY
VIDEO_STREAM_READY
AAC_FMP4_SOURCE_READY
AAC_STREAM_PREFILL_READY
VDEC_OPEN
AUDIO_STREAM_BUFFER ... held=... capacity=32768 fragments=... rebuffers=...
AAC_STREAM_DECODE_COMPLETE aus=1408 samples=1440000 ...
VIDEO_DECODE_COMPLETE expected=900 actual=900 ...
AUDIO_PLAYBACK_COMPLETE samples=1440000
AUDIO_WORKER_JOIN / AUDIO_QUIT / VDEC_CLOSE
EXIT_REQUESTED
```

重点检查帧数/样本数、缓冲高水位与重新缓冲次数、跨分片连续性、暂停和退出。
视频和AAC解码器的工作区、驱动函数、GPU和音频线程仍需实机结果，主机检查不能替代。

## 构建与离线检查

```sh
python3 ports/ps3/tools/build_platform_probe.py --video
```

生成真实30秒fMP4，运行 ASan/UBSan 下的解封装/时间戳/变异测试及 PCM 环保留/环绕/暂停回退检查，
对转换出的900帧Annex B和AAC做参考解码，并验证30组短音/静音窗口。
还保留构造函数、导入表/回调、退出调用、安装包解包校验。

## 0.7.1 第一帧修复与定位

0.7 日志显示 MP4 读取、AAC 预缓冲、两个解码器打开均成功，但第一张视频图像被检查拒绝。
当时没有记录具体图像字段，所以不能确定是状态、尺寸还是序号导致。
检查首个转换包发现顺序为 SPS/PPS/SEI/AUD/IDR；0.7.1 改为 AUD/SPS/PPS/SEI/IDR，
并为没有 AUD 的样本生成 AUD，保留单访问单元/参数集检查。
同时在送入视频和音频解码器前对修改过的输入包做缓存写回。

主机测试逐包验证 AUD 位于首部，并重新参考解码全部900帧；这不代表原生固件已经验证。
新增 VDEC_INPUT_AU 和 VDEC_PICTURE_DETAIL，记录实际图像 status、attr、宽高、userdata 和期望序号。
若仍报错，请返回两份日志，错误页仍可按 ○/Options 退出。素材 MP4 文件没有改变。
