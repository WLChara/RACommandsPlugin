#include "Commands/AutoLoadCommand/AutoLoadPlanner.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ra_commands::autoload
{
    namespace
    {
        const Unit* FindUnit(const Snapshot& snapshot, UnitId id)
        {
            const auto it = std::find_if(snapshot.Units.begin(), snapshot.Units.end(),
                [id](const Unit& unit) { return unit.Id == id; });
            return it == snapshot.Units.end() ? nullptr : &*it;
        }

        bool MatchesName(std::string_view ruleName, std::string_view unitName)
        {
            return ruleName == "*" || ruleName == unitName;
        }

        bool LoadingAllowed(const Snapshot& snapshot, const Unit& passenger, const Unit& transport)
        {
            if (!snapshot.LoadPolicy.UseCustomRules)
            {
                return true;
            }

            for (const auto& rule : snapshot.LoadPolicy.LoadingRules)
            {
                if (rule.Kind == RuleKind::Forbid &&
                    MatchesName(rule.PassengerName, passenger.TypeName) &&
                    MatchesName(rule.TransportName, transport.TypeName))
                {
                    return false;
                }
            }
            return true;
        }

        int LoadingPriority(const Snapshot& snapshot, const Unit& passenger, const Unit& transport,
            int& maxCount)
        {
            maxCount = 0;
            if (!snapshot.LoadPolicy.UseCustomRules)
            {
                return 0;
            }

            int highestPriority = 0;
            for (const auto& rule : snapshot.LoadPolicy.LoadingRules)
            {
                if (rule.Kind != RuleKind::Priority ||
                    !MatchesName(rule.PassengerName, passenger.TypeName) ||
                    !MatchesName(rule.TransportName, transport.TypeName))
                {
                    continue;
                }

                // 与 FVModule 一致：相同优先级的后续规则不覆盖首条规则的 MaxCount。
                if (rule.Priority > highestPriority)
                {
                    highestPriority = rule.Priority;
                    maxCount = rule.MaxCount;
                }
            }
            return highestPriority;
        }

        bool ValidPassenger(const Snapshot& snapshot, const Unit& unit)
        {
            if (unit.Kind != UnitKind::Infantry || !unit.HasOwner || unit.IsInTransport ||
                !unit.IsInPlayfield || !unit.HasType)
            {
                return false;
            }
            if (snapshot.LoadPolicy.RejectPassengersWithoutIfvMode && !unit.HasIfvMode)
            {
                return false;
            }
            return !snapshot.LoadPolicy.RejectPassengersWithWeaponRangeLessThan3 ||
                unit.HasPrimaryWeaponRangeAtLeast3;
        }

        bool CommandPassengerAllowed(const Unit& unit)
        {
            return unit.Kind == UnitKind::Infantry && unit.HasType &&
                !unit.UsesFlyingMovement && !unit.IsDog;
        }

        bool ValidVehiclePassenger(const Unit& unit)
        {
            // 先排除飞行载具，避免规划成功后被游戏适配器拒绝而失去步兵 fallback。
            return unit.Kind == UnitKind::Vehicle && unit.HasOwner &&
                !unit.IsInTransport && unit.IsInPlayfield && unit.HasType &&
                !unit.UsesFlyingMovement && unit.PassengerCount <= 0;
        }

        bool ValidCandidatePassenger(const Snapshot& snapshot, const Unit& unit, PairKind pairKind)
        {
            return pairKind == PairKind::VehicleIntoVehicle
                ? ValidVehiclePassenger(unit)
                : ValidPassenger(snapshot, unit) && CommandPassengerAllowed(unit);
        }

        bool ValidTransport(const Snapshot& snapshot, const Unit& unit)
        {
            if (unit.Kind != UnitKind::Vehicle || !unit.HasOwner || !unit.IsInPlayfield ||
                !unit.HasType || unit.PassengerCapacity <= 0 ||
                unit.PassengerCapacity - unit.PassengerCount <= 0)
            {
                return false;
            }
            return !snapshot.LoadPolicy.RequireArmedOrOpenToppedTransport ||
                unit.IsArmed || unit.IsOpenTopped;
        }

        int SlotsLeft(const Unit& transport)
        {
            return std::max(0, transport.PassengerCapacity - transport.PassengerCount);
        }

        std::uint64_t DistanceSquared(const Unit& a, const Unit& b)
        {
            const auto dx = static_cast<std::int64_t>(a.X) - b.X;
            const auto dy = static_cast<std::int64_t>(a.Y) - b.Y;
            const auto dz = static_cast<std::int64_t>(a.Z) - b.Z;
            return static_cast<std::uint64_t>(dx * dx + dy * dy + dz * dz);
        }

        struct Candidate
        {
            UnitId Passenger = 0;
            UnitId Transport = 0;
            std::size_t TransportIndex = 0;
            std::size_t CompatibleTransportCount = 0;
            std::size_t PassengerOrder = 0;
            std::uint64_t DistanceSquared = 0;
            double PassengerSize = 0.0;
            int Priority = 0;
            int MaxCount = 0;
        };

        struct TransportState
        {
            UnitId Id = 0;
            int SlotsLeft = 0;
        };

        struct PriorityCounter
        {
            UnitId Transport = 0;
            std::string PassengerType;
            int Count = 0;
            int MaxCount = 0;
        };

        bool CompareVehicleCandidates(const Candidate& left, const Candidate& right)
        {
            if (left.Priority != right.Priority) return left.Priority > right.Priority;
            if (left.CompatibleTransportCount != right.CompatibleTransportCount)
                return left.CompatibleTransportCount < right.CompatibleTransportCount;
            if (left.PassengerSize != right.PassengerSize) return left.PassengerSize > right.PassengerSize;
            if (left.TransportIndex != right.TransportIndex) return left.TransportIndex < right.TransportIndex;
            if (left.DistanceSquared != right.DistanceSquared) return left.DistanceSquared < right.DistanceSquared;
            return left.PassengerOrder < right.PassengerOrder;
        }

        bool CompareInfantryCandidates(const Candidate& left, const Candidate& right)
        {
            if (left.Priority != right.Priority) return left.Priority > right.Priority;
            return left.DistanceSquared < right.DistanceSquared;
        }

        std::vector<Pair> Assign(const Snapshot& snapshot,
            const std::vector<UnitId>& passengers,
            const std::vector<UnitId>& transports,
            PairKind pairKind)
        {
            std::vector<TransportState> states;
            states.reserve(transports.size());
            for (const UnitId id : transports)
            {
                const Unit* transport = FindUnit(snapshot, id);
                if (transport && ValidTransport(snapshot, *transport))
                {
                    states.push_back({ id, SlotsLeft(*transport) });
                }
            }

            std::vector<Candidate> candidates;
            for (std::size_t passengerOrder = 0; passengerOrder < passengers.size(); ++passengerOrder)
            {
                const UnitId passengerId = passengers[passengerOrder];
                const Unit* passenger = FindUnit(snapshot, passengerId);
                if (!passenger)
                {
                    continue;
                }

                if (!ValidCandidatePassenger(snapshot, *passenger, pairKind))
                {
                    continue;
                }

                std::size_t compatibleCount = 0;
                for (const auto& state : states)
                {
                    const Unit* transport = FindUnit(snapshot, state.Id);
                    if (transport && passenger->Size <= transport->SizeLimit &&
                        LoadingAllowed(snapshot, *passenger, *transport))
                    {
                        ++compatibleCount;
                    }
                }
                if (compatibleCount == 0)
                {
                    continue;
                }

                for (std::size_t transportIndex = 0; transportIndex < states.size(); ++transportIndex)
                {
                    const Unit* transport = FindUnit(snapshot, states[transportIndex].Id);
                    if (!transport || passenger->Size > transport->SizeLimit ||
                        !LoadingAllowed(snapshot, *passenger, *transport))
                    {
                        continue;
                    }

                    Candidate candidate;
                    candidate.Passenger = passengerId;
                    candidate.Transport = transport->Id;
                    candidate.TransportIndex = transportIndex;
                    candidate.CompatibleTransportCount = compatibleCount;
                    candidate.PassengerOrder = passengerOrder;
                    candidate.DistanceSquared = DistanceSquared(*passenger, *transport);
                    candidate.PassengerSize = passenger->Size;
                    candidate.Priority = LoadingPriority(snapshot, *passenger, *transport, candidate.MaxCount);
                    candidates.push_back(candidate);
                }
            }

            if (pairKind == PairKind::VehicleIntoVehicle)
            {
                std::stable_sort(candidates.begin(), candidates.end(), CompareVehicleCandidates);
            }
            else
            {
                // 原命令使用 std::sort 且未定义同优先级、同距离的先后；稳定排序保留候选枚举顺序。
                std::stable_sort(candidates.begin(), candidates.end(), CompareInfantryCandidates);
            }

            std::vector<Pair> result;
            std::vector<UnitId> usedPassengers;
            std::vector<PriorityCounter> counters;
            for (const auto& candidate : candidates)
            {
                if (candidate.TransportIndex >= states.size() || states[candidate.TransportIndex].SlotsLeft <= 0 ||
                    std::find(usedPassengers.begin(), usedPassengers.end(), candidate.Passenger) != usedPassengers.end())
                {
                    continue;
                }

                const Unit* passenger = FindUnit(snapshot, candidate.Passenger);
                const Unit* transport = FindUnit(snapshot, candidate.Transport);
                if (!passenger || !transport)
                {
                    continue;
                }

                if (candidate.Priority > 0 && candidate.MaxCount > 0)
                {
                    auto counter = std::find_if(counters.begin(), counters.end(), [&](const PriorityCounter& item)
                    {
                        return item.Transport == candidate.Transport && item.PassengerType == passenger->TypeName;
                    });
                    if (counter == counters.end())
                    {
                        counters.push_back({ candidate.Transport, passenger->TypeName, 0, candidate.MaxCount });
                        counter = counters.end() - 1;
                    }
                    if (counter->Count >= counter->MaxCount)
                    {
                        continue;
                    }
                    // 与 FVModule 一致：候选计数先于最终状态检查增加，不能随意挪到检查后。
                    ++counter->Count;
                }

                if (!ValidCandidatePassenger(snapshot, *passenger, pairKind) ||
                    !ValidTransport(snapshot, *transport) ||
                    passenger->Size > transport->SizeLimit || !LoadingAllowed(snapshot, *passenger, *transport))
                {
                    continue;
                }

                result.push_back({ candidate.Passenger, candidate.Transport, pairKind,
                    candidate.Priority, candidate.MaxCount });
                --states[candidate.TransportIndex].SlotsLeft;
                usedPassengers.push_back(candidate.Passenger);
            }
            return result;
        }

        enum class OwnerScope
        {
            Any,
            LocalOrAllied
        };

        std::vector<UnitId> FilterCommandPassengers(const Snapshot& snapshot,
            const std::vector<UnitId>& ids, OwnerScope ownerScope)
        {
            std::vector<UnitId> result;
            for (const UnitId id : ids)
            {
                const Unit* unit = FindUnit(snapshot, id);
                if (unit && ValidPassenger(snapshot, *unit) &&
                    (ownerScope == OwnerScope::Any || unit->IsLocalOrAllied) &&
                    CommandPassengerAllowed(*unit))
                {
                    result.push_back(id);
                }
            }
            return result;
        }

        std::vector<UnitId> FilterTransports(const Snapshot& snapshot,
            const std::vector<UnitId>& ids, OwnerScope ownerScope)
        {
            std::vector<UnitId> result;
            for (const UnitId id : ids)
            {
                const Unit* unit = FindUnit(snapshot, id);
                if (unit && ValidTransport(snapshot, *unit) &&
                    (ownerScope == OwnerScope::Any || unit->IsLocalOrAllied))
                {
                    result.push_back(id);
                }
            }
            return result;
        }

        std::vector<Pair> PlanVehicleSelection(const Snapshot& snapshot)
        {
            std::vector<UnitId> transports = FilterTransports(
                snapshot, snapshot.SelectedVehicles, OwnerScope::Any);
            std::stable_sort(transports.begin(), transports.end(), [&](UnitId a, UnitId b)
            {
                const Unit* left = FindUnit(snapshot, a);
                const Unit* right = FindUnit(snapshot, b);
                if (left->PassengerCapacity != right->PassengerCapacity)
                    return left->PassengerCapacity > right->PassengerCapacity;
                if (SlotsLeft(*left) != SlotsLeft(*right)) return SlotsLeft(*left) > SlotsLeft(*right);
                if (left->SizeLimit != right->SizeLimit) return left->SizeLimit > right->SizeLimit;
                return false;
            });

            const UnitId primaryTransportId = transports.empty() ? 0 : transports.front();
            std::vector<UnitId> vehiclePassengers;
            for (const UnitId id : snapshot.SelectedVehicles)
            {
                const Unit* unit = FindUnit(snapshot, id);
                // 其他空载具即使自己能载人，也可以进入本轮的主载具。
                if (id != primaryTransportId && unit && ValidVehiclePassenger(*unit))
                {
                    vehiclePassengers.push_back(id);
                }
            }

            const auto infantryFallback = [&]()
            {
                const auto fallbackTransports = FilterTransports(
                    snapshot, snapshot.SelectedVehicles, OwnerScope::LocalOrAllied);
                const auto fallbackPassengers = FilterCommandPassengers(
                    snapshot, snapshot.FriendlyPassengers, OwnerScope::LocalOrAllied);
                return Assign(snapshot, fallbackPassengers, fallbackTransports,
                    PairKind::InfantryFallback);
            };

            if (transports.empty() || vehiclePassengers.empty())
            {
                return infantryFallback();
            }

            const auto smallest = std::min_element(vehiclePassengers.begin(), vehiclePassengers.end(),
                [&](UnitId a, UnitId b)
                {
                    const Unit* left = FindUnit(snapshot, a);
                    const Unit* right = FindUnit(snapshot, b);
                    return left->Size < right->Size;
                });
            const Unit* smallestPassenger = FindUnit(snapshot, *smallest);
            const Unit* primaryTransport = FindUnit(snapshot, transports.front());
            if (!smallestPassenger || !primaryTransport ||
                smallestPassenger->Size > primaryTransport->SizeLimit ||
                !LoadingAllowed(snapshot, *smallestPassenger, *primaryTransport))
            {
                return infantryFallback();
            }

            // 只把主载具作为目标，避免同一辆车同时被规划为乘客和载具。
            auto result = Assign(snapshot, vehiclePassengers, { primaryTransportId },
                PairKind::VehicleIntoVehicle);
            return result.empty() ? infantryFallback() : result;
        }
    }

    std::vector<Pair> Plan(const Snapshot& snapshot)
    {
        if (snapshot.SelectedInfantries.empty() && snapshot.SelectedVehicles.empty())
        {
            return {};
        }
        if (snapshot.SelectedInfantries.empty())
        {
            return PlanVehicleSelection(snapshot);
        }

        const bool mixedSelection = !snapshot.SelectedVehicles.empty();
        const auto passengers = FilterCommandPassengers(snapshot, snapshot.SelectedInfantries,
            mixedSelection ? OwnerScope::Any : OwnerScope::LocalOrAllied);
        const auto transports = mixedSelection
            ? FilterTransports(snapshot, snapshot.SelectedVehicles, OwnerScope::Any)
            : FilterTransports(snapshot, snapshot.FriendlyTransports, OwnerScope::LocalOrAllied);
        if (passengers.empty() || transports.empty())
        {
            return {};
        }
        return Assign(snapshot, passengers, transports, PairKind::Command);
    }
}
