// 独立Win32 ABI夹具：实际Hook、队列、native adapter接入假的游戏内存和原生入口。
#include "NetworkEvent/CommandCapacity.h"
#include "NetworkEvent/CommandCapacityNativeAdapter.h"
#include "NetworkEvent/ExtraFrame.h"
#include "Game/PluginOrderScope.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <NetworkEvents.h>
#include <SessionClass.h>
#include <TechnoClass.h>
#include <Unsorted.h>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

// 装载器先于CRT堆/线程栈保留目标原址，避免隔离进程随机分配占用游戏页。
#pragma section(".fixture", read, write)
__declspec(allocate(".fixture")) std::uint8_t g_CommandCapacityFixtureImage[0xA00000]{};

void RunEventBacklogTests();
void RunExtraFrameTests();
void RunCommandCapacityTests();

namespace
{
    namespace capacity = ra_commands::game::command_capacity;
    namespace events = ra_commands::network_event;
    constexpr std::uintptr_t MEGA_ADDRESS = 0x646E90;
    constexpr std::uintptr_t FAST_ADDRESS = 0x646F70;
    constexpr std::uintptr_t PARSER_ADDRESS = 0x647260;
    constexpr std::uintptr_t SEND_ADDRESS = 0x649CA0;
    constexpr std::uintptr_t CONSUMER_ADDRESS = 0x64C380;
    constexpr std::uintptr_t PACK_ADDRESS = 0x6E6AB0;
    constexpr std::uintptr_t DO_EVENTS_ADDRESS = 0x8B4204;
    constexpr std::uintptr_t OUT_EVENTS_ADDRESS = 0xA802D4;
    constexpr int DO_CAPACITY = 0x4000;

    using MegaFunction = bool(__fastcall*)(int, const events::TargetPayload*, int,
        char, const events::TargetPayload*);
    using FastFunction = bool(__fastcall*)(int, const events::TargetPayload*, int,
        char, const events::TargetPayload*, int, int);
    using ParserFunction = void(__cdecl*)();
    using SendFunction = int(__fastcall*)(void*, events::NetworkEvent*, int, int, std::uint16_t);

    std::vector<events::NetworkEvent> g_Recorded;
    std::vector<events::NetworkEvent> g_Executed;
    std::vector<events::NetworkEvent> g_WireEvents;
    std::vector<std::vector<std::uint8_t>> g_Packets;
    bool g_WorldReady = true;
    bool g_FailFlush = false;
    bool g_CaptureDuringFlush = false;
    std::uint32_t g_SessionId = 0;
    std::uint32_t g_ParserCalls = 0;
    std::uint32_t g_OriginalMegaCalls = 0;
    std::uint32_t g_OriginalFastCalls = 0;
    int g_OriginalSpeed = 0;
    int g_OriginalMaximumSpeed = 0;
    int g_OriginalSelfId = 0;
    char g_OriginalSelfKind = 0;
    int g_OriginalMission = 0;
    events::TargetPayload g_OriginalTarget{}, g_OriginalDestination{};
    std::array<std::uint8_t, 4096> g_FrameBuffer{};

    struct FixtureTransport { void** mVtable; };
    std::array<void*, 3> g_TransportVtable{};
    FixtureTransport g_Transport{g_TransportVtable.data()};
    HouseClass* g_Player = nullptr;
    TechnoClass* g_Actor = nullptr;
    std::array<void*, 256> g_ActorVtable{};

    template<typename T>
    T& At(std::uintptr_t address) { return *reinterpret_cast<T*>(address); }

    void RequireNative(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    void Map(std::uintptr_t address, std::size_t size)
    {
        const auto begin = reinterpret_cast<std::uintptr_t>(g_CommandCapacityFixtureImage);
        const auto end = begin + sizeof(g_CommandCapacityFixtureImage);
        DWORD oldProtect = 0;
        if (address < begin || address + size > end ||
            !VirtualProtect(reinterpret_cast<void*>(address), size, PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            MEMORY_BASIC_INFORMATION info{};
            VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info));
            std::cerr << "Mapping owner " << info.AllocationBase << " region " << info.BaseAddress
                << " size " << info.RegionSize << " type " << info.Type << '\n';
            std::cerr << "Fixture mapping failed at " << std::hex << address << " error " << GetLastError() << '\n';
            throw std::runtime_error("cannot map isolated native fixture addresses");
        }
        std::memset(reinterpret_cast<void*>(address), 0, size);
    }

