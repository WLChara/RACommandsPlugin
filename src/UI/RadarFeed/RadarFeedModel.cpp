#include "UI/RadarFeed/RadarFeedModel.h"

#include <algorithm>
#include <limits>
#include <tuple>

namespace ra_commands::ui
{
    namespace
    {
        constexpr std::uint64_t COALESCE_MS = 2000;
        constexpr int COALESCE_RADIUS_SQUARED = 64;

        int GetRetention(const RadarNotice& notice)
        {
            // 固定记录和高优先级未读记录不会被低级噪声挤走。
            return notice.mPinned ? 100 : notice.mPriority * 2 + (notice.mRead ? 0 : 1);
        }
    }

    RadarFeedModel::RadarFeedModel(std::size_t capacity)
        : mCapacity((std::min)(capacity, std::size_t{128}))
    {
        mNotices.reserve(mCapacity);
    }

    void RadarFeedModel::Reset() noexcept { mNotices.clear(); }

    int RadarFeedModel::GetPriority(int type) noexcept
    {
        switch (type)
        {
        case 3: case 4: case 16: return 3;
        case 12: case 13: return 2;
        case 5: return 1;
        default: return 0;
        }
    }

    const wchar_t* RadarFeedModel::GetLabel(int type) noexcept
    {
        static constexpr const wchar_t* LABELS[] = {
            L"发生战斗", L"非战斗事件", L"空降区域", L"基地遭到攻击", L"矿车遭到攻击",
            L"发现敌人", L"单位已生产", L"单位损失", L"单位已修复", L"建筑遭到渗透",
            L"建筑被占领", L"信标已放置", L"发现超级武器", L"超级武器已启动",
            L"桥梁已修复", L"驻军已撤离", L"盟友基地遭到攻击"
        };
        return type >= 0 && type < 17 ? LABELS[type] : L"雷达事件";
    }

    void RadarFeedModel::OnAccepted(int type, HudMapCell cell, std::uint64_t nowMs)
    {
        if (type < 0 || type > 16 || !mCapacity) { return; }
        // 从最新记录开始合并；不同类型即使同格也各自保留。
        for (auto iterator = mNotices.rbegin(); iterator != mNotices.rend(); ++iterator)
        {
            auto& notice = *iterator;
            const int dx = static_cast<int>(notice.mCell.mX) - cell.mX;
            const int dy = static_cast<int>(notice.mCell.mY) - cell.mY;
            if (notice.mType == type && nowMs >= notice.mReceivedAtMs &&
                nowMs - notice.mReceivedAtMs <= COALESCE_MS &&
                static_cast<std::int64_t>(dx) * dx + static_cast<std::int64_t>(dy) * dy <=
                    COALESCE_RADIUS_SQUARED)
            {
                if (notice.mCount < (std::numeric_limits<unsigned int>::max)()) { ++notice.mCount; }
                notice.mReceivedAtMs = nowMs;
                notice.mCell = cell;
                notice.mRead = false;
                return;
            }
        }
        RadarNotice notice{0, type, cell, nowMs, 1, false, false,
            GetPriority(type), GetLabel(type)};
        if (mNotices.size() == mCapacity)
        {
            const auto victim = std::min_element(mNotices.begin(), mNotices.end(),
                [](const RadarNotice& left, const RadarNotice& right)
                {
                    return std::make_tuple(GetRetention(left), left.mReceivedAtMs, left.mId) <
                        std::make_tuple(GetRetention(right), right.mReceivedAtMs, right.mId);
                });
            if (victim->mPinned || GetRetention(*victim) > GetRetention(notice)) { return; }
            mNotices.erase(victim);
        }
        if (!mNextId) { return; }
        notice.mId = mNextId++;
        mNotices.push_back(std::move(notice));
    }

    std::vector<RadarNotice> RadarFeedModel::Capture() const
    {
        auto result = mNotices;
        std::sort(result.begin(), result.end(), [](const RadarNotice& left, const RadarNotice& right)
        {
            return std::make_tuple(left.mPinned, left.mPriority, !left.mRead,
                       left.mReceivedAtMs, left.mId) >
                std::make_tuple(right.mPinned, right.mPriority, !right.mRead,
                    right.mReceivedAtMs, right.mId);
        });
        return result;
    }

    std::optional<HudMapCell> RadarFeedModel::FindCell(std::uint64_t id) const
    {
        for (const auto& notice : mNotices)
        {
            if (notice.mId == id) { return notice.mCell; }
        }
        return std::nullopt;
    }

    bool RadarFeedModel::MarkRead(std::uint64_t id)
    {
        for (auto& notice : mNotices)
        {
            if (notice.mId == id) { notice.mRead = true; return true; }
        }
        return false;
    }

    bool RadarFeedModel::TogglePin(std::uint64_t id)
    {
        for (auto& notice : mNotices)
        {
            if (notice.mId == id) { notice.mPinned = !notice.mPinned; return true; }
        }
        return false;
    }
}
