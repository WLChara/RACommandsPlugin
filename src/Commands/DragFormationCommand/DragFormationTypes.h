#pragma once

#include "Commands/AutoFormationCommand/AutoFormationPlanner.h"
#include "Commands/AutoFormationCommand/FormationCellPacking.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ra_commands::drag_formation
{
    using WorldPoint = auto_formation::Center;
    constexpr std::size_t MAX_DEMO_ACTORS = 128;
    constexpr std::size_t MAX_PLACEMENT_CELLS = 1024;

    struct Layout
    {
        WorldPoint mAnchor;
        WorldPoint mEnd;
        WorldPoint mWidthAxis;
        WorldPoint mDepthAxis;
        WorldPoint mLeft;
        WorldPoint mRight;
        std::vector<WorldPoint> mSlots;
        std::size_t mColumns = 0;
        std::size_t mRows = 0;
        double mPitch = 1.1;
        bool mValid = false;
        std::vector<auto_formation::ActorProfile> mProfiles;
        std::vector<auto_formation::PlaneLayout> mPlanes;
        std::uint8_t mInfantryCapacity = 3;
    };

    enum class PointerEventKind { Down, Up, Move, Cancel };
    enum class PointerButton { None, Left, Right };
    enum class Outcome
    {
        None,
        Armed,
        Cancelled,
        InvalidGesture,
        PlacementRejected,
        ExecutionRejected,
        Scheduled
    };
    struct PointerEvent
    {
        PointerEventKind mKind = PointerEventKind::Cancel;
        PointerButton mButton = PointerButton::None;
        std::optional<WorldPoint> mPosition;
        std::uint64_t mTimeMs = 0;
    };

    struct Preview
    {
        bool mVisible = false;
        Layout mLayout;
    };
}