    void WriteJump(std::uintptr_t address, const void* target)
    {
        auto* bytes = reinterpret_cast<std::uint8_t*>(address);
        std::memset(bytes, 0x90, 32);
        bytes[0] = 0xE9;
        const auto relative = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target) - address - 5);
        std::memcpy(bytes + 1, &relative, sizeof(relative));
        FlushInstructionCache(GetCurrentProcess(), bytes, 32);
    }

    std::uint32_t ActorId(const events::NetworkEvent& event)
    {
        std::uint32_t actorId = 0;
        std::memcpy(&actorId, event.Data.Raw, sizeof(actorId));
        return actorId;
    }

    events::NetworkEvent MakeMega(std::uint32_t actorId, std::uint32_t frame)
    {
        return events::BuildCapacityMegaMission(2, frame, 2, {actorId, 52}, {}, {500, 11});
    }

    bool __fastcall DeadStub(void*, void*) { return false; }
    NetID* __fastcall PackStub(NetID* output, void*, AbstractClass* actor)
    {
        output->RTTI_ID = actor->UniqueID;
        output->WhatAmI = 52;
        return output;
    }

    void PushNativeDo(events::NetworkEvent event, std::uint32_t targetFrame)
    {
        auto& count = At<int>(0x8B41F8);
        auto& write = At<int>(0x8B4200);
        RequireNative(count < DO_CAPACITY, "native DoList overflow in fixture");
        event.Timestamp = targetFrame;
        event.Unused &= static_cast<std::uint8_t>(~1u);
        At<events::NetworkEvent>(DO_EVENTS_ADDRESS + write * sizeof(event)) = event;
        write = (write + 1) & (DO_CAPACITY - 1);
        ++count;
    }

    void PushNativeOut(events::NetworkEvent event)
    {
        auto& count = At<int>(0xA802C8);
        auto& write = At<int>(0xA802D0);
        RequireNative(count < 128, "native OutList overflow in fixture");
        At<events::NetworkEvent>(OUT_EVENTS_ADDRESS + write * sizeof(event)) = event;
        write = (write + 1) & 127;
        ++count;
    }

    void NativeConsumerBody()
    {
        std::array<std::uint32_t, 256> megaCounts{};
        auto& count = At<int>(0x8B41F8);
        auto& read = At<int>(0x8B41FC);
        const auto frame = static_cast<std::uint32_t>(Unsorted::CurrentFrame);
        while (count)
        {
            auto& event = At<events::NetworkEvent>(DO_EVENTS_ADDRESS + read * sizeof(events::NetworkEvent));
            if (!events::IsFrameDue(event.Timestamp, frame)) break;
            if ((event.Unused & 1u) == 0)
            {
                if (events::IsCapacityMegaEvent(event))
                    RequireNative(++megaCounts[event.HouseIndex] <= 256,
                        "native receiver would silently drop a 257th same-house Mega");
                g_Executed.push_back(event);
            }
            event.Unused |= 1u;
            read = (read + 1) & (DO_CAPACITY - 1);
            --count;
        }
    }

    void __cdecl ReplayThenConsumer()
    {
        const auto count = At<int>(0x8B41F8);
        const auto read = At<int>(0x8B41FC);
        const auto frame = static_cast<std::uint32_t>(Unsorted::CurrentFrame);
        for (int index = 0; index < count; ++index)
        {
            const auto& event = At<events::NetworkEvent>(DO_EVENTS_ADDRESS +
                ((read + index) & (DO_CAPACITY - 1)) * sizeof(events::NetworkEvent));
            if ((event.Unused & 1u) == 0 && events::IsFrameDue(event.Timestamp, frame))
                g_Recorded.push_back(event);
        }
        NativeConsumerBody();
    }

    void __cdecl OriginalParser()
    {
        ++g_ParserCalls;
        const auto mode = SessionClass::Instance.get().GameMode;
        if (mode == GameMode::Campaign || mode == GameMode::Skirmish)
        {
            const auto count = At<int>(0xA802C8);
            const auto read = (At<int>(0xA802D0) - count) & 127;
            for (int index = 0; index < count; ++index)
                PushNativeDo(At<events::NetworkEvent>(OUT_EVENTS_ADDRESS +
                    ((read + index) & 127) * sizeof(events::NetworkEvent)),
                    static_cast<std::uint32_t>(Unsorted::CurrentFrame));
            At<int>(0xA802C8) = 0;
        }
        reinterpret_cast<ParserFunction>(CONSUMER_ADDRESS)();
    }

    bool __fastcall OriginalMega(int mission, const events::TargetPayload* target,
        int selfId, char selfKind, const events::TargetPayload* destination)
    {
        ++g_OriginalMegaCalls;
        g_OriginalMission = mission;
        g_OriginalSelfId = selfId;
        g_OriginalSelfKind = selfKind;
        g_OriginalTarget = target ? *target : events::TargetPayload{};
        g_OriginalDestination = destination ? *destination : events::TargetPayload{};
        return false;
    }

    bool __fastcall OriginalFast(int mission, const events::TargetPayload* target,
        int selfId, char selfKind, const events::TargetPayload* destination,
        int speed, int maximumSpeed)
    {
        ++g_OriginalFastCalls;
        g_OriginalSpeed = speed;
        g_OriginalMaximumSpeed = maximumSpeed;
        return OriginalMega(mission, target, selfId, selfKind, destination);
    }

    void DecodePacket(const std::uint8_t* packet, int size)
    {
        RequireNative(size >= 14, "compressed packet header is truncated");
        events::NetworkEvent header{};
        std::memcpy(&header, packet, 14);
        RequireNative(header.Kind == 0x1C && header.HouseIndex == 2 &&
            header.Data.FrameInfo.Checksum == 0x11223344 &&
            header.Data.FrameInfo.CommandCount == 42,
            "native frame metadata changed on wire");
        int offset = 14;
        while (offset < size)
        {
            events::NetworkEvent event{};
            event.Kind = packet[offset++];
            event.HouseIndex = header.HouseIndex;
            event.Timestamp = header.Timestamp;
            const int payloadSize = event.Kind == 4 ? 23 : event.Kind == 5 ? 24 : 5;
            if (event.Kind == 4)
                RequireNative(offset < size && packet[offset++] == 1,
                    "Mega compressed count must remain one");
            RequireNative(offset + payloadSize <= size, "packet payload is truncated");
            std::memcpy(event.Data.Raw, packet + offset, payloadSize);
            offset += payloadSize;
            g_WireEvents.push_back(event);
        }
    }

    void __fastcall TransportSend(FixtureTransport*, void*, const void* packet,
        int size, bool reliable, int player)
    {
        RequireNative(reliable && player == -1, "extra packet must use native reliable broadcast shape");
        const auto* const bytes = static_cast<const std::uint8_t*>(packet);
        g_Packets.emplace_back(bytes, bytes + size);
        DecodePacket(bytes, size);
    }

    void __fastcall TransportFlush(FixtureTransport*, void*)
    {
        if (g_CaptureDuringFlush)
        {
            g_CaptureDuringFlush = false;
            RequireNative(reinterpret_cast<MegaFunction>(MEGA_ADDRESS)(2, nullptr, 9001, 52, nullptr),
                "transport reentry should be captured for a later logical frame");
        }
        if (g_FailFlush) RaiseException(0xE1234001, 0, 0, nullptr);
    }

    int __fastcall OriginalSend(void* transport, events::NetworkEvent* frameInfo,
        int capacityBytes, int frameDelay, std::uint16_t commandCount)
    {
        const auto frame = static_cast<std::uint32_t>(Unsorted::CurrentFrame);
        const auto mode = At<int>(0xA8B24C);
        std::uint32_t targetFrame = frame + static_cast<std::uint32_t>(frameDelay);
        if (mode == 2)
            RequireNative(events::TryGetAlignedFrameTimestamp(frame,
                static_cast<std::uint32_t>(frameDelay), At<std::uint32_t>(0xA8B554), targetFrame),
                "fixture native timestamp must be aligned");
        *frameInfo = {};
        frameInfo->Kind = 0x1C;
        frameInfo->HouseIndex = 2;
        frameInfo->Timestamp = targetFrame;
        frameInfo->Data.FrameInfo = {0x11223344, commandCount, static_cast<std::uint8_t>(frameDelay)};
        const auto count = At<int>(0xA802C8);
        const auto read = (At<int>(0xA802D0) - count) & 127;
        std::vector<events::NetworkEvent> outgoing;
        for (int index = 0; index < count; ++index)
        {
            const auto event = At<events::NetworkEvent>(OUT_EVENTS_ADDRESS +
                ((read + index) & 127) * sizeof(events::NetworkEvent));
            outgoing.push_back(event);
            PushNativeDo(event, targetFrame);
        }
        At<int>(0xA802C8) = 0;
        if (mode == 2 && !outgoing.empty())
        {
            std::vector<std::uint8_t> packet;
            const events::ExtraFrameHeader header{2, targetFrame, 0x11223344, commandCount,
                static_cast<std::uint8_t>(frameDelay)};
            RequireNative(events::BuildExtraFramePacket(header, outgoing,
                static_cast<std::size_t>(capacityBytes), packet), "fixture original packet must fit");
            TransportSend(static_cast<FixtureTransport*>(transport), nullptr,
                packet.data(), static_cast<int>(packet.size()), true, -1);
        }
        return count;
    }

    void StartSession(GameMode mode, std::uint32_t frame,
        int packetMode = 2, std::uint32_t rate = 1, std::uint32_t delay = 0)
    {
        capacity::Disable();
        g_WorldReady = true;
        g_FailFlush = false;
        g_CaptureDuringFlush = false;
        g_Recorded.clear(); g_Executed.clear(); g_WireEvents.clear(); g_Packets.clear();
        At<int>(0x8B41F8) = 0;
        At<int>(0x8B41FC) = DO_CAPACITY - 4;
        At<int>(0x8B4200) = DO_CAPACITY - 4;
        At<int>(0xA802C8) = 0;
        At<int>(0xA802D0) = 124;
        At<std::uint8_t>(0xA8E9A0) = 1;
        At<std::uint32_t>(0xA8D5F8) = 0;
        At<std::uint8_t>(0xAC4CF4) = 0;
        At<int>(0xA8B24C) = packetMode;
        At<std::uint32_t>(0xA8B554) = rate;
        At<std::uint32_t>(0xA8B550) = delay;
        At<int>(0xA8ED84) = static_cast<int>(frame);
        SessionClass::Instance.get().GameMode = mode;
        SessionClass::Instance.get().UniqueID = ++g_SessionId;
        HouseClass::Player = g_Player;
        HouseClass::Observer = nullptr;
        std::string error;
        RequireNative(capacity::Install(error), error.c_str());
        capacity::BindGameThread(GetCurrentThreadId());
        capacity::OnGameFrame();
    }

    void SetFrame(std::uint32_t frame)
    {
        At<int>(0xA8ED84) = static_cast<int>(frame);
        capacity::OnGameFrame();
    }

    void CaptureMany(std::uint32_t count, std::uint32_t first = 1)
    {
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const events::TargetPayload target{500, 52}, destination{600, 11};
            RequireNative(reinterpret_cast<MegaFunction>(MEGA_ADDRESS)(2, &target,
                static_cast<int>(first + index), 52, &destination),
                "actual hooked Mega producer must copy every ordinary actor");
        }
    }

    int SendWindow(int bytes = 4096, void* transport = &g_Transport)
    {
        return reinterpret_cast<SendFunction>(SEND_ADDRESS)(transport,
            reinterpret_cast<events::NetworkEvent*>(g_FrameBuffer.data()), bytes,
            At<int>(0xA8B550), 42);
    }

    void TestLocalReplayAndFifo()
    {
        StartSession(GameMode::Skirmish, 100);
        const auto marker = capacity::GetCaptureMarker();
        CaptureMany(1000);
        RequireNative(capacity::HasCapturedSince(marker) && capacity::GetStats().mPending == 1000,
            "capture marker must report copied events, not physical arrival");
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(g_Executed.size() == 256 && g_Recorded.size() == 256 &&
            capacity::GetStats().mPending == 744, "local first frame must record and execute exactly 256");
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(g_Executed.size() == 256, "same-frame parser reentry must not consume another snapshot");
        for (std::uint32_t frame = 101; capacity::GetStats().mPending; ++frame)
        {
            SetFrame(frame);
            reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        }
        RequireNative(g_Recorded.size() == 1000 && g_Executed.size() == 1000,
            "replay must observe every split local event before native consumer");
        for (std::uint32_t index = 0; index < 1000; ++index)
            RequireNative(ActorId(g_Recorded[index]) == index + 1 &&
                ActorId(g_Executed[index]) == index + 1, "local split frames must preserve complete FIFO");

        StartSession(GameMode::Campaign, 200);
        for (std::uint32_t index = 0; index < 100; ++index) PushNativeDo(MakeMega(2000 + index, 200), 200);
        for (std::uint32_t index = 0; index < 128; ++index) PushNativeOut(MakeMega(3000 + index, 200));
        CaptureMany(129);
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(g_Executed.size() == 256 && capacity::GetStats().mPending == 101,
            "local consumer budget must include due DoList and future native OutList transfer");

        StartSession(GameMode::Skirmish, 300);
        for (int index = 0; index < DO_CAPACITY - 128; ++index)
            PushNativeDo(*events::BuildCapacitySimpleEvent(3, 300, 6, {static_cast<std::uint32_t>(index + 1), 52}), 300);
        CaptureMany(2);
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(capacity::GetStats().mPending == 2,
            "DoList must retain 128 physical slots for original transfer before extra append");
        SetFrame(301);
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(capacity::GetStats().mPending == 0, "backpressure must preserve records for a later frame");
    }

    void TestScopeCancellationAndFallback()
    {
        StartSession(GameMode::Skirmish, 400);
        {
            const ra_commands::game::PluginOrderScope scope({3, 40});
            CaptureMany(1, 4001);
        }
        {
            const ra_commands::game::PluginOrderScope scope({4, 41});
            CaptureMany(1, 4001);
        }
        RequireNative(capacity::HasPendingProducerActor(3, 4001, 52, 40) &&
            !capacity::HasPendingProducerActor(3, 4001, 52, 41), "producer pending must retain intent epoch");
        RequireNative(capacity::CancelPendingProducerActor(3, 4001, 52) == 1 &&
            capacity::HasPendingProducerActor(4, 4001, 52, 41), "producer cancellation must spare other producers");
        RequireNative(capacity::CancelPendingActor(4001, 52) == 1, "manual order must cancel all unsent actor records");
        g_Actor->UniqueID = 4001;
        RequireNative(capacity::TryCaptureSimpleClickedEvent(g_Actor, 6) == capacity::CaptureResult::Queued,
            "verified Stop must be captured after manual cancellation");
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(g_Executed.size() == 1 && g_Executed.front().Kind == 6,
            "cancelled old Move must not revive after Stop");

        StartSession(GameMode::LAN, 500, 1);
        CaptureMany(257);
        SendWindow();
        RequireNative(capacity::GetStats().mPending == 141 && g_Packets.empty() && At<int>(0xA802C8) == 0,
            "mode1 must split via original OutList, preserve queue, and avoid compressed extra channel");
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        SetFrame(501); SendWindow(); reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        SetFrame(502); SendWindow(); reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(capacity::GetStats().mPending == 0, "native fallback must eventually accept all copied events");

        StartSession(GameMode::Skirmish, 600);
        CaptureMany(1);
        At<std::uint32_t>(0xA8D5F8) = 2;
        capacity::OnGameFrame();
        RequireNative(capacity::GetStats().mPending == 0 &&
            !reinterpret_cast<MegaFunction>(MEGA_ADDRESS)(7, nullptr, 77, 52, nullptr),
            "native replay flag must clear production backlog and forward native producer");
        RequireNative(g_OriginalMission == 7 && g_OriginalSelfId == 77 && g_OriginalSelfKind == 52,
            "forwarded base fastcall must preserve register and stack arguments");
        RequireNative(!reinterpret_cast<FastFunction>(FAST_ADDRESS)(8, nullptr, 78, 52, nullptr, -12, 500) &&
            g_OriginalSpeed == -12 && g_OriginalMaximumSpeed == 500 && g_OriginalSelfId == 78,
            "fastcall kind5 fallback must preserve all five stack arguments");

        StartSession(GameMode::Skirmish, 700);
        CaptureMany(1);
        At<std::uint8_t>(0xAC4CF4) = 1;
        RequireNative(capacity::TryCaptureSimpleClickedEvent(g_Actor, 9) == capacity::CaptureResult::NotHandled &&
            !reinterpret_cast<MegaFunction>(MEGA_ADDRESS)(2, nullptr, 90, 52, nullptr),
            "planning mode must retain original producer rules");
        At<std::uint8_t>(0xAC4CF4) = 0;
        capacity::Disable();
        RequireNative(capacity::GetStats().mPending == 0 && !capacity::HasCapturedSince(capacity::CaptureMarker{0, 0}),
            "disable must invalidate captures and release pending ownership");
    }

    void TestNetworkWireAndCollision()
    {
        StartSession(GameMode::Internet, 800, 2, 1, 0);
        CaptureMany(257);
        RequireNative(SendWindow() == 128 && g_WireEvents.size() == 128 && At<int>(0x8B41F8) == 128,
            "first aligned window must send and self-echo exactly 128 extras");
        RequireNative(SendWindow() == 0 && g_WireEvents.size() == 128,
            "duplicate same-timestamp window must not consume any extra records");
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        SetFrame(801); RequireNative(SendWindow() == 128, "next timestamp should open another 128-event window");
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        SetFrame(802); RequireNative(SendWindow() == 1, "final timestamp should send the retained FIFO tail");
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(g_WireEvents.size() == 257 && g_Executed.size() == 257,
            "remote-compatible decode and local self-echo must contain all events");
        for (std::uint32_t index = 0; index < 257; ++index)
            RequireNative(std::memcmp(&g_WireEvents[index], &g_Executed[index], sizeof(events::NetworkEvent)) == 0,
                "wire and local execution must have identical event, house, and timestamp shape");

        StartSession(GameMode::LAN, 900, 2, 1, 0);
        for (std::uint32_t index = 0; index < 128; ++index) PushNativeOut(MakeMega(9000 + index, 900));
        CaptureMany(129);
        RequireNative(SendWindow(4096) == 256 && g_WireEvents.size() == 256,
            "original 128 events must be sent before 128 extra events");
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(g_Executed.size() == 256 && capacity::GetStats().mPending == 1,
            "original plus extra must stay within unchanged receiver cap");

        StartSession(GameMode::LAN, 1000, 2, 3, 4);
        CaptureMany(257);
        RequireNative(SendWindow(64) == 128 && g_Packets.size() == 64,
            "byte budget should split a target frame into packets without exceeding its event budget");
        const auto sent = g_WireEvents.size();
        SetFrame(1001);
        At<std::uint32_t>(0xA8B554) = 2;
        capacity::OnGameFrame();
        SendWindow(4096);
        RequireNative(g_WireEvents.size() > sent && capacity::GetStats().mPending == 13,
            "quantum change must route retained backlog through bounded original OutList");
        // 第一批extra目标帧1005未经过consumer；后续原生同stamp补量不得超过256。
        SetFrame(1002); SendWindow(4096);
        RequireNative(capacity::GetStats().mPending <= 13 && g_WireEvents.size() <= 257,
            "timing collision fallback must not duplicate already accepted extras");
        SetFrame(1007);
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        SendWindow(4096);
        RequireNative(capacity::GetStats().mPending == 0,
            "passed in-flight timestamp must reopen aligned extras for any retained tail");

        StartSession(GameMode::Internet, 1100, 2, 1, 0);
        CaptureMany(2);
        RequireNative(SendWindow(38) == 0 && capacity::GetStats().mPending == 2,
            "a packet too small for one complete Mega must preserve the entire prefix");
        SetFrame(1101);
        RequireNative(SendWindow(4096, nullptr) == 0 && capacity::GetStats().mPending == 2,
            "unavailable native transport must not consume any record");
        SetFrame(1102);
        g_CaptureDuringFlush = true;
        RequireNative(SendWindow() == 2 && capacity::GetStats().mPending == 1 &&
            capacity::HasPendingActor(9001, 52), "transport reentry must be queued beyond the snapshot cutoff");
        RequireNative(SendWindow() == 0 && capacity::GetStats().mPending == 1,
            "same-frame second window must leave transport reentry for the next logical frame");
        SetFrame(1103); RequireNative(SendWindow() == 1, "next frame should send transport reentry once");

        StartSession(GameMode::Internet, 1200, 2, 1, 0);
        CaptureMany(1);
        g_FailFlush = true;
        SendWindow();
        RequireNative(g_WireEvents.size() == 1 && capacity::GetStats().mPending == 0 &&
            capacity::GetStats().mFaultedSubmissions > 0 && !capacity::GetStats().mReady,
            "exception after send invocation must discard unknown-progress copies and stop retries");
        SetFrame(1201); SendWindow();
        RequireNative(g_WireEvents.size() == 1, "accepted or uncertain packet must never be resent");
    }

    void TestMalformedNativeRing()
    {
        StartSession(GameMode::Skirmish, 1300);
        CaptureMany(1);
        const auto read = At<int>(0x8B41FC);
        At<int>(0x8B4200) = (read + 1) & (DO_CAPACITY - 1);
        const auto badWrite = At<int>(0x8B4200);
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(At<int>(0x8B41F8) == 0 && At<int>(0x8B4200) == badWrite &&
            capacity::GetStats().mPending == 1 && g_Recorded.empty(),
            "in-range but inconsistent DoList heads must not receive local copied events");
        At<int>(0x8B4200) = read;
        SetFrame(1301);
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(capacity::GetStats().mPending == 0 && g_Recorded.size() == 1,
            "repaired native ring must accept retained prefix in a later logical frame");

        StartSession(GameMode::Internet, 1400);
        CaptureMany(1);
        At<int>(0x8B4200) = (At<int>(0x8B41FC) + 1) & (DO_CAPACITY - 1);
        RequireNative(SendWindow() == 0 && g_Packets.empty() && At<int>(0x8B41F8) == 0 &&
            capacity::GetStats().mPending == 1,
            "inconsistent DoList must prevent an extra send that cannot self-echo safely");
        At<int>(0x8B4200) = At<int>(0x8B41FC);
        SetFrame(1401);
        RequireNative(SendWindow() == 1 && capacity::GetStats().mPending == 0,
            "repaired ring and a new timestamp must release the preserved network prefix");
    }
}

