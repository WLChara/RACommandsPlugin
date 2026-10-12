#include "Commands/DragFormationCommand/DragFormationGameAdapter.h"

#include "Commands/AutoFormationCommand/AutoFormationCommandService.h"
#include "Commands/AutoFormationCommand/AutoFormationGameAdapter.h"
#include "Commands/AutoFormationCommand/AutoFormationDiagnostics.h"
#include "Commands/DragFormationCommand/DragFormationProjection.h"
#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <CellClass.h>
#include <DisplayClass.h>
#include <MapClass.h>
#include <MessageListClass.h>
#include <Surface.h>
#include <TacticalClass.h>
#include <Unsorted.h>

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <array>
#include <sstream>

namespace ra_commands::game
{
    namespace
    {
        constexpr int LEPTONS_PER_CELL = 256;
        constexpr int MAX_MAP_AXIS = 512;
        constexpr int ANCHOR_HALF_SIZE_PX = 6;
        constexpr int SLOT_HALF_SIZE_PX = 3;
        constexpr int MAX_HEIGHT_REFINEMENTS = 4;

        int NativeFloorHeight(const CellClass* cell, const drag_formation::WorldPoint& point)
        {
            // 目标EXE 7cd005d2：0x47B3A0，ECX=Cell，Point2D* 栈参，ret 4。
            // 只查询本格局部 lepton；坡道的高度依赖 X/Y 低字节。
            using Query = int(__thiscall*)(const CellClass*, const Point2D*);
            const auto query = reinterpret_cast<Query>(0x47B3A0u);
            const int x = static_cast<int>(std::lround((point.mX + 0.5) * LEPTONS_PER_CELL));
            const int y = static_cast<int>(std::lround((point.mY + 0.5) * LEPTONS_PER_CELL));
            const Point2D local{x & 0xFF, y & 0xFF};
            return query(cell, &local) + (cell->ContainsBridge() ? CellClass::BridgeHeight : 0);
        }

        std::optional<drag_formation::ProjectionFrame> ProjectCellBasis(TacticalClass* tactical,
            const CellStruct& pickedCell, int height)
        {
            // 0x6D2140 的本地基向量直接来自游戏投影，含当前镜头/分辨率设置。
            const CoordStruct center{pickedCell.X * LEPTONS_PER_CELL + 128,
                pickedCell.Y * LEPTONS_PER_CELL + 128, height};
            const CoordStruct cellX{center.X + LEPTONS_PER_CELL, center.Y, height};
            const CoordStruct cellY{center.X, center.Y + LEPTONS_PER_CELL, height};
            Point2D projectedCenter{}, projectedX{}, projectedY{};
            const bool centerVisible = tactical->CoordsToClient(center, &projectedCenter);
            const bool xVisible = tactical->CoordsToClient(cellX, &projectedX);
            const bool yVisible = tactical->CoordsToClient(cellY, &projectedY);
            if (!centerVisible || !xVisible || !yVisible) { return std::nullopt; }
            return drag_formation::ProjectionFrame{
                {static_cast<double>(pickedCell.X), static_cast<double>(pickedCell.Y)},
                {static_cast<double>(projectedCenter.X), static_cast<double>(projectedCenter.Y)},
                {static_cast<double>(projectedX.X), static_cast<double>(projectedX.Y)},
                {static_cast<double>(projectedY.X), static_cast<double>(projectedY.Y)}};
        }

        bool ContainsClientPoint(const RectangleStruct& bounds, int x, int y)
        {
            if (bounds.Width <= 0 || bounds.Height <= 0) { return false; }
            return static_cast<std::int64_t>(x) >= bounds.X &&
                static_cast<std::int64_t>(y) >= bounds.Y &&
                static_cast<std::int64_t>(x) < static_cast<std::int64_t>(bounds.X) + bounds.Width &&
                static_cast<std::int64_t>(y) < static_cast<std::int64_t>(bounds.Y) + bounds.Height;
        }

