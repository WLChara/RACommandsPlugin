#include "Commands/AutoLoadCommand/AutoLoadPlanner.h"
#include "Commands/AutoLoadCommand/AutoLoadCommandService.h"
#include "ClickedMission/ClickedMissionQueue.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace ra_commands;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    autoload::Unit Infantry(autoload::UnitId id, int x = 0)
    {
        autoload::Unit unit;
        unit.Id = id;
        unit.Kind = autoload::UnitKind::Infantry;
        unit.TypeName = "GI";
        unit.HasOwner = true;
        unit.IsLocalOrAllied = true;
        unit.IsInPlayfield = true;
        unit.HasType = true;
        unit.Size = 1.0;
        unit.X = x;
        return unit;
    }

    autoload::Unit Vehicle(autoload::UnitId id, int capacity, double sizeLimit)
    {
        autoload::Unit unit;
        unit.Id = id;
        unit.Kind = autoload::UnitKind::Vehicle;
        unit.TypeName = "APC";
        unit.HasOwner = true;
        unit.IsLocalOrAllied = true;
        unit.IsInPlayfield = true;
        unit.HasType = true;
        unit.Size = 1.0;
        unit.SizeLimit = sizeLimit;
        unit.PassengerCapacity = capacity;
        return unit;
    }

    void TestMixedSelection()
    {
        autoload::Snapshot snapshot;
        snapshot.Units = { Infantry(1), Vehicle(2, 1, 2.0) };
        snapshot.SelectedInfantries = { 1 };
        snapshot.SelectedVehicles = { 2 };
        const auto pairs = autoload::Plan(snapshot);
        Require(pairs.size() == 1 && pairs[0].Passenger == 1 && pairs[0].Transport == 2,
            "mixed selection should load selected infantry into selected transport");

        snapshot.Units[0].IsDog = true;
        Require(autoload::Plan(snapshot).empty(), "dogs must not be loaded");
        snapshot.Units[0].IsDog = false;
        snapshot.Units[0].UsesFlyingMovement = true;
        Require(autoload::Plan(snapshot).empty(), "flying passengers must not be loaded");
        snapshot.Units[0].UsesFlyingMovement = false;
        snapshot.Units[1].SizeLimit = 0.5;
        Require(autoload::Plan(snapshot).empty(), "SizeLimit must be respected");
    }

    void TestInfantryOnly()
    {
        autoload::Snapshot snapshot;
        snapshot.Units = { Infantry(1), Vehicle(2, 1, 2.0) };
        snapshot.SelectedInfantries = { 1 };
        snapshot.FriendlyTransports = { 2 };
        Require(autoload::Plan(snapshot).size() == 1,
            "infantry-only selection should find a friendly transport");
        snapshot.Units[0].IsLocalOrAllied = false;
        Require(autoload::Plan(snapshot).empty(),
            "infantry-only selection should reject nonfriendly infantry");
    }

    void TestVehicleModeAndFallback()
    {
        autoload::Snapshot snapshot;
        snapshot.Units = { Vehicle(1, 1, 2.0), Vehicle(2, 0, 0.0), Infantry(3) };
        snapshot.SelectedVehicles = { 1, 2 };
        snapshot.FriendlyPassengers = { 3 };

        const auto vehiclePairs = autoload::Plan(snapshot);
        Require(vehiclePairs.size() == 1 && vehiclePairs[0].Passenger == 2 &&
            vehiclePairs[0].Transport == 1 &&
            vehiclePairs[0].Kind == autoload::PairKind::VehicleIntoVehicle,
            "vehicle-only selection should prefer vehicle loading");

        snapshot.Units[1].PassengerCapacity = 1;
        const auto transportPassengerPairs = autoload::Plan(snapshot);
        Require(transportPassengerPairs.size() == 1 &&
            transportPassengerPairs[0].Passenger == 2 &&
            transportPassengerPairs[0].Transport == 1 &&
            transportPassengerPairs[0].Kind == autoload::PairKind::VehicleIntoVehicle,
            "an empty transport-capable vehicle should also be loadable into the primary transport");
        snapshot.Units[1].PassengerCapacity = 0;

        snapshot.Units[1].Size = 3.0;
        const auto fallbackPairs = autoload::Plan(snapshot);
        Require(fallbackPairs.size() == 1 && fallbackPairs[0].Passenger == 3 &&
            fallbackPairs[0].Kind == autoload::PairKind::InfantryFallback,
            "incompatible vehicles should fall back to friendly infantry");

        snapshot.Units[1].Size = 1.0;
        snapshot.Units[1].UsesFlyingMovement = true;
        const auto flyingFallback = autoload::Plan(snapshot);
        Require(flyingFallback.size() == 1 && flyingFallback[0].Passenger == 3 &&
            flyingFallback[0].Kind == autoload::PairKind::InfantryFallback,
            "flying vehicle passengers must not suppress infantry fallback");
    }

    void TestSafeModeFillsOneTransportAndPrefersInfantry()
    {
        autoload::Snapshot snapshot;
        snapshot.Units = {
            Infantry(1), Infantry(2), Infantry(3),
            Vehicle(10, 2, 2.0), Vehicle(11, 2, 2.0)
        };
        snapshot.SelectedInfantries = {1, 2, 3};
        snapshot.SelectedVehicles = {10, 11};
        const auto pairs = autoload::PlanSafeMode(snapshot);
        Require(pairs.size() == 2 && pairs[0].Transport == pairs[1].Transport &&
            pairs[0].Passenger != pairs[1].Passenger,
            "safe mode must fill one transport with multiple infantry");

        auto vehiclePassenger = Vehicle(12, 0, 0.0);
        snapshot.Units = {Vehicle(10, 2, 2.0), vehiclePassenger, Infantry(3)};
        snapshot.SelectedInfantries.clear();
        snapshot.SelectedVehicles = {10, 12};
        snapshot.FriendlyPassengers = {3};
        const auto infantryFirst = autoload::PlanSafeMode(snapshot);
        Require(infantryFirst.size() == 1 && infantryFirst[0].Passenger == 3 &&
            infantryFirst[0].Kind == autoload::PairKind::InfantryFallback,
            "safe mode must prefer infantry even with vehicle-only selection");

        snapshot.FriendlyPassengers.clear();
        const auto vehicleFallback = autoload::PlanSafeMode(snapshot);
        Require(vehicleFallback.size() == 1 && vehicleFallback[0].Passenger == 12 &&
            vehicleFallback[0].Kind == autoload::PairKind::VehicleIntoVehicle,
            "safe mode must allow vehicle loading when no infantry pair exists");
    }

    void TestSeveralTransportCapableVehicles()
    {
        autoload::Snapshot snapshot;
        snapshot.Units = {
            Vehicle(1, 2, 2.0), Vehicle(2, 2, 0.5), Vehicle(3, 1, 0.5)
        };
        snapshot.Units[0].Size = 5.0;
        snapshot.SelectedVehicles = { 1, 2, 3 };

        const auto pairs = autoload::Plan(snapshot);
        Require(pairs.size() == 2 &&
            pairs[0].Transport == 1 && pairs[1].Transport == 1 &&
            pairs[0].Passenger != pairs[1].Passenger &&
            pairs[0].Kind == autoload::PairKind::VehicleIntoVehicle &&
            pairs[1].Kind == autoload::PairKind::VehicleIntoVehicle,
            "transport-capable passengers must share the primary transport without cycles");

        snapshot.Units[1].PassengerCount = 1;
        const auto occupiedPassengerPairs = autoload::Plan(snapshot);
        Require(occupiedPassengerPairs.size() == 1 &&
            occupiedPassengerPairs[0].Passenger == 3 &&
            occupiedPassengerPairs[0].Transport == 1,
            "a partially loaded vehicle must not become a nested passenger");

        snapshot.Units[1].PassengerCount = 2;
        const auto fullPassengerPairs = autoload::Plan(snapshot);
        Require(fullPassengerPairs.size() == 2 &&
            std::any_of(fullPassengerPairs.begin(), fullPassengerPairs.end(),
                [](const autoload::Pair& pair) { return pair.Passenger == 2 && pair.Transport == 1; }),
            "an already-full vehicle may enter a compatible larger transport");
    }

    void TestMultipleVehicleTransports()
    {
        autoload::Snapshot snapshot;
        for (autoload::UnitId id = 1; id <= 4; ++id)
        {
            auto ship = Vehicle(id, 4, 2.0);
            ship.Size = 5.0;
            snapshot.Units.push_back(ship);
            snapshot.SelectedVehicles.push_back(id);
        }
        for (autoload::UnitId id = 100; id < 113; ++id)
        {
            snapshot.Units.push_back(Vehicle(id, 0, 0.0));
            snapshot.SelectedVehicles.push_back(id);
        }

        const auto pairs = autoload::Plan(snapshot);
        int counts[4] = {};
        for (const auto& pair : pairs)
        {
            Require(pair.Passenger >= 100 && pair.Passenger < 113 &&
                pair.Transport >= 1 && pair.Transport <= 4 &&
                pair.Kind == autoload::PairKind::VehicleIntoVehicle,
                "only the 13 small vehicles should enter the four transports");
            ++counts[pair.Transport - 1];
        }
        Require(pairs.size() == 13 && counts[0] == 4 && counts[1] == 4 &&
            counts[2] == 4 && counts[3] == 1,
            "13 vehicles should be distributed as 4/4/4/1");
    }

    void TestAmbivalentVehiclePrefersCarrying()
    {
        autoload::Snapshot snapshot;
        auto large = Vehicle(1, 4, 3.0);
        large.Size = 5.0;
        auto middle = Vehicle(2, 2, 1.0);
        middle.Size = 2.0;
        snapshot.Units = { large, middle, Vehicle(3, 0, 0.0) };
        snapshot.SelectedVehicles = { 1, 2, 3 };

        const auto pairs = autoload::Plan(snapshot);
        Require(pairs.size() == 1 && pairs[0].Passenger == 3 &&
            pairs[0].Transport == 2,
            "an ambivalent vehicle should carry its compatible child before entering a larger vehicle");

        snapshot.Units[1].PassengerCount = 2;
        const auto fullMiddlePairs = autoload::Plan(snapshot);
        Require(std::any_of(fullMiddlePairs.begin(), fullMiddlePairs.end(),
            [](const autoload::Pair& pair)
            {
                return pair.Passenger == 2 && pair.Transport == 1;
            }), "a vehicle already full at snapshot time may enter a compatible larger vehicle");
    }

    void TestMutuallyCompatibleVehiclesAvoidCycle()
    {
        autoload::Snapshot snapshot;
        snapshot.Units = { Vehicle(1, 2, 2.0), Vehicle(2, 1, 2.0) };
        snapshot.SelectedVehicles = { 1, 2 };

        const auto pairs = autoload::Plan(snapshot);
        Require(pairs.size() == 1 && pairs[0].Passenger == 2 && pairs[0].Transport == 1,
            "mutually compatible empty transports need one deterministic loading direction");
    }

    void TestVehicleFitIsCheckedPerTransport()
    {
        autoload::Snapshot snapshot;
        auto first = Vehicle(1, 4, 1.0);
        first.Size = 5.0;
        auto second = Vehicle(2, 4, 3.0);
        second.Size = 5.0;
        auto passenger = Vehicle(3, 0, 0.0);
        passenger.Size = 2.0;
        snapshot.Units = { first, second, passenger };
        snapshot.SelectedVehicles = { 1, 2, 3 };

        const auto pairs = autoload::Plan(snapshot);
        Require(pairs.size() == 1 && pairs[0].Passenger == 3 && pairs[0].Transport == 2,
            "a transport that cannot fit one vehicle must not block another compatible transport");
    }

    void TestPriorityLimitAndOccupiedSeats()
    {
        autoload::Snapshot snapshot;
        snapshot.Units = { Infantry(1), Infantry(2, 100), Vehicle(3, 2, 2.0) };
        snapshot.SelectedInfantries = { 1, 2 };
        snapshot.SelectedVehicles = { 3 };
        snapshot.LoadPolicy.UseCustomRules = true;
        autoload::LoadingRule rule;
        rule.Kind = autoload::RuleKind::Priority;
        rule.TransportName = "APC";
        rule.PassengerName = "GI";
        rule.Priority = 10;
        rule.MaxCount = 1;
        snapshot.LoadPolicy.LoadingRules.push_back(rule);
        Require(autoload::Plan(snapshot).size() == 1,
            "priority MaxCount must limit passenger type per transport");

        snapshot.LoadPolicy.UseCustomRules = false;
        snapshot.Units[2].PassengerCount = 1;
        Require(autoload::Plan(snapshot).size() == 1,
            "occupied seats must reduce available capacity");
    }

    void TestOpenToppedDistributionByType()
    {
        autoload::Snapshot snapshot;
        for (autoload::UnitId id = 1; id <= 10; ++id)
        {
            auto fortress = Vehicle(id, 5, 2.0);
            fortress.TypeName = "BFRT";
            fortress.IsOpenTopped = true;
            snapshot.Units.push_back(fortress);
            snapshot.SelectedVehicles.push_back(id);
        }

        const char* types[] = { "GGI", "GI", "GHOST", "SUPR" };
        for (int typeIndex = 0; typeIndex < 4; ++typeIndex)
        {
            for (int unitIndex = 0; unitIndex < 10; ++unitIndex)
            {
                const auto id = static_cast<autoload::UnitId>(100 + typeIndex * 10 + unitIndex);
                auto infantry = Infantry(id);
                infantry.TypeName = types[typeIndex];
                snapshot.Units.push_back(infantry);
                snapshot.SelectedInfantries.push_back(id);
            }
        }

        const auto pairs = autoload::Plan(snapshot);
        int counts[10][4] = {};
        for (const auto& pair : pairs)
        {
            Require(pair.Transport >= 1 && pair.Transport <= 10 &&
                pair.Passenger >= 100 && pair.Passenger < 140,
                "balanced loading must use selected infantry and fortresses");
            ++counts[pair.Transport - 1][(pair.Passenger - 100) / 10];
        }
        Require(pairs.size() == 40, "all four infantry types must be assigned");
        for (const auto& fortress : counts)
        {
            for (const int count : fortress)
            {
                Require(count == 1, "each fortress needs one passenger of each type");
            }
        }
    }

    void TestOpenToppedBalanceRespectsCompatibility()
    {
        autoload::Snapshot snapshot;
        auto smallLimit = Vehicle(1, 2, 1.0);
        auto largeLimit = Vehicle(2, 2, 2.0);
        smallLimit.IsOpenTopped = true;
        largeLimit.IsOpenTopped = true;
        auto bigA = Infantry(10);
        auto bigB = Infantry(11);
        bigA.TypeName = bigB.TypeName = "BIG";
        bigA.Size = bigB.Size = 2.0;
        auto smallA = Infantry(12);
        auto smallB = Infantry(13);
        smallA.TypeName = smallB.TypeName = "SMALL";
        snapshot.Units = { smallLimit, largeLimit, bigA, bigB, smallA, smallB };
        snapshot.SelectedVehicles = { 1, 2 };
        snapshot.SelectedInfantries = { 10, 11, 12, 13 };

        const auto pairs = autoload::Plan(snapshot);
        Require(pairs.size() == 4, "compatible passengers should use all available seats");
        for (const auto& pair : pairs)
        {
            if (pair.Passenger == 10 || pair.Passenger == 11)
            {
                Require(pair.Transport == 2, "oversize infantry must skip the smaller SizeLimit");
            }
            else
            {
                Require(pair.Transport == 1, "remaining infantry should use the other transport");
            }
        }

        snapshot.LoadPolicy.UseCustomRules = true;
        snapshot.Units[0].PassengerCapacity = 5;
        snapshot.Units[1].PassengerCapacity = 5;
        auto smallC = Infantry(14);
        auto smallD = Infantry(15);
        smallC.TypeName = smallD.TypeName = "SMALL";
        snapshot.Units.push_back(smallC);
        snapshot.Units.push_back(smallD);
        snapshot.SelectedInfantries.push_back(14);
        snapshot.SelectedInfantries.push_back(15);
        autoload::LoadingRule rule;
        rule.Kind = autoload::RuleKind::Priority;
        rule.PassengerName = "SMALL";
        rule.Priority = 10;
        rule.MaxCount = 1;
        snapshot.LoadPolicy.LoadingRules.push_back(rule);
        const auto limitedPairs = autoload::Plan(snapshot);
        Require(std::count_if(limitedPairs.begin(), limitedPairs.end(),
            [](const autoload::Pair& pair) { return pair.Passenger >= 12 && pair.Passenger <= 15; }) == 2,
            "custom MaxCount must still limit each passenger type per transport");
    }

    void TestOpenToppedBalanceLeavesOtherTransportsAvailable()
    {
        autoload::Snapshot snapshot;
        for (autoload::UnitId id = 1; id <= 2; ++id)
        {
            auto fortress = Vehicle(id, 2, 2.0);
            fortress.IsOpenTopped = true;
            snapshot.Units.push_back(fortress);
            snapshot.SelectedVehicles.push_back(id);
        }
        snapshot.Units.push_back(Vehicle(3, 1, 2.0));
        snapshot.SelectedVehicles.push_back(3);
        for (autoload::UnitId id = 10; id < 15; ++id)
        {
            snapshot.Units.push_back(Infantry(id));
            snapshot.SelectedInfantries.push_back(id);
        }

        const auto pairs = autoload::Plan(snapshot);
        Require(pairs.size() == 5 &&
            std::count_if(pairs.begin(), pairs.end(),
                [](const autoload::Pair& pair) { return pair.Transport == 3; }) == 1,
            "unassigned infantry should still enter a selected non-open-topped transport");
    }

    commands::ClickedMissionIntent EnterIntent(std::uint32_t frame, std::int32_t rate)
    {
        commands::ClickedMissionIntent intent;
        intent.Actor = { 0x1000, 1, 1, 7 };
        intent.Mission = 7;
        intent.TargetCell = commands::ClickedMissionIdentity{ 0x2000, 2, 2, 7 };
        intent.Epoch = 7;
        intent.CreatedFrame = frame;
        intent.FrameSendRate = rate;
        return intent;
    }

    void TestIntentDedupeAndDeadline()
    {
        commands::ClickedMissionQueue queue(4, 7);
        const auto intent = EnterIntent(100, 7);
        Require(queue.Enqueue(intent) == commands::ClickedMissionEnqueueResult::Enqueued,
            "first intent should enqueue");
        Require(queue.Enqueue(EnterIntent(101, 7)) == commands::ClickedMissionEnqueueResult::Duplicate,
            "same pending mission should deduplicate without refreshing deadline");

        int attempts = 0;
        const auto result = queue.Drain(135, [] { return 13u; },
            [](const auto&) { return true; },
            [&](const auto&) { ++attempts; });
        Require(result.Expired == 1 && attempts == 0 && queue.Size() == 0,
            "rate 7 must expire at 35 frames from original enqueue");

        Require(queue.Enqueue(EnterIntent(100, 7)) ==
            commands::ClickedMissionEnqueueResult::Enqueued,
            "same mission may be reissued after the old pending item is removed");
        const auto beforeExpiry = queue.Drain(134, [] { return 13u; },
            [](const auto&) { return true; },
            [&](const auto&) { ++attempts; });
        Require(beforeExpiry.Attempted == 1 && attempts == 1,
            "rate 7 mission remains eligible at age 34");
    }

    void TestCapacityAndEpoch()
    {
        commands::ClickedMissionQueue queue(2, 7);
        auto first = EnterIntent(10, 1);
        auto second = first;
        second.Actor.UniqueId = 3;
        second.Actor.Address = 0x3000;
        Require(queue.Enqueue(first) == commands::ClickedMissionEnqueueResult::Enqueued,
            "first capacity test intent should enqueue");
        Require(queue.Enqueue(second) == commands::ClickedMissionEnqueueResult::Enqueued,
            "second capacity test intent should enqueue");

        int freeChecks = 0;
        int attempts = 0;
        const auto result = queue.Drain(11,
            [&] { return ++freeChecks == 1 ? 13u : 12u; },
            [](const auto&) { return true; },
            [&](const auto&) { ++attempts; });
        Require(result.Attempted == 1 && result.WasStoppedForCapacity &&
            attempts == 1 && queue.Size() == 1,
            "fresh capacity must be checked before every attempted issue");

        queue.Reset(8);
        Require(queue.Size() == 0 &&
            queue.Enqueue(first) == commands::ClickedMissionEnqueueResult::WrongEpoch,
            "match reset must reject old-epoch intents");
    }

    class FakeGame final : public autoload::IAutoLoadGamePort,
        public commands::IClickedMissionGamePort
    {
    public:
        bool MatchReady = true;
        bool VehicleOnly = false;
        std::optional<autoload::Snapshot> CustomSnapshot;
        std::uintptr_t Session = 1;
        std::uint32_t Frame = 100;
        std::uint64_t NowMs = 1'000;
        mutable std::uint32_t NativeFree = 12;
        mutable int Attempts = 0;
        mutable int Deselects = 0;
        mutable std::vector<std::uint32_t> AttemptedActors;
        mutable std::vector<autoload::UnitId> MadeTargets;

        bool IsMatchReady() const override { return MatchReady; }
        std::uintptr_t GetSessionIdentity() const override { return Session; }
        bool CaptureSnapshot(autoload::Snapshot& outSnapshot) const override
        {
            if (CustomSnapshot)
            {
                outSnapshot = *CustomSnapshot;
                return true;
            }
            if (VehicleOnly)
            {
                auto primary = Vehicle(1, 2, 2.0);
                primary.Size = 5.0;
                outSnapshot.Units = { primary, Vehicle(2, 1, 0.5) };
                outSnapshot.SelectedInfantries.clear();
                outSnapshot.SelectedVehicles = { 1, 2 };
                return true;
            }
            outSnapshot.Units = { Infantry(1), Vehicle(2, 1, 2.0) };
            outSnapshot.SelectedInfantries = { 1 };
            outSnapshot.SelectedVehicles = { 2 };
            return true;
        }
        std::uint64_t GetCurrentTimeMs() const override { return NowMs; }
        bool MakeEnterIntent(autoload::UnitId passengerId, autoload::UnitId transportId,
            std::uint32_t epoch,
            commands::ClickedMissionIntent& outIntent) const override
        {
            MadeTargets.push_back(transportId);
            outIntent = EnterIntent(Frame, 7);
            outIntent.Actor.UniqueId = static_cast<std::uint32_t>(passengerId);
            outIntent.TargetCell->UniqueId = static_cast<std::uint32_t>(transportId);
            outIntent.Actor.Epoch = epoch;
            outIntent.TargetCell->Epoch = epoch;
            outIntent.Producer = commands::ClickedMissionProducer::AutoLoad;
            outIntent.Epoch = epoch;
            return true;
        }
        bool ValidateClickedMissionIntent(const commands::ClickedMissionIntent&) const override
        {
            return true;
        }
        void AttemptClickedMission(const commands::ClickedMissionIntent& intent) const override
        {
            ++Attempts;
            AttemptedActors.push_back(intent.Actor.UniqueId);
            if (NativeFree > 0)
            {
                --NativeFree;
            }
        }
        void Deselect(autoload::UnitId) const override
        {
            ++Deselects;
        }
        std::uint32_t GetCurrentFrame() const override { return Frame; }
        std::uint32_t GetNativeFreeSlots() const override { return NativeFree; }
    };

    void TestServiceBackpressureAndSessionReset()
    {
        FakeGame game;
        commands::ClickedMissionDispatcher dispatcher(game);
        std::atomic<bool> isSafeModeEnabled{false};
        autoload::AutoLoadCommandService service(game, dispatcher, isSafeModeEnabled);
        dispatcher.OnGameFrame();
        service.OnHotkey();
        Require(game.Deselects == 2, "accepted intent should deselect the used pair");
        dispatcher.OnGameFrame();
        Require(game.Attempts == 0, "12 native free slots must block Enter");

        game.NativeFree = 13;
        ++game.Frame;
        dispatcher.OnGameFrame();
        Require(game.Attempts == 1, "13 native free slots should attempt Enter once");

        game.NativeFree = 12;
        service.OnHotkey();
        game.Session = 2;
        ++game.Frame;
        dispatcher.OnGameFrame();
        game.NativeFree = 13;
        ++game.Frame;
        dispatcher.OnGameFrame();
        Require(game.Attempts == 1, "old-session intent must not execute in a new session");
    }

    void TestVehicleServiceDispatch()
    {
        FakeGame game;
        game.VehicleOnly = true;
        game.NativeFree = 13;
        commands::ClickedMissionDispatcher dispatcher(game);
        std::atomic<bool> isSafeModeEnabled{false};
        autoload::AutoLoadCommandService service(game, dispatcher, isSafeModeEnabled);
        dispatcher.OnGameFrame();
        service.OnHotkey();
        Require(game.Deselects == 2,
            "vehicle loading should deselect the passenger and primary transport");

        ++game.Frame;
        dispatcher.OnGameFrame();
        Require(game.AttemptedActors.size() == 1 && game.AttemptedActors[0] == 2,
            "vehicle loading should submit the secondary vehicle as the Enter actor");
    }

    void TestSafeModeReservationsAcrossHotkeys()
    {
        FakeGame game;
        autoload::Snapshot snapshot;
        snapshot.Units = {
            Infantry(1), Infantry(2), Infantry(3), Infantry(4),
            Vehicle(10, 2, 2.0), Vehicle(11, 2, 2.0)
        };
        snapshot.SelectedInfantries = {1, 2, 3, 4};
        snapshot.SelectedVehicles = {10, 11};
        game.CustomSnapshot = snapshot;
        std::atomic<bool> isSafeModeEnabled{true};
        commands::ClickedMissionDispatcher dispatcher(game);
        autoload::AutoLoadCommandService service(game, dispatcher, isSafeModeEnabled);
        dispatcher.OnGameFrame();

        service.OnHotkey();
        Require(game.MadeTargets == std::vector<autoload::UnitId>({10, 10}) &&
            dispatcher.Counters().Enqueued == 2,
            "one safe hotkey must submit enough passengers for only one transport");

        service.OnHotkey();
        Require(game.MadeTargets == std::vector<autoload::UnitId>({10, 10, 11, 11}) &&
            dispatcher.Counters().Enqueued == 4,
            "next safe hotkey must skip in-flight passengers and transport");

        service.OnHotkey();
        Require(dispatcher.Counters().Enqueued == 4,
            "reserved transports must stay unavailable while loading is pending");

        game.NowMs += 5'001;
        service.OnHotkey();
        Require(game.MadeTargets.size() == 4,
            "pending native intents must keep their units reserved despite elapsed time");

        game.NativeFree = 13;
        for (int index = 0; index < 4; ++index)
        {
            ++game.Frame;
            dispatcher.OnGameFrame();
            game.NativeFree = 13;
        }
        service.OnHotkey();
        Require(game.MadeTargets.size() > 4,
            "stalled reservations must expire after pending intents leave the queue");
    }

    void TestSharedDispatcherOrderAndCapacity()
    {
        FakeGame game;
        commands::ClickedMissionDispatcher dispatcher(game);
        dispatcher.OnGameFrame();

        auto first = EnterIntent(game.Frame, 7);
        first.Epoch = dispatcher.Epoch();
        first.Actor.Epoch = first.Epoch;
        first.TargetCell->Epoch = first.Epoch;
        auto second = first;
        second.Actor.Address = 0x3000;
        second.Actor.UniqueId = 3;

        Require(dispatcher.Submit(first) == commands::ClickedMissionEnqueueResult::Enqueued &&
            dispatcher.Submit(second) == commands::ClickedMissionEnqueueResult::Enqueued,
            "independent producers should share one pending queue");

        game.NativeFree = 13;
        dispatcher.OnGameFrame();
        Require(game.AttemptedActors.size() == 1 && game.AttemptedActors[0] == 1,
            "first mission should use the shared native capacity before the second");

        game.NativeFree = 13;
        ++game.Frame;
        dispatcher.OnGameFrame();
        Require(game.AttemptedActors.size() == 2 && game.AttemptedActors[1] == 3,
            "remaining mission should retain FIFO order in the next frame");
    }
}

