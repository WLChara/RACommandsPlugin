#include "Commands/AutoNanoCloudCommand/AutoNanoCloudGameAdapter.h"

#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <InfantryClass.h>
#include <ObjectClass.h>
#include <TechnoTypeClass.h>
#include <WeaponTypeClass.h>

#include <cstring>
#include <unordered_set>
#include <utility>

namespace ra_commands::game
{
    namespace
    {
        constexpr int MAX_SELECTION_COUNT = 100000;

        bool IsLiveLocalInfantry(const TechnoClass* techno)
        {
            auto* const local = HouseClass::Player.get();
            return local && techno && techno->WhatAmI() == AbstractType::Infantry &&
                techno->Owner == local &&
                techno->UniqueID != 0 && techno->Health > 0 &&
                techno->IsAlive && techno->IsOnMap && !techno->InLimbo &&
                techno->IsInPlayfield && !techno->IsDead() &&
                !techno->Transporter && techno->GetTechnoType();
        }

        bool HasId(const TechnoClass* techno, const char* id)
        {
            const auto* const type = techno->GetTechnoType();
            return type && ::_stricmp(type->get_ID(), id) == 0;
        }

        bool IsHunter(const TechnoClass* techno)
        {
            return IsLiveLocalInfantry(techno) && HasId(techno, "HUNTR");
        }

        bool IsSacrificeCandidate(const TechnoClass* techno)
        {
            if (!IsLiveLocalInfantry(techno) || HasId(techno, "HUNTR") ||
                HasId(techno, "DUPL"))
            {
                return false;
            }
            const auto* const type = techno->GetTechnoType();
            // 本命令仅排除 BuildLimit=1 的限造步兵。
            return auto_nano_cloud::CanSacrificeBuildLimit(type->BuildLimit);
        }

        TechnoClass* ResolveUnit(auto_nano_cloud::UnitId id)
        {
            auto* const techno = FindLiveTechno(id.UniqueId);
            return techno && reinterpret_cast<std::uintptr_t>(techno) == id.Address
                ? techno : nullptr;
        }

        auto_nano_cloud::UnitId CaptureUnitId(const TechnoClass* techno)
        {
            return {reinterpret_cast<std::uintptr_t>(techno), techno->UniqueID};
        }

        auto_nano_cloud::VictimSnapshot CaptureVictim(TechnoClass* techno)
        {
            const auto cell = techno->GetMapCoords();
            return {CaptureUnitId(techno), techno->GetTechnoType()->Cost,
                techno->Health, cell.X, cell.Y};
        }

        int PrimaryDamage(TechnoClass* techno)
        {
            const auto* const weapon = techno->GetWeapon(0);
            return weapon && weapon->WeaponType ? weapon->WeaponType->Damage : 0;
        }

        bool IsSupportedIntent(const commands::ClickedMissionIntent& intent)
        {
            if (intent.Producer != commands::ClickedMissionProducer::AutoNanoCloud ||
                intent.Actor.Epoch != intent.Epoch || intent.Nearest ||
                intent.TargetCell || intent.DestinationCell)
            {
                return false;
            }
            if (intent.Mission == static_cast<std::int32_t>(Mission::Stop))
            {
                return !intent.Target;
            }
            return intent.Mission == static_cast<std::int32_t>(Mission::Attack) &&
                intent.Target && intent.Target->Epoch == intent.Epoch;
        }
    }

    bool AutoNanoCloudGameAdapter::CaptureSnapshot(
        std::optional<auto_nano_cloud::UnitId> retainedVictim,
        auto_nano_cloud::Snapshot& outSnapshot) const
    {
        if (!IsGameSessionReady())
        {
            return false;
        }
        auto* const selected = &ObjectClass::CurrentObjects.get();
        if (!selected->IsInitialized || selected->Count < 0 ||
            selected->Count > MAX_SELECTION_COUNT ||
            selected->Count > selected->Capacity ||
            (selected->Count > 0 && !selected->Items))
        {
            return false;
        }

        auto_nano_cloud::Snapshot snapshot;
        std::unordered_set<std::uint32_t> seen;
        for (int index = 0; index < selected->Count; ++index)
        {
            auto* const object = selected->Items[index];
            if (!object || object->WhatAmI() != AbstractType::Infantry ||
                !object->IsSelected)
            {
                continue;
            }
            auto* const techno = static_cast<TechnoClass*>(object);
            if (!IsLiveLocalInfantry(techno) || !seen.insert(techno->UniqueID).second)
            {
                continue;
            }

            if (IsHunter(techno))
            {
                const auto cell = techno->GetMapCoords();
                snapshot.Hunters.push_back({CaptureUnitId(techno), PrimaryDamage(techno),
                    cell.X, cell.Y});
            }
            else if (IsSacrificeCandidate(techno))
            {
                snapshot.SelectedVictims.push_back(CaptureVictim(techno));
            }
        }

        if (retainedVictim)
        {
            auto* const techno = ResolveUnit(*retainedVictim);
            if (IsSacrificeCandidate(techno))
            {
                snapshot.RetainedVictim = CaptureVictim(techno);
            }
        }
        outSnapshot = std::move(snapshot);
        return true;
    }

