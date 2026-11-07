# 1.0.0-r3 本地测试

- 首次弹幕默认 6/6，手动选择 0–6 后由工作线程保存到机器本地 /dev_hdd0/tmp/bibilabu_settings.cfg（有 .bak 备份）。换视频、重启应用、退出登录后保持设置。
- r2 日志确认内存申请/释放已成功，720×1280 在基本解码接口中首帧返回 80610106 与异常 64×64 图像。横屏同样七参考帧可以出帧，480×852 也可以出帧。
- r3 为编码高度超过 1088 的竖屏启用 QueryAttrEx/OpenEx，明确设置真实尺寸与所需 DPB 缓冲数量。查询/打开失败时记录错误，不减少参考帧数量强行播放。
- 固件实际是否支持这一配置尚待验证。主机测试及导入表校验不能代替 PS3 验证。

测试：先播放横屏，再试 BV1DKew61E2p 的 720p。若仍失败请保留 BIBI_PLAY.LOG，关注 VDEC_EX_BINDING、VDEC_EX_CONFIG、VDEC_EX_QUERY、VDEC_EX_OPEN 和首帧信息。

弹幕：第一次为6；手动设为3，切视频、退出重开应仍为3；再设为0重开应保持关闭。DANMAKU_SETTINGS_SAVE result=saved 表示已落盘。

仅本地测试包，不推送 GitHub，不写 U 盘。
