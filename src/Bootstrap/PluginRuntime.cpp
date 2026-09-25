#include "Bootstrap/PluginRuntime.h"

#include "Commands/AutoLoadCommand/AutoLoadCommandService.h"
#include "Commands/AutoNanoCloudCommand/AutoNanoCloudCommandRegistry.h"
#include "Commands/AutoNanoCloudCommand/AutoNanoCloudCommandService.h"
#include "Commands/AutoNanoCloudCommand/AutoNanoCloudGameAdapter.h"
#include "Commands/AutoCrush/AutoCrushCommandService.h"
#include "Commands/AutoCrush/AutoCrushGameAdapter.h"
#include "Commands/AutoCrush/AutoCrushIntentHandler.h"
#include "Commands/AutoCrushAddCommand/AutoCrushAddCommandRegistry.h"
#include "Commands/AutoCrushRemoveCommand/AutoCrushRemoveCommandRegistry.h"
#include "Commands/SafeModeToggleCommand/SafeModeState.h"
#include "Commands/SafeModeToggleCommand/SafeModeToggleCommandRegistry.h"
#include "Commands/SafeModeToggleCommand/SafeModeToggleCommandService.h"
#include "ClickedMission/ClickedMissionDispatcher.h"
#include "Commands/AutoLoadCommand/AutoLoadCommandRegistry.h"
#include "Commands/AutoLoadCommand/AutoLoadGameAdapter.h"
#include "Commands/AutoRepairCommand/AutoRepairCommandRegistry.h"
#include "Commands/AutoRepairCommand/AutoRepairCommandService.h"
#include "Commands/AutoRepairCommand/AutoRepairGameAdapter.h"
#include "Commands/AirSpreadCommand/AirSpreadCommandRegistry.h"
#include "Commands/AirSpreadCommand/AirSpreadCommandService.h"
#include "Commands/AirSpreadCommand/AirSpreadGameAdapter.h"
#include "Commands/AirSpreadCommand/AirSpreadIntentHandler.h"
#include "Commands/AFloorCommand/AFloorCommandRegistry.h"
#include "Commands/AFloorCommand/AFloorCommandService.h"
#include "Commands/AFloorCommand/AFloorGameAdapter.h"
#include "Commands/BeaconClearCommand/BeaconClearCommandRegistry.h"
#include "Commands/BeaconClearCommand/BeaconClearCommandService.h"
#include "Commands/BeaconClearCommand/BeaconClearGameAdapter.h"
#include "Commands/RangeDisplayCommand/RangeDisplayCommandRegistry.h"
#include "Commands/RangeDisplayCommand/RangeDisplayCommandService.h"
#include "Commands/RangeDisplayCommand/RangeDisplayGameAdapter.h"
#include "Commands/TeslaChargeCommand/TeslaChargeCommandRegistry.h"
#include "Commands/TeslaChargeCommand/TeslaChargeCommandService.h"
#include "Commands/TeslaChargeCommand/TeslaChargeGameAdapter.h"
#include "Commands/TeslaChargeCommand/TeslaChargeIntentHandler.h"
#include "Commands/Selection/SelectionCommandService.h"
#include "Commands/IfvModeSelectCommand/IfvModeSelectCommandRegistry.h"
#include "Commands/MindControlSelectCommand/MindControlSelectCommandRegistry.h"
#include "Commands/UnitKindSelectCommand/UnitKindSelectCommandRegistry.h"
#include "Commands/AmmoSelectCommand/AmmoSelectCommandRegistry.h"
#include "Commands/PassengerSelectCommand/PassengerSelectCommandRegistry.h"
#include "Commands/CycleSelectCommand/CycleSelectCommandRegistry.h"
#include "Commands/UndoSelectionCommand/UndoSelectionCommandRegistry.h"
#include "ClickedMission/ClickedMissionGameAdapter.h"
#include "NetworkEvent/NativeNetworkEventAdapter.h"
#include "Game/GameSymbols.h"
#include "Game/SelectionGameAdapter.h"
#include "Hooks/MainFrameHook.h"
#include "Hooks/AFloorHook.h"
#include "Hooks/RangeDisplayHook.h"
#include "Game/TargetVersion.h"

