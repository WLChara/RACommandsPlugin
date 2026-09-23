#include "Hooks/MainFrameHook.h"

#include "Memory/ProcessMemory.h"

#include <Windows.h>
#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>

namespace ra_commands::game
{
    namespace
    {
        // docs/ida-evidence.md 所列样本中的 GameUtils::MainFrame。
        constexpr std::uintptr_t MAIN_FRAME_ADDRESS = 0x0055D360u;
        constexpr std::array<std::uint8_t, 16> MAIN_FRAME_ENTRY = {
            0xA0, 0xA0, 0xE9, 0xA8, 0x00, 0x81, 0xEC, 0xB4,
            0x01, 0x00, 0x00, 0x84, 0xC0, 0x53, 0x55, 0x56
        };

        using MainFrameFunction = bool(__cdecl*)();
        std::atomic<MainFrameFunction> g_OriginalMainFrame{nullptr};
        std::atomic<GameFrameCallback> g_FrameCallback{nullptr};
        bool g_IsHookInstalled = false;

        bool __cdecl HookMainFrame()
        {
            if (const auto callback = g_FrameCallback.load(std::memory_order_acquire))
            {
                try
                {
                    callback();
                }
                catch (...)
                {
                    OutputDebugStringA("[RACommandsPlugin] unhandled game-frame callback exception\n");
                }
            }
            const auto original = g_OriginalMainFrame.load(std::memory_order_acquire);
            return original ? original() : false;
        }
    }

    bool InstallMainFrameHook(GameFrameCallback callback, std::string& outError)
    {
        if (callback == nullptr)
        {
            outError = "main-frame callback is null";
            return false;
        }
        if (g_IsHookInstalled)
        {
            g_FrameCallback.store(callback, std::memory_order_release);
            return true;
        }

        std::array<std::uint8_t, MAIN_FRAME_ENTRY.size()> actualEntry{};
        if (!memory::TryReadMemory(MAIN_FRAME_ADDRESS, actualEntry.data(), actualEntry.size()) ||
            actualEntry != MAIN_FRAME_ENTRY)
        {
            outError = "game main-frame entry differs from supported build or is already hooked";
            return false;
        }

        const MH_STATUS initialization = MH_Initialize();
        if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED)
        {
            outError = "MinHook initialization failed";
            return false;
        }

        void* const target = reinterpret_cast<void*>(MAIN_FRAME_ADDRESS);
        void* trampoline = nullptr;
        if (MH_CreateHook(target, &HookMainFrame, &trampoline) != MH_OK)
        {
            outError = "main-frame hook creation failed";
            return false;
        }
        if (!trampoline)
        {
            MH_RemoveHook(target);
            outError = "main-frame hook trampoline is null";
            return false;
        }
        g_OriginalMainFrame.store(
            reinterpret_cast<MainFrameFunction>(trampoline), std::memory_order_release);

        // Detour 与原生命令表都会保存本 DLL 的代码地址，启用前固定至进程退出。
        HMODULE pinnedModule = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&HookMainFrame), &pinnedModule))
        {
            MH_RemoveHook(target);
            g_OriginalMainFrame.store(nullptr, std::memory_order_release);
            outError = "cannot pin RACommandsPlugin for process lifetime";
            return false;
        }

        g_FrameCallback.store(callback, std::memory_order_release);
        if (MH_EnableHook(target) != MH_OK)
        {
            g_FrameCallback.store(nullptr, std::memory_order_release);
            MH_RemoveHook(target);
            g_OriginalMainFrame.store(nullptr, std::memory_order_release);
            outError = "main-frame hook activation failed";
            return false;
        }

        g_IsHookInstalled = true;
        outError.clear();
        return true;
    }

    void DisableMainFrameCallback()
    {
        g_FrameCallback.store(nullptr, std::memory_order_release);
    }
}
