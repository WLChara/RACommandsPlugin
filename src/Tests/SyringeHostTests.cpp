// 独立的 x86 宿主，不能链接到纯逻辑测试主程序；由 test-syringe-host.ps1 编译。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <TlHelp32.h>
#include <winternl.h>
#include <Syringe.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
    using HandshakeFunction = HRESULT(__cdecl*)(SyringeHandshakeInfo*);
    using IsReadyFunction = BOOL(__stdcall*)();
    constexpr DWORD TARGET_FILE_SIZE = 4'816'864;
    constexpr DWORD TARGET_TIMESTAMP = 0x3BDF544E;
    constexpr DWORD TARGET_CRC32 = 0x1B499086;

    bool CheckNoPluginWorker(std::uintptr_t pluginBase, std::uint32_t imageBytes)
    {
        using QueryThreadFunction = NTSTATUS(NTAPI*)(HANDLE, THREADINFOCLASS, PVOID, ULONG, PULONG);
        const auto queryThread = reinterpret_cast<QueryThreadFunction>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread"));
        if (!queryThread)
        {
            return false;
        }
        // Microsoft 文档规定该信息类返回 Win32 线程起始地址；只用于隔离测试。
        // Windows 并行 loader 本身可能新增线程，因此不能用进程总线程数断言插件行为。
        // https://learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntqueryinformationthread
        constexpr auto THREAD_START_ADDRESS = static_cast<THREADINFOCLASS>(9);
        const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        bool hasNoPluginWorker = true;
        THREADENTRY32 entry{};
        entry.dwSize = sizeof(entry);
        if (Thread32First(snapshot, &entry))
        {
            do
            {
                if (entry.th32OwnerProcessID != GetCurrentProcessId())
                {
                    continue;
                }
                const HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, entry.th32ThreadID);
                if (!thread)
                {
                    // 快照后已退出的系统 loader 线程没有可查询句柄。
                    hasNoPluginWorker = GetLastError() == ERROR_INVALID_PARAMETER;
                    if (!hasNoPluginWorker)
                    {
                        break;
                    }
                    continue;
                }
                void* start = nullptr;
                const NTSTATUS result = queryThread(thread, THREAD_START_ADDRESS,
                    &start, sizeof(start), nullptr);
                CloseHandle(thread);
                const auto startAddress = reinterpret_cast<std::uintptr_t>(start);
                if (result < 0 || (startAddress >= pluginBase &&
                    startAddress < pluginBase + imageBytes))
                {
                    std::cerr << "plugin worker or query failure for thread=" <<
                        entry.th32ThreadID << '\n';
                    hasNoPluginWorker = false;
                    break;
                }
            }
            while (Thread32Next(snapshot, &entry));
        }
        else
        {
            hasNoPluginWorker = false;
        }
        CloseHandle(snapshot);
        return hasNoPluginWorker;
    }

    std::vector<char> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

    using DirectorySnapshot = std::map<std::filesystem::path, std::uintmax_t>;

    DirectorySnapshot CaptureDirectory(const std::filesystem::path& path)
    {
        DirectorySnapshot snapshot;
        for (const auto& file : std::filesystem::directory_iterator(path))
        {
            if (file.is_regular_file())
            {
                snapshot.emplace(file.path().filename(), file.file_size());
            }
        }
        return snapshot;
    }

    bool Expect(HRESULT actual, HRESULT expected, const char* description)
    {
        if (actual == expected)
        {
            return true;
        }
        std::cerr << description << " returned 0x" << std::hex << actual <<
            ", expected 0x" << expected << '\n';
        return false;
    }
}