        bool TryProjectWorldPoint(const drag_formation::WorldPoint& worldPoint,
            const RectangleStruct& viewBounds, Point2D& outPoint)
        {
            if (!std::isfinite(worldPoint.mX) || !std::isfinite(worldPoint.mY) ||
                worldPoint.mX < -0.5 || worldPoint.mY < -0.5 ||
                worldPoint.mX >= MAX_MAP_AXIS - 0.5 || worldPoint.mY >= MAX_MAP_AXIS - 0.5)
            {
                return false;
            }

            // 连续格坐标的整数是格中心；用目标地块自身的高度投影。
            // 这里只画本地预览，不据此宣称可占格或实际抵达。
            const int cellX = static_cast<int>(std::floor(worldPoint.mX + 0.5));
            const int cellY = static_cast<int>(std::floor(worldPoint.mY + 0.5));
            const CellStruct cellCoords{static_cast<short>(cellX), static_cast<short>(cellY)};
            auto* const map = MapClass::Instance.get();
            auto* const tactical = TacticalClass::Instance.get();
            if (!map || !tactical || !map->CoordinatesLegal(cellCoords) ||
                !map->IsWithinUsableArea(cellCoords, false))
            {
                return false;
            }
            auto* const nativeCell = map->TryGetCellAt(cellCoords);
            if (!nativeCell || nativeCell->MapCoords != cellCoords) { return false; }

            CoordStruct worldCoords{
                static_cast<int>(std::lround((worldPoint.mX + 0.5) * LEPTONS_PER_CELL)),
                static_cast<int>(std::lround((worldPoint.mY + 0.5) * LEPTONS_PER_CELL)),
                NativeFloorHeight(nativeCell, worldPoint)};
            Point2D projected{};
            if (!tactical->CoordsToClient(worldCoords, &projected)) { return false; }
            projected.X += viewBounds.X;
            projected.Y += viewBounds.Y;
            outPoint = projected;
            return true;
        }

        void DrawCross(DSurface* surface, const RectangleStruct& clip,
            const Point2D& center, int halfSizePx, WORD color)
        {
            const Point2D horizontalStart{center.X - halfSizePx, center.Y};
            const Point2D horizontalEnd{center.X + halfSizePx, center.Y};
            const Point2D verticalStart{center.X, center.Y - halfSizePx};
            const Point2D verticalEnd{center.X, center.Y + halfSizePx};
            (void)surface->DrawLineEx(clip, horizontalStart, horizontalEnd, color);
            (void)surface->DrawLineEx(clip, verticalStart, verticalEnd, color);
        }
    }

    DragFormationGameAdapter::DragFormationGameAdapter(AutoFormationGameAdapter& game,
        auto_formation::AutoFormationCommandService& executor)
        : mGame(game), mExecutor(executor) { }

    std::uintptr_t DragFormationGameAdapter::OwnerWindowHandle() const noexcept
    {
        const HWND window = Game::hWnd.get();
        return window && IsWindow(window) ? reinterpret_cast<std::uintptr_t>(window) : 0;
    }

    bool DragFormationGameAdapter::IsSessionActive() const { return mGame.IsSessionActive(); }
    std::uint32_t DragFormationGameAdapter::Epoch() const { return mGame.Epoch(); }

    std::vector<auto_formation::ActorProfile>
        DragFormationGameAdapter::CaptureSelectedActorProfiles() const
    {
        return mGame.CaptureSelectedActorProfiles();
    }

    bool DragFormationGameAdapter::TryCapturePlacementSnapshot(
        const std::vector<auto_formation::ActorId>& actors,
        const std::vector<auto_formation::Cell>& cells,
        auto_formation::Snapshot& outSnapshot) const
    {
        return mGame.TryCaptureSnapshotForCells(actors, cells, outSnapshot);
    }

    bool DragFormationGameAdapter::Commit(const auto_formation::PlanResult& plan)
    {
        const bool hasWholePlan = IsSessionActive() && !plan.mBudgetExceeded &&
            plan.mUnassignedActors.empty() && !plan.mAssignments.empty() &&
            plan.mAssignments.size() <= drag_formation::MAX_DEMO_ACTORS;
        const bool accepted = hasWholePlan && mExecutor.StartPlan(plan);
        std::array<char, 192> event{};
        std::snprintf(event.data(), event.size(),
            "drag-commit assigned=%zu unassigned=%zu budgetExceeded=%d wholePlan=%d accepted=%d",
            plan.mAssignments.size(), plan.mUnassignedActors.size(),
            plan.mBudgetExceeded, hasWholePlan, accepted);
        TraceAutoFormation(event.data());
        if (accepted)
        {
            try
            {
                std::ostringstream targets;
                targets << "drag-fixed-targets center=" << plan.mCenter.mX << ',' << plan.mCenter.mY;
                for (const auto& assignment : plan.mAssignments)
                {
                    targets << ' ' << assignment.mActor << "->(" << assignment.mDestination.mX
                        << ',' << assignment.mDestination.mY << ',' << assignment.mDestination.mOnBridge
                        << ',' << (assignment.mKind == auto_formation::ActorKind::Infantry ? "infantry" : "vehicle")
                        << ',' << (assignment.mPlane == auto_formation::OccupancyPlane::Air ? "air" : "ground")
                        << ',' << static_cast<unsigned>(assignment.mSeat) << ')';
                }
                TraceAutoFormation(targets.str());
            }
            catch (...)
            {
                // 诊断异常不能撤销已经由原生执行器接受的计划。
            }
        }
        return accepted;
    }

