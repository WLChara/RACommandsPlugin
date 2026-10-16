#include "Game/GameAlertsUi.h"

#include "Game/GameObjectAccess.h"
#include "Game/TargetProfiles/AlertsUiProfile.h"
#include "Game/TargetVersion.h"
#include "Hooks/AlertsUiHook.h"
#include "Memory/ProcessMemory.h"
#include "UI/BeaconIndicator/BeaconNoticeModel.h"
#include "UI/BeaconIndicator/BeaconReconcileSchedule.h"
#include "UI/RadarFeed/RadarFeedModel.h"

#include <YRPPCore.h>
#include <CellClass.h>
#include <DisplayClass.h>
#include <HouseClass.h>
#include <MapClass.h>
#include <TacticalClass.h>
#include <Windows.h>

#include <array>
#include <atomic>

namespace ra_commands::game
{
    namespace
    {
        namespace profile = alerts_ui::profile;
        static_assert(sizeof(ui::HudWorldPoint) == sizeof(CoordStruct));
        static_assert(offsetof(CellClass, Level) == 0x11Bu);
        static_assert(offsetof(DisplayClass, CurrentBuildingType) == 0x11A8u);
        ui::BeaconNoticeModel g_Beacons;
        ui::BeaconReconcileSchedule g_BeaconReconcileSchedule;
        ui::RadarFeedModel g_Radar;
        std::atomic<bool> g_IsBeaconAvailable{false};
        std::atomic<bool> g_IsRadarAvailable{false};
        std::atomic<bool> g_IsBeaconEnabled{true};
        std::atomic<bool> g_IsRadarEnabled{true};
        std::atomic<bool> g_IsActive{false};
        std::atomic<bool> g_MustResetBeacons{false};
        std::atomic<bool> g_MustResetRadar{false};
        // 模型、对局身份与 SDK 访问仅由 Update 所在的游戏线程拥有。
        std::uint32_t g_GameThread = 0;
        std::uintptr_t g_SessionIdentity = 0;
        HouseClass* g_LocalPlayer = nullptr;
        std::uint32_t g_LastFrame = 0;

        bool IsCurrentSession()
        {
            return g_IsActive.load(std::memory_order_acquire) &&
                g_GameThread == GetCurrentThreadId() && IsGameSessionReady() &&
                HouseClass::Player.get() == g_LocalPlayer &&
                GetCurrentGameFrame() >= g_LastFrame;
        }

        bool IsValidCell(ui::HudMapCell cell)
        {
            auto* const map = MapClass::Instance.get();
            const CellStruct nativeCell{cell.mX, cell.mY};
            // SDK 的内联 JMP 会改写 EBP；用已核对的 thiscall/RET 4、RET 8 保持栈帧。
            using CoordinatesLegalFunction = bool(__thiscall*)(const MapClass*, const CellStruct*);
            using IsWithinUsableAreaFunction = bool(__thiscall*)(const MapClass*, const CellStruct*, bool);
            const auto coordinatesLegal = reinterpret_cast<CoordinatesLegalFunction>(profile::COORDINATES_LEGAL);
            const auto isWithinUsableArea = reinterpret_cast<IsWithinUsableAreaFunction>(profile::IS_WITHIN_USABLE_AREA);
            if (!map || !coordinatesLegal(map, &nativeCell) || !isWithinUsableArea(map, &nativeCell, false))
            {
                return false;
            }
            const auto* const found = map->TryGetCellAt(nativeCell);
            return found && found->MapCoords == nativeCell;
        }

