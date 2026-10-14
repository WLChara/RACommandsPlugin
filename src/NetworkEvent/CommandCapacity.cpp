#include "NetworkEvent/CommandCapacity.h"

#include "Hooks/CommandCapacityHook.h"
#include "NetworkEvent/CommandCapacityNativeAdapter.h"
#include "NetworkEvent/PlanningEvents.h"

#include <algorithm>
#include <atomic>
#include <limits>

namespace ra_commands::game::command_capacity
{
    namespace
    {
        using network_event::NetworkEvent;
        constexpr std::uint32_t NATIVE_TRANSFER_RESERVED_SLOTS = 128;
        // 上层提交前要求13空槽，单次原生写入后至少保留12槽。
        constexpr std::uint32_t FALLBACK_RESERVED_SLOTS = 12;
        constexpr int MAX_PACKET_CAPACITY_BYTES = 65536;

        network_event::EventBacklog g_Backlog;
        network_event::ExtraFrameWindow g_ExtraWindow;
        std::atomic<bool> g_Enabled{false};
        std::atomic<bool> g_Faulted{false};
        std::atomic<std::uint32_t> g_GameThread{0};
        std::atomic<bool> g_ResetSession{false};
        std::atomic<bool> g_Ready{false};
        native::SessionSnapshot g_Session;
        bool g_HasSession = false;
        std::atomic<std::uint64_t> g_LocalSubmitted{0};
        std::atomic<std::uint64_t> g_FallbackSubmitted{0};
        std::atomic<std::uint64_t> g_ExtraSubmitted{0};
        std::atomic<std::uint64_t> g_PausedWindows{0};
        std::atomic<std::uint64_t> g_FaultedSubmissions{0};

        bool IsGameThread() noexcept
        {
            const auto threadId = g_GameThread.load(std::memory_order_acquire);
            return threadId != 0 && threadId == native::GetCallingThreadId();
        }

        bool IsLocal(const native::SessionSnapshot& session) noexcept
        {
            return session.mMode == 0 || session.mMode == 5;
        }

        bool IsNetwork(const native::SessionSnapshot& session) noexcept
        {
            return session.mMode == 3 || session.mMode == 4;
        }

        bool RefreshSession() noexcept
        {
            if (!IsGameThread())
            {
                return false;
            }
            if (g_ResetSession.exchange(false, std::memory_order_acq_rel))
            {
                g_HasSession = false;
                g_ExtraWindow.Reset();
            }
            native::SessionSnapshot session{};
            const bool usable = native::ReadSession(session) && session.mReady &&
                !session.mReplay && !session.mObserver &&
                (IsLocal(session) || IsNetwork(session));
            if (!usable)
            {
                g_Ready.store(false, std::memory_order_release);
                if (g_HasSession)
                {
                    g_Backlog.Clear();
                    g_ExtraWindow.Reset();
                    g_HasSession = false;
                }
                return false;
            }
            if (!g_HasSession || session.mIdentity != g_Session.mIdentity ||
                session.mSessionId != g_Session.mSessionId || session.mMode != g_Session.mMode ||
                session.mHouseIndex != g_Session.mHouseIndex ||
                session.mFrame < g_Session.mFrame)
            {
                g_Backlog.Clear();
                g_ExtraWindow.Reset();
            }
            g_Session = session;
            g_HasSession = true;
            g_ExtraWindow.ObserveTiming(session.mFrame, session.mFrameSendRate,
                session.mFrameDelay, session.mPacketMode);
            const bool ready = g_Enabled.load(std::memory_order_acquire) &&
                !g_Faulted.load(std::memory_order_acquire) && hooks::IsInstalled();
            g_Ready.store(ready, std::memory_order_release);
            return ready;
        }

        void MarkFault() noexcept
        {
            ++g_FaultedSubmissions;
            g_Faulted.store(true, std::memory_order_release);
            // 原生提交进度未知；放弃剩余副本，绝不在下帧重复提交。
            g_Backlog.Clear();
        }

        CaptureResult Capture(NetworkEvent event) noexcept
        {
            if (!g_Enabled.load(std::memory_order_acquire) ||
                g_Faulted.load(std::memory_order_acquire))
            {
                return CaptureResult::Rejected;
            }
            return g_Backlog.Submit(event, native::ReadOrderMetadata())
                ? CaptureResult::Queued : CaptureResult::Rejected;
        }

