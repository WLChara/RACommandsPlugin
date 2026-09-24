#include "Commands/Selection/SelectionCore.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>

namespace ra_commands::selection
{
    namespace
    {
        template <typename TPredicate>
        SelectionIds Filter(const std::vector<SelectionMember>& members, TPredicate predicate)
        {
            SelectionIds result;
            result.reserve(members.size());
            for (const auto& member : members)
            {
                if (predicate(member))
                {
                    result.push_back(member.Id);
                }
            }
            return result;
        }

        bool MatchesFillState(int current, int capacity, FillState state)
        {
            if (capacity <= 0)
            {
                return false;
            }

            switch (state)
            {
            case FillState::Full: return current >= capacity;
            case FillState::NotFull: return current < capacity;
            case FillState::Empty: return current == 0;
            }
            return false;
        }
    }

    SelectionIds FilterSeedIfvModes(
        const std::vector<SelectionMember>& seeds,
        const std::vector<SelectionMember>& candidates)
    {
        std::unordered_set<int> modes;
        for (const auto& seed : seeds)
        {
            modes.insert(seed.IfvMode);
        }
        return Filter(candidates, [&](const SelectionMember& candidate)
        {
            return modes.contains(candidate.IfvMode);
        });
    }

    SelectionIds FilterMindControlled(
        const std::vector<SelectionMember>& members, bool isMindControlled)
    {
        return Filter(members, [=](const SelectionMember& member)
        {
            return member.IsMindControlled == isMindControlled;
        });
    }

    SelectionIds FilterKind(const std::vector<SelectionMember>& members, SelectionKind kind)
    {
        return Filter(members, [=](const SelectionMember& member)
        {
            return member.Kind == kind;
        });
    }

    SelectionKind NextKind(SelectionKind current)
    {
        switch (current)
        {
        case SelectionKind::Unit: return SelectionKind::Infantry;
        case SelectionKind::Infantry: return SelectionKind::Aircraft;
        case SelectionKind::Aircraft: return SelectionKind::Unit;
        }
        return SelectionKind::Unit;
    }

    SelectionIds FilterAmmo(const std::vector<SelectionMember>& members, FillState state)
    {
        return Filter(members, [=](const SelectionMember& member)
        {
            return MatchesFillState(member.AmmoCurrent, member.AmmoCapacity, state);
        });
    }

    SelectionIds FilterPassengers(const std::vector<SelectionMember>& members, FillState state)
    {
        return Filter(members, [=](const SelectionMember& member)
        {
            return MatchesFillState(member.PassengerCurrent, member.PassengerCapacity, state);
        });
    }

    void SelectionHistory::Save(const SelectionIds& ids)
    {
        if (mSnapshots.size() == MAX_SNAPSHOTS)
        {
            mSnapshots.erase(mSnapshots.begin());
        }
        mSnapshots.push_back(ids);
    }

    void SelectionHistory::Clear()
    {
        mSnapshots.clear();
    }

    std::size_t SelectionHistory::Size() const
    {
        return mSnapshots.size();
    }

    std::optional<SelectionId> SelectionCycle::Next(const SelectionIds& candidates)
    {
        if (candidates.empty())
        {
            Reset();
            return std::nullopt;
        }

        if (mLastId)
        {
            const auto it = std::find(candidates.begin(), candidates.end(), *mLastId);
            if (it != candidates.end())
            {
                const auto next = std::next(it);
                mLastId = next == candidates.end() ? candidates.front() : *next;
                return mLastId;
            }
        }

        mLastId = candidates.front();
        return mLastId;
    }

    void SelectionCycle::Reset()
    {
        mLastId.reset();
    }
}
