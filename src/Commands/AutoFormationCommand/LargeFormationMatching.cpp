#include "Commands/AutoFormationCommand/LargeFormationMatching.h"
#include "Commands/AutoFormationCommand/FormationLimits.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

namespace ra_commands::auto_formation
{
    namespace
    {
        constexpr int NONE = -1;
        constexpr int MAP_AXIS = 512;
        struct WorkBudget
        {
            std::size_t mRemaining = MAX_LARGE_FORMATION_WORK;
            bool mExceeded = false;
            bool Consume()
            {
                if (!mRemaining) { mExceeded = true; return false; }
                --mRemaining;
                return true;
            }
        };
        struct Plane
        {
            OccupancyPlane mPlane;
            bool mBridge;
            bool operator<(const Plane& other) const
            { return std::tie(mPlane, mBridge) < std::tie(other.mPlane, other.mBridge); }
            bool operator==(const Plane&) const = default;
        };
        struct Resource
        {
            Plane mPlane;
            Cell mCell;
            bool operator<(const Resource& other) const
            {
                return std::tie(mPlane.mPlane, mPlane.mBridge, mCell.mX, mCell.mY) <
                    std::tie(other.mPlane.mPlane, other.mPlane.mBridge, other.mCell.mX, other.mCell.mY);
            }
        };
        bool CellLess(const Cell& a, const Cell& b)
        { return std::tie(a.mX, a.mY, a.mOnBridge) < std::tie(b.mX, b.mY, b.mOnBridge); }
        bool Finite(Center center)
        {
            return std::isfinite(center.mX) && std::isfinite(center.mY)
                && center.mX >= std::numeric_limits<std::int32_t>::min()
                && center.mX <= std::numeric_limits<std::int32_t>::max()
                && center.mY >= std::numeric_limits<std::int32_t>::min()
                && center.mY <= std::numeric_limits<std::int32_t>::max();
        }
        double Distance(Center a, Center b)
        { const double x = a.mX - b.mX, y = a.mY - b.mY; return x * x + y * y; }
        Center CellCenter(Cell cell) { return {static_cast<double>(cell.mX), static_cast<double>(cell.mY)}; }
        PlanResult Reject(const Snapshot& snapshot, bool budget)
        {
            PlanResult result;
            result.mCenter = snapshot.mCenter;
            result.mBudgetExceeded = budget;
            for (const auto& actor : snapshot.mActors) result.mUnassignedActors.push_back(actor.mId);
            std::sort(result.mUnassignedActors.begin(), result.mUnassignedActors.end());
            return result;
        }
        struct Candidate { Cell mCell; std::uint8_t mAvailable; };
        struct Actor
        {
            const ActorSnapshot* mSource;
            Plane mPlane;
            std::vector<Candidate> mCandidates;
        };
        struct Request
        {
            Plane mPlane;
            std::vector<const Actor*> mMembers;
            std::vector<Cell> mCandidates;
            Center mOrigin;
        };
        struct Axes
        {
            Center mWidth{1, 0};
            Center mDepth{0, 1};
            std::pair<double, double> Key(Center point) const
            {
                return {point.mX * mDepth.mX + point.mY * mDepth.mY,
                    point.mX * mWidth.mX + point.mY * mWidth.mY};
            }
        };
        Axes GetAxes(const std::vector<Center>& centers)
        {
            Axes axes;
            if (centers.size() < 2) return axes;
            const double x = centers[1].mX - centers[0].mX, y = centers[1].mY - centers[0].mY;
            const double length = std::hypot(x, y);
            if (!std::isfinite(length) || length <= 0) return axes;
            axes.mWidth = {x / length, y / length};
            axes.mDepth = {-axes.mWidth.mY, axes.mWidth.mX};
            for (const auto center : centers)
            {
                const double component = (center.mX - centers[0].mX) * axes.mDepth.mX
                    + (center.mY - centers[0].mY) * axes.mDepth.mY;
                if (std::abs(component) < 0.000001) continue;
                if (component < 0) { axes.mDepth.mX = -axes.mDepth.mX; axes.mDepth.mY = -axes.mDepth.mY; }
                break;
            }
            return axes;
        }

