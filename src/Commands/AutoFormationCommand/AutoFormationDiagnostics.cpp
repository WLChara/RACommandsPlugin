#include "Commands/AutoFormationCommand/AutoFormationDiagnostics.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <string>

namespace ra_commands::game
{
    void TraceAutoFormation(std::string_view event) noexcept
    {
        try
        {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            std::array<char, 48> timestamp{};
            std::snprintf(timestamp.data(), timestamp.size(),
                "%04u-%02u-%02u %02u:%02u:%02u ", now.wYear, now.wMonth, now.wDay,
                now.wHour, now.wMinute, now.wSecond);
            const std::string line = std::string(timestamp.data()) + "[formation] " +
                std::string(event) + "\r\n";
            OutputDebugStringA(line.c_str());

            HMODULE module = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&TraceAutoFormation), &module))
            {
                return;
            }
            std::array<wchar_t, 32768> path{};
            const auto length = GetModuleFileNameW(module, path.data(),
                static_cast<DWORD>(path.size()));
            if (length == 0 || length >= path.size()) { return; }
            std::wstring directory(path.data(), length);
            const auto slash = directory.find_last_of(L"\\/");
            if (slash == std::wstring::npos) { return; }
            directory.resize(slash + 1);
            directory += L"RACommandsPlugin.formation.log";
            const auto file = CreateFileW(directory.c_str(), FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) { return; }
            DWORD written = 0;
            (void)WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
            CloseHandle(file);
        }
        catch (...)
        {
            // 诊断写入失败不影响列队状态或游戏原生任务。
        }
    }
}