    std::optional<drag_formation::WorldPoint> DragFormationGameAdapter::CaptureWorldPointFromClient(
        std::int32_t clientX, std::int32_t clientY) const
    {
        if (!IsSessionActive()) { return std::nullopt; }
        const HWND window = Game::hWnd.get();
        auto* const display = DisplayClass::Instance.get();
        auto* const map = MapClass::Instance.get();
        if (!window || !IsWindow(window) || GetForegroundWindow() != window || !display || !map)
        {
            return std::nullopt;
        }
        RECT clientBounds{};
        if (!GetClientRect(window, &clientBounds) || clientX < clientBounds.left ||
            clientY < clientBounds.top || clientX >= clientBounds.right ||
            clientY >= clientBounds.bottom)
        {
            return std::nullopt;
        }
        const auto viewBounds = DSurface::ViewBounds.get();
        const auto sidebarBounds = DSurface::SidebarBounds.get();
        if (!ContainsClientPoint(viewBounds, clientX, clientY) ||
            ContainsClientPoint(sidebarBounds, clientX, clientY))
        {
            return std::nullopt;
        }

        // 原生主窗口 WndProc(0x7775C0) 的 MouseClass 路径(0x6930A0)
        // 先减 ViewBounds.X/Y，再给 ProcessClickCoords(0x692300)。
        Point2D viewPoint{clientX - viewBounds.X, clientY - viewBounds.Y};
        CellStruct cell{};
        CoordStruct coordinate{};
        ObjectClass* target = nullptr;
        BYTE secondaryFlag = 0;
        BYTE primaryFlag = 0;
        if (!display->ProcessClickCoords(&viewPoint, &cell, &coordinate, &target,
                &secondaryFlag, &primaryFlag) ||
            !map->CoordinatesLegal(cell) || !map->IsWithinUsableArea(cell, false))
        {
            return std::nullopt;
        }
        auto* const nativeCell = map->TryGetCellAt(cell);
        if (!nativeCell || nativeCell->MapCoords != cell) { return std::nullopt; }
        auto* const tactical = TacticalClass::Instance.get();
        if (!tactical) { return std::nullopt; }
        // ProcessClickCoords 的 CoordStruct 来自 0x6D2280 的 Z=0 平面；
        // 原生命中格却由 0x6D6590 按地形/桥面拾取。以命中格实际高度
        // 建立本地投影基，并使用调用后 viewPoint，以兼容已有原生输入重映射。
        const drag_formation::PixelPoint effectivePixel{
            static_cast<double>(viewPoint.X), static_cast<double>(viewPoint.Y)};
        int height = nativeCell->GetCoordsWithBridge().Z;
        std::optional<drag_formation::WorldPoint> candidate;
        bool converged = false;
        for (int attempt = 0; attempt < MAX_HEIGHT_REFINEMENTS; ++attempt)
        {
            const auto basis = ProjectCellBasis(tactical, cell, height);
            if (!basis) { break; }
            candidate = drag_formation::UnprojectWorldPoint(*basis, effectivePixel);
            if (!candidate) { break; }
            const int nextHeight = NativeFloorHeight(nativeCell, *candidate);
            if (std::abs(nextHeight - height) <= 1)
            {
                converged = true;
                break;
            }
            height = nextHeight;
        }
        auto picked = drag_formation::KeepPointInPickedCell(
            converged ? candidate : std::nullopt, cell.X, cell.Y);
        if (!picked.mUsedCellCenter)
        {
            const CoordStruct reprojection{
                static_cast<int>(std::lround((picked.mPoint.mX + 0.5) * LEPTONS_PER_CELL)),
                static_cast<int>(std::lround((picked.mPoint.mY + 0.5) * LEPTONS_PER_CELL)),
                NativeFloorHeight(nativeCell, picked.mPoint)};
            Point2D projected{};
            if (!tactical->CoordsToClient(reprojection, &projected) ||
                std::abs(static_cast<double>(projected.X) - effectivePixel.mX) > 1.0 ||
                std::abs(static_cast<double>(projected.Y) - effectivePixel.mY) > 1.0)
            {
                picked = {{static_cast<double>(cell.X), static_cast<double>(cell.Y)}, true};
            }
        }
        if (picked.mUsedCellCenter &&
            (mProjectionFallbackEpoch != Epoch() || !mLoggedProjectionFallback))
        {
            mProjectionFallbackEpoch = Epoch();
            mLoggedProjectionFallback = true;
            TraceAutoFormation("drag-projection fallback=cell-center reason=nonconvergent-or-cell-mismatch");
        }
        return picked.mPoint;
    }

