#include "Commands/Selection/SelectionCore.h"

#include <cstdint>
#include <stdexcept>
#include <vector>

#ifdef RA_COMMANDS_SELECTION_STANDALONE_TEST
#include <iostream>
#endif

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

    SelectionMember Member(SelectionId id, int ifvMode = 0)
    {
        SelectionMember member;
        member.Id = id;
        member.IfvMode = ifvMode;
        return member;
    }

    void TestIfvSeedsAndCandidateScope()
    {
        const std::vector<SelectionMember> seeds = {
            Member(1, 2), Member(2, 3), Member(3, 2)
        };
        const std::vector<SelectionMember> screen = {
            Member(10, 3), Member(11, 1), Member(12, 2)
        };
        const std::vector<SelectionMember> field = {
            Member(10, 3), Member(11, 1), Member(12, 2), Member(13, 3)
        };

        Require(FilterSeedIfvModes(seeds, screen) == SelectionIds({10, 12}),
            "single-click candidate scope must match all distinct seed modes");
        Require(FilterSeedIfvModes(seeds, field) == SelectionIds({10, 12, 13}),
            "double-click candidate scope may include matching off-screen members");
        Require(FilterSeedIfvModes({}, field).empty(),
            "empty seeds have no IFV modes to match");
        Require(FilterSeedIfvModes(seeds, {}).empty(),
            "empty candidate scope cannot add members");
        Require(FilterSeedIfvModes({Member(1, 0)},
            {Member(20, 0), Member(21, 2)}) == SelectionIds({20}),
            "IFV mode zero must match as an ordinary mode");
    }

    void TestMindAndKindCyclesUseOriginalMembers()
    {
        auto unit = Member(1);
        unit.Kind = SelectionKind::Unit;
        unit.IsMindControlled = false;
        auto infantry = Member(2);
        infantry.Kind = SelectionKind::Infantry;
        infantry.IsMindControlled = true;
        auto aircraft = Member(3);
        aircraft.Kind = SelectionKind::Aircraft;
        aircraft.IsMindControlled = false;
        const std::vector<SelectionMember> original = {unit, infantry, aircraft};

        Require(FilterMindControlled(original, true) == SelectionIds({2}),
            "mind-controlled state must select matching members");
        Require(FilterMindControlled(original, false) == SelectionIds({1, 3}),
            "next mind-control press must use the original members");
        Require(FilterKind(original, SelectionKind::Unit) == SelectionIds({1}),
            "unit phase must select units");
        const auto infantryKind = NextKind(SelectionKind::Unit);
        const auto aircraftKind = NextKind(infantryKind);
        Require(infantryKind == SelectionKind::Infantry &&
            FilterKind(original, infantryKind) == SelectionIds({2}),
            "second kind phase must reach original infantry");
        Require(aircraftKind == SelectionKind::Aircraft &&
            FilterKind(original, aircraftKind) == SelectionIds({3}) &&
            NextKind(aircraftKind) == SelectionKind::Unit,
            "third kind phase must reach original aircraft and wrap");
    }

    void TestCapacityFiltersUseOriginalMembers()
    {
        auto empty = Member(1);
        empty.AmmoCapacity = 5;
        empty.PassengerCapacity = 3;
        auto partial = Member(2);
        partial.AmmoCurrent = 2;
        partial.AmmoCapacity = 5;
        partial.PassengerCurrent = 1;
        partial.PassengerCapacity = 3;
        auto full = Member(3);
        full.AmmoCurrent = 5;
        full.AmmoCapacity = 5;
        full.PassengerCurrent = 3;
        full.PassengerCapacity = 3;
        auto overfull = Member(4);
        overfull.AmmoCurrent = 6;
        overfull.AmmoCapacity = 5;
        overfull.PassengerCurrent = 4;
        overfull.PassengerCapacity = 3;
        auto noCapacity = Member(5);
        const std::vector<SelectionMember> original = {
            empty, partial, full, overfull, noCapacity
        };

        Require(FilterAmmo(original, FillState::Full) == SelectionIds({3, 4}),
            "full ammo includes capacity and over-capacity members");
        Require(FilterAmmo(original, FillState::NotFull) == SelectionIds({1, 2}),
            "later ammo phase must include empty members from the original set");
        Require(FilterAmmo(original, FillState::Empty) == SelectionIds({1}),
            "empty ammo phase must still use the original set");
        Require(FilterPassengers(original, FillState::Full) == SelectionIds({3, 4}),
            "full passenger filter must use passenger capacity");
        Require(FilterPassengers(original, FillState::NotFull) == SelectionIds({1, 2}),
            "not-full passengers include empty transports");
        Require(FilterPassengers(original, FillState::Empty) == SelectionIds({1}),
            "empty passenger phase must reach original transports");
    }

    void TestHistoryAndValidity()
    {
        SelectionHistory history;
        Require(!history.Restore(0, [](SelectionId) { return true; }).has_value(),
            "missing snapshot must differ from an empty snapshot");
        history.Save({});
        const auto empty = history.Restore(0, [](SelectionId) { return true; });
        Require(empty.has_value() && empty->empty(),
            "an empty snapshot must be restorable");

        SelectionIds source = {1, 2, 3};
        history.Save(source);
        source.clear();
        const auto restored = history.Restore(0, [](SelectionId id) { return id != 2; });
        Require(restored == SelectionIds({1, 3}),
            "history must own ID values and filter expired IDs at restore time");
        const auto allExpired = history.Restore(0, [](SelectionId) { return false; });
        Require(allExpired.has_value() && allExpired->empty(),
            "a snapshot with no surviving IDs must still restore as empty");
        Require(history.Restore(1, [](SelectionId) { return true; })->empty(),
            "older empty snapshot must remain restorable");

        history.Clear();
        for (SelectionId id = 1; id <= 13; ++id)
        {
            history.Save({id});
        }
        Require(history.Size() == SelectionHistory::MAX_SNAPSHOTS &&
            history.Restore(0, [](SelectionId) { return true; }) == SelectionIds({13}) &&
            history.Restore(11, [](SelectionId) { return true; }) == SelectionIds({2}) &&
            !history.Restore(12, [](SelectionId) { return true; }).has_value(),
            "the newest twelve snapshots must be kept in order");
        history.Clear();
        Require(history.Size() == 0, "clear must discard all history");
    }

    void TestSingleMemberCycle()
    {
        SelectionCycle cycle;
        const SelectionIds ids = {1, 2, 3};
        Require(cycle.Next(ids) == 1 && cycle.Next(ids) == 2 &&
            cycle.Next(ids) == 3 && cycle.Next(ids) == 1,
            "single-member selection must wrap through current candidates");
        Require(cycle.Next({3, 2, 1}) == 3,
            "each press must use the new candidate order");
        Require(cycle.Next({2, 1}) == 2,
            "missing last ID must restart from first current candidate");
        Require(!cycle.Next({}).has_value() && cycle.Next(ids) == 1,
            "empty candidates must reset the cycle");
        (void)cycle.Next(ids);
        cycle.Reset();
        Require(cycle.Next(ids) == 1, "explicit reset must restart the cycle");
    }
}

void RunSelectionTests()
{
    TestIfvSeedsAndCandidateScope();
    TestMindAndKindCyclesUseOriginalMembers();
    TestCapacityFiltersUseOriginalMembers();
    TestHistoryAndValidity();
    TestSingleMemberCycle();
}

#ifdef RA_COMMANDS_SELECTION_STANDALONE_TEST
int main()
{
    try
    {
        RunSelectionTests();
        std::cout << "Selection pure tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
#endif
