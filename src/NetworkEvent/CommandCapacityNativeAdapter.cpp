#include "NetworkEvent/CommandCapacityNativeAdapter.h"

#include "Game/GameObjectAccess.h"
#include "Game/PluginOrderScope.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <NetworkEvents.h>
#include <SessionClass.h>
#include <TechnoClass.h>
#include <Unsorted.h>

#include <Windows.h>
#include <mmsystem.h>

#include <cstring>

namespace ra_commands::game::command_capacity::native
{
    namespace
    {
        using network_event::NetworkEvent;
        constexpr std::uintptr_t DO_COUNT_ADDRESS = 0x008B41F8u;
        constexpr std::uintptr_t DO_READ_ADDRESS = 0x008B41FCu;
        constexpr std::uintptr_t DO_WRITE_ADDRESS = 0x008B4200u;
        constexpr std::uintptr_t DO_EVENTS_ADDRESS = 0x008B4204u;
        constexpr std::uintptr_t DO_TIMESTAMPS_ADDRESS = 0x00A70204u;
        constexpr std::uintptr_t OUT_COUNT_ADDRESS = 0x00A802C8u;
        constexpr std::uintptr_t OUT_WRITE_ADDRESS = 0x00A802D0u;
        constexpr std::uintptr_t OUT_EVENTS_ADDRESS = 0x00A802D4u;
        constexpr std::uintptr_t OUT_TIMESTAMPS_ADDRESS = 0x00A83A54u;
        constexpr std::uintptr_t PACKET_MODE_ADDRESS = 0x00A8B24Cu;
        constexpr std::uintptr_t FRAME_SEND_RATE_ADDRESS = 0x00A8B554u;
        constexpr std::uintptr_t FRAME_DELAY_ADDRESS = 0x00A8B550u;
        constexpr std::uintptr_t REPLAY_FLAGS_ADDRESS = 0x00A8D5F8u;
        constexpr std::uintptr_t GAME_CONTINUE_ADDRESS = 0x00A8E9A0u;
        constexpr std::uintptr_t PLANNING_FLAG_ADDRESS = 0x00AC4CF4u;
        constexpr std::uintptr_t SENT_FRAME_COUNT_ADDRESS = 0x00B0487Cu;
        constexpr std::uintptr_t PACK_TARGET_ADDRESS = 0x006E6AB0u;
        constexpr int DO_CAPACITY = 0x4000;
        constexpr int OUT_CAPACITY = 128;

        static_assert(sizeof(NetworkEvent) == sizeof(::NetworkEvent));
        static_assert(sizeof(network_event::TargetPayload) == sizeof(NetID));
        static_assert(offsetof(NetworkEvent, Data) == offsetof(::NetworkEvent, Checksum));

        struct RingView
        {
            volatile int* mCount;
            volatile int* mRead;
            volatile int* mWrite;
            NetworkEvent* mEvents;
            std::uint32_t* mTimestamps;
            int mCapacity;
        };

        RingView DoList() noexcept
        {
            return {reinterpret_cast<volatile int*>(DO_COUNT_ADDRESS),
                reinterpret_cast<volatile int*>(DO_READ_ADDRESS),
                reinterpret_cast<volatile int*>(DO_WRITE_ADDRESS),
                reinterpret_cast<NetworkEvent*>(DO_EVENTS_ADDRESS),
                reinterpret_cast<std::uint32_t*>(DO_TIMESTAMPS_ADDRESS), DO_CAPACITY};
        }
        RingView OutList() noexcept
        {
            return {reinterpret_cast<volatile int*>(OUT_COUNT_ADDRESS), nullptr,
                reinterpret_cast<volatile int*>(OUT_WRITE_ADDRESS),
                reinterpret_cast<NetworkEvent*>(OUT_EVENTS_ADDRESS),
                reinterpret_cast<std::uint32_t*>(OUT_TIMESTAMPS_ADDRESS), OUT_CAPACITY};
        }

        bool IsValidRing(RingView view) noexcept
        {
            return *view.mCount >= 0 && *view.mCount <= view.mCapacity &&
                *view.mWrite >= 0 && *view.mWrite < view.mCapacity &&
                (!view.mRead || (*view.mRead >= 0 && *view.mRead < view.mCapacity &&
                    ((*view.mRead + *view.mCount) & (view.mCapacity - 1)) == *view.mWrite));
        }

