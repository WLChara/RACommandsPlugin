#include "Commands/DragFormationCommand/DragFormationPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace ra_commands::drag_formation
{
    namespace
    {
        constexpr double MIN_DRAG_LENGTH = 0.25;
        constexpr double MAX_SLOT_DISPLACEMENT_SQUARED = 2.25;
        constexpr double INFINITE_COST = 1e100;
        constexpr std::size_t MAX_MATCH_STEPS = 25000000;
        constexpr int MAP_AXIS_LIMIT = 512;

        bool IsFinite(WorldPoint point)
        {
            return std::isfinite(point.mX) && std::isfinite(point.mY);
        }

        bool CellLess(const auto_formation::Cell& left, const auto_formation::Cell& right)
        {
            return std::tie(left.mX, left.mY, left.mOnBridge) <
                std::tie(right.mX, right.mY, right.mOnBridge);
        }

        bool IsUsableLayout(const Layout& layout)
        {
            if (!layout.mValid || !IsFinite(layout.mAnchor) || !IsFinite(layout.mEnd) ||
                !IsFinite(layout.mWidthAxis) || !IsFinite(layout.mDepthAxis) ||
                !IsFinite(layout.mLeft) || !IsFinite(layout.mRight) ||
                !std::isfinite(layout.mPitch) || layout.mPitch <= 0.0 ||
                layout.mSlots.empty() || layout.mSlots.size() > MAX_DEMO_ACTORS ||
                layout.mColumns == 0 || layout.mRows == 0 ||
                layout.mColumns > layout.mSlots.size() ||
                (layout.mProfiles.empty() &&
                    layout.mRows < (layout.mSlots.size() - 1) / layout.mColumns + 1) ||
                (!layout.mProfiles.empty() && layout.mProfiles.size() != layout.mSlots.size()))
            {
                return false;
            }
            if (!std::all_of(layout.mSlots.begin(), layout.mSlots.end(), IsFinite)) return false;
            for (const auto& plane : layout.mPlanes)
            {
                if (plane.mCellCenters.empty() || plane.mCellCenters.size() > MAX_DEMO_ACTORS ||
                    !std::all_of(plane.mCellCenters.begin(), plane.mCellCenters.end(), IsFinite))
                    return false;
            }
            return true;
        }

        double DistanceSquared(WorldPoint left, WorldPoint right)
        {
            const double dx = left.mX - right.mX;
            const double dy = left.mY - right.mY;
            return dx * dx + dy * dy;
        }

        struct MatchBudget
        {
            std::size_t mRemaining = MAX_MATCH_STEPS;

            bool Consume()
            {
                if (mRemaining == 0) return false;
                --mRemaining;
                return true;
            }
        };

        // 矩形 Hungarian。输入成本为有限非负数或 INFINITE_COST；返回每行唯一列。
        // 固定行、列顺序及严格比较确保同分时跨输入排列仍有稳定结果。
        bool TryMinCostMatch(const std::vector<std::vector<double>>& costs,
            MatchBudget& budget, std::vector<int>& outColumns)
        {
            const std::size_t rowCount = costs.size();
            if (rowCount == 0) { outColumns.clear(); return true; }
            const std::size_t columnCount = costs.front().size();
            if (columnCount < rowCount) return false;

            std::vector<double> rowPotential(rowCount + 1);
            std::vector<double> columnPotential(columnCount + 1);
            std::vector<int> columnRows(columnCount + 1);
            std::vector<int> previous(columnCount + 1);
            for (std::size_t row = 1; row <= rowCount; ++row)
            {
                columnRows[0] = static_cast<int>(row);
                std::size_t column = 0;
                std::vector<double> minimum(columnCount + 1, INFINITE_COST);
                std::vector<bool> used(columnCount + 1, false);
                do
                {
                    used[column] = true;
                    const int activeRow = columnRows[column];
                    double delta = INFINITE_COST;
                    std::size_t nextColumn = 0;
                    for (std::size_t next = 1; next <= columnCount; ++next)
                    {
                        if (!budget.Consume()) return false;
                        if (used[next]) continue;
                        const double reduced = costs[activeRow - 1][next - 1] -
                            rowPotential[activeRow] - columnPotential[next];
                        if (reduced < minimum[next])
                        {
                            minimum[next] = reduced;
                            previous[next] = static_cast<int>(column);
                        }
                        if (minimum[next] < delta)
                        {
                            delta = minimum[next];
                            nextColumn = next;
                        }
                    }
                    if (nextColumn == 0 || delta >= INFINITE_COST / 2.0) return false;
                    for (std::size_t next = 0; next <= columnCount; ++next)
                    {
                        if (used[next])
                        {
                            rowPotential[columnRows[next]] += delta;
                            columnPotential[next] -= delta;
                        }
                        else minimum[next] -= delta;
                    }
                    column = nextColumn;
                } while (columnRows[column] != 0);
                do
                {
                    const std::size_t next = static_cast<std::size_t>(previous[column]);
                    columnRows[column] = columnRows[next];
                    column = next;
                } while (column != 0);
            }

            outColumns.assign(rowCount, -1);
            for (std::size_t column = 1; column <= columnCount; ++column)
            {
                if (columnRows[column] == 0) continue;
                const std::size_t row = static_cast<std::size_t>(columnRows[column] - 1);
                if (costs[row][column - 1] >= INFINITE_COST / 2.0) return false;
                outColumns[row] = static_cast<int>(column - 1);
            }
            return std::none_of(outColumns.begin(), outColumns.end(),
                [](int column) { return column < 0; });
        }

        std::vector<auto_formation::Cell> CollectPlacementCells(
            const Layout& layout, bool& outBudgetExceeded)
        {
            outBudgetExceeded = false;
            if (!IsUsableLayout(layout)) return {};
            std::vector<auto_formation::Cell> cells;
            std::vector<WorldPoint> physicalCenters;
            for (const auto& plane : layout.mPlanes)
                physicalCenters.insert(physicalCenters.end(),
                    plane.mCellCenters.begin(), plane.mCellCenters.end());
            if (layout.mPlanes.empty()) physicalCenters = layout.mSlots;
            if (physicalCenters.size() > MAX_DEMO_ACTORS)
            {
                outBudgetExceeded = true;
                return {};
            }
            cells.reserve(std::min(MAX_PLACEMENT_CELLS, physicalCenters.size() * 9));
            for (const auto slot : physicalCenters)
            {
                // 远离地图的有限坐标不转换成整数，以免 round 的结果越过表示范围。
                if (slot.mX < -2.0 || slot.mX > MAP_AXIS_LIMIT + 1.0 ||
                    slot.mY < -2.0 || slot.mY > MAP_AXIS_LIMIT + 1.0)
                {
                    return {};
                }
                const int centerX = static_cast<int>(std::round(slot.mX));
                const int centerY = static_cast<int>(std::round(slot.mY));
                for (int x = centerX - 1; x <= centerX + 1; ++x)
                {
                    for (int y = centerY - 1; y <= centerY + 1; ++y)
                    {
                        if (x >= 0 && x < MAP_AXIS_LIMIT && y >= 0 && y < MAP_AXIS_LIMIT)
                            cells.push_back({x, y, false});
                    }
                }
            }
            std::sort(cells.begin(), cells.end(), CellLess);
            cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
            if (cells.size() > MAX_PLACEMENT_CELLS)
            {
                outBudgetExceeded = true;
                return {};
            }
            return cells;
        }
    }

    Layout BuildLayout(WorldPoint anchor, WorldPoint end,
        std::size_t actorCount, double radius, double spacing)
    {
        Layout layout;
        if (!IsFinite(anchor) || !IsFinite(end) || actorCount == 0 ||
            actorCount > MAX_DEMO_ACTORS || !std::isfinite(radius) ||
            !std::isfinite(spacing) || radius <= 0.0 || spacing < 0.0)
        {
            return layout;
        }
        const double dx = end.mX - anchor.mX;
        const double dy = end.mY - anchor.mY;
        const double length = std::hypot(dx, dy);
        const double pitch = 2.0 * radius + spacing;
        if (!std::isfinite(length) || length < MIN_DRAG_LENGTH ||
            !std::isfinite(pitch) || pitch <= 0.0)
        {
            return layout;
        }

        std::size_t columns = 1;
        if (length >= 2.0 * radius)
        {
            const double capacity = std::floor((length - 2.0 * radius) / pitch) + 1.0;
            columns = capacity >= static_cast<double>(actorCount)
                ? actorCount : static_cast<std::size_t>(capacity);
        }
        const std::size_t rows = (actorCount - 1) / columns + 1;
        const WorldPoint depthAxis { dx / length, dy / length };
        const WorldPoint widthAxis { -depthAxis.mY, depthAxis.mX };
        const auto left = WorldPoint { anchor.mX - widthAxis.mX * length / 2.0,
            anchor.mY - widthAxis.mY * length / 2.0 };
        const auto right = WorldPoint { anchor.mX + widthAxis.mX * length / 2.0,
            anchor.mY + widthAxis.mY * length / 2.0 };
        if (!IsFinite(left) || !IsFinite(right)) return {};

        layout.mAnchor = anchor;
        layout.mEnd = end;
        layout.mWidthAxis = widthAxis;
        layout.mDepthAxis = depthAxis;
        layout.mLeft = left;
        layout.mRight = right;
        layout.mColumns = columns;
        layout.mRows = rows;
        layout.mPitch = pitch;
        layout.mSlots.reserve(actorCount);
        for (std::size_t row = 0; row < rows; ++row)
        {
            const std::size_t rowCount = std::min(columns, actorCount - row * columns);
            const double depth = (static_cast<double>(row) -
                static_cast<double>(rows - 1) / 2.0) * pitch;
            for (std::size_t column = 0; column < rowCount; ++column)
            {
                const double width = (static_cast<double>(column) -
                    static_cast<double>(rowCount - 1) / 2.0) * pitch;
                const WorldPoint slot { anchor.mX + width * widthAxis.mX + depth * depthAxis.mX,
                    anchor.mY + width * widthAxis.mY + depth * depthAxis.mY };
                if (!IsFinite(slot)) return {};
                layout.mSlots.push_back(slot);
            }
        }
        layout.mValid = true;
        layout.mPlanes.push_back({auto_formation::OccupancyPlane::Ground, false, layout.mSlots});
        return layout;
    }

    Layout BuildLayout(WorldPoint anchor, WorldPoint end,
        const std::vector<auto_formation::ActorProfile>& profiles,
        double radius, double spacing, std::uint8_t infantryCapacity)
    {
        if (profiles.empty() || profiles.size() > MAX_DEMO_ACTORS ||
            infantryCapacity == 0 || infantryCapacity > 3) return {};
        using auto_formation::ActorKind;
        using auto_formation::OccupancyPlane;
        std::set<auto_formation::ActorId> ids;
        using PlaneKey = std::pair<OccupancyPlane, bool>;
        std::map<PlaneKey, std::vector<auto_formation::ActorProfile>> groups;
        for (const auto& profile : profiles)
        {
            if (profile.mId == 0 || !ids.insert(profile.mId).second ||
                (profile.mKind != ActorKind::Vehicle && profile.mKind != ActorKind::Infantry) ||
                (profile.mPlane != OccupancyPlane::Ground && profile.mPlane != OccupancyPlane::Air) ||
                (profile.mPlane == OccupancyPlane::Air && profile.mOnBridge)) return {};
            groups[{profile.mPlane, profile.mOnBridge}].push_back(profile);
        }
        Layout layout;
        layout.mProfiles = profiles; // 输入次序属于手势冻结契约。
        layout.mInfantryCapacity = infantryCapacity;
        std::map<auto_formation::ActorId, WorldPoint> visualByActor;
        std::size_t maximumDemand = 0;
        for (auto& [key, members] : groups)
        {
            std::sort(members.begin(), members.end(), [](const auto& left, const auto& right)
                { return left.mId < right.mId; });
            const auto demand = auto_formation::RequiredCellCount(
                members, key.first, key.second, infantryCapacity);
            const auto planeLayout = BuildLayout(anchor, end, demand, radius, spacing);
            if (!planeLayout.mValid) return {};
            if (demand > maximumDemand)
            {
                maximumDemand = demand;
                layout.mAnchor = planeLayout.mAnchor;
                layout.mEnd = planeLayout.mEnd;
                layout.mWidthAxis = planeLayout.mWidthAxis;
                layout.mDepthAxis = planeLayout.mDepthAxis;
                layout.mLeft = planeLayout.mLeft;
                layout.mRight = planeLayout.mRight;
                layout.mColumns = planeLayout.mColumns;
                layout.mRows = planeLayout.mRows;
                layout.mPitch = planeLayout.mPitch;
            }
            layout.mPlanes.push_back({key.first, key.second, planeLayout.mSlots});
            std::vector<auto_formation::ActorProfile> infantry;
            std::size_t cellIndex = 0;
            for (const auto& member : members)
            {
                if (member.mKind == ActorKind::Infantry) infantry.push_back(member);
                else visualByActor.emplace(member.mId, planeLayout.mSlots[cellIndex++]);
            }
            for (std::size_t start = 0; start < infantry.size(); start += infantryCapacity)
            {
                const auto count = std::min<std::size_t>(infantryCapacity, infantry.size() - start);
                const auto center = planeLayout.mSlots[cellIndex++];
                for (std::size_t member = 0; member < count; ++member)
                {
                    const double offset = (static_cast<double>(member) -
                        static_cast<double>(count - 1) / 2.0) * 0.22;
                    visualByActor.emplace(infantry[start + member].mId,
                        WorldPoint {center.mX + offset * planeLayout.mWidthAxis.mX,
                            center.mY + offset * planeLayout.mWidthAxis.mY});
                }
            }
        }
        layout.mSlots.reserve(profiles.size());
        for (const auto& profile : profiles) layout.mSlots.push_back(visualByActor.at(profile.mId));
        layout.mValid = true;
        return layout;
    }

    std::vector<auto_formation::Cell> BuildPlacementCells(const Layout& layout)
    {
        bool budgetExceeded = false;
        return CollectPlacementCells(layout, budgetExceeded);
    }

    auto_formation::PlanResult PlanPlacement(const Layout& layout,
        const auto_formation::Snapshot& snapshot)
    {
        auto_formation::PlanResult result;
        result.mCenter = layout.mAnchor;
        for (const auto& actor : snapshot.mActors)
            result.mUnassignedActors.push_back(actor.mId);
        std::sort(result.mUnassignedActors.begin(), result.mUnassignedActors.end());
        if (snapshot.mActors.size() > MAX_DEMO_ACTORS)
        {
            result.mBudgetExceeded = true;
            return result;
        }
        if (!IsUsableLayout(layout) || layout.mSlots.size() != snapshot.mActors.size())
            return result;

        if (!layout.mProfiles.empty())
        {
            std::map<auto_formation::ActorId, auto_formation::ActorProfile> expected;
            for (const auto& profile : layout.mProfiles)
                if (profile.mId == 0 || !expected.emplace(profile.mId, profile).second) return result;
            if (expected.size() != snapshot.mActors.size()) return result;
            for (const auto& actor : snapshot.mActors)
            {
                const auto found = expected.find(actor.mId);
                if (found == expected.end() || found->second.mKind != actor.mKind ||
                    found->second.mPlane != actor.mPlane ||
                    found->second.mOnBridge != actor.mCurrentCell.mOnBridge) return result;
            }
            auto packedSnapshot = snapshot;
            packedSnapshot.mCenter = layout.mAnchor;
            return auto_formation::PlanCellPacking(packedSnapshot, layout.mPlanes,
                true, 1.5, layout.mInfantryCapacity);
        }

        bool placementBudgetExceeded = false;
        const auto placementCells = CollectPlacementCells(layout, placementBudgetExceeded);
        result.mBudgetExceeded = placementBudgetExceeded;
        if (placementCells.empty()) return result;
        std::vector<const auto_formation::ActorSnapshot*> actors;
        actors.reserve(snapshot.mActors.size());
        for (const auto& actor : snapshot.mActors)
        {
            if (actor.mId == 0 || !IsFinite(actor.mPosition)) return result;
            if (actor.mCandidates.size() > MAX_PLACEMENT_CELLS)
            {
                result.mBudgetExceeded = true;
                return result;
            }
            actors.push_back(&actor);
        }
        std::sort(actors.begin(), actors.end(), [](const auto* left, const auto* right)
            { return left->mId < right->mId; });
        for (std::size_t actor = 1; actor < actors.size(); ++actor)
            if (actors[actor - 1]->mId == actors[actor]->mId) return result;

        // BuildPlacementCells 的 false 只作 XY 查询层；交集保留候选格真实桥层。
        std::vector<auto_formation::Cell> commonCells = actors.front()->mCandidates;
        std::sort(commonCells.begin(), commonCells.end(), CellLess);
        commonCells.erase(std::unique(commonCells.begin(), commonCells.end()), commonCells.end());
        for (std::size_t actor = 1; actor < actors.size() && !commonCells.empty(); ++actor)
        {
            auto candidates = actors[actor]->mCandidates;
            std::sort(candidates.begin(), candidates.end(), CellLess);
            candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
            std::vector<auto_formation::Cell> intersection;
            std::set_intersection(commonCells.begin(), commonCells.end(),
                candidates.begin(), candidates.end(), std::back_inserter(intersection), CellLess);
            commonCells = std::move(intersection);
        }
        commonCells.erase(std::remove_if(commonCells.begin(), commonCells.end(),
            [&](const auto_formation::Cell& cell)
            {
                return !std::binary_search(placementCells.begin(), placementCells.end(),
                    auto_formation::Cell {cell.mX, cell.mY, false}, CellLess);
            }), commonCells.end());
        if (commonCells.size() < layout.mSlots.size()) return result;

        std::vector<std::vector<double>> costs(layout.mSlots.size(),
            std::vector<double>(commonCells.size(), INFINITE_COST));
        for (std::size_t slot = 0; slot < layout.mSlots.size(); ++slot)
        {
            bool hasCandidate = false;
            for (std::size_t cell = 0; cell < commonCells.size(); ++cell)
            {
                const double distance = DistanceSquared(layout.mSlots[slot],
                    {static_cast<double>(commonCells[cell].mX), static_cast<double>(commonCells[cell].mY)});
                if (distance <= MAX_SLOT_DISPLACEMENT_SQUARED)
                {
                    costs[slot][cell] = distance;
                    hasCandidate = true;
                }
            }
            if (!hasCandidate) return result;
        }
        MatchBudget budget;
        std::vector<int> slotColumns;
        if (!TryMinCostMatch(costs, budget, slotColumns))
        {
            result.mBudgetExceeded = budget.mRemaining == 0;
            return result;
        }
        std::vector<auto_formation::Cell> assignedCells;
        assignedCells.reserve(slotColumns.size());
        for (const int column : slotColumns) assignedCells.push_back(commonCells[column]);
        std::sort(assignedCells.begin(), assignedCells.end(), CellLess);

        costs.assign(actors.size(), std::vector<double>(assignedCells.size(), INFINITE_COST));
        for (std::size_t actor = 0; actor < actors.size(); ++actor)
        {
            bool hasCandidate = false;
            for (std::size_t cell = 0; cell < assignedCells.size(); ++cell)
            {
                const double distance = DistanceSquared(actors[actor]->mPosition,
                    {static_cast<double>(assignedCells[cell].mX), static_cast<double>(assignedCells[cell].mY)});
                if (std::isfinite(distance) && distance < INFINITE_COST / 2.0)
                {
                    costs[actor][cell] = distance;
                    hasCandidate = true;
                }
            }
            if (!hasCandidate) return result;
        }
        std::vector<int> actorColumns;
        if (!TryMinCostMatch(costs, budget, actorColumns))
        {
            result.mBudgetExceeded = budget.mRemaining == 0;
            return result;
        }
        result.mUnassignedActors.clear();
        for (std::size_t actor = 0; actor < actors.size(); ++actor)
            result.mAssignments.push_back({actors[actor]->mId, assignedCells[actorColumns[actor]]});
        return result;
    }
}
