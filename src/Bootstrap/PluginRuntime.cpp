#include "Bootstrap/PluginRuntime.h"

#include "Commands/AutoLoadCommand/AutoLoadCommandService.h"
#include "ClickedMission/ClickedMissionDispatcher.h"
#include "Commands/AutoLoadCommand/AutoLoadCommandRegistry.h"
#include "Commands/AutoLoadCommand/AutoLoadGameAdapter.h"
#include "Commands/TeslaChargeCommand/TeslaChargeCommandRegistry.h"
#include "Commands/TeslaChargeCommand/TeslaChargeCommandService.h"
#include "Commands/TeslaChargeCommand/TeslaChargeGameAdapter.h"
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
        game::TeslaChargeGameAdapter g_TeslaChargeGameAdapter;
        commands::ClickedMissionDispatcher g_ClickedMissionDispatcher(g_ClickedMissionGameAdapter);
        autoload::AutoLoadCommandService g_AutoLoadCommandService(
            g_AutoLoadGameAdapter, g_ClickedMissionDispatcher);
        tesla_charge::TeslaChargeCommandService g_TeslaChargeCommandService(
            g_TeslaChargeGameAdapter, g_ClickedMissionDispatcher);
        std::string g_LastError;
        bool g_IsInitialized = false;
        bool g_IsAutoStartCancelled = false;
        CommandState g_AutoLoadCommandState = CommandState::WaitingForGame;
        CommandState g_TeslaChargeCommandState = CommandState::WaitingForGame;
        bool g_HotkeysReloaded = false;
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

        void OnTeslaChargeHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                g_TeslaChargeCommandService.OnHotkey();
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
            g_TeslaChargeCommandService.OnGameFrame();
            if (!g_ClickedMissionGameAdapter.IsMatchReady())
            {
                return;
            }

            if (g_AutoLoadCommandState == CommandState::WaitingForGame)
            {
                const auto registration = game::TryRegisterAutoLoadCommand(
                    g_GameSymbols, &OnAutoLoadHotkey, g_LastError);
                if (registration == game::CommandRegistrationResult::Registered)
                {
                    g_AutoLoadCommandState = CommandState::Registered;
                }
                else if (registration != game::CommandRegistrationResult::Pending)
                {
                    g_AutoLoadCommandState = CommandState::Failed;
                    OutputDebugStringA(("[RACommandsPlugin] " + g_LastError + "\n").c_str());
                }
            }

            if (g_TeslaChargeCommandState == CommandState::WaitingForGame)
            {
                const auto registration = game::TryRegisterTeslaChargeCommand(
                    g_GameSymbols, &OnTeslaChargeHotkey, g_LastError);
                if (registration == game::CommandRegistrationResult::Registered)
                {
                    g_TeslaChargeCommandState = CommandState::Registered;
                }
                else if (registration != game::CommandRegistrationResult::Pending)
                {
                    g_TeslaChargeCommandState = CommandState::Failed;
                    OutputDebugStringA(("[RACommandsPlugin] " + g_LastError + "\n").c_str());
                }
            }

            if (!g_HotkeysReloaded &&
                g_AutoLoadCommandState != CommandState::WaitingForGame &&
                g_TeslaChargeCommandState != CommandState::WaitingForGame &&
                (g_AutoLoadCommandState == CommandState::Registered ||
                    g_TeslaChargeCommandState == CommandState::Registered))
            {
                g_HotkeysReloaded = true;
                if (!g_GameSymbols.ReloadKeyboardHotkeys())
                {
                    OutputDebugStringA("[RACommandsPlugin] native hotkey reload failed\n");
                }
                else
                {
                    OutputDebugStringA("[RACommandsPlugin] native commands registered\n");
                }
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
        game::DisableTeslaChargeCommand();
        g_TeslaChargeCommandService.Reset();
        g_ClickedMissionDispatcher.Reset();
        g_AutoLoadCommandState = CommandState::WaitingForGame;
        g_TeslaChargeCommandState = CommandState::WaitingForGame;
        g_HotkeysReloaded = false;
        g_GameThreadId = 0;
        g_LastError.clear();
        g_GameSymbols = {};
    }
}
