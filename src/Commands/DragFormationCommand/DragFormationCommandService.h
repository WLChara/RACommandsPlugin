#pragma once

#include "Commands/DragFormationCommand/IDragFormationGamePort.h"

namespace ra_commands::drag_formation
{
    /** 独占一次双键手势；输入Hook只提供事件，不持有此状态。 */
    class DragFormationCommandService final
    {
    public:
        explicit DragFormationCommandService(IDragFormationGamePort& game);
        /** 首次热键锁定参与者并开启；再次热键取消。 */
        void OnHotkey();
        void OnPointerEvent(const PointerEvent& event);
        /** 有效拖拽后即使鼠标不再移动，也在500ms后显示预览；会话失效时取消。 */
        void OnGameFrame(std::uint64_t timeMs);
        void Reset();
        [[nodiscard]] bool IsEnabled() const;
        [[nodiscard]] const Preview& GetPreview() const;
        [[nodiscard]] Outcome GetOutcome() const;

    private:
        void UpdatePreview(std::uint64_t timeMs);
        void Finish(std::uint64_t timeMs);
        IDragFormationGamePort& mGame;
        std::vector<auto_formation::ActorProfile> mActors;
        Preview mPreview;
        WorldPoint mAnchor;
        WorldPoint mEnd;
        std::uint32_t mEpoch = 0;
        std::uint64_t mChordStartMs = 0;
        unsigned mButtons = 0;
        bool mEnabled = false;
        bool mHasChord = false;
        bool mPointerValid = false;
        Outcome mOutcome = Outcome::None;
    };
}