void RunTeslaChargeTests();
void RunSelectionTests();
void RunSelectionServiceTests();
void RunAirSpreadTests();
void RunAirSpreadServiceTests();
void RunAutoRepairTests();
void RunMoveIntentTests();
void RunIfvKeyPressTrackerTests();
void RunAFloorTests();
void RunBeaconClearTests();
void RunBeaconDeletePacketTests();
void RunBeaconRecipientProgressTests();
void RunRangeDisplayTests();
void RunClickedMissionIntentHandlersTests();
void RunSafeModeToggleTests();
void RunAutoCrushPlannerTests();
void RunAutoCrushServiceTests();

int main()
{
    try
    {
        TestMixedSelection();
        TestInfantryOnly();
        TestSafeModeFillsOneTransportAndPrefersInfantry();
        TestVehicleModeAndFallback();
        TestSeveralTransportCapableVehicles();
        TestMultipleVehicleTransports();
        TestAmbivalentVehiclePrefersCarrying();
        TestMutuallyCompatibleVehiclesAvoidCycle();
        TestVehicleFitIsCheckedPerTransport();
        TestPriorityLimitAndOccupiedSeats();
        TestOpenToppedDistributionByType();
        TestOpenToppedBalanceRespectsCompatibility();
        TestOpenToppedBalanceLeavesOtherTransportsAvailable();
        TestIntentDedupeAndDeadline();
        TestCapacityAndEpoch();
        TestServiceBackpressureAndSessionReset();
        TestVehicleServiceDispatch();
        TestSafeModeReservationsAcrossHotkeys();
        TestSharedDispatcherOrderAndCapacity();
        RunTeslaChargeTests();
        RunSelectionTests();
        RunSelectionServiceTests();
        RunAirSpreadTests();
        RunAirSpreadServiceTests();
        RunAutoRepairTests();
        RunMoveIntentTests();
        RunIfvKeyPressTrackerTests();
        RunAFloorTests();
        RunBeaconClearTests();
        RunBeaconDeletePacketTests();
        RunBeaconRecipientProgressTests();
        RunRangeDisplayTests();
        RunClickedMissionIntentHandlersTests();
        RunSafeModeToggleTests();
        RunAutoCrushPlannerTests();
        RunAutoCrushServiceTests();
        std::cout << "RACommandsPlugin pure tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
