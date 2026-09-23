# 版本化游戏 SDK

`YRpp/mo-7cd005d2` 是从 `C:\Users\26962\source\repos\YRHackMod\YRHackMod\YRpp` 复制的未修改快照。来源仓库提交为 `cee2b0c42f9787ad3d123a0eab6006bb917983fb`；目标样本 `gamemd.exe` 的 SHA-256 见 `docs/ida-evidence.md`。快照包含顶层源码文件及 `Blitters`、`FileFormats`、`Helpers`，不包含生成文档、示例或 Doxygen 配置。复制的 238 个文件已逐一核对 SHA-256。

使用 `scripts/build.ps1 -GameSdkVersion mo-7cd005d2` 选择该快照。SDK 版本是**编译期选择**，不是运行时自动切换；构建仍输出到 `build/Win32/Release/RACommandsPlugin.dll`，后一次构建会覆盖前一次产物。

YRpp 含有固定游戏地址与 ABI 假设。明确承担游戏 ABI 职责的适配器与 `Commands/NativeCommandRegistry` 可引用本快照；纯规划器、ClickedMission 队列、Signature 和 Memory 不引用它。当前只有 Win32 编译验证，尚未做真实游戏运行时验收。不要直接修改快照；现有 AOB/Memory 解析机制继续保留。将 YRpp 的固定地址逐步改为 AOB 是后续独立工作，本次不执行。

来源目录未提供独立的 LICENSE 文件；对外分发 SDK 快照前需核实其授权要求。
