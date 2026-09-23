#include "Commands/AutoLoadCommand/AutoLoadPlanner.h"
#include "Commands/AutoLoadCommand/AutoLoadCommandService.h"
#include "ClickedMission/ClickedMissionQueue.h"

#include <cstdint>
#include <iostream>
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
        std::uintptr_t Session = 1;
        std::uint32_t Frame = 100;
        mutable std::uint32_t NativeFree = 12;
        mutable int Attempts = 0;
        mutable int Deselects = 0;
        mutable std::vector<std::uint32_t> AttemptedActors;

        bool IsMatchReady() const override { return MatchReady; }
        std::uintptr_t GetSessionIdentity() const override { return Session; }
        bool CaptureSnapshot(autoload::Snapshot& outSnapshot) const override
        {
            outSnapshot.Units = { Infantry(1), Vehicle(2, 1, 2.0) };
            outSnapshot.SelectedInfantries = { 1 };
            outSnapshot.SelectedVehicles = { 2 };
            return true;
        }
        bool MakeEnterIntent(autoload::UnitId passengerId, autoload::UnitId transportId,
            std::uint32_t epoch,
            commands::ClickedMissionIntent& outIntent) const override
        {
            outIntent = EnterIntent(Frame, 7);
            outIntent.Actor.UniqueId = static_cast<std::uint32_t>(passengerId);
            outIntent.TargetCell->UniqueId = static_cast<std::uint32_t>(transportId);
            outIntent.Actor.Epoch = epoch;
            outIntent.TargetCell->Epoch = epoch;
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
        autoload::AutoLoadCommandService service(game, dispatcher);
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

int main()
{
    try
    {
        TestMixedSelection();
        TestInfantryOnly();
        TestVehicleModeAndFallback();
        TestPriorityLimitAndOccupiedSeats();
        TestIntentDedupeAndDeadline();
        TestCapacityAndEpoch();
        TestServiceBackpressureAndSessionReset();
        TestSharedDispatcherOrderAndCapacity();
        std::cout << "RACommandsPlugin pure tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