    bool AutoNanoCloudGameAdapter::MakeStopIntent(auto_nano_cloud::UnitId victim,
        std::uint32_t epoch, commands::ClickedMissionIntent& outIntent) const
    {
        auto* const techno = ResolveUnit(victim);
        if (!IsSacrificeCandidate(techno))
        {
            return false;
        }
        commands::ClickedMissionIntent intent;
        intent.Actor = CaptureIdentity(techno, epoch);
        intent.Mission = static_cast<std::int32_t>(Mission::Stop);
        intent.Producer = commands::ClickedMissionProducer::AutoNanoCloud;
        intent.Epoch = epoch;
        intent.CreatedFrame = GetCurrentGameFrame();
        intent.FrameSendRate = GetGameFrameSendRate();
        outIntent = intent;
        return true;
    }

    bool AutoNanoCloudGameAdapter::MakeAttackIntent(auto_nano_cloud::UnitId hunter,
        auto_nano_cloud::UnitId victim, std::uint32_t epoch,
        commands::ClickedMissionIntent& outIntent) const
    {
        auto* const hunterTechno = ResolveUnit(hunter);
        auto* const victimTechno = ResolveUnit(victim);
        if (!IsHunter(hunterTechno) || PrimaryDamage(hunterTechno) <= 0 ||
            !IsSacrificeCandidate(victimTechno))
        {
            return false;
        }
        commands::ClickedMissionIntent intent;
        intent.Actor = CaptureIdentity(hunterTechno, epoch);
        intent.Mission = static_cast<std::int32_t>(Mission::Attack);
        intent.Target = CaptureIdentity(victimTechno, epoch);
        intent.Producer = commands::ClickedMissionProducer::AutoNanoCloud;
        intent.Epoch = epoch;
        intent.CreatedFrame = GetCurrentGameFrame();
        intent.FrameSendRate = GetGameFrameSendRate();
        outIntent = intent;
        return true;
    }

    bool AutoNanoCloudGameAdapter::DeselectAndUngroup(auto_nano_cloud::UnitId victim) const
    {
        auto* const techno = ResolveUnit(victim);
        if (!IsSacrificeCandidate(techno))
        {
            return false;
        }
        // 先解除编队，避免按编队热键重新选中牺牲目标。
        techno->Group = -1;
        if (techno->IsSelected)
        {
            techno->Deselect();
        }
        return true;
    }

    bool ValidateAutoNanoCloudIntent(const commands::ClickedMissionIntent& intent)
    {
        if (!IsSupportedIntent(intent))
        {
            return false;
        }
        auto* const actor = ResolveIdentity(intent.Actor);
        if (intent.Mission == static_cast<std::int32_t>(Mission::Stop))
        {
            return IsSacrificeCandidate(actor);
        }
        return IsHunter(actor) && PrimaryDamage(actor) > 0 &&
            IsSacrificeCandidate(ResolveIdentity(*intent.Target));
    }

    void AttemptAutoNanoCloudIntent(const commands::ClickedMissionIntent& intent)
    {
        if (!ValidateAutoNanoCloudIntent(intent))
        {
            return;
        }
        auto* const actor = ResolveIdentity(intent.Actor);
        if (intent.Mission == static_cast<std::int32_t>(Mission::Stop))
        {
            (void)actor->ClickedMission(Mission::Stop, nullptr, nullptr, nullptr);
        }
        else
        {
            auto* const victim = ResolveIdentity(*intent.Target);
            (void)actor->ClickedMission(Mission::Attack, victim, nullptr, nullptr);
        }
    }
}
