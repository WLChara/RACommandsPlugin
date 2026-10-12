#include "Commands/AutoFormationCommand/FormationCellPacking.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace ra_commands::auto_formation
{
    namespace
    {
        constexpr std::size_t MAX_ACTORS = 256;
        constexpr std::size_t MAX_CANDIDATES_PER_ACTOR = 4096;
        constexpr std::size_t MAX_EDGES = 262144;
        constexpr std::size_t MAX_RESOURCE_CELLS = 8192;
        constexpr std::size_t MAX_MATCH_WORK = 80000000;
        constexpr std::size_t MAX_COST_WORK = 18000000;
        constexpr std::size_t MAX_FLOW_EDGES = 600000;
        constexpr double INFINITE_COST = 1e100;
        // 最多256名演员的移动成本先截到每人1e6，故1e9保证先多保留一辆原地车。
        constexpr double NON_CURRENT_PENALTY = 1e9;
        // 半径最多32格：每段几何成本≤1.024e9，移动成本截到1e9。
        // 256个请求的一条交替路径累计次要成本<1e12，故先最大化实际演员数。
        constexpr std::int64_t ACTOR_PRIORITY = 1000000000000LL;
        constexpr std::int64_t INFINITE_FLOW_COST = std::numeric_limits<std::int64_t>::max() / 4;
        constexpr int MAP_AXIS_LIMIT = 512;
        constexpr int NONE = -1;

        struct PlaneKey
        {
            OccupancyPlane mPlane = OccupancyPlane::Ground;
            bool mOnBridge = false;

            bool operator<(const PlaneKey& other) const
            {
                return std::tie(mPlane, mOnBridge) < std::tie(other.mPlane, other.mOnBridge);
            }
        };

        struct ResourceCell
        {
            PlaneKey mPlane;
            Cell mCell;

            bool operator<(const ResourceCell& other) const
            {
                return std::tie(mPlane.mPlane, mPlane.mOnBridge, mCell.mX, mCell.mY, mCell.mOnBridge) <
                    std::tie(other.mPlane.mPlane, other.mPlane.mOnBridge,
                        other.mCell.mX, other.mCell.mY, other.mCell.mOnBridge);
            }
        };

        struct Request
        {
            PlaneKey mPlane;
            ActorKind mKind = ActorKind::Vehicle;
            std::vector<const ActorSnapshot*> mMembers;
            std::vector<Cell> mCandidates;
            Center mOrigin;
        };

        struct WorkBudget
        {
            std::size_t mRemaining;

            bool Consume()
            {
                if (mRemaining == 0) return false;
                --mRemaining;
                return true;
            }
        };

        bool IsFinite(Center point)
        {
            return std::isfinite(point.mX) && std::isfinite(point.mY);
        }

        bool IsRepresentablePosition(Center point)
        {
            constexpr double LOW = std::numeric_limits<std::int32_t>::min();
            constexpr double HIGH = std::numeric_limits<std::int32_t>::max();
            return IsFinite(point) && point.mX >= LOW && point.mX <= HIGH &&
                point.mY >= LOW && point.mY <= HIGH;
        }

        bool IsValidCell(Cell cell)
        {
            return cell.mX >= 0 && cell.mX < MAP_AXIS_LIMIT &&
                cell.mY >= 0 && cell.mY < MAP_AXIS_LIMIT;
        }

        bool CellLess(const Cell& left, const Cell& right)
        {
            return std::tie(left.mX, left.mY, left.mOnBridge) <
                std::tie(right.mX, right.mY, right.mOnBridge);
        }

        bool IsValidKind(ActorKind kind)
        {
            return kind == ActorKind::Vehicle || kind == ActorKind::Infantry;
        }

        bool IsValidPlane(OccupancyPlane plane)
        {
            return plane == OccupancyPlane::Ground || plane == OccupancyPlane::Air;
        }

        PlaneKey KeyFor(const ActorSnapshot& actor)
        {
            return {actor.mPlane, actor.mPlane == OccupancyPlane::Ground && actor.mCurrentCell.mOnBridge};
        }

        int AvailableAt(const ActorSnapshot& actor, Cell cell)
        {
            int available = actor.mKind == ActorKind::Vehicle ? 1 : 3;
            for (const auto& limit : actor.mCapacityLimits)
                if (limit.mCell == cell) available = std::min(available, static_cast<int>(limit.mAvailable));
            return available;
        }

        double DistanceSquared(Center left, Center right)
        {
            const double dx = left.mX - right.mX;
            const double dy = left.mY - right.mY;
            return dx * dx + dy * dy;
        }

        Center CellCenter(Cell cell)
        {
            return {static_cast<double>(cell.mX), static_cast<double>(cell.mY)};
        }

        PlanResult Reject(const Snapshot& snapshot, bool budgetExceeded = false)
        {
            PlanResult result;
            result.mCenter = snapshot.mCenter;
            result.mBudgetExceeded = budgetExceeded;
            for (const auto& actor : snapshot.mActors) result.mUnassignedActors.push_back(actor.mId);
            std::sort(result.mUnassignedActors.begin(), result.mUnassignedActors.end());
            return result;
        }

        bool TryMinCostMatch(const std::vector<std::vector<double>>& costs,
            WorkBudget& budget, std::vector<int>& outColumns)
        {
            const std::size_t rows = costs.size();
            if (rows == 0) { outColumns.clear(); return true; }
            const std::size_t columns = costs.front().size();
            if (columns < rows) return false;
            std::vector<double> rowPotential(rows + 1), columnPotential(columns + 1);
            std::vector<int> columnRows(columns + 1), previous(columns + 1);
            for (std::size_t row = 1; row <= rows; ++row)
            {
                columnRows[0] = static_cast<int>(row);
                std::size_t column = 0;
                std::vector<double> minimum(columns + 1, INFINITE_COST);
                std::vector<bool> used(columns + 1, false);
                do
                {
                    used[column] = true;
                    const int activeRow = columnRows[column];
                    double delta = INFINITE_COST;
                    std::size_t nextColumn = 0;
                    for (std::size_t next = 1; next <= columns; ++next)
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
                    for (std::size_t next = 0; next <= columns; ++next)
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
            outColumns.assign(rows, NONE);
            for (std::size_t column = 1; column <= columns; ++column)
            {
                if (columnRows[column] == 0) continue;
                const std::size_t row = static_cast<std::size_t>(columnRows[column] - 1);
                if (costs[row][column - 1] >= INFINITE_COST / 2.0) return false;
                outColumns[row] = static_cast<int>(column - 1);
            }
            return std::none_of(outColumns.begin(), outColumns.end(),
                [](int column) { return column == NONE; });
        }

        class FlowGraph
        {
        public:
            struct Edge
            {
                int mTo;
                int mReverse;
                int mCapacity;
                std::int64_t mCost;
            };

            explicit FlowGraph(std::size_t nodeCount) : mEdges(nodeCount) { }

            bool AddEdge(int from, int to, std::int64_t cost)
            {
                if (mForwardCount >= MAX_FLOW_EDGES) return false;
                const int forwardIndex = static_cast<int>(mEdges[from].size());
                const int reverseIndex = static_cast<int>(mEdges[to].size());
                mEdges[from].push_back({to, reverseIndex, 1, cost});
                mEdges[to].push_back({from, forwardIndex, 0, -cost});
                ++mForwardCount;
                return true;
            }

            bool Run(int source, int sink, WorkBudget& budget)
            {
                // 初始前向图严格按源→请求→格入→格出→槽→汇分层，故可线性初始化负权势。
                std::vector<std::int64_t> potential(mEdges.size(), INFINITE_FLOW_COST);
                potential[source] = 0;
                for (std::size_t node = 0; node < mEdges.size(); ++node)
                {
                    if (potential[node] == INFINITE_FLOW_COST) continue;
                    for (const auto& edge : mEdges[node])
                        if (edge.mCapacity > 0)
                            potential[edge.mTo] = std::min(potential[edge.mTo], potential[node] + edge.mCost);
                }
                for (auto& item : potential)
                    if (item == INFINITE_FLOW_COST) item = 0;

                while (true)
                {
                    std::vector<std::int64_t> distance(mEdges.size(), INFINITE_FLOW_COST);
                    std::vector<int> previousNode(mEdges.size(), NONE);
                    std::vector<int> previousEdge(mEdges.size(), NONE);
                    using QueueItem = std::pair<std::int64_t, int>;
                    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;
                    distance[source] = 0;
                    queue.push({0, source});
                    while (!queue.empty())
                    {
                        const auto [known, node] = queue.top();
                        queue.pop();
                        if (known != distance[node]) continue;
                        for (std::size_t index = 0; index < mEdges[node].size(); ++index)
                        {
                            if (!budget.Consume()) return false;
                            const auto& edge = mEdges[node][index];
                            if (edge.mCapacity == 0) continue;
                            const auto reduced = edge.mCost + potential[node] - potential[edge.mTo];
                            if (reduced < 0) return false;
                            const auto candidate = known + reduced;
                            if (candidate < distance[edge.mTo])
                            {
                                distance[edge.mTo] = candidate;
                                previousNode[edge.mTo] = node;
                                previousEdge[edge.mTo] = static_cast<int>(index);
                                queue.push({candidate, edge.mTo});
                            }
                        }
                    }
                    if (previousNode[sink] == NONE) return true;
                    const auto pathCost = distance[sink] + potential[sink] - potential[source];
                    // 每条路径有固定的 -人数×优先级；非负说明再加请求不能提高目标人数。
                    if (pathCost >= 0) return true;
                    for (std::size_t node = 0; node < mEdges.size(); ++node)
                        if (distance[node] != INFINITE_FLOW_COST) potential[node] += distance[node];
                    for (int node = sink; node != source; node = previousNode[node])
                    {
                        auto& edge = mEdges[previousNode[node]][previousEdge[node]];
                        --edge.mCapacity;
                        ++mEdges[node][edge.mReverse].mCapacity;
                    }
                }
            }

            const std::vector<Edge>& Edges(int node) const { return mEdges[node]; }

        private:
            std::vector<std::vector<Edge>> mEdges;
            std::size_t mForwardCount = 0;
        };

        bool AssignMembers(const std::vector<Request>& requests,
            const std::vector<int>& requestCells, const std::vector<ResourceCell>& cells,
            const Snapshot& snapshot, PlanResult& outResult, WorkBudget& budget)
        {
            std::map<PlaneKey, std::vector<const ActorSnapshot*>> actorsByPlane;
            std::map<PlaneKey, std::vector<std::pair<Cell, int>>> infantryCells;
            std::map<PlaneKey, std::vector<Cell>> vehicleCells;
            for (std::size_t request = 0; request < requests.size(); ++request)
            {
                if (requestCells[request] == NONE) continue;
                const auto& item = requests[request];
                const auto cell = cells[requestCells[request]].mCell;
                if (item.mKind == ActorKind::Vehicle)
                {
                    vehicleCells[item.mPlane].push_back(cell);
                    actorsByPlane[item.mPlane].push_back(item.mMembers.front());
                }
                else
                {
                    infantryCells[item.mPlane].push_back({cell, static_cast<int>(item.mMembers.size())});
                    for (const auto* actor : item.mMembers) actorsByPlane[item.mPlane].push_back(actor);
                }
            }
            // 对每类已选物理格再作一次 Actor→格最小移动距离匹配。
            // 步兵格按获选组的实际人数复制席位，避免重排后超过某车所见剩余容量。
            for (auto& [plane, actors] : actorsByPlane)
            {
                std::vector<const ActorSnapshot*> vehicles;
                std::vector<const ActorSnapshot*> infantries;
                for (const auto* actor : actors)
                    (actor->mKind == ActorKind::Vehicle ? vehicles : infantries).push_back(actor);
                const auto assignKind = [&](std::vector<const ActorSnapshot*>& members,
                    std::vector<std::pair<Cell, int>> destinations, ActorKind kind)
                {
                    if (members.empty()) return true;
                    std::sort(members.begin(), members.end(), [](const auto* left, const auto* right)
                        { return left->mId < right->mId; });
                    std::sort(destinations.begin(), destinations.end(),
                        [](const auto& left, const auto& right) { return CellLess(left.first, right.first); });
                    struct Seat { Cell mCell; int mIndex; int mCapacity; };
                    std::vector<Seat> seats;
                    for (const auto& [cell, count] : destinations)
                        for (int seat = 0; seat < count; ++seat) seats.push_back({cell, seat, count});
                    if (members.size() != seats.size()) return false;
                    std::vector<std::vector<double>> costs(members.size(),
                        std::vector<double>(seats.size(), INFINITE_COST));
                    for (std::size_t actor = 0; actor < members.size(); ++actor)
                    {
                        for (std::size_t seat = 0; seat < seats.size(); ++seat)
                        {
                            const auto cell = seats[seat].mCell;
                            if (std::find(members[actor]->mCandidates.begin(),
                                members[actor]->mCandidates.end(), cell) ==
                                members[actor]->mCandidates.end() ||
                                AvailableAt(*members[actor], cell) < seats[seat].mCapacity)
                            {
                                continue;
                            }
                            const double cost = DistanceSquared(members[actor]->mPosition, CellCenter(cell));
                            if (std::isfinite(cost) && cost < INFINITE_COST / 2.0)
                                costs[actor][seat] = std::min(cost, 1000000.0) +
                                    (members[actor]->mCurrentCell == cell ? 0.0 : NON_CURRENT_PENALTY);
                        }
                    }
                    std::vector<int> columns;
                    if (!TryMinCostMatch(costs, budget, columns)) return false;
                    for (std::size_t actor = 0; actor < members.size(); ++actor)
                        outResult.mAssignments.push_back({members[actor]->mId,
                            seats[columns[actor]].mCell, kind, plane.mPlane,
                            static_cast<std::uint8_t>(seats[columns[actor]].mIndex)});
                    return true;
                };
                std::vector<std::pair<Cell, int>> vehicleDestinations;
                for (const auto cell : vehicleCells[plane]) vehicleDestinations.push_back({cell, 1});
                if (!assignKind(vehicles, std::move(vehicleDestinations), ActorKind::Vehicle) ||
                    !assignKind(infantries, infantryCells[plane], ActorKind::Infantry)) return false;
            }
            std::sort(outResult.mAssignments.begin(), outResult.mAssignments.end(),
                [](const Assignment& left, const Assignment& right) { return left.mActor < right.mActor; });
            for (const auto& actor : snapshot.mActors)
            {
                if (std::none_of(outResult.mAssignments.begin(), outResult.mAssignments.end(),
                    [&](const Assignment& assignment) { return assignment.mActor == actor.mId; }))
                    outResult.mUnassignedActors.push_back(actor.mId);
            }
            std::sort(outResult.mUnassignedActors.begin(), outResult.mUnassignedActors.end());
            return HasValidReservations(outResult.mAssignments);
        }

        std::vector<Center> CompactCenters(Center center, std::size_t count)
        {
            std::vector<Center> result;
            if (count == 0) return result;
            const std::size_t columns = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
            const std::size_t rows = (count - 1) / columns + 1;
            result.reserve(count);
            for (std::size_t row = 0; row < rows; ++row)
            {
                const std::size_t rowCount = std::min(columns, count - row * columns);
                for (std::size_t column = 0; column < rowCount; ++column)
                    result.push_back({center.mX + static_cast<double>(column) -
                        static_cast<double>(rowCount - 1) / 2.0,
                        center.mY + static_cast<double>(row) - static_cast<double>(rows - 1) / 2.0});
            }
            return result;
        }

        struct CompactQuality
        {
            std::size_t mPhysicalCells = 0;
            std::int64_t mArea = 0;
            double mCenterOffset = 0.0;
            double mMovement = 0.0;

            bool operator<(const CompactQuality& other) const
            {
                return std::tie(mPhysicalCells, mArea, mCenterOffset, mMovement) <
                    std::tie(other.mPhysicalCells, other.mArea,
                        other.mCenterOffset, other.mMovement);
            }
        };

        CompactQuality ScoreCompact(const Snapshot& snapshot, const PlanResult& plan)
        {
            CompactQuality score;
            std::set<ResourceCell> cells;
            struct Bounds
            {
                int mMinX = MAP_AXIS_LIMIT;
                int mMinY = MAP_AXIS_LIMIT;
                int mMaxX = -1;
                int mMaxY = -1;
            };
            std::map<PlaneKey, Bounds> bounds;
            for (const auto& assignment : plan.mAssignments)
            {
                const auto& cell = assignment.mDestination;
                const PlaneKey key {assignment.mPlane, cell.mOnBridge};
                if (cells.insert({key, cell}).second)
                {
                    auto& box = bounds[key];
                    box.mMinX = std::min(box.mMinX, cell.mX);
                    box.mMinY = std::min(box.mMinY, cell.mY);
                    box.mMaxX = std::max(box.mMaxX, cell.mX);
                    box.mMaxY = std::max(box.mMaxY, cell.mY);
                    score.mCenterOffset += DistanceSquared(snapshot.mCenter, CellCenter(cell));
                }
                const auto actor = std::find_if(snapshot.mActors.begin(), snapshot.mActors.end(),
                    [&](const ActorSnapshot& item) { return item.mId == assignment.mActor; });
                if (actor != snapshot.mActors.end())
                    score.mMovement += DistanceSquared(actor->mPosition, CellCenter(cell));
            }
            score.mPhysicalCells = cells.size();
            for (const auto& [key, box] : bounds)
            {
                (void)key;
                score.mArea += static_cast<std::int64_t>(box.mMaxX - box.mMinX + 1) *
                    (box.mMaxY - box.mMinY + 1);
            }
            return score;
        }
    }

    bool HasValidReservations(const std::vector<Assignment>& assignments)
    {
        std::map<ActorId, bool> ids;
        struct Usage { ActorKind mKind; unsigned mMask = 0; };
        std::map<ResourceCell, Usage> used;
        for (const auto& assignment : assignments)
        {
            if (assignment.mActor == 0 || !IsValidKind(assignment.mKind) ||
                !IsValidPlane(assignment.mPlane) || !IsValidCell(assignment.mDestination) ||
                (assignment.mPlane == OccupancyPlane::Air && assignment.mDestination.mOnBridge) ||
                !ids.emplace(assignment.mActor, true).second ||
                (assignment.mKind == ActorKind::Vehicle && assignment.mSeat != 0) ||
                (assignment.mKind == ActorKind::Infantry && assignment.mSeat >= 3)) return false;
            const ResourceCell key {{assignment.mPlane, assignment.mDestination.mOnBridge},
                assignment.mDestination};
            const auto [it, inserted] = used.try_emplace(key, Usage {assignment.mKind, 0});
            if (!inserted && it->second.mKind != assignment.mKind) return false;
            const unsigned bit = 1u << assignment.mSeat;
            if (it->second.mMask & bit) return false;
            it->second.mMask |= bit;
        }
        return true;
    }

    std::size_t RequiredCellCount(const std::vector<ActorProfile>& profiles,
        OccupancyPlane plane, bool onBridge, std::uint8_t infantryCapacity)
    {
        if (!IsValidPlane(plane) || infantryCapacity == 0 || infantryCapacity > 3) return 0;
        if (plane == OccupancyPlane::Air && onBridge) return 0;
        std::size_t vehicles = 0, infantry = 0;
        for (const auto& profile : profiles)
        {
            if (profile.mPlane != plane || profile.mOnBridge != onBridge) continue;
            if (profile.mKind == ActorKind::Vehicle) ++vehicles;
            else if (profile.mKind == ActorKind::Infantry) ++infantry;
        }
        return vehicles + (infantry == 0 ? 0 : (infantry - 1) / infantryCapacity + 1);
    }

    static PlanResult PlanCellPackingImpl(const Snapshot& snapshot,
        const std::vector<PlaneLayout>& layouts, bool requireComplete,
        double projectionRadius, std::uint8_t infantryCapacity,
        WorkBudget& matchBudget, WorkBudget& costBudget)
    {
        auto rejected = Reject(snapshot);
        if (snapshot.mActors.size() > MAX_ACTORS) return Reject(snapshot, true);
        if (snapshot.mActors.empty() || !IsRepresentablePosition(snapshot.mCenter) ||
            std::isnan(projectionRadius) || projectionRadius <= 0.0 ||
            infantryCapacity == 0 || infantryCapacity > 3) return rejected;

        std::map<ActorId, const ActorSnapshot*> byId;
        std::map<PlaneKey, std::vector<const ActorSnapshot*>> actorsByPlane;
        std::size_t inputEdges = 0;
        for (const auto& actor : snapshot.mActors)
        {
            if (actor.mId == 0 || !IsValidKind(actor.mKind) || !IsValidPlane(actor.mPlane) ||
                !IsRepresentablePosition(actor.mPosition) ||
                (actor.mPlane == OccupancyPlane::Air && actor.mCurrentCell.mOnBridge) ||
                !byId.emplace(actor.mId, &actor).second) return rejected;
            if (actor.mCandidates.size() > MAX_CANDIDATES_PER_ACTOR ||
                actor.mCandidates.size() > MAX_EDGES - inputEdges ||
                actor.mCapacityLimits.size() > MAX_CANDIDATES_PER_ACTOR)
                return Reject(snapshot, true);
            inputEdges += actor.mCandidates.size();
            actorsByPlane[KeyFor(actor)].push_back(&actor);
        }
        std::map<PlaneKey, const PlaneLayout*> layoutByPlane;
        for (const auto& layout : layouts)
        {
            const PlaneKey key {layout.mPlane, layout.mOnBridge};
            if (!IsValidPlane(layout.mPlane) ||
                (layout.mPlane == OccupancyPlane::Air && layout.mOnBridge) ||
                !layoutByPlane.emplace(key, &layout).second) return rejected;
        }
        if (layoutByPlane.size() != actorsByPlane.size()) return rejected;

        std::vector<Request> requests;
        requests.reserve(snapshot.mActors.size());
        for (auto& [plane, actors] : actorsByPlane)
        {
            const auto layout = layoutByPlane.find(plane);
            if (layout == layoutByPlane.end()) return rejected;
            std::sort(actors.begin(), actors.end(), [](const auto* left, const auto* right)
                { return left->mId < right->mId; });
            std::vector<const ActorSnapshot*> infantry;
            for (const auto* actor : actors)
            {
                if (actor->mKind == ActorKind::Infantry) { infantry.push_back(actor); continue; }
                Request request;
                request.mPlane = plane;
                request.mKind = ActorKind::Vehicle;
                request.mMembers.push_back(actor);
                request.mOrigin = actor->mPosition;
                for (const auto cell : actor->mCandidates)
                    if (IsValidCell(cell) && cell.mOnBridge == plane.mOnBridge &&
                        AvailableAt(*actor, cell) >= 1) request.mCandidates.push_back(cell);
                std::sort(request.mCandidates.begin(), request.mCandidates.end(), CellLess);
                request.mCandidates.erase(std::unique(request.mCandidates.begin(),
                    request.mCandidates.end()), request.mCandidates.end());
                requests.push_back(std::move(request));
            }
            for (std::size_t start = 0; start < infantry.size(); start += infantryCapacity)
            {
                Request request;
                request.mPlane = plane;
                request.mKind = ActorKind::Infantry;
                const auto count = std::min<std::size_t>(infantryCapacity, infantry.size() - start);
                double sumX = 0.0, sumY = 0.0;
                for (std::size_t member = 0; member < count; ++member)
                {
                    const auto* actor = infantry[start + member];
                    request.mMembers.push_back(actor);
                    sumX += actor->mPosition.mX;
                    sumY += actor->mPosition.mY;
                }
                request.mOrigin = {sumX / count, sumY / count};
                for (const auto cell : request.mMembers.front()->mCandidates)
                {
                    if (!IsValidCell(cell) || cell.mOnBridge != plane.mOnBridge) continue;
                    const bool legalForBundle = std::all_of(request.mMembers.begin(),
                        request.mMembers.end(), [&](const ActorSnapshot* actor)
                        {
                            return std::find(actor->mCandidates.begin(), actor->mCandidates.end(), cell) !=
                                actor->mCandidates.end() && AvailableAt(*actor, cell) >= static_cast<int>(count);
                        });
                    if (legalForBundle) request.mCandidates.push_back(cell);
                }
                std::sort(request.mCandidates.begin(), request.mCandidates.end(), CellLess);
                request.mCandidates.erase(std::unique(request.mCandidates.begin(),
                    request.mCandidates.end()), request.mCandidates.end());
                requests.push_back(std::move(request));
            }
            const auto& centers = layout->second->mCellCenters;
            const std::size_t count = std::count_if(requests.begin(), requests.end(),
                [plane](const Request& request)
                { return request.mPlane.mPlane == plane.mPlane &&
                    request.mPlane.mOnBridge == plane.mOnBridge; });
            if (centers.size() != count ||
                !std::all_of(centers.begin(), centers.end(), IsFinite)) return rejected;
        }
        if (!std::isfinite(projectionRadius) || projectionRadius > 32.0) return rejected;
        const double maxProjectionSquared = projectionRadius * projectionRadius;
        std::map<ResourceCell, std::vector<std::pair<int, std::int64_t>>> cellSlots;
        std::vector<std::pair<PlaneKey, Center>> slots;
        for (const auto& [plane, layout] : layoutByPlane)
        {
            for (const auto center : layout->mCellCenters)
            {
                if (center.mX < -32.0 || center.mX > MAP_AXIS_LIMIT + 31.0 ||
                    center.mY < -32.0 || center.mY > MAP_AXIS_LIMIT + 31.0) return rejected;
                const int slot = static_cast<int>(slots.size());
                slots.push_back({plane, center});
                const int lowX = std::max(0, static_cast<int>(std::ceil(center.mX - projectionRadius)));
                const int highX = std::min(MAP_AXIS_LIMIT - 1,
                    static_cast<int>(std::floor(center.mX + projectionRadius)));
                const int lowY = std::max(0, static_cast<int>(std::ceil(center.mY - projectionRadius)));
                const int highY = std::min(MAP_AXIS_LIMIT - 1,
                    static_cast<int>(std::floor(center.mY + projectionRadius)));
                for (int x = lowX; x <= highX; ++x)
                {
                    for (int y = lowY; y <= highY; ++y)
                    {
                        const double displacement = DistanceSquared(center,
                            {static_cast<double>(x), static_cast<double>(y)});
                        if (displacement > maxProjectionSquared) continue;
                        if (!matchBudget.Consume()) return Reject(snapshot, true);
                        const auto cost = static_cast<std::int64_t>(std::llround(displacement * 1000000.0));
                        cellSlots[{plane, {x, y, plane.mOnBridge}}].push_back({slot, cost});
                    }
                }
            }
        }
        std::map<ResourceCell, int> cellIndices;
        for (const auto& request : requests)
            for (const auto cell : request.mCandidates)
                if (cellSlots.contains({request.mPlane, cell}))
                {
                    if (!matchBudget.Consume()) return Reject(snapshot, true);
                    cellIndices.try_emplace({request.mPlane, cell}, NONE);
                    if (cellIndices.size() > MAX_RESOURCE_CELLS) return Reject(snapshot, true);
                }
        std::vector<ResourceCell> cells;
        cells.reserve(cellIndices.size());
        for (auto& [cell, index] : cellIndices)
        {
            index = static_cast<int>(cells.size());
            cells.push_back(cell);
        }
        // 同一资源平面且所有请求有完全相同的合法格时，可先把槽位投影到格，
        // 再把请求分配到已选格；任意请求均可使用任意格，故分解不会丢失可行方案。
        // 大型无遮挡混编由两次有界 Hungarian 完成，避免密集残量图长时间阻塞主帧。
        const bool uniformCandidates = !requests.empty() &&
            std::all_of(requests.begin(), requests.end(), [&](const Request& request)
            {
                return request.mPlane.mPlane == requests.front().mPlane.mPlane &&
                    request.mPlane.mOnBridge == requests.front().mPlane.mOnBridge &&
                    request.mCandidates == requests.front().mCandidates;
            });
        if (uniformCandidates && cells.size() >= slots.size())
        {
            std::vector<std::vector<double>> geometry(slots.size(),
                std::vector<double>(cells.size(), INFINITE_COST));
            for (std::size_t cell = 0; cell < cells.size(); ++cell)
                for (const auto [slot, cost] : cellSlots.at(cells[cell]))
                    geometry[slot][cell] = static_cast<double>(cost);
            std::vector<int> slotCells;
            if (TryMinCostMatch(geometry, costBudget, slotCells))
            {
                std::vector<int> chosenCells = slotCells;
                std::sort(chosenCells.begin(), chosenCells.end());
                std::vector<std::vector<double>> movement(requests.size(),
                    std::vector<double>(chosenCells.size(), INFINITE_COST));
                for (std::size_t request = 0; request < requests.size(); ++request)
                    for (std::size_t cell = 0; cell < chosenCells.size(); ++cell)
                        movement[request][cell] = DistanceSquared(requests[request].mOrigin,
                            CellCenter(cells[chosenCells[cell]].mCell));
                std::vector<int> requestColumns;
                if (TryMinCostMatch(movement, costBudget, requestColumns))
                {
                    std::vector<int> selected(requests.size(), NONE);
                    for (std::size_t request = 0; request < requests.size(); ++request)
                        selected[request] = chosenCells[requestColumns[request]];
                    PlanResult result;
                    result.mCenter = snapshot.mCenter;
                    if (AssignMembers(requests, selected, cells, snapshot, result, costBudget))
                        return result;
                    if (costBudget.mRemaining == 0) return Reject(snapshot, true);
                }
            }
            if (costBudget.mRemaining == 0) return Reject(snapshot, true);
        }
        const int source = 0;
        const int requestStart = 1;
        const int cellInStart = requestStart + static_cast<int>(requests.size());
        const int cellOutStart = cellInStart + static_cast<int>(cells.size());
        const int slotStart = cellOutStart + static_cast<int>(cells.size());
        const int sink = slotStart + static_cast<int>(slots.size());
        FlowGraph graph(static_cast<std::size_t>(sink) + 1);
        for (std::size_t request = 0; request < requests.size(); ++request)
            if (!graph.AddEdge(source, requestStart + static_cast<int>(request),
                -ACTOR_PRIORITY * static_cast<std::int64_t>(requests[request].mMembers.size())))
                return Reject(snapshot, true);
        for (std::size_t request = 0; request < requests.size(); ++request)
        {
            const auto& item = requests[request];
            for (const auto cell : item.mCandidates)
            {
                const auto found = cellIndices.find({item.mPlane, cell});
                if (found == cellIndices.end()) continue;
                const double movement = DistanceSquared(item.mOrigin, CellCenter(cell));
                const auto cost = std::isfinite(movement)
                    ? static_cast<std::int64_t>(std::llround(std::min(movement, 1000000000.0)))
                    : 1000000000LL;
                if (!graph.AddEdge(requestStart + static_cast<int>(request),
                    cellInStart + found->second, cost)) return Reject(snapshot, true);
            }
        }
        for (std::size_t cell = 0; cell < cells.size(); ++cell)
            if (!graph.AddEdge(cellInStart + static_cast<int>(cell),
                cellOutStart + static_cast<int>(cell), 0)) return Reject(snapshot, true);
        for (std::size_t cell = 0; cell < cells.size(); ++cell)
        {
            for (const auto [slot, cost] : cellSlots.at(cells[cell]))
                if (!graph.AddEdge(cellOutStart + static_cast<int>(cell),
                    slotStart + slot, cost)) return Reject(snapshot, true);
        }
        for (std::size_t slot = 0; slot < slots.size(); ++slot)
            if (!graph.AddEdge(slotStart + static_cast<int>(slot), sink, 0))
                return Reject(snapshot, true);
        if (!graph.Run(source, sink, matchBudget)) return Reject(snapshot, true);
        std::vector<int> requestCells(requests.size(), NONE);
        for (std::size_t request = 0; request < requests.size(); ++request)
            for (const auto& edge : graph.Edges(requestStart + static_cast<int>(request)))
                if (edge.mTo >= cellInStart && edge.mTo < cellOutStart && edge.mCapacity == 0)
                    requestCells[request] = edge.mTo - cellInStart;
        if (requireComplete && std::any_of(requestCells.begin(), requestCells.end(),
            [](int cell) { return cell == NONE; })) return rejected;
        if (std::all_of(requestCells.begin(), requestCells.end(),
            [](int cell) { return cell == NONE; })) return rejected;

        // 只重排已确认可行的演员集合与已选物理格；不再由二次优化更换容量模式。
        PlanResult result;
        result.mCenter = snapshot.mCenter;
        if (!AssignMembers(requests, requestCells, cells, snapshot, result, costBudget))
            return costBudget.mRemaining == 0 ? Reject(snapshot, true) : rejected;
        if (requireComplete && result.mAssignments.size() != snapshot.mActors.size()) return rejected;
        return result;
    }

    PlanResult PlanCellPacking(const Snapshot& snapshot,
        const std::vector<PlaneLayout>& layouts, bool requireComplete,
        double projectionRadius, std::uint8_t infantryCapacity)
    {
        WorkBudget matchBudget {MAX_MATCH_WORK};
        WorkBudget costBudget {MAX_COST_WORK};
        return PlanCellPackingImpl(snapshot, layouts, requireComplete,
            projectionRadius, infantryCapacity, matchBudget, costBudget);
    }

    PlanResult PlanCompactPacking(const Snapshot& snapshot)
    {
        PlanResult best = Reject(snapshot);
        if (snapshot.mActors.size() > MAX_ACTORS) return Reject(snapshot, true);
        if (snapshot.mActors.empty() || !IsFinite(snapshot.mCenter)) return best;
        WorkBudget matchBudget {MAX_MATCH_WORK};
        WorkBudget costBudget {MAX_COST_WORK};
        const auto tryCapacity = [&](std::uint8_t capacity, double radius)
        {
            std::map<PlaneKey, std::vector<ActorProfile>> profiles;
            for (const auto& actor : snapshot.mActors)
            {
                const auto key = KeyFor(actor);
                profiles[key].push_back({actor.mId, actor.mKind, actor.mPlane, key.mOnBridge});
            }
            std::vector<PlaneLayout> layouts;
            for (const auto& [key, actors] : profiles)
                layouts.push_back({key.mPlane, key.mOnBridge,
                    CompactCenters(snapshot.mCenter,
                        RequiredCellCount(actors, key.mPlane, key.mOnBridge, capacity))});
            return PlanCellPackingImpl(snapshot, layouts, false, radius, capacity,
                matchBudget, costBudget);
        };
        for (std::uint8_t capacity : {std::uint8_t {3}, std::uint8_t {2}, std::uint8_t {1}})
        {
            for (const double radius : {2.0, 4.0, 8.0, 16.0, 32.0})
            {
                auto candidate = tryCapacity(capacity, radius);
                if (candidate.mBudgetExceeded) return Reject(snapshot, true);
                if (candidate.mAssignments.size() > best.mAssignments.size() ||
                    (candidate.mAssignments.size() == best.mAssignments.size() &&
                        candidate.mAssignments.size() > 0 &&
                        ScoreCompact(snapshot, candidate) < ScoreCompact(snapshot, best)))
                {
                    best = std::move(candidate);
                }
                if (best.mAssignments.size() == snapshot.mActors.size()) return best;
            }
        }
        return best;
    }
}
