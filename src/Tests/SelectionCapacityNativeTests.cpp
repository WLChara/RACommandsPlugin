#include <Windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <vector>

// PE 装载器先于堆保留原址区间；只改隔离进程的页，不写磁盘游戏映像。
#pragma section(".fixture", read, write)
__declspec(allocate(".fixture")) std::uint8_t g_FixtureImageSpace[0xA00000]{};

// 纳入真实实现，允许测试观察网关跳板和唯一状态所有者。
#include "../Game/SelectionCapacity.cpp"
#include "../Hooks/SelectionCapacityHooks.cpp"

namespace ra_commands::game
{
    bool IsGameSessionReady() { return HouseClass::Player.get() != nullptr; }
}

namespace
{
    namespace sc = ra_commands::game::selection_capacity;
    namespace hooks = ra_commands::hooks::selection_capacity;
    namespace profile = sc::profile;
    namespace memory = ra_commands::memory;
    using Entry = ra_commands::selection_capacity::CandidateEntry;
    using AddFn = bool(__thiscall*)(void*, void*, int, int);
    using SelectFn = int(__thiscall*)(void*, void*, void*);
    int g_Failures = 0;
    volatile std::uint32_t g_GateEax = 0, g_GateEbx = 0, g_GateEcx = 0, g_GateEdx = 0;
    volatile std::uint32_t g_GateEsi = 0, g_GateEdi = 0, g_GateEbp = 0, g_GateFlags = 0, g_GateBranch = 0;
    std::uintptr_t g_TestGate = profile::CAPACITY_GATE_ADDRESS;
    bool g_ResetInsideReader = false;
    bool g_NestedAdded = true;
    bool g_NestedValid = true;
    unsigned g_NestedDepth = 0;
    const Entry* g_NestedBuffer = nullptr;
    int g_ChainCalls = 0;
    AddFn g_OldAdd = nullptr;

