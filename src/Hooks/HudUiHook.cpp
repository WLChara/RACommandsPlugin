#include "Hooks/HudUiHook.h"
#include "Game/TargetProfiles/HudUiProfile.h"
#include "Memory/ProcessMemory.h"

#include <CommCtrl.h>
#include <array>
#include <atomic>
#include <cstring>

#pragma comment(lib, "comctl32.lib")

namespace ra_commands::game
{
    namespace
    {
        constexpr UINT_PTR SUBCLASS_ID = 0x52414855u;
        constexpr UINT DISABLE_MESSAGE = WM_APP + 0x2D48;
        std::atomic_bool g_IsEnabled{false};
        std::atomic<HWND> g_Window{nullptr};
        DWORD g_OwnerThread = 0;
        HudRenderCallback g_Restore = nullptr;
        HudRenderCallback g_Render = nullptr;
        HudMessageCallback g_Input = nullptr;
        bool g_HasPatches = false;
        std::uintptr_t g_MessageDraw = hud_profile::MESSAGE_DRAW;

        void __cdecl RestoreHud() noexcept
        {
            if (g_Restore && GetCurrentThreadId() == g_OwnerThread)
            {
                try { g_Restore(); }
                catch (...) { OutputDebugStringA("[RACommandsPlugin] HUD restore failed\n"); }
            }
        }
        void __cdecl RenderHud() noexcept
        {
            if (g_IsEnabled.load(std::memory_order_acquire) && g_Render &&
                GetCurrentThreadId() == g_OwnerThread)
            {
                try { g_Render(); }
                catch (...) { OutputDebugStringA("[RACommandsPlugin] HUD render failed\n"); }
            }
        }

        #include "Hooks/HudUiBridge.inl"
        template<std::size_t TSize>
        bool Matches(std::uintptr_t address, const std::array<std::uint8_t, TSize>& expected)
        {
            std::array<std::uint8_t, TSize> actual{};
            return memory::TryReadMemory(address, actual.data(), actual.size()) && actual == expected;
        }
        template<std::size_t TSize>
        bool WriteCode(std::uintptr_t address, const std::array<std::uint8_t, TSize>& bytes)
        {
            DWORD oldProtect = 0;
            auto* const destination = reinterpret_cast<void*>(address);
            if (!VirtualProtect(destination, bytes.size(), PAGE_EXECUTE_READWRITE, &oldProtect)) { return false; }
            std::memcpy(destination, bytes.data(), bytes.size());
            FlushInstructionCache(GetCurrentProcess(), destination, bytes.size());
            DWORD discarded = 0;
            (void)VirtualProtect(destination, bytes.size(), oldProtect, &discarded);
            return true;
        }
        template<std::size_t TSize>
        std::array<std::uint8_t, TSize> MakeCall(std::uintptr_t site, void(*bridge)())
        {
            static_assert(TSize >= 5);
            std::array<std::uint8_t, TSize> bytes{};
            bytes.fill(0x90);
            bytes[0] = 0xE8;
            const auto displacement = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(bridge) - site - 5);
            std::memcpy(bytes.data() + 1, &displacement, 4);
            return bytes;
        }
        LRESULT CALLBACK HudSubclassProc(HWND window, UINT message, WPARAM wParam,
            LPARAM lParam, UINT_PTR subclassId, DWORD_PTR)
        {
            if (subclassId != SUBCLASS_ID || window != g_Window.load(std::memory_order_acquire))
            { return DefSubclassProc(window, message, wParam, lParam); }
            bool consumed = false;
            // 即使停用，消息回调仍可消费先前接管的 Up 并清理捕获。
            if (g_Input)
            {
                try { consumed = g_Input(window, message == DISABLE_MESSAGE ? WM_CANCELMODE : message, wParam, lParam); }
                catch (...) { OutputDebugStringA("[RACommandsPlugin] HUD input failed\n"); }
            }
            if (message == WM_NCDESTROY)
            {
                g_IsEnabled.store(false, std::memory_order_release);
                const auto result = DefSubclassProc(window, message, wParam, lParam);
                RemoveWindowSubclass(window, &HudSubclassProc, SUBCLASS_ID);
                g_Window.store(nullptr, std::memory_order_release);
                return result;
            }
            if (consumed || message == DISABLE_MESSAGE) { return 0; }
            return DefSubclassProc(window, message, wParam, lParam);
        }
    }
    bool InstallHudUiHook(HWND window, HudRenderCallback restore,
        HudRenderCallback render, HudMessageCallback input, std::string& outError)
    {
        if (!window || !IsWindow(window) || !restore || !render || !input ||
            GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId())
        { outError = "HUD requires the game window owner thread and valid callbacks"; return false; }
        const auto installedWindow = g_Window.load(std::memory_order_acquire);
        if (installedWindow && installedWindow != window)
        { outError = "HUD input is attached to another window"; return false; }
        if (!g_HasPatches && (!Matches(hud_profile::RESTORE_SITE, hud_profile::RESTORE_BYTES) ||
            !Matches(hud_profile::DRAW_SITE, hud_profile::DRAW_BYTES)))
        { outError = "HUD composition sites differ from the verified game build"; return false; }
        if (!installedWindow && !SetWindowSubclass(window, &HudSubclassProc, SUBCLASS_ID, 0))
        { outError = "cannot attach shared HUD input"; return false; }
        g_Window.store(window, std::memory_order_release);
        g_OwnerThread = GetCurrentThreadId();
        g_Restore = restore;
        g_Render = render;
        g_Input = input;
        if (!g_HasPatches)
        {
            // 安装点本身由游戏窗口线程执行，不会与该线程的 DrawOnTop 重入。
            if (!WriteCode(hud_profile::RESTORE_SITE, MakeCall<10>(hud_profile::RESTORE_SITE, &RestoreBridge)))
            { outError = "cannot install HUD background restoration bridge"; return false; }
            if (!WriteCode(hud_profile::DRAW_SITE, MakeCall<5>(hud_profile::DRAW_SITE, &DrawBridge)))
            {
                (void)WriteCode(hud_profile::RESTORE_SITE, hud_profile::RESTORE_BYTES);
                outError = "cannot install HUD final composition bridge"; return false;
            }
            g_HasPatches = true;
        }
        g_IsEnabled.store(true, std::memory_order_release);
        outError.clear();
        return true;
    }
    void DisableHudUiHook() noexcept
    {
        g_IsEnabled.store(false, std::memory_order_release);
        const auto window = g_Window.load(std::memory_order_acquire);
        if (!window) { return; }
        if (GetCurrentThreadId() == GetWindowThreadProcessId(window, nullptr))
        {
            WPARAM flags = 0;
            if (g_Input) { try { (void)g_Input(window, WM_CANCELMODE, flags, 0); } catch (...) {} }
        }
        else { (void)PostMessageW(window, DISABLE_MESSAGE, 0, 0); }
    }
    bool IsHudUiHookEnabled() noexcept { return g_IsEnabled.load(std::memory_order_acquire); }
}
