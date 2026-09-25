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
            // 已载客的载具只有实际满员时才可尝试进入更大的载具。
            return unit.Kind == UnitKind::Vehicle && unit.HasOwner &&
                !unit.IsInTransport && unit.IsInPlayfield && unit.HasType &&
                !unit.UsesFlyingMovement &&
                (unit.PassengerCount <= 0 ||
                    (unit.PassengerCapacity > 0 && unit.PassengerCount >= unit.PassengerCapacity));
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
                    if (pairKind == PairKind::VehicleIntoVehicle && passengerId == state.Id)
                    {
                        continue;
                    }
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
                    if (pairKind == PairKind::VehicleIntoVehicle && passengerId == states[transportIndex].Id)
                    {
                        continue;
                    }
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

        std::vector<Pair> AssignOpenToppedEvenly(const Snapshot& snapshot,
            const std::vector<UnitId>& passengers,
            const std::vector<UnitId>& transports)
        {
            struct PassengerBucket
            {
                std::string TypeName;
                std::vector<UnitId> Passengers;
                int Priority = 0;
            };

            std::vector<TransportState> states;
            states.reserve(transports.size());
            for (const UnitId id : transports)
            {
                const Unit* transport = FindUnit(snapshot, id);
                if (transport && ValidTransport(snapshot, *transport) &&
                    transport->IsOpenTopped && transport->PassengerCapacity > 1)
                {
                    states.push_back({ id, SlotsLeft(*transport) });
                }
            }
            if (states.empty())
            {
                return {};
            }

            std::vector<PassengerBucket> buckets;
            for (const UnitId id : passengers)
            {
                const Unit* passenger = FindUnit(snapshot, id);
                if (!passenger || !ValidCandidatePassenger(snapshot, *passenger, PairKind::Command))
                {
                    continue;
                }
                auto bucket = std::find_if(buckets.begin(), buckets.end(), [&](const PassengerBucket& item)
                {
                    return item.TypeName == passenger->TypeName;
                });
                if (bucket == buckets.end())
                {
                    buckets.push_back({ passenger->TypeName, { id } });
                }
                else
                {
                    bucket->Passengers.push_back(id);
                }
            }

            for (auto& bucket : buckets)
            {
                const Unit* passenger = FindUnit(snapshot, bucket.Passengers.front());
                for (const auto& state : states)
                {
                    const Unit* transport = FindUnit(snapshot, state.Id);
                    if (passenger && transport && passenger->Size <= transport->SizeLimit &&
                        LoadingAllowed(snapshot, *passenger, *transport))
                    {
                        int maxCount = 0;
                        bucket.Priority = std::max(bucket.Priority,
                            LoadingPriority(snapshot, *passenger, *transport, maxCount));
                    }
                }
            }
            std::stable_sort(buckets.begin(), buckets.end(),
                [](const PassengerBucket& left, const PassengerBucket& right)
                {
                    if (left.Priority != right.Priority) return left.Priority > right.Priority;
                    return left.Passengers.size() > right.Passengers.size();
                });

            std::vector<Pair> result;
            std::vector<PriorityCounter> counters;
            for (const auto& bucket : buckets)
            {
                std::size_t nextTransportIndex = 0;
                for (const UnitId id : bucket.Passengers)
                {
                    const Unit* passenger = FindUnit(snapshot, id);
                    if (!passenger || !ValidCandidatePassenger(snapshot, *passenger, PairKind::Command))
                    {
                        continue;
                    }

                    for (std::size_t attempt = 0; attempt < states.size(); ++attempt)
                    {
                        const std::size_t index = (nextTransportIndex + attempt) % states.size();
                        auto& state = states[index];
                        const Unit* transport = FindUnit(snapshot, state.Id);
                        if (!transport || state.SlotsLeft <= 0 ||
                            passenger->Size > transport->SizeLimit ||
                            !LoadingAllowed(snapshot, *passenger, *transport))
                        {
                            continue;
                        }

                        int maxCount = 0;
                        const int priority = LoadingPriority(snapshot, *passenger, *transport, maxCount);
                        auto counter = std::find_if(counters.begin(), counters.end(), [&](const PriorityCounter& item)
                        {
                            return item.Transport == state.Id && item.PassengerType == passenger->TypeName;
                        });
                        if (priority > 0 && maxCount > 0 &&
                            counter != counters.end() && counter->Count >= maxCount)
                        {
                            continue;
                        }

                        result.push_back({ id, state.Id, PairKind::Command, priority, maxCount });
                        --state.SlotsLeft;
                        if (priority > 0 && maxCount > 0)
                        {
                            if (counter == counters.end())
                            {
                                counters.push_back({ state.Id, passenger->TypeName, 1, maxCount });
                            }
                            else
                            {
                                ++counter->Count;
                            }
                        }
                        nextTransportIndex = (index + 1) % states.size();
                        break;
                    }
                }
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

        std::vector<Pair> PlanInfantryFallback(const Snapshot& snapshot)
        {
            const auto fallbackTransports = FilterTransports(
                snapshot, snapshot.SelectedVehicles, OwnerScope::LocalOrAllied);
            const auto fallbackPassengers = FilterCommandPassengers(
                snapshot, snapshot.FriendlyPassengers, OwnerScope::LocalOrAllied);
            return Assign(snapshot, fallbackPassengers, fallbackTransports,
                PairKind::InfantryFallback);
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
            const auto rankedTransports = transports;
            const auto canCarrySelected = [&](UnitId transportId)
            {
                const Unit* transport = FindUnit(snapshot, transportId);
                if (!transport || !ValidTransport(snapshot, *transport))
                {
                    return false;
                }
                for (const UnitId passengerId : snapshot.SelectedVehicles)
                {
                    if (passengerId == transportId)
                    {
                        continue;
                    }
                    const Unit* passenger = FindUnit(snapshot, passengerId);
                    if (passenger && ValidVehiclePassenger(*passenger) &&
                        passenger->Size <= transport->SizeLimit &&
                        LoadingAllowed(snapshot, *passenger, *transport))
                    {
                        // 双方都能互装时，只保留排名较高的一方作为本轮载具。
                        const bool reverseAllowed = ValidTransport(snapshot, *passenger) &&
                            ValidVehiclePassenger(*transport) &&
                            transport->Size <= passenger->SizeLimit &&
                            LoadingAllowed(snapshot, *transport, *passenger);
                        if (reverseAllowed &&
                            std::find(rankedTransports.begin(), rankedTransports.end(), transportId) >
                                std::find(rankedTransports.begin(), rankedTransports.end(), passengerId))
                        {
                            continue;
                        }
                        return true;
                    }
                }
                return false;
            };
            transports.erase(std::remove_if(transports.begin(), transports.end(),
                [&](UnitId id) { return !canCarrySelected(id); }), transports.end());

            std::vector<UnitId> ambivalentTransports;
            for (const UnitId id : transports)
            {
                const Unit* passenger = FindUnit(snapshot, id);
                if (!passenger || !ValidVehiclePassenger(*passenger))
                {
                    continue;
                }
                for (const UnitId otherId : transports)
                {
                    if (id == otherId)
                    {
                        continue;
                    }
                    const Unit* other = FindUnit(snapshot, otherId);
                    if (other && passenger->Size <= other->SizeLimit &&
                        LoadingAllowed(snapshot, *passenger, *other))
                    {
                        ambivalentTransports.push_back(id);
                        break;
                    }
                }
            }
            std::stable_sort(transports.begin(), transports.end(), [&](UnitId a, UnitId b)
            {
                const bool leftAmbivalent = std::find(ambivalentTransports.begin(),
                    ambivalentTransports.end(), a) != ambivalentTransports.end();
                const bool rightAmbivalent = std::find(ambivalentTransports.begin(),
                    ambivalentTransports.end(), b) != ambivalentTransports.end();
                if (leftAmbivalent != rightAmbivalent) return leftAmbivalent;
                return false;
            });

            std::vector<UnitId> vehiclePassengers;
            for (const UnitId id : snapshot.SelectedVehicles)
            {
                const Unit* unit = FindUnit(snapshot, id);
                const bool canCarry = std::find(transports.begin(), transports.end(), id) != transports.end();
                if (!canCarry && unit && ValidVehiclePassenger(*unit))
                {
                    vehiclePassengers.push_back(id);
                }
            }

            if (transports.empty() || vehiclePassengers.empty())
            {
                return PlanInfantryFallback(snapshot);
            }

            auto result = Assign(snapshot, vehiclePassengers, transports,
                PairKind::VehicleIntoVehicle);
            return result.empty() ? PlanInfantryFallback(snapshot) : result;
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
        if (mixedSelection)
        {
            std::vector<UnitId> openToppedTransports;
            for (const UnitId id : transports)
            {
                const Unit* transport = FindUnit(snapshot, id);
                if (transport && transport->IsOpenTopped && transport->PassengerCapacity > 1)
                {
                    openToppedTransports.push_back(id);
                }
            }
            if (openToppedTransports.size() > 1)
            {
                auto balanced = AssignOpenToppedEvenly(snapshot, passengers, openToppedTransports);
                if (!balanced.empty())
                {
                    std::vector<UnitId> remainingPassengers;
                    for (const UnitId id : passengers)
                    {
                        if (std::none_of(balanced.begin(), balanced.end(),
                                [id](const Pair& pair) { return pair.Passenger == id; }))
                        {
                            remainingPassengers.push_back(id);
                        }
                    }
                    std::vector<UnitId> otherTransports;
                    for (const UnitId id : transports)
                    {
                        if (std::find(openToppedTransports.begin(), openToppedTransports.end(), id) ==
                            openToppedTransports.end())
                        {
                            otherTransports.push_back(id);
                        }
                    }
                    auto remainder = Assign(snapshot, remainingPassengers, otherTransports, PairKind::Command);
                    balanced.insert(balanced.end(), remainder.begin(), remainder.end());
                    return balanced;
                }
            }
        }
        return Assign(snapshot, passengers, transports, PairKind::Command);
    }

    std::vector<Pair> PlanSafeMode(const Snapshot& snapshot)
    {
        const auto infantryPairs = snapshot.SelectedInfantries.empty()
            ? PlanInfantryFallback(snapshot)
            : Plan(snapshot);
        if (!infantryPairs.empty())
        {
            const UnitId transportId = infantryPairs.front().Transport;
            Snapshot focused = snapshot;
            if (focused.SelectedInfantries.empty() || !focused.SelectedVehicles.empty())
            {
                focused.SelectedVehicles = {transportId};
            }
            else
            {
                focused.FriendlyTransports = {transportId};
            }
            return focused.SelectedInfantries.empty()
                ? PlanInfantryFallback(focused) : Plan(focused);
        }

        if (snapshot.SelectedVehicles.empty())
        {
            return {};
        }
        const auto vehiclePairs = PlanVehicleSelection(snapshot);
        UnitId transportId = 0;
        for (const auto& pair : vehiclePairs)
        {
            if (pair.Kind == PairKind::VehicleIntoVehicle)
            {
                transportId = pair.Transport;
                break;
            }
        }
        std::vector<Pair> selected;
        for (const auto& pair : vehiclePairs)
        {
            if (pair.Kind == PairKind::VehicleIntoVehicle && pair.Transport == transportId)
            {
                selected.push_back(pair);
            }
        }
        return selected;
    }
}
