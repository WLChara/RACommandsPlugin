#include "Commands/DragFormationCommand/DragFormationProjection.h"

#include <algorithm>
#include <cmath>

namespace ra_commands::drag_formation
{
    std::optional<WorldPoint> UnprojectWorldPoint(
        const ProjectionFrame& frame, PixelPoint pixel) noexcept
    {
        const double xx = frame.mCellXPixel.mX - frame.mCenterPixel.mX;
        const double xy = frame.mCellXPixel.mY - frame.mCenterPixel.mY;
        const double yx = frame.mCellYPixel.mX - frame.mCenterPixel.mX;
        const double yy = frame.mCellYPixel.mY - frame.mCenterPixel.mY;
        const double determinant = xx * yy - xy * yx;
        const double dx = pixel.mX - frame.mCenterPixel.mX;
        const double dy = pixel.mY - frame.mCenterPixel.mY;
        if (!std::isfinite(frame.mCenter.mX) || !std::isfinite(frame.mCenter.mY) ||
            !std::isfinite(xx) || !std::isfinite(xy) || !std::isfinite(yx) ||
            !std::isfinite(yy) || !std::isfinite(dx) || !std::isfinite(dy) ||
            !std::isfinite(determinant) || std::abs(determinant) < 1.0e-6)
        {
            return std::nullopt;
        }
        const WorldPoint result{frame.mCenter.mX + (dx * yy - dy * yx) / determinant,
            frame.mCenter.mY + (dy * xx - dx * xy) / determinant};
        if (!std::isfinite(result.mX) || !std::isfinite(result.mY)) { return std::nullopt; }
        return result;
    }

    PickedWorldPoint KeepPointInPickedCell(
        std::optional<WorldPoint> point, int cellX, int cellY) noexcept
    {
        constexpr double ONE_LEPTON_IN_CELLS = 1.0 / 256.0;
        const WorldPoint center{static_cast<double>(cellX), static_cast<double>(cellY)};
        if (!point || !std::isfinite(point->mX) || !std::isfinite(point->mY))
        {
            return {center, true};
        }
        const double minX = center.mX - 0.5;
        const double maxX = center.mX + 0.5;
        const double minY = center.mY - 0.5;
        const double maxY = center.mY + 0.5;
        if (point->mX < minX - ONE_LEPTON_IN_CELLS ||
            point->mX >= maxX + ONE_LEPTON_IN_CELLS ||
            point->mY < minY - ONE_LEPTON_IN_CELLS ||
            point->mY >= maxY + ONE_LEPTON_IN_CELLS)
        {
            return {center, true};
        }
        // 轻微整数舍入越界时仍留在命中格内部，确保后续 floor(point+.5) 一致。
        return {{(std::clamp)(point->mX, minX, maxX - ONE_LEPTON_IN_CELLS),
            (std::clamp)(point->mY, minY, maxY - ONE_LEPTON_IN_CELLS)}, false};
    }
}
