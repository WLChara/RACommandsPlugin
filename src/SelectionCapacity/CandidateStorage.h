#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace ra_commands::selection_capacity
{
    struct CandidateEntry
    {
        std::uint32_t mObject;
        int mX;
        int mY;
    };
    static_assert(sizeof(CandidateEntry) == 12);

    /** 单机候选表的唯一内存所有者。发布新地址成功后才释放旧表；调用方保证没有活跃 reader。 */
    class CandidateStorage
    {
    public:
        enum class PublishResult { Completed, Rejected, Uncertain };
        using PublishFn = PublishResult(*)(void*, CandidateEntry*, std::size_t);
        // 原生索引使用 32 位有符号整数，字节偏移也必须可表示。
        static constexpr std::size_t MAX_CAPACITY =
            static_cast<std::size_t>((std::numeric_limits<int>::max)()) / sizeof(CandidateEntry);

        bool TryReserve(std::size_t required, const CandidateEntry* current,
            std::size_t count, std::size_t currentCapacity, PublishFn publish, void* context)
        {
            if (mUnpublished || !current || !publish || count > currentCapacity || count > required
                || currentCapacity > MAX_CAPACITY || required > MAX_CAPACITY)
                return false;
            if (required <= currentCapacity)
                return true;
            const std::size_t doubled = currentCapacity <= MAX_CAPACITY / 2
                ? currentCapacity * 2 : MAX_CAPACITY;
            const std::size_t capacity = (std::max)(required, doubled);
            try
            {
                // 清零未使用项，原生移除路径会扫描整个 capacity，而非仅扫描 count。
                std::unique_ptr<CandidateEntry[]> replacement(new CandidateEntry[capacity]{});
                std::memcpy(replacement.get(), current, count * sizeof(CandidateEntry));
                const PublishResult result = publish(context, replacement.get(), capacity);
                if (result == PublishResult::Uncertain)
                {
                    // 回滚不完整时引擎可能仍引用新旧两表，保留二者直到引用被可靠恢复。
                    mUnpublished = std::move(replacement);
                    return false;
                }
                if (result != PublishResult::Completed)
                    return false;
                mEntries = std::move(replacement);
                mCapacity = capacity;
                return true;
            }
            catch (const std::bad_alloc&)
            {
                return false;
            }
        }

        /** 只能在原生引用已经切回固定表后调用。 */
        void Reset() { mUnpublished.reset(); mEntries.reset(); mCapacity = 0; }
        const CandidateEntry* Buffer() const { return mEntries.get(); }
        std::size_t Capacity() const { return mCapacity; }

    private:
        std::unique_ptr<CandidateEntry[]> mEntries;
        std::unique_ptr<CandidateEntry[]> mUnpublished;
        std::size_t mCapacity = 0;
    };
}