#include <Windows.h>

#include <array>
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

        using RegisterCommand = game::CommandRegistrationResult(*)(
            const game::GameSymbols&, std::string&);

        template<auto TRegister, auto TCallback>
        game::CommandRegistrationResult RegisterConfiguredCommand(
            const game::GameSymbols& symbols, std::string& outError)
        {
            return TRegister(symbols, TCallback, outError);
        }

        struct CommandEntry
        {
            RegisterCommand Register;
            void(*Disable)();
            CommandState State = CommandState::WaitingForGame;
        };

        std::mutex g_StateMutex;
        game::GameSymbols g_GameSymbols;
        game::ClickedMissionGameAdapter g_ClickedMissionGameAdapter;
        game::NativeNetworkEventAdapter g_NativeNetworkEventAdapter;
        game::AutoLoadGameAdapter g_AutoLoadGameAdapter;
        game::AutoNanoCloudGameAdapter g_AutoNanoCloudGameAdapter;
        game::AutoRepairGameAdapter g_AutoRepairGameAdapter;
        game::TeslaChargeGameAdapter g_TeslaChargeGameAdapter;
        game::SelectionGameAdapter g_SelectionGameAdapter;
        game::AFloorGameAdapter g_AFloorGameAdapter;
        game::BeaconClearGameAdapter g_BeaconClearGameAdapter;
        game::RangeDisplayGameAdapter g_RangeDisplayGameAdapter;
        commands::ClickedMissionDispatcher g_ClickedMissionDispatcher(g_ClickedMissionGameAdapter);
        auto_nano_cloud::AutoNanoCloudCommandService g_AutoNanoCloudCommandService(
            g_AutoNanoCloudGameAdapter, g_ClickedMissionDispatcher);
        safe_mode::SafeModeToggleCommandService g_SafeModeToggleCommandService(
            safe_mode::g_IsSafeModeEnabled);
        game::AirSpreadGameAdapter g_AirSpreadGameAdapter(g_ClickedMissionDispatcher);
        game::AutoCrushGameAdapter g_AutoCrushGameAdapter(g_ClickedMissionDispatcher);
        auto_crush::AutoCrushCommandService g_AutoCrushCommandService(g_AutoCrushGameAdapter);
        autoload::AutoLoadCommandService g_AutoLoadCommandService(
            g_AutoLoadGameAdapter, g_ClickedMissionDispatcher,
            safe_mode::g_IsSafeModeEnabled);
        auto_repair::AutoRepairCommandService g_AutoRepairCommandService(
            g_AutoRepairGameAdapter, safe_mode::g_IsSafeModeEnabled);
        air_spread::AirSpreadCommandService g_AirSpreadCommandService(g_AirSpreadGameAdapter);
        tesla_charge::TeslaChargeCommandService g_TeslaChargeCommandService(
            g_TeslaChargeGameAdapter, g_ClickedMissionDispatcher);
        selection::SelectionCommandService g_SelectionCommandService(g_SelectionGameAdapter);
        a_floor::AFloorCommandService g_AFloorCommandService(g_AFloorGameAdapter);
        beacon_clear::BeaconClearCommandService g_BeaconClearCommandService(g_BeaconClearGameAdapter);
        range_display::RangeDisplayCommandService g_RangeDisplayCommandService(g_RangeDisplayGameAdapter);
        std::string g_LastError;
        bool g_IsInitialized = false;
        bool g_IsAutoStartCancelled = false;
        bool g_HotkeysReloaded = false;
        bool g_AFloorHooksReady = false;
        bool g_RangeDisplayHookReady = false;
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

        void OnAutoNanoCloudHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                g_AutoNanoCloudCommandService.OnHotkey();
            }
        }

        void OnAutoCrushAddHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_AFloorHooksReady &&
                g_GameThreadId == GetCurrentThreadId())
            {
                g_AutoCrushCommandService.OnAddHotkey();
            }
        }

        void OnAutoCrushRemoveHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_AFloorHooksReady &&
                g_GameThreadId == GetCurrentThreadId())
            {
                g_AutoCrushCommandService.OnRemoveHotkey();
            }
        }

        void OnManualVehicleOrder(std::uint64_t actorId)
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_AFloorHooksReady &&
                g_GameThreadId == GetCurrentThreadId())
            {
                g_AutoCrushCommandService.OnManualOrder(actorId);
            }
        }

        void OnSafeModeToggleHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                const bool wasEnabled = safe_mode::g_IsSafeModeEnabled.load(
                    std::memory_order_acquire);
                (void)g_SafeModeToggleCommandService.OnHotkey(
                    g_ClickedMissionDispatcher.IsSessionActive());
                const bool isEnabled = safe_mode::g_IsSafeModeEnabled.load(
                    std::memory_order_acquire);
                if (wasEnabled != isEnabled)
                {
                    g_AutoRepairCommandService.OnSafeModeChanged();
                    if (isEnabled)
                    {
                        g_ClickedMissionDispatcher.CancelByProducer(
                            commands::ClickedMissionProducer::AutoLoad);
                    }
                }
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

        void OnAutoRepairHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                g_AutoRepairCommandService.OnHotkey();
            }
        }

        void OnAirSpreadHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                (void)g_AirSpreadCommandService.OnHotkey();
            }
        }

        void OnAFloorHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_AFloorHooksReady &&
                g_GameThreadId == GetCurrentThreadId())
            {
                g_AFloorCommandService.OnHotkey();
            }
        }

        void OnBeaconClearHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                g_BeaconClearCommandService.OnHotkey();
            }
        }

        void OnRangeDisplayHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_RangeDisplayHookReady &&
                g_GameThreadId == GetCurrentThreadId())
            {
                g_RangeDisplayCommandService.OnHotkey();
            }
        }

        bool IsRangeDisplayModeEnabled()
        {
            return g_RangeDisplayCommandService.IsEnabled();
        }

        game::CommandRegistrationResult RegisterRangeWhenHookReady(
            const game::GameSymbols& symbols, void(*callback)(), std::string& outError)
        {
            if (!g_RangeDisplayHookReady)
            {
                outError = "range display hook is unavailable";
                return game::CommandRegistrationResult::Failed;
            }
            return game::TryRegisterRangeDisplayCommand(symbols, callback, outError);
        }

        bool IsAFloorModeEnabled()
        {
            return g_AFloorCommandService.IsEnabled();
        }

        game::CommandRegistrationResult RegisterAFloorWhenHookReady(
            const game::GameSymbols& symbols, void(*callback)(), std::string& outError)
        {
            if (!g_AFloorHooksReady)
            {
                outError = "A-floor hooks are unavailable";
                return game::CommandRegistrationResult::Failed;
            }
            return game::TryRegisterAFloorCommand(symbols, callback, outError);
        }

        game::CommandRegistrationResult RegisterAutoCrushAddWhenHookReady(
            const game::GameSymbols& symbols, void(*callback)(), std::string& outError)
        {
            if (!g_AFloorHooksReady)
            {
                outError = "manual-order observation hook is unavailable";
                return game::CommandRegistrationResult::Failed;
            }
            return game::TryRegisterAutoCrushAddCommand(symbols, callback, outError);
        }

        game::CommandRegistrationResult RegisterAutoCrushRemoveWhenHookReady(
            const game::GameSymbols& symbols, void(*callback)(), std::string& outError)
        {
            if (!g_AFloorHooksReady)
            {
                outError = "manual-order observation hook is unavailable";
                return game::CommandRegistrationResult::Failed;
            }
            return game::TryRegisterAutoCrushRemoveCommand(symbols, callback, outError);
        }

        void DispatchSelectionHotkey(void (selection::SelectionCommandService::*action)())
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                (g_SelectionCommandService.*action)();
            }
        }

        void OnMindControlSelectHotkey()
        {
            DispatchSelectionHotkey(&selection::SelectionCommandService::OnMindControlHotkey);
        }

        void OnUnitKindSelectHotkey()
        {
            DispatchSelectionHotkey(&selection::SelectionCommandService::OnKindHotkey);
        }

        void OnAmmoSelectHotkey()
        {
            DispatchSelectionHotkey(&selection::SelectionCommandService::OnAmmoHotkey);
        }

        void OnPassengerSelectHotkey()
        {
            DispatchSelectionHotkey(&selection::SelectionCommandService::OnPassengersHotkey);
        }

        void OnCycleSelectHotkey()
        {
            DispatchSelectionHotkey(&selection::SelectionCommandService::OnCycleHotkey);
        }

        void OnUndoSelectionHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                (void)g_SelectionCommandService.Undo();
            }
        }

        void OnIfvModeSelectHotkey(bool secondPress)
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                g_SelectionCommandService.OnIfvHotkey(secondPress, GetDoubleClickTime());
            }
        }

        // 所有原生命令共享主帧注册时机与一次热键重读。
        std::array<CommandEntry, 18> g_Commands{{
            {&RegisterConfiguredCommand<&game::TryRegisterSafeModeToggleCommand, &OnSafeModeToggleHotkey>, &game::DisableSafeModeToggleCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAutoLoadCommand, &OnAutoLoadHotkey>, &game::DisableAutoLoadCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAutoNanoCloudCommand, &OnAutoNanoCloudHotkey>, &game::DisableAutoNanoCloudCommand},
            {&RegisterConfiguredCommand<&RegisterAutoCrushAddWhenHookReady, &OnAutoCrushAddHotkey>, &game::DisableAutoCrushAddCommand},
            {&RegisterConfiguredCommand<&RegisterAutoCrushRemoveWhenHookReady, &OnAutoCrushRemoveHotkey>, &game::DisableAutoCrushRemoveCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterTeslaChargeCommand, &OnTeslaChargeHotkey>, &game::DisableTeslaChargeCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAutoRepairCommand, &OnAutoRepairHotkey>, &game::DisableAutoRepairCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAirSpreadCommand, &OnAirSpreadHotkey>, &game::DisableAirSpreadCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterMindControlSelectCommand, &OnMindControlSelectHotkey>, &game::DisableMindControlSelectCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterUnitKindSelectCommand, &OnUnitKindSelectHotkey>, &game::DisableUnitKindSelectCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAmmoSelectCommand, &OnAmmoSelectHotkey>, &game::DisableAmmoSelectCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterPassengerSelectCommand, &OnPassengerSelectHotkey>, &game::DisablePassengerSelectCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterCycleSelectCommand, &OnCycleSelectHotkey>, &game::DisableCycleSelectCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterUndoSelectionCommand, &OnUndoSelectionHotkey>, &game::DisableUndoSelectionCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterIfvModeSelectCommand, &OnIfvModeSelectHotkey>, &game::DisableIfvModeSelectCommand},
            {&RegisterConfiguredCommand<&RegisterAFloorWhenHookReady, &OnAFloorHotkey>, &game::DisableAFloorCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterBeaconClearCommand, &OnBeaconClearHotkey>, &game::DisableBeaconClearCommand},
            {&RegisterConfiguredCommand<&RegisterRangeWhenHookReady, &OnRangeDisplayHotkey>, &game::DisableRangeDisplayCommand}
        }};

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
                g_NativeNetworkEventAdapter.BindGameThread(threadId);
            }
            if (g_GameThreadId != threadId)
            {
                return;
            }

            if (g_AFloorHooksReady)
            {
                game::SetAFloorGameThread(threadId);
            }
            if (g_RangeDisplayHookReady)
            {
                game::SetRangeDisplayGameThread(threadId);
            }

            g_ClickedMissionDispatcher.OnGameFrame();
            g_AutoCrushCommandService.OnGameFrame();
            g_SafeModeToggleCommandService.OnGameFrame(
                g_ClickedMissionDispatcher.IsSessionActive(), g_ClickedMissionDispatcher.Epoch());
            g_TeslaChargeCommandService.OnGameFrame();
            g_AutoRepairCommandService.OnGameFrame();
            g_SelectionCommandService.OnGameFrame();
            g_AFloorCommandService.OnGameFrame();
            g_BeaconClearCommandService.OnGameFrame();
            g_RangeDisplayCommandService.OnGameFrame();
            if (!g_ClickedMissionGameAdapter.IsMatchReady())
            {
                return;
            }

            for (auto& command : g_Commands)
            {
                if (command.State != CommandState::WaitingForGame)
                {
                    continue;
                }

                const auto registration = command.Register(g_GameSymbols, g_LastError);
                if (registration == game::CommandRegistrationResult::Registered)
                {
                    command.State = CommandState::Registered;
                }
                else if (registration != game::CommandRegistrationResult::Pending)
                {
                    command.State = CommandState::Failed;
                    OutputDebugStringA(("[RACommandsPlugin] " + g_LastError + "\n").c_str());
                }
            }

            bool allCommandsSettled = true;
            bool hasRegisteredCommand = false;
            for (const auto& command : g_Commands)
            {
                allCommandsSettled &= command.State != CommandState::WaitingForGame;
                hasRegisteredCommand |= command.State == CommandState::Registered;
            }

            if (!g_HotkeysReloaded && allCommandsSettled && hasRegisteredCommand)
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

            // 在主帧回调启用前绑定已实现的命令处理器。
            if (!g_ClickedMissionGameAdapter.BindIntentHandler(
                    commands::ClickedMissionProducer::AutoCrush,
                    {&game::ValidateAutoCrushIntent, &game::AttemptAutoCrushIntent}) ||
                !g_ClickedMissionGameAdapter.BindIntentHandler(
                    commands::ClickedMissionProducer::AirSpread,
                    {&game::ValidateAirSpreadMoveIntent, &game::AttemptAirSpreadMoveIntent}) ||
                !g_ClickedMissionGameAdapter.BindIntentHandler(
                    commands::ClickedMissionProducer::TeslaCharge,
                    {&game::ValidateTeslaChargeIntent, &game::AttemptTeslaChargeIntent}) ||
                !g_ClickedMissionGameAdapter.BindIntentHandler(
                    commands::ClickedMissionProducer::AutoNanoCloud,
                    {&game::ValidateAutoNanoCloudIntent, &game::AttemptAutoNanoCloudIntent}))
            {
                g_LastError = "clicked mission intent handler binding failed";
                OutputDebugStringA(("[RACommandsPlugin] " + g_LastError + "\n").c_str());
                return false;
            }

            if (!game::InstallMainFrameHook(&OnGameFrame, g_LastError))
            {
                OutputDebugStringA(("[RACommandsPlugin] " + g_LastError + "\n").c_str());
                return false;
            }

            std::string hookError;
            g_AFloorHooksReady = game::InstallAFloorHooks(&IsAFloorModeEnabled, hookError);
            if (!g_AFloorHooksReady)
            {
                OutputDebugStringA(("[RACommandsPlugin] " + hookError + "\n").c_str());
            }
            else
            {
                game::SetManualVehicleOrderObserver(&OnManualVehicleOrder);
            }
            g_RangeDisplayHookReady = game::InstallRangeDisplayHook(
                &IsRangeDisplayModeEnabled, hookError);
            if (!g_RangeDisplayHookReady)
            {
                OutputDebugStringA(("[RACommandsPlugin] " + hookError + "\n").c_str());
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
        for (auto& command : g_Commands)
        {
            command.Disable();
            command.State = CommandState::WaitingForGame;
        }
        g_TeslaChargeCommandService.Reset();
        g_SafeModeToggleCommandService.Reset();
        g_AutoLoadCommandService.Reset();
        g_AutoNanoCloudCommandService.Reset();
        g_AutoCrushCommandService.Reset();
        g_AutoRepairCommandService.Reset();
        g_SelectionCommandService.Reset();
        g_AFloorCommandService.Reset();
        g_BeaconClearCommandService.Reset();
        g_BeaconClearGameAdapter.Reset();
        g_RangeDisplayCommandService.Reset();
        game::DisableRangeDisplayHook();
        g_RangeDisplayHookReady = false;
        game::DisableAFloorHooks();
        g_AFloorHooksReady = false;
        g_ClickedMissionDispatcher.Reset();
        g_NativeNetworkEventAdapter.Reset();
        g_HotkeysReloaded = false;
        g_GameThreadId = 0;
        g_LastError.clear();
        g_GameSymbols = {};
    }
}
