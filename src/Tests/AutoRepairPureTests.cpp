#include "Commands/AutoRepairCommand/AutoRepairCommandService.h"

#include <algorithm>
#include <cstdint>
#include <atomic>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace ra_commands::auto_repair;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    constexpr BuildingId Id(std::uint32_t uniqueId, std::uintptr_t address = 0)
    {
        return {address != 0 ? address : uniqueId * 0x100u, uniqueId};
    }

    class FakeGame final : public IAutoRepairGamePort
    {
    public:
        bool Ready = true;
        std::uintptr_t Session = 1;
        std::uint32_t Frame = 100;
        std::uint64_t NowMs = 1'000;
        mutable std::uint32_t FreeSlots = 13;
        Snapshot Buildings = {1, {{Id(10), 1, true, false, true}}};
        mutable std::vector<BuildingId> Attempts;

        bool IsMatchReady() const override { return Ready; }
        std::uintptr_t GetSessionIdentity() const override { return Session; }
        std::uint32_t GetCurrentFrame() const override { return Frame; }
        std::uint64_t GetCurrentTimeMs() const override { return NowMs; }
        bool CaptureSnapshot(Snapshot& outSnapshot, bool) const override
        {
            outSnapshot = Buildings;
            return true;
        }
        std::uint32_t GetNativeFreeSlots() const override { return FreeSlots; }
        bool TryRepair(BuildingId id, bool requireViewport) const override
        {
            if (requireViewport)
            {
                const auto it = std::find_if(Buildings.Buildings.begin(), Buildings.Buildings.end(),
                    [id](const BuildingSnapshot& building) { return building.Id == id; });
                if (it == Buildings.Buildings.end() || !it->IsInViewport)
                {
                    return false;
                }
            }
            Attempts.push_back(id);
            --FreeSlots;
            return true;
        }
    };

    void TestToggleAndEligibility()
    {
        FakeGame game;
        game.Buildings.Buildings = {
            {Id(10), 1, true, false, true},
            {Id(11), 2, true, false, true},
            {Id(12), 1, false, false, true},
            {Id(13), 1, true, true, true},
            {Id(14), 1, true, false, false}
        };
        std::atomic<bool> isSafeModeEnabled{false};
        AutoRepairCommandService service(game, isSafeModeEnabled);
        service.OnGameFrame();
        Require(game.Attempts.empty(), "repair must be disabled initially");

        service.OnHotkey();
        Require(service.IsEnabled(), "first hotkey must enable repair");
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BuildingId>{Id(10)},
            "only local damaged non-repairing repairable buildings qualify");

        service.OnHotkey();
        Require(!service.IsEnabled(), "second hotkey must disable repair");
        game.Frame += 40;
        game.FreeSlots = 13;
        service.OnGameFrame();
        Require(game.Attempts.size() == 1, "disabled repair must stop all retries");
    }

    void TestRetryAndBackpressure()
    {
        FakeGame game;
        std::atomic<bool> isSafeModeEnabled{false};
        AutoRepairCommandService service(game, isSafeModeEnabled);
        service.OnHotkey();

        game.FreeSlots = 12;
        service.OnGameFrame();
        Require(game.Attempts.empty(), "12 free slots must prevent repair");

        game.FreeSlots = 13;
        service.OnGameFrame();
        Require(game.Attempts.size() == 1, "13 free slots must allow one attempt");
        game.FreeSlots = 13;
        game.Frame += 29;
        service.OnGameFrame();
        Require(game.Attempts.size() == 1, "same building must wait 30 frames");

        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts.size() == 2, "repair may retry at frame 30");
    }

    void TestCapacityAcrossBuildings()
    {
        FakeGame game;
        game.Buildings.Buildings.push_back({Id(11), 1, true, false, true});
        std::atomic<bool> isSafeModeEnabled{false};
        AutoRepairCommandService service(game, isSafeModeEnabled);
        service.OnHotkey();
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BuildingId>{Id(10)},
            "one attempt must consume capacity before the next building");

        game.FreeSlots = 13;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BuildingId>({Id(10), Id(11)}),
            "blocked building must remain eligible on the next frame");
    }

    void TestReusedUniqueIdHasDistinctThrottleKey()
    {
        FakeGame game;
        std::atomic<bool> isSafeModeEnabled{false};
        AutoRepairCommandService service(game, isSafeModeEnabled);
        service.OnHotkey();
        service.OnGameFrame();

        const auto replacement = Id(10, 0x9000);
        game.Buildings.Buildings[0].Id = replacement;
        game.FreeSlots = 13;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BuildingId>({Id(10), replacement}),
            "a reused UniqueID at a new address must have an independent throttle");
    }

    void TestSessionReset()
    {
        FakeGame game;
        std::atomic<bool> isSafeModeEnabled{false};
        AutoRepairCommandService service(game, isSafeModeEnabled);
        service.OnHotkey();
        service.OnGameFrame();

        game.Session = 2;
        game.FreeSlots = 13;
        ++game.Frame;
        service.OnGameFrame();
        Require(!service.IsEnabled() && game.Attempts.size() == 1,
            "new session must disable the old toggle");
        service.OnHotkey();
        service.OnGameFrame();
        Require(game.Attempts.size() == 2,
            "new session must not inherit the old building throttle");

        game.Frame = 1;
        game.FreeSlots = 13;
        service.OnGameFrame();
        Require(!service.IsEnabled() && game.Attempts.size() == 2,
            "frame rewind must reset even when session pointer is reused");

        service.OnHotkey();
        service.OnGameFrame();
        Require(game.Attempts.size() == 3,
            "frame rewind must clear old timestamps before retry subtraction");
        game.Ready = false;
        service.OnGameFrame();
        Require(!service.IsEnabled(), "leaving the match must disable repair");
    }

    void TestSafeModeDelayViewportAndOnePerFrame()
    {
        FakeGame game;
        game.Buildings.Buildings = {
            {Id(10), 1, true, false, true, 70, true},
            {Id(11), 1, true, false, true, 60, true},
            {Id(12), 1, true, false, true, 50, false}
        };
        std::atomic<bool> isSafeModeEnabled{true};
        AutoRepairCommandService service(game, isSafeModeEnabled);
        service.OnHotkey();
        service.OnGameFrame();
        Require(game.Attempts.empty(), "first visible repair must start with a delay");

        game.NowMs = 1'999;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts.empty(), "safe repair must wait at least 1000 ms");

        game.NowMs = 3'000;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BuildingId>{Id(10)},
            "first visible repair must become ready within 2000 ms");
        game.Buildings.Buildings[0].IsBeingRepaired = true;

        game.FreeSlots = 13;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts.size() == 1,
            "next scan must not bypass the shared safe repair interval");

        game.NowMs = 3'099;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts.size() == 1,
            "subsequent buildings must wait at least 100 ms");

        game.NowMs = 4'000;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BuildingId>({Id(10), Id(11)}),
            "subsequent buildings must become ready within 1000 ms");
        game.Buildings.Buildings[1].IsBeingRepaired = true;

        game.FreeSlots = 13;
        game.NowMs = 5'000;
        game.Frame += 31;
        service.OnGameFrame();
        Require(game.Attempts.size() == 2, "off-screen building must not be repaired");

        game.Buildings.Buildings[2].IsInViewport = true;
        ++game.Frame;
        service.OnGameFrame();
        game.NowMs = 5'999;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts.size() == 2,
            "a new visible batch must wait the first-building delay again");
        game.NowMs = 7'000;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BuildingId>({Id(10), Id(11), Id(12)}),
            "new visible batch must become ready within 2000 ms");
    }

    void TestSafeModeDamageDoesNotRestartSchedule()
    {
        FakeGame game;
        game.Buildings.Buildings = {{Id(10), 1, true, false, true, 70, true}};
        std::atomic<bool> isSafeModeEnabled{true};
        AutoRepairCommandService service(game, isSafeModeEnabled);
        service.OnHotkey();
        service.OnGameFrame();

        game.NowMs = 1'800;
        game.Buildings.Buildings[0].Health = 60;
        ++game.Frame;
        service.OnGameFrame();
        game.NowMs = 3'000;
        game.Buildings.Buildings[0].Health = 50;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BuildingId>{Id(10)},
            "continued damage must not postpone the first-building schedule");

        isSafeModeEnabled.store(false);
        service.OnSafeModeChanged();
        game.Buildings.Buildings[0].IsInViewport = false;
        game.FreeSlots = 13;
        game.Frame += 31;
        service.OnGameFrame();
        Require(game.Attempts.size() == 2,
            "normal mode must retain repair behavior outside the viewport");
    }

    void TestSafeModeToggleAndBackpressure()
    {
        FakeGame game;
        game.Buildings.Buildings = {
            {Id(10), 1, true, false, true, 70, true},
            {Id(11), 1, true, false, true, 60, true}
        };
        std::atomic<bool> isSafeModeEnabled{true};
        AutoRepairCommandService service(game, isSafeModeEnabled);
        service.OnGameFrame();
        game.NowMs = 10'000;
        service.OnHotkey();
        service.OnGameFrame();
        Require(game.Attempts.empty(), "disabled repair must not prewarm the timer");

        game.NowMs = 12'000;
        game.FreeSlots = 12;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts.empty(), "capacity must still block an elapsed delay");
        game.NowMs = 12'500;
        game.FreeSlots = 13;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BuildingId>{Id(10)},
            "capacity recovery must not start another first-building delay");
        game.Buildings.Buildings[0].IsBeingRepaired = true;

        service.OnHotkey();
        service.OnHotkey();
        game.NowMs = 13'000;
        game.FreeSlots = 13;
        ++game.Frame;
        service.OnGameFrame();
        game.NowMs = 13'999;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts.size() == 1, "re-enabling repair must reset the first delay");
        game.NowMs = 15'000;
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Attempts == std::vector<BuildingId>({Id(10), Id(11)}),
            "re-enabled repair must become ready within 2000 ms");
    }
}

void RunAutoRepairTests()
{
    TestToggleAndEligibility();
    TestRetryAndBackpressure();
    TestCapacityAcrossBuildings();
    TestReusedUniqueIdHasDistinctThrottleKey();
    TestSessionReset();
    TestSafeModeDelayViewportAndOnePerFrame();
    TestSafeModeDamageDoesNotRestartSchedule();
    TestSafeModeToggleAndBackpressure();
}

#ifdef RA_COMMANDS_AUTO_REPAIR_STANDALONE_TEST
int main()
{
    RunAutoRepairTests();
}
#endif