    std::optional<drag_formation::WorldPoint> DragFormationGameAdapter::CaptureCursorWorldPoint() const
    {
        const HWND window = Game::hWnd.get();
        POINT cursor{};
        if (!window || !IsWindow(window) || !GetCursorPos(&cursor) ||
            !ScreenToClient(window, &cursor))
        {
            return std::nullopt;
        }
        return CaptureWorldPointFromClient(cursor.x, cursor.y);
    }

    void DragFormationGameAdapter::TracePointerPoint(std::int32_t clientX, std::int32_t clientY,
        const std::optional<drag_formation::WorldPoint>& point) const
    {
        POINT cursor{};
        const HWND window = Game::hWnd.get();
        const bool hasCursor = window && GetCursorPos(&cursor) && ScreenToClient(window, &cursor);
        RECT client{};
        const bool hasClient = window && GetClientRect(window, &client);
        const auto view = DSurface::ViewBounds.get();
        std::ostringstream event;
        event << "drag-pointer-down rawClient=" << clientX << ',' << clientY
            << " winCursorClient=";
        if (hasCursor) { event << cursor.x << ',' << cursor.y; }
        else { event << "unavailable"; }
        event << " clientSize=";
        if (hasClient) { event << client.right - client.left << ',' << client.bottom - client.top; }
        else { event << "unavailable"; }
        event << " viewBounds=" << view.X << ',' << view.Y << ',' << view.Width << ',' << view.Height
            << " world=";
        if (!point || !std::isfinite(point->mX) || !std::isfinite(point->mY) ||
            point->mX < -0.5 || point->mY < -0.5 ||
            point->mX >= MAX_MAP_AXIS - 0.5 || point->mY >= MAX_MAP_AXIS - 0.5)
        {
            event << "unavailable";
            TraceAutoFormation(event.str());
            return;
        }
        event << point->mX << ',' << point->mY;
        const int cellX = static_cast<int>(std::floor(point->mX + 0.5));
        const int cellY = static_cast<int>(std::floor(point->mY + 0.5));
        event << " pickedCell=" << cellX << ',' << cellY;
        auto* const map = MapClass::Instance.get();
        auto* const tactical = TacticalClass::Instance.get();
        const CellStruct coords{static_cast<short>(cellX), static_cast<short>(cellY)};
        auto* const cell = map && map->CoordinatesLegal(coords) ? map->TryGetCellAt(coords) : nullptr;
        if (cell && cell->MapCoords == coords && tactical)
        {
            const int height = NativeFloorHeight(cell, *point);
            const CoordStruct world{
                static_cast<int>(std::lround((point->mX + 0.5) * LEPTONS_PER_CELL)),
                static_cast<int>(std::lround((point->mY + 0.5) * LEPTONS_PER_CELL)),
                height};
            Point2D projected{};
            const bool projectedVisible = tactical->CoordsToClient(world, &projected);
            event << " localHeight=" << height << " projectClient=";
            if (projectedVisible)
            {
                const int screenX = projected.X + view.X;
                const int screenY = projected.Y + view.Y;
                event << screenX << ',' << screenY << " errorRaw="
                    << screenX - clientX << ',' << screenY - clientY;
            }
            else { event << "unavailable"; }
        }
        else { event << " localHeight=unavailable projectClient=unavailable"; }
        TraceAutoFormation(event.str());
    }