namespace ra_commands::game
{
    bool IsGameSessionReady() { return g_WorldReady; }
    bool IsLocal(const TechnoClass* actor) { return actor && actor->Owner == g_Player; }
}

int main()
{
    try
    {
        Map(0x640000, 0x10000);
        Map(0x6E0000, 0x10000);
        Map(0x8B0000, 0x260000);
        RunEventBacklogTests();
        RunExtraFrameTests();
        RunCommandCapacityTests();
        g_Player = static_cast<HouseClass*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(HouseClass)));
        g_Actor = static_cast<TechnoClass*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TechnoClass)));
        RequireNative(g_Player && g_Actor, "fixture game object allocation failed");
        g_Player->ArrayIndex = 2;
        g_Actor->Owner = g_Player;
        g_Actor->UniqueID = 4001;
        g_ActorVtable.fill(reinterpret_cast<void*>(&DeadStub));
        *reinterpret_cast<void***>(g_Actor) = g_ActorVtable.data();
        g_TransportVtable[1] = reinterpret_cast<void*>(&TransportFlush);
        g_TransportVtable[2] = reinterpret_cast<void*>(&TransportSend);
        WriteJump(MEGA_ADDRESS, &OriginalMega);
        WriteJump(FAST_ADDRESS, &OriginalFast);
        WriteJump(PARSER_ADDRESS, &OriginalParser);
        WriteJump(SEND_ADDRESS, &OriginalSend);
        WriteJump(CONSUMER_ADDRESS, &ReplayThenConsumer);
        WriteJump(PACK_ADDRESS, &PackStub);
        TestLocalReplayAndFifo();
        TestScopeCancellationAndFallback();
        TestNetworkWireAndCollision();
        TestMalformedNativeRing();
        capacity::Disable();
        std::cout << "Command capacity pure tests and Win32 native fixture passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Command capacity fixture failed: " << error.what() << '\n';
        return 1;
    }
}
