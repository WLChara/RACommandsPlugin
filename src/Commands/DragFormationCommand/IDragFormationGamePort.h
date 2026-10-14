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
        /** 选中身份必须全部仍有效；layout 提供本轮旋转理想槽及3/2/1容量，cells 是显式 XY 查询层。 */
        [[nodiscard]] virtual bool TryCapturePlacementSnapshot(
            const std::vector<auto_formation::ActorId>& actors,
            const std::vector<auto_formation::Cell>& cells,
            const Layout& layout,
            auto_formation::Snapshot& outSnapshot) const = 0;
        /** 使用现有列队执行器；失败不执行部分队伍。 */
        [[nodiscard]] virtual bool Commit(const auto_formation::PlanResult& plan) = 0;
    };
}