        bool Normalize(const Snapshot& snapshot, std::vector<Actor>& outActors, WorkBudget& budget)
        {
            if (snapshot.mActors.empty() || snapshot.mActors.size() > MAX_FORMATION_ACTORS
                || !Finite(snapshot.mCenter)) return false;
            std::size_t edges = 0;
            std::set<ActorId> ids;
            outActors.reserve(snapshot.mActors.size());
            for (const auto& source : snapshot.mActors)
            {
                if (!source.mId || !ids.insert(source.mId).second || !Finite(source.mPosition)
                    || (source.mKind != ActorKind::Vehicle && source.mKind != ActorKind::Infantry)
                    || (source.mPlane != OccupancyPlane::Ground && source.mPlane != OccupancyPlane::Air)
                    || (source.mPlane == OccupancyPlane::Air && source.mCurrentCell.mOnBridge)) return false;
                if (source.mCandidates.size() > MAX_FORMATION_CANDIDATES_PER_ACTOR
                    || source.mCandidates.size() > MAX_FORMATION_RAW_EDGES - edges
                    || source.mCapacityLimits.size() > MAX_FORMATION_CANDIDATES_PER_ACTOR)
                { budget.mExceeded = true; return false; }
                edges += source.mCandidates.size();
                Actor actor{&source, {source.mPlane, source.mCurrentCell.mOnBridge}, {}};
                auto cells = source.mCandidates;
                const auto compareCell = [&](const Cell& a, const Cell& b)
                { (void)budget.Consume(); return CellLess(a, b); };
                if (!std::is_sorted(cells.begin(), cells.end(), compareCell))
                    std::sort(cells.begin(), cells.end(), compareCell);
                cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
                auto limits = source.mCapacityLimits;
                const auto compareLimit = [&](const CandidateCapacity& a, const CandidateCapacity& b)
                { (void)budget.Consume(); return CellLess(a.mCell, b.mCell); };
                if (!std::is_sorted(limits.begin(), limits.end(), compareLimit))
                    std::sort(limits.begin(), limits.end(), compareLimit);
                std::size_t limit = 0;
                for (const auto cell : cells)
                {
                    if (!budget.Consume()) return false;
                    if (cell.mX < 0 || cell.mX >= MAP_AXIS || cell.mY < 0 || cell.mY >= MAP_AXIS
                        || cell.mOnBridge != actor.mPlane.mBridge) continue;
                    unsigned available = source.mKind == ActorKind::Infantry ? 3 : 1;
                    while (limit < limits.size() && CellLess(limits[limit].mCell, cell))
                    { if (!budget.Consume()) return false; ++limit; }
                    std::size_t same = limit;
                    while (same < limits.size() && limits[same].mCell == cell)
                    { if (!budget.Consume()) return false; available = std::min(available, unsigned(limits[same++].mAvailable)); }
                    limit = same;
                    if (available) actor.mCandidates.push_back({cell, static_cast<std::uint8_t>(available)});
                }
                if (budget.mExceeded) return false;
                outActors.push_back(std::move(actor));
            }
            std::sort(outActors.begin(), outActors.end(), [](const Actor& a, const Actor& b)
                { return a.mSource->mId < b.mSource->mId; });
            return true;
        }