        void DrainNativeQueue(bool toDoList) noexcept
        {
            if (!RefreshSession())
            {
                return;
            }
            const auto snapshot = g_Backlog.BeginSnapshot(g_Session.mFrame);
            if (!snapshot.mValid)
            {
                return;
            }
            try
            {
                std::uint32_t budgetFrame = g_Session.mFrame;
                if (!toDoList && IsNetwork(g_Session))
                {
                    if (g_Session.mPacketMode == 2)
                    {
                        if (!network_event::TryGetAlignedFrameTimestamp(g_Session.mFrame,
                            g_Session.mFrameDelay, g_Session.mFrameSendRate, budgetFrame)) return;
                    }
                    else budgetFrame += g_Session.mFrameDelay;
                }
                native::QueueBudget budget{};
                if (!native::ReadBudget(g_Session.mHouseIndex, budgetFrame, budget)) return;
                for (;;)
                {
                    const auto prefix = g_Backlog.CopyPrefix(snapshot, 1);
                    if (prefix.empty())
                    {
                        return;
                    }
                    const auto submit = [&](const std::vector<NetworkEvent>& events) {
                        if (!g_Enabled.load(std::memory_order_acquire) ||
                            events.front().HouseIndex != g_Session.mHouseIndex)
                        {
                            return false;
                        }
                        native::SubmitResult result = native::SubmitResult::NotAccepted;
                        if (toDoList)
                        {
                            const auto totalMega = budget.mDoListMega + budget.mOutListMega;
                            if (budget.mDoListFree <= NATIVE_TRANSFER_RESERVED_SLOTS ||
                                (network_event::IsCapacityMegaEvent(events.front()) &&
                                    totalMega >= network_event::NATIVE_MEGA_EVENTS_PER_HOUSE_FRAME))
                            {
                                return false;
                            }
                            result = native::AppendDoList(events, g_Session.mFrame,
                                NATIVE_TRANSFER_RESERVED_SLOTS);
                        }
                        else
                        {
                            if (budget.mOutListCount >= 128 - FALLBACK_RESERVED_SLOTS ||
                                (network_event::IsCapacityMegaEvent(events.front()) &&
                                    budget.mDoListMega + budget.mOutListMega >=
                                        network_event::NATIVE_MEGA_EVENTS_PER_HOUSE_FRAME))
                            {
                                return false;
                            }
                            result = native::AppendOutList(events, g_Session.mFrame,
                                FALLBACK_RESERVED_SLOTS);
                        }
                        if (result == native::SubmitResult::Faulted)
                        {
                            MarkFault();
                            return true;
                        }
                        if (result == native::SubmitResult::Accepted)
                        {
                            if (toDoList)
                            {
                                ++g_LocalSubmitted;
                                --budget.mDoListFree;
                                if (network_event::IsCapacityMegaEvent(events.front())) ++budget.mDoListMega;
                            }
                            else
                            {
                                ++g_FallbackSubmitted;
                                ++budget.mOutListCount;
                                if (network_event::IsCapacityMegaEvent(events.front())) ++budget.mOutListMega;
                            }
                            return true;
                        }
                        return false;
                    };
                    if (!g_Backlog.ConsumePrefix(snapshot, 1, submit) || g_Faulted.load())
                    {
                        return;
                    }
                }
            }
            catch (...)
            {
                // 尚未调用原生提交时的复制分配失败不消费前缀；下帧可以重新尝试。
                ++g_PausedWindows;
            }
        }
    }

    bool Install(std::string& outError)
    {
        if (!hooks::Install(outError))
        {
            return false;
        }
        g_Faulted.store(false, std::memory_order_release);
        g_ResetSession.store(true, std::memory_order_release);
        g_Ready.store(false, std::memory_order_release);
        g_Enabled.store(true, std::memory_order_release);
        outError.clear();
        return true;
    }

    void BindGameThread(std::uint32_t threadId) noexcept
    {
        if (g_GameThread.exchange(threadId, std::memory_order_acq_rel) != threadId)
        {
            g_Backlog.Clear();
            g_ResetSession.store(true, std::memory_order_release);
        }
    }

    void OnGameFrame() noexcept { (void)RefreshSession(); }

    void Disable() noexcept
    {
        g_Enabled.store(false, std::memory_order_release);
        g_Ready.store(false, std::memory_order_release);
        g_Backlog.Clear();
        g_ResetSession.store(true, std::memory_order_release);
    }

