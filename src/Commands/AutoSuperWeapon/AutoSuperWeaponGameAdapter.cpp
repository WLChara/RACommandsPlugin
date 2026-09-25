#include "Commands/AutoSuperWeapon/AutoSuperWeaponGameAdapter.h"

#include "Game/GameObjectAccess.h"
#include "Game/NativeEventCapacity.h"
#include "Game/SuperWeaponAccess.h"
#include "NetworkEvent/NativeNetworkEventAdapter.h"
#include "NetworkEvent/NetworkEvent.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <InfantryClass.h>
#include <MapClass.h>
#include <ObjectClass.h>
#include <TechnoClass.h>
#include <UnitClass.h>
#include <WeaponTypeClass.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace ra_commands::game
{
    namespace
    {
        using auto_super_weapon::Cell;
        using auto_super_weapon::Kind;
        using auto_super_weapon::SelectedUnit;

        constexpr std::uint32_t MIN_NATIVE_FREE_SLOTS = 13;
        constexpr int MAX_SANE_OBJECT_COUNT = 100000;
        constexpr std::size_t MAX_SELECTED_UNITS = 128;
        constexpr std::size_t MAX_ENEMY_UNITS = 128;

        const char* RegisteredName(Kind kind) noexcept
        {
            return kind == Kind::IronCurtain
                ? "IronCurtainSpecial" : "RageInductorSpecial";
        }

        bool IsLiveLocalUnit(const TechnoClass* unit,
            const HouseClass* local, Kind kind)
        {
            if (!unit || unit->Owner != local || unit->UniqueID == 0 ||
                unit->Health <= 0 || !unit->IsAlive || !unit->IsOnMap ||
                unit->InLimbo || !unit->IsInPlayfield || unit->IsDead() ||
                unit->Transporter || unit->InAir || !unit->GetTechnoType())
            {
                return false;
            }
            if (unit->WhatAmI() == AbstractType::Unit)
            {
                const auto* const vehicle = static_cast<const UnitClass*>(unit);
                return vehicle->Type &&
                    vehicle->Type->MovementZone != MovementZone::Fly &&
                    vehicle->Type->SpeedType != SpeedType::Winged &&
                    vehicle->InWhichLayer() == Layer::Ground;
            }
            return kind == Kind::RageInductor &&
                unit->WhatAmI() == AbstractType::Infantry &&
                unit->InWhichLayer() == Layer::Ground;
        }

        bool IsVisibleEnemyUnit(TechnoClass* unit, HouseClass* local)
        {
            if (!unit || !unit->Owner || local->IsAlliedWith(unit->Owner) ||
                unit->UniqueID == 0 || unit->Health <= 0 ||
                !unit->IsAlive || !unit->IsOnMap || unit->InLimbo ||
                !unit->IsInPlayfield || unit->IsDead() ||
                unit->Transporter || !unit->GetTechnoType())
            {
                return false;
            }
            const auto kind = unit->WhatAmI();
            return (kind == AbstractType::Infantry ||
                kind == AbstractType::Unit ||
                kind == AbstractType::Aircraft) &&
                unit->IsSensorVisibleToHouse(local);
        }

        bool IsPotentialFireError(FireError error) noexcept
        {
            switch (error)
            {
            case FireError::OK:
            case FireError::AMMO:
            case FireError::FACING:
            case FireError::REARM:
            case FireError::ROTATING:
            case FireError::MOVING:
            case FireError::RANGE:
            case FireError::BUSY:
            case FireError::MUST_DEPLOY:
                return true;
            default:
                return false;
            }
        }

        bool CanAttackNowOrAfterTemporaryDelay(
            TechnoClass* attacker, TechnoClass* enemy)
        {
            const int weaponIndex = attacker->SelectWeapon(enemy);
            if (weaponIndex < 0)
            {
                return false;
            }
            const auto* const weapon = attacker->GetWeapon(weaponIndex);
            if (!weapon || !weapon->WeaponType ||
                weapon->WeaponType->Damage <= 0 ||
                weapon->WeaponType->Range <= 0)
            {
                return false;
            }
            const auto source = attacker->GetCoords();
            const auto target = enemy->GetCoords();
            const auto dx = static_cast<std::int64_t>(source.X) - target.X;
            const auto dy = static_cast<std::int64_t>(source.Y) - target.Y;
            const auto range = static_cast<std::int64_t>(weapon->WeaponType->Range);
            if (dx * dx + dy * dy > range * range)
            {
                return false;
            }
            return IsPotentialFireError(
                attacker->GetFireErrorWithoutRange(enemy, weaponIndex));
        }

        bool CellLess(Cell left, Cell right) noexcept
        {
            return left.X != right.X ? left.X < right.X : left.Y < right.Y;
        }

        bool IsValidCenter(const MapClass* map, Cell center)
        {
            if (!map ||
                center.X < std::numeric_limits<std::int16_t>::min() ||
                center.X > std::numeric_limits<std::int16_t>::max() ||
                center.Y < std::numeric_limits<std::int16_t>::min() ||
                center.Y > std::numeric_limits<std::int16_t>::max())
            {
                return false;
            }
            const CellStruct cell{
                static_cast<std::int16_t>(center.X),
                static_cast<std::int16_t>(center.Y)};
            return map->CoordinatesLegal(cell) &&
                map->IsWithinUsableArea(cell, false) && map->TryGetCellAt(cell);
        }
    }

    AutoSuperWeaponGameAdapter::AutoSuperWeaponGameAdapter(
        NativeNetworkEventAdapter& events) : mEvents(events)
    {
    }

    bool AutoSuperWeaponGameAdapter::IsMatchReady() const
    {
        return IsGameSessionReady() && HouseClass::Player.get() != nullptr;
    }

    std::uintptr_t AutoSuperWeaponGameAdapter::SessionIdentity() const
    {
        return reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());
    }

    std::uint32_t AutoSuperWeaponGameAdapter::CurrentFrame() const
    {
        return GetCurrentGameFrame();
    }

    bool AutoSuperWeaponGameAdapter::IsWeaponReady(Kind kind) const
    {
        return IsMatchReady() && FindReadyLocalSuperWeapon(
            HouseClass::Player.get(), RegisteredName(kind)).has_value();
    }

    bool AutoSuperWeaponGameAdapter::CaptureSnapshot(
        Kind kind, auto_super_weapon::Snapshot& outSnapshot) const
    {
        if (!IsMatchReady())
        {
            return false;
        }
        auto* const local = HouseClass::Player.get();
        auto* const selected = &ObjectClass::CurrentObjects.get();
        auto* const allTechnos = TechnoClass::Array.get();
        auto* const map = MapClass::Instance.get();
        if (!selected->IsInitialized || selected->Count < 0 ||
            selected->Count > MAX_SANE_OBJECT_COUNT ||
            selected->Count > selected->Capacity ||
            (selected->Count > 0 && !selected->Items) ||
            !allTechnos || !allTechnos->IsInitialized ||
            allTechnos->Count < 0 ||
            allTechnos->Count > MAX_SANE_OBJECT_COUNT ||
            allTechnos->Count > allTechnos->Capacity ||
            (allTechnos->Count > 0 && !allTechnos->Items) || !map)
        {
            return false;
        }

        struct LiveSelected
        {
            TechnoClass* Object;
            Cell Position;
        };
        std::vector<LiveSelected> liveSelected;
        std::unordered_set<std::uint32_t> seenIds;
        auto_super_weapon::Snapshot snapshot;
        for (int index = 0; index < selected->Count; ++index)
        {
            auto* const object = selected->Items[index];
            if (!object || !object->IsSelected ||
                (object->WhatAmI() != AbstractType::Unit &&
                    object->WhatAmI() != AbstractType::Infantry))
            {
                continue;
            }
            auto* const unit = static_cast<TechnoClass*>(object);
            if (!IsLiveLocalUnit(unit, local, kind) ||
                !seenIds.insert(unit->UniqueID).second)
            {
                continue;
            }
            if (snapshot.Selected.size() == MAX_SELECTED_UNITS)
            {
                return false;
            }
            const auto cell = unit->GetMapCoords();
            liveSelected.push_back({unit, {cell.X, cell.Y}});
            snapshot.Selected.push_back({
                {cell.X, cell.Y}, unit->GetTechnoType()->Cost, {}
            });
        }
        if (snapshot.Selected.empty())
        {
            outSnapshot = std::move(snapshot);
            return true;
        }

        struct EnemyCandidate
        {
            TechnoClass* Object;
            std::int64_t DistanceSquared;
            std::uint32_t Id;
        };
        std::vector<EnemyCandidate> enemies;
        for (int index = 0; index < allTechnos->Count; ++index)
        {
            auto* const enemy = allTechnos->Items[index];
            if (!IsVisibleEnemyUnit(enemy, local))
            {
                continue;
            }
            const auto cell = enemy->GetMapCoords();
            std::int64_t nearest = (std::numeric_limits<std::int64_t>::max)();
            for (const auto& selectedUnit : liveSelected)
            {
                const auto dx = static_cast<std::int64_t>(cell.X) - selectedUnit.Position.X;
                const auto dy = static_cast<std::int64_t>(cell.Y) - selectedUnit.Position.Y;
                nearest = (std::min)(nearest, dx * dx + dy * dy);
            }
            enemies.push_back({enemy, nearest, enemy->UniqueID});
        }
        std::sort(enemies.begin(), enemies.end(), [](const auto& left, const auto& right)
        {
            return left.DistanceSquared != right.DistanceSquared
                ? left.DistanceSquared < right.DistanceSquared
                : left.Id < right.Id;
        });
        if (enemies.size() > MAX_ENEMY_UNITS)
        {
            enemies.resize(MAX_ENEMY_UNITS);
        }
        snapshot.EnemyCount = enemies.size();
        for (std::size_t selectedIndex = 0;
            selectedIndex < liveSelected.size(); ++selectedIndex)
        {
            for (std::size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex)
            {
                if (CanAttackNowOrAfterTemporaryDelay(
                    liveSelected[selectedIndex].Object, enemies[enemyIndex].Object))
                {
                    snapshot.Selected[selectedIndex].AttackableEnemies.push_back(
                        static_cast<std::uint16_t>(enemyIndex));
                }
            }
        }

        const int radius = kind == Kind::IronCurtain ? 1 : 4;
        std::vector<Cell> candidates;
        candidates.reserve(snapshot.Selected.size() *
            static_cast<std::size_t>((radius * 2 + 1) * (radius * 2 + 1)));
        for (const auto& unit : snapshot.Selected)
        {
            for (int dx = -radius; dx <= radius; ++dx)
            {
                for (int dy = -radius; dy <= radius; ++dy)
                {
                    const Cell candidate{
                        unit.Position.X + dx, unit.Position.Y + dy};
                    if (auto_super_weapon::Covers(kind, candidate, unit.Position))
                    {
                        candidates.push_back(candidate);
                    }
                }
            }
        }
        std::sort(candidates.begin(), candidates.end(), CellLess);
        candidates.erase(std::unique(candidates.begin(), candidates.end()),
            candidates.end());
        for (const auto candidate : candidates)
        {
            if (IsValidCenter(map, candidate))
            {
                snapshot.CandidateCenters.push_back(candidate);
            }
        }
        outSnapshot = std::move(snapshot);
        return true;
    }

    bool AutoSuperWeaponGameAdapter::TryFireAt(Kind kind, Cell center) const
    {
        if (!IsMatchReady() ||
            GetNativeEventFreeSlots() < MIN_NATIVE_FREE_SLOTS ||
            !IsValidCenter(MapClass::Instance.get(), center))
        {
            return false;
        }
        auto* const local = HouseClass::Player.get();
        if (local->ArrayIndex < 0 || local->ArrayIndex > 255)
        {
            return false;
        }
        const auto ready = FindReadyLocalSuperWeapon(local, RegisteredName(kind));
        if (!ready)
        {
            return false;
        }
        const auto event = network_event::BuildSpecialPlaceEvent(
            static_cast<std::uint8_t>(local->ArrayIndex), ready->Index,
            static_cast<std::int16_t>(center.X),
            static_cast<std::int16_t>(center.Y));
        return mEvents.TryEnqueueLocal(event);
    }
}
