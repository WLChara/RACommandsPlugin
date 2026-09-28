#include "Bootstrap/PluginRuntime.h"

#include "Commands/AutoLoadCommand/AutoLoadCommandService.h"
#include "Commands/AutoBuild/AutoBuildCommandRegistry.h"
#include "Commands/AutoBuild/AutoBuildCommandService.h"
#include "Commands/AutoBuild/AutoBuildGameAdapter.h"
#include "Commands/ForceShieldCommand/ForceShieldCommandRegistry.h"
#include "Commands/ForceShieldCommand/ForceShieldGameAdapter.h"
#include "Commands/AutoSuperWeapon/AutoSuperWeaponCommandRegistry.h"
#include "Commands/AutoSuperWeapon/AutoSuperWeaponCommandService.h"
#include "Commands/AutoSuperWeapon/AutoSuperWeaponGameAdapter.h"
#include "Commands/AutoNanoCloudCommand/AutoNanoCloudCommandRegistry.h"
#include "Commands/AutoNanoCloudCommand/AutoNanoCloudCommandService.h"
#include "Commands/AutoNanoCloudCommand/AutoNanoCloudGameAdapter.h"
#include "Commands/AutoCrush/AutoCrushCommandService.h"
#include "Commands/AutoCrush/AutoCrushGameAdapter.h"
#include "Commands/AutoCrush/AutoCrushIntentHandler.h"
#include "Commands/AutoCrushAddCommand/AutoCrushAddCommandRegistry.h"
#include "Commands/AutoCrushRemoveCommand/AutoCrushRemoveCommandRegistry.h"
#include "ClickedMission/ClickedMissionDispatcher.h"
#include "Commands/AutoLoadCommand/AutoLoadCommandRegistry.h"
#include "Commands/AutoLoadCommand/AutoLoadGameAdapter.h"
#include "Commands/SafeModeToggleCommand/SafeModeState.h"
#include "Commands/SafeModeToggleCommand/SafeModeToggleCommandRegistry.h"
#include "Commands/SafeModeToggleCommand/SafeModeToggleCommandService.h"
#include "Commands/AutoRepairCommand/AutoRepairCommandRegistry.h"
#include "Commands/AutoRepairCommand/AutoRepairCommandService.h"
#include "Commands/AutoRepairCommand/AutoRepairGameAdapter.h"
#include "Commands/AirSpreadCommand/AirSpreadCommandRegistry.h"
#include "Commands/AirSpreadCommand/AirSpreadCommandService.h"
#include "Commands/AirSpreadCommand/AirSpreadGameAdapter.h"
#include "Commands/AirSpreadCommand/AirSpreadIntentHandler.h"
#include "Commands/AutoFormationCommand/AutoFormationCommandRegistry.h"
#include "Commands/AutoFormationCommand/AutoFormationCommandService.h"
#include "Commands/AutoFormationCommand/AutoFormationGameAdapter.h"
#include "Commands/AutoFormationCommand/AutoFormationIntentHandler.h"
#include "Commands/AutoFormationCommand/AutoFormationDiagnostics.h"
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
#include "Game/GameObjectAccess.h"
#include "Game/SelectionGameAdapter.h"
#include "Game/SelectionAccess.h"
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
        game::AutoBuildGameAdapter g_AutoBuildGameAdapter(g_NativeNetworkEventAdapter);
        game::ForceShieldGameAdapter g_ForceShieldGameAdapter(g_NativeNetworkEventAdapter);
        game::AutoSuperWeaponGameAdapter g_AutoSuperWeaponGameAdapter(g_NativeNetworkEventAdapter);
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
        game::AirSpreadGameAdapter g_AirSpreadGameAdapter(g_ClickedMissionDispatcher);
        game::AutoFormationGameAdapter g_AutoFormationGameAdapter(g_ClickedMissionDispatcher);
        auto_formation::AutoFormationCommandService g_AutoFormationCommandService(g_AutoFormationGameAdapter);
        game::AutoCrushGameAdapter g_AutoCrushGameAdapter(g_ClickedMissionDispatcher);
        auto_crush::AutoCrushCommandService g_AutoCrushCommandService(g_AutoCrushGameAdapter);
        safe_mode::SafeModeToggleCommandService g_SafeModeToggleCommandService(
            safe_mode::g_IsSafeModeEnabled);
        autoload::AutoLoadCommandService g_AutoLoadCommandService(
            g_AutoLoadGameAdapter, g_ClickedMissionDispatcher,
            safe_mode::g_IsSafeModeEnabled);
        auto_repair::AutoRepairCommandService g_AutoRepairCommandService(
            g_AutoRepairGameAdapter, safe_mode::g_IsSafeModeEnabled);
        auto_build::AutoBuildCommandService g_AutoBuildCommandService(g_AutoBuildGameAdapter);
        auto_super_weapon::AutoSuperWeaponCommandService g_AutoSuperWeaponCommandService(
            g_AutoSuperWeaponGameAdapter);
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
        bool g_FormationOrderHookReady = false;
        bool g_FormationTraceWasActive = false;
        std::uint32_t g_LastFormationTraceFrame = 0;
        DWORD g_GameThreadId = 0;

        void CancelSelectedFormationActors()
        {
            if (!g_AutoFormationCommandService.IsActive())
            {
                return;
            }
            // 插件热键不会进入人工原生观察器，故在新的载具任务用例入口主动让出控制。
            for (const auto actor : game::CaptureSelectedUnitIds())
            {
                g_AutoFormationCommandService.OnManualOrder(actor);
            }
        }

        void OnAutoLoadHotkey()
        {
            // 外部 Shutdown 可并发执行；服务状态在热键回调期间保持不变。
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                CancelSelectedFormationActors();
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
                CancelSelectedFormationActors();
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

        void OnMainAutoBuildHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                (void)g_AutoBuildCommandService.OnHotkey(auto_build::BuildSlot::Main);
            }
        }

        void OnDefenseAutoBuildHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                (void)g_AutoBuildCommandService.OnHotkey(auto_build::BuildSlot::Defense);
            }
        }

        void OnFormationVehicleOrder(std::uint64_t actorId)
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_FormationOrderHookReady &&
                g_GameThreadId == GetCurrentThreadId())
            {
                g_AutoFormationCommandService.OnManualOrder(actorId);
            }
        }

        void TraceFormationProgress(std::uint32_t frame)
        {
            const auto progress = g_AutoFormationCommandService.Progress();
            // 只复用服务的值快照；60 帧一条，避免为诊断增加原生对象扫描和文件写入。
            if ((progress.mActive && (!g_FormationTraceWasActive ||
                    frame < g_LastFormationTraceFrame || frame - g_LastFormationTraceFrame >= 60)) ||
                (!progress.mActive && g_FormationTraceWasActive))
            {
                game::TraceAutoFormation("frame=" + std::to_string(frame) +
                    ", active=" + std::to_string(progress.mActive) +
                    ", total=" + std::to_string(progress.mTotalActors) +
                    ", arrived=" + std::to_string(progress.mArrived) +
                    ", queued=" + std::to_string(progress.mQueued) +
                    ", moving=" + std::to_string(progress.mMoving) +
                    ", waiting=" + std::to_string(progress.mWaiting) +
                    ", removed=" + std::to_string(progress.mRemoved) +
                    ", accepted=" + std::to_string(progress.mSubmitted) +
                    ", rejected=" + std::to_string(progress.mRejectedSubmissions) +
                    ", timedOut=" + std::to_string(progress.mTimedOut));
                g_LastFormationTraceFrame = frame;
            }
            g_FormationTraceWasActive = progress.mActive;
        }

        void OnAutoFormationHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (!g_IsInitialized || !g_FormationOrderHookReady ||
                g_GameThreadId != GetCurrentThreadId())
            {
                game::TraceAutoFormation("hotkey rejected by runtime/thread gate");
                return;
            }
            const auto plan = g_AutoFormationCommandService.OnHotkey();
            game::TraceAutoFormation("build=" __DATE__ " " __TIME__ "; hotkey: assigned=" +
                std::to_string(plan.mAssignments.size()) + ", unassigned=" +
                std::to_string(plan.mUnassignedActors.size()) + ", active=" +
                std::to_string(g_AutoFormationCommandService.IsActive()) +
                ", center=" + std::to_string(plan.mCenter.mX) + "," +
                std::to_string(plan.mCenter.mY));
            std::string targets = "fixed targets:";
            for (const auto& assignment : plan.mAssignments)
            {
                targets += " " + std::to_string(static_cast<std::uint32_t>(assignment.mActor)) +
                    "->(" + std::to_string(assignment.mDestination.mX) + "," +
                    std::to_string(assignment.mDestination.mY) + "," +
                    std::to_string(assignment.mDestination.mOnBridge) + ")";
            }
            game::TraceAutoFormation(targets);
            g_FormationTraceWasActive = g_AutoFormationCommandService.IsActive();
            g_LastFormationTraceFrame = game::GetCurrentGameFrame();
            for (const auto& assignment : plan.mAssignments)
            {
                // 列队是玩家主动的新移动任务；持续碾压不得继续给这些载具重下 Move。
                g_AutoCrushCommandService.OnManualOrder(assignment.mActor);
            }
            if (plan.mBudgetExceeded || !plan.mUnassignedActors.empty())
            {
                OutputDebugStringA("[RACommandsPlugin] formation plan incomplete or over budget\n");
            }
        }

        void OnForceShieldHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                (void)g_ForceShieldGameAdapter.TryFireAtCursor();
            }
        }

        void OnAutoIronCurtainHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                (void)g_AutoSuperWeaponCommandService.OnHotkey(
                    auto_super_weapon::Kind::IronCurtain);
            }
        }

        void OnAutoRageInductorHotkey()
        {
            std::lock_guard lock(g_StateMutex);
            if (g_IsInitialized && g_GameThreadId == GetCurrentThreadId())
            {
                (void)g_AutoSuperWeaponCommandService.OnHotkey(
                    auto_super_weapon::Kind::RageInductor);
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

        game::CommandRegistrationResult RegisterAutoFormationWhenHookReady(
            const game::GameSymbols& symbols, void(*callback)(), std::string& outError)
        {
            if (!g_FormationOrderHookReady)
            {
                outError = "formation order cancellation hook is unavailable";
                return game::CommandRegistrationResult::Failed;
            }
            return game::TryRegisterAutoFormationCommand(symbols, callback, outError);
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
        auto g_Commands = std::to_array<CommandEntry>({
            {&RegisterConfiguredCommand<&game::TryRegisterSafeModeToggleCommand, &OnSafeModeToggleHotkey>, &game::DisableSafeModeToggleCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAutoLoadCommand, &OnAutoLoadHotkey>, &game::DisableAutoLoadCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAutoNanoCloudCommand, &OnAutoNanoCloudHotkey>, &game::DisableAutoNanoCloudCommand},
            {&RegisterConfiguredCommand<&RegisterAutoCrushAddWhenHookReady, &OnAutoCrushAddHotkey>, &game::DisableAutoCrushAddCommand},
            {&RegisterConfiguredCommand<&RegisterAutoCrushRemoveWhenHookReady, &OnAutoCrushRemoveHotkey>, &game::DisableAutoCrushRemoveCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterTeslaChargeCommand, &OnTeslaChargeHotkey>, &game::DisableTeslaChargeCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAutoRepairCommand, &OnAutoRepairHotkey>, &game::DisableAutoRepairCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterMainAutoBuildCommand, &OnMainAutoBuildHotkey>, &game::DisableMainAutoBuildCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterDefenseAutoBuildCommand, &OnDefenseAutoBuildHotkey>, &game::DisableDefenseAutoBuildCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterForceShieldCommand, &OnForceShieldHotkey>, &game::DisableForceShieldCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAutoIronCurtainCommand, &OnAutoIronCurtainHotkey>, &game::DisableAutoIronCurtainCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAutoRageInductorCommand, &OnAutoRageInductorHotkey>, &game::DisableAutoRageInductorCommand},
            {&RegisterConfiguredCommand<&game::TryRegisterAirSpreadCommand, &OnAirSpreadHotkey>, &game::DisableAirSpreadCommand},
            {&RegisterConfiguredCommand<&RegisterAutoFormationWhenHookReady, &OnAutoFormationHotkey>, &game::DisableAutoFormationCommand},
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
        });

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
            g_AutoFormationCommandService.OnGameFrame();
            TraceFormationProgress(game::GetCurrentGameFrame());
            g_SafeModeToggleCommandService.OnGameFrame(
                g_ClickedMissionDispatcher.IsSessionActive(), g_ClickedMissionDispatcher.Epoch());
            g_TeslaChargeCommandService.OnGameFrame();
            g_AutoRepairCommandService.OnGameFrame();
            g_AutoBuildCommandService.OnGameFrame();
            g_AutoSuperWeaponCommandService.OnGameFrame();
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

            // 处理器必须在主帧回调和原生命令启动前全部绑定。
            game::BindAutoFormationGameAdapter(&g_AutoFormationGameAdapter);
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
                    {&game::ValidateAutoNanoCloudIntent, &game::AttemptAutoNanoCloudIntent}) ||
                !g_ClickedMissionGameAdapter.BindIntentHandler(
                    commands::ClickedMissionProducer::AutoFormation,
                    {&game::ValidateAutoFormationIntent, &game::AttemptAutoFormationIntent}))
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
            g_FormationOrderHookReady = g_AFloorHooksReady &&
                game::InstallFormationOrderHook(&OnFormationVehicleOrder, hookError);
            if (!g_FormationOrderHookReady)
            {
                OutputDebugStringA(("[RACommandsPlugin] formation: " + hookError + "\n").c_str());
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
        g_AutoFormationCommandService.Reset();
        g_AutoRepairCommandService.Reset();
        g_AutoBuildCommandService.Reset();
        g_AutoSuperWeaponCommandService.Reset();
        g_ForceShieldGameAdapter.Reset();
        g_SelectionCommandService.Reset();
        g_AFloorCommandService.Reset();
        g_BeaconClearCommandService.Reset();
        g_BeaconClearGameAdapter.Reset();
        g_RangeDisplayCommandService.Reset();
        game::DisableRangeDisplayHook();
        g_RangeDisplayHookReady = false;
        game::DisableAFloorHooks();
        g_AFloorHooksReady = false;
        g_FormationOrderHookReady = false;
        g_FormationTraceWasActive = false;
        g_LastFormationTraceFrame = 0;
        g_ClickedMissionDispatcher.Reset();
        g_NativeNetworkEventAdapter.Reset();
        g_HotkeysReloaded = false;
        g_GameThreadId = 0;
        g_LastError.clear();
        g_GameSymbols = {};
    }
}
