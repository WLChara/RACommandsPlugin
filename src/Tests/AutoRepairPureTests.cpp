#include "Commands/AutoRepairCommand/AutoRepairCommandService.h"

#include <cstdint>
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
        mutable std::uint32_t FreeSlots = 13;
        Snapshot Buildings = {1, {{Id(10), 1, true, false, true}}};
        mutable std::vector<BuildingId> Attempts;

        bool IsMatchReady() const override { return Ready; }
        std::uintptr_t GetSessionIdentity() const override { return Session; }
        std::uint32_t GetCurrentFrame() const override { return Frame; }
        bool CaptureSnapshot(Snapshot& outSnapshot) const override
        {
            outSnapshot = Buildings;
            return true;
        }
        std::uint32_t GetNativeFreeSlots() const override { return FreeSlots; }
        bool TryRepair(BuildingId id) const override
        {
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
        AutoRepairCommandService service(game);
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
        AutoRepairCommandService service(game);
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
        AutoRepairCommandService service(game);
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
        AutoRepairCommandService service(game);
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
        AutoRepairCommandService service(game);
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
}

void RunAutoRepairTests()
{
    TestToggleAndEligibility();
    TestRetryAndBackpressure();
    TestCapacityAcrossBuildings();
    TestReusedUniqueIdHasDistinctThrottleKey();
    TestSessionReset();
}

#ifdef RA_COMMANDS_AUTO_REPAIR_STANDALONE_TEST
int main()
{
    RunAutoRepairTests();
}
#endif
