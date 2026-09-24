#include "Commands/Selection/SelectionCommandService.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace
{
    using namespace ra_commands::selection;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    SelectionMember Member(SelectionId id, SelectionKind kind, int ifvMode = 0)
    {
        SelectionMember member;
        member.Id = id;
        member.Kind = kind;
        member.IfvMode = ifvMode;
        return member;
    }

    class MockSelectionPort final : public ISelectionGamePort
    {
    public:
        std::vector<SelectionMember> Selected;
        SelectionIds ExtraSelected;
        std::unordered_set<SelectionId> ExtraKnown;
        std::vector<SelectionMember> Candidates;
        std::unordered_set<SelectionId> ViewportIds;
        std::unordered_set<SelectionId> ValidIds;
        std::uint64_t Session = 1;
        std::uint64_t Frame = 1;
        std::uint64_t TimeMs = 100;

        std::vector<SelectionMember> CaptureSelectedMembers() const override
        {
            return Selected;
        }

        SelectionIds CaptureSelectedIds() const override
        {
            SelectionIds ids;
            for (const auto& member : Selected)
            {
                ids.push_back(member.Id);
            }
            ids.insert(ids.end(), ExtraSelected.begin(), ExtraSelected.end());
            return ids;
        }

        std::vector<SelectionMember> CaptureMembersById(const SelectionIds& ids) const override
        {
            std::vector<SelectionMember> members;
            for (const auto id : ids)
            {
                const auto found = std::find_if(Candidates.begin(), Candidates.end(),
                    [id](const auto& member) { return member.Id == id; });
                if (found != Candidates.end() && ValidIds.contains(id))
                {
                    members.push_back(*found);
                }
            }
            return members;
        }

        std::vector<SelectionMember> CaptureCandidates(SelectionScope scope) const override
        {
            if (scope == SelectionScope::WholeMap)
            {
                return Candidates;
            }
            std::vector<SelectionMember> result;
            for (const auto& member : Candidates)
            {
                if (ViewportIds.contains(member.Id))
                {
                    result.push_back(member);
                }
            }
            return result;
        }

        SelectionIds Apply(const SelectionIds& ids, bool append) override
        {
            std::unordered_map<SelectionId, SelectionMember> known;
            for (const auto& member : Selected)
            {
                known.emplace(member.Id, member);
            }
            for (const auto& member : Candidates)
            {
                known.emplace(member.Id, member);
            }

            if (!append)
            {
                Selected.clear();
                ExtraSelected.clear();
            }
            for (const auto id : ids)
            {
                if (!ValidIds.contains(id))
                {
                    continue;
                }
                if (ExtraKnown.contains(id))
                {
                    if (std::find(ExtraSelected.begin(), ExtraSelected.end(), id) ==
                        ExtraSelected.end())
                    {
                        ExtraSelected.push_back(id);
                    }
                    continue;
                }
                if (!known.contains(id))
                {
                    continue;
                }
                const auto found = std::find_if(Selected.begin(), Selected.end(), [id](const auto& member)
                {
                    return member.Id == id;
                });
                if (found == Selected.end())
                {
                    Selected.push_back(known.at(id));
                }
            }

            SelectionIds applied;
            for (const auto& member : Selected)
            {
                applied.push_back(member.Id);
            }
            applied.insert(applied.end(), ExtraSelected.begin(), ExtraSelected.end());
            return applied;
        }

        bool IsSelectable(SelectionId id) const override
        {
            return ValidIds.contains(id);
        }

        std::uint64_t GetSessionIdentity() const override { return Session; }
        std::uint64_t GetGameFrame() const override { return Frame; }
        std::uint64_t GetMonotonicTimeMs() const override { return TimeMs; }
    };

    void TestKindCycleRetainsOriginalGroup()
    {
        MockSelectionPort port;
        port.Selected = {
            Member(1, SelectionKind::Unit),
            Member(2, SelectionKind::Infantry),
            Member(3, SelectionKind::Aircraft)
        };
        port.Candidates = port.Selected;
        port.ValidIds = {1, 2, 3};
        SelectionCommandService service(port);
        service.OnGameFrame();

        service.OnKindHotkey();
        Require(port.Selected.size() == 1 && port.Selected[0].Id == 1,
            "first kind press must select units");
        service.OnKindHotkey();
        Require(port.Selected.size() == 1 && port.Selected[0].Id == 2,
            "second kind press must use the original group for infantry");
        service.OnKindHotkey();
        Require(port.Selected.size() == 1 && port.Selected[0].Id == 3,
            "third kind press must use the original group for aircraft");
    }

    void TestIfvExpansionKeepsFirstSeeds()
    {
        MockSelectionPort port;
        port.Selected = {Member(1, SelectionKind::Infantry, 2),
            Member(2, SelectionKind::Infantry, 3)};
        port.Candidates = port.Selected;
        port.Candidates.push_back(Member(3, SelectionKind::Unit, 2));
        port.Candidates.push_back(Member(4, SelectionKind::Unit, 3));
        port.Candidates.push_back(Member(5, SelectionKind::Unit, 0));
        port.ViewportIds = {1, 2, 3};
        port.ValidIds = {1, 2, 3, 4, 5};
        SelectionCommandService service(port);
        service.OnGameFrame();

        service.OnIfvHotkey(false, 500);
        Require(port.Selected.size() == 3, "first IFV press must expand within viewport");
        port.TimeMs = 150;
        service.OnIfvHotkey(false, 500);
        Require(port.Selected.size() == 3, "unconfirmed key repeat must not expand whole map");
        port.TimeMs = 180;
        service.OnIfvHotkey(true, 500);
        Require(port.Selected.size() == 4 && port.Selected.back().Id == 4,
            "confirmed second press must use first seeds and include offscreen matches");
    }

    void TestUndoDropsInvalidObjectsAndResetsOnNewSession()
    {
        MockSelectionPort port;
        port.Selected = {Member(1, SelectionKind::Unit), Member(2, SelectionKind::Infantry)};
        port.Candidates = port.Selected;
        port.ValidIds = {1, 2};
        SelectionCommandService service(port);
        service.OnGameFrame();
        service.OnKindHotkey();
        port.ValidIds.erase(2);
        Require(service.Undo(), "undo must find prior snapshot");
        Require(port.Selected.size() == 1 && port.Selected[0].Id == 1,
            "undo must drop invalid object IDs");
        port.Session = 2;
        port.Frame = 0;
        service.OnGameFrame();
        Require(!service.Undo(), "new session must not expose old selection history");
    }

    void TestUndoRestoresBuildingWithUnits()
    {
        MockSelectionPort port;
        port.Selected = {Member(1, SelectionKind::Unit)};
        port.ExtraSelected = {9};
        port.ExtraKnown = {9};
        port.Candidates = port.Selected;
        port.ValidIds = {1, 9};
        SelectionCommandService service(port);
        service.OnGameFrame();
        service.OnKindHotkey();
        Require(port.ExtraSelected.empty(), "unit filter should clear selected building");
        Require(service.Undo() && port.ExtraSelected == SelectionIds{9},
            "undo should restore building IDs from the complete prior selection");
        service.OnGameFrame();
        Require(!service.Undo(),
            "observing restored buildings must not create a false new history entry");
    }

    void TestAmmoCycleRefreshesLiveValues()
    {
        MockSelectionPort port;
        auto full = Member(1, SelectionKind::Unit);
        full.AmmoCurrent = 10;
        full.AmmoCapacity = 10;
        auto partial = Member(2, SelectionKind::Unit);
        partial.AmmoCurrent = 5;
        partial.AmmoCapacity = 10;
        port.Selected = {full, partial};
        port.Candidates = port.Selected;
        port.ValidIds = {1, 2};
        SelectionCommandService service(port);
        service.OnGameFrame();
        service.OnAmmoHotkey();
        Require(port.Selected.size() == 1 && port.Selected[0].Id == 1,
            "first ammo phase should select initially full unit");

        port.Candidates[0].AmmoCurrent = 5;
        port.Candidates[1].AmmoCurrent = 10;
        service.OnAmmoHotkey();
        Require(port.Selected.size() == 1 && port.Selected[0].Id == 1,
            "next ammo phase must use current values from original group IDs");
    }
}

void RunSelectionServiceTests()
{
    TestKindCycleRetainsOriginalGroup();
    TestIfvExpansionKeepsFirstSeeds();
    TestUndoDropsInvalidObjectsAndResetsOnNewSession();
    TestUndoRestoresBuildingWithUnits();
    TestAmmoCycleRefreshesLiveValues();
}
