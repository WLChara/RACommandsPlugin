#include "Hooks/MainFrameHook.h"

#include <Windows.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>

namespace ra_commands::game
{
    namespace
    {
        // docs/ida-evidence.md 所列样本中的 GameUtils::MainFrame。
        constexpr std::uintptr_t MAIN_FRAME_ADDRESS = 0x0055D360u;
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

        const MH_STATUS initialization = MH_Initialize();
        if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED)
        {
            outError = "MinHook initialization failed: ";
            outError += MH_StatusToString(initialization);
            return false;
        }

        void* const target = reinterpret_cast<void*>(MAIN_FRAME_ADDRESS);
        void* trampoline = nullptr;
        // 目标EXE指纹已由Bootstrap校验。让MinHook从当前入口构造trampoline；
        // x86先前Hook的E9由其搬迁，回调再经trampoline转发给既有链。
        const MH_STATUS creation = MH_CreateHook(target, &HookMainFrame, &trampoline);
        if (creation != MH_OK)
        {
            outError = "main-frame hook creation failed: ";
            outError += MH_StatusToString(creation);
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
        const MH_STATUS activation = MH_EnableHook(target);
        if (activation != MH_OK)
        {
            g_FrameCallback.store(nullptr, std::memory_order_release);
            MH_RemoveHook(target);
            g_OriginalMainFrame.store(nullptr, std::memory_order_release);
            outError = "main-frame hook activation failed: ";
            outError += MH_StatusToString(activation);
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
