# wiliwili PS3 启动链检查（不是 B 站客户端）

**使用 0.2 版。0.1 已确认退出编号错误，会返回进程 ID 后进入死循环，请勿再运行。**
0.2 修正了该错误，并加了低层退出的备选路径；实际能否正常退回 XMB 仍需实机验证。

当前的包只检查自制程序能否进入入口、调用系统文件接口并正常退出。
没有图形、联网或视频播放。正常现象是黑屏约两秒，再回到 XMB，并生成 `WILI_BOOT.LOG`。
这与旧 DS4 诊断包没有日志的失败现象不同：**以日志为准，不以“退回桌面”为成功。**

## 为什么先做它

新项目需要先验证真实 PS3 4.93 HEN 的启动链。
检查旧诊断程序发现它没有声明 `SYS_PROCESS_PARAM`，但尚不能确认那就是唯一故障原因。
此检查程序显式提供进程参数，并移除了图形、手柄库、C 运行时初始化和动态模块导入，
使用直接系统文件调用尽早留痕。是否成功仍需实机验证。

## 测试

1. 将 `wiliwili-PS3-boot-probe-0.2.pkg` 放在 FAT32 U 盘根目录。
2. PS3 启用 HEN，从 Package Manager 安装，覆盖旧版。标题 ID 为 `WILIPRB01`，XMB 信息里的版本应为 `00.20`。
3. 保持 U 盘插在 PS3 上，运行 **wiliwili PS3 Boot Probe**。
4. 预期黑屏约两秒后返回 XMB。这一版不会显示界面。
5. 把 U 盘插回电脑，查看根目录的 `WILI_BOOT.LOG`。

新记录应以 `WILIWILI_PS3_BOOT_02 ENTRY_REACHED` 开头；USB 日志应含
`CLOSE_HDD_LOGS_BEGIN`、两个关闭返回值和 `EXIT_REQUESTED process_syscall=3 fallback=22`。
HDD 日志因为先关闭，末尾可能停在 `CLOSE_HDD_LOGS_BEGIN`。
**日志写到末尾仍不等于退出成功，必须同时确认主机自动返回 XMB。**
记录只含启动阶段和系统返回值，不含账户、Cookie 或设备地址。

若 U 盘没有文件，用 IRISMAN 看以下两个后备位置：

```
/dev_hdd0/tmp/WILI_BOOT.LOG
/dev_hdd0/game/WILIPRB01/USRDIR/WILI_BOOT.LOG
```

三处都没有记录时，请说明是否有错误码、黑屏时长、HEN 版本和当时是否启用 HEN。
不要据此判断手柄或解码功能异常；本程序尚未调用它们。
日志追加到已有文件，单个日志超过 512 KiB 时跳过该位置。

这轮跑通后才进入 RSX 界面、联网和媒体解码实机验证，不把启动成功当作完整移植完成。
