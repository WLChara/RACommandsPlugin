#pragma once

#include "Commands/Selection/SelectionCore.h"

#include <cstdint>
#include <vector>

namespace ra_commands::game
{
    enum class SelectionScope
    {
        Viewport,
        WholeMap
    };

    /** 仅在游戏主线程调用；返回稳定 ID 和值快照，不向命令层泄漏游戏对象指针。 */
    [[nodiscard]] std::vector<selection::SelectionMember> CaptureSelectedUnits();
    [[nodiscard]] std::vector<selection::SelectionMember> CaptureUnitsById(
        const selection::SelectionIds& ids);
    [[nodiscard]] std::vector<selection::SelectionMember> CaptureSelectableUnits(
        SelectionScope scope);
    [[nodiscard]] selection::SelectionIds CaptureSelectedUnitIds();
    [[nodiscard]] selection::SelectionIds CaptureSelectedObjectIds();

    /** 重验身份、当前归属及可选状态后修改本地选区；返回实际选中的单位 ID。 */
    [[nodiscard]] selection::SelectionIds ApplySelectionIds(
        const selection::SelectionIds& ids, bool append);
    [[nodiscard]] bool IsSelectableObjectId(selection::SelectionId id);
    [[nodiscard]] std::uintptr_t GetSelectionSessionIdentity();
}
