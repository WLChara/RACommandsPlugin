#pragma once

#include <string>
#include <cstdint>

namespace ra_commands::game
{
    // Syringe 的目标身份字段对应当前唯一支持的 gamemd.exe 磁盘样本。
    inline constexpr std::uint32_t SUPPORTED_EXE_FILE_SIZE = 4'816'864;
    inline constexpr std::uint32_t SUPPORTED_EXE_TIMESTAMP = 0x3BDF544E;
    inline constexpr std::uint32_t SUPPORTED_EXE_CRC32 = 0x1B499086;

    /** 只检查已映射 PE 头，允许在 DllMain 中使用；不代替 SHA-256 门禁。 */
    bool IsCandidateGameHost() noexcept;
    /** 只读磁盘文件，不访问游戏虚拟地址；path 不得为 nullptr。 */
    bool IsSupportedExecutableFile(const wchar_t* path, std::string& outError);
    // YRpp 的固定地址只允许用于 docs/ida-evidence.md 记录的目标样本。
    // 当前进程的验证结果只计算一次，自动入口与显式入口共用该结果。
    bool IsSupportedHost(std::string& outError);

    /** 调用前必须通过 IsSupportedHost；只读已验证游戏的原生命令初始化状态。 */
    bool IsNativeCommandTableReady() noexcept;
}
