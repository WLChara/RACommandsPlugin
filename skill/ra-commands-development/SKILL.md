---
name: ra-commands-development
description: Develop or review native CommandClass features in RACommandsPlugin, including command registration, game adapters, ClickedMission dispatch, and tests. Do not use for unrelated YRHackMod projects.
---

# RACommandsPlugin 开发

本 Skill 面向在本仓库新增或修改游戏原生命令的开发者与编码代理。先读仓库的 `docs/README.md`，再按任务查看现有实现；不要将本机被 Git 忽略的设计、逆向文档当作公开仓库的必需依赖。

## 项目边界

- 目标是 Win32 x86、MSVC v143 的注入式 DLL。`scripts/build.ps1` 构建 `Release|Win32` 并运行纯逻辑测试。
- `src/Commands/<命令名>/` 拥有命令专属的注册类、状态、规划器、服务、接口和游戏适配器；通用 `NativeCommandRegistry` 留在 `src/Commands/`。
- 纯规划器和服务通过值对象及端口与游戏隔离，不直接引用 YRpp 或 Windows API。游戏对象布局、类型判断和指针解析属于游戏适配器及 `src/Game/`。
- 需要下达 `ClickedMission` 时，复用 `src/ClickedMission/` 的调度队列。新增 Mission 必须在游戏适配器中核对参数形状、对象身份、当前归属和即时条件；不要让功能层直接操作原生 OutList。
- `src/Bootstrap/PluginRuntime.cpp` 负责注入后的初始化、游戏线程上的命令注册、热键重读与生命周期。不要给每条命令各装一套主帧 Hook。
- `src/Signature/` 放通用扫描与解码；具体游戏样本的 Sig 属于 `src/Game/TargetProfiles/`。`src/SDK/` 和 `third_party/` 是外部快照，不因普通功能改动而重写。

## 不可省略的兼容约束

- 现有原生命令的内部名是热键持久化键；除非任务明确要求迁移，不要改 `YRHMAutoLoad` 或 `RAAutoTeslaCharge`。新增命令使用唯一且稳定的内部名；ESC 界面的名称、分类、描述使用简体中文。
- 编译期切换 YRpp 目录不等于兼容另一版本游戏。运行时受目标 `gamemd.exe` SHA-256 门禁约束；未经新证据验证，不放宽门禁或猜测固定地址与 AOB。
- 待发游戏事件要遵守共用队列的容量、去重、期限和对局代次规则。可停用的持续功能应取消属于自己的待发意图；不要误删其他命令的事件。
- 编译与纯测试是离线证据。实际注入、ESC 热键显示和持久化、单机效果与联机 OOS 均需分别验收；未实测时明确标注未验证。
- 不擅自纳入被 `.gitignore` 排除的本地文档，也不因完成代码而自动提交或推送。

## 工作方式

先检查工作区状态、相近的现有命令和构建基线，再做最小范围的改动。新增命令时，按 [新增原生命令](references/add-native-command.md) 核对接入点。完工后运行 `& .\scripts\build.ps1`，检查 `.vcxproj` 与 `.filters` 是否包含新文件，并说明离线通过了什么、哪些游戏内路径仍未验证。
