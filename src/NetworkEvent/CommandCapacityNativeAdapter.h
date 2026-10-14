#pragma once

#include "NetworkEvent/CommandCapacity.h"
#include "NetworkEvent/EventBacklog.h"
#include "NetworkEvent/ExtraFrame.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class TechnoClass;

namespace ra_commands::game::command_capacity::native
{
    struct SessionSnapshot
    {
        std::uintptr_t mIdentity = 0;
        std::uint32_t mSessionId = 0;
        std::uint32_t mFrame = 0;
        std::uint32_t mFrameSendRate = 0;
        std::uint32_t mFrameDelay = 0;
        std::uint8_t mHouseIndex = 0;
        int mMode = -1;
        int mPacketMode = -1;
        bool mReady = false;
        bool mReplay = false;
        bool mObserver = false;
        bool mPlanning = false;
    };

    struct QueueBudget
    {
        std::uint32_t mDoListFree = 0;
        std::uint32_t mDoListMega = 0;
        std::uint32_t mOutListCount = 0;
        std::uint32_t mOutListMega = 0;
    };

    enum class SubmitResult { NotAccepted, Accepted, Faulted };

    [[nodiscard]] std::uint32_t GetCallingThreadId() noexcept;
    [[nodiscard]] bool ReadSession(SessionSnapshot& outSession) noexcept;
    [[nodiscard]] bool ReadBudget(std::uint8_t house,
        std::uint32_t targetFrame, QueueBudget& outBudget) noexcept;
    [[nodiscard]] std::uint32_t GetOutListFreeSlots() noexcept;
    [[nodiscard]] bool ReadActor(TechnoClass* actor,
        network_event::TargetPayload& outActor) noexcept;
    [[nodiscard]] bool CopyTarget(const network_event::TargetPayload* source,
        network_event::TargetPayload& outTarget) noexcept;
    [[nodiscard]] network_event::EventBacklogMetadata ReadOrderMetadata() noexcept;
    [[nodiscard]] bool ReadFrameHeader(const network_event::NetworkEvent* frameInfo,
        network_event::ExtraFrameHeader& outHeader) noexcept;
    [[nodiscard]] SubmitResult AppendDoList(
        const std::vector<network_event::NetworkEvent>& events,
        std::uint32_t targetFrame, std::uint32_t reservedSlots) noexcept;
    [[nodiscard]] SubmitResult AppendOutList(
        const std::vector<network_event::NetworkEvent>& events,
        std::uint32_t frame, std::uint32_t reservedSlots) noexcept;
    /** Accepted只表示原生void发送及flush返回；Faulted进度未知，禁止重发。 */
    [[nodiscard]] SubmitResult SendPacket(void* transport,
        const std::uint8_t* packet, std::size_t sizeBytes) noexcept;
}

namespace ra_commands::game::command_capacity::detail
{
    // 仅供原生生产 Hook 调用；完整事件的复制契约不暴露给上层命令业务。
    CaptureResult CapturePlanningEvent(const network_event::NetworkEvent& event) noexcept;
    CaptureResult CaptureMegaMission(int mission,
        const network_event::TargetPayload* target, int selfId, char selfKind,
        const network_event::TargetPayload* destination) noexcept;
    CaptureResult CaptureMegaMissionF(int mission,
        const network_event::TargetPayload* target, int selfId, char selfKind,
        const network_event::TargetPayload* destination, int speed, int maximumSpeed) noexcept;
    void BeforeNativeParser() noexcept;
    void BeforeNativeSend() noexcept;
    int AfterNativeSend(void* transport, network_event::NetworkEvent* frameInfo,
        int packetCapacityBytes, int frameDelay, std::uint16_t commandCount) noexcept;
}
