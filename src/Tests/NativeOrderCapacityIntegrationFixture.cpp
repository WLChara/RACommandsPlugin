// 共用隔离原址、假的世界与网络接收器；不复制既有 ABI 夹具，也不修改生产状态。
#define main RunCommandCapacityFixtureBaseline
#include "CommandCapacityNativeFixture.cpp"
#undef main

#include "Hooks/AFloorHook.h"
#include <DisplayClass.h>

namespace
{
    namespace game = ra_commands::game;
    constexpr std::uintptr_t ORDER_LEFT_ADDRESS = 0x4AB9B0;
    constexpr std::uintptr_t ORDER_MISSION_ADDRESS = 0x6FFBE0;
    constexpr std::uintptr_t ORDER_EVENT_ADDRESS = 0x6FFE00;
    // 目标 SDK 的主虚表：WhatAmI +0x2C、IsDead +0x44；只替换隔离 actor 的虚表。
    constexpr std::size_t WHAT_AM_I_SLOT = 0x2C / sizeof(void*);
    constexpr std::size_t IS_DEAD_SLOT = 0x44 / sizeof(void*);
    using OrderMissionFunction = char(__thiscall*)(TechnoClass*, Mission,
        AbstractClass*, AbstractClass*, CellClass*);
    using OrderEventFunction = bool(__thiscall*)(TechnoClass*, NetworkEvents);
    using OrderLeftFunction = void(__thiscall*)(DisplayClass*, const CoordStruct&,
        const CellStruct&, ObjectClass*, Action, DWORD);

    std::uint32_t g_FormationObserverCalls = 0;
    std::uint32_t g_ManualObserverCalls = 0;
    std::uint32_t g_MissionChainCalls = 0;
    std::uint32_t g_EventChainCalls = 0;
    std::uint32_t g_LeftChainCalls = 0;
    bool g_EventChainResult = false;
    TechnoClass* g_LastMissionActor = nullptr;
    Mission g_LastMission = Mission::None;
    AbstractClass* g_LastMissionTarget = nullptr;
    AbstractClass* g_LastEnterTarget = nullptr;
    CellClass* g_LastNearestCell = nullptr;
    TechnoClass* g_LastEventActor = nullptr;
    NetworkEvents g_LastEvent = NetworkEvents::Empty;
    std::array<std::uint8_t, sizeof(DisplayClass)> g_DisplayBytes{};

    AbstractType __fastcall ActorTypeStub(const TechnoClass*, void*)
    {
        return AbstractType::Unit;
    }

    bool IsFloorDisabled() { return false; }

    void OnFormationOrder(std::uint64_t actorId)
    {
        RequireNative(static_cast<std::uint32_t>(actorId >> 32) ==
                reinterpret_cast<std::uintptr_t>(g_Actor) &&
                static_cast<std::uint32_t>(actorId) == g_Actor->UniqueID,
            "formation observer identity lost pointer or UniqueID");
        ++g_FormationObserverCalls;
        (void)capacity::CancelPendingActor(static_cast<std::uint32_t>(actorId), 52);
    }

    void OnManualOrder(std::uint64_t actorId)
    {
        RequireNative(static_cast<std::uint32_t>(actorId) == g_Actor->UniqueID,
            "manual observer lost actor identity");
        ++g_ManualObserverCalls;
        (void)capacity::CancelPendingActor(static_cast<std::uint32_t>(actorId), 52);
    }

    char __fastcall ExistingMissionChain(TechnoClass* actor, void*, Mission mission,
        AbstractClass* target, AbstractClass* enterTarget, CellClass* nearestCell)
    {
        ++g_MissionChainCalls;
        g_LastMissionActor = actor;
        g_LastMission = mission;
        g_LastMissionTarget = target;
        g_LastEnterTarget = enterTarget;
        g_LastNearestCell = nearestCell;
        const events::TargetPayload packedTarget{500, 52}, destination{600, 11};
        // 模拟原生 ClickedMission 的下游调用，必须经过真实 646E90 detour。
        return reinterpret_cast<MegaFunction>(MEGA_ADDRESS)(static_cast<int>(mission),
            &packedTarget, static_cast<int>(actor->UniqueID), 52, &destination) ? 1 : 0;
    }

    bool __fastcall ExistingEventChain(TechnoClass* actor, void*, NetworkEvents event)
    {
        ++g_EventChainCalls;
        g_LastEventActor = actor;
        g_LastEvent = event;
        return g_EventChainResult;
    }

