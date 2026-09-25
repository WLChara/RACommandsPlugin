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

## 入队边界

目标样本的 `Networking::QueueClickedMissionEvent`（`0x646E90`）展示原生 OutList 写入：容量为 128，事件数组起始于 `0xA802D4`，每项复制 111 字节，时间戳数组起始于 `0xA83A54`，写入索引位于 `0xA802D0`，待发数量位于 `0xA802C8`。适配器沿用这一路径的已验证布局，并只允许本地玩家的 Produce／Place。返回成功表示已复制到 OutList；事件是否在游戏模拟中被接受以及联机是否一致，仍需实机验收。
