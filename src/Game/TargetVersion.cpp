#include "Game/TargetVersion.h"

#include <Windows.h>
#include <bcrypt.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ra_commands::game
{
    namespace
    {
        // 与 docs/ida-evidence.md 的 gamemd.exe 样本保持一致。
        constexpr std::array<std::uint8_t, 32> SUPPORTED_EXE_SHA256 = {
            0x7C, 0xD0, 0x05, 0xD2, 0x63, 0xFD, 0xE2, 0x03,
            0xD9, 0xC8, 0x45, 0x48, 0x20, 0x0A, 0x05, 0x7A,
            0x8D, 0xF6, 0x1D, 0x72, 0x4D, 0xA3, 0xC6, 0xBD,
            0x1E, 0x52, 0x1E, 0xEB, 0x61, 0xCD, 0x07, 0x47
        };
        // 防止异常路径令缓冲区无限扩容；达到此界限时拒绝继续尝试。
        constexpr std::size_t MAX_EXE_PATH_CHARS = 32768;
        constexpr std::size_t HASH_READ_BUFFER_BYTES = 65536;

        struct HashResources
        {
            HANDLE mFile = INVALID_HANDLE_VALUE;
            BCRYPT_ALG_HANDLE mAlgorithm = nullptr;
            BCRYPT_HASH_HANDLE mHash = nullptr;

            ~HashResources()
            {
                if (mHash)
                {
                    BCryptDestroyHash(mHash);
                }
                if (mAlgorithm)
                {
                    BCryptCloseAlgorithmProvider(mAlgorithm, 0);
                }
                if (mFile != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(mFile);
                }
            }
        };

        bool GetCurrentExePath(std::wstring& outPath)
        {
            std::wstring path(MAX_PATH, L'\0');
            for (;;)
            {
                const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                    static_cast<DWORD>(path.size()));
                if (length == 0)
                {
                    return false;
                }
                if (length < path.size() - 1)
                {
                    path.resize(length);
                    outPath = std::move(path);
                    return true;
                }
                if (path.size() >= MAX_EXE_PATH_CHARS)
                {
                    return false;
                }
                path.resize(path.size() * 2);
            }
        }

        bool GetFileSha256(const wchar_t* path, std::array<std::uint8_t, 32>& outDigest)
        {
            HashResources resources;
            resources.mFile = CreateFileW(path, GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (resources.mFile == INVALID_HANDLE_VALUE ||
                BCryptOpenAlgorithmProvider(&resources.mAlgorithm, BCRYPT_SHA256_ALGORITHM,
                    nullptr, 0) < 0)
            {
                return false;
            }

            DWORD objectBytes = 0;
            DWORD propertyBytes = 0;
            if (BCryptGetProperty(resources.mAlgorithm, BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
                    &propertyBytes, 0) < 0 || objectBytes == 0)
            {
                return false;
            }

            std::vector<std::uint8_t> hashObject(objectBytes);
            if (BCryptCreateHash(resources.mAlgorithm, &resources.mHash,
                    hashObject.data(), objectBytes, nullptr, 0, 0) < 0)
            {
                return false;
            }

            // 分块读取，避免为验证目标版本而将整个 EXE 装入内存。
            std::array<std::uint8_t, HASH_READ_BUFFER_BYTES> buffer{};
            for (;;)
            {
                DWORD readBytes = 0;
                if (!ReadFile(resources.mFile, buffer.data(),
                        static_cast<DWORD>(buffer.size()), &readBytes, nullptr))
                {
                    return false;
                }
                if (readBytes == 0)
                {
                    break;
                }
                if (BCryptHashData(resources.mHash, buffer.data(), readBytes, 0) < 0)
                {
                    return false;
                }
            }

            return BCryptFinishHash(resources.mHash, outDigest.data(),
                static_cast<ULONG>(outDigest.size()), 0) >= 0;
        }
    }

    bool IsCandidateGameHost() noexcept
    {
        const auto* image = reinterpret_cast<const std::byte*>(GetModuleHandleW(nullptr));
        if (!image)
        {
            return false;
        }
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
        {
            return false;
        }
        // 主模块由 Windows loader 验证并映射；此处不读取文件、不获取游戏对象。
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE &&
            nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
            nt->FileHeader.TimeDateStamp == SUPPORTED_EXE_TIMESTAMP &&
            nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
            nt->OptionalHeader.ImageBase == 0x400000;
    }

    bool IsSupportedExecutableFile(const wchar_t* path, std::string& outError)
    {
        std::array<std::uint8_t, 32> digest{};
        if (!path || !GetFileSha256(path, digest))
        {
            outError = "cannot hash host executable";
            return false;
        }
        if (digest != SUPPORTED_EXE_SHA256)
        {
            outError = "unsupported gamemd.exe build (SHA-256 mismatch)";
            return false;
        }
        outError.clear();
        return true;
    }

    bool IsSupportedHost(std::string& outError)
    {
        struct HostVerificationResult
        {
            bool mIsSupported = false;
            std::string mError;
        };
        // 函数局部静态初始化串行化两个入口；拒绝结果也缓存，绝不在轮询中重读 EXE。
        static const HostVerificationResult result = []
        {
            HostVerificationResult verification;
            std::wstring path;
            if (!GetCurrentExePath(path))
            {
                verification.mError = "cannot locate host executable";
                return verification;
            }
            verification.mIsSupported = IsSupportedExecutableFile(path.c_str(), verification.mError);
            return verification;
        }();
        outError = result.mError;
        return result.mIsSupported;
    }

    bool IsNativeCommandTableReady() noexcept
    {
        // 目标 EXE 的 CommandClass::Array 初始全零。IDA 已确认 0x532150 在
        // Init_Game (0x52CAE4) 内写入首条原生命令；只有 EXE 入口已运行才可满足此门槛。
        // Syringe 在恢复 EXE 入口前已完成统一 patch，避免本插件先接入再被其覆盖。
        constexpr std::uintptr_t COMMAND_ARRAY_ADDRESS = 0x87F658;
        constexpr std::int32_t MAX_COMMAND_CAPACITY = 32768;
        struct NativeCommandVectorSnapshot
        {
            std::uint32_t mVtable;
            std::uint32_t mItems;
            std::int32_t mCapacity;
            std::uint8_t mIsInitialized;
            std::uint8_t mIsAllocated;
            std::uint16_t mPadding;
            std::int32_t mCount;
            std::int32_t mCapacityIncrement;
        };
        static_assert(sizeof(NativeCommandVectorSnapshot) == 24);
        static_assert(offsetof(NativeCommandVectorSnapshot, mItems) == 4);
        static_assert(offsetof(NativeCommandVectorSnapshot, mCount) == 16);

        NativeCommandVectorSnapshot commands{};
        SIZE_T readBytes = 0;
        // 工作线程只复制状态，不调用 SDK 方法、不解引用可变 Items 指针。
        return ReadProcessMemory(GetCurrentProcess(),
                reinterpret_cast<const void*>(COMMAND_ARRAY_ADDRESS), &commands,
                sizeof(commands), &readBytes) && readBytes == sizeof(commands) &&
            commands.mItems != 0 && commands.mIsInitialized != 0 &&
            commands.mCount > 0 && commands.mCount <= commands.mCapacity &&
            commands.mCapacity <= MAX_COMMAND_CAPACITY;
    }
}
