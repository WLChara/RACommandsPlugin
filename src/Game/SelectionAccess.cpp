#include "Game/SelectionAccess.h"

#include "Game/GameObjectAccess.h"

#include <YRPPCore.h>
#include <HouseClass.h>
#include <ObjectClass.h>
#include <TacticalClass.h>
#include <TechnoClass.h>
#include <TechnoTypeClass.h>

#include <unordered_set>

namespace ra_commands::game
{
    namespace
    {
        // 仅防御损坏的游戏数组；不是允许选中的单位数量上限。
        constexpr int MAX_SELECTION_ARRAY_COUNT = 100000;

        template<typename T>
        bool IsUsableArray(const DynamicVectorClass<T*>* array)
        {
            return array && array->IsInitialized && array->Count >= 0 &&
                array->Count <= MAX_SELECTION_ARRAY_COUNT && array->Count <= array->Capacity &&
                (array->Count == 0 || array->Items != nullptr);
        }

        bool IsUnitKind(AbstractType kind)
        {
            return kind == AbstractType::Unit || kind == AbstractType::Infantry ||
                kind == AbstractType::Aircraft;
        }

        bool IsSelectionKind(AbstractType kind)
        {
            return IsUnitKind(kind) || kind == AbstractType::Building;
        }

        bool IsLocalLiveSelectable(TechnoClass* techno)
        {
            return techno && IsSelectionKind(techno->WhatAmI()) &&
                techno->Owner == HouseClass::Player.get() &&
                techno->IsAlive && techno->IsOnMap && !techno->InLimbo &&
                techno->IsInPlayfield && !techno->IsDead() &&
                techno->CanBeSelectedNow();
        }

        selection::SelectionId EncodeIdentity(TechnoClass* techno)
        {
            static_assert(sizeof(void*) == sizeof(std::uint32_t));
            return (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(techno)) << 32) |
                techno->UniqueID;
        }

        TechnoClass* ResolveSelectable(selection::SelectionId id)
        {
            const auto uniqueId = static_cast<std::uint32_t>(id);
            const auto address = static_cast<std::uintptr_t>(id >> 32);
            auto* const techno = FindLiveTechno(uniqueId);
            return techno && reinterpret_cast<std::uintptr_t>(techno) == address &&
                IsLocalLiveSelectable(techno) ? techno : nullptr;
        }

        selection::SelectionKind ConvertKind(AbstractType kind)
        {
            switch (kind)
            {
            case AbstractType::Infantry: return selection::SelectionKind::Infantry;
            case AbstractType::Aircraft: return selection::SelectionKind::Aircraft;
            default: return selection::SelectionKind::Unit;
            }
        }

