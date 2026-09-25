#include "Hooks/AFloorHook.h"

#include "Game/GameObjectAccess.h"
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
        constexpr std::array<std::uint8_t, 16> LEFT_MOUSE_UP_ENTRY = {
            0x83, 0xEC, 0x7C, 0x53, 0x55, 0x56, 0x8B, 0xB4,
            0x24, 0x94, 0x00, 0x00, 0x00, 0x85, 0xF6, 0x57
        };
        constexpr std::array<std::uint8_t, 16> CLICKED_MISSION_ENTRY = {
            0x81, 0xEC, 0x88, 0x00, 0x00, 0x00, 0x53, 0x55,
            0x56, 0x57, 0x8B, 0xF1, 0xE8, 0xFF, 0x1F, 0x03
        };

        using LeftMouseUpFunction = void(__thiscall*)(DisplayClass*,
            const CoordStruct&, const CellStruct&, ObjectClass*, Action, DWORD);
        using ClickedMissionFunction = char(__thiscall*)(TechnoClass*, Mission,
            AbstractClass*, AbstractClass*, CellClass*);

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
        std::atomic<DWORD> g_AllowedGameThreadId{0};
        bool g_AreHooksInstalled = false;
        bool g_InstallationFailed = false;

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
        g_AllowedGameThreadId.store(0, std::memory_order_release);
    }

    void SetManualVehicleOrderObserver(ManualVehicleOrderObserver observer) noexcept
    {
        g_ManualOrderObserver.store(observer, std::memory_order_release);
    }

    void SetAFloorGameThread(DWORD threadId) noexcept
    {
        g_AllowedGameThreadId.store(threadId, std::memory_order_release);
    }
}
