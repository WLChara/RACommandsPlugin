#pragma once

#include "Commands/DragFormationCommand/IDragFormationGamePort.h"

#include <cstdint>

namespace ra_commands::auto_formation { class AutoFormationCommandService; }
namespace ra_commands::game
{
    class AutoFormationGameAdapter;
    /** SDK/屏幕投影在此边界内；与现有执行器组合，不复制跨帧移动车辆状态。 */
    class DragFormationGameAdapter final : public drag_formation::IDragFormationGamePort
    {
    public:
        DragFormationGameAdapter(AutoFormationGameAdapter& game,
            auto_formation::AutoFormationCommandService& executor);
        /** Bootstrap在游戏线程安装输入Hook时读取主窗口句柄；0表示尚未建立。 */
        [[nodiscard]] std::uintptr_t OwnerWindowHandle() const noexcept;
        [[nodiscard]] bool IsSessionActive() const override;
        [[nodiscard]] std::uint32_t Epoch() const override;
        [[nodiscard]] std::vector<auto_formation::ActorProfile> CaptureSelectedActorProfiles() const override;
        [[nodiscard]] bool TryCapturePlacementSnapshot(
            const std::vector<auto_formation::ActorId>& actors,
            const std::vector<auto_formation::Cell>& cells,
            const drag_formation::Layout& layout,
            auto_formation::Snapshot& outSnapshot) const override;
        [[nodiscard]] bool Commit(const auto_formation::PlanResult& plan) override;
        /** client坐标必须位于战场视窗；返回连续格坐标，格中心为整数。 */
        [[nodiscard]] std::optional<drag_formation::WorldPoint> CaptureWorldPointFromClient(
            std::int32_t clientX, std::int32_t clientY) const;
        /** 每帧读取物理鼠标位置并经同一视窗校验转换；镜头滚动时世界坐标随之更新。 */
        [[nodiscard]] std::optional<drag_formation::WorldPoint> CaptureCursorWorldPoint() const;
        /** 仅按键按下时调用：记录 Win client、战场视窗和反投影误差。 */
        void TracePointerPoint(std::int32_t clientX, std::int32_t clientY,
            const std::optional<drag_formation::WorldPoint>& point) const;
        void RenderPreview(const drag_formation::Preview& preview) const;
        /** 游戏线程显示本地结果并写一条诊断；None 不产生输出。 */
        void ShowFeedback(drag_formation::Outcome outcome) const;

    private:
        mutable std::uint32_t mProjectionFallbackEpoch = 0;
        mutable bool mLoggedProjectionFallback = false;
        AutoFormationGameAdapter& mGame;
        auto_formation::AutoFormationCommandService& mExecutor;
    };
}