        std::uint32_t CountMega(RingView view, std::uint8_t house,
            std::uint32_t targetFrame, bool dueOnly) noexcept
        {
            std::uint32_t count = 0;
            const auto read = view.mRead ? *view.mRead :
                ((*view.mWrite - *view.mCount) & (view.mCapacity - 1));
            for (int index = 0; index < *view.mCount; ++index)
            {
                const auto& event = view.mEvents[(read + index) & (view.mCapacity - 1)];
                if (event.HouseIndex == house && network_event::IsCapacityMegaEvent(event) &&
                    (!dueOnly || ((event.Unused & 1u) == 0 &&
                        network_event::IsFrameDue(event.Timestamp, targetFrame))))
                {
                    ++count;
                }
            }
            return count;
        }

        // SEH桩不拥有需析构的对象；Faulted表示可能已写入部分记录，调用方禁止重试。
        SubmitResult AppendGuarded(RingView view, const NetworkEvent* events,
            std::size_t count, std::uint32_t frame, std::uint32_t reserve) noexcept
        {
            bool writing = false;
            __try
            {
                if (!events || !IsValidRing(view) || reserve >= static_cast<std::uint32_t>(view.mCapacity) ||
                    count > static_cast<std::size_t>(view.mCapacity - *view.mCount) ||
                    reserve > static_cast<std::uint32_t>(view.mCapacity - *view.mCount - count))
                {
                    return SubmitResult::NotAccepted;
                }
                const auto nowMs = timeGetTime();
                int write = *view.mWrite;
                writing = true;
                for (std::size_t index = 0; index < count; ++index)
                {
                    NetworkEvent event = events[index];
                    event.Timestamp = frame;
                    event.Unused &= static_cast<std::uint8_t>(~1u);
                    std::memcpy(&view.mEvents[write], &event, sizeof(event));
                    view.mTimestamps[write] = nowMs;
                    write = (write + 1) & (view.mCapacity - 1);
                }
                *view.mWrite = write;
                *view.mCount += static_cast<int>(count);
                return SubmitResult::Accepted;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return writing ? SubmitResult::Faulted : SubmitResult::NotAccepted;
            }
        }
    }

    std::uint32_t GetCallingThreadId() noexcept { return GetCurrentThreadId(); }

