#include "Hooks/DragFormationInputHook.h"

#include <CommCtrl.h>
#include <Windowsx.h>

#include <atomic>
#include <cstdint>

#pragma comment(lib, "comctl32.lib")

namespace ra_commands::game
{
    namespace
    {
        constexpr UINT_PTR SUBCLASS_ID = 0x52414446u;
        constexpr UINT DISABLE_MESSAGE = WM_APP + 0x2D46u;
        constexpr unsigned int LEFT_BUTTON_MASK = 1u;
        constexpr unsigned int RIGHT_BUTTON_MASK = 2u;

        struct InputHookState
        {
            HWND mOwnerWnd = nullptr;
            DragPointerCallback mCallback = nullptr;
            DragModeQuery mModeQuery = nullptr;
            DWORD mOwnerThreadId = 0;
            unsigned int mSwallowedButtons = 0;
            int mLastClientX = 0;
            int mLastClientY = 0;
            bool mOwnsCapture = false;
            bool mIsReleasingCapture = false;
            bool mIsCancelling = false;
            bool mSwallowEscapeUp = false;
            bool mIsCompletingDisable = false;
            bool mIsInstalled = false;
        };

        InputHookState g_InputHook;
        std::atomic<HWND> g_PublishedOwnerWnd{nullptr};
        std::atomic_bool g_DisableRequested{false};

        bool IsOnOwnerThread() noexcept
        {
            return g_InputHook.mOwnerThreadId != 0 &&
                GetCurrentThreadId() == g_InputHook.mOwnerThreadId;
        }

        bool IsModeEnabled() noexcept
        {
            if (g_DisableRequested.load(std::memory_order_acquire) ||
                !g_InputHook.mModeQuery)
            {
                return false;
            }
            try
            {
                return g_InputHook.mModeQuery();
            }
            catch (...)
            {
                OutputDebugStringA("[RACommandsPlugin] drag mode query failed\n");
                return false;
            }
        }

        void Emit(DragPointerEventKind kind, DragPointerButton button,
            int clientX, int clientY) noexcept
        {
            if (g_DisableRequested.load(std::memory_order_acquire))
            {
                return;
            }
            const auto callback = g_InputHook.mCallback;
            if (!callback)
            {
                return;
            }
            try
            {
                callback({kind, button, clientX, clientY, GetTickCount64()});
            }
            catch (...)
            {
                OutputDebugStringA("[RACommandsPlugin] drag input callback failed\n");
            }
        }

        void ReleaseOwnedCapture() noexcept
        {
            if (!g_InputHook.mOwnsCapture)
            {
                return;
            }
            g_InputHook.mOwnsCapture = false;
            if (GetCapture() == g_InputHook.mOwnerWnd)
            {
                // ReleaseCapture 同步发送 WM_CAPTURECHANGED，避免它把主动释放当作外部取消。
                g_InputHook.mIsReleasingCapture = true;
                ReleaseCapture();
                g_InputHook.mIsReleasingCapture = false;
            }
        }

        void CancelGesture() noexcept
        {
            if (g_InputHook.mIsCancelling)
            {
                return;
            }
            g_InputHook.mIsCancelling = true;
            if (IsModeEnabled())
            {
                Emit(DragPointerEventKind::Cancel, DragPointerButton::None,
                    g_InputHook.mLastClientX, g_InputHook.mLastClientY);
            }
            ReleaseOwnedCapture();
            g_InputHook.mIsCancelling = false;
        }

        void CompleteDisableOnOwnerThread() noexcept
        {
            if (g_InputHook.mIsCompletingDisable)
            {
                return;
            }
            g_InputHook.mIsCompletingDisable = true;
            CancelGesture();
            g_InputHook.mCallback = nullptr;
            g_InputHook.mModeQuery = nullptr;
            g_InputHook.mIsCompletingDisable = false;
        }

        unsigned int ButtonMask(DragPointerButton button) noexcept
        {
            return button == DragPointerButton::Left ? LEFT_BUTTON_MASK : RIGHT_BUTTON_MASK;
        }

        bool HasUnmanagedOtherButton(DragPointerButton button, WPARAM flags) noexcept
        {
            if (button == DragPointerButton::Left)
            {
                return (flags & MK_RBUTTON) != 0 &&
                    (g_InputHook.mSwallowedButtons & RIGHT_BUTTON_MASK) == 0;
            }
            return (flags & MK_LBUTTON) != 0 &&
                (g_InputHook.mSwallowedButtons & LEFT_BUTTON_MASK) == 0;
        }

