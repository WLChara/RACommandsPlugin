#include "Game/PlanningParticipantCapacity.h"
#include "Game/GameObjectAccess.h"
#include "Game/TargetProfiles/PlanningParticipantCapacityProfile.h"
#include "Hooks/PlanningParticipantCapacityHook.h"
#include "Memory/ProcessMemory.h"
#include "NetworkEvent/CommandCapacity.h"
#include <Windows.h>
#include <HouseClass.h>
#include <SessionClass.h>
#include <atomic>

namespace ra_commands::game::planning_participant_capacity
{
    namespace
    {
        std::atomic<bool> g_Enabled{false};
        std::atomic<DWORD> g_OwnerThread{0};
        bool g_Installed = false;

        bool SessionReady() noexcept
        {
            __try { return IsGameSessionReady(); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool CanBypass() noexcept
        {
            if (!g_Enabled.load(std::memory_order_acquire)
                || GetCurrentThreadId() != g_OwnerThread.load(std::memory_order_acquire)
                || !command_capacity::CanCapturePlanningEvents()) return false;
            // 传输能力失效时沿用原生数量限制，避免入口放行后又被128槽OutList截断。
            // 所有 SDK 状态只在安装所属游戏线程读取；损坏的会话数据按原生检查回退。
            __try
            {
                if (!IsGameSessionReady()) return false;
                const auto* player = HouseClass::Player.get();
                if (!player || player == HouseClass::Observer.get()
                    || player->ArrayIndex < 0 || player->ArrayIndex >= profile::HOUSE_COUNTER_SLOTS) return false;
                const auto mode = SessionClass::Instance->GameMode;
                if (mode != GameMode::Campaign && mode != GameMode::Skirmish
                    && mode != GameMode::LAN && mode != GameMode::Internet) return false;
                std::uint32_t replayFlags = profile::REPLAY_FLAG;
                return memory::TryReadMemory(profile::REPLAY_FLAGS_ADDRESS, &replayFlags, sizeof(replayFlags))
                    && (replayFlags & profile::REPLAY_FLAG) == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }
    }

    bool Install(std::string& outError)
    {
        if (!SessionReady()) { outError = "planning participant installation requires a ready game session"; return false; }
        const auto thread = GetCurrentThreadId();
        if (g_Installed)
        {
            if (thread != g_OwnerThread.load(std::memory_order_acquire))
            { outError = "planning participant hook belongs to another game thread"; return false; }
            g_Enabled.store(true, std::memory_order_release);
            outError.clear();
            return true;
        }
        g_OwnerThread.store(thread, std::memory_order_release);
        if (!hooks::planning_participant_capacity::Install(&CanBypass, outError)) return false;
        g_Installed = true;
        g_Enabled.store(true, std::memory_order_release);
        return true;
    }
    void Disable() noexcept { g_Enabled.store(false, std::memory_order_release); }
    bool IsEnabled() noexcept { return g_Enabled.load(std::memory_order_acquire); }
}
