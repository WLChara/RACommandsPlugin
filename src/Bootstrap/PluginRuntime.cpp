#include "Bootstrap/PluginRuntime.h"

#include "Commands/AutoLoadCommand/AutoLoadCommandService.h"
#include "ClickedMission/ClickedMissionDispatcher.h"
#include "Commands/AutoLoadCommand/AutoLoadCommandRegistry.h"
#include "Commands/AutoLoadCommand/AutoLoadGameAdapter.h"
#include "ClickedMission/ClickedMissionGameAdapter.h"
#include "Game/GameSymbols.h"
#include "Hooks/MainFrameHook.h"
#include "Game/TargetVersion.h"

#include <Windows.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace ra_commands::bootstrap
{
    namespace
    {
        enum class CommandState
        {
            WaitingForGame,
            Registered,
            Failed
        };

        std::mutex g_StateMutex;
        game::GameSymbols g_GameSymbols;
        game::ClickedMissionGameAdapter g_ClickedMissionGameAdapter;
        game::AutoLoadGameAdapter g_AutoLoadGameAdapter;
        commands::ClickedMissionDispatcher g_ClickedMissionDispatcher(g_ClickedMissionGameAdapter);
        autoload::AutoLoadCommandService g_AutoLoadCommandService(
            g_AutoLoadGameAdapter, g_ClickedMissionDispatcher);
        std::string g_LastError;
        bool g_IsInitialized = false;
        bool g_IsAutoStartCancelled = false;
        CommandState g_CommandState = CommandState::WaitingForGame;
        DWORD g_GameThreadId = 0;

        void OnAutoLoadHotkey()
        {
            // 外部 Shutdown 可并发执行；服务状态在热键回调期间保持不变。
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                g_AutoLoadCommandService.OnHotkey();
            }
        }

        void OnGameFrame()
        {
            // 该锁也覆盖游戏调用，防止外部 Shutdown 在半次提交中清空状态。
            // 若未来出现游戏回调重入本 DLL，需改为游戏线程上的停用协议。
            std::lock_guard lock(g_StateMutex);
            if (!g_IsInitialized)
            {
                return;
            }

            const DWORD threadId = GetCurrentThreadId();
            if (g_GameThreadId == 0)
            {
                g_GameThreadId = threadId;
            }
            if (g_GameThreadId != threadId)
            {
                return;
            }

            g_ClickedMissionDispatcher.OnGameFrame();
            if (g_CommandState != CommandState::WaitingForGame ||
                !g_ClickedMissionGameAdapter.IsMatchReady())
            {
                return;
            }

            const auto registration = game::TryRegisterAutoLoadCommand(
                g_GameSymbols, &OnAutoLoadHotkey, g_LastError);
            if (registration == game::CommandRegistrationResult::Pending)
            {
                return;
            }
            if (registration != game::CommandRegistrationResult::Registered)
            {
                g_CommandState = CommandState::Failed;
                OutputDebugStringA(("[RACommandsPlugin] " + g_LastError + "\n").c_str());
                return;
            }

            g_CommandState = CommandState::Registered;
            if (!g_GameSymbols.ReloadKeyboardHotkeys())
            {
                OutputDebugStringA("[RACommandsPlugin] native hotkey reload failed\n");
            }
            else
            {
                OutputDebugStringA("[RACommandsPlugin] YRHMAutoLoad registered\n");
            }
        }

        bool InitializeLocked()
        {
            if (g_IsInitialized)
            {
                return true;
            }

            const auto moduleBase = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (!game::IsSupportedHost(g_LastError) ||
                !g_GameSymbols.Resolve(moduleBase, g_LastError))
            {
                OutputDebugStringA(("[RACommandsPlugin] " + g_LastError + "\n").c_str());
                return false;
            }

            if (!game::InstallMainFrameHook(&OnGameFrame, g_LastError))
            {
                OutputDebugStringA(("[RACommandsPlugin] " + g_LastError + "\n").c_str());
                return false;
            }

            g_IsInitialized = true;
            OutputDebugStringA("[RACommandsPlugin] command signatures resolved; main-frame hook installed\n");
            return true;
        }
    }

    bool AutoInitialize()
    {
        std::lock_guard lock(g_StateMutex);
        return !g_IsAutoStartCancelled && InitializeLocked();
    }

    bool Initialize()
    {
        std::lock_guard lock(g_StateMutex);
        g_IsAutoStartCancelled = false;
        return InitializeLocked();
    }

    bool IsReady()
    {
        std::lock_guard lock(g_StateMutex);
        return g_IsInitialized && g_GameSymbols.IsReady();
    }

    void Shutdown()
    {
        std::lock_guard lock(g_StateMutex);
        g_IsAutoStartCancelled = true;
        g_IsInitialized = false;
        game::DisableMainFrameCallback();
        game::DisableAutoLoadCommand();
        g_ClickedMissionDispatcher.Reset();
        g_CommandState = CommandState::WaitingForGame;
        g_GameThreadId = 0;
        g_LastError.clear();
        g_GameSymbols = {};
    }
}
