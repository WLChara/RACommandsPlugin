#pragma once

#include "Commands/AutoFormationCommand/IAutoFormationGamePort.h"
#include "ClickedMission/ClickedMissionQueue.h"

#include <map>
#include <unordered_set>

namespace ra_commands::commands { class ClickedMissionDispatcher; }

namespace ra_commands::game
{
    /** 游戏线程独占。Bootstrap 持有进程期实例，调度器须比本适配器存活更久。 */
    class AutoFormationGameAdapter final : public auto_formation::IAutoFormationGamePort
    {
    public:
        explicit AutoFormationGameAdapter(commands::ClickedMissionDispatcher& dispatcher);
        [[nodiscard]] bool IsSessionActive() const override;
        [[nodiscard]] std::uint32_t Epoch() const override;
        [[nodiscard]] std::uint32_t CurrentFrame() const override;
        [[nodiscard]] std::vector<auto_formation::ActorId> CaptureSelectedActorIds() const override;
        [[nodiscard]] bool TryCaptureSnapshot(const std::vector<auto_formation::ActorId>& ids,
            std::optional<auto_formation::Center> fixedCenter,
            auto_formation::Snapshot& outSnapshot) const override;
        void BeginPlan(const std::vector<auto_formation::ActorId>& assignedActors) override;
        [[nodiscard]] bool TryGetActorCell(auto_formation::ActorId actor,
            auto_formation::Cell& outCell) const override;
        [[nodiscard]] bool SubmitMove(auto_formation::ActorId actor,
            auto_formation::Cell destination) override;
        [[nodiscard]] auto_formation::MoveState ObserveMove(auto_formation::ActorId actor,
            auto_formation::Cell destination) const override;
        void CancelPending(auto_formation::ActorId actor) override;
        void Reset() override;

        [[nodiscard]] bool ValidateIntent(const commands::ClickedMissionIntent& intent) const;
        void AttemptIntent(const commands::ClickedMissionIntent& intent);

    private:
        struct PendingMove
        {
            commands::ClickedMissionIdentity mIdentity;
            auto_formation::Cell mDestination;
            std::uint32_t mIssuedFrame = 0;
            bool mHasIssued = false;
            bool mHasObservedDestination = false;
        };

        commands::ClickedMissionDispatcher& mDispatcher;
        mutable std::map<auto_formation::ActorId, PendingMove> mMoves;
        std::unordered_set<auto_formation::ActorId> mParticipants;
        mutable std::unordered_set<auto_formation::ActorId> mLoggedRejectedActors;
        mutable std::uint32_t mLastSelectionTraceFrame = 0;
        mutable std::uint32_t mSelectionTraceEpoch = 0;
        mutable bool mHasSelectionTrace = false;
        std::uint32_t mPlanEpoch = 0;
    };
}
