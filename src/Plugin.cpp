#include "Bootstrap/PluginRuntime.h"

#include <Windows.h>

namespace
{
    DWORD WINAPI BootstrapThread(LPVOID)
    {
        try
        {
            ra_commands::bootstrap::AutoInitialize();
        }
        catch (...)
        {
            OutputDebugStringA("[RACommandsPlugin] bootstrap initialization failed\n");
        }
        return 0;
    }
}

// 保留旧导出用于诊断和兼容；DLL 注入后的启动不依赖此调用。
extern "C" BOOL __stdcall RACommandsPlugin_Initialize()
{
    try
    {
        return ra_commands::bootstrap::Initialize() ? TRUE : FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}

extern "C" BOOL __stdcall RACommandsPlugin_IsReady()
{
    try
    {
        return ra_commands::bootstrap::IsReady() ? TRUE : FALSE;
    }
    catch (...)
    {
        return FALSE;
    }
}

extern "C" void __stdcall RACommandsPlugin_Shutdown()
{
    try
    {
        ra_commands::bootstrap::Shutdown();
    }
    catch (...)
    {
        OutputDebugStringA("[RACommandsPlugin] shutdown failed\n");
    }
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        // 不等待此线程；它要在 DllMain 返回、loader lock 释放后才扫描映像。
        HANDLE bootstrapThread = CreateThread(nullptr, 0, BootstrapThread, nullptr, 0, nullptr);
        if (bootstrapThread == nullptr)
        {
            return FALSE;
        }
        CloseHandle(bootstrapThread);
    }
    return TRUE;
}