        bool HandleButtonDown(HWND ownerWnd, DragPointerButton button,
            WPARAM flags, LPARAM coordinates) noexcept
        {
            const auto mask = ButtonMask(button);
            const auto otherMask = mask == LEFT_BUTTON_MASK ?
                RIGHT_BUTTON_MASK : LEFT_BUTTON_MASK;
            const bool isOtherHeld = (flags & (mask == LEFT_BUTTON_MASK ?
                MK_RBUTTON : MK_LBUTTON)) != 0;
            // 鼠标按钮不会重复发送 Down；若曾丢失对应 Up，此次已是新按键序列。
            g_InputHook.mSwallowedButtons &= ~mask;
            if (!isOtherHeld)
            {
                g_InputHook.mSwallowedButtons &= ~otherMask;
            }
            if (!IsModeEnabled())
            {
                // 模式刚完成、另一键仍被插件接管时，新按键也必须成对吞掉。
                if (g_InputHook.mSwallowedButtons != 0)
                {
                    g_InputHook.mSwallowedButtons |= mask;
                    return true;
                }
                return false;
            }
            // 模式开启前已有原生按键或鼠标捕获时，不能吞掉后续抬起而留下原生状态。
            if (HasUnmanagedOtherButton(button, flags) ||
                (g_InputHook.mSwallowedButtons == 0 && GetCapture() == ownerWnd))
            {
                CancelGesture();
                return false;
            }
            if (g_InputHook.mSwallowedButtons == 0)
            {
                SetCapture(ownerWnd);
                if (GetCapture() != ownerWnd)
                {
                    CancelGesture();
                    return false;
                }
                g_InputHook.mOwnsCapture = true;
            }
            g_InputHook.mSwallowedButtons |= mask;
            g_InputHook.mLastClientX = GET_X_LPARAM(coordinates);
            g_InputHook.mLastClientY = GET_Y_LPARAM(coordinates);
            Emit(DragPointerEventKind::Down, button,
                g_InputHook.mLastClientX, g_InputHook.mLastClientY);
            return true;
        }

        bool HandleButtonUp(DragPointerButton button, LPARAM coordinates) noexcept
        {
            const auto mask = ButtonMask(button);
            if ((g_InputHook.mSwallowedButtons & mask) == 0)
            {
                return false;
            }
            g_InputHook.mLastClientX = GET_X_LPARAM(coordinates);
            g_InputHook.mLastClientY = GET_Y_LPARAM(coordinates);
            if (IsModeEnabled())
            {
                Emit(DragPointerEventKind::Up, button,
                    g_InputHook.mLastClientX, g_InputHook.mLastClientY);
            }
            g_InputHook.mSwallowedButtons &= ~mask;
            if (g_InputHook.mSwallowedButtons == 0)
            {
                ReleaseOwnedCapture();
            }
            return true;
        }

        LRESULT CALLBACK DragSubclassProc(HWND ownerWnd, UINT message,
            WPARAM wParam, LPARAM lParam, UINT_PTR subclassId, DWORD_PTR)
        {
            if (subclassId != SUBCLASS_ID || ownerWnd != g_InputHook.mOwnerWnd)
            {
                return DefSubclassProc(ownerWnd, message, wParam, lParam);
            }
            if (g_DisableRequested.load(std::memory_order_acquire))
            {
                CompleteDisableOnOwnerThread();
            }
            if (message == DISABLE_MESSAGE)
            {
                return 0;
            }

            switch (message)
            {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONDBLCLK:
                if (HandleButtonDown(ownerWnd, DragPointerButton::Left, wParam, lParam))
                {
                    return 0;
                }
                break;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONDBLCLK:
                if (HandleButtonDown(ownerWnd, DragPointerButton::Right, wParam, lParam))
                {
                    return 0;
                }
                break;
            case WM_LBUTTONUP:
                if (HandleButtonUp(DragPointerButton::Left, lParam))
                {
                    return 0;
                }
                break;
            case WM_RBUTTONUP:
                if (HandleButtonUp(DragPointerButton::Right, lParam))
                {
                    return 0;
                }
                break;
            case WM_MOUSEMOVE:
                if (IsModeEnabled())
                {
                    g_InputHook.mLastClientX = GET_X_LPARAM(lParam);
                    g_InputHook.mLastClientY = GET_Y_LPARAM(lParam);
                    Emit(DragPointerEventKind::Move, DragPointerButton::None,
                        g_InputHook.mLastClientX, g_InputHook.mLastClientY);
                }
                // 原生窗口过程仍接收悬停/移动，但不得看到插件已接管的按钮按下状态。
                if ((g_InputHook.mSwallowedButtons & LEFT_BUTTON_MASK) != 0)
                {
                    wParam &= ~static_cast<WPARAM>(MK_LBUTTON);
                }
                if ((g_InputHook.mSwallowedButtons & RIGHT_BUTTON_MASK) != 0)
                {
                    wParam &= ~static_cast<WPARAM>(MK_RBUTTON);
                }
                break;
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
                if (wParam == VK_ESCAPE && IsModeEnabled())
                {
                    g_InputHook.mSwallowEscapeUp = true;
                    CancelGesture();
                    return 0;
                }
                break;
            case WM_KEYUP:
            case WM_SYSKEYUP:
                if (wParam == VK_ESCAPE && g_InputHook.mSwallowEscapeUp)
                {
                    g_InputHook.mSwallowEscapeUp = false;
                    return 0;
                }
                break;
            case WM_CAPTURECHANGED:
                if (g_InputHook.mOwnsCapture && !g_InputHook.mIsReleasingCapture &&
                    reinterpret_cast<HWND>(lParam) != ownerWnd)
                {
                    g_InputHook.mOwnsCapture = false;
                    CancelGesture();
                }
                break;
            case WM_KILLFOCUS:
            case WM_CANCELMODE:
                CancelGesture();
                break;
            case WM_ACTIVATEAPP:
                if (wParam == FALSE)
                {
                    CancelGesture();
                }
                break;
            case WM_NCDESTROY:
            {
                CancelGesture();
                const auto result = DefSubclassProc(ownerWnd, message, wParam, lParam);
                RemoveWindowSubclass(ownerWnd, &DragSubclassProc, SUBCLASS_ID);
                g_InputHook = {};
                g_PublishedOwnerWnd.store(nullptr, std::memory_order_release);
                return result;
            }
            default:
                break;
            }
            return DefSubclassProc(ownerWnd, message, wParam, lParam);
        }
    }

