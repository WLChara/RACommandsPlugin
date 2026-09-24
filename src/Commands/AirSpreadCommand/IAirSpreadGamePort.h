#pragma once

#include "Commands/AirSpreadCommand/AirSpreadPlanner.h"

namespace ra_commands::air_spread
{
    struct AirSpreadSnapshot
    {
        std::vector<ActorCandidates> InfantryActors;
        std::vector<CellOccupancy> InfantryOccupancy;
        std::vector<ActorCandidates> FlyingActors;
        std::vector<CellOccupancy> FlyingOccupancy;
    };

    /** 空军分散命令所需的游戏边界；调用方在游戏主线程持有实现与服务。 */
    class IAirSpreadGamePort
    {
    public:
        virtual ~IAirSpreadGamePort() = default;

        /**
         * 按热键当时鼠标所指地图格采集独立的值快照；失败时返回 false。
         * 步兵组只含飞行 Infantry，另一组只含飞行 Unit 和 Aircraft。
         * 两组候选格均须按接近鼠标格排序且有效，占用数分别属于对应组。
         */
        [[nodiscard]] virtual bool TryCaptureSnapshot(AirSpreadSnapshot& outSnapshot) const = 0;

        /**
         * 复验后提交 Move 意图到共用高级队列；拒绝时返回 false。
         * 返回 true 只表示意图被接受，不保证已经进入游戏原生队列。
         * 游戏适配器还须在真正发出任务前再次复验。
         */
        [[nodiscard]] virtual bool SubmitMove(ActorId actor, Cell destination) = 0;
    };
}
