#pragma once

#include "UI/Overlay/HudTypes.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ra_commands::ui
{
    /** 已被原生 Create 接受的事件历史；不保存 RadarEventClass 指针或八格环。 */
    class RadarFeedModel
    {
    public:
        explicit RadarFeedModel(std::size_t capacity = 128);
        void Reset() noexcept;
        void OnAccepted(int type, HudMapCell cell, std::uint64_t nowMs);
        std::vector<RadarNotice> Capture() const;
        std::optional<HudMapCell> FindCell(std::uint64_t id) const;
        bool MarkRead(std::uint64_t id);
        bool TogglePin(std::uint64_t id);
        static int GetPriority(int type) noexcept;
        static const wchar_t* GetLabel(int type) noexcept;

    private:
        std::size_t mCapacity;
        std::uint64_t mNextId = 1;
        std::vector<RadarNotice> mNotices;
    };
}
