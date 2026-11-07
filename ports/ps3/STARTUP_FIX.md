# 平台测试 0.3.3：运行时初始化顺序修正

## 实机证据

`evidence/hardware/platform-0.3.2-early.log` 的最后一段为：

```
CTOR_BEGIN sbrk_init
CTOR_END sbrk_init
CTOR_BEGIN __syscalls_init
CTOR_END __syscalls_init
CTOR_BEGIN __glob_file_init
CRT_ABORT_REQUESTED
```

这确认程序在文件路径构造函数执行期间请求了 abort，没有进入显示初始化。
日志没有完整调用栈；具体的锁错误码尚未测得。

## 链接产物中的依赖错误

当前固定版本 SDK 链接出的 `.ctors` 由 CRT 从后往前执行。原执行顺序是：

```
sbrk_init -> __syscalls_init -> __glob_file_init -> init_metalock
```

反汇编显示 `__glob_file_init` 调用 `strdup`，后者申请内存。
分配器的自动锁创建流程使用全局 metaLock，锁操作失败时调用 abort。
该全局锁由 `init_metalock` 创建，但原表把它排在文件路径初始化之后。
这与实机失败位置吻合；是否完全解决退出故障，需要 0.3.3 实机验证。

修正后的执行顺序是：

```
sbrk_init -> __syscalls_init -> init_metalock -> __glob_file_init
```

保留堆地址和系统调用表的先决初始化，所有构造函数仍各执行一次。
调整仅作用于本项目的 ELF，不修改工具链安装目录，也不修改 PS3 系统。
它是针对当前已核对构造函数表的局部兼容修正，不代表所有 PSL1GHT 版本都有同样问题。

## 构建保护与检查

`tools/build_platform_probe.py` 在 sprxlinker 之后、生成 SELF 之前检查原表的完整内容。
SDK 原构造函数表与预期不同会直接终止构建，避免对其他版本盲目重排。
每项构造函数保留开始/返回标记，abort 也保留标记并继续执行原 abort 行为。
不会通过忽略 abort 或跳过初始化来强行进入 main。

在项目根目录的 WSL 中运行：

```sh
python3 ports/ps3/tools/build_platform_probe.py
python3 ports/ps3/tests/test_ctor_repair.py
```

回归检查使用真实链接产物验证依赖顺序、原函数指针、一次性执行、修改字节范围，
检查最终 ELF 表与校验后的修复一致，并验证不同 SDK 表或重复处理时拒绝写入。
这些检查不会执行 PS3 固件调用，不能替代实机测试。

## 下一轮实测

0.3.3 应出现四项构造函数的完整 BEGIN/END、main 标记、
`ALLOCATOR_CHECK_OK`，随后是 RSX、视频配置和手柄初始化阶段。
内存自检覆盖 malloc 分配、数据读写、memalign 对齐和 free。
显示部分仍沿用此前未完成验证的代码；后续可能继续暴露独立显示问题。

安装与日志位置见 `PLATFORM_TEST.md`。
