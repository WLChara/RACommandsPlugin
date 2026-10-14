#pragma once

#include <cstdint>
#include <string>

class TechnoClass;

namespace ra_commands::game::command_capacity
{
    enum class CaptureResult { NotHandled, Queued, Rejected };

    struct CaptureMarker
    {
        std::uint64_t mGeneration = 0;
        std::uint64_t mAccepted = 0;
    };

    struct Stats
    {
        std::uint64_t mGeneration = 0;
        std::uint64_t mAccepted = 0;
        std::uint64_t mCancelled = 0;
        std::uint64_t mRejected = 0;
        std::uint64_t mLocalSubmitted = 0;
        std::uint64_t mNativeFallbackSubmitted = 0;
        std::uint64_t mExtraSubmitted = 0;
        std::uint64_t mPausedWindows = 0;
        std::uint64_t mFaultedSubmissions = 0;
        std::uint32_t mPending = 0;
        std::uint32_t mCapacity = 0;
        bool mInstalled = false;
        bool mReady = false;
    };

    /** 版本门禁及Syringe启动阶段完成后调用；成功的detour保留至进程退出。 */
    [[nodiscard]] bool Install(std::string& outError);
    void BindGameThread(std::uint32_t threadId) noexcept;
    /** 游戏主线程每帧更新会话；实际消费由原生parser/发送窗口触发。 */
    void OnGameFrame() noexcept;
    /** 原子停用并取消当前代次；不移除永久Hook，也不允许FreeLibrary卸载。 */
    void Disable() noexcept;
    [[nodiscard]] std::uint32_t GetEffectiveClickedMissionFreeSlots() noexcept;
    /** 仅在既有6FFE00 Hook的人工取消观察之后调用，不自行安装同点Hook。 */
    [[nodiscard]] CaptureResult TryCaptureSimpleClickedEvent(
        TechnoClass* actor, int eventKind) noexcept;

    std::uint32_t CancelPendingActor(std::uint32_t uniqueId,
        std::uint8_t nativeTargetKind) noexcept;
    std::uint32_t CancelPendingProducer(std::uint8_t producerTag) noexcept;
    std::uint32_t CancelPendingProducerActor(std::uint8_t producerTag,
        std::uint32_t uniqueId, std::uint8_t nativeTargetKind) noexcept;
    [[nodiscard]] bool HasPendingActor(std::uint32_t uniqueId,
        std::uint8_t nativeTargetKind) noexcept;
    [[nodiscard]] bool HasPendingProducerActor(std::uint8_t producerTag,
        std::uint32_t uniqueId, std::uint8_t nativeTargetKind,
        std::uint32_t intentEpoch) noexcept;

    /** 返回本地复制入队的代次/计数；不表示已进入DoList、物理送达或网络确认。 */
    [[nodiscard]] CaptureMarker GetCaptureMarker() noexcept;
    [[nodiscard]] bool HasCapturedSince(CaptureMarker marker) noexcept;
    [[nodiscard]] Stats GetStats() noexcept;
}
