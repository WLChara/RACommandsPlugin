#include <Windows.h>
#include <MinHook.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <vector>

// 在隔离32位进程保留原址区间；只映射真实PE代码/数据页，不执行完整游戏。
#pragma section(".fixture", read, write)
__declspec(allocate(".fixture")) std::uint8_t g_FixtureImageSpace[0xA00000]{};

#include "../Game/PlanningParticipantCapacity.cpp"
#include "../Hooks/PlanningParticipantCapacityHook.cpp"

namespace
{
    bool g_SessionReady = true;
    bool g_FaultSessionQuery = false;
    bool g_PlanningTransportReady = true;
}
namespace ra_commands::game::command_capacity
{
    // 传输测试在独立夹具中执行；这里单独验证参与者入口的能力降级门禁。
    bool CanCapturePlanningEvents() noexcept { return g_PlanningTransportReady; }
}
namespace ra_commands::game
{
    // 就绪检测是测试端口；Game模块中的线程、mode、player、observer及replay门禁执行真实生产代码。
    bool IsGameSessionReady()
    {
        if (g_FaultSessionQuery) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
        return g_SessionReady;
    }
}
namespace
{
    namespace capacity = ra_commands::game::planning_participant_capacity;
    namespace hooks = ra_commands::hooks::planning_participant_capacity;
    namespace profile = capacity::profile;
    constexpr std::uintptr_t ENTRY_ADDRESS = 0x00639130u;
    constexpr std::uintptr_t NATIVE_REJECT_ADDRESS = 0x006391B1u;
    constexpr std::uintptr_t EARLY_LENGTH_ADDRESS = 0x006383B4u;
    constexpr std::uintptr_t TOKEN_LENGTH_ADDRESS = 0x006384DCu;
    constexpr std::array<std::uint8_t, 6> ENTRY_BYTES{0x51,0xA0,0xF4,0x4C,0xAC,0x00};
    constexpr std::array<std::uint8_t, 6> EARLY_LENGTH_BYTES{0x83,0x7D,0x14,0x0F,0x75,0x0A};
    constexpr std::array<std::uint8_t, 5> TOKEN_LENGTH_BYTES{0x83,0xF8,0x10,0x75,0x96};
    std::uint8_t g_House[0x100]{};
    std::array<std::array<std::uint8_t, 0x518>, 1000> g_Objects{};
    std::array<void*, 1000> g_ObjectPointers{};
    int g_Failures = 0;
    volatile std::uint32_t g_NativeRejected = 0;
    volatile std::uint32_t g_Eax = 0, g_Ebx = 0, g_Ecx = 0, g_Edx = 0;
    volatile std::uint32_t g_Esi = 0, g_Edi = 0, g_Ebp = 0, g_Flags = 0;
    volatile std::uint32_t g_StackBefore = 0, g_StackAfter = 0;
    int g_ProbeCount = 129;
    std::uintptr_t g_CompareAddress = profile::CAPACITY_COMPARE_ADDRESS;
    void* g_OriginalEntry = nullptr;
    volatile std::uint32_t g_WrapperBefore = 0, g_WrapperAfter = 0;
    std::uint8_t g_ThirdPartyDeny = 0;