        bool BuildRequests(const std::vector<Actor>& actors, std::uint8_t capacity,
            const std::map<Plane, Axes>& axes, std::vector<Request>& outRequests, WorkBudget& budget)
        {
            std::map<Plane, std::vector<const Actor*>> groups;
            for (const auto& actor : actors) groups[actor.mPlane].push_back(&actor);
            for (auto& [plane, group] : groups)
            {
                const auto found = axes.find(plane);
                if (found == axes.end()) return false;
                const auto compareActor = [&](const Actor* a, const Actor* b)
                {
                    (void)budget.Consume();
                    return std::tuple{found->second.Key(a->mSource->mPosition), a->mSource->mId}
                        < std::tuple{found->second.Key(b->mSource->mPosition), b->mSource->mId};
                };
                std::sort(group.begin(), group.end(), compareActor);
                std::vector<const Actor*> infantry;
                const auto append = [&](std::vector<const Actor*> members)
                {
                    Request request{plane, std::move(members), {}, {}};
                    const auto size = request.mMembers.size();
                    for (const auto* member : request.mMembers)
                    { request.mOrigin.mX += member->mSource->mPosition.mX / size; request.mOrigin.mY += member->mSource->mPosition.mY / size; }
                    for (const auto candidate : request.mMembers.front()->mCandidates)
                    { if (!budget.Consume()) return false; if (candidate.mAvailable >= size) request.mCandidates.push_back(candidate.mCell); }
                    for (std::size_t member = 1; member < size; ++member)
                    {
                        std::vector<Cell> intersection;
                        std::size_t next = 0;
                        const auto& candidates = request.mMembers[member]->mCandidates;
                        for (const auto cell : request.mCandidates)
                        {
                            while (next < candidates.size() && CellLess(candidates[next].mCell, cell))
                            { if (!budget.Consume()) return false; ++next; }
                            if (!budget.Consume()) return false;
                            if (next < candidates.size() && candidates[next].mCell == cell && candidates[next].mAvailable >= size)
                                intersection.push_back(cell);
                        }
                        request.mCandidates = std::move(intersection);
                    }
                    outRequests.push_back(std::move(request));
                    return !budget.mExceeded;
                };
                for (const auto* actor : group)
                {
                    if (actor->mSource->mKind == ActorKind::Infantry) infantry.push_back(actor);
                    else if (!append({actor})) return false;
                }
                for (std::size_t start = 0; start < infantry.size(); start += capacity)
                {
                    const auto end = std::min(infantry.size(), start + capacity);
                    if (!append(std::vector<const Actor*>(infantry.begin() + start, infantry.begin() + end))) return false;
                }
            }
            return !budget.mExceeded;
        }

        // 只求完整单位容量流，完全去掉巨大人数优先权和全局移动成本。
        // 所有请求都完成才提交，因此不存在用较小成本换掉某个演员的权重取舍。
        class FlowGraph
        {
        public:
            struct Edge { int mTo; int mReverse; int mCapacity; };
            explicit FlowGraph(std::size_t nodes) : mEdges(nodes), mDepth(nodes), mNext(nodes) {}
            bool Add(int from, int to, WorkBudget& budget)
            {
                if (mForwardEdges >= MAX_LARGE_FORMATION_FLOW_EDGES || !budget.Consume())
                { budget.mExceeded = true; return false; }
                const int reverse = static_cast<int>(mEdges[to].size());
                const int forward = static_cast<int>(mEdges[from].size());
                mEdges[from].push_back({to, reverse, 1});
                mEdges[to].push_back({from, forward, 0});
                ++mForwardEdges;
                return true;
            }
            bool Run(int source, int sink, std::size_t required, WorkBudget& budget)
            {
                std::size_t flow = 0;
                while (flow < required)
                {
                    std::fill(mDepth.begin(), mDepth.end(), NONE);
                    std::queue<int> pending;
                    pending.push(source); mDepth[source] = 0;
                    while (!pending.empty())
                    {
                        const int node = pending.front(); pending.pop();
                        for (const auto& edge : mEdges[node])
                        {
                            if (!budget.Consume()) return false;
                            if (edge.mCapacity && mDepth[edge.mTo] == NONE)
                            { mDepth[edge.mTo] = mDepth[node] + 1; pending.push(edge.mTo); }
                        }
                    }
                    if (mDepth[sink] == NONE) return false;
                    std::fill(mNext.begin(), mNext.end(), 0);
                    while (flow < required && Augment(source, sink, budget)) ++flow;
                    if (budget.mExceeded) return false;
                }
                return true;
            }
            const std::vector<Edge>& Edges(int node) const { return mEdges[node]; }
        private:
            bool Augment(int node, int sink, WorkBudget& budget)
            {
                if (node == sink) return true;
                for (auto& next = mNext[node]; next < mEdges[node].size(); ++next)
                {
                    if (!budget.Consume()) return false;
                    auto& edge = mEdges[node][next];
                    if (edge.mCapacity && mDepth[edge.mTo] == mDepth[node] + 1 && Augment(edge.mTo, sink, budget))
                    { --edge.mCapacity; ++mEdges[edge.mTo][edge.mReverse].mCapacity; return true; }
                    if (budget.mExceeded) return false;
                }
                return false;
            }
            std::vector<std::vector<Edge>> mEdges;
            std::vector<int> mDepth;
            std::vector<std::size_t> mNext;
            std::size_t mForwardEdges = 0;
        };

