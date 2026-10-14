#include "Hooks/PlanningParticipantCapacityHook.h"
#include "Game/TargetProfiles/PlanningParticipantCapacityProfile.h"
#include "Memory/ProcessMemory.h"
#include <Windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>

namespace ra_commands::hooks::planning_participant_capacity
{
    namespace
    {
        namespace profile = game::planning_participant_capacity::profile;
        std::atomic<BypassQuery> g_Query{nullptr};
        void* g_OriginalCompare = nullptr;
        std::uintptr_t g_AcceptContinue = profile::ACCEPT_CONTINUE;
        bool g_Installed = false;
        bool g_Attempted = false;

        template<std::size_t N>
        bool Matches(std::uintptr_t address, const std::array<std::uint8_t, N>& expected)
        {
            std::array<std::uint8_t, N> actual{};
            return memory::TryReadMemory(address, actual.data(), actual.size()) && actual == expected;
        }

        bool __cdecl CanBypass() noexcept
        {
            const auto query = g_Query.load(std::memory_order_acquire);
            return query && query();
        }

        __declspec(naked) void HookParticipantCompare()
        {
            __asm {
                pushfd
                pushad
                call CanBypass
                test al, al
                jz nativePath
                popad
                popfd
                // 这是中段，原生保存的 ebp/ebx 仍在栈上；成功段会 pop ecx 并 ret。
                pop ebp
                pop ebx
                jmp dword ptr [g_AcceptContinue]
            nativePath:
                popad
                popfd
                jmp dword ptr [g_OriginalCompare]
            }
        }
    }

    bool Install(BypassQuery query, std::string& outError)
    {
        if (!query) { outError = "planning participant gate callback is null"; return false; }
        if (g_Installed) { g_Query.store(query, std::memory_order_release); outError.clear(); return true; }
        if (g_Attempted) { outError = "planning participant hook activation failed earlier"; return false; }
        if (!Matches(profile::CAPACITY_COMPARE_ADDRESS, profile::CAPACITY_COMPARE_BYTES)
            || !Matches(profile::RESTORE_REGISTERS_ADDRESS, profile::RESTORE_REGISTERS_BYTES)
            || !Matches(profile::ACCEPT_CONTINUE, profile::ACCEPT_BYTES))
        {
            outError = "planning participant compare or epilogue differs from supported game build";
            return false;
        }
        const auto initialization = MH_Initialize();
        if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED)
        { outError = "MinHook initialization failed for planning participants"; return false; }
        void* trampoline = nullptr;
        void* const target = reinterpret_cast<void*>(profile::CAPACITY_COMPARE_ADDRESS);
        const auto creation = MH_CreateHook(target, &HookParticipantCompare, &trampoline);
        if (creation != MH_OK || !trampoline)
        { outError = "cannot create planning participant compare hook"; return false; }
        g_OriginalCompare = trampoline;
        g_Attempted = true;
        g_Query.store(query, std::memory_order_release);
        if (MH_EnableHook(target) != MH_OK)
        {
            // 激活状态不确定时只停用放行；保留跳板，避免释放潜在原生引用。
            g_Query.store(nullptr, std::memory_order_release);
            outError = "cannot activate planning participant compare hook";
            return false;
        }
        g_Installed = true;
        outError.clear();
        return true;
    }
}
