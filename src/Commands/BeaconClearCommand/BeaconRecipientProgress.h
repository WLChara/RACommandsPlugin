#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_set>

namespace ra_commands::beacon_clear
{
    /** 记住每个信标槽已提交过的远端，避免部分失败后重发给先前远端。 */
    class BeaconRecipientProgress final
    {
    public:
        static constexpr std::size_t SLOT_COUNT = 24;

        /** 返回 false 表示槽内信标已换成另一对象，旧意图不得继续删除它。 */
        [[nodiscard]] bool Begin(std::size_t flatIndex, std::uintptr_t beaconAddress);
        [[nodiscard]] bool HasSubmitted(std::size_t flatIndex,
            std::uintptr_t recipientAddress) const;
        void MarkSubmitted(std::size_t flatIndex, std::uintptr_t recipientAddress);
        void ClearSlot(std::size_t flatIndex) noexcept;
        void Reset() noexcept;

    private:
        std::array<std::unordered_set<std::uintptr_t>, SLOT_COUNT> mSubmittedRecipients;
        std::array<std::uintptr_t, SLOT_COUNT> mBeaconAddresses{};
    };
}