        struct Projection { int mSlot; double mDistance; };
        using ProjectedCells = std::map<Resource, std::vector<Projection>>;
        bool Project(const std::vector<PlaneLayout>& layouts, double radius,
            ProjectedCells& outCells, std::map<Plane, std::vector<int>>& outSlots,
            WorkBudget& budget)
        {
            int slot = 0;
            for (const auto& layout : layouts)
            {
                const Plane plane{layout.mPlane, layout.mOnBridge};
                auto& slots = outSlots[plane];
                for (const auto center : layout.mCellCenters)
                {
                    if (!Finite(center) || center.mX < -32 || center.mX > 543 || center.mY < -32 || center.mY > 543) return false;
                    slots.push_back(slot);
                    const int lowX = std::max(0, static_cast<int>(std::ceil(center.mX - radius)));
                    const int highX = std::min(MAP_AXIS - 1, static_cast<int>(std::floor(center.mX + radius)));
                    const int lowY = std::max(0, static_cast<int>(std::ceil(center.mY - radius)));
                    const int highY = std::min(MAP_AXIS - 1, static_cast<int>(std::floor(center.mY + radius)));
                    for (int x = lowX; x <= highX; ++x)
                        for (int y = lowY; y <= highY; ++y)
                        {
                            if (!budget.Consume()) return false;
                            const double distance = Distance(center, {double(x), double(y)});
                            if (distance > radius * radius) continue;
                            outCells[{plane, {x, y, plane.mBridge}}].push_back({slot, distance});
                            if (outCells.size() > MAX_FORMATION_RESOURCE_CELLS)
                            { budget.mExceeded = true; return false; }
                        }
                    ++slot;
                }
            }
            return true;
        }

        bool SameCandidates(const std::vector<const Request*>& requests, WorkBudget& budget)
        {
            for (const auto* request : requests)
            {
                if (request->mCandidates.size() != requests.front()->mCandidates.size()) return false;
                for (std::size_t i = 0; i < request->mCandidates.size(); ++i)
                { if (!budget.Consume() || request->mCandidates[i] != requests.front()->mCandidates[i]) return false; }
            }
            return true;
        }
        void Expand(const Request& request, Cell destination, PlanResult& result)
        {
            for (std::size_t seat = 0; seat < request.mMembers.size(); ++seat)
                result.mAssignments.push_back({request.mMembers[seat]->mSource->mId, destination,
                    request.mMembers[seat]->mSource->mKind, request.mPlane.mPlane, static_cast<std::uint8_t>(seat)});
        }

