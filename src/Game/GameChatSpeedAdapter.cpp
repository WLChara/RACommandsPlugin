#include "Game/GameChatSpeedAdapter.h"

#include "Game/GameObjectAccess.h"
#include "Game/TargetProfiles/GameChatSpeedProfile.h"
#include "Game/TargetVersion.h"
#include "Memory/ProcessMemory.h"
#include "NetworkEvent/NativeNetworkEventAdapter.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <Windows.h>
#include <atomic>
#include <cstdint>

namespace ra_commands::game
{
    namespace
    {
        namespace profile = chat_speed_profile;
        std::atomic<const NativeNetworkEventAdapter*> g_NetworkAdapter{nullptr};
        std::atomic<DWORD> g_GameThread{0};
    }

    void BindGameChatSpeedAdapter(const NativeNetworkEventAdapter* adapter) noexcept
    {
        g_NetworkAdapter.store(nullptr, std::memory_order_release);
        g_GameThread.store(0, std::memory_order_release);
        if (!adapter) return;
        try
        {
            std::string error;
            if (!IsSupportedHost(error))
            {
                OutputDebugStringA("[RACommandsPlugin] ChatCommand speed host verification failed\n");
                return;
            }
            g_GameThread.store(GetCurrentThreadId(), std::memory_order_release);
            g_NetworkAdapter.store(adapter, std::memory_order_release);
        }
        catch (...) { OutputDebugStringA("[RACommandsPlugin] ChatCommand speed binding failed\n"); }
    }

    bool TryChangeGameChatSpeed(int playerSpeed, std::wstring& outFeedback)
    {
        const int nativeSpeed = GetNativeChatSpeed(playerSpeed);
        const auto* adapter = g_NetworkAdapter.load(std::memory_order_acquire);
        if (nativeSpeed < 0)
        {
            outFeedback = L"速度必须是 1 到 6 之间的整数。";
            return false;
        }
        if (!adapter)
        {
            outFeedback = L"调速适配器尚未绑定，请检查插件载入状态。";
            return false;
        }
        if (g_GameThread.load(std::memory_order_acquire) != GetCurrentThreadId())
        {
            outFeedback = L"调速请求不在游戏线程，未执行。";
            return false;
        }
        if (!IsGameSessionReady())
        {
            outFeedback = L"当前尚未进入有效对局，无法调速。";
            return false;
        }
        int mode = -1;
        std::uint8_t networkActive = 0;
        if (!memory::TryReadMemory(profile::SESSION_MODE_ADDRESS, &mode, sizeof(mode)))
        {
            outFeedback = L"无法读取当前对局模式。";
            return false;
        }
        // 先按模式决定是否允许本地修改；网络状态失败不能成为本地写入的理由。
        if (mode == 3 || mode == 4)
            memory::TryReadMemory(profile::NETWORK_ACTIVE_ADDRESS, &networkActive, sizeof(networkActive));
        const auto route = GetChatSpeedRoute(mode, networkActive == 1);
        if (route == ChatSpeedRoute::Local)
        {
            if (mode == 0 && !profile::ValidateCampaign(memory::TryReadMemory))
            {
                outFeedback = L"当前版本的战役调速规则无法验证，未修改速度。";
                return false;
            }
            std::uint8_t campaignUnlocked = 0;
            if (mode == 0 && (!memory::TryReadMemory(profile::CAMPAIGN_SPEED_UNLOCKED_ADDRESS,
                    &campaignUnlocked, sizeof(campaignUnlocked)) || !campaignUnlocked))
            {
                outFeedback = L"当前战役锁定了游戏速度。";
                return false;
            }
            int currentSpeed = 0;
            if (!memory::TryReadMemory(profile::GAME_SPEED_ADDRESS, &currentSpeed, sizeof(currentSpeed)))
            {
                outFeedback = L"无法读取当前游戏速度，未修改。";
                return false;
            }
            // 与 4E1DE0 单机分支一致，只改活动选项；不调用速度菜单或修改回放插件挂钩。
            *reinterpret_cast<int*>(profile::GAME_SPEED_ADDRESS) = nativeSpeed;
            outFeedback = L"游戏速度已设为 " + std::to_wstring(playerSpeed) + L"。";
            return true;
        }
        if (route == ChatSpeedRoute::Network)
        {
            if (!profile::ValidateNetwork(memory::TryReadMemory))
            {
                outFeedback = L"当前版本的同步调速入口无法验证，未发送事件。";
                return false;
            }
            const auto* local = HouseClass::Player.get();
            if (local && local->ArrayIndex >= 0 && local->ArrayIndex <= 255)
            {
                const auto event = network_event::BuildGameSpeedEvent(static_cast<std::uint8_t>(local->ArrayIndex), nativeSpeed);
                if (event && adapter->TryEnqueueLocal(*event))
                {
                    outFeedback = L"已请求将对局速度设为 " + std::to_wstring(playerSpeed) + L"，等待同步事件执行。";
                    return true;
                }
            }
            outFeedback = L"调速事件未能入队，请稍后重试。";
            return false;
        }
        outFeedback = L"当前网络或对局模式不允许发送调速事件。";
        return false;
    }
}
