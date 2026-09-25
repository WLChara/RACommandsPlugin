#include "Commands/AutoBuild/AutoBuildCommandService.h"
#include "Commands/AutoBuild/AutoBuildKeyPressTracker.h"

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace ra_commands::auto_build;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    std::size_t Index(BuildSlot slot)
    {
        return slot == BuildSlot::Main ? 0 : 1;
    }

    SlotSnapshot Building(int typeIndex, bool combat)
    {
        SlotSnapshot snapshot;
        snapshot.Product = ProductId{typeIndex, "BUILD" + std::to_string(typeIndex), false};
        snapshot.IsCombat = combat;
        snapshot.IsInProgress = true;
        snapshot.IsEmpty = false;
        return snapshot;
    }

    SlotSnapshot Ready(int typeIndex, bool combat)
    {
        auto snapshot = Building(typeIndex, combat);
        snapshot.IsInProgress = false;
        snapshot.IsReady = true;
        return snapshot;
    }

    class FakeGame final : public IAutoBuildGamePort
    {
    public:
        bool ReadyForMatch = true;
        std::uintptr_t Session = 100;
        std::uint32_t Frame = 20;
        bool AcceptEvents = true;
        std::array<SlotSnapshot, 2> Slots{};
        mutable std::vector<BuildSlot> Sent;

        bool IsMatchReady() const override { return ReadyForMatch; }
        std::uintptr_t GetSessionIdentity() const override { return Session; }
        std::uint32_t GetCurrentFrame() const override { return Frame; }
        bool CaptureSlot(BuildSlot slot, SlotSnapshot& outSnapshot) const override
        {
            outSnapshot = Slots[Index(slot)];
            return true;
        }
        bool TryEnqueueProduce(BuildSlot slot, const ProductId&) const override
        {
            if (!AcceptEvents)
            {
                return false;
            }
            Sent.push_back(slot);
            return true;
        }
    };

    void TestIndependentSlotsAndBackpressure()
    {
        FakeGame game;
        AutoBuildCommandService service(game);
        Require(service.OnHotkey(BuildSlot::Main) &&
            service.GetPhase(BuildSlot::Main) == BuildPhase::Armed &&
            service.GetPhase(BuildSlot::Defense) == BuildPhase::Off,
            "main toggle must arm only the main slot");

        game.Slots[0] = Building(4, false);
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Main) == BuildPhase::Building,
            "next manual main construction must become the tracked product");
        game.Slots[0] = Ready(4, false);
        service.OnGameFrame();
        game.Slots[0] = {};
        game.AcceptEvents = false;
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Main) == BuildPhase::Ready && game.Sent.empty(),
            "full native queue must retain the pending continuation");
        game.AcceptEvents = true;
        service.OnGameFrame();
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Main) == BuildPhase::Queued &&
            game.Sent == std::vector<BuildSlot>{BuildSlot::Main},
            "accepted continuation must enter once and wait for game confirmation");

        game.Slots[1] = Ready(9, true);
        Require(service.OnHotkey(BuildSlot::Defense) &&
            service.GetPhase(BuildSlot::Defense) == BuildPhase::Ready,
            "defense toggle must capture an already completed defense");
        game.Slots[1] = {};
        service.OnGameFrame();
        Require(game.Sent ==
            (std::vector<BuildSlot>{BuildSlot::Main, BuildSlot::Defense}),
            "defense continuation must not change main slot state");

        game.Slots[0] = Building(4, false);
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Main) == BuildPhase::Building,
            "matching new production must confirm the queued event");
        game.Slots[0] = Ready(4, false);
        service.OnGameFrame();
        game.Slots[0] = {};
        service.OnGameFrame();
        Require(game.Sent.size() == 2 && service.GetPhase(BuildSlot::Main) == BuildPhase::Ready,
            "cooldown must preserve a second ready continuation");
        game.Frame += 15;
        service.OnGameFrame();
        Require(game.Sent.size() == 3 && game.Sent.back() == BuildSlot::Main,
            "continuation must resume after cooldown");
    }

    void TestCancellationAndManualOverride()
    {
        FakeGame game;
        AutoBuildCommandService service(game);
        game.Slots[0] = Building(3, false);
        Require(service.OnHotkey(BuildSlot::Main), "main slot should enable");
        game.Slots[0] = Building(5, false);
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Main) == BuildPhase::Off,
            "manual product change before completion must disable continuation");

        game.Slots[0] = Building(3, false);
        Require(service.OnHotkey(BuildSlot::Main), "main slot should re-enable");
        game.Slots[0] = {};
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Main) == BuildPhase::Off,
            "cancellation before completion must disable continuation");

        game.Slots[0] = Building(3, false);
        Require(service.OnHotkey(BuildSlot::Main), "main slot should re-enable again");
        game.Slots[0].IsManuallyStopped = true;
        game.Slots[0].IsInProgress = false;
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Main) == BuildPhase::Off,
            "manual suspension must disable continuation");

        game.Slots[1] = Building(8, true);
        game.Slots[1].BuildLimit = 1;
        Require(service.OnHotkey(BuildSlot::Defense) &&
            service.GetPhase(BuildSlot::Defense) == BuildPhase::Armed,
            "limited defense type must not be captured");
        game.Slots[1] = Building(8, false);
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Defense) == BuildPhase::Armed,
            "main-category product must not be captured by defense slot");
    }

    void TestPauseToggleAndSessionReset()
    {
        FakeGame game;
        AutoBuildCommandService service(game);
        game.Slots[0] = Ready(2, false);
        Require(service.OnHotkey(BuildSlot::Main), "ready product should enable main slot");
        game.Slots[0] = Building(7, false);
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Main) == BuildPhase::Ready,
            "other product must pause a completed target");
        game.Slots[0] = {};
        service.OnGameFrame();
        Require(game.Sent.size() == 1,
            "tracked product should resume after the other product leaves");
        Require(!service.OnHotkey(BuildSlot::Main) &&
            service.GetPhase(BuildSlot::Main) == BuildPhase::Off,
            "second press must turn off and clear the tracked task");

        game.Slots[1] = Building(6, true);
        Require(service.OnHotkey(BuildSlot::Defense), "defense slot should enable");
        game.Session = 101;
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Defense) == BuildPhase::Off,
            "new match must reset both toggle states");
        Require(service.OnHotkey(BuildSlot::Defense), "defense slot should enable after reset");
        game.Frame = 0;
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Defense) == BuildPhase::Off,
            "frame rollback must clear previous match state");
    }

    void TestQueuedTimeoutAndInputRepeat()
    {
        FakeGame game;
        AutoBuildCommandService service(game);
        game.Slots[0] = Ready(2, false);
        Require(service.OnHotkey(BuildSlot::Main), "main slot should enable");
        game.Slots[0] = {};
        service.OnGameFrame();
        game.Frame += 120;
        service.OnGameFrame();
        Require(service.GetPhase(BuildSlot::Main) == BuildPhase::Off && game.Sent.size() == 1,
            "unconfirmed native event must time out without duplicate enqueue");

        AutoBuildKeyPressTracker input;
        Require(input.Consume(0x41) && !input.Consume(0x41) &&
            !input.Consume(0x841) && input.Consume(0x41),
            "long key hold must toggle only once until key-up");
        input.Reset();
        Require(input.Consume(0x41), "input reset must permit a fresh press");
    }
}

void RunAutoBuildPureTests()
{
    TestIndependentSlotsAndBackpressure();
    TestCancellationAndManualOverride();
    TestPauseToggleAndSessionReset();
    TestQueuedTimeoutAndInputRepeat();
}
