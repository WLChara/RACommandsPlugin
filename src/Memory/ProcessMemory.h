#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace ra_commands::memory
{
    /**
     * 从当前进程地址读取目标游戏数据。
     * 调用方负责从受支持版本的证据中提供字段偏移；读取失败时输出不变。
     */
    bool TryReadMemory(std::uintptr_t address, void* outValue, std::size_t sizeBytes);

    template<typename T>
    bool TryReadField(std::uintptr_t objectAddress, std::size_t fieldOffset, T& outValue)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        if (objectAddress == 0 ||
            fieldOffset > (std::numeric_limits<std::uintptr_t>::max)() - objectAddress)
        {
            return false;
        }
        return TryReadMemory(objectAddress + fieldOffset, &outValue, sizeof(T));
    }
}