        ui::BeaconSlotSnapshot CaptureBeaconPointer(int owner, std::uintptr_t beacon)
        {
            ui::BeaconSlotSnapshot result;
            if (owner < 0 || owner >= 8 || !beacon) { return result; }
            std::uint8_t flags = 0;
            int recordOwner = -1;
            if (!memory::TryReadMemory(beacon, &result.mPosition, sizeof(result.mPosition)) ||
                !memory::TryReadField(beacon, profile::BEACON_FLAGS_OFFSET, flags) ||
                !memory::TryReadField(beacon, profile::BEACON_OWNER_OFFSET, recordOwner) ||
                !(flags & 1) || recordOwner != owner)
            {
                return result;
            }
            result.mPresent = true;
            const auto* const houses = HouseClass::Array.get();
            auto* const local = HouseClass::Player.get();
            if (!local || !houses || !houses->IsInitialized || !houses->Items ||
                houses->Count < 0 || houses->Count > houses->Capacity ||
                houses->Count > 256 || owner >= houses->Count)
            {
                return result;
            }
            const auto* const house = houses->Items[owner];
            if (!house || house->ArrayIndex != owner ||
                !ui::BeaconNoticeModel::IsEligibleOwner(owner, local->ArrayIndex,
                    house->Defeated, local->IsAlliedWith(owner), house->IsAlliedWith(local)))
            {
                return result;
            }
            // 原生快照的 Z 原样保留，不调用随机子格选择，也不从 Cell2Coord 猜高度。
            if (result.mPosition.mX < 0 || result.mPosition.mY < 0 ||
                result.mPosition.mX / 256 > 32767 || result.mPosition.mY / 256 > 32767 ||
                !IsValidCell({static_cast<std::int16_t>(result.mPosition.mX / 256),
                    static_cast<std::int16_t>(result.mPosition.mY / 256)}))
            {
                return result;
            }
            result.mColor = {house->LaserColor.R, house->LaserColor.G, house->LaserColor.B};
            result.mEligible = true;
            return result;
        }

        ui::BeaconSlotSnapshot CaptureBeaconSlot(int owner, int slot)
        {
            if (owner < 0 || owner >= 8 || slot < 0 || slot >= 3) return {};
            std::uint32_t beacon = 0;
            if (!memory::TryReadMemory(profile::BEACON_MANAGER +
                static_cast<std::uintptr_t>(owner * 3 + slot) * sizeof(beacon), &beacon, sizeof(beacon))) return {};
            return CaptureBeaconPointer(owner, beacon);
        }

        void OnBeaconPlaced(int owner, int slot)
        {
            if (!AreBeaconIndicatorsEnabled() || !IsCurrentSession()) { return; }
            if (g_MustResetBeacons.exchange(false)) { g_Beacons.Reset(); g_BeaconReconcileSchedule.Reset(); }
            g_Beacons.OnPlaced(owner, slot, CaptureBeaconSlot(owner, slot), GetTickCount64());
        }

        void OnRadarAccepted(int type, std::int16_t cellX, std::int16_t cellY)
        {
            const ui::HudMapCell cell{cellX, cellY};
            if (!IsRadarFeedEnabled() || !IsCurrentSession() || !IsValidCell(cell)) { return; }
            if (g_MustResetRadar.exchange(false)) { g_Radar.Reset(); }
            // Create 的 AL=true 表示原生接受；不声称其堆分配或向量追加必定成功。
            g_Radar.OnAccepted(type, cell, GetTickCount64());
        }

        template<std::size_t TSize>
        bool MatchesEntry(std::uintptr_t address, const std::array<std::uint8_t, TSize>& expected)
        {
            std::array<std::uint8_t, TSize> actual{};
            return memory::TryReadMemory(address, actual.data(), actual.size()) && actual == expected;
        }

        bool IsCameraProfileValid()
        {
            return MatchesEntry(profile::SPACE_CAMERA, profile::SPACE_CAMERA_ENTRY) &&
                MatchesEntry(profile::SET_TACTICAL_POSITION, profile::SET_TACTICAL_POSITION_ENTRY) &&
                MatchesEntry(profile::REFRESH_BUILDING_PREVIEW, profile::REFRESH_BUILDING_PREVIEW_ENTRY);
        }

