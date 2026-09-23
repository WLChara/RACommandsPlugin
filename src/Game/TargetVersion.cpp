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
            HANDLE File = INVALID_HANDLE_VALUE;
            BCRYPT_ALG_HANDLE Algorithm = nullptr;
            BCRYPT_HASH_HANDLE Hash = nullptr;

            ~HashResources()
            {
                if (Hash)
                {
                    BCryptDestroyHash(Hash);
                }
                if (Algorithm)
                {
                    BCryptCloseAlgorithmProvider(Algorithm, 0);
                }
                if (File != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(File);
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

        bool GetCurrentExeSha256(std::array<std::uint8_t, 32>& outDigest)
        {
            std::wstring path;
            if (!GetCurrentExePath(path))
            {
                return false;
            }

            HashResources resources;
            resources.File = CreateFileW(path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (resources.File == INVALID_HANDLE_VALUE ||
                BCryptOpenAlgorithmProvider(&resources.Algorithm, BCRYPT_SHA256_ALGORITHM,
                    nullptr, 0) < 0)
            {
                return false;
            }

            DWORD objectBytes = 0;
            DWORD propertyBytes = 0;
            if (BCryptGetProperty(resources.Algorithm, BCRYPT_OBJECT_LENGTH,
                    reinterpret_cast<PUCHAR>(&objectBytes), sizeof(objectBytes),
                    &propertyBytes, 0) < 0 || objectBytes == 0)
            {
                return false;
            }

            std::vector<std::uint8_t> hashObject(objectBytes);
            if (BCryptCreateHash(resources.Algorithm, &resources.Hash,
                    hashObject.data(), objectBytes, nullptr, 0, 0) < 0)
            {
                return false;
            }

            // 分块读取，避免为验证目标版本而将整个 EXE 装入内存。
            std::array<std::uint8_t, HASH_READ_BUFFER_BYTES> buffer{};
            for (;;)
            {
                DWORD readBytes = 0;
                if (!ReadFile(resources.File, buffer.data(),
                        static_cast<DWORD>(buffer.size()), &readBytes, nullptr))
                {
                    return false;
                }
                if (readBytes == 0)
                {
                    break;
                }
                if (BCryptHashData(resources.Hash, buffer.data(), readBytes, 0) < 0)
                {
                    return false;
                }
            }

            return BCryptFinishHash(resources.Hash, outDigest.data(),
                static_cast<ULONG>(outDigest.size()), 0) >= 0;
        }
    }

    bool IsSupportedHost(std::string& outError)
    {
        std::array<std::uint8_t, 32> digest{};
        if (!GetCurrentExeSha256(digest))
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
}
