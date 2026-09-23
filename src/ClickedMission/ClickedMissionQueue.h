#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>

namespace ra_commands::commands
{
    struct ClickedMissionIdentity
    {
        std::uintptr_t Address = 0;
        std::uint32_t UniqueId = 0;
        std::uint32_t Kind = 0;
        std::uint32_t Epoch = 0;

        bool operator==(const ClickedMissionIdentity&) const = default;
    };

    enum class ClickedMissionProducer
    {
        Unspecified,
        TeslaCharge,
    };

    struct ClickedMissionIntent
    {
        ClickedMissionIdentity Actor;
        std::int32_t Mission = 0;
        std::optional<ClickedMissionIdentity> Target;
        std::optional<ClickedMissionIdentity> TargetCell;
        std::optional<ClickedMissionIdentity> Nearest;
        ClickedMissionProducer Producer = ClickedMissionProducer::Unspecified;
        std::uint32_t Epoch = 0;
        std::uint32_t CreatedFrame = 0;
        std::int32_t FrameSendRate = 30;
    };

    enum class ClickedMissionEnqueueResult
    {
        Enqueued,
        Duplicate,
        Full,
        WrongEpoch,
    };

    struct ClickedMissionQueueCounters
    {
        std::uint64_t Enqueued = 0;
        std::uint64_t Duplicates = 0;
        std::uint64_t RejectedFull = 0;
        std::uint64_t RejectedWrongEpoch = 0;
        std::uint64_t Expired = 0;
        std::uint64_t RejectedByValidation = 0;
        std::uint64_t Attempted = 0;
        std::uint64_t ClearedByReset = 0;
        std::uint64_t Cancelled = 0;
    };

    struct ClickedMissionDrainResult
    {
        std::size_t Expired = 0;
        std::size_t RejectedByValidation = 0;
        std::size_t Attempted = 0;
        bool WasStoppedForCapacity = false;
    };

    /**
     * 游戏线程独占的待发意图队列；不保存可解引用的游戏对象指针。
     * 容量检查只是发送前背压，Attempted 不代表原生 OutList 已接收事件。
     */
    class ClickedMissionQueue final
    {
    public:
        // 一次普通 Enter 至多占一个原生槽，发送前留 13 槽以保留发送后的 12 槽。
        static constexpr std::uint32_t MINIMUM_NATIVE_FREE = 13;

        using NativeFreeCount = std::function<std::uint32_t()>;
        using ValidateIntent = std::function<bool(const ClickedMissionIntent&)>;
        using IssueIntent = std::function<void(const ClickedMissionIntent&)>;

        explicit ClickedMissionQueue(std::size_t maximumPending, std::uint32_t epoch = 0);

        // 仅对当前待发意图去重；重复提交不会延长原意图的到期帧。
        ClickedMissionEnqueueResult Enqueue(const ClickedMissionIntent& intent);

        /**
         * 在游戏线程逐项检查到期、有效性和原生剩余槽位。
         * 每次提交前重新读取槽数；Attempted 只统计提交回调次数，不是原生入队确认。
         */
        ClickedMissionDrainResult Drain(
            std::uint32_t currentFrame,
            const NativeFreeCount& getNativeFreeCount,
            const ValidateIntent& validate,
            const IssueIntent& issue);

        std::size_t CancelByProducer(ClickedMissionProducer producer);

        // 对局代次变化时清除待发意图，避免旧对象身份在新对局被复用。
        void Reset(std::uint32_t epoch);

        [[nodiscard]] std::size_t Size() const noexcept;
        [[nodiscard]] std::size_t MaximumPending() const noexcept;
        [[nodiscard]] std::uint32_t Epoch() const noexcept;
        [[nodiscard]] const ClickedMissionQueueCounters& Counters() const noexcept;

    private:
        [[nodiscard]] static std::uint32_t ExpiryTtl(std::int32_t frameSendRate) noexcept;
        [[nodiscard]] static bool HasSameSemanticKey(
            const ClickedMissionIntent& left,
            const ClickedMissionIntent& right) noexcept;

        std::deque<ClickedMissionIntent> mPending;
        std::size_t mMaximumPending;
        std::uint32_t mEpoch;
        ClickedMissionQueueCounters mCounters;
    };
}