    void __fastcall ExistingLeftChain(DisplayClass* display, void*, const CoordStruct& coords,
        const CellStruct& cell, ObjectClass* target, Action action, DWORD argument)
    {
        RequireNative(display == reinterpret_cast<DisplayClass*>(g_DisplayBytes.data()) &&
                coords.X == 111 && coords.Y == 222 && coords.Z == 333 &&
                cell.X == 7 && cell.Y == 9 && target == g_Actor &&
                action == Action::Move && argument == 0x12345678,
            "left-click E9 chain changed thiscall register or stack arguments");
        ++g_LeftChainCalls;
        RequireNative(reinterpret_cast<OrderMissionFunction>(ORDER_MISSION_ADDRESS)(
                g_Actor, Mission::Move, target, nullptr, nullptr) == 1,
            "manual left-click downstream capture did not report success");
    }

    char IssueMission(std::uint32_t actorId, Mission mission = Mission::Move,
        AbstractClass* target = nullptr, AbstractClass* enterTarget = nullptr,
        CellClass* nearestCell = nullptr)
    {
        g_Actor->UniqueID = actorId;
        return reinterpret_cast<OrderMissionFunction>(ORDER_MISSION_ADDRESS)(g_Actor,
            mission, target, enterTarget, nearestCell);
    }

    bool IssueEvent(std::uint32_t actorId, int kind)
    {
        g_Actor->UniqueID = actorId;
        return reinterpret_cast<OrderEventFunction>(ORDER_EVENT_ADDRESS)(
            g_Actor, static_cast<NetworkEvents>(kind));
    }

    void TestPluginScopeAndManualStop()
    {
        StartSession(GameMode::Skirmish, 2000);
        const auto observersBefore = g_FormationObserverCalls;
        const auto manualBefore = g_ManualObserverCalls;
        const auto megaBefore = g_OriginalMegaCalls;
        const auto missionBefore = g_MissionChainCalls;
        const auto cancelledBefore = capacity::GetStats().mCancelled;
        auto* const target = static_cast<AbstractClass*>(g_Actor);
        auto* const nearest = reinterpret_cast<CellClass*>(g_Player);
        {
            const game::PluginOrderScope scope({3, 401});
            RequireNative(IssueMission(5001, Mission::Move, target, target, nearest) == 1,
                "plugin Move must report copied backlog acceptance through native ClickedMission");
            RequireNative(g_LastMissionActor == g_Actor && g_LastMission == Mission::Move &&
                    g_LastMissionTarget == target && g_LastEnterTarget == target &&
                    g_LastNearestCell == nearest,
                "plugin ClickedMission E9 chain changed register or any stack argument");
        }
        {
            const game::PluginOrderScope scope({4, 402});
            RequireNative(IssueMission(5001) == 1, "another producer Move must remain independent");
        }
        RequireNative(g_FormationObserverCalls == observersBefore &&
                g_ManualObserverCalls == manualBefore && g_MissionChainCalls == missionBefore + 2 &&
                g_OriginalMegaCalls == megaBefore && capacity::GetStats().mPending == 2 &&
                capacity::HasPendingProducerActor(3, 5001, 52, 401) &&
                capacity::HasPendingProducerActor(4, 5001, 52, 402) && !game::IsPluginOrderActive(),
            "plugin scope was treated as manual or lost producer/epoch after E9 forwarding");
        RequireNative(g_LastMissionActor == g_Actor && g_LastMission == Mission::Move,
            "ClickedMission thiscall actor or mission changed in trampoline");

        RequireNative(IssueEvent(5001, 6), "native Stop must return true after capture");
        RequireNative(g_FormationObserverCalls == observersBefore + 1 &&
                g_ManualObserverCalls == manualBefore && capacity::GetStats().mPending == 1 &&
                capacity::GetStats().mCancelled == cancelledBefore + 2 &&
                !capacity::HasPendingProducerActor(3, 5001, 52, 401) &&
                !capacity::HasPendingProducerActor(4, 5001, 52, 402),
            "manual Stop did not cancel both unsent producer Moves before append");
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        for (std::uint32_t frame = 2001; frame <= 2004; ++frame)
        {
            SetFrame(frame);
            reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        }
        RequireNative(g_Executed.size() == 1 && g_Recorded.size() == 1 &&
                g_Executed.front().Kind == 6 && ActorId(g_Executed.front()) == 5001,
            "cancelled old Move revived in a later parser frame");

        StartSession(GameMode::Skirmish, 2010);
        const auto newObserversBefore = g_FormationObserverCalls;
        {
            const game::PluginOrderScope scope({3, 403});
            RequireNative(IssueMission(5002), "plugin Move capture failed");
        }
        {
            const game::PluginOrderScope scope({4, 404});
            RequireNative(IssueEvent(5002, 9), "plugin simple ClickedEvent capture failed");
        }
        RequireNative(capacity::GetStats().mPending == 2 &&
                capacity::HasPendingProducerActor(3, 5002, 52, 403) &&
                capacity::HasPendingProducerActor(4, 5002, 52, 404) &&
                g_FormationObserverCalls == newObserversBefore,
            "plugin simple event triggered manual cancellation or lost metadata");
    }

