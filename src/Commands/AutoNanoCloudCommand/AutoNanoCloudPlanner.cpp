#include "Commands/AutoNanoCloudCommand/AutoNanoCloudPlanner.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace ra_commands::auto_nano_cloud
{
    namespace
    {
        constexpr double DAMAGE_DIVISOR = 1.3;

        std::uint64_t DistanceSquared(const HunterSnapshot& hunter,
            const VictimSnapshot& victim)
        {
            const auto dx = static_cast<std::int64_t>(hunter.CellX) - victim.CellX;
            const auto dy = static_cast<std::int64_t>(hunter.CellY) - victim.CellY;
            return static_cast<std::uint64_t>(dx * dx + dy * dy);
        }

        bool HasLowerId(UnitId left, UnitId right)
        {
            return left.UniqueId < right.UniqueId ||
                (left.UniqueId == right.UniqueId && left.Address < right.Address);
        }
    }

    std::optional<PlanResult> Plan(const Snapshot& snapshot)
    {
        if (snapshot.Hunters.empty())
        {
            return std::nullopt;
        }

        const VictimSnapshot* victim = nullptr;
        bool usesRetainedVictim = false;
        if (!snapshot.SelectedVictims.empty())
        {
            int maxCost = 1;
            int maxHealth = 1;
            for (const auto& candidate : snapshot.SelectedVictims)
            {
                maxCost = std::max(maxCost, std::max(0, candidate.Cost));
                maxHealth = std::max(maxHealth, candidate.Health);
            }

            double lowestScore = (std::numeric_limits<double>::max)();
            for (const auto& candidate : snapshot.SelectedVictims)
            {
                if (candidate.Id.Address == 0 || candidate.Id.UniqueId == 0 ||
                    candidate.Health <= 0)
                {
                    continue;
                }
                // 造价和当前血量分别归一化，避免两种量纲直接相加。
                const double score = static_cast<double>(std::max(0, candidate.Cost)) / maxCost +
                    static_cast<double>(candidate.Health) / maxHealth;
                if (score < lowestScore ||
                    (score == lowestScore && victim && HasLowerId(candidate.Id, victim->Id)))
                {
                    victim = &candidate;
                    lowestScore = score;
                }
            }
        }
        else if (snapshot.RetainedVictim && snapshot.RetainedVictim->Health > 0)
        {
            victim = &*snapshot.RetainedVictim;
            usesRetainedVictim = true;
        }
        if (!victim)
        {
            return std::nullopt;
        }

        std::vector<const HunterSnapshot*> hunters;
        for (const auto& hunter : snapshot.Hunters)
        {
            if (hunter.Id.Address != 0 && hunter.Id.UniqueId != 0 &&
                hunter.PrimaryDamage > 0 && hunter.Id != victim->Id)
            {
                hunters.push_back(&hunter);
            }
        }
        std::sort(hunters.begin(), hunters.end(), [victim](const auto* left, const auto* right)
        {
            if (left->PrimaryDamage != right->PrimaryDamage)
            {
                return left->PrimaryDamage > right->PrimaryDamage;
            }
            const auto leftDistance = DistanceSquared(*left, *victim);
            const auto rightDistance = DistanceSquared(*right, *victim);
            return leftDistance != rightDistance
                ? leftDistance < rightDistance : HasLowerId(left->Id, right->Id);
        });
        if (hunters.empty())
        {
            return std::nullopt;
        }

        PlanResult result;
        result.Victim = victim->Id;
        result.UsesRetainedVictim = usesRetainedVictim;
        double totalDamage = 0.0;
        for (const auto* hunter : hunters)
        {
            result.Hunters.push_back(hunter->Id);
            totalDamage += hunter->PrimaryDamage / DAMAGE_DIVISOR;
            if (totalDamage >= victim->Health)
            {
                break;
            }
        }
        return result;
    }
}
