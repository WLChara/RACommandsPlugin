#pragma once

#include "Commands/AutoFormationCommand/AutoFormationPlanner.h"

namespace ra_commands::auto_formation
{
    struct PlaneLayout
    {
        OccupancyPlane mPlane = OccupancyPlane::Ground;
        bool mOnBridge = false;
        std::vector<Center> mCellCenters;
    };

    /** 校验全组逻辑容量：同层同格载具独占或步兵最多3席；陆空分别计数。 */
    [[nodiscard]] bool HasValidReservations(const std::vector<Assignment>& assignments);
    /** 每个物理格请求包含一辆车或最多 infantryCapacity 名步兵。 */
    [[nodiscard]] std::size_t RequiredCellCount(const std::vector<ActorProfile>& profiles,
        OccupancyPlane plane, bool onBridge, std::uint8_t infantryCapacity = 3);
    /**
     * 有界物理格匹配后分配步兵席位。完整模式缺任何演员即整次拒绝；
     * 部分模式按请求代表的演员数量加权，优先最大化实际分配人数。
     * projectionRadius限制偏离理想格心的距离，逻辑席位不传给原生移动函数。
     */
    [[nodiscard]] PlanResult PlanCellPacking(const Snapshot& snapshot,
        const std::vector<PlaneLayout>& layouts, bool requireComplete,
        double projectionRadius, std::uint8_t infantryCapacity = 3);
    /** 新类别的紧凑规划；比较3/2/1人组以适应部分占用，优先人数、再占格数。 */
    [[nodiscard]] PlanResult PlanCompactPacking(const Snapshot& snapshot);
}
