#include "Game/SelectionGameAdapter.h"

#include "Game/GameObjectAccess.h"
#include "Game/SelectionAccess.h"

#include <Windows.h>

namespace ra_commands::game
{
    std::vector<selection::SelectionMember> SelectionGameAdapter::CaptureSelectedMembers() const
    {
        return CaptureSelectedUnits();
    }

    selection::SelectionIds SelectionGameAdapter::CaptureSelectedIds() const
    {
        return CaptureSelectedObjectIds();
    }

    std::vector<selection::SelectionMember> SelectionGameAdapter::CaptureMembersById(
        const selection::SelectionIds& ids) const
    {
        return CaptureUnitsById(ids);
    }

    std::vector<selection::SelectionMember> SelectionGameAdapter::CaptureCandidates(
        selection::SelectionScope scope) const
    {
        return CaptureSelectableUnits(scope == selection::SelectionScope::Viewport
            ? SelectionScope::Viewport : SelectionScope::WholeMap);
    }

    selection::SelectionIds SelectionGameAdapter::Apply(
        const selection::SelectionIds& ids, bool append)
    {
        return ApplySelectionIds(ids, append);
    }

    bool SelectionGameAdapter::IsSelectable(selection::SelectionId id) const
    {
        return IsSelectableObjectId(id);
    }

    std::uint64_t SelectionGameAdapter::GetSessionIdentity() const
    {
        return GetSelectionSessionIdentity();
    }

    std::uint64_t SelectionGameAdapter::GetGameFrame() const
    {
        return GetCurrentGameFrame();
    }

    std::uint64_t SelectionGameAdapter::GetMonotonicTimeMs() const
    {
        return GetTickCount64();
    }
}