        bool TryJumpToCell(ui::HudMapCell cell)
        {
            if (!IsValidCell(cell)) { return false; }
            auto* const map = MapClass::Instance.get();
            auto* const tactical = TacticalClass::Instance.get();
            auto* const display = DisplayClass::Instance.get();
            auto* const nativeCell = map->TryGetCellAt(CellStruct{cell.mX, cell.mY});
            if (!tactical || !display || !nativeCell) { return false; }

            // 精确复用 Space 的后半段：格心 Z=0，XY 各减原始 Level*128。
            // 不用 FocusOn，也不修改原生八格环、轮换索引或上次按键时间。
            const int terrainOffset = static_cast<signed char>(nativeCell->Level) * 128;
            CoordStruct position{static_cast<int>(cell.mX) * 256 + 128 - terrainOffset,
                static_cast<int>(cell.mY) * 256 + 128 - terrainOffset, 0};
            using SetTacticalPositionFunction = void(__thiscall*)(TacticalClass*, CoordStruct*);
            reinterpret_cast<SetTacticalPositionFunction>(profile::SET_TACTICAL_POSITION)(
                tactical, &position);
            if (display->CurrentBuildingType)
            {
                CellStruct emptyCell{};
                if (memory::TryReadMemory(profile::EMPTY_CELL, &emptyCell, sizeof(emptyCell)))
                {
                    CellStruct ignoredOldCell{};
                    // 0x7341F1 原生传出缓冲区在前，空格哨兵地址在后；RET 8。
                    using RefreshBuildingPreviewFunction = CellStruct*(__thiscall*)(
                        DisplayClass*, CellStruct*, const CellStruct*);
                    reinterpret_cast<RefreshBuildingPreviewFunction>(profile::REFRESH_BUILDING_PREVIEW)(
                        display, &ignoredOldCell, &emptyCell);
                }
            }
            return true;
        }
    }

    bool InstallGameAlertsUi(std::string& outError)
    {
        outError.clear();
        if (!IsSupportedHost(outError)) { return false; }
        if (!MatchesEntry(profile::COORDINATES_LEGAL, profile::COORDINATES_LEGAL_ENTRY) ||
            !MatchesEntry(profile::IS_WITHIN_USABLE_AREA, profile::IS_WITHIN_USABLE_AREA_ENTRY))
        {
            outError = "alerts map validation entries differ from supported build";
            return false;
        }
        std::string beaconError;
        std::string radarError;
        const bool beaconOk = InstallBeaconCaptureHook(&OnBeaconPlaced, beaconError);
        const bool cameraOk = IsCameraProfileValid();
        const bool radarOk = cameraOk && InstallRadarCaptureHook(&OnRadarAccepted, radarError);
        if (!cameraOk) { radarError = "Space camera adapter differs from supported build"; }
        g_IsBeaconAvailable.store(beaconOk, std::memory_order_release);
        g_IsRadarAvailable.store(radarOk, std::memory_order_release);
        if (!beaconError.empty()) { outError = "beacon: " + beaconError; }
        if (!radarError.empty())
        {
            if (!outError.empty()) { outError += "; "; }
            outError += "radar: " + radarError;
        }
        return beaconOk || radarOk;
    }

