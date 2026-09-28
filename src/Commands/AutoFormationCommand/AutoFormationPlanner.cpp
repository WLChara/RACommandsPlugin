#include "Commands/AutoFormationCommand/AutoFormationPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ra_commands::auto_formation
{
    namespace
    {
        // 对应一次热键快照的上限；预算按候选边检查计数，不依赖机器时钟。
        constexpr std::size_t MAX_ACTORS = 256;
        constexpr std::size_t MAX_CANDIDATES_PER_ACTOR = 4096;
        constexpr std::size_t MAX_EDGES = 262144;
        constexpr std::size_t MAX_CELLS = 8192;
        constexpr std::size_t MATCH_WORK_LIMIT = 12000000;
        constexpr std::size_t REFINE_WORK_LIMIT = 2000000;
        constexpr std::size_t CURRENT_SLOT_WORK_LIMIT = 2000000;
        constexpr std::size_t EXACT_COST_ACTORS = 32;
        constexpr int NONE = -1;

        bool ValidPosition(const Center& position)
        {
            constexpr double MIN_COORD = std::numeric_limits<std::int32_t>::min();
            constexpr double MAX_COORD = std::numeric_limits<std::int32_t>::max();
            return std::isfinite(position.mX) && std::isfinite(position.mY) &&
                position.mX >= MIN_COORD && position.mX <= MAX_COORD &&
                position.mY >= MIN_COORD && position.mY <= MAX_COORD;
        }

        bool CellLess(const Cell& left, const Cell& right)
        {
            return std::tie(left.mX, left.mY, left.mOnBridge) <
                std::tie(right.mX, right.mY, right.mOnBridge);
        }

        struct CellHash
        {
            std::size_t operator()(const Cell& cell) const
            {
                const auto x = static_cast<std::uint32_t>(cell.mX);
                const auto y = static_cast<std::uint32_t>(cell.mY);
                return static_cast<std::size_t>((x * 73856093U) ^ (y * 19349663U) ^
                    (cell.mOnBridge ? 83492791U : 0U));
            }
        };

        std::map<ActorId, const ActorSnapshot*> FirstActors(const std::vector<ActorSnapshot>& actors)
        {
            std::map<ActorId, const ActorSnapshot*> result;
            for (const auto& actor : actors)
            {
                if (actor.mId != 0)
                {
                    result.emplace(actor.mId, &actor);
                }
            }
            return result;
        }

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

        struct Matching
        {
            std::vector<int> mActorCells;
            std::size_t mCount = 0;
            bool mComplete = true;
        };

        // Hopcroft-Karp：先保证最大分配数量，避免先占通用格而困住受限车辆。
        class Matcher
        {
        public:
            Matcher(const std::vector<std::vector<int>>& edges, const std::vector<bool>& allowed,
                WorkBudget& budget) : mEdges(edges), mAllowed(allowed), mBudget(budget),
                mActorCells(edges.size(), NONE), mCellActors(allowed.size(), NONE),
                mDepth(edges.size(), NONE)
            {
            }

            Matching Run()
            {
                std::size_t count = 0;
                while (BuildLayers())
                {
                    for (std::size_t actor = 0; actor < mEdges.size(); ++actor)
                    {
                        if (mActorCells[actor] == NONE && Augment(static_cast<int>(actor))) ++count;
                        if (!mComplete) return { std::move(mActorCells), count, false };
                    }
                }
                return { std::move(mActorCells), count, mComplete };
            }

        private:
            bool BuildLayers()
            {
                std::queue<int> pending;
                for (std::size_t actor = 0; actor < mEdges.size(); ++actor)
                {
                    mDepth[actor] = mActorCells[actor] == NONE ? 0 : NONE;
                    if (mDepth[actor] == 0) pending.push(static_cast<int>(actor));
                }
                bool found = false;
                while (!pending.empty())
                {
                    const int actor = pending.front();
                    pending.pop();
                    for (const int cell : mEdges[actor])
                    {
                        if (!mBudget.Consume()) { mComplete = false; return false; }
                        if (!mAllowed[cell]) continue;
                        const int next = mCellActors[cell];
                        if (next == NONE) found = true;
                        else if (mDepth[next] == NONE)
                        {
                            mDepth[next] = mDepth[actor] + 1;
                            pending.push(next);
                        }
                    }
                }
                return found;
            }

            bool Augment(int actor)
            {
                for (const int cell : mEdges[actor])
                {
                    if (!mBudget.Consume()) { mComplete = false; return false; }
                    if (!mAllowed[cell]) continue;
                    const int next = mCellActors[cell];
                    if (next == NONE || (mDepth[next] == mDepth[actor] + 1 && Augment(next)))
                    {
                        mActorCells[actor] = cell;
                        mCellActors[cell] = actor;
                        return true;
                    }
                    if (!mComplete) return false;
                }
                mDepth[actor] = NONE;
                return false;
            }

            const std::vector<std::vector<int>>& mEdges;
            const std::vector<bool>& mAllowed;
            WorkBudget& mBudget;
            std::vector<int> mActorCells;
            std::vector<int> mCellActors;
            std::vector<int> mDepth;
            bool mComplete = true;
        };

        struct Envelope
        {
            std::int64_t mX;
            std::int64_t mY;
            std::int64_t mWidth;
            std::int64_t mHeight;

            bool Contains(const Cell& cell) const
            {
                const auto x = static_cast<std::int64_t>(cell.mX) - mX;
                const auto y = static_cast<std::int64_t>(cell.mY) - mY;
                return x >= 0 && x < mWidth && y >= 0 && y < mHeight;
            }

            std::int64_t Depth(const Cell& cell) const
            {
                const auto x = static_cast<std::int64_t>(cell.mX) - mX;
                const auto y = static_cast<std::int64_t>(cell.mY) - mY;
                return std::min({ x, y, mWidth - 1 - x, mHeight - 1 - y });
            }
        };

        Envelope MakeEnvelope(const Center& center, std::int64_t width, std::int64_t height,
            int anchor)
        {
            const double x = center.mX - static_cast<double>(width - 1) / 2.0;
            const double y = center.mY - static_cast<double>(height - 1) / 2.0;
            return { static_cast<std::int64_t>((anchor & 1) ? std::ceil(x) : std::floor(x)),
                static_cast<std::int64_t>((anchor & 2) ? std::ceil(y) : std::floor(y)), width, height };
        }

        std::vector<bool> AllowedCells(const Envelope& envelope, const std::vector<Cell>& cells)
        {
            std::vector<bool> allowed(cells.size());
            for (std::size_t cell = 0; cell < cells.size(); ++cell)
                allowed[cell] = envelope.Contains(cells[cell]);
            return allowed;
        }

        long double CenterOffset(const Envelope& envelope, const Center& center)
        {
            const long double dx = envelope.mX + static_cast<long double>(envelope.mWidth - 1) / 2 - center.mX;
            const long double dy = envelope.mY + static_cast<long double>(envelope.mHeight - 1) / 2 - center.mY;
            return dx * dx + dy * dy;
        }

        // 三层成本：少留内部空格，再向更深的内层填充，最后减少总移动距离。
        // 深内层使用总深度评分，未枚举所有可能形状，不能宣称全局几何最优。
        struct Cost
        {
            std::int64_t mBoundary = 0;
            std::int64_t mDepth = 0;
            long double mMovement = 0;

            Cost operator+(const Cost& other) const
            {
                return { mBoundary + other.mBoundary, mDepth + other.mDepth,
                    mMovement + other.mMovement };
            }
            Cost operator-(const Cost& other) const
            {
                return { mBoundary - other.mBoundary, mDepth - other.mDepth,
                    mMovement - other.mMovement };
            }
            bool operator<(const Cost& other) const
            {
                return std::tie(mBoundary, mDepth, mMovement) <
                    std::tie(other.mBoundary, other.mDepth, other.mMovement);
            }
        };

        constexpr Cost INFINITE_COST { 1000000000000LL, 0, 0 };

        Cost AssignmentCost(const ActorSnapshot& actor, const Cell& cell, const Envelope& envelope)
        {
            const auto depth = envelope.Depth(cell);
            const auto maxDepth = (std::min(envelope.mWidth, envelope.mHeight) - 1) / 2;
            const long double dx = static_cast<long double>(actor.mPosition.mX) - cell.mX;
            const long double dy = static_cast<long double>(actor.mPosition.mY) - cell.mY;
            return { depth == 0 ? 1 : 0, maxDepth - depth, dx * dx + dy * dy };
        }

        Cost TotalCost(const Matching& matching, const std::vector<const ActorSnapshot*>& actors,
            const std::vector<Cell>& cells, const Envelope& envelope)
        {
            Cost cost;
            for (std::size_t actor = 0; actor < actors.size(); ++actor)
                if (matching.mActorCells[actor] != NONE)
                    cost = cost + AssignmentCost(*actors[actor], cells[matching.mActorCells[actor]], envelope);
            return cost;
        }

        // 按格层次构造匹配基：每次增广保留所有已选格，因此先填可用内部格。
        // 这是匹配拟阵的贪心基，不会因通用车辆先占格而丢失最大匹配数量。
        class InteriorMatcher
        {
        public:
            InteriorMatcher(const std::vector<std::vector<int>>& cellActors, std::size_t actorCount,
                WorkBudget& budget) : mCellActors(cellActors), mActorCells(actorCount, NONE),
                mVisited(actorCount, false), mBudget(budget)
            {
            }

            Matching Run(const std::vector<int>& orderedCells, std::size_t count)
            {
                std::size_t assigned = 0;
                for (const int cell : orderedCells)
                {
                    std::fill(mVisited.begin(), mVisited.end(), false);
                    if (Augment(cell)) ++assigned;
                    if (!mComplete) return { {}, 0, false };
                    if (assigned == count) return { std::move(mActorCells), assigned, true };
                }
                return { {}, 0, false };
            }

        private:
            bool Augment(int cell)
            {
                // 先用空闲车辆，密集 256 车图只需约 N²/2 次检查。
                for (const int actor : mCellActors[cell])
                {
                    if (!mBudget.Consume()) { mComplete = false; return false; }
                    if (!mVisited[actor] && mActorCells[actor] == NONE)
                    {
                        mVisited[actor] = true;
                        mActorCells[actor] = cell;
                        return true;
                    }
                }
                for (const int actor : mCellActors[cell])
                {
                    if (!mBudget.Consume()) { mComplete = false; return false; }
                    if (mVisited[actor]) continue;
                    mVisited[actor] = true;
                    if (Augment(mActorCells[actor]))
                    {
                        mActorCells[actor] = cell;
                        return true;
                    }
                    if (!mComplete) return false;
                }
                return false;
            }

            const std::vector<std::vector<int>>& mCellActors;
            std::vector<int> mActorCells;
            std::vector<bool> mVisited;
            WorkBudget& mBudget;
            bool mComplete = true;
        };

        void ReduceMovement(Matching& matching, const std::vector<const ActorSnapshot*>& actors,
            const std::vector<Cell>& cells, const std::vector<std::vector<int>>& edges,
            const Envelope& envelope, WorkBudget& budget)
        {
            // 一轮确定性的两车交换/空闲车替换；不改变占用格，不能声称全局最短移动。
            for (std::size_t left = 0; left < actors.size(); ++left)
            {
                for (std::size_t right = left + 1; right < actors.size(); ++right)
                {
                    if (!budget.Consume()) return;
                    const int a = matching.mActorCells[left];
                    const int b = matching.mActorCells[right];
                    if (a == NONE && b == NONE) continue;
                    if (a != NONE && !std::binary_search(edges[right].begin(), edges[right].end(), a)) continue;
                    if (b != NONE && !std::binary_search(edges[left].begin(), edges[left].end(), b)) continue;
                    Cost oldCost;
                    Cost newCost;
                    if (a != NONE)
                    {
                        oldCost = oldCost + AssignmentCost(*actors[left], cells[a], envelope);
                        newCost = newCost + AssignmentCost(*actors[right], cells[a], envelope);
                    }
                    if (b != NONE)
                    {
                        oldCost = oldCost + AssignmentCost(*actors[right], cells[b], envelope);
                        newCost = newCost + AssignmentCost(*actors[left], cells[b], envelope);
                    }
                    if (newCost < oldCost) std::swap(matching.mActorCells[left], matching.mActorCells[right]);
                }
            }
        }

        // 最终槽集已经确定：只调整车与槽的对应关系，不改变形状、内部填充或分配数量。
        // 每次保留当前槽都经交替路径验证；受限车型无法重新安置时回滚，不能盲目强占。
        class CurrentSlotMatcher
        {
        public:
            CurrentSlotMatcher(const std::vector<const ActorSnapshot*>& actors,
                const std::vector<Cell>& cells, const std::vector<std::vector<int>>& edges,
                Matching& matching, WorkBudget& budget) : mActors(actors), mCells(cells),
                mEdges(edges), mMatching(matching), mBudget(budget),
                mCellActors(cells.size(), NONE), mSelectedCells(cells.size(), false),
                mLockedActors(actors.size(), false), mVisitedActors(actors.size(), false)
            {
                for (std::size_t actor = 0; actor < actors.size(); ++actor)
                {
                    const int cell = matching.mActorCells[actor];
                    if (cell != NONE)
                    {
                        mCellActors[cell] = static_cast<int>(actor);
                        mSelectedCells[cell] = true;
                        mLockedActors[actor] = cells[cell] == actors[actor]->mCurrentCell;
                    }
                }
            }

            void Run()
            {
                // ID 顺序的可行固定点启发式；不承诺受限候选图中的全局最多固定点。
                for (std::size_t actor = 0; actor < mActors.size(); ++actor)
                {
                    if (mLockedActors[actor]) continue;
                    const auto found = std::lower_bound(mCells.begin(), mCells.end(),
                        mActors[actor]->mCurrentCell, CellLess);
                    if (found == mCells.end() || *found != mActors[actor]->mCurrentCell) continue;
                    const int current = static_cast<int>(found - mCells.begin());
                    if (!mSelectedCells[current] ||
                        !std::binary_search(mEdges[actor].begin(), mEdges[actor].end(), current)) continue;
                    const int displaced = mCellActors[current];
                    if (mLockedActors[displaced]) continue;
                    if (!mBudget.Consume()) return;

                    auto trialActors = mMatching.mActorCells;
                    auto trialCells = mCellActors;
                    const int freed = trialActors[actor];
                    if (freed != NONE) trialCells[freed] = NONE;
                    trialActors[displaced] = NONE;
                    trialActors[actor] = current;
                    trialCells[current] = static_cast<int>(actor);
                    std::fill(mVisitedActors.begin(), mVisitedActors.end(), false);
                    mVisitedActors[actor] = true;
                    bool feasible = freed == NONE;
                    // 常见空地共享候选图可直接交换，避免重新扫描整个密集候选图。
                    if (!feasible && std::binary_search(mEdges[displaced].begin(), mEdges[displaced].end(), freed))
                    {
                        trialActors[displaced] = freed;
                        trialCells[freed] = displaced;
                        feasible = true;
                    }
                    if (!feasible)
                        feasible = Augment(displaced, trialActors, trialCells);
                    if (feasible)
                    {
                        mMatching.mActorCells = std::move(trialActors);
                        mCellActors = std::move(trialCells);
                        mLockedActors[actor] = true;
                        // 交替路径可能顺带让先前车辆回到自己的槽，也一并保护这些固定点。
                        for (std::size_t member = 0; member < mActors.size(); ++member)
                        {
                            const int cell = mMatching.mActorCells[member];
                            if (cell != NONE && mCells[cell] == mActors[member]->mCurrentCell)
                                mLockedActors[member] = true;
                        }
                    }
                    if (mBudget.mRemaining == 0) return;
                }
            }

        private:
            bool Augment(int actor, std::vector<int>& actorCells, std::vector<int>& cellActors)
            {
                mVisitedActors[actor] = true;
                for (const int cell : mEdges[actor])
                {
                    if (!mBudget.Consume()) return false;
                    if (!mSelectedCells[cell]) continue;
                    const int next = cellActors[cell];
                    if (next != NONE && (mLockedActors[next] || mVisitedActors[next])) continue;
                    if (next == NONE || Augment(next, actorCells, cellActors))
                    {
                        actorCells[actor] = cell;
                        cellActors[cell] = actor;
                        return true;
                    }
                }
                return false;
            }

            const std::vector<const ActorSnapshot*>& mActors;
            const std::vector<Cell>& mCells;
            const std::vector<std::vector<int>>& mEdges;
            Matching& mMatching;
            WorkBudget& mBudget;
            std::vector<int> mCellActors;
            std::vector<bool> mSelectedCells;
            std::vector<bool> mLockedActors;
            std::vector<bool> mVisitedActors;
        };

        // 仅对最多 32 车做矩形 Hungarian，加入 N-K 个虚格，保留已验证的最大数量 K。
        // 大队使用内部格增广与一轮两车交换，避免 O(N³) 次要移动优化阻塞主帧。
        // 非法边不会成为有效匹配；内部优先和移动成本用元组算术避免巨大权重溢出。
        Matching Refine(const std::vector<const ActorSnapshot*>& actors, const std::vector<Cell>& cells,
            const std::vector<std::vector<int>>& edges, const Envelope& envelope,
            std::size_t count, WorkBudget& budget)
        {
            if (actors.size() > EXACT_COST_ACTORS) return { {}, 0, false };
            std::vector<int> columns;
            for (std::size_t cell = 0; cell < cells.size(); ++cell)
                if (envelope.Contains(cells[cell])) columns.push_back(static_cast<int>(cell));
            const std::size_t realColumns = columns.size();
            const std::size_t columnCount = realColumns + actors.size() - count;
            const std::size_t rowCount = actors.size();
            if (realColumns > 0 && rowCount > budget.mRemaining / realColumns) return { {}, 0, false };
            std::vector<std::vector<Cost>> costs(rowCount, std::vector<Cost>(realColumns, INFINITE_COST));
            for (std::size_t actor = 0; actor < rowCount; ++actor)
            {
                for (std::size_t column = 0; column < realColumns; ++column)
                {
                    if (!budget.Consume()) return { {}, 0, false };
                    if (std::binary_search(edges[actor].begin(), edges[actor].end(), columns[column]))
                        costs[actor][column] = AssignmentCost(*actors[actor], cells[columns[column]], envelope);
                }
            }
            std::vector<Cost> rowPotential(rowCount + 1), columnPotential(columnCount + 1);
            std::vector<int> columnRows(columnCount + 1), previous(columnCount + 1);
            for (std::size_t row = 1; row <= rowCount; ++row)
            {
                columnRows[0] = static_cast<int>(row);
                std::size_t column = 0;
                std::vector<Cost> minimum(columnCount + 1, INFINITE_COST);
                std::vector<bool> used(columnCount + 1, false);
                do
                {
                    used[column] = true;
                    const int activeRow = columnRows[column];
                    Cost delta = INFINITE_COST;
                    std::size_t nextColumn = 0;
                    for (std::size_t next = 1; next <= columnCount; ++next)
                    {
                        if (!budget.Consume()) return { {}, 0, false };
                        if (used[next]) continue;
                        const Cost edge = next <= realColumns ? costs[activeRow - 1][next - 1] : Cost {};
                        const Cost reduced = edge - rowPotential[activeRow] - columnPotential[next];
                        if (reduced < minimum[next]) { minimum[next] = reduced; previous[next] = static_cast<int>(column); }
                        if (minimum[next] < delta) { delta = minimum[next]; nextColumn = next; }
                    }
                    if (nextColumn == 0) return { {}, 0, false };
                    for (std::size_t next = 0; next <= columnCount; ++next)
                    {
                        if (used[next])
                        {
                            rowPotential[columnRows[next]] = rowPotential[columnRows[next]] + delta;
                            columnPotential[next] = columnPotential[next] - delta;
                        }
                        else minimum[next] = minimum[next] - delta;
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
            Matching result { std::vector<int>(rowCount, NONE), 0, true };
            for (std::size_t column = 1; column <= realColumns; ++column)
            {
                if (columnRows[column] == 0) continue;
                const std::size_t row = static_cast<std::size_t>(columnRows[column] - 1);
                if (!(costs[row][column - 1] < INFINITE_COST)) return { {}, 0, false };
                result.mActorCells[row] = columns[column - 1];
                ++result.mCount;
            }
            result.mComplete = result.mCount == count;
            return result;
        }
    }

    Center CalculateCenter(const std::vector<ActorSnapshot>& actors)
    {
        long double x = 0;
        long double y = 0;
        std::size_t count = 0;
        // ID 顺序固定累加，候选及输入排序不会改变舍入结果；重复 ID 保留首次记录。
        for (const auto& [id, actor] : FirstActors(actors))
        {
            (void)id;
            if (!ValidPosition(actor->mPosition)) continue;
            x += actor->mPosition.mX;
            y += actor->mPosition.mY;
            ++count;
        }
        return count == 0 ? Center {} : Center { static_cast<double>(x / count), static_cast<double>(y / count) };
    }

    PlanResult Plan(const Snapshot& snapshot)
    {
        PlanResult result;
        result.mCenter = snapshot.mCenter;
        const auto firstActors = FirstActors(snapshot.mActors);
        for (const auto& [id, actor] : firstActors)
        {
            (void)actor;
            result.mUnassignedActors.push_back(id);
        }
        const auto rejectBudget = [&]()
        {
            result.mBudgetExceeded = true;
            result.mAssignments.clear();
            return result;
        };
        if (firstActors.size() > MAX_ACTORS) return rejectBudget();
        if (!ValidPosition(snapshot.mCenter)) return result;

        std::vector<const ActorSnapshot*> actors;
        std::vector<Cell> cells;
        std::unordered_map<Cell, int, CellHash> cellIndices;
        cellIndices.reserve(MAX_CELLS);
        std::size_t edgeCount = 0;
        for (const auto& [id, actor] : firstActors)
        {
            (void)id;
            if (!ValidPosition(actor->mPosition)) continue;
            if (actor->mCandidates.size() > MAX_CANDIDATES_PER_ACTOR ||
                actor->mCandidates.size() > MAX_EDGES - edgeCount) return rejectBudget();
            edgeCount += actor->mCandidates.size();
            actors.push_back(actor);
            for (const auto& cell : actor->mCandidates)
            {
                cellIndices.emplace(cell, NONE);
                if (cellIndices.size() > MAX_CELLS) return rejectBudget();
            }
        }
        if (actors.empty() || cellIndices.empty()) return result;
        cells.reserve(cellIndices.size());
        for (const auto& [cell, index] : cellIndices)
        {
            (void)index;
            cells.push_back(cell);
        }
        std::sort(cells.begin(), cells.end(), CellLess);
        for (std::size_t cell = 0; cell < cells.size(); ++cell) cellIndices[cells[cell]] = static_cast<int>(cell);
        std::vector<std::vector<int>> edges(actors.size());
        std::vector<std::vector<int>> cellActors(cells.size());
        for (std::size_t actor = 0; actor < actors.size(); ++actor)
        {
            edges[actor].reserve(actors[actor]->mCandidates.size());
            for (const auto& cell : actors[actor]->mCandidates)
                edges[actor].push_back(cellIndices.at(cell));
            if (!std::is_sorted(edges[actor].begin(), edges[actor].end()))
                std::sort(edges[actor].begin(), edges[actor].end());
            edges[actor].erase(std::unique(edges[actor].begin(), edges[actor].end()), edges[actor].end());
            for (const int cell : edges[actor]) cellActors[cell].push_back(static_cast<int>(actor));
        }

        WorkBudget matchBudget { MATCH_WORK_LIMIT };
        const auto global = Matcher(edges, std::vector<bool>(cells.size(), true), matchBudget).Run();
        if (!global.mComplete) return rejectBudget();
        if (global.mCount == 0) return result;

        // 四个 floor/ceil 锚点解决偶数边长的半格中心问题；同一锚点随边长增长为嵌套包络。
        // 只搜索居中正方形及边长相差一格的矩形，不枚举无界的形状/平移组合。
        std::int64_t upperSide = 1;
        for (const auto& cell : cells)
        {
            const double delta = std::max(std::abs(static_cast<double>(cell.mX) - snapshot.mCenter.mX),
                std::abs(static_cast<double>(cell.mY) - snapshot.mCenter.mY));
            upperSide = std::max(upperSide, static_cast<std::int64_t>(std::ceil(delta * 2 + 3)));
        }
        std::int64_t side = upperSide;
        for (int anchor = 0; anchor < 4; ++anchor)
        {
            std::int64_t low = 1;
            std::int64_t high = side;
            while (low < high)
            {
                const auto middle = low + (high - low) / 2;
                const auto envelope = MakeEnvelope(snapshot.mCenter, middle, middle, anchor);
                const auto allowed = AllowedCells(envelope, cells);
                if (static_cast<std::size_t>(std::count(allowed.begin(), allowed.end(), true)) < global.mCount)
                {
                    low = middle + 1;
                    continue;
                }
                const auto matching = Matcher(edges, allowed, matchBudget).Run();
                if (!matching.mComplete) return rejectBudget();
                if (matching.mCount == global.mCount) high = middle;
                else low = middle + 1;
            }
            // 其他锚点可能在相同 side 无法达到 K，最终评估会再次检验。
            side = std::min(side, low);
        }

        struct Candidate
        {
            Envelope mEnvelope;
            Matching mMatching;
        };
        std::vector<Candidate> candidates;
        for (const auto& dimensions : { std::pair { side, side }, std::pair { side - 1, side }, std::pair { side, side - 1 } })
        {
            if (dimensions.first == 0 || dimensions.second == 0) continue;
            for (int anchor = 0; anchor < 4; ++anchor)
            {
                const auto envelope = MakeEnvelope(snapshot.mCenter, dimensions.first, dimensions.second, anchor);
                if (std::any_of(candidates.begin(), candidates.end(), [&](const Candidate& candidate)
                    {
                        return candidate.mEnvelope.mX == envelope.mX && candidate.mEnvelope.mY == envelope.mY &&
                            candidate.mEnvelope.mWidth == envelope.mWidth && candidate.mEnvelope.mHeight == envelope.mHeight;
                    })) continue;
                const auto allowed = AllowedCells(envelope, cells);
                if (static_cast<std::size_t>(std::count(allowed.begin(), allowed.end(), true)) < global.mCount) continue;
                auto matching = Matcher(edges, allowed, matchBudget).Run();
                if (!matching.mComplete) return rejectBudget();
                if (matching.mCount == global.mCount) candidates.push_back({ envelope, std::move(matching) });
            }
        }
        if (candidates.empty()) return rejectBudget();
        const auto shapeKey = [&](const Envelope& envelope)
        {
            // 面积可能超出 int64，使用 long double 乘法而非整数相乘。
            return std::tuple { static_cast<long double>(envelope.mWidth) * envelope.mHeight,
                std::abs(envelope.mWidth - envelope.mHeight), CenterOffset(envelope, snapshot.mCenter) };
        };
        std::stable_sort(candidates.begin(), candidates.end(), [&](const Candidate& left, const Candidate& right)
            { return shapeKey(left.mEnvelope) < shapeKey(right.mEnvelope); });
        const auto bestShape = shapeKey(candidates.front().mEnvelope);
        Matching best = candidates.front().mMatching;
        Cost bestCost = TotalCost(best, actors, cells, candidates.front().mEnvelope);
        WorkBudget refineBudget { REFINE_WORK_LIMIT };
        for (auto& candidate : candidates)
        {
            if (shapeKey(candidate.mEnvelope) != bestShape) break;
            std::vector<int> orderedCells;
            for (std::size_t cell = 0; cell < cells.size(); ++cell)
                if (candidate.mEnvelope.Contains(cells[cell])) orderedCells.push_back(static_cast<int>(cell));
            std::stable_sort(orderedCells.begin(), orderedCells.end(), [&](int left, int right)
            {
                return candidate.mEnvelope.Depth(cells[left]) > candidate.mEnvelope.Depth(cells[right]);
            });
            auto interior = InteriorMatcher(cellActors, actors.size(), refineBudget).Run(orderedCells, global.mCount);
            if (interior.mComplete) candidate.mMatching = std::move(interior);
            auto refined = Refine(actors, cells, edges, candidate.mEnvelope, global.mCount, refineBudget);
            // 最大匹配已完整验证；次要成本精化耗尽预算时保留可信结果，不截断车辆或候选。
            // 成本预算耗尽后的包络只比较已有可信匹配，跨包络移动成本是有界启发式。
            if (refined.mComplete) candidate.mMatching = std::move(refined);
            else ReduceMovement(candidate.mMatching, actors, cells, edges, candidate.mEnvelope, refineBudget);
            const auto cost = TotalCost(candidate.mMatching, actors, cells, candidate.mEnvelope);
            if (cost < bestCost) { bestCost = cost; best = std::move(candidate.mMatching); }
        }
        // 留在自己的最终槽优先于次要移动成本；独立预算不被包络美化消耗。
        // 预算耗尽保留已验证的同槽集最大匹配，不丢车、不随机更换目标槽。
        WorkBudget currentSlotBudget { CURRENT_SLOT_WORK_LIMIT };
        CurrentSlotMatcher(actors, cells, edges, best, currentSlotBudget).Run();
        result.mUnassignedActors.clear();
        for (const auto& [id, actor] : firstActors)
            if (!ValidPosition(actor->mPosition)) result.mUnassignedActors.push_back(id);
        for (std::size_t actor = 0; actor < actors.size(); ++actor)
        {
            if (best.mActorCells[actor] == NONE) result.mUnassignedActors.push_back(actors[actor]->mId);
            else result.mAssignments.push_back({ actors[actor]->mId, cells[best.mActorCells[actor]] });
        }
        std::sort(result.mUnassignedActors.begin(), result.mUnassignedActors.end());
        return result;
    }
}
