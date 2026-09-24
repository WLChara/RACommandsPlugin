#pragma once

#include "Commands/AirSpreadCommand/IAirSpreadGamePort.h"

namespace ra_commands::air_spread
{
    struct AirSpreadCommandResult
    {
        bool SnapshotCaptured = false;
        PlanResult InfantryPlan;
        PlanResult FlyingPlan;
        std::vector<Assignment> AcceptedMoves;
        std::vector<Assignment> RejectedMoves;
    };

    /** 单次热键的应用编排；不持有快照或跨帧游戏对象。 */
    class AirSpreadCommandService final
    {
    public:
        explicit AirSpreadCommandService(IAirSpreadGamePort& game);

        /**
         * 分别按每格 3 名飞行步兵、每格 1 个飞行单位或飞机规划并提交。
         * 采集失败时不规划、不提交；候选不足保留在各组的 UnassignedActors。
         * 提交失败记录在 RejectedMoves，后续分配仍继续提交。
         */
        [[nodiscard]] AirSpreadCommandResult OnHotkey();

    private:
        IAirSpreadGamePort& mGame;
    };
}