    void Check(bool condition, const char* message)
    {
        if (!condition) { std::fprintf(stderr,"%s\n",message); ++g_Failures; }
    }
    bool MapGameImage(const wchar_t* path)
    {
        constexpr std::array<std::uintptr_t, 5> PAGES = {
            0x00638000u, 0x00639000u, 0x00705000u, 0x00A80000u, 0x00AC0000u
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
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
            static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > bytes.size())
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt->OptionalHeader.ImageBase != 0x00400000u)
            return false;
        const auto fixtureStart = reinterpret_cast<std::uintptr_t>(g_FixtureImageSpace);
        const auto fixtureEnd = fixtureStart + sizeof(g_FixtureImageSpace);
        for (const auto page : PAGES)
        {
            if (page < fixtureStart || page + 0x10000u > fixtureEnd) return false;
            DWORD previous = 0;
            if (!VirtualProtect(reinterpret_cast<void*>(page), 0x10000u,
                    PAGE_EXECUTE_READWRITE, &previous)) return false;
        }
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if (static_cast<std::size_t>(sections[i].PointerToRawData) +
                sections[i].SizeOfRawData > bytes.size()) return false;
            const std::uintptr_t start = 0x00400000u + sections[i].VirtualAddress;
            const std::uintptr_t end = start + sections[i].SizeOfRawData;
            for (const auto page : PAGES)
            {
                const auto copyStart = (std::max)(start, page);
                const auto copyEnd = (std::min)(end, page + 0x10000u);
                if (copyStart < copyEnd)
                    std::memcpy(reinterpret_cast<void*>(copyStart),
                        bytes.data() + sections[i].PointerToRawData + copyStart - start,
                        copyEnd - copyStart);
            }
        }
        return true;
    }

    void PatchJump(std::uintptr_t address, const void* target)
    {
        auto* bytes = reinterpret_cast<std::uint8_t*>(address);
        bytes[0] = 0xE9;
        const auto displacement = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target) - address - 5);
        std::memcpy(bytes + 1, &displacement, 4);
        FlushInstructionCache(GetCurrentProcess(), bytes, 5);
    }
    // 替代原生拒绝反馈副作用；保留其成功/失败返回与原生pop ecx栈契约。
    __declspec(naked) void RejectFeedback()
    {
        __asm {
            mov g_NativeRejected, 1
            xor al, al
            pop ecx
            ret
        }
    }
    __declspec(naked) void EntryWrapper()
    {
        __asm {
            pushfd
            inc g_WrapperBefore
            popfd
            call dword ptr [g_OriginalEntry]
            pushfd
            inc g_WrapperAfter
            cmp g_ThirdPartyDeny, 0
            je leaveResult
            mov al, 0
        leaveResult:
            popfd
            ret
        }
    }
    bool AddExistingEntryChain()
    {
        auto* original = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 32,
            MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!original) return false;
        std::memcpy(original, reinterpret_cast<const void*>(ENTRY_ADDRESS), ENTRY_BYTES.size());
        original[6] = 0xE9;
        const auto displacement = static_cast<std::int32_t>(ENTRY_ADDRESS + 6
            - reinterpret_cast<std::uintptr_t>(original + 11));
        std::memcpy(original + 7, &displacement, 4);
        g_OriginalEntry = original;
        PatchJump(ENTRY_ADDRESS, &EntryWrapper);
        return true;
    }
    void SetCount(int count)
    {
        *reinterpret_cast<void***>(0x00A8ECBCu) = g_ObjectPointers.data();
        *reinterpret_cast<int*>(0x00A8ECC8u) = count;
    }
    void Mode(int mode) { *reinterpret_cast<std::uint32_t*>(0x00A8B238u) = static_cast<std::uint32_t>(mode); }
    bool CallEntry()
    {
        using EntryFunction = bool(__cdecl*)();
        g_NativeRejected = 0;
        return reinterpret_cast<EntryFunction>(ENTRY_ADDRESS)();
    }
    void CheckOriginal(int count)
    {
        SetCount(count);
        Check(CallEntry() == (count <= 128), "fallback did not preserve the real native128 policy");
        Check((g_NativeRejected != 0) == (count > 128), "fallback did not follow the native cmp branch");
    }
    // 单独构造真实cmp处的保存栈，跳入已安装的生产中段Hook，成功后运行真实epilogue。
    __declspec(naked) void ProbeCompare()
    {
        __asm {
            pushad
            mov eax, 12345678h
            mov ebx, 10203040h
            mov ecx, 11223344h
            mov edx, 55667788h
            mov esi, 90ABCDEFh
            mov edi, 0FEDCBA9h
            mov ebp, 0A1B2C3D4h
            mov g_StackBefore, esp
            push offset returned
            push ecx
            push ebx
            push ebp
            mov ebp, g_ProbeCount
            cmp ecx, 11223344h
            jmp dword ptr [g_CompareAddress]
        returned:
            mov g_Eax, eax
            mov g_Ebx, ebx
            mov g_Ecx, ecx
            mov g_Edx, edx
            mov g_Esi, esi
            mov g_Edi, edi
            mov g_Ebp, ebp
            mov g_StackAfter, esp
            pushfd
            pop eax
            mov g_Flags, eax
            popad
            ret
        }
    }
    void CheckProbe(bool accepted, bool enabledFlags)
    {
        g_NativeRejected = 0;
        ProbeCompare();
        Check(g_Eax == (accepted ? 0x12345601u : 0x12345600u) && g_Ebx == 0x10203040u
            && g_Ecx == 0x11223344u && g_Edx == 0x55667788u
            && g_Esi == 0x90ABCDEFu && g_Edi == 0x0FEDCBA9u && g_Ebp == 0xA1B2C3D4u,
            "middle gate or native epilogue changed saved registers/return ABI");
        Check(g_StackAfter == g_StackBefore, "middle gate did not balance the real saved stack");
        if (enabledFlags) Check((g_Flags & 0x8D5u) == 0x44u,
            "enabled middle gate failed to preserve seeded arithmetic EFLAGS");
    }
    DWORD WINAPI ForeignThread(void*)
    {
        const auto before = g_WrapperBefore;
        const bool accepted = CallEntry();
        return (!accepted && g_NativeRejected && g_WrapperBefore >= before) ? 0 : 1;
    }
    DWORD WINAPI DisableThread(void*) { capacity::Disable(); return capacity::IsEnabled() ? 1 : 0; }
    void CheckLengths()
    {
        Check(std::memcmp(reinterpret_cast<const void*>(EARLY_LENGTH_ADDRESS), EARLY_LENGTH_BYTES.data(),
            EARLY_LENGTH_BYTES.size()) == 0, "native15 length check was modified");
        Check(std::memcmp(reinterpret_cast<const void*>(TOKEN_LENGTH_ADDRESS), TOKEN_LENGTH_BYTES.data(),
            TOKEN_LENGTH_BYTES.size()) == 0, "native16 length check was modified");
    }
}
int wmain(int argc, wchar_t** argv)
{
    if (argc < 2 || !MapGameImage(argv[1])) { std::fprintf(stderr,"matching PE32 fixture required\n"); return 1; }
    const bool chain = argc > 2 && std::wcscmp(argv[2], L"--entry-chain") == 0;
    const bool conflict = argc > 2 && std::wcscmp(argv[2], L"--compare-conflict") == 0;
    const bool tailConflict = argc > 2 && std::wcscmp(argv[2], L"--tail-conflict") == 0;
    Check(std::memcmp(reinterpret_cast<void*>(ENTRY_ADDRESS), ENTRY_BYTES.data(), ENTRY_BYTES.size()) == 0,
        "actual native participant entry differs");
    CheckLengths();
    PatchJump(NATIVE_REJECT_ADDRESS, &RejectFeedback);
    *reinterpret_cast<void**>(0xA83D4C) = g_House;
    *reinterpret_cast<void**>(0xAC1198) = nullptr;
    *reinterpret_cast<int*>(g_House + 0x30) = 0;
    *reinterpret_cast<int*>(0xAC4B84) = 0;
    *reinterpret_cast<std::uint8_t*>(0xAC4CF4) = 1;
    *reinterpret_cast<std::uint32_t*>(profile::REPLAY_FLAGS_ADDRESS) = 0;
    for (std::size_t i = 0; i < g_Objects.size(); ++i)
    { g_Objects[i][0x14] = 1; g_ObjectPointers[i] = g_Objects[i].data(); }
    Mode(5);
    std::string error;
    g_SessionReady = false;
    Check(!capacity::Install(error) && !capacity::IsEnabled(), "installation accepted an unready session");
    g_SessionReady = true;
    if (chain) Check(AddExistingEntryChain(), "existing entry wrapper setup failed");
    if (tailConflict) {
        auto* restore = reinterpret_cast<std::uint8_t*>(profile::RESTORE_REGISTERS_ADDRESS);
        restore[0] = 0x90;
        Check(!capacity::Install(error) && !capacity::IsEnabled(), "incompatible register restore was accepted");
        restore[0] = 0x5D;
        auto* accept = reinterpret_cast<std::uint8_t*>(profile::ACCEPT_CONTINUE);
        accept[0] = 0xE9;
        Check(!capacity::Install(error) && !capacity::IsEnabled(), "incompatible success epilogue was accepted");
        Check(std::memcmp(reinterpret_cast<void*>(profile::CAPACITY_COMPARE_ADDRESS),
            profile::CAPACITY_COMPARE_BYTES.data(), profile::CAPACITY_COMPARE_BYTES.size()) == 0,
            "tail preflight failure installed a partial compare hook");
        CheckLengths();
        return g_Failures ? 1 : 0;
    }
    if (conflict) {
        auto* compare = reinterpret_cast<std::uint8_t*>(profile::CAPACITY_COMPARE_ADDRESS);
        compare[0] = 0xE9;
        Check(!capacity::Install(error) && !capacity::IsEnabled(), "existing middle compare hook was overwritten");
        Check(compare[0] == 0xE9, "middle compare failure changed third-party bytes");
        CheckLengths();
        return g_Failures ? 1 : 0;
    }
    Check(capacity::Install(error) && capacity::IsEnabled(), "production planning participant installation failed");
    if (!capacity::IsEnabled()) { std::fprintf(stderr,"%s\n",error.c_str()); return 1; }
    for (const int mode : {0,5,3,4}) {
        Mode(mode);
        for (const int count : {128,129,1000}) {
            SetCount(count);
            const auto before = g_WrapperBefore, after = g_WrapperAfter;
            Check(CallEntry() && !g_NativeRejected, "campaign/skirmish/LAN/internet participant expansion failed");
            if (chain) Check(g_WrapperBefore == before + 1 && g_WrapperAfter == after + 1,
                "enabled middle hook bypassed the existing entry wrapper");
            g_ProbeCount = count;
            CheckProbe(true, true);
        }
    }
    SetCount(1000);
    if (chain) {
        g_ThirdPartyDeny = 1;
        Check(!CallEntry(), "plugin swallowed third-party post-return policy");
        g_ThirdPartyDeny = 0;
    }
    capacity::Disable();
    Check(!capacity::IsEnabled(), "Disable retained the logical enable state");
    CheckOriginal(128); CheckOriginal(129); CheckOriginal(1000);
    g_ProbeCount = 129; CheckProbe(false, false);
    Check(capacity::Install(error), "reactivation of resident middle hook failed");
    Mode(5);
    g_PlanningTransportReady = false;
    CheckOriginal(129);
    g_PlanningTransportReady = true;
    *reinterpret_cast<int*>(g_House + 0x30) = -1;
    CheckOriginal(129);
    *reinterpret_cast<int*>(g_House + 0x30) = 24;
    CheckOriginal(129);
    *reinterpret_cast<int*>(g_House + 0x30) = 0;
    *reinterpret_cast<std::uint32_t*>(profile::REPLAY_FLAGS_ADDRESS) = 2;
    CheckOriginal(129);
    *reinterpret_cast<std::uint32_t*>(profile::REPLAY_FLAGS_ADDRESS) = 0;
    *reinterpret_cast<void**>(0xAC1198) = g_House;
    CheckOriginal(129);
    *reinterpret_cast<void**>(0xAC1198) = nullptr;
    for (const int mode : {1,2,6,99}) { Mode(mode); CheckOriginal(129); }
    Mode(5);
    g_SessionReady = false; CheckOriginal(129); g_SessionReady = true;
    g_FaultSessionQuery = true; CheckOriginal(129); g_FaultSessionQuery = false;
    // 无player仅从中段探测；避免执行原生主体前半段对House指针的固有解引用。
    *reinterpret_cast<void**>(0xA83D4C) = nullptr;
    CheckProbe(false, false);
    *reinterpret_cast<void**>(0xA83D4C) = g_House;
    DWORD previous = 0, ignored = 0;
    const bool protectedReplay = VirtualProtect(reinterpret_cast<void*>(profile::REPLAY_FLAGS_ADDRESS),
        sizeof(std::uint32_t), PAGE_NOACCESS, &previous) != FALSE;
    Check(protectedReplay, "cannot simulate failed replay-flag read");
    if (protectedReplay) {
        CheckProbe(false, false);
        VirtualProtect(reinterpret_cast<void*>(profile::REPLAY_FLAGS_ADDRESS), sizeof(std::uint32_t), previous, &ignored);
    }
    SetCount(129);
    DWORD id = 0, result = 1;
    auto thread = CreateThread(nullptr, 0, ForeignThread, nullptr, 0, &id);
    Check(thread != nullptr, "foreign-thread fixture creation failed");
    if (thread) { WaitForSingleObject(thread, INFINITE); GetExitCodeThread(thread, &result); CloseHandle(thread); }
    Check(result == 0, "foreign thread bypassed native participant policy");
    thread = CreateThread(nullptr, 0, DisableThread, nullptr, 0, &id);
    Check(thread != nullptr, "Disable-thread fixture creation failed");
    if (thread) { WaitForSingleObject(thread, INFINITE); GetExitCodeThread(thread, &result); CloseHandle(thread); }
    Check(result == 0 && !capacity::IsEnabled(), "cross-thread Disable failed");
    CheckOriginal(129);
    Check(capacity::Install(error) && CallEntry(), "resident hook reactivation failed");
    if (chain) Check(g_WrapperBefore == g_WrapperAfter && g_WrapperBefore > 20,
        "existing entry chain lost pre/post callbacks in fallback scenarios");
    CheckLengths();
    std::printf("planning participant native fixture: %d failure(s)\n",g_Failures);
    return g_Failures ? 1 : 0;
}
