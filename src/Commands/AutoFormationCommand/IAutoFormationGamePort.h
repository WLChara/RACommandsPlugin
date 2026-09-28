#pragma once

#include "Commands/AutoFormationCommand/AutoFormationPlanner.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace ra_commands::auto_formation
{
    enum class MoveState
    {
        Invalid,
        Waiting,
        Queued,
        Moving,
        Arrived,
        Overridden
    };

    /**
     * 列队专属游戏边界；实现与调用方均在游戏线程。
     * ID 和值快照可跨帧，游戏指针不得跨帧保留。
     */
    class IAutoFormationGamePort
    {
    public:
        virtual ~IAutoFormationGamePort() = default;

        [[nodiscard]] virtual bool IsSessionActive() const = 0;
        [[nodiscard]] virtual std::uint32_t Epoch() const = 0;
        [[nodiscard]] virtual std::uint32_t CurrentFrame() const = 0;
        [[nodiscard]] virtual std::vector<ActorId> CaptureSelectedActorIds() const = 0;
        /** fixedCenter 保留首次列队中心；未提供时采集后调用 CalculateCenter。 */
        [[nodiscard]] virtual bool TryCaptureSnapshot(const std::vector<ActorId>& ids,
            std::optional<Center> fixedCenter, Snapshot& outSnapshot) const = 0;
        /** 授权本次已获目标格的参与者；队友临时占格不等同静态障碍。 */
        virtual void BeginPlan(const std::vector<ActorId>& assignedActors) = 0;
        /** 读取当前位置用于判断真实进展；false 表示身份或控制权已失效。 */
        [[nodiscard]] virtual bool TryGetActorCell(ActorId actor, Cell& outCell) const = 0;
        /** true 仅表示高级队列接受；暂时占用/拥塞返回 false，在操作期限内重试。 */
        [[nodiscard]] virtual bool SubmitMove(ActorId actor, Cell destination) = 0;
        [[nodiscard]] virtual MoveState ObserveMove(ActorId actor, Cell destination) const = 0;
        virtual void CancelPending(ActorId actor) = 0;
        virtual void Reset() = 0;
    };
}
