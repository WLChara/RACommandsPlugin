#include "Bootstrap/PluginStartup.h"
#include "Bootstrap/PluginRuntime.h"
#include "Bootstrap/RuntimeDiagnostics.h"
#include "Game/TargetVersion.h"

#include <atomic>
#include <string>

namespace ra_commands::bootstrap
{
    namespace
    {
        // 自动入口只启动一次；显式 Initialize 的重新启用仍由 PluginRuntime 拥有。
        std::atomic<bool> g_IsStartupCancelled{false};
        // 这是只读就绪状态的轮询间隔，不是初始化完成的时间假设。
        constexpr DWORD STARTUP_POLL_INTERVAL_MS = 50;

        DWORD WINAPI BootstrapThread(LPVOID)
        {
            try
            {
                if (g_IsStartupCancelled.load(std::memory_order_acquire))
                {
                    return 0;
                }
                TraceRuntimeStartup();
                std::string error;
                if (!game::IsSupportedHost(error))
                {
                    TraceRuntime("bootstrap host-check failed: " + error);
                    return 0;
                }
                TraceRuntime("bootstrap waiting-for-native-commands");
                while (!g_IsStartupCancelled.load(std::memory_order_acquire))
                {
                    if (game::IsNativeCommandTableReady())
                    {
                        // AutoInitialize 在 runtime 锁内再次核对 Shutdown 状态；取消与安装竞态
                        // 由既有生命周期锁解决。版本校验结果缓存，不在轮询中重算 SHA。
                        const bool initialized = AutoInitialize();
                        TraceRuntime(initialized ? "bootstrap-complete initialized=1" :
                            "bootstrap-complete initialized=0");
                        return 0;
                    }
                    Sleep(STARTUP_POLL_INTERVAL_MS);
                }
                TraceRuntime("bootstrap-cancelled before-native-command-ready");
            }
            catch (...)
            {
                OutputDebugStringA("[RACommandsPlugin] bootstrap initialization failed\n");
                TraceRuntime("bootstrap initialization threw an exception");
            }
            return 0;
        }
    }

    bool StartPluginFromDllMain() noexcept
    {
        if (!game::IsCandidateGameHost())
        {
            return true;
        }

        // 后续 worker 等待与 Hook / 原生命令均保存本 DLL 的代码地址。
        // 游戏进程沿用既有常驻契约；必须在线程创建前固定，消除 worker 启动前卸载窗口。
        HMODULE pinnedModule = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&BootstrapThread), &pinnedModule))
        {
            return false;
        }
        HANDLE thread = CreateThread(nullptr, 0, &BootstrapThread, nullptr, 0, nullptr);
        if (!thread)
        {
            return false;
        }
        CloseHandle(thread);
        return true;
    }

    void CancelPluginStartup() noexcept
    {
        g_IsStartupCancelled.store(true, std::memory_order_release);
    }
}
