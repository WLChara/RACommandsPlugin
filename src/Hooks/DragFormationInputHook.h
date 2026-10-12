#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>

namespace ra_commands::game
{
    enum class DragPointerEventKind
    {
        Down,
        Up,
        Move,
        Cancel
    };

    enum class DragPointerButton
    {
        None,
        Left,
        Right
    };

    struct DragPointerEvent
    {
        DragPointerEventKind mKind;
        DragPointerButton mButton;
        int mClientX;
        int mClientY;
        std::uint64_t mTimeMs;
    };

    using DragPointerCallback = void(*)(const DragPointerEvent&);
    using DragModeQuery = bool(*)();

    /**
     * 在游戏主窗口所属线程安装一次。回调与模式查询也只在该线程执行；
     * ownerWnd 必须是已经创建的游戏主窗口。失败时不会接管输入。
     * DLL 固定到进程退出，禁止安装后卸载 DLL。
     */
    [[nodiscard]] bool InstallDragFormationInputHook(HWND ownerWnd,
        DragPointerCallback callback, DragModeQuery isModeEnabled,
        std::string& outError);

    /**
     * 可从任意线程请求停用；模式查询立即返回关闭，窗口线程异步清理回调与鼠标捕获。
     * 子类过程仍安装至 WM_NCDESTROY；已吞掉的按键仍吞掉对应抬起消息。
     */
    void DisableDragFormationInputHook() noexcept;

    /**
     * 热键取消或其他主动退出模式时由游戏线程调用。保留已吞按下消息的配对抬起屏蔽。
     */
    void CancelDragFormationInputCapture() noexcept;
}