        bool TryCaptureMember(TechnoClass* techno, selection::SelectionMember& outMember)
        {
            if (!IsLocalLiveSelectable(techno) ||
                !IsUnitKind(techno->WhatAmI()) || techno->UniqueID == 0)
            {
                return false;
            }
            auto* const type = techno->GetTechnoType();
            if (!type)
            {
                return false;
            }

            outMember = {
                EncodeIdentity(techno),
                type->IFVMode,
                techno->IsMindControlled(),
                ConvertKind(techno->WhatAmI()),
                techno->Ammo,
                type->Ammo,
                techno->Passengers.NumPassengers,
                type->Passengers
            };
            return true;
        }
    }

    std::vector<selection::SelectionMember> CaptureSelectedUnits()
    {
        std::vector<selection::SelectionMember> members;
        auto* const selected = &ObjectClass::CurrentObjects.get();
        if (!IsGameSessionReady() || !IsUsableArray(selected))
        {
            return members;
        }

        members.reserve(selected->Count);
        std::unordered_set<selection::SelectionId> seen;
        for (int index = 0; index < selected->Count; ++index)
        {
            auto* const object = selected->Items[index];
            if (!object || !IsUnitKind(object->WhatAmI()) || !object->IsSelected)
            {
                continue;
            }
            selection::SelectionMember member;
            if (TryCaptureMember(static_cast<TechnoClass*>(object), member) &&
                seen.insert(member.Id).second)
            {
                members.push_back(member);
            }
        }
        return members;
    }

    std::vector<selection::SelectionMember> CaptureUnitsById(
        const selection::SelectionIds& ids)
    {
        std::vector<selection::SelectionMember> members;
        members.reserve(ids.size());
        std::unordered_set<selection::SelectionId> seen;
        for (const auto id : ids)
        {
            if (!seen.insert(id).second)
            {
                continue;
            }
            selection::SelectionMember member;
            if (TryCaptureMember(ResolveSelectable(id), member))
            {
                members.push_back(member);
            }
        }
        return members;
    }

    std::vector<selection::SelectionMember> CaptureSelectableUnits(SelectionScope scope)
    {
        std::vector<selection::SelectionMember> members;
        auto* const technos = TechnoClass::Array.get();
        auto* const tactical = TacticalClass::Instance.get();
        if (!IsGameSessionReady() || !IsUsableArray(technos) ||
            (scope == SelectionScope::Viewport && !tactical))
        {
            return members;
        }

        members.reserve(technos->Count);
        std::unordered_set<selection::SelectionId> seen;
        for (int index = 0; index < technos->Count; ++index)
        {
            auto* const techno = technos->Items[index];
            selection::SelectionMember member;
            if (!TryCaptureMember(techno, member))
            {
                continue;
            }
            if (scope == SelectionScope::Viewport)
            {
                Point2D position{};
                if (!tactical->CoordsToClient(techno->GetCoords(), &position))
                {
                    continue;
                }
            }
            if (seen.insert(member.Id).second)
            {
                members.push_back(member);
            }
        }
        return members;
    }

    selection::SelectionIds CaptureSelectedUnitIds()
    {
        selection::SelectionIds ids;
        const auto members = CaptureSelectedUnits();
        ids.reserve(members.size());
        for (const auto& member : members)
        {
            ids.push_back(member.Id);
        }
        return ids;
    }

    selection::SelectionIds CaptureSelectedObjectIds()
    {
        selection::SelectionIds ids;
        auto* const selected = &ObjectClass::CurrentObjects.get();
        if (!IsGameSessionReady() || !IsUsableArray(selected))
        {
            return ids;
        }

        ids.reserve(selected->Count);
        std::unordered_set<selection::SelectionId> seen;
        for (int index = 0; index < selected->Count; ++index)
        {
            auto* const object = selected->Items[index];
            if (!object || !IsSelectionKind(object->WhatAmI()) || !object->IsSelected)
            {
                continue;
            }
            auto* const techno = static_cast<TechnoClass*>(object);
            if (IsLocalLiveSelectable(techno) && techno->UniqueID != 0)
            {
                const auto id = EncodeIdentity(techno);
                if (seen.insert(id).second)
                {
                    ids.push_back(id);
                }
            }
        }
        return ids;
    }

    selection::SelectionIds ApplySelectionIds(const selection::SelectionIds& ids, bool append)
    {
        auto* const selected = &ObjectClass::CurrentObjects.get();
        if (!IsGameSessionReady() || !IsUsableArray(selected))
        {
            return {};
        }

        if (!append)
        {
            // Deselect 会改动 CurrentObjects，必须先复制当前指针列表。
            std::vector<ObjectClass*> prior;
            if (selected->Count > 0)
            {
                prior.assign(selected->Items, selected->Items + selected->Count);
            }
            for (auto* const object : prior)
            {
                if (object && object->IsSelected)
                {
                    object->Deselect();
                }
            }
        }

        std::unordered_set<selection::SelectionId> seen;
        for (const auto id : ids)
        {
            if (!seen.insert(id).second)
            {
                continue;
            }
            auto* const techno = ResolveSelectable(id);
            if (techno && !techno->IsSelected)
            {
                (void)techno->Select();
            }
        }
        return CaptureSelectedObjectIds();
    }

    bool IsSelectableObjectId(selection::SelectionId id)
    {
        return ResolveSelectable(id) != nullptr;
    }

    std::uintptr_t GetSelectionSessionIdentity()
    {
        return reinterpret_cast<std::uintptr_t>(HouseClass::Player.get());
    }
}
