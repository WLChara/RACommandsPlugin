#pragma once

#include "Commands/DragFormationCommand/DragFormationTypes.h"

namespace ra_commands::drag_formation
{
    /** 游戏线程边界；跨帧仅保存身份值、坐标与epoch。 */
    class IDragFormationGamePort
    {
    public:
        virtual ~IDragFormationGamePort() = default;
        [[nodiscard]] virtual bool IsSessionActive() const = 0;
        [[nodiscard]] virtual std::uint32_t Epoch() const = 0;
        [[nodiscard]] virtual std::vector<auto_formation::ActorProfile> CaptureSelectedActorProfiles() const = 0;
        /** 选中身份必须全部仍有效，候选格为游戏合法性查询的显式输入。 */
        [[nodiscard]] virtual bool TryCapturePlacementSnapshot(
            const std::vector<auto_formation::ActorId>& actors,
            const std::vector<auto_formation::Cell>& cells,
            auto_formation::Snapshot& outSnapshot) const = 0;
        /** 使用现有列队执行器；失败不执行部分队伍。 */
        [[nodiscard]] virtual bool Commit(const auto_formation::PlanResult& plan) = 0;
    };
}
