#include "Commands/AutoSuperWeapon/AutoSuperWeaponPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace ra_commands::auto_super_weapon
{
    namespace
    {
        constexpr std::int64_t RAGE_RADIUS_SQUARED_X100 = 1764;
        constexpr double SCORE_TIE_EPSILON = 1e-12;

        bool CellLess(Cell left, Cell right) noexcept
        {
            return left.X != right.X ? left.X < right.X : left.Y < right.Y;
        }

        bool BetterTie(const PlanResult& left, const PlanResult& right) noexcept
        {
            if (left.CoveredUnits != right.CoveredUnits)
            {
                return left.CoveredUnits > right.CoveredUnits;
            }
            if (left.CoveredValue != right.CoveredValue)
            {
                return left.CoveredValue > right.CoveredValue;
            }
            if (left.AttackableEnemies != right.AttackableEnemies)
            {
                return left.AttackableEnemies > right.AttackableEnemies;
            }
            return CellLess(left.Center, right.Center);
        }
    }

    bool Covers(Kind kind, Cell center, Cell unit) noexcept
    {
        const auto dx = static_cast<std::int64_t>(center.X) - unit.X;
        const auto dy = static_cast<std::int64_t>(center.Y) - unit.Y;
        if (kind == Kind::IronCurtain)
        {
            return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
        }
        if (dx < -4 || dx > 4 || dy < -4 || dy > 4)
        {
            return false;
        }
        return (dx * dx + dy * dy) * 100 <= RAGE_RADIUS_SQUARED_X100;
    }

    std::optional<PlanResult> Plan(Kind kind, const Snapshot& snapshot)
    {
        if (snapshot.Selected.empty() || snapshot.CandidateCenters.empty())
        {
            return std::nullopt;
        }

        struct ScoredCandidate
        {
            PlanResult Result;
            double Score = 0.0;
        };
        std::vector<PlanResult> candidates;
        candidates.reserve(snapshot.CandidateCenters.size());
        std::vector<std::uint32_t> seen(snapshot.EnemyCount, 0);
        std::uint32_t stamp = 0;
        for (const auto center : snapshot.CandidateCenters)
        {
            if (stamp == (std::numeric_limits<std::uint32_t>::max)())
            {
                std::fill(seen.begin(), seen.end(), 0);
                stamp = 0;
            }
            ++stamp;
            PlanResult result;
            result.Center = center;
            for (const auto& unit : snapshot.Selected)
            {
                if (!Covers(kind, center, unit.Position))
                {
                    continue;
                }
                ++result.CoveredUnits;
                result.CoveredValue += (std::max)(0, unit.Cost);
                for (const auto enemy : unit.AttackableEnemies)
                {
                    if (enemy < seen.size() && seen[enemy] != stamp)
                    {
                        seen[enemy] = stamp;
                        ++result.AttackableEnemies;
                    }
                }
            }
            if (result.CoveredUnits > 0)
            {
                candidates.push_back(result);
            }
        }
        if (candidates.empty())
        {
            return std::nullopt;
        }

        std::size_t maxUnits = 1;
        std::int64_t maxValue = 1;
        std::size_t maxEnemies = 1;
        for (const auto& candidate : candidates)
        {
            maxUnits = (std::max)(maxUnits, candidate.CoveredUnits);
            maxValue = (std::max)(maxValue, candidate.CoveredValue);
            maxEnemies = (std::max)(maxEnemies, candidate.AttackableEnemies);
        }

        std::optional<ScoredCandidate> best;
        for (const auto& candidate : candidates)
        {
            // 三项各归一化到 0..1，避免造价的量纲压倒数量与可攻击目标数。
            const double score =
                static_cast<double>(candidate.CoveredUnits) / maxUnits +
                static_cast<double>(candidate.CoveredValue) / maxValue +
                static_cast<double>(candidate.AttackableEnemies) / maxEnemies;
            if (!best || score > best->Score + SCORE_TIE_EPSILON ||
                (std::abs(score - best->Score) <= SCORE_TIE_EPSILON &&
                    BetterTie(candidate, best->Result)))
            {
                best = ScoredCandidate{candidate, score};
            }
        }
        return best->Result;
    }
}