        bool UniformMatch(const std::vector<const Request*>& requests, const std::vector<int>& slots,
            const ProjectedCells& projected, Plane plane, const Axes& axes,
            PlanResult& result, WorkBudget& budget)
        {
            std::vector<Cell> cells;
            std::vector<const std::vector<Projection>*> projections;
            for (const auto cell : requests.front()->mCandidates)
            {
                if (!budget.Consume()) return false;
                const auto found = projected.find({plane, cell});
                if (found != projected.end()) { cells.push_back(cell); projections.push_back(&found->second); }
            }
            if (cells.size() < slots.size()) return false;
            const int slotStart = 1, cellStart = slotStart + static_cast<int>(slots.size());
            const int sink = cellStart + static_cast<int>(cells.size());
            FlowGraph graph(static_cast<std::size_t>(sink) + 1);
            std::map<int, int> slotIndices;
            for (std::size_t i = 0; i < slots.size(); ++i) slotIndices.emplace(slots[i], static_cast<int>(i));
            std::vector<std::vector<std::pair<double, int>>> slotCells(slots.size());
            for (std::size_t cell = 0; cell < cells.size(); ++cell)
                for (const auto projection : *projections[cell])
                { if (!budget.Consume()) return false; slotCells[slotIndices.at(projection.mSlot)].push_back({projection.mDistance, static_cast<int>(cell)}); }
            for (std::size_t i = 0; i < slots.size(); ++i)
            {
                if (!graph.Add(0, slotStart + static_cast<int>(i), budget)) return false;
                std::sort(slotCells[i].begin(), slotCells[i].end());
                for (const auto [distance, cell] : slotCells[i])
                { (void)distance; if (!graph.Add(slotStart + static_cast<int>(i), cellStart + cell, budget)) return false; }
            }
            for (std::size_t i = 0; i < cells.size(); ++i)
                if (!graph.Add(cellStart + static_cast<int>(i), sink, budget)) return false;
            if (!graph.Run(0, sink, slots.size(), budget)) return false;
            std::vector<Cell> chosen;
            for (std::size_t i = 0; i < cells.size(); ++i)
                for (const auto& edge : graph.Edges(cellStart + static_cast<int>(i)))
                    if (edge.mTo == sink && !edge.mCapacity) chosen.push_back(cells[i]);
            auto ordered = requests;
            std::sort(ordered.begin(), ordered.end(), [&](const Request* a, const Request* b)
                { return std::tuple{axes.Key(a->mOrigin), a->mMembers.front()->mSource->mId}
                    < std::tuple{axes.Key(b->mOrigin), b->mMembers.front()->mSource->mId}; });
            std::sort(chosen.begin(), chosen.end(), [&](Cell a, Cell b)
                { return std::tuple{axes.Key(CellCenter(a)), a.mX, a.mY}
                    < std::tuple{axes.Key(CellCenter(b)), b.mX, b.mY}; });
            std::vector<bool> used(chosen.size());
            std::set<const Request*> stationary;
            for (const auto* request : ordered)
            {
                const auto current = request->mMembers.front()->mSource->mCurrentCell;
                if (!std::all_of(request->mMembers.begin(), request->mMembers.end(), [&](const Actor* member)
                    { return member->mSource->mCurrentCell == current; })) continue;
                for (std::size_t cell = 0; cell < chosen.size(); ++cell)
                {
                    if (!budget.Consume()) return false;
                    if (!used[cell] && chosen[cell] == current)
                    { Expand(*request, chosen[cell], result); used[cell] = true; stationary.insert(request); break; }
                }
            }
            std::size_t next = 0;
            for (const auto* request : ordered)
            {
                if (stationary.contains(request)) continue;
                while (used[next]) ++next;
                Expand(*request, chosen[next], result); used[next++] = true;
            }
            return true;
        }

