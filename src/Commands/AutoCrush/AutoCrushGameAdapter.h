#pragma once

#include "Commands/AutoCrush/IAutoCrushGamePort.h"
#include "ClickedMission/ClickedMissionQueue.h"

#include <unordered_set>

class CellClass;
class TechnoClass;

namespace ra_commands::commands
{
    class ClickedMissionDispatcher;
}

namespace ra_commands::game
{
    /** 仅在游戏线程使用；调度器由 Bootstrap 持有，且须比本适配器存活更久。 */
    class AutoCrushGameAdapter final : public auto_crush::IAutoCrushGamePort
    {
    public:
        explicit AutoCrushGameAdapter(commands::ClickedMissionDispatcher& dispatcher);

        [[nodiscard]] bool IsSessionActive() const override;
        [[nodiscard]] std::uint32_t Epoch() const override;
        [[nodiscard]] std::uint32_t CurrentFrame() const override;
        [[nodiscard]] std::vector<auto_crush::CrusherId>
            CaptureSelectedEligibleCrushers() const override;
        [[nodiscard]] std::vector<auto_crush::CrusherId>
            CaptureSelectedVehicleIds() const override;
        [[nodiscard]] bool IsEligibleCrusher(auto_crush::CrusherId id) const override;
        [[nodiscard]] bool TryCaptureSnapshot(
            const std::vector<auto_crush::CrusherId>& ids,
            auto_crush::Snapshot& outSnapshot) const override;
        [[nodiscard]] bool SubmitMove(
            auto_crush::CrusherId id, auto_crush::Cell destination) override;
        void CancelPending(auto_crush::CrusherId id) override;

    private:
        void RefreshEligibleIds() const;

        commands::ClickedMissionDispatcher& mDispatcher;
        // 只缓存本帧的稳定 ID，绝不跨帧保存可解引用的游戏对象指针。
        mutable std::unordered_set<auto_crush::CrusherId> mEligibleIds;
        mutable std::uint32_t mEligibleFrame = 0;
        mutable std::uint32_t mEligibleEpoch = 0;
        mutable bool mHasEligibleIds = false;
    };

    /** 供共用调度器在校验和实际发送前调用；返回值仅在本次调用期间有效。 */
    [[nodiscard]] CellClass* ResolveAutoCrushMoveCell(
        const commands::ClickedMissionIntent& intent,
        TechnoClass*& outActor);
}
