#include "Commands/AutoSuperWeapon/AutoSuperWeaponCommandService.h"
#include "Commands/AutoSuperWeapon/AutoSuperWeaponPlanner.h"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace
{
    using namespace ra_commands::auto_super_weapon;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    void TestFootprintsAndTargetScore()
    {
        Require(Covers(Kind::IronCurtain, {10, 10}, {11, 11}) &&
            !Covers(Kind::IronCurtain, {10, 10}, {12, 10}),
            "iron curtain must cover exactly the surrounding 3-by-3 cells");
        Require(Covers(Kind::RageInductor, {10, 10}, {14, 11}) &&
            !Covers(Kind::RageInductor, {10, 10}, {14, 12}),
            "rage radius 4.2 must include squared distance 17 but exclude 20");

        Snapshot snapshot;
        snapshot.EnemyCount = 3;
        snapshot.Selected = {
            {{10, 10}, 1000, {0, 1}},
            {{11, 10}, 1000, {1}},
            {{20, 20}, 5000, {2}}
        };
        snapshot.CandidateCenters = {{10, 10}, {20, 20}};
        const auto iron = Plan(Kind::IronCurtain, snapshot);
        Require(iron && iron->Center == Cell{10, 10} &&
            iron->CoveredUnits == 2 && iron->CoveredValue == 2000 &&
            iron->AttackableEnemies == 2,
            "density and distinct attackable enemies should beat one valuable unit");

        snapshot.Selected = {{{10, 10}, 1000, {0}},
            {{14, 11}, 1000, {1}}};
        snapshot.CandidateCenters = {{10, 10}, {14, 12}};
        const auto rage = Plan(Kind::RageInductor, snapshot);
        Require(rage && rage->Center == Cell{10, 10} &&
            rage->CoveredUnits == 2 && rage->AttackableEnemies == 2,
            "rage planner must score units within the circular footprint");
    }

    class FakeGame final : public IAutoSuperWeaponGamePort
    {
    public:
        bool MatchReady = true;
        std::uintptr_t Session = 7;
        std::uint32_t Frame = 100;
        std::array<bool, 2> Ready{true, true};
        bool Accept = true;
        Snapshot Captured{{{{5, 5}, 1000, {}}}, {{5, 5}}, 0};
        mutable std::vector<Kind> Sent;

        bool IsMatchReady() const override { return MatchReady; }
        std::uintptr_t SessionIdentity() const override { return Session; }
        std::uint32_t CurrentFrame() const override { return Frame; }
        bool IsWeaponReady(Kind kind) const override
        {
            return Ready[kind == Kind::IronCurtain ? 0 : 1];
        }
        bool CaptureSnapshot(Kind, Snapshot& out) const override
        {
            out = Captured;
            return true;
        }
        bool TryFireAt(Kind kind, Cell) const override
        {
            if (!Accept)
            {
                return false;
            }
            Sent.push_back(kind);
            return true;
        }
    };

    void TestIndependentTogglesAndConfirmation()
    {
        FakeGame game;
        AutoSuperWeaponCommandService service(game);
        Require(service.OnHotkey(Kind::IronCurtain) &&
            service.IsEnabled(Kind::IronCurtain) &&
            !service.IsEnabled(Kind::RageInductor) &&
            game.Sent == std::vector<Kind>{Kind::IronCurtain},
            "enabling iron curtain should fire only its ready weapon");
        service.OnGameFrame();
        Require(game.Sent.size() == 1,
            "a queued weapon must not be sent again while awaiting confirmation");
        Require(service.OnHotkey(Kind::RageInductor) && game.Sent.size() == 2 &&
            game.Sent.back() == Kind::RageInductor,
            "rage toggle must remain independent of iron curtain");
        game.Ready[0] = false;
        service.OnGameFrame();
        game.Frame += 30;
        game.Ready[0] = true;
        service.OnGameFrame();
        Require(game.Sent.size() == 3 && game.Sent.back() == Kind::IronCurtain,
            "a fresh readiness period should allow another release");
        game.Session = 8;
        service.OnGameFrame();
        Require(!service.IsEnabled(Kind::IronCurtain) &&
            !service.IsEnabled(Kind::RageInductor),
            "new match must disable both continuous commands");
    }

    void TestBackpressureAndRetry()
    {
        FakeGame game;
        game.Accept = false;
        AutoSuperWeaponCommandService service(game);
        Require(service.OnHotkey(Kind::IronCurtain) && game.Sent.empty(),
            "queue rejection must keep automatic firing enabled");
        game.Frame += 29;
        game.Accept = true;
        service.OnGameFrame();
        Require(game.Sent.empty(), "a rejected attempt must be rate limited");
        ++game.Frame;
        service.OnGameFrame();
        Require(game.Sent == std::vector<Kind>{Kind::IronCurtain},
            "a later frame should retry when the native queue has capacity");
        Require(!service.OnHotkey(Kind::IronCurtain) &&
            !service.IsEnabled(Kind::IronCurtain),
            "second hotkey press must disable automatic firing");
    }
}

void RunAutoSuperWeaponPureTests()
{
    TestFootprintsAndTargetScore();
    TestIndependentTogglesAndConfirmation();
    TestBackpressureAndRetry();
}