    bool CanCapturePlanningEvents() noexcept
    {
        return g_Enabled.load(std::memory_order_acquire) &&
            g_Ready.load(std::memory_order_acquire) && !g_Faulted.load(std::memory_order_acquire) &&
            hooks::IsInstalled() && hooks::IsPlanningInstalled();
    }

    std::uint32_t GetEffectiveClickedMissionFreeSlots() noexcept
    {
        if (!RefreshSession() || (g_Session.mPlanning && !CanCapturePlanningEvents()))
        {
            return IsGameThread() ? native::GetOutListFreeSlots() : 0;
        }
        const auto stats = g_Backlog.GetStats();
        const auto available = static_cast<std::uint32_t>(stats.mCapacity - stats.mPending);
        // 返回真实剩余槽；上层13阈值继续保留插件余量，原生输入仍可用全部动态容量。
        return available;
    }

    CaptureResult TryCaptureSimpleClickedEvent(TechnoClass* actor, int eventKind) noexcept
    {
        if (eventKind < 0 || eventKind > 255 ||
            !network_event::IsCapacitySimpleEvent(static_cast<std::uint8_t>(eventKind)) ||
            !RefreshSession() || g_Session.mPlanning)
        {
            return CaptureResult::NotHandled;
        }
        network_event::TargetPayload actorId{};
        if (!native::ReadActor(actor, actorId))
        {
            return CaptureResult::Rejected;
        }
        const auto event = network_event::BuildCapacitySimpleEvent(g_Session.mHouseIndex,
            g_Session.mFrame, static_cast<std::uint8_t>(eventKind), actorId);
        return event ? Capture(*event) : CaptureResult::Rejected;
    }

    std::uint32_t CancelPendingActor(std::uint32_t uniqueId,
        std::uint8_t nativeTargetKind) noexcept
    {
        return g_Backlog.CancelActor(uniqueId, nativeTargetKind);
    }
    std::uint32_t CancelPendingProducer(std::uint8_t producerTag) noexcept
    {
        return g_Backlog.CancelProducer(producerTag);
    }
    std::uint32_t CancelPendingProducerActor(std::uint8_t producerTag,
        std::uint32_t uniqueId, std::uint8_t nativeTargetKind) noexcept
    {
        return g_Backlog.CancelProducerActor(producerTag, uniqueId, nativeTargetKind);
    }
    bool HasPendingActor(std::uint32_t uniqueId, std::uint8_t nativeTargetKind) noexcept
    {
        return g_Backlog.HasActor(uniqueId, nativeTargetKind);
    }
    bool HasPendingProducerActor(std::uint8_t producerTag, std::uint32_t uniqueId,
        std::uint8_t nativeTargetKind, std::uint32_t intentEpoch) noexcept
    {
        return g_Backlog.HasProducerActor(producerTag, uniqueId, nativeTargetKind, intentEpoch);
    }
    CaptureMarker GetCaptureMarker() noexcept
    {
        const auto stats = g_Backlog.GetStats();
        return {stats.mGeneration, stats.mAccepted};
    }
    bool HasCapturedSince(CaptureMarker marker) noexcept
    {
        const auto stats = g_Backlog.GetStats();
        return marker.mGeneration == stats.mGeneration && marker.mAccepted != stats.mAccepted;
    }
    Stats GetStats() noexcept
    {
        const auto backlog = g_Backlog.GetStats();
        return {backlog.mGeneration, backlog.mAccepted, backlog.mCancelled,
            backlog.mRejected, g_LocalSubmitted.load(), g_FallbackSubmitted.load(),
            g_ExtraSubmitted.load(), g_PausedWindows.load(), g_FaultedSubmissions.load(),
            static_cast<std::uint32_t>(backlog.mPending),
            static_cast<std::uint32_t>(backlog.mCapacity), hooks::IsInstalled(),
            g_Enabled.load() && !g_Faulted.load() && g_Ready.load(), hooks::IsPlanningInstalled()};
    }

    namespace detail
    {
        CaptureResult CapturePlanningEvent(const NetworkEvent& event) noexcept
        {
            if (!RefreshSession() || !CanCapturePlanningEvents() ||
                event.HouseIndex != g_Session.mHouseIndex || !network_event::IsPlanningQueuedEvent(event))
            {
                return CaptureResult::NotHandled;
            }
            // 637DD0已设置flag1；Commit关闭规划模式后仍须保留在同一FIFO末尾。
            // 只复制原始111字节，跟随引用和所有规划字段不由普通Mission builder重建。
            return Capture(event);
        }

