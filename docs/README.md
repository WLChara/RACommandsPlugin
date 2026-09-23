# RACommandsPlugin

这是一个独立的 Win32 C++ 注入式 DLL 项目，逐步封装《红色警戒 2：尤里的复仇》的原生命令接入。当前版本仅面向特定的 Mental Omega `gamemd.exe` 样本：注入后验证 EXE SHA-256、解析命令符号，在游戏主帧注册「自动装车」与「自动为线圈充电」原生命令，并通过直接 `ClickedMission` 尝试执行任务。真实游戏注入和联机行为尚未验收。

## 目录

```text
RACommandsPlugin/
├── RACommandsPlugin.sln
├── RACommandsPlugin.vcxproj
├── RACommandsPlugin.Tests.vcxproj
├── build/                 编译产物与中间文件
├── docs/                  项目说明
├── scripts/               构建脚本
├── src/                   项目源码与按版本保存的 SDK
│   ├── Bootstrap/         注入后初始化与生命周期
│   ├── Commands/          原生命令基类及各命令的专属实现
│   │   ├── AutoLoadCommand/   自动装车命令及其专属工具
│   │   └── TeslaChargeCommand/ 线圈充能命令及其专属工具
│   ├── ClickedMission/    共用的游戏任务调度链路
│   ├── Game/              游戏对象访问与目标版本 Sig 定义
│   ├── Hooks/             游戏 Hook 安装与转发
│   ├── Memory/            通用进程内存访问
│   ├── Signature/         通用 Sig 扫描与解析
│   ├── SDK/               按目标版本保存的外部 YRpp 快照
│   └── Tests/             纯逻辑测试
└── third_party/           libhat、MinHook、nlohmann
```

## 构建

在 PowerShell 中运行：

```powershell
& .\scripts\build.ps1
```

当前唯一解决方案配置为 `Release|Win32`，目标为 `build\Win32\Release\RACommandsPlugin.dll`。脚本同时编译、运行纯逻辑测试。默认使用 `src/SDK/YRpp/mo-7cd005d2`；可通过 `build.ps1 -GameSdkVersion <版本目录名>` 在编译期切换 SDK。明确的游戏适配器与原生命令注册层使用 YRpp 类布局和固定地址；启动时的目标 EXE 哈希门禁阻止在其他版本执行。

## 当前运行方式

使用常规 `LoadLibrary` 注入后，DLL 会自行安排初始化，调用方不需要调用 `RACommandsPlugin_Initialize`。初始化在 `DllMain` 返回后验证 EXE 并解析 AOB，然后安装主帧 Hook；原生命令在游戏命令表就绪的主帧注册。旧的 `RACommandsPlugin_Initialize` 导出仍可手动重试；`RACommandsPlugin_IsReady` 只表示基础解析和主帧 Hook 已就绪，不表示命令、热键或联机已经通过运行时验收。

该 DLL 在安装主帧 Hook 前将自身固定到进程退出；`Shutdown` 仅停用回调并清空待发命令，不会移除原生命令、撤销 Hook 或使 `FreeLibrary` 成为安全操作。与同时 Hook `MainFrame` 的 YRHackMod 版本不支持共存，遇到已改写的入口会拒绝安装。使用前请在目标游戏环境自行验收，尤其不要将尚未验证的联机行为视为安全。

「自动为线圈充电」默认关闭；按下热键切换开关。开启时，每座本地玩家的磁暴线圈优先保持现有有效配对，再从 32 格内选择最近的空闲 SHK／SHOCK 充能兵；每个充能兵只分配给一座线圈。已分配单位被选中时会定期取消选择。关闭时撤销尚未送出的充能任务，不会强制中止已经进入游戏队列的任务。
