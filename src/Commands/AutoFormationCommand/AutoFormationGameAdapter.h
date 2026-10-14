#pragma once

#include "Commands/AutoFormationCommand/IAutoFormationGamePort.h"
#include "Commands/AutoFormationCommand/FormationCellPacking.h"
#include "ClickedMission/ClickedMissionQueue.h"

#include <map>
#include <cstddef>
#include <unordered_map>
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
        [[nodiscard]] std::vector<auto_formation::ActorProfile> CaptureSelectedActorProfiles() const;
        [[nodiscard]] bool TryCaptureSnapshot(const std::vector<auto_formation::ActorId>& ids,
            std::optional<auto_formation::Center> fixedCenter,
            auto_formation::Snapshot& outSnapshot) const override;
        /** 有界显式候选查询；任一身份无效整次失败，输出保持不变。
         * 大队用 planes 和 infantryCapacity 确定实际足迹并查询局部带；不保证覆盖复杂地形的所有可占格。
         * 目标层按参与者所在层查询，真实占用始终逐演员核对。
         */
        [[nodiscard]] bool TryCaptureSnapshotForCells(
            const std::vector<auto_formation::ActorId>& ids,
            const std::vector<auto_formation::Cell>& cells,
            auto_formation::Snapshot& outSnapshot,
            const std::vector<auto_formation::PlaneLayout>& planes = {},
            std::uint8_t infantryCapacity = 3) const;
        void BeginPlan(const std::vector<auto_formation::Assignment>& assignments) override;
        [[nodiscard]] bool TryGetActorState(auto_formation::ActorId actor,
            auto_formation::ActorObservation& outState) const override;
        [[nodiscard]] bool SubmitMove(const auto_formation::Assignment& assignment) override;
        [[nodiscard]] auto_formation::MoveState ObserveMove(
            const auto_formation::Assignment& assignment) const override;
        void CancelPending(auto_formation::ActorId actor) override;
        void Reset() override;

        [[nodiscard]] bool ValidateIntent(const commands::ClickedMissionIntent& intent) const;
        void AttemptIntent(const commands::ClickedMissionIntent& intent);

    private:
        void PrepareCandidateQueryBudget() const;
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
        std::map<auto_formation::ActorId, auto_formation::Assignment> mPlanAssignments;
        mutable std::unordered_set<auto_formation::ActorId> mLoggedRejectedActors;
        mutable std::uint32_t mLastSelectionTraceFrame = 0;
        mutable std::uint32_t mSelectionTraceEpoch = 0;
        mutable bool mHasSelectionTrace = false;
        // 大队 3/2/1 重试共用一帧查询上限，防止三份各 256k 的查询阻塞主帧。
        mutable std::uint32_t mCandidateQueryEpoch = 0;
        mutable std::uint32_t mCandidateQueryFrame = 0;
        mutable std::size_t mCandidateQueries = 0;
        mutable std::unordered_map<auto_formation::ActorId, std::size_t> mCandidateQueriesByActor;
        std::uint32_t mPlanEpoch = 0;
    };
}
