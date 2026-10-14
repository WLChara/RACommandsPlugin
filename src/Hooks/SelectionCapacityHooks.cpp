#include "Hooks/SelectionCapacityHooks.h"
#include "Game/TargetProfiles/SelectionCapacityProfile.h"
#include "Memory/ProcessMemory.h"
#include <Windows.h>
#include <MinHook.h>
#include <array>
#include <cstdint>

namespace ra_commands::hooks::selection_capacity
{
    namespace
    {
        namespace profile = game::selection_capacity::profile;
        using BuildFn = int(__thiscall*)(void*, int, int, int, int);
        using AddFn = bool(__thiscall*)(void*, void*, int, int);
        using RectFn = bool(__thiscall*)(void*);
        using FindFn = void*(__thiscall*)(void*, void*);
        // 原生调用方忽略 EAX；保留机器返回值及两个栈参数。
        using SelectFn = int(__thiscall*)(void*, void*, void*);
        Callbacks g_Callbacks{};
        void* g_RealEpoch = nullptr;
        void* g_RealCapacity = nullptr;
        std::uintptr_t g_SkipCandidate = profile::CAPACITY_SKIP_ADDRESS;
        BuildFn g_RealBuild = nullptr;
        AddFn g_RealAdd = nullptr;
        RectFn g_RealRect = nullptr;
        FindFn g_RealFind = nullptr;
        SelectFn g_RealSelect = nullptr;
        bool g_Installed = false;
        bool g_Attempted = false;

        void __cdecl OnEpoch(void* tactical) { g_Callbacks.mOnEpoch(tactical); }
        bool __cdecl EnsureBuildRoom(void* tactical, int x, int y)
        { return g_Callbacks.mEnsureBuildRoom(tactical, x, y); }

        // popad 前使用 helper 的布尔返回值分流；两条出口恢复全部寄存器和 EFLAGS。
        __declspec(naked) void HookCapacityGate()
        {
            __asm {
                pushfd
                pushad
                mov eax, edi
                sub eax, ecx
                push eax
                push ebp
                push esi
                call EnsureBuildRoom
                add esp, 12
                test al, al
                jz rejected
                popad
                popfd
                jmp dword ptr [g_RealCapacity]
            rejected:
                popad
                popfd
                jmp dword ptr [g_SkipCandidate]
            }
        }

        __declspec(naked) void HookEpochGate()
        {
            __asm {
                pushfd
                pushad
                push ebp
                call OnEpoch
                add esp, 4
                popad
                popfd
                jmp dword ptr [g_RealEpoch]
            }
        }

        int __fastcall HookBuild(void* tactical, void*, int x, int y, int width, int height)
        {
            if (!g_Callbacks.mBeginWriter()) return 0;
            int result = 0;
            __try { result = g_RealBuild(tactical, x, y, width, height); }
            __finally { g_Callbacks.mEndWriter(); }
            return result;
        }

        bool __fastcall HookAdd(void* tactical, void*, void* object, int x, int y)
        {
            if (!g_Callbacks.mBeginWriter()) return false;
            bool result = false;
            __try {
                if (g_Callbacks.mEnsureAddRoom(tactical, x, y))
                    result = g_RealAdd(tactical, object, x, y);
            }
            __finally { g_Callbacks.mEndWriter(); }
            return result;
        }

        bool __fastcall HookRect(void* tactical, void*)
        {
            if (!g_Callbacks.mBeginReader(tactical)) return false;
            bool result = false;
            __try { result = g_RealRect(tactical); }
            __finally { g_Callbacks.mEndReader(); }
            return result;
        }

        void* __fastcall HookFind(void* tactical, void*, void* point)
        {
            if (!g_Callbacks.mBeginReader(tactical)) return nullptr;
            void* result = nullptr;
            __try { result = g_RealFind(tactical, point); }
            __finally { g_Callbacks.mEndReader(); }
            return result;
        }

        int __fastcall HookSelect(void* tactical, void*, void* rectangle, void* callback)
        {
            if (!g_Callbacks.mBeginReader(tactical)) return 0;
            int result = 0;
            __try { result = g_RealSelect(tactical, rectangle, callback); }
            __finally { g_Callbacks.mEndReader(); }
            return result;
        }

        template<std::size_t N>
        bool Matches(std::uintptr_t address, const std::array<std::uint8_t, N>& expected)
        {
            std::array<std::uint8_t, N> actual{};
            return memory::TryReadMemory(address, actual.data(), actual.size()) && actual == expected;
        }
    }

    bool VerifyGates()
    {
        return Matches(profile::EPOCH_GATE_ADDRESS, profile::EPOCH_BYTES)
            && Matches(profile::CAPACITY_GATE_ADDRESS, profile::CAPACITY_BYTES);
    }

    bool Install(const Callbacks& callbacks, std::string& outError)
    {
        if (g_Installed) return true;
        if (g_Attempted || !VerifyGates()) {
            outError = "selection mid-function gate differs; fixed storage retained";
            return false;
        }
        g_Attempted = true;
        g_Callbacks = callbacks;
        const auto initialization = MH_Initialize();
        if (initialization != MH_OK && initialization != MH_ERROR_ALREADY_INITIALIZED) {
            outError = "MinHook initialization failed for selection capacity";
            return false;
        }
        const std::array<void*, 7> detours = {
            reinterpret_cast<void*>(&HookEpochGate), reinterpret_cast<void*>(&HookCapacityGate),
            reinterpret_cast<void*>(&HookBuild), reinterpret_cast<void*>(&HookAdd),
            reinterpret_cast<void*>(&HookRect), reinterpret_cast<void*>(&HookFind),
            reinterpret_cast<void*>(&HookSelect)
        };
        std::array<void**, 7> originals = {
            &g_RealEpoch, &g_RealCapacity, reinterpret_cast<void**>(&g_RealBuild),
            reinterpret_cast<void**>(&g_RealAdd), reinterpret_cast<void**>(&g_RealRect),
            reinterpret_cast<void**>(&g_RealFind), reinterpret_cast<void**>(&g_RealSelect)
        };
        std::size_t created = 0;
        for (; created < detours.size(); ++created) {
            if (MH_CreateHook(reinterpret_cast<void*>(profile::HOOK_ADDRESSES[created]),
                    detours[created], originals[created]) != MH_OK) break;
        }
        bool enabled = created == detours.size();
        if (enabled) {
            for (const auto address : profile::HOOK_ADDRESSES) {
                if (MH_EnableHook(reinterpret_cast<void*>(address)) != MH_OK) {
                    enabled = false;
                    break;
                }
            }
        }
        if (!enabled) {
            // 回滚启用状态；保留已创建跳板，避免部分启用失败后释放潜在引用。
            for (std::size_t index = 0; index < created; ++index)
                MH_DisableHook(reinterpret_cast<void*>(profile::HOOK_ADDRESSES[index]));
            outError = "selection hooks unavailable; fixed storage retained";
            return false;
        }
        g_Installed = true;
        outError.clear();
        return true;
    }
}
