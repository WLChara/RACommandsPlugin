#include "Commands/AutoNanoCloudCommand/AutoNanoCloudPlanner.h"
#include "Commands/AutoNanoCloudCommand/AutoNanoCloudCommandService.h"

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

    constexpr auto_nano_cloud::UnitId Id(std::uint32_t uniqueId)
    {
        return {static_cast<std::uintptr_t>(uniqueId) * 0x1000u, uniqueId};
    }

    auto_nano_cloud::HunterSnapshot Hunter(std::uint32_t uniqueId, int damage)
    {
        return {Id(uniqueId), damage, 0, 0};
    }

    auto_nano_cloud::VictimSnapshot Victim(std::uint32_t uniqueId, int cost, int health)
    {
        return {Id(uniqueId), cost, health, 0, 0};
    }

    void TestTargetScoreAndDamageCount()
    {
        auto_nano_cloud::Snapshot snapshot;
        snapshot.Hunters = {Hunter(1, 130), Hunter(2, 50)};
        snapshot.SelectedVictims = {Victim(10, 100, 100), Victim(11, 300, 10)};
        auto plan = auto_nano_cloud::Plan(snapshot);
        Require(plan && plan->Victim == Id(11) && plan->Hunters.size() == 1,
            "current health must affect victim score and one sufficient hunter must be used");

        snapshot.SelectedVictims = {Victim(10, 100, 101)};
        plan = auto_nano_cloud::Plan(snapshot);
        Require(plan && plan->Hunters.size() == 2,
            "raw damage 130 must count as 100 damage after division by 1.3");

        snapshot.Hunters = {Hunter(1, 30), Hunter(2, 30)};
        plan = auto_nano_cloud::Plan(snapshot);
        Require(plan && plan->Hunters.size() == 2,
            "insufficient damage must use every available hunter");
    }

    void TestRetainedVictimAndNoop()
    {
        auto_nano_cloud::Snapshot snapshot;
        snapshot.SelectedVictims = {Victim(10, 100, 50)};
        Require(!auto_nano_cloud::Plan(snapshot), "no hunter must produce no plan");

        snapshot.Hunters = {Hunter(1, 100)};
        snapshot.RetainedVictim = Victim(11, 200, 20);
        snapshot.SelectedVictims.clear();
        const auto plan = auto_nano_cloud::Plan(snapshot);
        Require(plan && plan->Victim == Id(11) && plan->UsesRetainedVictim,
            "a surviving victim must remain available after deselection");
    }

    void TestBuildLimitEligibility()
    {
        Require(auto_nano_cloud::CanSacrificeBuildLimit(0) &&
            !auto_nano_cloud::CanSacrificeBuildLimit(1) &&
            auto_nano_cloud::CanSacrificeBuildLimit(2) &&
            auto_nano_cloud::CanSacrificeBuildLimit(-1),
            "only BuildLimit=1 must be excluded");
    }

    class FakeGame final : public auto_nano_cloud::IAutoNanoCloudGamePort,
        public commands::IClickedMissionGamePort
    {
    public:
        mutable auto_nano_cloud::Snapshot Current;
        auto_nano_cloud::VictimSnapshot StoredVictim = Victim(10, 100, 100);
        std::uint32_t Frame = 100;
        mutable std::uint32_t FreeSlots = 13;
        mutable int Deselects = 0;
        mutable std::vector<std::int32_t> IssuedMissions;

        bool CaptureSnapshot(std::optional<auto_nano_cloud::UnitId> retainedVictim,
            auto_nano_cloud::Snapshot& outSnapshot) const override
        {
            outSnapshot = Current;
            if (retainedVictim && *retainedVictim == StoredVictim.Id)
            {
                outSnapshot.RetainedVictim = StoredVictim;
            }
            return true;
        }

        bool MakeStopIntent(auto_nano_cloud::UnitId victim, std::uint32_t epoch,
            commands::ClickedMissionIntent& outIntent) const override
        {
            outIntent.Actor = {victim.Address, victim.UniqueId, 3, epoch};
            outIntent.Mission = 1;
            outIntent.Producer = commands::ClickedMissionProducer::AutoNanoCloud;
            outIntent.Epoch = epoch;
            outIntent.CreatedFrame = Frame;
            return true;
        }

        bool MakeAttackIntent(auto_nano_cloud::UnitId hunter,
            auto_nano_cloud::UnitId victim, std::uint32_t epoch,
            commands::ClickedMissionIntent& outIntent) const override
        {
            outIntent.Actor = {hunter.Address, hunter.UniqueId, 3, epoch};
            outIntent.Mission = 2;
            outIntent.Target = commands::ClickedMissionIdentity{
                victim.Address, victim.UniqueId, 3, epoch};
            outIntent.Producer = commands::ClickedMissionProducer::AutoNanoCloud;
            outIntent.Epoch = epoch;
            outIntent.CreatedFrame = Frame;
            return true;
        }

        bool DeselectAndUngroup(auto_nano_cloud::UnitId victim) const override
        {
            if (victim != StoredVictim.Id)
            {
                return false;
            }
            ++Deselects;
            Current.SelectedVictims.clear();
            return true;
        }

        bool IsMatchReady() const override { return true; }
        std::uintptr_t GetSessionIdentity() const override { return 1; }
        std::uint32_t GetCurrentFrame() const override { return Frame; }
        std::uint32_t GetNativeFreeSlots() const override { return FreeSlots; }
        bool ValidateClickedMissionIntent(const commands::ClickedMissionIntent&) const override
        {
            return true;
        }
        void AttemptClickedMission(const commands::ClickedMissionIntent& intent) const override
        {
            IssuedMissions.push_back(intent.Mission);
            --FreeSlots;
        }
    };

    void TestServiceStopsBeforeAttackAndReusesVictim()
    {
        FakeGame game;
        game.Current.Hunters = {Hunter(1, 130)};
        game.Current.SelectedVictims = {game.StoredVictim};
        commands::ClickedMissionDispatcher dispatcher(game);
        auto_nano_cloud::AutoNanoCloudCommandService service(game, dispatcher);
        dispatcher.OnGameFrame();

        service.OnHotkey();
        Require(game.Deselects == 1 && dispatcher.Counters().Enqueued == 2,
            "first hotkey must release one victim and queue stop plus attack");

        dispatcher.OnGameFrame();
        game.FreeSlots = 13;
        ++game.Frame;
        dispatcher.OnGameFrame();
        Require(game.IssuedMissions == std::vector<std::int32_t>({1, 2}),
            "native stop must be attempted before hunter attack");

        service.OnHotkey();
        Require(game.Deselects == 1 && dispatcher.Counters().Enqueued == 4,
            "second hotkey must reuse the deselected victim without deselecting again");
        service.Reset();
        Require(dispatcher.Counters().Cancelled == 2,
            "reset must cancel this command's pending intents");
    }
}

void RunAutoNanoCloudTests()
{
    TestTargetScoreAndDamageCount();
    TestRetainedVictimAndNoop();
    TestBuildLimitEligibility();
    TestServiceStopsBeforeAttackAndReusesVictim();
}
