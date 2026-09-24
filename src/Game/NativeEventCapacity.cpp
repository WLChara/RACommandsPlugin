#include "Game/NativeEventCapacity.h"

#include <Networking.h>

namespace ra_commands::game
{
    namespace
    {
        // 当前目标样本的 OutList 物理容量；更换目标版本须重新取证。
        constexpr int NATIVE_OUTLIST_CAPACITY = 128;
    }

    std::uint32_t GetNativeEventFreeSlots()
    {
        const int queued = Networking::LastEventIndex();
        return queued >= 0 && queued <= NATIVE_OUTLIST_CAPACITY
            ? static_cast<std::uint32_t>(NATIVE_OUTLIST_CAPACITY - queued)
            : 0;
    }
}
