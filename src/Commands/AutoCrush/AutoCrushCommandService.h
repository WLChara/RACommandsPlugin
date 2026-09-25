#pragma once

#include "Commands/AutoCrush/AutoCrushPlanner.h"
#include "Commands/AutoCrush/IAutoCrushGamePort.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace ra_commands::auto_crush
{
    /**
     * 仅持有本对局中的稳定 ID 与规划状态；所有游戏对象都由适配器按帧重新解析。
     * 热键增删标记不会直接修改游戏同步状态，也不会发送 Stop。
     */
    class AutoCrushCommandService final
    {
    public:
        explicit AutoCrushCommandService(IAutoCrushGamePort& game);

        void OnAddHotkey();
        void OnRemoveHotkey();
        void OnManualOrder(CrusherId actorId);
        void OnGameFrame();
        void Reset();

        [[nodiscard]] std::size_t MarkedCount() const noexcept;

    private:
        struct CrusherState
        {
            Cell LastDestination;
            std::uint32_t LastSentFrame = 0;
            bool HasLastDestination = false;
        };

        void Remove(const std::vector<CrusherId>& ids);

        IAutoCrushGamePort& mGame;
        std::map<CrusherId, CrusherState> mMarked;
        std::uint32_t mEpoch = 0;
        std::uint32_t mLastPlanFrame = 0;
        bool mHasPlanned = false;
    };
}
