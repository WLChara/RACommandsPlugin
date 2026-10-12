#pragma once

#include "Commands/DragFormationCommand/DragFormationTypes.h"

namespace ra_commands::drag_formation
{
    /** 连续格坐标几何；默认半径0.4、边缘间距0.3是演示参数，非原生碰撞半径。 */
    [[nodiscard]] Layout BuildLayout(WorldPoint anchor, WorldPoint end,
        std::size_t actorCount, double radius = 0.4, double spacing = 0.3);
    /** 按陆空/桥层及每格容量生成矩形；profiles在手势开始时固定。 */
    [[nodiscard]] Layout BuildLayout(WorldPoint anchor, WorldPoint end,
        const std::vector<auto_formation::ActorProfile>& profiles,
        double radius = 0.4, double spacing = 0.3, std::uint8_t infantryCapacity = 3);
    /** 每个理想槽位附近一格内的有限候选；超预算返回空，不截断阵型。 */
    [[nodiscard]] std::vector<auto_formation::Cell> BuildPlacementCells(const Layout& layout);
    /**
     * 将物理格槽位投影到可占格，再分配每格的逻辑席位并减少移动距离。
     * 载具独占、步兵最多三席、陆空独立；无法容纳全组时整次拒绝。
     * 返回预算失败或未分配ID时 assignments 必须为空。
     */
    [[nodiscard]] auto_formation::PlanResult PlanPlacement(const Layout& layout,
        const auto_formation::Snapshot& snapshot);
}
