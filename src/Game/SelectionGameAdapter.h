#pragma once

#include "Commands/Selection/ISelectionGamePort.h"

namespace ra_commands::game
{
    /** 将选取服务的值型边界映射到当前目标游戏；仅在游戏线程使用。 */
    class SelectionGameAdapter final : public selection::ISelectionGamePort
    {
    public:
        [[nodiscard]] std::vector<selection::SelectionMember> CaptureSelectedMembers() const override;
        [[nodiscard]] selection::SelectionIds CaptureSelectedIds() const override;
        [[nodiscard]] std::vector<selection::SelectionMember> CaptureMembersById(
            const selection::SelectionIds& ids) const override;
        [[nodiscard]] std::vector<selection::SelectionMember> CaptureCandidates(
            selection::SelectionScope scope) const override;
        [[nodiscard]] selection::SelectionIds Apply(
            const selection::SelectionIds& ids, bool append) override;
        [[nodiscard]] bool IsSelectable(selection::SelectionId id) const override;
        [[nodiscard]] std::uint64_t GetSessionIdentity() const override;
        [[nodiscard]] std::uint64_t GetGameFrame() const override;
        [[nodiscard]] std::uint64_t GetMonotonicTimeMs() const override;
    };
}
