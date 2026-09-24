#pragma once

#include "Commands/Selection/ISelectionGamePort.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ra_commands::selection
{
    /**
     * 编排选区命令及本局历史；由上层持有，只在游戏主线程使用。
     * 游戏对象的采集、复验、选区修改和系统按键判定均由边界外负责。
     */
    class SelectionCommandService final
    {
    public:
        explicit SelectionCommandService(ISelectionGamePort& game);

        /** 每个游戏帧调用，以记录玩家在插件命令之外的选区变化。 */
        void OnGameFrame();

        /**
         * 首按立即追加视口内同 IFV 模式；确认的第二按且未超过系统时限才追加全场。
         * 调用方须传入系统双击时限（毫秒），并依据释放后的新按下沿判定 secondPress；
         * 没有释放证据时不得将自动重复按键标记为 secondPress。
         */
        void OnIfvHotkey(bool secondPress, std::uint64_t systemDoubleClickTimeMs);
        void OnMindControlHotkey();
        void OnKindHotkey();
        void OnAmmoHotkey();
        void OnPassengersHotkey();
        void OnCycleHotkey();

        /** 撤销到上一个快照；无历史时返回 false，失效 ID 在恢复前被过滤。 */
        [[nodiscard]] bool Undo();
        /** 对局结束或服务停用时调用；下一次观察会建立新基线。 */
        void Reset();

    private:
        enum class ActiveCommand
        {
            None,
            MindControl,
            Kind,
            Ammo,
            Passengers,
            Cycle
        };

        [[nodiscard]] bool ObserveSelection();
        [[nodiscard]] bool PrepareCommand(ActiveCommand command);
        void ApplySelection(const SelectionIds& ids, bool append, bool recordHistory = true);
        void SavePreviousSelection();
        void ResetCommandState();
        void ResetIfvState();
        [[nodiscard]] SelectionIds FilterValidIds(const SelectionIds& ids) const;
        [[nodiscard]] static SelectionIds GetIds(const std::vector<SelectionMember>& members);
        [[nodiscard]] static bool SameSelection(const SelectionIds& left, const SelectionIds& right);

        ISelectionGamePort& mGame;
        SelectionHistory mHistory;
        SelectionCycle mCycle;
        SelectionIds mObservedIds;
        std::vector<SelectionMember> mCurrentMembers;
        std::vector<SelectionMember> mOriginalMembers;
        std::vector<SelectionMember> mIfvSeeds;
        std::optional<std::uint64_t> mIfvFirstPressMs;
        std::uint64_t mSessionIdentity = 0;
        std::uint64_t mLastFrame = 0;
        std::size_t mUndoDepth = 0;
        std::size_t mNextPhase = 0;
        SelectionKind mNextKind = SelectionKind::Unit;
        ActiveCommand mActiveCommand = ActiveCommand::None;
        bool mIsApplying = false;
    };
}