    bool ReadSession(SessionSnapshot& outSession) noexcept
    {
        __try
        {
            SessionSnapshot session{};
            auto* const player = HouseClass::Player.get();
            const auto& nativeSession = SessionClass::Instance.get();
            session.mIdentity = reinterpret_cast<std::uintptr_t>(player);
            session.mSessionId = nativeSession.UniqueID;
            session.mMode = static_cast<int>(nativeSession.GameMode);
            session.mPacketMode = *reinterpret_cast<const int*>(PACKET_MODE_ADDRESS);
            const int rate = *reinterpret_cast<const int*>(FRAME_SEND_RATE_ADDRESS);
            const int delay = *reinterpret_cast<const int*>(FRAME_DELAY_ADDRESS);
            session.mFrameSendRate = rate > 0 ? static_cast<std::uint32_t>(rate) : 0;
            session.mFrameDelay = delay >= 0 ? static_cast<std::uint32_t>(delay) : 0;
            session.mFrame = static_cast<std::uint32_t>(Unsorted::CurrentFrame);
            session.mReplay = (*reinterpret_cast<const std::uint32_t*>(REPLAY_FLAGS_ADDRESS) & 2u) != 0;
            session.mObserver = HouseClass::IsPlayerObserver();
            session.mPlanning = *reinterpret_cast<const std::uint8_t*>(PLANNING_FLAG_ADDRESS) != 0;
            session.mReady = player && player->ArrayIndex >= 0 && player->ArrayIndex <= 255 &&
                *reinterpret_cast<const std::uint8_t*>(GAME_CONTINUE_ADDRESS) != 0 &&
                IsGameSessionReady();
            if (player) session.mHouseIndex = static_cast<std::uint8_t>(player->ArrayIndex);
            outSession = session;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ReadBudget(std::uint8_t house, std::uint32_t targetFrame,
        QueueBudget& outBudget) noexcept
    {
        __try
        {
            const auto doList = DoList();
            const auto outList = OutList();
            if (!IsValidRing(doList) || !IsValidRing(outList)) return false;
            const QueueBudget budget{
                static_cast<std::uint32_t>(DO_CAPACITY - *doList.mCount),
                CountMega(doList, house, targetFrame, true),
                static_cast<std::uint32_t>(*outList.mCount),
                CountMega(outList, house, targetFrame, false)};
            outBudget = budget;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    std::uint32_t GetOutListFreeSlots() noexcept
    {
        __try
        {
            const auto outList = OutList();
            return IsValidRing(outList) ? static_cast<std::uint32_t>(OUT_CAPACITY - *outList.mCount) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    bool ReadActor(TechnoClass* actor, network_event::TargetPayload& outActor) noexcept
    {
        __try
        {
            if (!actor || !IsLocal(actor) || actor->IsDead() || actor->UniqueID == 0) return false;
            NetID packed{};
            // 当前目标的__thiscall ABI已核对；避免SDK内联JMP_THIS依赖调用方EBP框架。
            using PackTargetFunction = NetID*(__thiscall*)(NetID*, AbstractClass*);
            reinterpret_cast<PackTargetFunction>(PACK_TARGET_ADDRESS)(&packed, actor);
            if (packed.RTTI_ID != actor->UniqueID ||
                packed.WhatAmI != network_event::NATIVE_TECHNO_TARGET_KIND) return false;
            outActor = {packed.RTTI_ID, packed.WhatAmI};
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool CopyTarget(const network_event::TargetPayload* source,
        network_event::TargetPayload& outTarget) noexcept
    {
        __try
        {
            outTarget = source ? *source : network_event::TargetPayload{};
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    network_event::EventBacklogMetadata ReadOrderMetadata() noexcept
    {
        const auto context = GetPluginOrderContext();
        return {context.mProducerTag, context.mEpoch};
    }

    bool ReadFrameHeader(const NetworkEvent* frameInfo,
        network_event::ExtraFrameHeader& outHeader) noexcept
    {
        __try
        {
            if (!frameInfo || frameInfo->Kind != 0x1C) return false;
            outHeader = {frameInfo->HouseIndex, frameInfo->Timestamp,
                frameInfo->Data.FrameInfo.Checksum, frameInfo->Data.FrameInfo.CommandCount,
                frameInfo->Data.FrameInfo.Delay};
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    SubmitResult AppendDoList(const std::vector<NetworkEvent>& events,
        std::uint32_t targetFrame, std::uint32_t reservedSlots) noexcept
    {
        return AppendGuarded(DoList(), events.data(), events.size(), targetFrame, reservedSlots);
    }

    SubmitResult AppendOutList(const std::vector<NetworkEvent>& events,
        std::uint32_t frame, std::uint32_t reservedSlots) noexcept
    {
        return AppendGuarded(OutList(), events.data(), events.size(), frame, reservedSlots);
    }

    SubmitResult SendPacket(void* transport, const std::uint8_t* packet,
        std::size_t sizeBytes) noexcept
    {
        using SendFunction = void(__thiscall*)(void*, const void*, int, bool, int);
        using FlushFunction = void(__thiscall*)(void*);
        bool invoked = false;
        __try
        {
            if (!transport || !packet || sizeBytes == 0 || sizeBytes > 65536) return SubmitResult::NotAccepted;
            auto** const vtable = *reinterpret_cast<void***>(transport);
            if (!vtable || !vtable[1] || !vtable[2]) return SubmitResult::NotAccepted;
            const auto send = reinterpret_cast<SendFunction>(vtable[2]);
            const auto flush = reinterpret_cast<FlushFunction>(vtable[1]);
            // void发送接口没有确认；进入原生调用之后发生异常时，不能证明它没有发送。
            invoked = true;
            send(transport, packet, static_cast<int>(sizeBytes), true, -1);
            ++*reinterpret_cast<volatile std::uint32_t*>(SENT_FRAME_COUNT_ADDRESS);
            flush(transport);
            return SubmitResult::Accepted;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return invoked ? SubmitResult::Faulted : SubmitResult::NotAccepted;
        }
    }
}
