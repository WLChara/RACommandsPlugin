#include "Memory/ProcessMemory.h"

#include <Windows.h>

namespace ra_commands::memory
{
    bool TryReadMemory(std::uintptr_t address, void* outValue, std::size_t sizeBytes)
    {
        if (address == 0 || outValue == nullptr || sizeBytes == 0)
        {
            return false;
        }

        SIZE_T readBytes = 0;
        return ReadProcessMemory(
            GetCurrentProcess(),
            reinterpret_cast<const void*>(address),
            outValue,
            sizeBytes,
            &readBytes) != FALSE && readBytes == sizeBytes;
    }
}