    bool InstallDragFormationInputHook(HWND ownerWnd,
        DragPointerCallback callback, DragModeQuery isModeEnabled,
        std::string& outError)
    {
        if (!ownerWnd || !callback || !isModeEnabled || !IsWindow(ownerWnd))
        {
            outError = "drag input hook requires a valid window and callbacks";
            return false;
        }
        const auto ownerThreadId = GetWindowThreadProcessId(ownerWnd, nullptr);
        if (!ownerThreadId || ownerThreadId != GetCurrentThreadId())
        {
            outError = "drag input hook must be installed on the window owner thread";
            return false;
        }
        if (g_InputHook.mIsInstalled)
        {
            if (g_InputHook.mOwnerWnd != ownerWnd)
            {
                outError = "drag input hook is already attached to another window";
                return false;
            }
            // 旧 Shutdown 投递的消息可能仍在队列中；先在所属线程完成旧捕获清理。
            if (g_DisableRequested.load(std::memory_order_acquire))
            {
                CompleteDisableOnOwnerThread();
            }
            g_DisableRequested.store(false, std::memory_order_release);
            g_InputHook.mCallback = callback;
            g_InputHook.mModeQuery = isModeEnabled;
            outError.clear();
            return true;
        }
        g_InputHook.mOwnerWnd = ownerWnd;
        g_InputHook.mOwnerThreadId = ownerThreadId;
        g_InputHook.mCallback = callback;
        g_InputHook.mModeQuery = isModeEnabled;
        g_DisableRequested.store(false, std::memory_order_release);
        if (!SetWindowSubclass(ownerWnd, &DragSubclassProc, SUBCLASS_ID, 0))
        {
            g_InputHook = {};
            outError = "cannot subclass the game owner window for drag input";
            return false;
        }
        g_InputHook.mIsInstalled = true;
        g_PublishedOwnerWnd.store(ownerWnd, std::memory_order_release);
        outError.clear();
        return true;
    }

    void DisableDragFormationInputHook() noexcept
    {
        g_DisableRequested.store(true, std::memory_order_release);
        const auto ownerWnd = g_PublishedOwnerWnd.load(std::memory_order_acquire);
        if (!ownerWnd)
        {
            return;
        }
        if (GetWindowThreadProcessId(ownerWnd, nullptr) == GetCurrentThreadId())
        {
            CompleteDisableOnOwnerThread();
        }
        else
        {
            // 窗口所有权与捕获只能在所属线程清理；随后任意窗口消息也会检查此请求。
            (void)PostMessageW(ownerWnd, DISABLE_MESSAGE, 0, 0);
        }
    }

    void CancelDragFormationInputCapture() noexcept
    {
        if (IsOnOwnerThread())
        {
            CancelGesture();
        }
    }
}