    void UpdateGameAlertsUi(std::uint64_t nowMs, bool isSessionActive,
        std::uintptr_t sessionIdentity)
    {
        g_GameThread = GetCurrentThreadId();
        SetAlertsUiGameThread(g_GameThread);
        const bool isActive = isSessionActive && sessionIdentity && IsGameSessionReady();
        const auto frame = isActive ? GetCurrentGameFrame() : 0;
        auto* const local = isActive ? HouseClass::Player.get() : nullptr;
        const bool mustReset = !isActive || !g_IsActive.load(std::memory_order_acquire) ||
            sessionIdentity != g_SessionIdentity || local != g_LocalPlayer || frame < g_LastFrame;
        const bool mustResetBeacons = g_MustResetBeacons.exchange(false);
        const bool mustResetRadar = g_MustResetRadar.exchange(false);
        if (mustReset || mustResetBeacons) { g_Beacons.Reset(); g_BeaconReconcileSchedule.Reset(); }
        if (mustReset || mustResetRadar) { g_Radar.Reset(); }
        g_SessionIdentity = isActive ? sessionIdentity : 0;
        g_LocalPlayer = local;
        g_LastFrame = frame;
        g_IsActive.store(isActive, std::memory_order_release);
        if (isActive && AreBeaconIndicatorsEnabled() && g_BeaconReconcileSchedule.ShouldReconcile(nowMs))
        {
            std::array<ui::BeaconSlotSnapshot, ui::BeaconNoticeModel::SLOT_COUNT> slots{};
            std::array<std::uint32_t, ui::BeaconNoticeModel::SLOT_COUNT> pointers{};
            // 表是连续的 24 个 x86 指针；读取失败仍用空快照使旧提示失效。
            if (!memory::TryReadMemory(profile::BEACON_MANAGER, pointers.data(), sizeof(pointers))) pointers.fill(0);
            for (int owner = 0; owner < 8; ++owner)
            {
                for (int slot = 0; slot < 3; ++slot)
                {
                    const auto index = static_cast<std::size_t>(owner * 3 + slot);
                    slots[index] = CaptureBeaconPointer(owner, pointers[index]);
                }
            }
            // 每 100ms 校对删除和关系变化；OnBeaconPlaced 仍即时更新，不延迟新提示。
            // 初次进入只校对，不把已经存在的槽位误报为刚收到的信标。
            g_Beacons.Reconcile(slots, nowMs);
        }
    }

    std::vector<ui::BeaconNotice> CaptureBeaconNotices(std::uint64_t nowMs)
    {
        return AreBeaconIndicatorsEnabled() && !g_MustResetBeacons.load() && IsCurrentSession()
            ? g_Beacons.Capture(nowMs) : std::vector<ui::BeaconNotice>{};
    }

    std::vector<ui::RadarNotice> CaptureRadarNotices()
    {
        return IsRadarFeedEnabled() && !g_MustResetRadar.load() && IsCurrentSession()
            ? g_Radar.Capture() : std::vector<ui::RadarNotice>{};
    }

    bool TryJumpToRadarNotice(std::uint64_t noticeId)
    {
        if (!IsRadarFeedEnabled() || g_MustResetRadar.load() || !IsCurrentSession()) { return false; }
        const auto cell = g_Radar.FindCell(noticeId);
        if (!cell || !TryJumpToCell(*cell)) { return false; }
        return g_Radar.MarkRead(noticeId);
    }

    bool ToggleRadarNoticePin(std::uint64_t noticeId)
    {
        return IsRadarFeedEnabled() && !g_MustResetRadar.load() && IsCurrentSession() &&
            g_Radar.TogglePin(noticeId);
    }

    bool AreBeaconIndicatorsEnabled() noexcept
    {
        return g_IsBeaconAvailable.load(std::memory_order_acquire) &&
            g_IsBeaconEnabled.load(std::memory_order_acquire);
    }

    bool IsRadarFeedEnabled() noexcept
    {
        return g_IsRadarAvailable.load(std::memory_order_acquire) &&
            g_IsRadarEnabled.load(std::memory_order_acquire);
    }

    void SetBeaconIndicatorsEnabled(bool isEnabled)
    {
        if (g_IsBeaconEnabled.exchange(isEnabled) != isEnabled)
        {
            g_MustResetBeacons.store(true, std::memory_order_release);
        }
    }

    void SetRadarFeedEnabled(bool isEnabled)
    {
        if (g_IsRadarEnabled.exchange(isEnabled) != isEnabled)
        {
            g_MustResetRadar.store(true, std::memory_order_release);
        }
    }

    void DisableGameAlertsUi() noexcept
    {
        // Shutdown 可以来自其他线程；只停用入口，模型回收留给游戏线程。
        g_IsActive.store(false, std::memory_order_release);
        g_IsBeaconAvailable.store(false, std::memory_order_release);
        g_IsRadarAvailable.store(false, std::memory_order_release);
        g_MustResetBeacons.store(true, std::memory_order_release);
        g_MustResetRadar.store(true, std::memory_order_release);
        DisableAlertsUiHooks();
    }
}
