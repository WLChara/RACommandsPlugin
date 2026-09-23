#include "Commands/TeslaChargeCommand/TeslaChargePlanner.h"
#include "Commands/TeslaChargeCommand/TeslaChargeCommandService.h"

#include <cstdint>
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

    tesla_charge::ObjectSnapshot Object(
        tesla_charge::UnitId id, std::uintptr_t owner, int x, int y = 0)
    {
        return {id, owner, x, y};
    }

    void TestNearestUniqueAndLocalOwner()
    {
        tesla_charge::Snapshot snapshot;
        snapshot.LocalOwner = 1;
        snapshot.Teslas = {Object(1, 1, 0), Object(2, 1, 20), Object(3, 2, 0)};
        snapshot.Chargers = {
            Object(10, 1, 2), Object(11, 1, 21), Object(12, 2, 1)
        };
        const auto plan = tesla_charge::Plan(snapshot, {});
        Require(plan == std::vector<tesla_charge::Assignment>{{1, 10}, {2, 11}},
            "each local Tesla needs its nearest distinct local charger");

        snapshot.Teslas = {Object(1, 1, 0)};
        snapshot.Chargers = {Object(10, 1, 33), Object(11, 1, 32)};
        Require(tesla_charge::Plan(snapshot, {}) ==
            std::vector<tesla_charge::Assignment>{{1, 11}},
            "32 cells must be included and 33 cells excluded");

        snapshot.Chargers[1].Owner = 2;
        Require(tesla_charge::Plan(snapshot, {}).empty(),
            "non-local charger cannot serve a local Tesla");
    }

    void TestExistingAssignments()
    {
        tesla_charge::Snapshot snapshot;
        snapshot.LocalOwner = 1;
        snapshot.Teslas = {Object(1, 1, 0), Object(2, 1, 10)};
        snapshot.Chargers = {Object(10, 1, 1), Object(11, 1, 9)};

        const std::vector<tesla_charge::Assignment> previous = {{1, 11}, {2, 10}};
        Require(tesla_charge::Plan(snapshot, previous) == previous,
            "valid assignments must remain stable between maintenance ticks");

        snapshot.Chargers[1].CellX = 100;
        const auto replanned = tesla_charge::Plan(snapshot, previous);
        Require(replanned == std::vector<tesla_charge::Assignment>{{2, 10}},
            "an out-of-range assignment must be dropped without stealing a valid pair");
    }

    class FakeGame final : public tesla_charge::ITeslaChargeGamePort,
        public commands::IClickedMissionGamePort
    {
    public:
        bool MatchReady = true;
        bool Targeting = false;
        mutable bool Selected = true;
        std::uintptr_t Session = 1;
        std::uint32_t Frame = 100;
        mutable std::uint32_t NativeFree = 12;
        mutable int Attempts = 0;
        mutable int Deselects = 0;
        mutable std::vector<commands::ClickedMissionProducer> AttemptedProducers;

        bool IsMatchReady() const override { return MatchReady; }
        std::uintptr_t GetSessionIdentity() const override { return Session; }
        std::uint32_t GetCurrentFrame() const override { return Frame; }
        std::uint32_t GetNativeFreeSlots() const override { return NativeFree; }
        bool CaptureSnapshot(tesla_charge::Snapshot& outSnapshot) const override
        {
            outSnapshot.LocalOwner = 1;
            outSnapshot.Teslas = {Object(1, 1, 0)};
            outSnapshot.Chargers = {Object(2, 1, 1)};
            return true;
        }
        bool MakeAttackIntent(
            tesla_charge::UnitId charger, tesla_charge::UnitId tesla,
            std::uint32_t epoch, commands::ClickedMissionIntent& outIntent) const override
        {
            outIntent.Actor = {0x2000, static_cast<std::uint32_t>(charger), 1, epoch};
            outIntent.Target = commands::ClickedMissionIdentity{
                0x1000, static_cast<std::uint32_t>(tesla), 2, epoch};
            outIntent.Mission = 1;
            outIntent.Producer = commands::ClickedMissionProducer::TeslaCharge;
            outIntent.Epoch = epoch;
            outIntent.CreatedFrame = Frame;
            outIntent.FrameSendRate = 7;
            return true;
        }
        bool IsTargetingTesla(tesla_charge::UnitId, tesla_charge::UnitId) const override
        {
            return Targeting;
        }
        void DeselectIfSelected(tesla_charge::UnitId) const override
        {
            if (Selected)
            {
                ++Deselects;
                Selected = false;
            }
        }
        bool ValidateClickedMissionIntent(const commands::ClickedMissionIntent&) const override
        {
            return true;
        }
        void AttemptClickedMission(const commands::ClickedMissionIntent& intent) const override
        {
            ++Attempts;
            AttemptedProducers.push_back(intent.Producer);
            if (NativeFree > 0)
            {
                --NativeFree;
            }
        }
    };

    void TestToggleCancellationAndSession()
    {
        FakeGame game;
        commands::ClickedMissionDispatcher dispatcher(game);
        tesla_charge::TeslaChargeCommandService service(game, dispatcher);
        dispatcher.OnGameFrame();

        service.OnHotkey();
        Require(service.IsEnabled() && game.Deselects == 1,
            "first hotkey must enable charging and deselect the assigned charger");
        dispatcher.OnGameFrame();
        Require(game.Attempts == 0,
            "charging must respect the native queue's 13-free-slot threshold");

        service.OnHotkey();
        Require(!service.IsEnabled() && dispatcher.Counters().Cancelled == 1,
            "second hotkey must disable charging and cancel its pending intent");
        game.NativeFree = 13;
        ++game.Frame;
        dispatcher.OnGameFrame();
        Require(game.Attempts == 0, "disabled charging must not issue an old intent");

        service.OnHotkey();
        ++game.Frame;
        dispatcher.OnGameFrame();
        Require(game.Attempts == 1, "enabled charging must attempt Attack when capacity returns");

        game.Selected = true;
        game.Frame += 8;
        service.OnGameFrame();
        Require(game.Deselects == 2, "selected assigned charger must be deselected again");

        game.Session = 2;
        ++game.Frame;
        dispatcher.OnGameFrame();
        service.OnGameFrame();
        Require(!service.IsEnabled(), "a new match must reset the toggle state");
    }

    void TestCancellationDoesNotTouchOtherCommands()
    {
        FakeGame game;
        commands::ClickedMissionDispatcher dispatcher(game);
        tesla_charge::TeslaChargeCommandService service(game, dispatcher);
        dispatcher.OnGameFrame();
        service.OnHotkey();

        commands::ClickedMissionIntent other;
        other.Actor = {0x3000, 3, 1, dispatcher.Epoch()};
        other.Mission = 7;
        other.TargetCell = commands::ClickedMissionIdentity{
            0x4000, 4, 2, dispatcher.Epoch()};
        other.Epoch = dispatcher.Epoch();
        other.CreatedFrame = game.Frame;
        other.FrameSendRate = 7;
        Require(dispatcher.Submit(other) == commands::ClickedMissionEnqueueResult::Enqueued,
            "another command must share the queue");

        service.OnHotkey();
        game.NativeFree = 13;
        ++game.Frame;
        dispatcher.OnGameFrame();
        Require(game.AttemptedProducers ==
            std::vector<commands::ClickedMissionProducer>{
                commands::ClickedMissionProducer::Unspecified},
            "disabling Tesla charging must preserve other producers' intents");
    }
}

void RunTeslaChargeTests()
{
    TestNearestUniqueAndLocalOwner();
    TestExistingAssignments();
    TestToggleCancellationAndSession();
    TestCancellationDoesNotTouchOtherCommands();
}
