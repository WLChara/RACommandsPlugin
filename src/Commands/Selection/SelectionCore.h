#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ra_commands::selection
{
    using SelectionId = std::uint64_t;
    using SelectionIds = std::vector<SelectionId>;

    enum class SelectionKind
    {
        Unit,
        Infantry,
        Aircraft
    };

    enum class FillState
    {
        Full,
        NotFull,
        Empty
    };

    struct SelectionMember
    {
        SelectionId Id = 0;
        int IfvMode = 0;
        bool IsMindControlled = false;
        SelectionKind Kind = SelectionKind::Unit;
        int AmmoCurrent = 0;
        int AmmoCapacity = 0;
        int PassengerCurrent = 0;
        int PassengerCapacity = 0;
    };

    // 所有筛选均保留输入顺序；轮换命令的调用方应反复传入首次按键前的母集。
    // 调用方负责采集有效成员，并判断游戏选区何时构成新的母集。
    /** 从选中种子提取去重的 IFVMode；候选范围由游戏适配器按单击或双击提供。 */
    [[nodiscard]] SelectionIds FilterSeedIfvModes(
        const std::vector<SelectionMember>& seeds,
        const std::vector<SelectionMember>& candidates);
    [[nodiscard]] SelectionIds FilterMindControlled(
        const std::vector<SelectionMember>& members, bool isMindControlled);
    [[nodiscard]] SelectionIds FilterKind(
        const std::vector<SelectionMember>& members, SelectionKind kind);
    [[nodiscard]] SelectionKind NextKind(SelectionKind current);
    [[nodiscard]] SelectionIds FilterAmmo(
        const std::vector<SelectionMember>& members, FillState state);
    [[nodiscard]] SelectionIds FilterPassengers(
        const std::vector<SelectionMember>& members, FillState state);

    /** 保存至多 12 份 ID 快照；不保存可解引用的游戏对象。 */
    class SelectionHistory
    {
    public:
        static constexpr std::size_t MAX_SNAPSHOTS = 12;

        void Save(const SelectionIds& ids);
        void Clear();
        [[nodiscard]] std::size_t Size() const;

        /** 从最新快照起按 0 编号；无此快照时返回 nullopt，空快照返回空 vector。 */
        template <typename TIsValid>
        [[nodiscard]] std::optional<SelectionIds> Restore(
            std::size_t newestIndex, TIsValid&& isValid) const
        {
            if (newestIndex >= mSnapshots.size())
            {
                return std::nullopt;
            }

            SelectionIds validIds;
            const auto& ids = mSnapshots[mSnapshots.size() - 1 - newestIndex];
            validIds.reserve(ids.size());
            for (const SelectionId id : ids)
            {
                if (isValid(id))
                {
                    validIds.push_back(id);
                }
            }
            return validIds;
        }

    private:
        std::vector<SelectionIds> mSnapshots;
    };

    /** 仅记住上次的稳定 ID；候选列表由调用方在每次选取时重新提供。 */
    class SelectionCycle
    {
    public:
        [[nodiscard]] std::optional<SelectionId> Next(const SelectionIds& candidates);
        void Reset();

    private:
        std::optional<SelectionId> mLastId;
    };
}
