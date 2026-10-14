#pragma once
#include "Commands/AutoFormationCommand/FormationCellPacking.h"

namespace ra_commands::auto_formation
{
    struct FormationCandidateAnchor
    {
        ActorId mActor;
        Center mIdealCenter;
    };
    /** 与大队规划复用空间排序和步兵组，输出每个演员的理想物理格心。
     * planes 为空时生成紧凑包络；传入显式 planes 时使用其旋转槽位和容量。
     * 采集前的 snapshot 可没有 candidates；无效身份/资源或需求不符返回空。
     */
    [[nodiscard]] std::vector<FormationCandidateAnchor> BuildFormationCandidateAnchors(
        const Snapshot& snapshot, const std::vector<PlaneLayout>& planes = {},
        std::uint8_t infantryCapacity = 3);
    /** 大队完整匹配。请求绑定一辆车或固定步兵组；按资源平面独占物理格。
     * 只接受全队，失败不返回部分 assignments；几何/移动偏好使用确定性的边顺序。
     * 不进行全局最小移动距离求解，不访问游戏对象或调用原生 API。
     */
    [[nodiscard]] PlanResult PlanLargeCellPacking(const Snapshot& snapshot,
        const std::vector<PlaneLayout>& layouts, double projectionRadius,
        std::uint8_t infantryCapacity);
    [[nodiscard]] PlanResult PlanLargeCompactPacking(const Snapshot& snapshot);
}
