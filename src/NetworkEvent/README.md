# NetworkEvent 布局证据

当前目标是 SHA-256 为 `7cd005d263fde203d9c84548200a057a8df61d724da3c6bd1e521eeb61cd0747` 的 32 位 `gamemd.exe`。以下地址和布局仅适用于这个样本。Tiberian Dawn 的 `EventClass::Data` 说明了按事件种类解释 union 的设计方式；本项目的字段偏移和含义以目标样本为准。

## 内存布局

`NetworkEvent` 占 111 字节。偏移 `+0` 是事件种类，`+1` 是未确认用途的字节，`+2` 是发起玩家索引，`+3..+6` 是游戏帧。偏移 `+7..+110` 是 104 字节的事件数据 union。旧 SDK／IDB 使用 `Checksum`、`CommandCount`、`Delay`、`ExtraData` 等扁平字段名；这些字段只适合特定事件，不能视为每个事件共同拥有的头部。

在 IDB 中，`ClientNetworkHandler::handle` 位于 `0x4C6CB0`，按事件种类分派普通事件。`FrameInfo` 在上游 `ClientNetworkHandler::handleAny`（`0x64C380`）处理，用于帧同步与 OOS 检查。目标 EXE 的压缩事件长度表位于 `0x8208EC`；它描述网络帧中的 payload 长度，不能替代内存中的 111 字节记录大小。

| union 成员 | 对应事件及已确认用途 | 数据长度 | 主要证据 |
| --- | --- | ---: | --- |
| `Target` | PowerOn／PowerOff、Idle、Scatter、Deploy、Detonate、Primary、Repair、Sell：对象运行时 ID 与种类 | 5 | `0x4C6CB0` 对 payload `+0` 调用 NetID 解包 |
| `Value` | Ally、GameSpeed 等：单个 32 位参数 | 4 | `0x4C6CB0` 读取 payload `+0` |
| `Cell` | SellCell：两个 16 位地图格坐标 | 4 | `0x4C6CB0` 读取 payload `+0..+3` |
| `Production` | Produce／Suspend／Abandon／AbandonAll：对象类型、类型索引、海军标记 | 12 | 建构器 `0x4C6970` 写入 `+7/+11/+15`，分发器按相同偏移读取 |
| `Place` | Place：对象类型、类型索引、附加标记、两个 16 位地图格坐标 | 16 | 建构器 `0x4C6AE0` 写入 `+7/+11/+15/+19`；分发器调用 `0x4FB0E0` |
| `SpecialPlace` | SpecialPlace：超级武器索引与地图格 | 8 | `0x4C6CB0` 读取 payload `+0/+4..+7` |
| `FrameInfo` | FrameInfo 的校验和、命令数与延迟；ResponseTime 使用同一布局中的延迟字节 | 7 | `0x64C380` 处理 FrameInfo；长度表 `0x8208EC` |
| `Timing` | Timing：目标帧率、帧延迟、帧发送率 | 5 | `0x4C6CB0` 读取 payload `+0..+4` |
| `AddressChange` | AddressChange：玩家索引与地址值 | 5 | `0x4C6CB0` 读取 payload `+0/+1..+4` |
| `Archive` | Archive：两个 NetID | 10 | `0x4C6CB0` 解包 payload `+0` 和 `+5` |
| `Raw` | MegaMission、扩展网络事件和仍未恢复的变体 | 最多 104 | 保留原始字节，不赋予未证实的字段语义 |

`Produce` 对建筑使用 `BuildingType`（7）。`Place` 的处理函数接受 `Building`（6）或 `BuildingType`（7）；当前建构器采用原有建筑放置调用使用的 `Building`（6）。两类事件的处理函数都依据建筑类型的 `BuildCat` 选择主建造栏或防御栏，`BuildCat::Combat`（5）指向防御栏。

`SpecialPlace` 的索引是发起玩家 `HouseClass::Supers` 中的位置，不是全局超武类型索引；建构器按此布局写入索引与目标格。

## 入队边界

目标样本的 `Networking::QueueClickedMissionEvent`（`0x646E90`）展示原生 OutList 写入：容量为 128，事件数组起始于 `0xA802D4`，每项复制 111 字节，时间戳数组起始于 `0xA83A54`，写入索引位于 `0xA802D0`，待发数量位于 `0xA802C8`。适配器沿用这一路径的已验证布局，并只允许本地玩家的 Produce／Place／SpecialPlace。对 SpecialPlace 会重新检查本地玩家的超武槽、就绪状态和目标地图格。返回成功表示已复制到 OutList；事件是否在游戏模拟中被接受以及联机是否一致，仍需实机验收。

## 规划参与者与批量规划事件

`Game/PlanningParticipantCapacity` 在游戏线程安装数量比较处的 Hook（`0x6391A7`），允许战役、遭遇战、LAN 和 Internet 对局超过本方 128 个规划参与者。会话、线程、回放和观察者门禁由游戏适配层负责，Hook 层只负责寄存器恢复和原函数转发。原生 `0x639130` 入口保留，已有入口包装仍可执行。单单位路径长度的 15 条反馈和 16 条追加检查保持原状。

只有批量规划传输能力就绪时才放行参与者数量检查；可选生产 Hook 不兼容或传输故障时回退原生数量策略，普通批量命令能力独立保留。

规划输入中的 `ClickedMission` 经 `0x637DD0` 写入规划标志，再调用通用 OutList 生产入口 `0x6521C0`。后者的 ABI 是 `bool __stdcall(NetworkEvent)`：按值复制 111 字节，调用栈占 112 字节，原生以 `RET 0x70` 清理。普通命令的生产入口仍是独立路径。

规划节点及以下控制事件进入与普通命令共用的 `EventBacklog`，保持 FIFO；提交不得越过未发完的节点。传输使用已有的单机 DoList 消费或联机 ExtraFrame／OutList 回退，不增加事件种类，不修改远端处理器。

| 事件 | Kind | 线上 payload 长度 |
| --- | ---: | ---: |
| 规划 MegaMission | `0x04`，payload `+22` 为 `1` | 23，Kind 后另有聚合数量字节 |
| PlanConnect | `0x2A` | 10 |
| PlanCommit | `0x2B` | 0 |
| PlanNodeDelete | `0x2C` | 5 |

队列保留完整事件及跟随目标；普通单位意图替换不删除尚未送出的规划节点。规划模式中的停止、部署和散开仍由原生逻辑提示拒绝。回放、观察、无效会话及停用状态回退原生路径；换局取消 DLL 尚未发送的副本，已经进入原生系统的规划图仍由游戏拥有。

本次解除的是参与者准入限制和规划编辑时的发送容量。原版 DoList 的单次 House 扫描使用 256 槽 MegaMission 缓冲；原生 `PlanCommit` 后续生成的执行事件可能触及这个边界，但尚未取得真实对局中丢命令的具体复现方式。这不等于整次操作只能控制 256 个单位。当前只记录待复现风险，保留原生执行帧和调度方式；出现实际问题后，再按游戏模式、协议模式、人数、任务、House 和执行帧核对发送与消费数量。隔离测试不等于真实联机验收。

验证脚本：`scripts/test-planning-participant-capacity.ps1` 核对中段网关和入口包装；`scripts/test-command-capacity.ps1` 核对共享队列、原生生产 ABI、规划事件顺序和线上字节。
