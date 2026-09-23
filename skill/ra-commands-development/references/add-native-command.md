# 新增原生命令

此清单只在新增或扩展游戏原生命令时使用。先参考 `src/Commands/AutoLoadCommand/` 的一次性命令，以及 `src/Commands/TeslaChargeCommand/` 的持续开关命令；不要盲目复制其游戏规则。

1. 定义行为：热键是一次执行还是切换状态？状态归谁所有、何时按对局重置？明确支持的游戏版本、单位归属、任务类型及关停时待发命令的处理。
2. 在 `src/Commands/<命令名>/` 实现纯规则与服务。快照使用稳定身份和值，不跨帧保存可解引用的游戏指针；需要游戏信息或副作用时通过专属端口获取。
3. 在同目录实现 YRpp 游戏适配器。采集快照时过滤无效对象；提交意图前及真正下达任务前复验身份、归属、存活与任务条件。可共用的游戏对象规则放在 `src/Game/GameObjectAccess.*`，避免功能间复制。
4. 继承 `src/Commands/NativeCommandRegistry.*` 注册命令。内部名唯一且稳定，可见名称、分类、描述全部使用简体中文；命令对象须保持进程存续期有效。
5. 在 `src/Bootstrap/PluginRuntime.cpp` 接入游戏线程回调与注册。多条命令共用主帧入口，并在本轮待注册命令结束后统一重读热键；Shutdown 要停用回调并清理该命令状态。
6. 仅当功能需要游戏任务时接入 `src/ClickedMission/ClickedMissionDispatcher`。若是新 Mission，在 `ClickedMissionGameAdapter` 中明确允许其参数组合并做发送前验证；遵守原生队列发送前至少 13 个空槽的背压规则。不要以 `ClickedMission` 返回值声称原生事件已入队。
7. 把新源码、头文件和纯测试列入相应 `.vcxproj` 与 `.vcxproj.filters`，确保 Visual Studio 文件夹视图完整。为纯规划规则、开关/会话重置、容量不足、过期或取消等真实行为写测试。
8. 执行 `& .\scripts\build.ps1`，再进行适用的游戏内验收：注入时机、ESC 中的热键及重启后保存、单机任务效果、不支持版本拒绝加载；联机功能另做双客户端 OOS 检查。没有完成的验证必须在交付说明中列出。
