#include "Bootstrap/SyringeCompatibility.h"
#include "Game/TargetVersion.h"

#include <Syringe.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace
{
    // 原样复用 SDK 的握手 ABI；Syringe 按 x86 cdecl 调用未修饰的导出名。
    static_assert(sizeof(SyringeHandshakeInfo) == 32);
    static_assert(offsetof(SyringeHandshakeInfo, Message) == 28);

    struct alignas(16) SyringeLoadDeclaration
    {
        std::uint32_t mHookAddress;
        std::uint32_t mHookSize;
        const char* mExportName;
    };
    static_assert(sizeof(SyringeLoadDeclaration) == 16);
    static_assert(offsetof(SyringeLoadDeclaration, mExportName) == 8);
}

// Syringe 需要有效的导出名才会给游戏装载 DLL。零地址记录只触发 LoadLibrary 和
// GetProcAddress；其 patch 阶段跳过 nullptr，不会调用 Initialize，也不安装 Syringe Hook。
// 强制保留外部 C 符号，确保 Release / LTCG / OPT:REF 仍输出该节及字符串重定位。
#pragma section(".syhks00", read)
extern "C" __declspec(allocate(".syhks00")) const SyringeLoadDeclaration
    g_RACommandsPluginSyringeLoad = {0, 0, "RACommandsPlugin_Initialize"};
#pragma comment(linker, "/include:_g_RACommandsPluginSyringeLoad")

namespace ra_commands::bootstrap
{
    HRESULT HandleSyringeHandshake(SyringeHandshakeInfo* info) noexcept
    {
        try
        {
            if (!info || info->cbSize < static_cast<int>(sizeof(SyringeHandshakeInfo)))
            {
                return S_FALSE;
            }
            if (info->exeFilesize != game::SUPPORTED_EXE_FILE_SIZE ||
                info->exeTimestamp != game::SUPPORTED_EXE_TIMESTAMP ||
                info->exeCRC != game::SUPPORTED_EXE_CRC32)
            {
                return S_FALSE;
            }

            // Syringe 在游戏目录扫描 DLL 并完成握手；磁盘 SHA 是最终版本门禁。
            // 该路径故意不使用当前进程映像：此时宿主是 Syringe，而非 gamemd.exe。
            std::string error;
            if (!game::IsSupportedExecutableFile(L"gamemd.exe", error))
            {
                return S_FALSE;
            }
            constexpr char MESSAGE[] = "RACommandsPlugin: load only; MinHook initializes after native commands";
            if (info->Message && info->cchMessage > 0)
            {
                const auto capacity = static_cast<std::size_t>(info->cchMessage);
                const auto count = capacity - 1 < sizeof(MESSAGE) - 1 ?
                    capacity - 1 : sizeof(MESSAGE) - 1;
                std::memcpy(info->Message, MESSAGE, count);
                info->Message[count] = '\0';
            }
            return S_OK;
        }
        catch (...)
        {
            return S_FALSE;
        }
    }
}

extern "C" HRESULT __cdecl SyringeHandshake(SyringeHandshakeInfo* info)
{
    return ra_commands::bootstrap::HandleSyringeHandshake(info);
}