    void DragFormationGameAdapter::RenderPreview(const drag_formation::Preview& preview) const
    {
        if (!IsSessionActive() || !preview.mVisible || !preview.mLayout.mValid ||
            preview.mLayout.mSlots.size() > drag_formation::MAX_DEMO_ACTORS)
        {
            return;
        }
        auto* const surface = DSurface::Hidden_2.get(); // 0x887314，原生 g_TempSurface。
        if (!surface) { return; }
        const auto clip = DSurface::ViewBounds.get();
        if (clip.Width <= 0 || clip.Height <= 0) { return; }

        // 0x6D2140 投影结果相对战场视窗，0x456980 原生圈绘制也加
        // ViewBounds.X/Y；0x7BA610 DrawLineEx 按16位颜色裁剪并锁定Surface。
        const WORD slotColor = static_cast<WORD>(Color16Struct(ColorStruct(38, 205, 230)));
        const WORD anchorColor = static_cast<WORD>(Color16Struct(ColorStruct(255, 230, 90)));
        const WORD guideColor = static_cast<WORD>(Color16Struct(ColorStruct(70, 140, 245)));
        Point2D left{};
        Point2D right{};
        if (TryProjectWorldPoint(preview.mLayout.mLeft, clip, left) &&
            TryProjectWorldPoint(preview.mLayout.mRight, clip, right))
        {
            (void)surface->DrawLineEx(clip, left, right, guideColor);
        }
        Point2D anchor{};
        if (TryProjectWorldPoint(preview.mLayout.mAnchor, clip, anchor))
        {
            DrawCross(surface, clip, anchor, ANCHOR_HALF_SIZE_PX, anchorColor);
            Point2D end{};
            if (TryProjectWorldPoint(preview.mLayout.mEnd, clip, end))
            {
                (void)surface->DrawLineEx(clip, anchor, end, guideColor);
            }
        }
        for (const auto& slot : preview.mLayout.mSlots)
        {
            Point2D projected{};
            if (TryProjectWorldPoint(slot, clip, projected))
            {
                DrawCross(surface, clip, projected, SLOT_HALF_SIZE_PX, slotColor);
            }
        }
    }

    void DragFormationGameAdapter::ShowFeedback(drag_formation::Outcome outcome) const
    {
        const wchar_t* message = nullptr;
        const char* diagnostic = nullptr;
        switch (outcome)
        {
        case drag_formation::Outcome::Armed:
            message = L"同时按住鼠标左键和右键至少0.5秒并拖动；按Esc取消";
            diagnostic = "drag-feedback outcome=Armed reason=waiting-for-two-button-drag";
            break;
        case drag_formation::Outcome::Cancelled:
            message = L"编队模式已取消";
            diagnostic = "drag-feedback outcome=Cancelled reason=mode-exit";
            break;
        case drag_formation::Outcome::InvalidGesture:
            message = L"拖拽距离不足或鼠标不在战场";
            diagnostic = "drag-feedback outcome=InvalidGesture reason=short-drag-or-outside-battlefield";
            break;
        case drag_formation::Outcome::PlacementRejected:
            message = L"无法为整组单位找到编队位置";
            diagnostic = "drag-feedback outcome=PlacementRejected reason=whole-group-placement-infeasible";
            break;
        case drag_formation::Outcome::ExecutionRejected:
            message = L"编队提交失败，请重试";
            diagnostic = "drag-feedback outcome=ExecutionRejected reason=executor-rejected-plan";
            break;
        case drag_formation::Outcome::Scheduled:
            message = L"已安排矩形编队";
            diagnostic = "drag-feedback outcome=Scheduled reason=complete-plan-accepted";
            break;
        case drag_formation::Outcome::None:
            return;
        }
        TraceAutoFormation(diagnostic);
        // SDK只提供实例布局与类型；真正下发仍须目标版本ABI门禁。
        // 回调及ShowFeedback由Bootstrap限制在游戏线程，文本不进入联机协议。
        if (mGame.IsSessionActive())
        {
            // 目标EXE哈希门禁后使用本地封装；IDA 0x5D3BA0 为ECX=this、
            // 七个栈参且 ret 1Ch。避开SDK JMP_THIS的EBP内联汇编帧约束。
            using NativePrintMessage = void(__thiscall*)(MessageListClass*,
                const wchar_t*, DWORD, const wchar_t*, int, DWORD, int, bool);
            const auto printMessage = reinterpret_cast<NativePrintMessage>(0x5D3BA0u);
            printMessage(&MessageListClass::Instance.get(), nullptr, 0, message,
                ColorScheme::White, 0x4046u, 120, false);
        }
    }
}
