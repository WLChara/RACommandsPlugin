#include "Commands/AutoCrush/AutoCrushPlanner.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <utility>

namespace ra_commands::auto_crush
{
    namespace
    {
        constexpr double TURN_STEP_COST = 0.5;

        struct DirectionStep
        {
            int mX;
            int mY;
        };

        constexpr std::array<DirectionStep, 8> DIRECTIONS = {{
            {0, -1}, {1, -1}, {1, 0}, {1, 1},
            {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}
        }};

        struct Route
        {
            Facing mDirection;
            int mTurnSteps;
            std::vector<Cell> mPath;
        };

        struct Reservation
        {
            Cell mCell;
            int mArrivalSteps;
        };

        struct Choice
        {
            std::size_t mVehicleIndex;
            const Route* mRoute;
            PlannedMove mMove;
        };

        bool ContainsCell(const std::vector<Cell>& cells, Cell cell)
        {
            return std::find(cells.begin(), cells.end(), cell) != cells.end();
        }

        int CountTurnSteps(Facing from, Facing to)
        {
            const int difference = std::abs(static_cast<int>(from) - static_cast<int>(to));
            return (std::min)(difference, static_cast<int>(DIRECTIONS.size()) - difference);
        }

        std::vector<Route> BuildRoutes(const CrusherSnapshot& crusher)
        {
            std::vector<Route> routes;
            for (std::size_t directionIndex = 0; directionIndex < DIRECTIONS.size(); ++directionIndex)
            {
                const auto direction = static_cast<Facing>(directionIndex);
                std::vector<Cell> path;
                for (int step = 1; step <= MAX_ROUTE_CELLS; ++step)
                {
                    const auto x = static_cast<std::int64_t>(crusher.mCurrentCell.mX) +
                        static_cast<std::int64_t>(DIRECTIONS[directionIndex].mX) * step;
                    const auto y = static_cast<std::int64_t>(crusher.mCurrentCell.mY) +
                        static_cast<std::int64_t>(DIRECTIONS[directionIndex].mY) * step;
                    if (x < (std::numeric_limits<std::int32_t>::min)() ||
                        x > (std::numeric_limits<std::int32_t>::max)() ||
                        y < (std::numeric_limits<std::int32_t>::min)() ||
                        y > (std::numeric_limits<std::int32_t>::max)())
                    {
                        break;
                    }

                    const Cell cell{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
                    if (!ContainsCell(crusher.mTraversableCells, cell))
                    {
                        break;
                    }

                    path.push_back(cell);
                    if (step >= 2 && !ContainsCell(crusher.mNonStoppableCells, cell))
                    {
                        routes.push_back({direction, CountTurnSteps(crusher.mFacing, direction), path});
                    }
                }
            }
            return routes;
        }

        double ProbabilityAt(const TargetPrediction& target, Cell cell, int arrivalSteps)
        {
            const double confidence = (std::clamp)(target.mConfidence, 0.0, 1.0);
            if (target.mCurrentCell == target.mNextCell)
            {
                return cell == target.mCurrentCell ? 1.0 : 0.0;
            }
            if (static_cast<std::uint64_t>(arrivalSteps) < target.mTransitionSteps)
            {
                return cell == target.mCurrentCell ? 1.0 : 0.0;
            }
            if (cell == target.mNextCell)
            {
                return confidence;
            }
            return cell == target.mCurrentCell ? 1.0 - confidence : 0.0;
        }

        PlannedMove ScoreRoute(
            const CrusherSnapshot& crusher,
            const Route& route,
            const std::set<TargetId>& assignedTargets)
        {
            PlannedMove move;
            move.mCrusherId = crusher.mId;
            move.mDestination = route.mPath.back();
            std::map<TargetId, double> probabilities;
            for (const auto& target : crusher.mTargets)
            {
                if (assignedTargets.contains(target.mId))
                {
                    continue;
                }

                double bestProbability = 0.0;
                // 终点不计收益：原生 Move 可能在步兵所在格之前停下。
                for (std::size_t index = 0; index + 1 < route.mPath.size(); ++index)
                {
                    const int arrivalSteps = static_cast<int>(index) + 1 + route.mTurnSteps;
                    bestProbability = (std::max)(bestProbability,
                        ProbabilityAt(target, route.mPath[index], arrivalSteps));
                }
                auto& probability = probabilities[target.mId];
                probability = (std::max)(probability, bestProbability);
            }

            for (const auto& [targetId, probability] : probabilities)
            {
                if (probability > 0.0)
                {
                    move.mExpectedTargetIds.push_back(targetId);
                    move.mExpectedCrushCount += probability;
                }
            }
            if (move.mExpectedCrushCount > 0.0)
            {
                move.mPath = route.mPath;
                move.mScore = move.mExpectedCrushCount /
                    (static_cast<double>(route.mPath.size()) + TURN_STEP_COST * route.mTurnSteps);
            }
            return move;
        }

        bool Conflicts(const Route& route, const std::vector<Reservation>& reservations)
        {
            // 以初始转向步数粗估到达时间，拒绝同格相差至多一步的预约。
            for (std::size_t index = 0; index < route.mPath.size(); ++index)
            {
                const int arrivalSteps = static_cast<int>(index) + 1 + route.mTurnSteps;
                for (const auto& reservation : reservations)
                {
                    if (route.mPath[index] == reservation.mCell &&
                        std::abs(arrivalSteps - reservation.mArrivalSteps) <= 1)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        bool IsBetter(const Choice& candidate, const Choice& current)
        {
            if (candidate.mMove.mScore != current.mMove.mScore)
            {
                return candidate.mMove.mScore > current.mMove.mScore;
            }
            if (candidate.mMove.mExpectedCrushCount != current.mMove.mExpectedCrushCount)
            {
                return candidate.mMove.mExpectedCrushCount > current.mMove.mExpectedCrushCount;
            }
            if (candidate.mMove.mCrusherId != current.mMove.mCrusherId)
            {
                return candidate.mMove.mCrusherId < current.mMove.mCrusherId;
            }
            if (candidate.mRoute->mDirection != current.mRoute->mDirection)
            {
                return candidate.mRoute->mDirection < current.mRoute->mDirection;
            }
            return candidate.mRoute->mPath.size() < current.mRoute->mPath.size();
        }
    }

    PlanResult Plan(const Snapshot& snapshot)
    {
        const auto& crushers = snapshot.mCrushers;
        std::vector<std::size_t> order(crushers.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&crushers](std::size_t left, std::size_t right)
        {
            if (crushers[left].mId != crushers[right].mId)
            {
                return crushers[left].mId < crushers[right].mId;
            }
            return left < right;
        });

        std::vector<std::vector<Route>> routes(crushers.size());
        std::vector<std::size_t> uniqueOrder;
        for (const auto index : order)
        {
            if (!uniqueOrder.empty() && crushers[uniqueOrder.back()].mId == crushers[index].mId)
            {
                continue;
            }
            uniqueOrder.push_back(index);
            if (!crushers[index].mTargets.empty())
            {
                routes[index] = BuildRoutes(crushers[index]);
            }
        }

        PlanResult result;
        std::vector<bool> allocated(crushers.size(), false);
        std::set<TargetId> assignedTargets;
        std::vector<Reservation> reservations;
        while (true)
        {
            std::optional<Choice> best;
            for (const auto index : uniqueOrder)
            {
                if (allocated[index])
                {
                    continue;
                }
                for (const auto& route : routes[index])
                {
                    if (Conflicts(route, reservations))
                    {
                        continue;
                    }
                    auto move = ScoreRoute(crushers[index], route, assignedTargets);
                    if (move.mScore <= 0.0)
                    {
                        continue;
                    }
                    Choice choice{index, &route, std::move(move)};
                    if (!best || IsBetter(choice, *best))
                    {
                        best = std::move(choice);
                    }
                }
            }
            if (!best)
            {
                break;
            }

            allocated[best->mVehicleIndex] = true;
            for (const auto targetId : best->mMove.mExpectedTargetIds)
            {
                assignedTargets.insert(targetId);
            }
            reservations.push_back({crushers[best->mVehicleIndex].mCurrentCell, 0});
            for (std::size_t index = 0; index < best->mRoute->mPath.size(); ++index)
            {
                reservations.push_back({best->mRoute->mPath[index],
                    static_cast<int>(index) + 1 + best->mRoute->mTurnSteps});
            }
            result.mMoves.push_back(std::move(best->mMove));
        }

        std::sort(result.mMoves.begin(), result.mMoves.end(),
            [](const PlannedMove& left, const PlannedMove& right)
            {
                return left.mCrusherId < right.mCrusherId;
            });
        return result;
    }
}
