#include "Commands/DragFormationCommand/DragFormationProjection.h"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace
{
    using namespace ra_commands::drag_formation;

    void Require(bool condition, const char* message)
    {
        if (!condition) { throw std::runtime_error(message); }
    }

    ProjectionFrame FrameAt(int cellX, int cellY, double projectedY)
    {
        // 原生等距投影的一格基向量，投影值由调用方采样而非生产代码写死。
        return {{static_cast<double>(cellX), static_cast<double>(cellY)},
            {400.0, projectedY}, {430.0, projectedY + 15.0},
            {370.0, projectedY + 15.0}};
    }
}

void RunDragFormationProjectionTests()
{
    const auto flat = FrameAt(100, 200, 300.0);
    const auto flatCenter = UnprojectWorldPoint(flat, {400.0, 300.0});
    Require(flatCenter && flatCenter->mX == 100.0 && flatCenter->mY == 200.0,
        "flat native cell centre must roundtrip exactly");

    // 高地把同一格中心上移60像素；旧 Z=0 反算会错两格。
    const auto high = FrameAt(100, 200, 240.0);
    const auto oldGroundIntersection = UnprojectWorldPoint(flat, {400.0, 240.0});
    const auto highCenter = UnprojectWorldPoint(high, {400.0, 240.0});
    Require(oldGroundIntersection && oldGroundIntersection->mX == 98.0 &&
        oldGroundIntersection->mY == 198.0 && highCenter &&
        highCenter->mX == 100.0 && highCenter->mY == 200.0,
        "actual terrain projection must remove the old two-cell Z=0 offset");

    // 桥面也只改变原生采样的屏幕原点；同一逆矩阵应保持格中心。
    const auto bridge = FrameAt(100, 200, 180.0);
    const auto bridgeCenter = UnprojectWorldPoint(bridge, {400.0, 180.0});
    Require(bridgeCenter && bridgeCenter->mX == 100.0 && bridgeCenter->mY == 200.0,
        "bridge height must use its own native projection origin");

    const auto fractional = UnprojectWorldPoint(high, {410.5, 242.25});
    Require(fractional && std::abs(fractional->mX - 100.25) < 1.0e-12 &&
        std::abs(fractional->mY - 199.9) < 1.0e-12,
        "fractional drag point inside a cell must retain continuous XY");
    const auto kept = KeepPointInPickedCell(fractional, 100, 200);
    Require(!kept.mUsedCellCenter && kept.mPoint.mX == fractional->mX &&
        kept.mPoint.mY == fractional->mY,
        "native picked cell must preserve an interior fractional point");

    // Win client(450,260) 减视窗原点(50,20) 才进入与投影相同的坐标系。
    const PixelPoint client{450.0, 260.0};
    const PixelPoint viewLocal{client.mX - 50.0, client.mY - 20.0};
    const auto offsetView = UnprojectWorldPoint(high, viewLocal);
    Require(offsetView && offsetView->mX == 100.0 && offsetView->mY == 200.0,
        "nonzero viewport origin must not translate the anchor");

    auto degenerate = high;
    degenerate.mCellYPixel = degenerate.mCellXPixel;
    Require(!UnprojectWorldPoint(degenerate, {400.0, 240.0}),
        "singular native projection basis must be rejected");
    Require(!UnprojectWorldPoint(high,
        {std::numeric_limits<double>::infinity(), 240.0}),
        "nonfinite cursor input must be rejected");

    const auto outside = KeepPointInPickedCell(WorldPoint{102.0, 202.0}, 100, 200);
    Require(outside.mUsedCellCenter && outside.mPoint.mX == 100.0 &&
        outside.mPoint.mY == 200.0,
        "a mismatch with the native picked cell must safely use its centre");
    const auto nearEdge = KeepPointInPickedCell(
        WorldPoint{100.5 + 0.5 / 256.0, 200.0}, 100, 200);
    Require(!nearEdge.mUsedCellCenter &&
        std::floor(nearEdge.mPoint.mX + 0.5) == 100.0,
        "one-lepton native rounding is tolerated but kept inside the picked cell");
}
