#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ra_commands::autoload
{
    using UnitId = std::uint64_t;

    enum class UnitKind
    {
        Other,
        Infantry,
        Vehicle,
        Aircraft
    };

    enum class RuleKind
    {
        Forbid,
        Priority
    };

    struct Unit
    {
        UnitId Id = 0;
        std::uintptr_t Address = 0;
        UnitKind Kind = UnitKind::Other;
        std::string TypeName;
        bool HasOwner = false;
        bool IsLocalOrAllied = false;
        bool IsInPlayfield = false;
        bool IsInTransport = false;
        bool IsEnteringTransport = false;
        bool IsDog = false;
        bool UsesFlyingMovement = false;
        bool HasType = false;
        bool HasIfvMode = true;
        bool HasPrimaryWeaponRangeAtLeast3 = true;
        bool IsArmed = false;
        bool IsOpenTopped = false;
        int PassengerCount = 0;
        int PassengerCapacity = 0;
        double Size = 0.0;
        double SizeLimit = 0.0;
        std::int32_t X = 0;
        std::int32_t Y = 0;
        std::int32_t Z = 0;
    };

    struct LoadingRule
    {
        RuleKind Kind = RuleKind::Forbid;
        // 与 FVModule 配置一致，星号匹配任意载具或乘客的注册名。
        std::string TransportName = "*";
        std::string PassengerName = "*";
        int Priority = 10;
        int MaxCount = 1;
    };

    struct Policy
    {
        bool UseCustomRules = false;
        bool RequireArmedOrOpenToppedTransport = false;
        bool RejectPassengersWithoutIfvMode = false;
        bool RejectPassengersWithWeaponRangeLessThan3 = false;
        std::vector<LoadingRule> LoadingRules;
    };

    // 候选集合保留游戏数组的枚举顺序；本方集合由游戏适配器按本地玩家和盟友筛选。
    struct Snapshot
    {
        std::vector<Unit> Units;
        std::vector<UnitId> SelectedInfantries;
        std::vector<UnitId> SelectedVehicles;
        std::vector<UnitId> FriendlyPassengers;
        std::vector<UnitId> FriendlyTransports;
        Policy LoadPolicy;
    };

    enum class PairKind
    {
        Command,
        VehicleIntoVehicle,
        InfantryFallback
    };

    struct Pair
    {
        UnitId Passenger = 0;
        UnitId Transport = 0;
        PairKind Kind = PairKind::Command;
        int Priority = 0;
        int MaxCount = 0;
    };

    /**
     * 仅规划自动装车配对，不访问游戏对象，也不发出任务。
     * 调用方负责在游戏线程对配对重新验证，并处理实际 ClickedMission 提交。
     */
    [[nodiscard]] std::vector<Pair> Plan(const Snapshot& snapshot);

    /** 安全模式优先安排步兵，并且每次只规划装载一辆目标载具。 */
    [[nodiscard]] std::vector<Pair> PlanSafeMode(const Snapshot& snapshot);
}
