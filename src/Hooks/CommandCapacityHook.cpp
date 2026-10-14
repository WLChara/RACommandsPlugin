#include "Hooks/CommandCapacityHook.h"

#include "NetworkEvent/CommandCapacityNativeAdapter.h"

#include <Windows.h>
#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace ra_commands::game::command_capacity::hooks
{
    namespace
    {
        using network_event::NetworkEvent;
        using network_event::TargetPayload;
        constexpr std::uintptr_t MEGA_MISSION_ADDRESS = 0x00646E90u;
        constexpr std::uintptr_t MEGA_MISSION_F_ADDRESS = 0x00646F70u;
        constexpr std::uintptr_t FRAME_PARSER_ADDRESS = 0x00647260u;
        constexpr std::uintptr_t FRAME_SEND_ADDRESS = 0x00649CA0u;

        using MegaMissionFunction = bool(__fastcall*)(int, const TargetPayload*, int,
            char, const TargetPayload*);
        using MegaMissionFFunction = bool(__fastcall*)(int, const TargetPayload*, int,
            char, const TargetPayload*, int, int);
        using FrameParserFunction = void(__cdecl*)();
        using FrameSendFunction = int(__fastcall*)(void*, NetworkEvent*, int, int, std::uint16_t);

        std::atomic<MegaMissionFunction> g_OriginalMegaMission{nullptr};
        std::atomic<MegaMissionFFunction> g_OriginalMegaMissionF{nullptr};
        std::atomic<FrameParserFunction> g_OriginalFrameParser{nullptr};
        std::atomic<FrameSendFunction> g_OriginalFrameSend{nullptr};
        std::atomic<bool> g_Installed{false};
        std::mutex g_InstallMutex;
        bool g_InstallationFailed = false;

        bool __fastcall HookMegaMission(int mission, const TargetPayload* target,
            int selfId, char selfKind, const TargetPayload* destination)
        {
            const auto result = detail::CaptureMegaMission(mission, target, selfId, selfKind, destination);
            if (result != CaptureResult::NotHandled) return result == CaptureResult::Queued;
            const auto original = g_OriginalMegaMission.load(std::memory_order_acquire);
            return original && original(mission, target, selfId, selfKind, destination);
        }

        bool __fastcall HookMegaMissionF(int mission, const TargetPayload* target,
            int selfId, char selfKind, const TargetPayload* destination, int speed, int maximumSpeed)
        {
            const auto result = detail::CaptureMegaMissionF(mission, target, selfId,
                selfKind, destination, speed, maximumSpeed);
            if (result != CaptureResult::NotHandled) return result == CaptureResult::Queued;
            const auto original = g_OriginalMegaMissionF.load(std::memory_order_acquire);
            return original && original(mission, target, selfId, selfKind, destination, speed, maximumSpeed);
        }

        void __cdecl HookFrameParser()
        {
            detail::BeforeNativeParser();
            const auto original = g_OriginalFrameParser.load(std::memory_order_acquire);
            if (original) original();
        }

        int __fastcall HookFrameSend(void* transport, NetworkEvent* frameInfo,
            int packetCapacityBytes, int frameDelay, std::uint16_t commandCount)
        {
            detail::BeforeNativeSend();
            const auto original = g_OriginalFrameSend.load(std::memory_order_acquire);
            if (!original) return 0;
            const int originalCount = original(transport, frameInfo,
                packetCapacityBytes, frameDelay, commandCount);
            return originalCount + detail::AfterNativeSend(transport, frameInfo,
                packetCapacityBytes, frameDelay, commandCount);
        }

        bool IsExecutable(std::uintptr_t address) noexcept
        {
            MEMORY_BASIC_INFORMATION info{};
            if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) ||
                info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
            return (info.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                PAGE_EXECUTE_WRITECOPY)) != 0;
        }

        template<typename TFunction>
        bool Create(std::uintptr_t address, void* detour,
            std::atomic<TFunction>& original, std::string& outError)
        {
            void* trampoline = nullptr;
            const auto status = MH_CreateHook(reinterpret_cast<void*>(address), detour, &trampoline);
            if (status != MH_OK || !trampoline)
            {
                outError = "command capacity hook creation failed: ";
                outError += MH_StatusToString(status);
                return false;
            }
            original.store(reinterpret_cast<TFunction>(trampoline), std::memory_order_release);
            return true;
        }
    }

    bool Install(std::string& outError)
    {
        std::lock_guard lock(g_InstallMutex);
        if (g_Installed.load(std::memory_order_acquire)) { outError.clear(); return true; }
        if (g_InstallationFailed)
        {
            outError = "command capacity hooks failed earlier in this process";
            return false;
        }
        const std::array addresses{MEGA_MISSION_ADDRESS, MEGA_MISSION_F_ADDRESS,
            FRAME_PARSER_ADDRESS, FRAME_SEND_ADDRESS};
        for (const auto address : addresses)
        {
            if (!IsExecutable(address))
            {
                outError = "command capacity native entry is not executable";
                return false;
            }
        }
        const auto initialization = MH_Initialize();
        if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED)
        {
            outError = "MinHook initialization failed for command capacity";
            return false;
        }
        // Bootstrap的目标指纹和Syringe时序门禁已通过；从当前入口沿既有永久链转发。
        if (!Create(MEGA_MISSION_ADDRESS, &HookMegaMission, g_OriginalMegaMission, outError) ||
            !Create(MEGA_MISSION_F_ADDRESS, &HookMegaMissionF, g_OriginalMegaMissionF, outError) ||
            !Create(FRAME_PARSER_ADDRESS, &HookFrameParser, g_OriginalFrameParser, outError) ||
            !Create(FRAME_SEND_ADDRESS, &HookFrameSend, g_OriginalFrameSend, outError))
        {
            g_InstallationFailed = true;
            return false;
        }
        HMODULE pinned = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&HookFrameParser), &pinned))
        {
            g_InstallationFailed = true;
            outError = "cannot pin command capacity hooks for process lifetime";
            return false;
        }
        for (const auto address : addresses)
        {
            const auto activation = MH_EnableHook(reinterpret_cast<void*>(address));
            if (activation != MH_OK)
            {
                // 已启用的入口可能正在执行，保留所有trampoline；上层Ready仍保持关闭。
                g_InstallationFailed = true;
                outError = "command capacity hook activation failed: ";
                outError += MH_StatusToString(activation);
                return false;
            }
        }
        g_Installed.store(true, std::memory_order_release);
        outError.clear();
        return true;
    }

    bool IsInstalled() noexcept { return g_Installed.load(std::memory_order_acquire); }
}
