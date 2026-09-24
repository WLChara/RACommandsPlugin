#include "Hooks/RangeDisplayHook.h"

#include "Game/GameObjectAccess.h"
#include "Memory/ProcessMemory.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <ObjectClass.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>
#include <WeaponTypeClass.h>

#include <MinHook.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>

namespace ra_commands::game
{
    namespace
    {
        constexpr std::uintptr_t RADIAL_DISPATCH_ADDRESS = 0x006DBE20u;
        constexpr std::uintptr_t NATIVE_DRAW_RADIAL_ADDRESS = 0x00456980u;
        constexpr std::array<std::uint8_t, 16> RADIAL_DISPATCH_ENTRY = {
            0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x83, 0xEC,
            0x38, 0x53, 0x8B, 0x1D, 0xC8, 0xEC, 0xA8, 0x00
        };
        constexpr int MAX_SELECTED_OBJECTS = 100000;
        constexpr int MAX_DISPLAY_RANGE_CELLS = 512;
        constexpr std::int64_t LEPTONS_PER_CELL = 256;

        using RadialDispatcherFunction = void(__cdecl*)();
        using NativeDrawRadialFunction = void(__fastcall*)(bool drawLine,
            bool adjustColor, CoordStruct center, ColorStruct color,
            float radiusCells, bool radiusIsPixelBased,
            bool convertMultiplierToInteger);

        std::atomic<RadialDispatcherFunction> g_OriginalDispatcher{nullptr};
        std::atomic<RangeDisplayModeQuery> g_ModeQuery{nullptr};
        std::atomic<DWORD> g_AllowedGameThreadId{0};
        bool g_IsInstalled = false;
        bool g_InstallationFailed = false;

        bool IsSelectedUnit(TechnoClass* techno)
        {
            if (!techno || !techno->IsSelected || !techno->Owner ||
                techno->Owner != HouseClass::Player.get() ||
                !techno->IsAlive || !techno->IsOnMap || techno->InLimbo ||
                !techno->IsInPlayfield || techno->IsDead() || techno->Transporter)
            {
                return false;
            }
            const auto kind = techno->WhatAmI();
            return kind == AbstractType::Unit || kind == AbstractType::Infantry ||
                kind == AbstractType::Aircraft;
        }

        int GetCurrentWeaponRangeCells(TechnoClass* techno)
        {
            const auto* const type = techno->GetTechnoType();
            if (!type || type->WeaponCount <= 0 ||
                type->WeaponCount > TechnoTypeClass::MaxWeapons)
            {
                return 0;
            }
            const int index = techno->CurrentWeaponNumber >= 0 &&
                techno->CurrentWeaponNumber < type->WeaponCount
                ? techno->CurrentWeaponNumber : 0;
            auto* weapon = techno->GetWeapon(index);
            if ((!weapon || !weapon->WeaponType) && index != 0)
            {
                weapon = techno->GetWeapon(0);
            }
            if (!weapon || !weapon->WeaponType || weapon->WeaponType->Range <= 0)
            {
                return 0;
            }
            const auto range = static_cast<std::int64_t>(weapon->WeaponType->Range);
            // 与原生建筑射程圈一致：正数 Range 向上取整到 256 leptons 一格。
            const auto cells = (range + LEPTONS_PER_CELL - 1) / LEPTONS_PER_CELL;
            return static_cast<int>((std::min)(cells,
                static_cast<std::int64_t>(MAX_DISPLAY_RANGE_CELLS)));
        }

        void __cdecl HookRadialDispatcher()
        {
            const auto original = g_OriginalDispatcher.load(std::memory_order_acquire);
            if (original)
            {
                original();
            }

            const auto isEnabled = g_ModeQuery.load(std::memory_order_acquire);
            const auto allowedThread = g_AllowedGameThreadId.load(std::memory_order_acquire);
            if (!isEnabled || !allowedThread ||
                allowedThread != GetCurrentThreadId() || !isEnabled() ||
                !IsGameSessionReady())
            {
                return;
            }

            auto* const selected = &ObjectClass::CurrentObjects.get();
            if (!selected->IsInitialized || selected->Count < 0 ||
                selected->Count > MAX_SELECTED_OBJECTS ||
                selected->Count > selected->Capacity ||
                (selected->Count > 0 && !selected->Items))
            {
                return;
            }

            const auto draw = reinterpret_cast<NativeDrawRadialFunction>(
                NATIVE_DRAW_RADIAL_ADDRESS);
            // 渲染热路径避免复制选区，只读取当前数组，不修改其内容。
            for (int index = 0; index < selected->Count; ++index)
            {
                auto* const object = selected->Items[index];
                if (!object)
                {
                    continue;
                }
                const auto kind = object->WhatAmI();
                if (kind != AbstractType::Unit && kind != AbstractType::Infantry &&
                    kind != AbstractType::Aircraft)
                {
                    continue;
                }
                auto* const techno = static_cast<TechnoClass*>(object);
                if (!IsSelectedUnit(techno))
                {
                    continue;
                }
                const int radiusCells = GetCurrentWeaponRangeCells(techno);
                if (radiusCells <= 0)
                {
                    continue;
                }
                // 复用建筑普通射程圈的 native 参数，不建立独立绘制器。
                draw(true, true, techno->GetCoords(), techno->Owner->LaserColor,
                    static_cast<float>(radiusCells), false, true);
            }
        }
    }

    bool InstallRangeDisplayHook(
        RangeDisplayModeQuery isEnabled, std::string& outError)
    {
        if (!isEnabled)
        {
            outError = "range display callback is null";
            return false;
        }
        if (g_IsInstalled)
        {
            g_ModeQuery.store(isEnabled, std::memory_order_release);
            return true;
        }
        if (g_InstallationFailed)
        {
            outError = "range display hook failed earlier in this process";
            return false;
        }

        std::array<std::uint8_t, RADIAL_DISPATCH_ENTRY.size()> actual{};
        if (!memory::TryReadMemory(RADIAL_DISPATCH_ADDRESS,
                actual.data(), actual.size()) || actual != RADIAL_DISPATCH_ENTRY)
        {
            outError = "radial dispatcher differs from supported game build";
            return false;
        }

        const auto initialization = MH_Initialize();
        if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED)
        {
            outError = "MinHook initialization failed for range display";
            return false;
        }

        void* const target = reinterpret_cast<void*>(RADIAL_DISPATCH_ADDRESS);
        void* trampoline = nullptr;
        if (MH_CreateHook(target, &HookRadialDispatcher, &trampoline) != MH_OK ||
            !trampoline)
        {
            g_InstallationFailed = true;
            outError = "cannot create range display hook";
            return false;
        }
        g_OriginalDispatcher.store(
            reinterpret_cast<RadialDispatcherFunction>(trampoline), std::memory_order_release);
        if (MH_EnableHook(target) != MH_OK)
        {
            // 若底层部分启用了 detour，保留 trampoline 并维持纯透传。
            g_InstallationFailed = true;
            outError = "cannot activate range display hook";
            return false;
        }

        g_ModeQuery.store(isEnabled, std::memory_order_release);
        g_IsInstalled = true;
        outError.clear();
        return true;
    }

    void SetRangeDisplayGameThread(DWORD threadId) noexcept
    {
        g_AllowedGameThreadId.store(threadId, std::memory_order_release);
    }

    void DisableRangeDisplayHook() noexcept
    {
        g_ModeQuery.store(nullptr, std::memory_order_release);
        g_AllowedGameThreadId.store(0, std::memory_order_release);
    }
}