        bool GeneralMatch(const std::vector<const Request*>& requests, const std::vector<int>& slots,
            const ProjectedCells& projected, Plane plane, PlanResult& result, WorkBudget& budget)
        {
            std::map<Cell, int, decltype(&CellLess)> indices(&CellLess);
            for (const auto* request : requests)
                for (const auto cell : request->mCandidates)
                { if (!budget.Consume()) return false; if (projected.contains({plane, cell})) indices.try_emplace(cell, NONE); }
            if (indices.size() < requests.size()) return false;
            std::vector<Cell> cells;
            for (auto& [cell, index] : indices) { index = static_cast<int>(cells.size()); cells.push_back(cell); }
            const int requestStart = 1, cellInStart = requestStart + static_cast<int>(requests.size());
            const int cellOutStart = cellInStart + static_cast<int>(cells.size());
            const int slotStart = cellOutStart + static_cast<int>(cells.size());
            const int sink = slotStart + static_cast<int>(slots.size());
            FlowGraph graph(static_cast<std::size_t>(sink) + 1);
            for (std::size_t i = 0; i < requests.size(); ++i)
            {
                if (!graph.Add(0, requestStart + static_cast<int>(i), budget)) return false;
                std::vector<int> preferred;
                for (const auto cell : requests[i]->mCandidates)
                { if (!budget.Consume()) return false; const auto found = indices.find(cell); if (found != indices.end()) preferred.push_back(found->second); }
                const auto current = requests[i]->mMembers.front()->mSource->mCurrentCell;
                const bool shareCurrent = std::all_of(requests[i]->mMembers.begin(), requests[i]->mMembers.end(),
                    [&](const Actor* actor) { return actor->mSource->mCurrentCell == current; });
                std::sort(preferred.begin(), preferred.end(), [&](int a, int b)
                {
                    (void)budget.Consume();
                    return std::tuple{!(shareCurrent && cells[a] == current), Distance(requests[i]->mOrigin, CellCenter(cells[a])), a}
                        < std::tuple{!(shareCurrent && cells[b] == current), Distance(requests[i]->mOrigin, CellCenter(cells[b])), b};
                });
                if (budget.mExceeded) return false;
                for (const int cell : preferred)
                    if (!graph.Add(requestStart + static_cast<int>(i), cellInStart + cell, budget)) return false;
            }
            std::map<int, int> slotIndices;
            for (std::size_t i = 0; i < slots.size(); ++i) slotIndices.emplace(slots[i], static_cast<int>(i));
            for (std::size_t cell = 0; cell < cells.size(); ++cell)
            {
                if (!graph.Add(cellInStart + static_cast<int>(cell), cellOutStart + static_cast<int>(cell), budget)) return false;
                auto projections = projected.at({plane, cells[cell]});
                std::sort(projections.begin(), projections.end(), [](Projection a, Projection b)
                    { return std::tie(a.mDistance, a.mSlot) < std::tie(b.mDistance, b.mSlot); });
                for (const auto projection : projections)
                    if (!graph.Add(cellOutStart + static_cast<int>(cell), slotStart + slotIndices.at(projection.mSlot), budget)) return false;
            }
            for (std::size_t slot = 0; slot < slots.size(); ++slot)
                if (!graph.Add(slotStart + static_cast<int>(slot), sink, budget)) return false;
            if (!graph.Run(0, sink, requests.size(), budget)) return false;
            for (std::size_t i = 0; i < requests.size(); ++i)
                for (const auto& edge : graph.Edges(requestStart + static_cast<int>(i)))
                    if (edge.mTo >= cellInStart && edge.mTo < cellOutStart && !edge.mCapacity)
                    { Expand(*requests[i], cells[edge.mTo - cellInStart], result); break; }
            return true;
        }

