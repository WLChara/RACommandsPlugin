#include "Hooks/AlertsUiHook.h"
#include "Game/TargetProfiles/AlertsUiProfile.h"
#include "Memory/ProcessMemory.h"

#include <Windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>

namespace ra_commands::game
{
    namespace
    {
        namespace profile = alerts_ui::profile;
        struct NativeCoord { std::int32_t mX; std::int32_t mY; std::int32_t mZ; };
        struct NativeCell { std::int16_t mX; std::int16_t mY; };
        static_assert(sizeof(NativeCoord) == 12 && sizeof(NativeCell) == 4);
        // 0x430BA0: ECX=this，owner/CoordStruct/slot 共20栈字节，RET 0x14。
        using PlaceBeaconFunction = void(__thiscall*)(void*, int, NativeCoord, int);
        // 0x65FA70: ECX=type，EDX未使用，CellStruct 占4栈字节，RET 4，AL=bool。
        using CreateRadarFunction = bool(__fastcall*)(int, void*, NativeCell);
        std::atomic<PlaceBeaconFunction> g_OriginalPlace{nullptr};
        std::atomic<CreateRadarFunction> g_OriginalRadar{nullptr};
        std::atomic<BeaconPlacedCallback> g_BeaconCallback{nullptr};
        std::atomic<RadarAcceptedCallback> g_RadarCallback{nullptr};
        std::atomic<std::uint32_t> g_GameThread{0};
        bool g_IsBeaconInstalled = false;
        bool g_IsRadarInstalled = false;
        bool g_HasBeaconFailed = false;
        bool g_HasRadarFailed = false;

        bool IsGameThread() noexcept
        {
            const auto id = g_GameThread.load(std::memory_order_acquire);
            return id && id == GetCurrentThreadId();
        }

        void __fastcall HookPlaceBeacon(void* manager, void*, int owner,
            NativeCoord position, int slot)
        {
            const auto original = g_OriginalPlace.load(std::memory_order_acquire);
            if (!original) { return; }
            const auto callback = g_BeaconCallback.load(std::memory_order_acquire);
            const bool canCapture = callback && IsGameThread() &&
                reinterpret_cast<std::uintptr_t>(manager) == profile::BEACON_MANAGER &&
                owner >= 0 && owner < 8;
            int actualSlot = slot;
            if (canCapture && slot == -1)
            {
                std::array<std::uintptr_t, 3> before{};
                if (memory::TryReadMemory(profile::BEACON_MANAGER + owner * sizeof(before),
                        before.data(), sizeof(before)))
                {
                    for (int index = 0; index < 3; ++index)
                    {
                        if (!before[index]) { actualSlot = index; break; }
                    }
                }
            }
            original(manager, owner, position, slot);
            // 显式替换可能复用分配器地址；成功落槽后每次放置都产生新代次。
            if (canCapture && actualSlot >= 0 && actualSlot < 3)
            {
                try { callback(owner, actualSlot); }
                catch (...) { OutputDebugStringA("[RACommandsPlugin] beacon capture failed\n"); }
            }
        }

        bool __fastcall HookCreateRadar(int type, void* unused, NativeCell cell)
        {
            const auto original = g_OriginalRadar.load(std::memory_order_acquire);
            if (!original) { return false; }
            const bool accepted = original(type, unused, cell);
            const auto callback = g_RadarCallback.load(std::memory_order_acquire);
            if (accepted && callback && IsGameThread())
            {
                try { callback(type, cell.mX, cell.mY); }
                catch (...) { OutputDebugStringA("[RACommandsPlugin] radar capture failed\n"); }
            }
            return accepted;
        }

        template<std::size_t TSize>
        bool MatchesEntry(std::uintptr_t address, const std::array<std::uint8_t, TSize>& expected)
        {
            std::array<std::uint8_t, TSize> actual{};
            return memory::TryReadMemory(address, actual.data(), actual.size()) && actual == expected;
        }

        bool PrepareMinHook(std::string& outError)
        {
            const auto status = MH_Initialize();
            if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED) { return true; }
            outError = "MinHook initialization failed for alerts UI";
            return false;
        }
    }

    bool InstallBeaconCaptureHook(BeaconPlacedCallback callback, std::string& outError)
    {
        outError.clear();
        if (!callback) { outError = "beacon callback is null"; return false; }
        if (g_IsBeaconInstalled) { g_BeaconCallback.store(callback); return true; }
        if (g_HasBeaconFailed) { outError = "beacon hook failed earlier"; return false; }
        if (!MatchesEntry(profile::PLACE_BEACON, profile::PLACE_BEACON_ENTRY))
        {
            outError = "beacon placement entry differs from supported build";
            return false;
        }
        if (!PrepareMinHook(outError)) { return false; }
        auto* target = reinterpret_cast<void*>(profile::PLACE_BEACON);
        void* trampoline = nullptr;
        if (MH_CreateHook(target, &HookPlaceBeacon, &trampoline) != MH_OK || !trampoline)
        {
            g_HasBeaconFailed = true;
            outError = "cannot create beacon capture hook";
            return false;
        }
        g_OriginalPlace.store(reinterpret_cast<PlaceBeaconFunction>(trampoline), std::memory_order_release);
        if (MH_EnableHook(target) != MH_OK)
        {
            // 保留 trampoline；部分启用时也只透传，不撤销其他功能的 Hook。
            g_HasBeaconFailed = true;
            outError = "cannot enable beacon capture hook";
            return false;
        }
        g_BeaconCallback.store(callback, std::memory_order_release);
        g_IsBeaconInstalled = true;
        return true;
    }

    bool InstallRadarCaptureHook(RadarAcceptedCallback callback, std::string& outError)
    {
        outError.clear();
        if (!callback) { outError = "radar callback is null"; return false; }
        if (g_IsRadarInstalled) { g_RadarCallback.store(callback); return true; }
        if (g_HasRadarFailed) { outError = "radar hook failed earlier"; return false; }
        if (!MatchesEntry(profile::CREATE_RADAR_EVENT, profile::CREATE_RADAR_EVENT_ENTRY))
        {
            outError = "radar Create entry differs from supported build";
            return false;
        }
        if (!PrepareMinHook(outError)) { return false; }
        auto* target = reinterpret_cast<void*>(profile::CREATE_RADAR_EVENT);
        void* trampoline = nullptr;
        if (MH_CreateHook(target, &HookCreateRadar, &trampoline) != MH_OK || !trampoline)
        {
            g_HasRadarFailed = true;
            outError = "cannot create radar capture hook";
            return false;
        }
        g_OriginalRadar.store(reinterpret_cast<CreateRadarFunction>(trampoline), std::memory_order_release);
        if (MH_EnableHook(target) != MH_OK)
        {
            g_HasRadarFailed = true;
            outError = "cannot enable radar capture hook";
            return false;
        }
        g_RadarCallback.store(callback, std::memory_order_release);
        g_IsRadarInstalled = true;
        return true;
    }

    void SetAlertsUiGameThread(std::uint32_t threadId) noexcept
    {
        g_GameThread.store(threadId, std::memory_order_release);
    }

    void DisableAlertsUiHooks() noexcept
    {
        g_BeaconCallback.store(nullptr, std::memory_order_release);
        g_RadarCallback.store(nullptr, std::memory_order_release);
        g_GameThread.store(0, std::memory_order_release);
    }
}
