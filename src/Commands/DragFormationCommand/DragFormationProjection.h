#pragma once

#include "Commands/DragFormationCommand/DragFormationTypes.h"

#include <optional>

namespace ra_commands::drag_formation
{
    struct PixelPoint
    {
        double mX = 0.0;
        double mY = 0.0;
    };

    /** 三个原生投影样本：格中心、世界X前进一格、世界Y前进一格。 */
    struct ProjectionFrame
    {
        WorldPoint mCenter;
        PixelPoint mCenterPixel;
        PixelPoint mCellXPixel;
        PixelPoint mCellYPixel;
    };

    [[nodiscard]] std::optional<WorldPoint> UnprojectWorldPoint(
        const ProjectionFrame& frame, PixelPoint pixel) noexcept;

    struct PickedWorldPoint
    {
        WorldPoint mPoint;
        bool mUsedCellCenter = false;
    };

    /** 原生命中格是权威边界；最多容忍一 lepton 的整数投影舍入。 */
    [[nodiscard]] PickedWorldPoint KeepPointInPickedCell(
        std::optional<WorldPoint> point, int cellX, int cellY) noexcept;
}