        PlanResult Match(const Snapshot& snapshot, const std::vector<Request>& requests,
            const std::vector<PlaneLayout>& layouts, double radius, WorkBudget& budget)
        {
            if (!std::isfinite(radius) || radius <= 0 || radius > 32) return Reject(snapshot, false);
            std::map<Plane, const PlaneLayout*> byPlane;
            for (const auto& layout : layouts)
            {
                if ((layout.mPlane != OccupancyPlane::Ground && layout.mPlane != OccupancyPlane::Air)
                    || (layout.mPlane == OccupancyPlane::Air && layout.mOnBridge)
                    || layout.mCellCenters.empty() || layout.mCellCenters.size() > MAX_FORMATION_ACTORS
                    || !byPlane.emplace(Plane{layout.mPlane, layout.mOnBridge}, &layout).second)
                    return Reject(snapshot, false);
            }
            std::map<Plane, std::vector<const Request*>> groups;
            for (const auto& request : requests) groups[request.mPlane].push_back(&request);
            if (std::any_of(requests.begin(), requests.end(), [](const Request& request)
                { return request.mCandidates.empty(); })) return Reject(snapshot, false);
            if (groups.size() != byPlane.size()) return Reject(snapshot, false);
            for (const auto& [plane, group] : groups)
                if (!byPlane.contains(plane) || byPlane.at(plane)->mCellCenters.size() != group.size())
                    return Reject(snapshot, false);
            ProjectedCells projected;
            std::map<Plane, std::vector<int>> slots;
            if (!Project(layouts, radius, projected, slots, budget)) return Reject(snapshot, budget.mExceeded);
            PlanResult result; result.mCenter = snapshot.mCenter;
            for (const auto& [plane, group] : groups)
            {
                const bool uniform = SameCandidates(group, budget);
                if (budget.mExceeded) return Reject(snapshot, true);
                const bool completed = uniform ? UniformMatch(group, slots.at(plane), projected, plane,
                    GetAxes(byPlane.at(plane)->mCellCenters), result, budget)
                    : GeneralMatch(group, slots.at(plane), projected, plane, result, budget);
                if (!completed) return Reject(snapshot, budget.mExceeded);
            }
            if (result.mAssignments.size() != snapshot.mActors.size() || !HasValidReservations(result.mAssignments))
                return Reject(snapshot, false);
            std::sort(result.mAssignments.begin(), result.mAssignments.end(), [](const Assignment& a, const Assignment& b)
                { return a.mActor < b.mActor; });
            return result;
        }

        std::vector<Center> CompactCenters(Center center, std::size_t count)
        {
            std::vector<Center> result;
            const auto columns = static_cast<std::size_t>(std::ceil(std::sqrt(double(count))));
            const auto rows = (count - 1) / columns + 1;
            for (std::size_t row = 0; row < rows; ++row)
            {
                const auto width = std::min(columns, count - row * columns);
                for (std::size_t column = 0; column < width; ++column)
                    result.push_back({center.mX + double(column) - double(width - 1) / 2,
                        center.mY + double(row) - double(rows - 1) / 2});
            }
            return result;
        }
    }

    PlanResult PlanLargeCellPacking(const Snapshot& snapshot, const std::vector<PlaneLayout>& layouts,
        double projectionRadius, std::uint8_t infantryCapacity)
    {
        WorkBudget budget;
        if (snapshot.mActors.size() > MAX_FORMATION_ACTORS) return Reject(snapshot, true);
        if (!infantryCapacity || infantryCapacity > 3) return Reject(snapshot, false);
        std::vector<Actor> actors;
        if (!Normalize(snapshot, actors, budget)) return Reject(snapshot, budget.mExceeded);
        std::map<Plane, Axes> axes;
        for (const auto& layout : layouts) axes.emplace(Plane{layout.mPlane, layout.mOnBridge}, GetAxes(layout.mCellCenters));
        std::vector<Request> requests;
        if (!BuildRequests(actors, infantryCapacity, axes, requests, budget)) return Reject(snapshot, budget.mExceeded);
        return Match(snapshot, requests, layouts, projectionRadius, budget);
    }

    PlanResult PlanLargeCompactPacking(const Snapshot& snapshot)
    {
        WorkBudget budget;
        if (snapshot.mActors.size() > MAX_FORMATION_ACTORS) return Reject(snapshot, true);
        std::vector<Actor> actors;
        if (!Normalize(snapshot, actors, budget)) return Reject(snapshot, budget.mExceeded);
        std::map<Plane, Axes> axes;
        for (const auto& actor : actors) axes.try_emplace(actor.mPlane);
        for (const std::uint8_t capacity : {std::uint8_t{3}, std::uint8_t{2}, std::uint8_t{1}})
        {
            std::vector<Request> requests;
            if (!BuildRequests(actors, capacity, axes, requests, budget)) return Reject(snapshot, budget.mExceeded);
            if (std::any_of(requests.begin(), requests.end(), [](const Request& request)
                { return request.mCandidates.empty(); })) continue;
            std::map<Plane, std::size_t> demand;
            for (const auto& request : requests) ++demand[request.mPlane];
            std::vector<PlaneLayout> layouts;
            for (const auto& [plane, count] : demand)
                layouts.push_back({plane.mPlane, plane.mBridge, CompactCenters(snapshot.mCenter, count)});
            for (const double radius : {2.0, 4.0, 8.0, 16.0, 32.0})
            {
                auto result = Match(snapshot, requests, layouts, radius, budget);
                if (result.mBudgetExceeded || result.mAssignments.size() == snapshot.mActors.size()) return result;
            }
        }
        return Reject(snapshot, false);
    }

