#include "Commands/BeaconClearCommand/BeaconClearCommandService.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace ra_commands::beacon_clear;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    class FakeGame final : public IBeaconClearGamePort
    {
    public:
        bool Ready = true;
        std::uintptr_t Session = 1;
        std::uint32_t Frame = 100;
        std::int32_t FrameSendRate = 8;
        std::vector<BeaconSlot> Occupied;
        std::array<int, 24> FailuresRemaining{};
        std::vector<BeaconSlot> Attempts;
        std::vector<BeaconSlot> DeletedLocally;

        bool IsMatchReady() const override { return Ready; }
        std::uintptr_t GetSessionIdentity() const override { return Session; }
        std::uint32_t GetCurrentFrame() const override { return Frame; }
        std::int32_t GetFrameSendRate() const override { return FrameSendRate; }
        std::vector<BeaconSlot> CaptureOccupiedSlots() const override { return Occupied; }

        bool TryBroadcastAndDelete(std::int32_t owner, std::int32_t slot) override
        {
            const BeaconSlot target{owner, slot};
            Require(owner >= 0 && owner < 8 && slot >= 0 && slot < 3,
                "service must never submit an invalid slot");
            Attempts.push_back(target);
            auto& failures = FailuresRemaining[owner * 3 + slot];
            if (failures > 0)
            {
                --failures;
                return false;
            }
            DeletedLocally.push_back(target);
            return true;
        }
    };

    void TestSortingAcrossOwnersAndInvalidSlots()
    {
        FakeGame game;
        game.Occupied = {{7, 2}, {0, 2}, {3, 1}, {0, 0}, {7, 2},
            {-1, 0}, {8, 0}, {0, -1}, {0, 3}};
        BeaconClearCommandService service(game);
        service.OnHotkey();
        Require(service.GetPendingCount() == 4, "only four distinct valid occupied slots qualify");

        for (int index = 0; index < 4; ++index)
        {
            service.OnGameFrame();
            service.OnGameFrame();
            Require(game.Attempts.size() == static_cast<std::size_t>(index + 1),
                "a frame may attempt at most one slot");
            ++game.Frame;
        }
        Require(game.Attempts == std::vector<BeaconSlot>{{0, 0}, {0, 2}, {3, 1}, {7, 2}},
            "slots must be sent in owner then slot order");
        Require(service.GetPendingCount() == 0, "accepted slots must leave the queue");
    }

    void TestCapacityAndEmptyCapture()
    {
        FakeGame game;
        BeaconClearCommandService service(game);
        service.OnHotkey();
        Require(service.GetPendingCount() == 0, "empty capture must not enqueue work");

        for (int owner = 7; owner >= 0; --owner)
        {
            for (int slot = 2; slot >= 0; --slot)
            {
                game.Occupied.push_back({owner, slot});
            }
        }
        game.Occupied.push_back({7, 2});
        game.Occupied.push_back({7, 1});
        game.Occupied.push_back({7, 0});
        service.OnHotkey();
        service.OnHotkey();
        Require(service.GetPendingCount() == 24, "queue must hold at most 24 unique slots");

        for (int owner = 0; owner < 8; ++owner)
        {
            for (int slot = 0; slot < 3; ++slot)
            {
                service.OnGameFrame();
                Require(game.Attempts.back() == BeaconSlot{owner, slot},
                    "all 24 slots must drain in canonical order");
                ++game.Frame;
            }
        }
        Require(game.Attempts.size() == 24 && service.GetPendingCount() == 0,
            "all accepted slots must drain exactly once");
    }

    void TestFailureRotatesBehindOtherOwners()
    {
        FakeGame game;
        game.Occupied = {{0, 0}, {1, 0}, {7, 2}};
        game.FailuresRemaining[0] = 2;
        BeaconClearCommandService service(game);
        service.OnHotkey();

        for (int index = 0; index < 5; ++index)
        {
            service.OnGameFrame();
            ++game.Frame;
        }
        Require(game.Attempts == std::vector<BeaconSlot>{
            {0, 0}, {1, 0}, {7, 2}, {0, 0}, {0, 0}},
            "a failed slot must rotate behind other pending slots");
        Require(game.DeletedLocally == std::vector<BeaconSlot>{
            {1, 0}, {7, 2}, {0, 0}},
            "failed attempts must not count as local deletion");
        Require(service.GetPendingCount() == 0, "eventual acceptance must remove the intent");
    }

    void TestTtlAndDuplicateHotkeyDoNotRefresh()
    {
        FakeGame game;
        game.Occupied = {{2, 1}};
        BeaconClearCommandService service(game);
        service.OnHotkey();
        game.Frame = 120;
        service.OnHotkey();
        Require(service.GetPendingCount() == 1, "duplicate hotkey must not duplicate intent");
        game.Frame = 132; // ceil(30 / 8) * 8 = 32 帧。
        service.OnGameFrame();
        Require(game.Attempts.empty() && service.GetPendingCount() == 0,
            "intent must expire at its original deadline");

        game.Frame = 200;
        game.FrameSendRate = 45;
        game.FailuresRemaining[7] = 1;
        service.OnHotkey();
        game.Frame = 244;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BeaconSlot>{{2, 1}},
            "rate above 30 must give one send-rate interval of lifetime");
        game.Frame = 245;
        service.OnGameFrame();
        Require(game.Attempts.size() == 1 && service.GetPendingCount() == 0,
            "failed intent must expire on its next attempted turn");

    }

    void TestInvalidRateFallsBackToThirtyFrames()
    {
        FakeGame game;
        game.Occupied = {{2, 1}};
        game.FrameSendRate = 0;
        BeaconClearCommandService service(game);
        service.OnHotkey();
        Require(service.GetPendingCount() == 1, "zero send rate must still enqueue an intent");
        game.FailuresRemaining[7] = 1;
        game.Frame = 129;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BeaconSlot>{{2, 1}},
            "zero rate fallback must allow an attempt before frame 30");
        game.Frame = 130;
        service.OnGameFrame();
        Require(game.Attempts.size() == 1 && service.GetPendingCount() == 0,
            "zero rate fallback must expire at frame 30");

        game.Frame = 200;
        game.FrameSendRate = -4;
        service.OnHotkey();
        Require(service.GetPendingCount() == 1, "negative send rate must still enqueue an intent");
        game.Frame = 230;
        service.OnGameFrame();
        Require(game.Attempts.size() == 1 && service.GetPendingCount() == 0,
            "negative rate fallback must expire at 30 frames, not an unsigned duration");
    }

    void TestExpiredFrontDoesNotBlockLiveSlot()
    {
        FakeGame game;
        BeaconClearCommandService service(game);
        game.Occupied = {{0, 0}};
        service.OnHotkey();
        game.Frame = 120;
        game.Occupied = {{7, 2}};
        service.OnHotkey();

        game.Frame = 132;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BeaconSlot>{{7, 2}} &&
            service.GetPendingCount() == 0,
            "expired front intent must be discarded before the live slot is attempted");
    }

    void TestSessionResetAndFrameRewind()
    {
        FakeGame game;
        game.Occupied = {{0, 0}, {0, 1}};
        BeaconClearCommandService service(game);
        service.OnHotkey();
        game.Session = 2;
        game.Occupied = {{7, 2}};
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts.empty() && service.GetPendingCount() == 0,
            "new session must clear old intents before sending");
        service.OnHotkey();
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BeaconSlot>{{7, 2}},
            "new session may enqueue and send its own slots");

        game.Occupied = {{1, 1}};
        service.OnHotkey();
        game.Frame = 1;
        service.OnGameFrame();
        Require(service.GetPendingCount() == 0 && game.Attempts.size() == 1,
            "frame rewind must discard pending work even with a reused session identity");

        service.OnHotkey();
        game.Ready = false;
        service.OnGameFrame();
        Require(service.GetPendingCount() == 0, "leaving the match must clear pending work");
        game.Ready = true;
        game.Session = 0;
        service.OnHotkey();
        Require(service.GetPendingCount() == 0, "missing session identity must prevent enqueue");
    }
}

void RunBeaconClearTests()
{
    TestSortingAcrossOwnersAndInvalidSlots();
    TestCapacityAndEmptyCapture();
    TestFailureRotatesBehindOtherOwners();
    TestTtlAndDuplicateHotkeyDoNotRefresh();
    TestInvalidRateFallsBackToThirtyFrames();
    TestExpiredFrontDoesNotBlockLiveSlot();
    TestSessionResetAndFrameRewind();
}

#ifdef RA_COMMANDS_BEACON_CLEAR_STANDALONE_TEST
int main()
{
    RunBeaconClearTests();
}
#endif