    void TestManualLeftOrderAndOriginalFallback()
    {
        StartSession(GameMode::Skirmish, 2100);
        const auto manualBefore = g_ManualObserverCalls;
        const auto formationBefore = g_FormationObserverCalls;
        const auto leftBefore = g_LeftChainCalls;
        const auto cancelledBefore = capacity::GetStats().mCancelled;
        {
            const game::PluginOrderScope scope({5, 501});
            RequireNative(IssueMission(5100), "left-click fixture old Move capture failed");
        }
        const CoordStruct coords{111, 222, 333};
        const CellStruct cell{7, 9};
        reinterpret_cast<OrderLeftFunction>(ORDER_LEFT_ADDRESS)(
            reinterpret_cast<DisplayClass*>(g_DisplayBytes.data()), coords, cell,
            g_Actor, Action::Move, 0x12345678);
        RequireNative(g_LeftChainCalls == leftBefore + 1 &&
                g_ManualObserverCalls == manualBefore + 1 &&
                g_FormationObserverCalls == formationBefore + 1 &&
                capacity::GetStats().mCancelled == cancelledBefore + 1 &&
                capacity::GetStats().mPending == 1 &&
                !capacity::HasPendingProducerActor(5, 5100, 52, 501),
            "manual left-click failed to cancel old Move exactly once before its replacement");

        const auto eventBefore = g_EventChainCalls;
        g_EventChainResult = true;
        RequireNative(IssueEvent(5101, 8), "unsupported event did not preserve original true result");
        g_EventChainResult = false;
        RequireNative(!IssueEvent(5102, 15) && g_EventChainCalls == eventBefore + 2 &&
                g_LastEventActor == g_Actor && g_LastEvent == static_cast<NetworkEvents>(15),
            "unsupported ClickedEvent did not preserve E9 actor, stack kind, or false result");

        At<std::uint8_t>(0xAC4CF4) = 1;
        const auto originalBefore = g_OriginalMegaCalls;
        auto* const target = static_cast<AbstractClass*>(g_Actor);
        auto* const nearest = reinterpret_cast<CellClass*>(g_Player);
        RequireNative(IssueMission(5103, Mission::Attack, target, target, nearest) == 0 &&
                g_OriginalMegaCalls == originalBefore + 1 &&
                g_LastMissionActor == g_Actor && g_LastMission == Mission::Attack &&
                g_LastMissionTarget == target && g_LastEnterTarget == target &&
                g_LastNearestCell == nearest && g_OriginalSelfId == 5103 &&
                g_OriginalSelfKind == 52 && g_OriginalMission == static_cast<int>(Mission::Attack),
            "planning fallback did not forward both native E9 chains or preserve ClickedMission ABI");
        At<std::uint8_t>(0xAC4CF4) = 0;
    }

