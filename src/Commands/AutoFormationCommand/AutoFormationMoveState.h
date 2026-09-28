#pragma once

#include "Commands/AutoFormationCommand/IAutoFormationGamePort.h"

#include <optional>

namespace ra_commands::auto_formation
{
    /** 普通停步/走动不是失效；只拒绝实际阻止下令的停用、时空锁或磁电牵引。 */
    [[nodiscard]] constexpr bool CanAcceptFormationMove(
        bool deactivated, bool immobilized, bool attackedByLocomotor) noexcept
    {
        return !deactivated && !immobilized && !attackedByLocomotor;
    }

    /**
     * 尚未观察到列队目的地之前，旧任务可以正常推进、切换任务和停止。
     * 这些模拟状态变化不能代表玩家覆盖；显式新命令由观察器取消该 actor。
     * 返回空值表示已观察到列队目的地，调用方继续检查当前原生移动状态。
     */
    [[nodiscard]] constexpr std::optional<MoveState> ObservePendingMovePhase(
        bool hasIssued, bool isQueued, bool hasObservedDestination,
        std::uint32_t elapsedFrames, std::uint32_t applyGraceFrames) noexcept
    {
        if (!hasIssued) { return isQueued ? MoveState::Queued : MoveState::Waiting; }
        if (!hasObservedDestination)
        {
            return elapsedFrames <= applyGraceFrames ? MoveState::Moving : MoveState::Waiting;
        }
        return std::nullopt;
    }

    /** 已发出的原生绕行/就近停靠不代表玩家覆盖；停错格要保留固定目标重试。 */
    [[nodiscard]] constexpr MoveState ObserveIssuedGoalState(
        bool isAtAssignedCell, bool isStopped, bool hasMoveMission) noexcept
    {
        if (isAtAssignedCell && isStopped) { return MoveState::Arrived; }
        return !isStopped && hasMoveMission ? MoveState::Moving : MoveState::Waiting;
    }
}
