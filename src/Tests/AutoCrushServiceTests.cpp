#include "Commands/AutoCrush/AutoCrushCommandService.h"

#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
    using namespace ra_commands::auto_crush;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    class FakeGame final : public IAutoCrushGamePort
    {
    public:
        bool Active = true;
        bool Eligible = true;
        std::uint32_t SessionEpoch = 1;
        std::uint32_t Frame = 100;
        std::vector<CrusherId> Selected{42};
        std::vector<Cell> Submitted;
        std::vector<CrusherId> Cancelled;

        bool IsSessionActive() const override { return Active; }
        std::uint32_t Epoch() const override { return SessionEpoch; }
        std::uint32_t CurrentFrame() const override { return Frame; }
        std::vector<CrusherId> CaptureSelectedEligibleCrushers() const override
        {
            return Eligible ? Selected : std::vector<CrusherId>{};
        }
        std::vector<CrusherId> CaptureSelectedVehicleIds() const override
        {
            return Selected;
        }
        bool IsEligibleCrusher(CrusherId id) const override
        {
            return Eligible && id == 42;
        }
        bool TryCaptureSnapshot(const std::vector<CrusherId>& ids,
            Snapshot& outSnapshot) const override
        {
            Snapshot snapshot;
            for (const auto id : ids)
            {
                CrusherSnapshot crusher;
                crusher.mId = id;
                crusher.mCurrentCell = {0, 0};
                crusher.mFacing = Facing::East;
                crusher.mTraversableCells = {{1, 0}, {2, 0}, {3, 0}};
                crusher.mTargets.push_back({9, {1, 0}, {1, 0}, 0, 1.0});
                snapshot.mCrushers.push_back(std::move(crusher));
            }
            outSnapshot = std::move(snapshot);
            return true;
        }
        bool SubmitMove(CrusherId id, Cell destination) override
        {
            if (id != 42)
            {
                return false;
            }
            Submitted.push_back(destination);
            return true;
        }
        void CancelPending(CrusherId id) override
        {
            Cancelled.push_back(id);
        }
    };

    void TestMarkManualCancelAndExplicitExit()
    {
        FakeGame game;
        AutoCrushCommandService service(game);
        service.OnAddHotkey();
        Require(service.MarkedCount() == 1, "selected eligible vehicle must be marked");
        service.OnGameFrame();
        Require(game.Submitted.size() == 1 && game.Submitted[0] == Cell{2, 0},
            "marked vehicle must plan a through-target Move");
        game.Selected.clear();
        game.Frame = 116;
        service.OnGameFrame();
        Require(service.MarkedCount() == 1 && game.Submitted.size() == 1,
            "deselecting must not unmark or spam a new Move during its cooldown");
        game.Selected = {42};

        service.OnManualOrder(42);
        Require(service.MarkedCount() == 0 && game.Cancelled == std::vector<CrusherId>{42},
            "manual move or attack must clear only the marked vehicle and pending intent");
        game.Frame = 200;
        service.OnGameFrame();
        Require(game.Submitted.size() == 1,
            "manual cancellation must stop new automatic moves without sending Stop");

        service.OnAddHotkey();
        service.OnRemoveHotkey();
        Require(service.MarkedCount() == 0 && game.Cancelled.size() == 2,
            "exit hotkey must clear mark and pending intent");
    }

    void TestEpochAndEligibilityReset()
    {
        FakeGame game;
        AutoCrushCommandService service(game);
        service.OnAddHotkey();
        game.Eligible = false;
        service.OnGameFrame();
        Require(service.MarkedCount() == 0 && game.Cancelled.size() == 1,
            "ineligible crusher must be removed without dereferencing stale objects");

        game.Eligible = true;
        service.OnAddHotkey();
        game.SessionEpoch = 2;
        service.OnGameFrame();
        Require(service.MarkedCount() == 0,
            "new match epoch must not inherit prior match markings");
        service.OnAddHotkey();
        game.Active = false;
        service.OnGameFrame();
        Require(service.MarkedCount() == 0,
            "leaving a match must clear all markings");
    }
}

void RunAutoCrushServiceTests()
{
    TestMarkManualCancelAndExplicitExit();
    TestEpochAndEligibilityReset();
}