    void TestSimpleKindsAndFiveHundredStops()
    {
        StartSession(GameMode::Skirmish, 2200);
        constexpr std::array<int, 5> SIMPLE_KINDS{6, 7, 9, 10, 22};
        const auto originalEventsBefore = g_EventChainCalls;
        for (std::size_t index = 0; index < SIMPLE_KINDS.size(); ++index)
            RequireNative(IssueEvent(static_cast<std::uint32_t>(6000 + index), SIMPLE_KINDS[index]),
                "verified native simple event failed at actual 6FFE00 Hook");
        RequireNative(capacity::GetStats().mPending == SIMPLE_KINDS.size() &&
                g_EventChainCalls == originalEventsBefore,
            "simple event capture fell through the original producer or lost backlog entries");
        reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        RequireNative(g_Executed.size() == SIMPLE_KINDS.size(), "simple events were not all consumed");
        for (std::size_t index = 0; index < SIMPLE_KINDS.size(); ++index)
        {
            const auto expected = events::BuildCapacitySimpleEvent(2, 2200,
                static_cast<std::uint8_t>(SIMPLE_KINDS[index]),
                {static_cast<std::uint32_t>(6000 + index), 52});
            RequireNative(expected && std::memcmp(&g_Executed[index], &*expected,
                    sizeof(events::NetworkEvent)) == 0,
                "actual simple capture changed native actor kind 52 or 0x6F-byte event shape");
        }

        StartSession(GameMode::LAN, 2300, 2, 1, 0);
        constexpr std::uint32_t STOP_COUNT = 500;
        constexpr std::uint32_t FIRST_ACTOR_ID = 10000;
        const auto observersBefore = g_FormationObserverCalls;
        const auto cancelledBefore = capacity::GetStats().mCancelled;
        const auto acceptedBefore = capacity::GetStats().mAccepted;
        {
            const game::PluginOrderScope scope({7, 701});
            for (std::uint32_t index = 0; index < STOP_COUNT; ++index)
                RequireNative(IssueMission(FIRST_ACTOR_ID + index), "500-actor old Move capture failed");
        }
        RequireNative(capacity::GetStats().mPending == STOP_COUNT &&
                g_FormationObserverCalls == observersBefore,
            "500 plugin Moves did not remain queued independently of manual observers");
        for (std::uint32_t index = 0; index < STOP_COUNT; ++index)
            RequireNative(IssueEvent(FIRST_ACTOR_ID + index, 6), "actual native 500-actor Stop was lost");
        RequireNative(capacity::GetStats().mPending == STOP_COUNT &&
                capacity::GetStats().mCancelled == cancelledBefore + STOP_COUNT &&
                capacity::GetStats().mAccepted == acceptedBefore + 2 * STOP_COUNT &&
                g_FormationObserverCalls == observersBefore + STOP_COUNT &&
                g_EventChainCalls == originalEventsBefore,
            "500 native Stops did not replace old unsent Moves before legacy OutList saturation");
        for (std::uint32_t frame = 2300; frame < 2308; ++frame)
        {
            SetFrame(frame);
            const int submitted = SendWindow();
            RequireNative(submitted >= 0 && submitted <= 128,
                "native stop extras exceeded unchanged per-timestamp network budget");
            reinterpret_cast<ParserFunction>(PARSER_ADDRESS)();
        }
        RequireNative(capacity::GetStats().mPending == 0 && g_WireEvents.size() == STOP_COUNT &&
                g_Executed.size() == STOP_COUNT && g_Recorded.size() == STOP_COUNT,
            "500 Stops were truncated, duplicated, or old Moves revived across parser frames");
        for (std::uint32_t index = 0; index < STOP_COUNT; ++index)
            RequireNative(g_Executed[index].Kind == 6 &&
                    ActorId(g_Executed[index]) == FIRST_ACTOR_ID + index &&
                    g_Executed[index].Data.Raw[4] == 52 &&
                    std::memcmp(&g_WireEvents[index], &g_Executed[index], sizeof(events::NetworkEvent)) == 0,
                "network Stop FIFO, native target identity, or wire/self-echo equivalence changed");
    }
}

int main()
{
    if (RunCommandCapacityFixtureBaseline() != 0) return 1;
    try
    {
        Map(0x4A0000, 0x10000);
        Map(0x6F0000, 0x10000);
        g_ActorVtable[WHAT_AM_I_SLOT] = reinterpret_cast<void*>(&ActorTypeStub);
        g_ActorVtable[IS_DEAD_SLOT] = reinterpret_cast<void*>(&DeadStub);
        RequireNative(g_Actor->WhatAmI() == AbstractType::Unit && !g_Actor->IsDead(),
            "isolated actor SDK primary virtual layout is invalid");
        WriteJump(ORDER_LEFT_ADDRESS, &ExistingLeftChain);
        WriteJump(ORDER_MISSION_ADDRESS, &ExistingMissionChain);
        WriteJump(ORDER_EVENT_ADDRESS, &ExistingEventChain);
        std::string error;
        // 低层已初始化 MinHook；ClickedEvent 必须能独立于 A-floor 双 Hook 安装。
        RequireNative(game::InstallFormationOrderHook(&OnFormationOrder, error), error.c_str());
        RequireNative(game::InstallAFloorHooks(&IsFloorDisabled, error), error.c_str());
        game::SetManualVehicleOrderObserver(&OnManualOrder);
        game::SetAFloorGameThread(GetCurrentThreadId());
        TestPluginScopeAndManualStop();
        std::cout << "Plugin scopes, producer epochs, and manual Stop cancellation passed.\n";
        TestManualLeftOrderAndOriginalFallback();
        std::cout << "Manual left order and E9 native fallback ABI passed.\n";
        TestSimpleKindsAndFiveHundredStops();
        std::cout << "Five simple kinds and 500 native Stops across network frames passed.\n";
        capacity::Disable();
        game::DisableAFloorHooks();
        std::cout << "Native order / command capacity real-Hook integration passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Native order capacity integration failed: " << error.what() << '\n';
        return 1;
    }
}
