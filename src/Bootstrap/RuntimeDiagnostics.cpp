#include "Bootstrap/RuntimeDiagnostics.h"

#include <Windows.h>

#include <array>
#include <cstdio>
#include <string>

namespace ra_commands::bootstrap
{
    namespace
    {
        std::wstring ModulePath(HMODULE module)
        {
            std::array<wchar_t, 32768> path{};
            const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
            return length > 0 && length < path.size() ? std::wstring(path.data(), length) : std::wstring{};
        }

        HMODULE DiagnosticsModule()
        {
            HMODULE module = nullptr;
            (void)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&TraceRuntime), &module);
            return module;
        }

        std::string Utf8(const std::wstring& value)
        {
            if (value.empty()) { return {}; }
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
            if (bytes <= 0) { return {}; }
            std::string result(bytes, '\0');
            (void)WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
                result.data(), bytes, nullptr, nullptr);
            return result;
        }
    }

    void TraceRuntime(std::string_view event) noexcept
    {
        try
        {
            SYSTEMTIME now{};
            GetLocalTime(&now);
            std::array<char, 96> prefix{};
            std::snprintf(prefix.data(), prefix.size(),
                "%04u-%02u-%02u %02u:%02u:%02u [runtime pid=%lu tid=%lu] ",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
                GetCurrentProcessId(), GetCurrentThreadId());
            const std::string line = std::string(prefix.data()) + std::string(event) + "\r\n";
            OutputDebugStringA(line.c_str());
            const auto module = DiagnosticsModule();
            if (!module) { return; }
            auto path = ModulePath(module);
            const auto slash = path.find_last_of(L"\\/");
            if (slash == std::wstring::npos) { return; }
            path.resize(slash + 1);
            path += L"RACommandsPlugin.runtime.log";
            const auto file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) { return; }
            DWORD written = 0;
            (void)WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
            CloseHandle(file);
        }
        catch (...) { /* 日志失败不改变命令注册或宿主版本门禁。 */ }
    }

    void TraceRuntimeStartup() noexcept
    {
        try
        {
            TraceRuntime("bootstrap-start build=" __DATE__ " " __TIME__ "; dll=" +
                Utf8(ModulePath(DiagnosticsModule())) + "; host=" + Utf8(ModulePath(nullptr)));
        }
        catch (...) { /* 同样保留无诊断时的启动行为。 */ }
    }
}
