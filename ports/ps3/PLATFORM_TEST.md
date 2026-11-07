# 0.3.3 启动分层与显示输入验证

0.2 的入口、直接文件 I/O 和自动退回 XMB 已由日志与用户反馈确认通过。
0.3 实机反馈：没有画面、退回 XMB；检查返回的 U 盘确认没有日志。
仅确认退出表现，尚不清楚是否进入 main，也未读取主机硬盘日志。
0.3.1 实机日志确认入口与第一个构造函数已执行，但没有 main 标记。
0.3.2 实机日志：sbrk_init、__syscalls_init 返回后，__glob_file_init 内请求 abort。
反汇编显示该构造函数经 strdup 申请内存，初始化分配器锁的 init_metalock 却排在它后面。
0.3.3 把 init_metalock 调整到 __syscalls_init 之后、__glob_file_init 之前，每项仍只执行一次；尚未实机验证。
它不是完整 wiliwili，没有联网、视频播放或 Borealis 界面。

## 操作

1. PS3 启用 HEN，安装 `wiliwili-PS3-platform-test-0.3.3.pkg`，覆盖旧测试程序。
   XMB 显示名称变为 **wiliwili PS3 Platform Test 0.3.3**，版本应为 `00.33`。
2. 保持 FAT32 U 盘插着，再运行程序。
3. 预期显示深色测试页和 `wiliwili / PS3` 标题、分辨率、帧计数及手柄状态。
4. 按 **×**应切换蓝色/粉色强调色；拨动摇杆时数值应变化。
5. 按 **○**或 **Start / Options** 返回 XMB。主循环正常运行时也会在约 60 秒自动退出。

## 结果

请确认是否看到画面、颜色/数值是否响应，以及是否顺利退回 XMB。
新版本有两份日志：**`WILI_EARLY.LOG`**（入口、构造函数、main 前后）和
**`WILI_PLATFORM.LOG`**（显示与输入初始化）。保留两份，不要只找旧的 `WILI_BOOT.LOG`。

U 盘根目录没有新日志时，用 IRISMAN 查看：

```
/dev_hdd0/tmp/WILI_EARLY.LOG
/dev_hdd0/tmp/WILI_PLATFORM.LOG
/dev_hdd0/game/WILIPRB01/USRDIR/WILI_EARLY.LOG
/dev_hdd0/game/WILIPRB01/USRDIR/WILI_PLATFORM.LOG
```

日志逐项记录 `MAIN_REACHED`、`RSX_INIT`、`VIDEO_CONFIGURE`、`DISPLAY_READY`、
`PAD_INIT`、`FIRST_FLIP_SUBMITTED` 和清理阶段。
如果黑屏或没有自动退出，日志最后一个阶段可用来定位新增层的故障；不要用重复启动代替定位。

等待翻页的轮询有超时。60 秒退出计时依赖主循环能够继续运行，不覆盖底层图形初始化调用卡住的情形。
这版由 CPU 绘制像素、RSX 扫描输出，用于验证显示通路，不代表最终 NanoVG 硬件渲染已实现。

## 启动诊断边界

`BEFORE_SDK_CRT` 表示新入口已执行；`CRT_CONSTRUCTOR_REACHED` 表示进入构造函数，
不代表全部初始化完成；`MAIN_BEFORE_OPEN_LOGS` 表示进入 main。
只有前者时，应检查 CRT/构造函数；两份日志都没有时，仍需考虑加载器、模块导入、
入口执行或日志路径写入失败，不能直接断言是 GPU 故障。

0.3.3 修复当前 SDK 在此程序中链接出的初始化依赖顺序，保留相同显示代码与逐项日志。
这项修复能否使实机进入 main 和显示画面，仍需测试。

## 0.3.2 新增标记

`CTOR_BEGIN` / `CTOR_END` 分别包围 sbrk_init、__syscalls_init、
__glob_file_init、init_metalock。缺少对应 END 时，可定位未返回的初始化函数。
`CRT_ABORT_REQUESTED` 表示调用了 libc abort；它不提供完整调用栈。
仍沿用 WILI_EARLY.LOG 和 WILI_PLATFORM.LOG，日志追加写入，按 PLATFORM_033 区分本次启动。

## 0.3.3 验证目标

新启动段应依次出现 sbrk_init、__syscalls_init、init_metalock、__glob_file_init 的 BEGIN/END，
然后出现 MAIN_BEFORE_OPEN_LOGS 和显示日志。若仍退出，保留两份日志以确定新的停止点。
该顺序修正只作用于本项目生成的 ELF，不修改系统或已安装的工具链。

进入 main 后增加内存自检，预期显示日志中出现 `ALLOCATOR_CHECK_OK`，再开始 RSX 初始化。
修复依据及离线回归检查见 `STARTUP_FIX.md`。