        CaptureResult CaptureMegaMission(int mission,
            const network_event::TargetPayload* target, int selfId, char selfKind,
            const network_event::TargetPayload* destination) noexcept
        {
            if (!RefreshSession() || g_Session.mPlanning)
            {
                return CaptureResult::NotHandled;
            }
            const network_event::TargetPayload actor{
                static_cast<std::uint32_t>(selfId), static_cast<std::uint8_t>(selfKind)};
            network_event::TargetPayload packedTarget{}, packedDestination{};
            if (actor.RuntimeId == 0 || actor.AbstractKind != network_event::NATIVE_TECHNO_TARGET_KIND ||
                !native::CopyTarget(target, packedTarget) ||
                !native::CopyTarget(destination, packedDestination))
            {
                return CaptureResult::Rejected;
            }
            return Capture(network_event::BuildCapacityMegaMission(g_Session.mHouseIndex,
                g_Session.mFrame, mission, actor, packedTarget, packedDestination));
        }

        CaptureResult CaptureMegaMissionF(int mission,
            const network_event::TargetPayload* target, int selfId, char selfKind,
            const network_event::TargetPayload* destination, int speed, int maximumSpeed) noexcept
        {
            if (!RefreshSession() || g_Session.mPlanning)
            {
                return CaptureResult::NotHandled;
            }
            const network_event::TargetPayload actor{
                static_cast<std::uint32_t>(selfId), static_cast<std::uint8_t>(selfKind)};
            network_event::TargetPayload packedTarget{}, packedDestination{};
            if (actor.RuntimeId == 0 || actor.AbstractKind != network_event::NATIVE_TECHNO_TARGET_KIND ||
                !native::CopyTarget(target, packedTarget) ||
                !native::CopyTarget(destination, packedDestination))
            {
                return CaptureResult::Rejected;
            }
            return Capture(network_event::BuildCapacityMegaMissionF(g_Session.mHouseIndex,
                g_Session.mFrame, mission, actor, packedTarget, packedDestination, speed, maximumSpeed));
        }

        void BeforeNativeParser() noexcept
        {
            if (!RefreshSession()) return;
            if (IsLocal(g_Session)) DrainNativeQueue(true);
            else if (g_Session.mPacketMode != 2 || g_ExtraWindow.RequiresNativeFallback())
                DrainNativeQueue(false);
        }

        void BeforeNativeSend() noexcept
        {
            if (RefreshSession() && IsNetwork(g_Session) &&
                (g_Session.mPacketMode != 2 || g_ExtraWindow.RequiresNativeFallback()))
            {
                DrainNativeQueue(false);
            }
        }