    std::vector<FormationCandidateAnchor> BuildFormationCandidateAnchors(const Snapshot& snapshot,
        const std::vector<PlaneLayout>& planes, std::uint8_t infantryCapacity)
    {
        if (!infantryCapacity || infantryCapacity > 3) return {};
        WorkBudget budget;
        std::vector<Actor> actors;
        if (!Normalize(snapshot, actors, budget)) return {};
        std::vector<PlaneLayout> layouts = planes;
        if (layouts.empty())
        {
            std::map<Plane, std::pair<std::size_t, std::size_t>> counts;
            for (const auto& actor : actors)
            {
                auto& [vehicles, infantry] = counts[actor.mPlane];
                if (actor.mSource->mKind == ActorKind::Vehicle) ++vehicles;
                else ++infantry;
            }
            for (const auto& [plane, count] : counts)
            {
                const auto demand = count.first + (count.second + infantryCapacity - 1) / infantryCapacity;
                layouts.push_back({plane.mPlane, plane.mBridge, CompactCenters(snapshot.mCenter, demand)});
            }
        }
        std::map<Plane, Axes> axes;
        std::map<Plane, const PlaneLayout*> layoutByPlane;
        for (const auto& layout : layouts)
        {
            const Plane plane{layout.mPlane, layout.mOnBridge};
            if (layout.mCellCenters.empty() || layout.mCellCenters.size() > MAX_FORMATION_ACTORS
                || !std::all_of(layout.mCellCenters.begin(), layout.mCellCenters.end(), Finite)
                || !layoutByPlane.emplace(plane, &layout).second) return {};
            axes.emplace(plane, GetAxes(layout.mCellCenters));
        }
        std::vector<Request> requests;
        if (!BuildRequests(actors, infantryCapacity, axes, requests, budget)) return {};
        std::map<Plane, std::vector<const Request*>> groups;
        for (const auto& request : requests) groups[request.mPlane].push_back(&request);
        if (groups.size() != layouts.size()) return {};
        std::vector<FormationCandidateAnchor> anchors;
        anchors.reserve(actors.size());
        for (auto& [plane, group] : groups)
        {
            const auto found = layoutByPlane.find(plane);
            if (found == layoutByPlane.end() || found->second->mCellCenters.size() != group.size()) return {};
            const auto& axis = axes.at(plane);
            auto centers = found->second->mCellCenters;
            std::sort(group.begin(), group.end(), [&](const Request* a, const Request* b)
                { return std::tuple{axis.Key(a->mOrigin), a->mMembers.front()->mSource->mId}
                    < std::tuple{axis.Key(b->mOrigin), b->mMembers.front()->mSource->mId}; });
            std::sort(centers.begin(), centers.end(), [&](Center a, Center b)
                { return std::tuple{axis.Key(a), a.mX, a.mY} < std::tuple{axis.Key(b), b.mX, b.mY}; });
            for (std::size_t i = 0; i < group.size(); ++i)
                for (const auto* member : group[i]->mMembers) anchors.push_back({member->mSource->mId, centers[i]});
        }
        std::sort(anchors.begin(), anchors.end(), [](const auto& a, const auto& b) { return a.mActor < b.mActor; });
        return anchors;
    }
}