    void Check(bool condition, const char* message)
    {
        if (!condition) { std::fprintf(stderr, "%s\n", message); ++g_Failures; }
    }
    bool MapGameImage(const wchar_t* path)
    {
        constexpr std::array<std::uintptr_t, 4> PAGES = {
            0x006D0000, 0x00880000, 0x00A80000, 0x00B00000
        };
        const HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        const DWORD fileSize = GetFileSize(file, nullptr);
        std::vector<std::uint8_t> bytes(fileSize);
        DWORD read = 0;
        const bool loaded = ReadFile(file, bytes.data(), fileSize, &read, nullptr) && read == fileSize;
        CloseHandle(file);
        if (!loaded || fileSize < sizeof(IMAGE_DOS_HEADER)) return false;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0
            || static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > bytes.size())
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
            || nt->OptionalHeader.ImageBase != 0x00400000)
        {
            std::fprintf(stderr, "unexpected PE image base %08x or machine %04x\n",
                nt->OptionalHeader.ImageBase, nt->FileHeader.Machine);
            return false;
        }
        const auto fixtureStart = reinterpret_cast<std::uintptr_t>(g_FixtureImageSpace);
        const auto fixtureEnd = fixtureStart + sizeof(g_FixtureImageSpace);
        for (const auto page : PAGES)
        {
            if (page < fixtureStart || page + 0x10000 > fixtureEnd) return false;
            DWORD previous = 0;
            if (!VirtualProtect(reinterpret_cast<void*>(page), 0x10000,
                    PAGE_EXECUTE_READWRITE, &previous)) return false;
        }
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if (static_cast<std::size_t>(section[i].PointerToRawData)
                    + section[i].SizeOfRawData > bytes.size())
            {
                std::fprintf(stderr, "invalid PE section %u\n", i);
                return false;
            }
            const std::uintptr_t start = 0x00400000 + section[i].VirtualAddress;
            const std::uintptr_t end = start + section[i].SizeOfRawData;
            for (const auto page : PAGES)
            {
                const auto copyStart = (std::max)(start, page);
                const auto copyEnd = (std::min)(end, page + 0x10000);
                if (copyStart < copyEnd)
                    std::memcpy(reinterpret_cast<void*>(copyStart),
                        bytes.data() + section[i].PointerToRawData + copyStart - start,
                        copyEnd - copyStart);
            }
        }
        return true;
    }

    void RecordGate();

    __declspec(naked) void GateAccepted()
    {
        __asm {
            mov g_GateBranch, 1
            jmp RecordGate
        }
    }

    __declspec(naked) void GateRejected()
    {
        __asm {
            mov g_GateBranch, 2
            jmp RecordGate
        }
    }

    __declspec(naked) void RecordGate()
    {
        __asm {
            mov g_GateEax, eax
            mov g_GateEbx, ebx
            mov g_GateEcx, ecx
            mov g_GateEdx, edx
            mov g_GateEsi, esi
            mov g_GateEdi, edi
            mov g_GateEbp, ebp
            pushfd
            pop eax
            mov g_GateFlags, eax
            ret
        }
    }

    __declspec(naked) void __cdecl ProbeGate(void*)
    {
        __asm {
            pushad
            mov esi, [esp + 36]
            mov eax, 13579BDFh
            mov ebx, 2468ACE0h
            mov ecx, 11223344h
            mov edx, 55667788h
            mov edi, 1122340Ch
            mov ebp, 100
            cmp eax, 13579BDFh
            call dword ptr [g_TestGate]
            popad
            ret
        }
    }

    void CheckGate(void* tactical, std::uint32_t branch)
    {
        g_GateBranch = 0;
        ProbeGate(tactical);
        Check(g_GateBranch == branch, "capacity gate chose the wrong continuation");
        Check(g_GateEax == 0x13579BDF && g_GateEbx == 0x2468ACE0
            && g_GateEcx == 0x11223344 && g_GateEdx == 0x55667788
            && g_GateEsi == reinterpret_cast<std::uintptr_t>(tactical)
            && g_GateEdi == 0x1122340C && g_GateEbp == 100
            && (g_GateFlags & 0x40) != 0,
            "capacity gate did not preserve registers or ZF");
    }

    void CheckSites(std::size_t capacity)
    {
        Check(sc::GetState().mCapacity == capacity, "capacity state differs");
        Check(memory::VerifyPatchSites(sc::g_State.mSites.data(), sc::g_State.mSites.size(),
            profile::BUILD_ADDRESS, profile::BUILD_ADDRESS,
            reinterpret_cast<std::uintptr_t>(sc::g_State.mActive), memory::PatchState::Replacement)
            == memory::PatchResult::Success, "14 native operands disagree with table");
    }

    int __fastcall FixtureSelect(void* tactical, void*, void*, void*)
    {
        g_NestedDepth = sc::g_State.mReaderDepth;
        if (g_ResetInsideReader) sc::OnGameFrame(false, 2);
        else {
            int object = 123;
            g_NestedAdded = reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, 1, 2);
        }
        g_NestedValid = sc::g_State.mEpochValid;
        g_NestedBuffer = sc::g_State.mActive;
        return 77;
    }

    bool __fastcall ExistingAdd(void* tactical, void*, void* object, int x, int y)
    {
        ++g_ChainCalls;
        return g_OldAdd(tactical, object, x, y);
    }

    int __fastcall FixtureBuild(void*, void*, int x, int y, int width, int height)
    {
        Check(sc::g_State.mWriterDepth == 1, "Build writer depth differs");
        Check(x == 1 && y == 2 && width == 3 && height == 4, "Build stack ABI differs");
        return 141;
    }

    bool __fastcall FixtureRect(void*, void*)
    {
        Check(sc::g_State.mReaderDepth == 1, "Rect reader depth differs");
        return true;
    }

    void* __fastcall FixtureFind(void*, void*, void* point)
    {
        Check(sc::g_State.mReaderDepth == 1, "Find reader depth differs");
        return point;
    }

    __declspec(naked) void __cdecl ProbeEpoch(void*)
    {
        __asm {
            pushad
            mov ebp, [esp + 36]
            mov eax, 13579BDFh
            mov ebx, 2468ACE0h
            mov ecx, 11223344h
            mov edx, 55667788h
            mov esi, 99
            mov edi, 1122340Ch
            cmp eax, 13579BDFh
            mov eax, 006D3D21h
            call eax
            popad
            ret
        }
    }

    bool InstallExistingChain()
    {
        auto* trampoline = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 32,
            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        if (!trampoline) return false;
        std::memcpy(trampoline, reinterpret_cast<void*>(profile::ADD_ADDRESS), 6);
        trampoline[6] = 0xE9;
        const auto onward = static_cast<std::int32_t>(profile::ADD_ADDRESS + 6
            - reinterpret_cast<std::uintptr_t>(trampoline + 11));
        std::memcpy(trampoline + 7, &onward, 4);
        g_OldAdd = reinterpret_cast<AddFn>(trampoline);
        auto* entry = reinterpret_cast<std::uint8_t*>(profile::ADD_ADDRESS);
        entry[0] = 0xE9;
        const auto detour = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&ExistingAdd)
            - profile::ADD_ADDRESS - 5);
        std::memcpy(entry + 1, &detour, 4);
        FlushInstructionCache(GetCurrentProcess(), entry, 6);
        return true;
    }

    DWORD WINAPI ForeignThread(void* tactical)
    {
        int object = 123;
        return reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, 1, 2) ? 1 : 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2 || !MapGameImage(argv[1])) {
        std::fprintf(stderr, "matching PE32 game fixture required\n"); return 1;
    }
    const bool fallback = argc > 2 && std::wcscmp(argv[2], L"--fallback") == 0;
    const bool conflict = argc > 2 && std::wcscmp(argv[2], L"--operand-conflict") == 0;
    const bool chain = argc > 2 && std::wcscmp(argv[2], L"--chain") == 0;
    std::array<std::uint8_t, 0x1000> tacticalBytes{};
    void* tactical = tacticalBytes.data();
    *reinterpret_cast<void**>(0x887324) = tactical;
    *reinterpret_cast<void**>(0xA83D4C) = tactical;
    *reinterpret_cast<int*>(0xA8B238) = 5;
    *reinterpret_cast<int*>(0x886FA8) = 100000;
    *reinterpret_cast<int*>(0x886FAC) = 100000;
    *reinterpret_cast<std::uint32_t*>(profile::REPLAY_FLAGS_ADDRESS) = 0;
    *reinterpret_cast<int*>(tacticalBytes.data() + 0xB0) = 100;
    *reinterpret_cast<int*>(tacticalBytes.data() + 0xB4) = 200;
    auto* original = reinterpret_cast<Entry*>(profile::ORIGINAL_BASE);
    for (int i = 0; i < 500; ++i) original[i] = {static_cast<std::uint32_t>(0x12345678 + i), 17 + i, 19 + i};
    sc::Count(tactical) = 500;
    if (fallback) *reinterpret_cast<std::uint8_t*>(profile::EPOCH_GATE_ADDRESS) = 0x90;
    if (conflict) *reinterpret_cast<std::uint32_t*>(profile::FIXED_PATCH_SITES[2].mInstructionAddress + 3) = 0x12345678;
    if (chain && !InstallExistingChain()) return 1;
    if (argc > 2 && std::wcscmp(argv[2], L"--pre-disabled") == 0) sc::Disable();
    std::string error;
    const bool installed = sc::Install(error);
    if (conflict) {
        Check(!installed && !sc::GetState().mFixedAvailable, "conflicting table accepted");
        Check(*reinterpret_cast<std::uint32_t*>(profile::FIXED_PATCH_SITES[0].mInstructionAddress + 1) == 500,
            "failed group verification partially patched capacities");
        return g_Failures ? 1 : 0;
    }
    Check(installed && sc::GetState().mFixedAvailable, "fixed installation failed");
    if (!installed) { std::fprintf(stderr, "%s\n", error.c_str()); return 1; }
    Check(sc::Count(tactical) == 500 && sc::g_State.mActive[0].mObject == 0x12345678
        && sc::g_State.mActive[499].mObject == 0x12345678 + 499
        && sc::g_State.mActive[499].mX == 516, "mid-session 500 original candidates were lost");
    CheckSites(4096);
    if (fallback) {
        Check(!sc::GetState().mDynamicAvailable, "bad mid-gate enabled dynamic hooks");
        sc::Disable();
        CheckSites(4096);
        return g_Failures ? 1 : 0;
    }
    Check(sc::GetState().mDynamicAvailable, "dynamic hook installation failed");
    if (!sc::GetState().mDynamicAvailable) return 1;
    sc::OnGameFrame(true, 1);
    sc::OnEpoch(tactical);
    sc::Count(tactical) = 0;
    const auto originalEpoch = hooks::g_RealEpoch;
    hooks::g_RealEpoch = reinterpret_cast<void*>(&GateAccepted);
    ProbeEpoch(tactical);
    Check(g_GateBranch == 1 && g_GateEax == profile::EPOCH_GATE_ADDRESS
        && g_GateEbx == 0x2468ACE0 && g_GateEcx == 0x11223344 && g_GateEdx == 0x55667788
        && g_GateEsi == 99 && g_GateEdi == 0x1122340C
        && g_GateEbp == reinterpret_cast<std::uintptr_t>(tactical) && (g_GateFlags & 0x40) != 0,
        "epoch gate failed register or EFLAGS preservation");
    hooks::g_RealEpoch = originalEpoch;
    const auto originalBuild = hooks::g_RealBuild;
    hooks::g_RealBuild = reinterpret_cast<hooks::BuildFn>(&FixtureBuild);
    Check(reinterpret_cast<hooks::BuildFn>(profile::BUILD_ADDRESS)(tactical, 1, 2, 3, 4) == 141
        && sc::g_State.mWriterDepth == 0, "Build return or writer unwind differs");
    hooks::g_RealBuild = originalBuild;
    const auto originalRect = hooks::g_RealRect;
    const auto originalFind = hooks::g_RealFind;
    hooks::g_RealRect = reinterpret_cast<hooks::RectFn>(&FixtureRect);
    hooks::g_RealFind = reinterpret_cast<hooks::FindFn>(&FixtureFind);
    Check(reinterpret_cast<hooks::RectFn>(profile::RECT_ADDRESS)(tactical), "Rect result differs");
    Check(reinterpret_cast<hooks::FindFn>(profile::FIND_ADDRESS)(tactical, tactical) == tactical
        && sc::g_State.mReaderDepth == 0, "Find result or reader unwind differs");
    hooks::g_RealRect = originalRect;
    hooks::g_RealFind = originalFind;
    int object = 456;
    for (int i = 0; i < 20000; ++i) {
        Check(reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, i % 31, i % 37),
            "real Add rejected candidate");
    }
    Check(sc::Count(tactical) == 20000, "real Add count differs");
    CheckSites(32768);
    for (int i = 0; i < 20000; ++i) {
        const auto& entry = sc::g_State.mActive[i];
        Check(entry.mObject == reinterpret_cast<std::uintptr_t>(&object)
            && entry.mX == 100 + i % 31 && entry.mY == 200 + i % 37, "real Add payload differs");
    }
    Check(sc::g_State.mActive[32767].mObject == 0, "growth tail not zero");
    if (chain) Check(g_ChainCalls == 20000, "existing E9 chain was bypassed");

    const auto realCapacity = hooks::g_RealCapacity;
    const auto realSkip = hooks::g_SkipCandidate;
    hooks::g_RealCapacity = reinterpret_cast<void*>(&GateAccepted);
    hooks::g_SkipCandidate = reinterpret_cast<std::uintptr_t>(&GateRejected);
    CheckGate(tactical, 1);
    sc::Count(tactical) = 32768;
    CheckGate(tactical, 1);
    CheckSites(65536);
    sc::Count(tactical) = 20000;
    hooks::g_RealCapacity = realCapacity;
    hooks::g_SkipCandidate = realSkip;

    DWORD threadId = 0, result = 1;
    const HANDLE thread = CreateThread(nullptr, 0, &ForeignThread, tactical, 0, &threadId);
    if (thread) { WaitForSingleObject(thread, INFINITE); GetExitCodeThread(thread, &result); CloseHandle(thread); }
    Check(thread && result == 0 && sc::Count(tactical) == 20000, "foreign-thread Add accepted");

    const auto* dynamic = sc::g_State.mActive;
    const auto originalSelect = hooks::g_RealSelect;
    hooks::g_RealSelect = reinterpret_cast<hooks::SelectFn>(&FixtureSelect);
    sc::Count(tactical) = static_cast<int>(sc::g_State.mCapacity);
    Check(reinterpret_cast<SelectFn>(profile::SELECT_ADDRESS)(tactical, nullptr, nullptr) == 77,
        "Select return value not forwarded");
    Check(g_NestedDepth == 1 && !g_NestedAdded && !g_NestedValid
        && sc::Count(tactical) == 0 && g_NestedBuffer == dynamic && sc::g_State.mDynamic.Buffer() == dynamic,
        "nested growth changed reader-held storage or failed invalidation");
    Check(!reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, 1, 2),
        "invalid epoch accepted Add");
    sc::OnEpoch(tactical);
    Check(reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, 1, 2), "epoch recovery failed");
    g_ResetInsideReader = true;
    Check(reinterpret_cast<SelectFn>(profile::SELECT_ADDRESS)(tactical, nullptr, nullptr) == 77,
        "reader Reset changed return value");
    Check(g_NestedDepth == 1 && !g_NestedValid && sc::Count(tactical) == 0
        && g_NestedBuffer == dynamic && sc::g_State.mDynamic.Buffer() == dynamic,
        "session reset freed reader-held storage");
    hooks::g_RealSelect = originalSelect;
    // 会话在下一次原生 epoch 前重新激活，延期 reset 仍须完成。
    sc::OnGameFrame(true, 3);
    sc::OnEpoch(tactical);
    Check(sc::g_State.mActive == sc::g_State.mFixed && !sc::g_State.mDynamic.Buffer(), "safe reset did not rebind fixed table");
    CheckSites(4096);

    for (const int mode : {0, 5, 3, 4}) {
        sc::OnGameFrame(true, 3);
        *reinterpret_cast<int*>(0xA8B238) = mode;
        sc::OnEpoch(tactical);
        sc::Count(tactical) = 4096;
        const bool added = reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, 1, 2);
        Check(added == (mode == 0 || mode == 5), "mode policy differs");
        if (mode == 0 || mode == 5) { CheckSites(8192); sc::ResetWorld(tactical); sc::OnEpoch(tactical); }
    }
    *reinterpret_cast<int*>(0xA8B238) = 5;
    *reinterpret_cast<std::uint32_t*>(profile::REPLAY_FLAGS_ADDRESS) = 2;
    sc::OnEpoch(tactical);
    sc::Count(tactical) = 4096;
    Check(!reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, 1, 2), "replay expanded table");
    CheckSites(4096);
    hooks::g_RealCapacity = reinterpret_cast<void*>(&GateAccepted);
    hooks::g_SkipCandidate = reinterpret_cast<std::uintptr_t>(&GateRejected);
    CheckGate(tactical, 2);
    hooks::g_RealCapacity = realCapacity;
    hooks::g_SkipCandidate = realSkip;
    *reinterpret_cast<std::uint32_t*>(profile::REPLAY_FLAGS_ADDRESS) = 0;
    sc::OnEpoch(tactical);
    Check(reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, 1, 2), "local growth after replay failed");
    dynamic = sc::g_State.mActive;
    sc::Disable();
    Check(sc::g_State.mActive == dynamic && !sc::GetState().mDynamicAvailable, "Disable released active storage");
    sc::Count(tactical) = 8192;
    Check(!reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, 1, 2), "disabled storage expanded");
    sc::OnEpoch(tactical);
    CheckSites(4096);
    sc::Count(tactical) = 4095;
    Check(reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, 1, 2)
        && sc::Count(tactical) == 4096, "retained Hook stopped native fixed-capacity access");
    sc::ResetWorld(tactical);
    Check(sc::g_State.mFixed[4095].mObject == 0, "reset retained stale fixed candidate");
    Check(sc::Install(error) && sc::GetState().mDynamicAvailable, "Install did not reactivate retained hooks");
    sc::OnEpoch(tactical);
    sc::Count(tactical) = 4096;
    Check(reinterpret_cast<AddFn>(profile::ADD_ADDRESS)(tactical, &object, 1, 2), "reactivated storage did not expand");
    CheckSites(8192);
    return g_Failures ? 1 : 0;
}