        int AfterNativeSend(void* transport, NetworkEvent* frameInfo,
            int packetCapacityBytes, int frameDelay, std::uint16_t commandCount) noexcept
        {
            (void)commandCount; // 原生已建构的帧头是计数、CRC和Delay的唯一来源。
            if (!RefreshSession() || !IsNetwork(g_Session) || g_Session.mPacketMode != 2 ||
                g_ExtraWindow.RequiresNativeFallback() ||
                packetCapacityBytes <= static_cast<int>(network_event::SHORT_FRAME_INFO_SIZE) ||
                packetCapacityBytes > MAX_PACKET_CAPACITY_BYTES || frameDelay < 0)
            {
                return 0;
            }
            network_event::ExtraFrameHeader header{};
            std::uint32_t expectedTimestamp = 0;
            if (!native::ReadFrameHeader(frameInfo, header) ||
                header.mHouseIndex != g_Session.mHouseIndex ||
                !network_event::TryGetAlignedFrameTimestamp(g_Session.mFrame,
                    static_cast<std::uint32_t>(frameDelay), g_Session.mFrameSendRate,
                    expectedTimestamp) || header.mTimestamp != expectedTimestamp ||
                !g_ExtraWindow.TryBegin(g_Session.mFrame, header.mTimestamp,
                    g_Session.mFrameSendRate))
            {
                ++g_PausedWindows;
                return 0;
            }
            const auto snapshot = g_Backlog.BeginSnapshot(g_Session.mFrame);
            if (!snapshot.mValid) return 0;
            const auto sourceSession = g_Session;
            std::size_t sent = 0;
            std::uint32_t remainingMega = network_event::EXTRA_EVENTS_PER_TIMESTAMP;
            try
            {
                while (sent < network_event::EXTRA_EVENTS_PER_TIMESTAMP)
                {
                    native::QueueBudget budget{};
                    if (!native::ReadBudget(header.mHouseIndex, header.mTimestamp, budget) ||
                        budget.mDoListFree <= NATIVE_TRANSFER_RESERVED_SLOTS)
                    {
                        ++g_PausedWindows;
                        break;
                    }
                    remainingMega = (std::min)(remainingMega,
                        network_event::GetExtraMegaBudget(budget.mDoListMega));
                    // 超过原生预留时，不再冒险追加本窗口，剩余全部FIFO保留到新目标帧。
                    if (budget.mDoListMega > network_event::EXTRA_EVENTS_PER_TIMESTAMP && sent == 0)
                    {
                        ++g_PausedWindows;
                        break;
                    }
                    const auto candidates = g_Backlog.CopyPrefix(snapshot,
                        network_event::EXTRA_EVENTS_PER_TIMESTAMP - sent);
                    auto sizeBytes = network_event::SHORT_FRAME_INFO_SIZE;
                    std::size_t count = 0;
                    std::uint32_t megaCount = 0;
                    for (const auto& event : candidates)
                    {
                        const auto eventSize = network_event::GetCapacityCompressedEventSize(event);
                        if (eventSize == 0 || event.HouseIndex != header.mHouseIndex ||
                            eventSize > static_cast<std::size_t>(packetCapacityBytes) - sizeBytes ||
                            count >= budget.mDoListFree - NATIVE_TRANSFER_RESERVED_SLOTS ||
                            (network_event::IsCapacityMegaEvent(event) && megaCount >= remainingMega))
                        {
                            break;
                        }
                        sizeBytes += eventSize;
                        ++count;
                        if (network_event::IsCapacityMegaEvent(event)) ++megaCount;
                    }
                    if (count == 0) break;
                    const auto submit = [&](const std::vector<NetworkEvent>& events) {
                        if (!g_Enabled.load(std::memory_order_acquire)) return false;
                        std::uint32_t actualMega = 0;
                        for (const auto& event : events)
                        {
                            if (network_event::IsCapacityMegaEvent(event)) ++actualMega;
                        }
                        if (actualMega > remainingMega ||
                            events.size() > budget.mDoListFree - NATIVE_TRANSFER_RESERVED_SLOTS)
                        {
                            return false;
                        }
                        std::vector<std::uint8_t> packet;
                        if (!network_event::BuildExtraFramePacket(header, events,
                            static_cast<std::size_t>(packetCapacityBytes), packet))
                        {
                            return false;
                        }
                        // 所有可能分配的副本在发送前准备；发送接受后不得因复制失败保留原队列重发。
                        std::vector<NetworkEvent> echoEvents;
                        echoEvents.reserve(events.size());
                        for (const auto& event : events)
                        {
                            const auto received = network_event::BuildCapacityReceivedEvent(event, header);
                            if (!received) return false;
                            echoEvents.push_back(*received);
                        }
                        const auto sendResult = native::SendPacket(transport, packet.data(), packet.size());
                        if (sendResult == native::SubmitResult::NotAccepted) return false;
                        if (sendResult == native::SubmitResult::Faulted)
                        {
                            MarkFault();
                            return true;
                        }
                        // 发送接受后只能自回流一次；其后失败不重发已经接受的包。
                        native::SessionSnapshot echoSession{};
                        if (!native::ReadSession(echoSession) || !echoSession.mReady ||
                            echoSession.mIdentity != sourceSession.mIdentity ||
                            echoSession.mSessionId != sourceSession.mSessionId ||
                            echoSession.mMode != sourceSession.mMode || echoSession.mReplay ||
                            echoSession.mObserver)
                        {
                            MarkFault();
                            return true;
                        }
                        const auto echoResult = native::AppendDoList(echoEvents, header.mTimestamp,
                            NATIVE_TRANSFER_RESERVED_SLOTS);
                        g_ExtraWindow.MarkSent(header.mTimestamp);
                        sent += events.size();
                        remainingMega -= actualMega;
                        g_ExtraSubmitted += events.size();
                        if (echoResult != native::SubmitResult::Accepted) MarkFault();
                        return true;
                    };
                    if (!g_Backlog.ConsumePrefix(snapshot, count, submit) || g_Faulted.load()) break;
                }
            }
            catch (...)
            {
                ++g_PausedWindows;
            }
            return static_cast<int>(sent);
        }
    }
}
