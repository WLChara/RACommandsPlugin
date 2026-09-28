#include "Hooks/AFloorHook.h"

#include "Game/GameObjectAccess.h"
#include "Game/PluginOrderScope.h"
#include "Memory/ProcessMemory.h"

#include <YRPPCore.h>
#include <DisplayClass.h>
#include <HouseClass.h>
#include <InputManagerClass.h>
#include <MapClass.h>
#include <ObjectClass.h>
#include <TechnoClass.h>

#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace ra_commands::game
{
    namespace
    {
        constexpr std::uintptr_t LEFT_MOUSE_UP_ADDRESS = 0x004AB9B0u;
        constexpr std::uintptr_t CLICKED_MISSION_ADDRESS = 0x006FFBE0u;
        // 目标样本 Unit vtable +0x374；Stop 热键经 ClickedEvent(Idle) 发出。
        constexpr std::uintptr_t CLICKED_EVENT_ADDRESS = 0x006FFE00u;
        constexpr std::array<std::uint8_t, 16> LEFT_MOUSE_UP_ENTRY = {
            0x83, 0xEC, 0x7C, 0x53, 0x55, 0x56, 0x8B, 0xB4,
            0x24, 0x94, 0x00, 0x00, 0x00, 0x85, 0xF6, 0x57
        };
        constexpr std::array<std::uint8_t, 16> CLICKED_MISSION_ENTRY = {
            0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x53, 0x55,
            0x56, 0x57, 0x8B, 0xF1, 0xE8, 0xFF, 0x1F, 0x03
        };
        constexpr std::array<std::uint8_t, 16> CLICKED_EVENT_ENTRY = {
            0x83, 0xEC, 0x78, 0x56, 0x57, 0x51, 0x8D, 0x4C,
            0x24, 0x0C, 0xE8, 0xA1, 0x6C, 0xFE, 0xFF, 0x8B
        };

        using LeftMouseUpFunction = void(__thiscall*)(DisplayClass*,
            const CoordStruct&, const CellStruct&, ObjectClass*, Action, DWORD);
        using ClickedMissionFunction = char(__thiscall*)(TechnoClass*, Mission,
            AbstractClass*, AbstractClass*, CellClass*);
        using ClickedEventFunction = bool(__thiscall*)(TechnoClass*, NetworkEvents);

        struct ClickContext
        {
            ObjectClass* OriginalTarget = nullptr;
            CellClass* GroundCell = nullptr;
            bool IsManualOrder = false;
        };

        thread_local const ClickContext* g_ActiveClick = nullptr;
        std::atomic<LeftMouseUpFunction> g_OriginalLeftMouseUp{nullptr};
        std::atomic<ClickedMissionFunction> g_OriginalClickedMission{nullptr};
        std::atomic<AFloorModeQuery> g_ModeQuery{nullptr};
        std::atomic<ManualVehicleOrderObserver> g_ManualOrderObserver{nullptr};
        std::atomic<ManualVehicleOrderObserver> g_FormationOrderObserver{nullptr};
        std::atomic<ClickedEventFunction> g_OriginalClickedEvent{nullptr};
        std::atomic<DWORD> g_AllowedGameThreadId{0};
        bool g_AreHooksInstalled = false;
        bool g_InstallationFailed = false;
        bool g_IsFormationOrderHookInstalled = false;
        bool g_FormationOrderHookFailed = false;

        class ClickScope final
        {
        public:
            explicit ClickScope(const ClickContext& context) noexcept
                : mPrevious(g_ActiveClick)
            {
                g_ActiveClick = &context;
            }

            ~ClickScope()
            {
                g_ActiveClick = mPrevious;
            }

        private:
            const ClickContext* mPrevious;
        };

        bool IsEntryUnmodified(std::uintptr_t address,
            const std::array<std::uint8_t, 16>& expected)
        {
            std::array<std::uint8_t, 16> actual{};
            return memory::TryReadMemory(address, actual.data(), actual.size()) &&
                actual == expected;
        }

        void NotifyFormationOrder(TechnoClass* actor)
        {
            const auto observer = g_FormationOrderObserver.load(std::memory_order_acquire);
            const auto allowedThread = g_AllowedGameThreadId.load(std::memory_order_acquire);
            if (!observer || !allowedThread || allowedThread != GetCurrentThreadId() ||
                IsPluginOrderActive() || !IsGameSessionReady() || !actor ||
                actor->WhatAmI() != AbstractType::Unit || !actor->UniqueID ||
                actor->Owner != HouseClass::Player.get())
            {
                return;
            }
            const auto id =
                (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(actor)) << 32) |
                actor->UniqueID;
            try
            {
                observer(id);
            }
            catch (...)
            {
                OutputDebugStringA("[RACommandsPlugin] formation order observer failed\n");
            }
        }

        bool __fastcall HookClickedEvent(TechnoClass* actor, void*, NetworkEvents event)
        {
            const auto original = g_OriginalClickedEvent.load(std::memory_order_acquire);
            if (!original)
            {
                return false;
            }
            // 在事件入队前撤销列队意图，防止队列拥塞时旧 Move 延迟覆盖 Stop 等新命令。
            NotifyFormationOrder(actor);
            return original(actor, event);
        }

        void __fastcall HookLeftMouseButtonUp(DisplayClass* display, void*,
            const CoordStruct& coords, const CellStruct& cell,
            ObjectClass* target, Action action, DWORD argument)
        {
            const auto original = g_OriginalLeftMouseUp.load(std::memory_order_acquire);
            if (!original)
            {
                return;
            }

            const bool isManualOrder = action == Action::Move ||
                action == Action::Attack || action == Action::AttackMoveNav ||
                action == Action::AttackMoveTar;
            const auto allowedThread = g_AllowedGameThreadId.load(std::memory_order_acquire);
            if (!isManualOrder || !allowedThread ||
                allowedThread != GetCurrentThreadId() ||
                !IsGameSessionReady() || !display)
            {
                original(display, coords, cell, target, action, argument);
                return;
            }

            ClickContext context{nullptr, nullptr, isManualOrder};
            const auto isEnabled = g_ModeQuery.load(std::memory_order_acquire);
            auto* const input = InputManagerClass::Instance.get();
            auto* const map = MapClass::Instance.get();
            if (isEnabled && isEnabled() && !display->PlanningMode &&
                target && action == Action::Attack && input &&
                input->IsForceFireKeyPressed() &&
                !input->IsForceSelectKeyPressed() && map &&
                map->CoordinatesLegal(cell) &&
                map->IsWithinUsableArea(cell, false))
            {
                auto* const groundCell = map->TryGetCellAt(cell);
                if (groundCell && groundCell->MapCoords == cell)
                {
                    context.OriginalTarget = target;
                    context.GroundCell = groundCell;
                }
            }

            // 本地手动命令与 A 地板目标替换均局限于此次左键调用栈。
            const ClickScope scope(context);
            original(display, coords, cell, target, action, argument);
        }

        char __fastcall HookClickedMission(TechnoClass* actor, void*,
            Mission mission, AbstractClass* target,
            AbstractClass* enterTarget, CellClass* nearestCell)
        {
            const auto original = g_OriginalClickedMission.load(std::memory_order_acquire);
            if (!original)
            {
                return 0;
            }

            // 与左键专属的自动碾压观察分开；键盘和右键原生调用也能取消列队。
            NotifyFormationOrder(actor);

            const auto* const context = g_ActiveClick;
            const auto isEnabled = g_ModeQuery.load(std::memory_order_acquire);
            if (context && context->IsManualOrder && actor &&
                actor->WhatAmI() == AbstractType::Unit && actor->UniqueID != 0 &&
                actor->Owner == HouseClass::Player.get() &&
                (mission == Mission::Move || mission == Mission::Attack ||
                 mission == Mission::AttackMove))
            {
                const auto observer = g_ManualOrderObserver.load(std::memory_order_acquire);
                if (observer)
                {
                    static_assert(sizeof(void*) == sizeof(std::uint32_t));
                    const auto id =
                        (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(actor)) << 32) |
                        actor->UniqueID;
                    try
                    {
                        observer(id);
                    }
                    catch (...)
                    {
                        OutputDebugStringA("[RACommandsPlugin] manual order observer failed\n");
                    }
                }
            }
            if (context && context->GroundCell && isEnabled && isEnabled() &&
                mission == Mission::Attack && actor &&
                actor->Owner == HouseClass::Player.get() &&
                target == static_cast<AbstractClass*>(context->OriginalTarget))
            {
                target = static_cast<AbstractClass*>(context->GroundCell);
            }
            return original(actor, mission, target, enterTarget, nearestCell);
        }
    }

    bool InstallAFloorHooks(AFloorModeQuery isEnabled, std::string& outError)
    {
        if (!isEnabled)
        {
            outError = "A-floor mode callback is null";
            return false;
        }
        if (g_AreHooksInstalled)
        {
            g_ModeQuery.store(isEnabled, std::memory_order_release);
            return true;
        }
        if (g_InstallationFailed)
        {
            outError = "A-floor hooks failed earlier in this process";
            return false;
        }
        if (!IsEntryUnmodified(LEFT_MOUSE_UP_ADDRESS, LEFT_MOUSE_UP_ENTRY) ||
            !IsEntryUnmodified(CLICKED_MISSION_ADDRESS, CLICKED_MISSION_ENTRY))
        {
            outError = "A-floor input or mission entry differs from supported game build";
            return false;
        }

        const auto initialization = MH_Initialize();
        if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED)
        {
            outError = "MinHook initialization failed for A-floor mode";
            return false;
        }

        void* const leftTarget = reinterpret_cast<void*>(LEFT_MOUSE_UP_ADDRESS);
        void* const missionTarget = reinterpret_cast<void*>(CLICKED_MISSION_ADDRESS);
        void* leftTrampoline = nullptr;
        void* missionTrampoline = nullptr;
        if (MH_CreateHook(leftTarget, &HookLeftMouseButtonUp, &leftTrampoline) != MH_OK ||
            !leftTrampoline)
        {
            g_InstallationFailed = true;
            outError = "cannot create A-floor left-click hook";
            return false;
        }
        g_OriginalLeftMouseUp.store(
            reinterpret_cast<LeftMouseUpFunction>(leftTrampoline), std::memory_order_release);
        if (MH_CreateHook(missionTarget, &HookClickedMission, &missionTrampoline) != MH_OK ||
            !missionTrampoline)
        {
            g_InstallationFailed = true;
            outError = "cannot create A-floor ClickedMission hook";
            return false;
        }

        g_OriginalClickedMission.store(
            reinterpret_cast<ClickedMissionFunction>(missionTrampoline), std::memory_order_release);

        if (MH_EnableHook(leftTarget) != MH_OK ||
            MH_EnableHook(missionTarget) != MH_OK)
        {
            // 可能已有线程进入 detour；保留 trampoline 至进程退出，且不开放模式。
            g_InstallationFailed = true;
            outError = "cannot activate A-floor hooks";
            return false;
        }

        g_ModeQuery.store(isEnabled, std::memory_order_release);
        g_AreHooksInstalled = true;
        outError.clear();
        return true;
    }

    void DisableAFloorHooks() noexcept
    {
        g_ModeQuery.store(nullptr, std::memory_order_release);
        g_ManualOrderObserver.store(nullptr, std::memory_order_release);
        g_FormationOrderObserver.store(nullptr, std::memory_order_release);
        g_AllowedGameThreadId.store(0, std::memory_order_release);
    }

    void SetManualVehicleOrderObserver(ManualVehicleOrderObserver observer) noexcept
    {
        g_ManualOrderObserver.store(observer, std::memory_order_release);
    }

    bool InstallFormationOrderHook(
        ManualVehicleOrderObserver observer, std::string& outError)
    {
        if (!observer || !g_AreHooksInstalled)
        {
            outError = "formation order hook requires the native mission hooks";
            return false;
        }
        if (g_IsFormationOrderHookInstalled)
        {
            g_FormationOrderObserver.store(observer, std::memory_order_release);
            outError.clear();
            return true;
        }
        if (g_FormationOrderHookFailed ||
            !IsEntryUnmodified(CLICKED_EVENT_ADDRESS, CLICKED_EVENT_ENTRY))
        {
            outError = "formation event hook is unavailable or its entry differs";
            return false;
        }
        void* const target = reinterpret_cast<void*>(CLICKED_EVENT_ADDRESS);
        void* trampoline = nullptr;
        if (MH_CreateHook(target, &HookClickedEvent, &trampoline) != MH_OK || !trampoline)
        {
            g_FormationOrderHookFailed = true;
            outError = "cannot create formation ClickedEvent hook";
            return false;
        }
        g_OriginalClickedEvent.store(
            reinterpret_cast<ClickedEventFunction>(trampoline), std::memory_order_release);
        if (MH_EnableHook(target) != MH_OK)
        {
            g_FormationOrderHookFailed = true;
            outError = "cannot enable formation ClickedEvent hook";
            return false;
        }
        g_IsFormationOrderHookInstalled = true;
        g_FormationOrderObserver.store(observer, std::memory_order_release);
        outError.clear();
        return true;
    }

    void SetAFloorGameThread(DWORD threadId) noexcept
    {
        g_AllowedGameThreadId.store(threadId, std::memory_order_release);
    }
}
