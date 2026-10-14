#include "Bootstrap/PluginRuntime.h"
#include "Bootstrap/PluginStartup.h"

#include <Windows.h>

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
        ra_commands::bootstrap::CancelPluginStartup();
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
        // 适配层只核对内存 PE 指纹；磁盘版本检查和就绪等待都在 loader lock 之外。
        return ra_commands::bootstrap::StartPluginFromDllMain() ? TRUE : FALSE;
    }
    return TRUE;
}