int wmain(int argc, wchar_t** argv)
{
    static_assert(sizeof(void*) == 4, "Syringe host must be x86");
    static_assert(sizeof(SyringeHandshakeInfo) == 32);
    if (argc != 2 && argc != 3)
    {
        return 2;
    }
    const bool shouldRejectTarget = argc == 3 && std::wstring(argv[2]) == L"--reject-target";
    const auto pluginPath = std::filesystem::absolute(argv[1]);
    const auto runtimeLogPath = pluginPath.parent_path() / L"RACommandsPlugin.runtime.log";
    const bool hadRuntimeLog = std::filesystem::exists(runtimeLogPath);
    const auto runtimeLogBytes = ReadBytes(runtimeLogPath);
    const auto directoryBefore = CaptureDirectory(std::filesystem::current_path());
    std::uintptr_t lastPluginBase = 0;
    std::uint32_t lastImageBytes = 0;

    // 反复装载、握手、卸载暴露宿主中误建 worker、PIN 或静态状态污染。
    for (int iteration = 0; iteration < 4; ++iteration)
    {
        const HMODULE module = LoadLibraryW(pluginPath.c_str());
        if (!module)
        {
            std::cerr << "LoadLibrary failed: " << GetLastError() << '\n';
            return 4;
        }
        lastPluginBase = reinterpret_cast<std::uintptr_t>(module);
        const auto* image = reinterpret_cast<const std::byte*>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
        lastImageBytes = nt->OptionalHeader.SizeOfImage;
        if (!CheckNoPluginWorker(lastPluginBase, lastImageBytes))
        {
            return 3;
        }
        const auto handshake = reinterpret_cast<HandshakeFunction>(
            GetProcAddress(module, "SyringeHandshake"));
        const auto isReady = reinterpret_cast<IsReadyFunction>(
            GetProcAddress(module, "RACommandsPlugin_IsReady"));
        if (!handshake || !isReady ||
            !GetProcAddress(module, "RACommandsPlugin_Initialize") ||
            !GetProcAddress(module, "RACommandsPlugin_Shutdown"))
        {
            return 5;
        }

        std::array<char, 80> message{};
        SyringeHandshakeInfo info{};
        info.cbSize = sizeof(info);
        info.num_hooks = 1;
        info.exeFilesize = TARGET_FILE_SIZE;
        info.exeTimestamp = TARGET_TIMESTAMP;
        info.exeCRC = TARGET_CRC32;
        info.cchMessage = static_cast<int>(message.size());
        info.Message = message.data();

        if (!Expect(handshake(nullptr), S_FALSE, "null handshake"))
        {
            return 6;
        }
        info.cbSize = sizeof(info) - 1;
        if (!Expect(handshake(&info), S_FALSE, "short handshake ABI"))
        {
            return 7;
        }
        info.cbSize = sizeof(info);
        ++info.exeFilesize;
        if (!Expect(handshake(&info), S_FALSE, "wrong target size"))
        {
            return 8;
        }
        info.exeFilesize = TARGET_FILE_SIZE;
        info.exeTimestamp ^= 1;
        if (!Expect(handshake(&info), S_FALSE, "wrong target timestamp"))
        {
            return 9;
        }
        info.exeTimestamp = TARGET_TIMESTAMP;
        info.exeCRC ^= 1;
        if (!Expect(handshake(&info), S_FALSE, "wrong target CRC"))
        {
            return 10;
        }
        info.exeCRC = TARGET_CRC32;
        const HRESULT expected = shouldRejectTarget ? S_FALSE : S_OK;
        if (!Expect(handshake(&info), expected, "disk target SHA gate"))
        {
            return 11;
        }
        // 长度为一时只写终止符，长度为零或 nullptr 时不能写调用方内存。
        char oneChar = '!';
        info.Message = &oneChar;
        info.cchMessage = 1;
        if (!Expect(handshake(&info), expected, "one-byte message") ||
            (!shouldRejectTarget && oneChar != '\0'))
        {
            return 12;
        }
        oneChar = '!';
        info.cchMessage = 0;
        if (!Expect(handshake(&info), expected, "zero-byte message") || oneChar != '!')
        {
            return 13;
        }
        info.Message = nullptr;
        info.cchMessage = 256;
        if (!Expect(handshake(&info), expected, "no message buffer") || isReady())
        {
            return 14;
        }
        if (!CheckNoPluginWorker(lastPluginBase, lastImageBytes) || !FreeLibrary(module))
        {
            return 15;
        }
        if (GetModuleHandleW(pluginPath.c_str()))
        {
            std::cerr << "plugin remains loaded in handshake host\n";
            return 16;
        }
    }

    // 若旧实现误建 worker，给调度器机会启动后核对日志和目录副作用。
    Sleep(100);
    if (!CheckNoPluginWorker(lastPluginBase, lastImageBytes) ||
        hadRuntimeLog != std::filesystem::exists(runtimeLogPath) ||
        runtimeLogBytes != ReadBytes(runtimeLogPath) ||
        directoryBefore != CaptureDirectory(std::filesystem::current_path()))
    {
        std::cerr << "handshake host produced runtime side effects\n";
        return 17;
    }
    std::cout << "Syringe host LoadLibrary/handshake/FreeLibrary boundary passed\n";
    return 0;
}
