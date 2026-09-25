#pragma once

#include <cstdint>
#include <vector>

namespace ra_commands::auto_repair
{
    struct BuildingId
    {
        std::uintptr_t Address = 0;
        std::uint32_t UniqueId = 0;

        [[nodiscard]] bool operator==(const BuildingId&) const = default;
    };

    // Repair() 进入原生 OutList 前须保留的最小空槽数。
    constexpr std::uint32_t MIN_NATIVE_FREE_SLOTS = 13;

    struct BuildingSnapshot
    {
        BuildingId Id;
        std::uintptr_t Owner = 0;
        bool IsDamaged = false;
        bool IsBeingRepaired = false;
        bool CanBeRepaired = false;
        int Health = 0;
        bool IsInViewport = false;
    };

    struct Snapshot
    {
        std::uintptr_t LocalOwner = 0;
        std::vector<BuildingSnapshot> Buildings;
    };

    /** 游戏侧只提供当前帧的值；不把可解引用的游戏指针交给服务保存。 */
    class IAutoRepairGamePort
    {
    public:
        virtual ~IAutoRepairGamePort() = default;

        [[nodiscard]] virtual bool IsMatchReady() const = 0;
        [[nodiscard]] virtual std::uintptr_t GetSessionIdentity() const = 0;
        [[nodiscard]] virtual std::uint32_t GetCurrentFrame() const = 0;
        [[nodiscard]] virtual std::uint64_t GetCurrentTimeMs() const = 0;
        [[nodiscard]] virtual bool CaptureSnapshot(Snapshot& outSnapshot,
            bool captureViewport) const = 0;
        [[nodiscard]] virtual std::uint32_t GetNativeFreeSlots() const = 0;

        /** 返回 true 表示已调用 Repair()；游戏是否实际入队由原生逻辑决定。 */
        [[nodiscard]] virtual bool TryRepair(BuildingId id, bool requireViewport) const = 0;
    };
}
