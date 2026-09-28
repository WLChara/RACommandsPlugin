#include "Commands/AutoFormationCommand/NativeCellOccupancy.h"
#include "Commands/AutoFormationCommand/AutoFormationMoveState.h"

#include <array>
#include <stdexcept>

namespace
{
    using namespace ra_commands::auto_formation;

    bool Allows(const std::array<std::uint8_t, 4>& nativeField, bool hasVehicle)
    {
        return OccupationAllowsVehicleContents(ReadNativeOccupationByte(nativeField.data()), hasVehicle);
    }

    void Require(bool condition, const char* message)
    {
        if (!condition) { throw std::runtime_error(message); }
    }
}

void RunAutoFormationGameAdapterRulesTests()
{
    // 原生只初始化/读取首字节；高三字节非零是回归所需的黄金布局。
    const std::array<std::uint8_t, 4> empty{0x00, 0xA5, 0x3C, 0xFF};
    const std::array<std::uint8_t, 4> vehicle{0x20, 0xA5, 0x3C, 0xFF};
    const std::array<std::uint8_t, 4> infantry{0x01, 0xA5, 0x3C, 0xFF};
    const std::array<std::uint8_t, 4> object{0x40, 0xA5, 0x3C, 0xFF};
    const std::array<std::uint8_t, 4> building{0x80, 0xA5, 0x3C, 0xFF};
    const std::array<std::uint8_t, 4> mixed{0x21, 0xA5, 0x3C, 0xFF};

    Require(Allows(empty, false), "native empty cell must ignore neighboring nonzero bytes");
    Require(Allows(vehicle, true), "native vehicle content must ignore neighboring nonzero bytes");
    Require(!Allows(vehicle, false), "unowned native vehicle reservation must remain blocked");
    Require(!Allows(infantry, false) && !Allows(object, false) && !Allows(building, false),
        "native infantry object and building bits must remain blocked");
    Require(!Allows(mixed, true), "vehicle content must not hide native infantry mixed occupancy");

    for (unsigned raw = 0; raw <= 0xFF; ++raw)
    {
        const auto flags = static_cast<std::uint8_t>(raw);
        const std::array<std::uint8_t, 4> clean{flags, 0, 0, 0};
        const std::array<std::uint8_t, 4> neighbors{flags, 0xA5, 0x3C, 0xFF};
        Require(Allows(clean, false) == Allows(neighbors, false) &&
            Allows(clean, true) == Allows(neighbors, true),
            "neighbor bytes must not change any native occupation decision");
    }

    // 旧任务的自然变化不是取消依据。未发送时只查询高级队列，已发送但尚未
    // 观察到新目的地时只查询应用宽限；不以旧 Mission/QueuedMission/开始时间比较。
    Require(ObservePendingMovePhase(false, true, false, 0, 60) == MoveState::Queued,
        "unissued formation must remain queued while old Move progresses");
    Require(ObservePendingMovePhase(false, false, false, 45, 60) == MoveState::Waiting,
        "unissued formation without pending event must allow bounded retry after old Move ends");
    Require(ObservePendingMovePhase(true, false, false, 0, 60) == MoveState::Moving &&
        ObservePendingMovePhase(true, false, false, 30, 60) == MoveState::Moving &&
        ObservePendingMovePhase(true, false, false, 60, 60) == MoveState::Moving,
        "issued formation must tolerate old task transitions throughout apply grace");
    Require(ObservePendingMovePhase(true, false, false, 61, 60) == MoveState::Waiting,
        "unobserved destination after apply grace must retry rather than cancel as overridden");
    Require(!ObservePendingMovePhase(true, false, true, 61, 60).has_value(),
        "observed formation destination must delegate to actual native movement observation");

    Require(NativeMoveAllowsGroupDestination(0, false, false),
        "empty legal native destination must be accepted without another participant");
    Require(NativeMoveAllowsGroupDestination(6, true, false) &&
        NativeMoveAllowsGroupDestination(2, true, false),
        "stopped and moving authorized teammates must not deadlock the assigned destination");
    Require(!NativeMoveAllowsGroupDestination(6, false, false) &&
        !NativeMoveAllowsGroupDestination(2, false, false),
        "unassigned or manually canceled vehicles must not authorize temporary occupancy");
    Require(!NativeMoveAllowsGroupDestination(7, true, false) &&
        NativeMoveAllowsGroupDestination(7, true, true) &&
        !NativeMoveAllowsGroupDestination(7, false, true),
        "native No may be relaxed only for the verified authorized dynamic head-on case");
    for (const int blocked : {1, 3, 4, 5, 8})
    {
        Require(!NativeMoveAllowsGroupDestination(blocked, true, true),
            "group authorization must not permit cloak gate destroyable or unknown native result");
    }

    // 原生迎面避碰方向与 511-lepton 距离边界的固定输入，不依赖模拟游戏类。
    Require(IsNativeHeadOnBlock(0, 0x8000, 0, -256, 0, true) &&
        IsNativeHeadOnBlock(0x4000, 0xC000, 256, 0, 0, true) &&
        IsNativeHeadOnBlock(0x8000, 0, 0, 256, 0, true) &&
        IsNativeHeadOnBlock(0xC000, 0x4000, -256, 0, 0, true),
        "native cardinal head-on moving scenes must be recognized");
    Require(IsNativeHeadOnBlock(0, 0x8000, 0, -511, 0, true) &&
        !IsNativeHeadOnBlock(0, 0x8000, 0, -512, 0, true),
        "native head-on relaxation must remain within its verified distance bound");
    Require(!IsNativeHeadOnBlock(0, 0x8000, 0, -256, 0, false) &&
        !IsNativeHeadOnBlock(0, 0, 0, -256, 0, true) &&
        !IsNativeHeadOnBlock(0, 0x8000, 0, 256, 0, true),
        "stopped same-direction and behind-actor scenes must not use the native No exception");

    // 同一个固定目标：先观察到了目标任务，随后原生改为附近格并继续走，最后
    // 在旁格停下。整个序列必须保留目标并重试，不能把原生停靠当作玩家新命令。
    Require(ObserveIssuedGoalState(false, false, true) == MoveState::Moving &&
        ObserveIssuedGoalState(false, true, true) == MoveState::Waiting &&
        ObserveIssuedGoalState(false, true, false) == MoveState::Waiting &&
        ObserveIssuedGoalState(true, false, true) == MoveState::Moving &&
        ObserveIssuedGoalState(true, true, false) == MoveState::Arrived,
        "native detour and nearby parking must retain the goal until exact stopped arrival");

    // Win32 offsetof + 原生黄金布局：0x6B6 是 IsStandingStill 的正常状态字节。
    // 同样的15辆车从停步到走动再停步，资格必须保持一致；该字节不进下令门禁。
    for (const std::uint8_t stopped : {std::uint8_t{1}, std::uint8_t{0}, std::uint8_t{1}})
    {
        std::size_t eligible = 0;
        for (int actor = 0; actor < 15; ++actor)
        {
            std::array<std::uint8_t, 0x6B7> nativeActor{};
            nativeActor[0x6B6] = stopped;
            if (CanAcceptFormationMove(nativeActor[0x1C8] != 0,
                nativeActor[0x27C] != 0, nativeActor[0x6AD] != 0)) { ++eligible; }
        }
        Require(eligible == 15, "all 15 ordinary tanks must stay eligible through native stopped/moving/stopped");
    }
    Require(!CanAcceptFormationMove(true, false, false) &&
        !CanAcceptFormationMove(false, true, false) &&
        !CanAcceptFormationMove(false, false, true),
        "deactivation chrono immobilization and locomotor attacks must remain protected");
}
