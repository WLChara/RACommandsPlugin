#include "Commands/AutoFormationCommand/AutoFormationGameAdapter.h"
#include "Commands/AutoFormationCommand/AutoFormationMoveState.h"
#include "Commands/AutoFormationCommand/NativeCellOccupancy.h"
#include "Commands/AutoFormationCommand/AutoFormationDiagnostics.h"
#include "Commands/AutoFormationCommand/FormationCandidateBands.h"
#include "Commands/AutoFormationCommand/FormationLimits.h"

#include "ClickedMission/ClickedMissionDispatcher.h"
#include "Game/GameObjectAccess.h"
#include "Game/PluginOrderScope.h"

#include <YRPPCore.h>
#include <CellClass.h>
#include <DisplayClass.h>
#include <FootClass.h>
#include <HouseClass.h>
#include <InfantryClass.h>
#include <MapClass.h>
#include <ObjectClass.h>
#include <TechnoTypeClass.h>
#include <UnitClass.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace ra_commands::game
{
    namespace
    {
        using auto_formation::ActorId;
        using auto_formation::ActorKind;
        using auto_formation::ActorProfile;
        using auto_formation::Cell;
        using auto_formation::OccupancyPlane;
        constexpr std::size_t MAX_ACTORS = auto_formation::MAX_FORMATION_ACTORS;
        constexpr std::size_t MAX_NATIVE_QUERIES = MAX_ACTORS * auto_formation::LARGE_FORMATION_QUERIES_PER_ACTOR;
        constexpr int SEARCH_RADIUS = 32;
        constexpr int AXIS_LIMIT = 512;
        constexpr int MAX_CONTENT = 64;
        constexpr std::size_t MAX_REJECT_SAMPLES = 8;

        struct RejectDiagnostic
        {
            const char* mReason = "unknown";
            int mSourceZone = -2;
            int mDestinationZone = -2;
            int mNativeMove = -1; // 未调用过原生端点查询，不伪造 Move 返回值。
            std::optional<std::uint8_t> mOccupation;
        };

        void TraceRejectedMove(ActorId actor, Cell target, Cell current,
            ActorKind kind, OccupancyPlane plane, bool authorized,
            const RejectDiagnostic& diagnostic) noexcept
        {
            try
            {
            std::ostringstream event;
            event << "submit-rejected actor=" << actor << " target=" << target.mX << ',' << target.mY
                << " kind=" << (kind == ActorKind::Infantry ? "infantry" : "vehicle")
                << " plane=" << (plane == OccupancyPlane::Air ? "air" : "ground")
                << " layer=" << (plane == OccupancyPlane::Air ? "air" :
                    (target.mOnBridge ? "bridge" : "ground"))
                << " current=" << current.mX << ',' << current.mY
                << " authorized=" << authorized << " reason=" << diagnostic.mReason << " sourceZone=";
            if (diagnostic.mSourceZone != -2) { event << diagnostic.mSourceZone; }
            else { event << "not-read"; }
            event << " destinationZone=";
            if (diagnostic.mDestinationZone != -2) { event << diagnostic.mDestinationZone; }
            else { event << "not-read"; }
            event << " occupyByte=";
            if (diagnostic.mOccupation)
            {
                event << "0x" << std::hex << static_cast<unsigned>(*diagnostic.mOccupation) << std::dec;
            }
            else { event << "not-read"; }
            event << " nativeMove=";
            if (diagnostic.mNativeMove >= 0) { event << diagnostic.mNativeMove; }
            else { event << "not-called"; }
            TraceAutoFormation(event.str());
            }
            catch (...) { /* 诊断失败不改变下令的拒绝结果。 */ }
        }

        ActorId EncodeActor(const TechnoClass* actor)
        {
            static_assert(sizeof(void*) == sizeof(std::uint32_t));
            return (static_cast<ActorId>(reinterpret_cast<std::uintptr_t>(actor)) << 32) |
                actor->UniqueID;
        }

        std::optional<ActorProfile> DescribeActor(const TechnoClass* actor)
        {
            if (!actor) { return std::nullopt; }
            // AircraftClass 不属于本命令；在任何 FootClass 转型之前检查真实类型。
            const auto kind = actor->WhatAmI();
            if (kind != AbstractType::Unit && kind != AbstractType::Infantry)
            {
                return std::nullopt;
            }
            const auto* const foot = static_cast<const FootClass*>(actor);
            const auto* const type = actor->GetTechnoType();
            if (!type || !actor->UniqueID || actor->Owner != HouseClass::Player.get() ||
                !actor->IsAlive || !actor->IsOnMap || actor->InLimbo ||
                !actor->IsInPlayfield || actor->IsDead() || actor->Transporter ||
                !auto_formation::CanAcceptFormationMove(actor->Deactivated,
                    actor->IsImmobilized, foot->IsAttackedByLocomotor) ||
                !foot->Locomotor.get())
            {
                return std::nullopt;
            }
            // FrozenStill(+0x6B6) 是正常停步状态；不可用于排除静止步兵或载具。
            const auto layer = foot->InWhichLayer();
            const bool inAir = foot->InAir || layer == Layer::Air || layer == Layer::Top;
            if (!inAir && layer != Layer::Ground && layer != Layer::Surface)
            {
                return std::nullopt;
            }
            const int speed = static_cast<int>(type->SpeedType);
            const int zone = static_cast<int>(type->MovementZone);
            if (speed < 0 || speed > 7 || zone < 0 || zone > 12)
            {
                return std::nullopt;
            }
            return ActorProfile{EncodeActor(actor),
                kind == AbstractType::Infantry ? ActorKind::Infantry : ActorKind::Vehicle,
                inAir ? OccupancyPlane::Air : OccupancyPlane::Ground,
                !inAir && foot->OnBridge};
        }

        FootClass* ResolveActor(ActorId id, ActorProfile* outProfile = nullptr)
        {
            auto* const techno = FindLiveTechno(static_cast<std::uint32_t>(id));
            if (!techno || reinterpret_cast<std::uintptr_t>(techno) !=
                    static_cast<std::uintptr_t>(id >> 32))
            {
                return nullptr;
            }
            const auto profile = DescribeActor(techno);
            if (!profile) { return nullptr; }
            if (outProfile) { *outProfile = *profile; }
            return static_cast<FootClass*>(techno);
        }

        commands::ClickedMissionIdentity Identity(ActorId id, ActorKind kind, std::uint32_t epoch)
        {
            return {static_cast<std::uintptr_t>(id >> 32), static_cast<std::uint32_t>(id),
                static_cast<std::uint32_t>(kind == ActorKind::Infantry
                    ? AbstractType::Infantry : AbstractType::Unit), epoch};
        }

        Cell CurrentCell(const FootClass* actor, const ActorProfile& profile)
        {
            const auto coords = actor->GetMapCoords();
            return {coords.X, coords.Y,
                profile.mPlane == OccupancyPlane::Ground && profile.mOnBridge};
        }

        CellClass* FindCell(MapClass* map, Cell cell)
        {
            if (!map || cell.mX < 0 || cell.mX >= AXIS_LIMIT ||
                cell.mY < 0 || cell.mY >= AXIS_LIMIT)
            {
                return nullptr;
            }
            const CellStruct coords{static_cast<short>(cell.mX), static_cast<short>(cell.mY)};
            // 当前 SHA 门禁样本：ECX=Map，坐标按指针传栈，分别 ret 4/ret 8。
            // 用明确 ABI 避免 SDK 的内联 JMP 改写 EBP，影响当前函数的栈帧。
            using CoordinatesLegalFunction = bool(__thiscall*)(const MapClass*, const CellStruct*);
            using IsWithinUsableAreaFunction = bool(__thiscall*)(const MapClass*, const CellStruct*, bool);
            constexpr std::uintptr_t COORDINATES_LEGAL_ADDRESS = 0x00568300u;
            constexpr std::uintptr_t IS_WITHIN_USABLE_AREA_ADDRESS = 0x00578460u;
            const auto coordinatesLegal = reinterpret_cast<CoordinatesLegalFunction>(COORDINATES_LEGAL_ADDRESS);
            const auto isWithinUsableArea = reinterpret_cast<IsWithinUsableAreaFunction>(IS_WITHIN_USABLE_AREA_ADDRESS);
            if (!coordinatesLegal(map, &coords) || !isWithinUsableArea(map, &coords, false))
            {
                return nullptr;
            }
            auto* const found = map->TryGetCellAt(coords);
            return found && found->MapCoords == coords &&
                (!cell.mOnBridge || found->ContainsBridge()) ? found : nullptr;
        }

        int MovementZoneIndex(MapClass* map, CellClass* cell, MovementZone zone, bool bridge)
        {
            // 仅由 Bootstrap 的 EXE SHA-256 门禁通过后的游戏线程调用。
            // 目标 7cd005d2：0x56D230，ECX=Map，三个栈参，ret 0Ch。
            // 桥面会借 ZoneConnections 映射桥头；合法坐标避免 InvalidCell 写入分支。
            // 结果是预计算静态连通区，不证明动态障碍下的实际寻路成功。
            using Query = int(__thiscall*)(MapClass*, const CellStruct*, MovementZone, bool);
            return reinterpret_cast<Query>(0x56D230u)(map, &cell->MapCoords, zone, bridge);
        }

        bool HasTerrainSpeed(const FootClass* actor, const CellClass* cell, bool bridge)
        {
            // 0x73FAB5 的终点检查读取 0x89EA40 的 float[LandType * 9 + SpeedType]。
            // 桥面路径绕过底层地形倍率，桥下则仍采用底层（例如水面）规则。
            if (bridge) { return cell->ContainsBridge(); }
            const int land = static_cast<int>(cell->LandType);
            const int speed = static_cast<int>(actor->GetTechnoType()->SpeedType);
            if (land < 0 || land > 11 || speed < 0 || speed > 7) { return false; }
            const auto* const costs = reinterpret_cast<const float*>(0x89EA40u);
            const float cost = costs[land * 9 + speed];
            return std::isfinite(cost) && cost > 0.0f;
        }

        bool SameStaticZone(MapClass* map, const FootClass* actor,
            CellClass* origin, CellClass* destination, bool bridge,
            const int* knownSourceZone, std::map<std::uint32_t, int>* zoneCache,
            RejectDiagnostic* diagnostic)
        {
            const auto zone = actor->GetTechnoType()->MovementZone;
            const int sourceZone = knownSourceZone ? *knownSourceZone :
                MovementZoneIndex(map, origin, zone, bridge);
            if (diagnostic) { diagnostic->mSourceZone = sourceZone; }
            if (sourceZone < 0) { return false; }
            int destinationZone;
            if (zoneCache)
            {
                const auto key = (static_cast<std::uint32_t>(zone) << 19) |
                    (static_cast<std::uint32_t>(bridge) << 18) |
                    (static_cast<std::uint32_t>(destination->MapCoords.Y) << 9) |
                    static_cast<std::uint32_t>(destination->MapCoords.X);
                const auto [entry, inserted] = zoneCache->try_emplace(key, -1);
                if (inserted) { entry->second = MovementZoneIndex(map, destination, zone, bridge); }
                destinationZone = entry->second;
            }
            else { destinationZone = MovementZoneIndex(map, destination, zone, bridge); }
            if (diagnostic) { diagnostic->mDestinationZone = destinationZone; }
            return sourceZone == destinationZone;
        }

        struct GroundContents
        {
            unsigned mForeignInfantry = 0;
            unsigned mAuthorizedInfantry = 0;
            bool mHasAuthorizedParticipant = false;
        };

        bool InspectGroundContents(const FootClass* actor, const ActorProfile& profile,
            const CellClass* cell, const std::unordered_set<ActorId>* participants,
            GroundContents& outContents)
        {
            const auto& storedFlags = profile.mOnBridge ? cell->AltOccupationFlags : cell->OccupationFlags;
            const auto flags = auto_formation::ReadNativeOccupationByte(
                reinterpret_cast<const std::uint8_t*>(&storedFlags));
            // 0x1F 为步兵子位置；只读取这个字节，不把相邻三字节当占用。
            if ((flags & 0x80u) != 0) { return false; }
            bool hasVehicle = false;
            bool hasInfantry = false;
            bool hasAirObject = false;
            auto* object = profile.mOnBridge ? cell->AltObject : cell->FirstObject;
            for (int count = 0; object && count < MAX_CONTENT; ++count)
            {
                const auto kind = object->WhatAmI();
                if (kind != AbstractType::Unit && kind != AbstractType::Infantry &&
                    kind != AbstractType::Aircraft)
                {
                    return false;
                }
                auto* const occupant = static_cast<TechnoClass*>(object);
                const auto* const occupantFoot = static_cast<FootClass*>(occupant);
                const auto occupantLayer = occupantFoot->Locomotor.get()
                    ? occupantFoot->InWhichLayer() : Layer::None;
                if (occupantFoot->InAir || occupantLayer == Layer::Air || occupantLayer == Layer::Top)
                {
                    // 空中对象可能也在Cell内容链中；它不消耗地面容量。
                    hasAirObject = true;
                    if (kind == AbstractType::Unit) { hasVehicle = true; }
                    else if (kind == AbstractType::Infantry) { hasInfantry = true; }
                    object = object->NextObject;
                    continue;
                }
                if (kind == AbstractType::Aircraft) { return false; }
                const auto occupantProfile = DescribeActor(occupant);
                if (object->OnBridge != profile.mOnBridge) { return false; }
                const bool isAuthorized = object == actor ||
                    (occupantProfile && participants &&
                        participants->contains(EncodeActor(occupant)));
                if (kind == AbstractType::Unit)
                {
                    hasVehicle = true;
                    if (!isAuthorized) { return false; }
                }
                else
                {
                    hasInfantry = true;
                    if (!isAuthorized) { ++outContents.mForeignInfantry; }
                    else if (object != actor) { ++outContents.mAuthorizedInfantry; }
                }
                if (isAuthorized && object != actor)
                {
                    outContents.mHasAuthorizedParticipant = true;
                }
                object = object->NextObject;
            }
            if (object || ((flags & 0x40u) != 0 &&
                    (!hasAirObject || cell->OverlayTypeIndex != -1)) ||
                ((flags & 0x20u) != 0 && !hasVehicle) ||
                ((flags & 0x1Fu) != 0 && !hasInfantry))
            {
                return false; // 无法归属到实际占用者的原生预约位。
            }
            return profile.mKind != ActorKind::Vehicle || outContents.mForeignInfantry == 0;
        }

        bool HasKnownGroupHeadOnBlock(const UnitClass* actor, const CellClass* cell,
            bool bridge, const std::unordered_set<ActorId>* participants)
        {
            if (bridge || cell->LandType != LandType::Clear || cell->OverlayTypeIndex != -1 ||
                (static_cast<int>(actor->Type->MovementRestrictedTo) != -1 &&
                    actor->Type->MovementRestrictedTo != cell->LandType) || !participants)
            {
                return false;
            }
            const auto actorFacing = static_cast<std::uint16_t>(actor->Facing.current().value());
            auto* object = cell->FirstObject;
            for (int count = 0; object && count < MAX_CONTENT; ++count, object = object->NextObject)
            {
                if (object == actor || object->WhatAmI() != AbstractType::Unit) { continue; }
                const auto* const occupant = static_cast<UnitClass*>(object);
                const auto occupantProfile = DescribeActor(occupant);
                if (occupantProfile && occupantProfile->mPlane == OccupancyPlane::Ground &&
                    participants->contains(EncodeActor(occupant)) &&
                    auto_formation::IsNativeHeadOnBlock(actorFacing,
                        static_cast<std::uint16_t>(occupant->Facing.current().value()),
                        static_cast<double>(occupant->Location.X) - actor->Location.X,
                        static_cast<double>(occupant->Location.Y) - actor->Location.Y,
                        static_cast<double>(occupant->Location.Z) - actor->Location.Z,
                        occupant->Locomotor.get()->Is_Moving()))
                {
                    return true;
                }
            }
            return false;
        }

        struct AirContents
        {
            std::uint8_t mForeignInfantry = 0;
            bool mForeignVehicle = false;
            bool mForeignAircraft = false;
        };
        using AirContentsByCell = std::unordered_map<std::uint32_t, AirContents>;

        std::uint32_t CellKey(Cell cell)
        {
            return (static_cast<std::uint32_t>(cell.mY) << 9) |
                static_cast<std::uint32_t>(cell.mX);
        }

        AirContentsByCell CaptureAirContents(const std::unordered_set<ActorId>* participants)
        {
            AirContentsByCell contents;
            const auto* const technos = TechnoClass::Array.get();
            if (!technos) { return contents; }
            for (int index = 0; index < technos->Count; ++index)
            {
                auto* const techno = technos->Items[index];
                if (!techno || !techno->IsAlive || !techno->IsOnMap || techno->InLimbo ||
                    !techno->IsInPlayfield || techno->IsDead()) { continue; }
                const auto kind = techno->WhatAmI();
                if (kind != AbstractType::Aircraft && kind != AbstractType::Unit &&
                    kind != AbstractType::Infantry) { continue; }
                if (kind != AbstractType::Aircraft)
                {
                    const auto* const foot = static_cast<const FootClass*>(techno);
                    const auto layer = foot->Locomotor.get() ? foot->InWhichLayer() : Layer::None;
                    if (!foot->InAir && layer != Layer::Air && layer != Layer::Top) { continue; }
                    if (participants && participants->contains(EncodeActor(techno)) &&
                        DescribeActor(techno)) { continue; }
                }
                const auto location = techno->GetMapCoords();
                if (location.X < 0 || location.X >= AXIS_LIMIT ||
                    location.Y < 0 || location.Y >= AXIS_LIMIT) { continue; }
                auto& item = contents[CellKey({location.X, location.Y, false})];
                if (kind == AbstractType::Aircraft) { item.mForeignAircraft = true; }
                else if (kind == AbstractType::Unit) { item.mForeignVehicle = true; }
                else if (item.mForeignInfantry < 3) { ++item.mForeignInfantry; }
            }
            return contents;
        }

        std::uint8_t CanOccupy(MapClass* map, const FootClass* actor,
            const ActorProfile& profile, Cell destination,
            const std::unordered_set<ActorId>* participants = nullptr,
            CellClass* knownOrigin = nullptr, CellClass* knownDestination = nullptr,
            const int* knownSourceZone = nullptr, std::map<std::uint32_t, int>* zoneCache = nullptr,
            const AirContentsByCell* knownAirContents = nullptr,
            RejectDiagnostic* diagnostic = nullptr, bool knownStaticPassable = false)
        {
            if (destination.mOnBridge != profile.mOnBridge)
            {
                if (diagnostic) { diagnostic->mReason = "bridge-layer"; }
                return 0;
            }
            auto* const cell = knownDestination ? knownDestination : FindCell(map, destination);
            if (!cell || (destination.mOnBridge && !cell->ContainsBridge()))
            {
                if (diagnostic) { diagnostic->mReason = "map-cell"; }
                return 0;
            }
            if (profile.mPlane == OccupancyPlane::Air)
            {
                if (diagnostic) { diagnostic->mReason = "air-resource"; }
                const AirContentsByCell local = knownAirContents ? AirContentsByCell{} :
                    CaptureAirContents(participants);
                const auto& all = knownAirContents ? *knownAirContents : local;
                const auto found = all.find(CellKey(destination));
                const AirContents contents = found == all.end() ? AirContents{} : found->second;
                if (contents.mForeignAircraft || contents.mForeignVehicle) { return 0; }
                if (profile.mKind == ActorKind::Vehicle)
                {
                    return contents.mForeignInfantry == 0 ? 1 : 0;
                }
                return static_cast<std::uint8_t>(3 - contents.mForeignInfantry);
            }

            auto* const origin = knownOrigin ? knownOrigin : FindCell(map, CurrentCell(actor, profile));
            if (!origin)
            {
                if (diagnostic) { diagnostic->mReason = "map-cell"; }
                return 0;
            }
            if (diagnostic)
            {
                const auto& storedFlags = destination.mOnBridge ? cell->AltOccupationFlags : cell->OccupationFlags;
                diagnostic->mOccupation = auto_formation::ReadNativeOccupationByte(
                    reinterpret_cast<const std::uint8_t*>(&storedFlags));
            }
            if (!knownStaticPassable && !HasTerrainSpeed(actor, cell, destination.mOnBridge))
            {
                if (diagnostic)
                {
                    diagnostic->mReason = "terrain-speed";
                    (void)SameStaticZone(map, actor, origin, cell, destination.mOnBridge,
                        knownSourceZone, zoneCache, diagnostic);
                }
                return 0;
            }
            if (!knownStaticPassable && !SameStaticZone(map, actor, origin, cell, destination.mOnBridge,
                knownSourceZone, zoneCache, diagnostic))
            {
                if (diagnostic) { diagnostic->mReason = "static-zone"; }
                return 0;
            }
            GroundContents ground;
            if (!InspectGroundContents(actor, profile, cell, participants, ground))
            {
                if (diagnostic) { diagnostic->mReason = "content-or-reservation"; }
                return 0;
            }
            const unsigned capacity = profile.mKind == ActorKind::Infantry
                ? 3u - (std::min)(ground.mForeignInfantry, 3u) : 1u;
            if (capacity == 0) { return 0; }

            // Unit与Infantry共享ObjectClass五参虚函数ABI，各由其真实虚表实现。
            // 只查当前端点，不修改原生占位，也不承诺动态路线必能完成。
            const int level = cell->Level + (destination.mOnBridge ? CellClass::BridgeLevels : 0);
            const auto result = actor->IsCellOccupied(cell, -1, level, origin, false);
            if (diagnostic)
            {
                diagnostic->mNativeMove = static_cast<int>(result);
                diagnostic->mReason = "native-endpoint";
            }
            bool boundedNo = false;
            if (result == Move::No && ground.mHasAuthorizedParticipant &&
                !destination.mOnBridge && cell->LandType == LandType::Clear &&
                cell->OverlayTypeIndex == -1)
            {
                if (profile.mKind == ActorKind::Vehicle)
                {
                    boundedNo = HasKnownGroupHeadOnBlock(
                        static_cast<const UnitClass*>(actor), cell, false, participants);
                }
                else
                {
                    const auto& storedFlags = cell->OccupationFlags;
                    const auto flags = auto_formation::ReadNativeOccupationByte(
                        reinterpret_cast<const std::uint8_t*>(&storedFlags));
                    boundedNo = ground.mForeignInfantry == 0 &&
                        ground.mAuthorizedInfantry >= 3 && (flags & 0x1Cu) == 0x1Cu;
                }
            }
            const bool mayWait = ground.mHasAuthorizedParticipant ||
                (profile.mKind == ActorKind::Infantry &&
                    ground.mForeignInfantry > 0 && ground.mForeignInfantry < 3);
            return auto_formation::NativeMoveAllowsGroupDestination(
                static_cast<int>(result), mayWait, boundedNo)
                ? static_cast<std::uint8_t>(capacity) : 0;
        }

        bool IsStopped(const FootClass* actor, const ActorProfile& profile)
        {
            // Jumpjet悬停的原生state2使Is_Moving_Now=false；其+0x6B6
            // 并未见随悬停状态置位，不能沿用地面Foot停步字节判到位。
            if (profile.mPlane == OccupancyPlane::Air)
            {
                return !actor->Locomotor.get()->Is_Moving_Now();
            }
            return actor->IsStandingStill() && !actor->Locomotor.get()->Is_Moving();
        }

        bool CanUseGoalCapacity(const std::map<ActorId, auto_formation::Assignment>& assignments,
            const auto_formation::Assignment& requested, unsigned available)
        {
            std::vector<auto_formation::CapacityClaim> claims;
            for (const auto& [id, assignment] : assignments)
            {
                if (assignment.mDestination != requested.mDestination ||
                    assignment.mPlane != requested.mPlane) { continue; }
                ActorProfile profile;
                auto* const actor = ResolveActor(id, &profile);
                if (!actor || profile.mKind != assignment.mKind ||
                    profile.mPlane != assignment.mPlane ||
                    profile.mOnBridge != assignment.mDestination.mOnBridge) { continue; }
                claims.push_back({id, assignment.mSeat,
                    CurrentCell(actor, profile) == assignment.mDestination});
            }
            return auto_formation::CanAdmitCapacityClaim(claims, requested.mActor, available);
        }

        bool IsMoveMission(Mission mission)
        {
            return mission == Mission::Move || mission == Mission::AttackMove;
        }

        void BudgetDiagnostic()
        {
            OutputDebugStringA("[RACommandsPlugin] AutoFormation snapshot budget exceeded; whole snapshot rejected\n");
        }

        struct OrderedCell
        {
            Cell mCell;
            double mDistance = 0.0;
            CellClass* mNativeCell = nullptr;
        };

        bool CaptureActors(const std::vector<ActorId>& ids,
            auto_formation::Snapshot& outSnapshot,
            std::map<ActorId, FootClass*>& outActors,
            std::unordered_set<ActorId>& outParticipants)
        {
            if (ids.empty() || ids.size() > MAX_ACTORS) { return false; }
            outSnapshot.mActors.reserve(ids.size());
            for (const auto id : ids)
            {
                ActorProfile profile;
                auto* const actor = ResolveActor(id, &profile);
                if (!actor || !outParticipants.insert(id).second) { return false; }
                outActors.emplace(id, actor); // 原生指针只在本次同步采集使用。
                auto_formation::ActorSnapshot snapshot;
                snapshot.mId = id;
                snapshot.mKind = profile.mKind;
                snapshot.mPlane = profile.mPlane;
                snapshot.mCurrentCell = CurrentCell(actor, profile);
                snapshot.mPosition = {actor->Location.X / 256.0 - 0.5,
                    actor->Location.Y / 256.0 - 0.5};
                outSnapshot.mActors.push_back(std::move(snapshot));
            }
            return true;
        }

        bool FillCandidatePass(auto_formation::Snapshot& snapshot,
            const std::map<ActorId, FootClass*>& actors,
            const std::unordered_set<ActorId>& participants,
            const std::vector<OrderedCell>& cells, bool stopAtPerActorLimit,
            const std::vector<auto_formation::PlaneLayout>& planes,
            std::uint8_t infantryCapacity, std::size_t& queries,
            std::unordered_map<ActorId, std::size_t>& queriesByActor,
            std::unordered_map<ActorId, std::unordered_map<std::size_t, std::uint8_t>>& cachedQueries,
            std::unordered_map<ActorId, std::unordered_set<std::size_t>>& acceptedIndices)
        {
            auto* const map = MapClass::Instance.get();
            const auto airContents = CaptureAirContents(&participants); // 整份快照只扫描一次。
            std::map<std::uint32_t, int> zoneCache;
            const bool sparse = snapshot.mActors.size() > auto_formation::LEGACY_FORMATION_ACTORS;
            std::vector<auto_formation::FormationCandidateBand> bands;
            std::unordered_map<ActorId, const auto_formation::FormationCandidateBand*> bandByActor;
            if (sparse)
            {
                std::vector<Cell> coordinates;
                coordinates.reserve(cells.size());
                for (const auto& cell : cells) coordinates.push_back(cell.mCell);
                bands = auto_formation::BuildFormationCandidateBands(snapshot, coordinates, planes, infantryCapacity);
                if (bands.size() != snapshot.mActors.size()) return false;
                for (const auto& band : bands) bandByActor.emplace(band.mActor, &band);
            }
            // 只共享速度/静态连通性；占用链、原生虚调用和对头阻挡仍逐演员判断。
            using StaticProfileKey = std::tuple<int, int, bool, int>;
            std::map<StaticProfileKey, std::unordered_map<std::size_t, bool>> staticPassability;
            const std::size_t perActor = (std::min)(std::size_t{4096},
                (std::max)(std::size_t{64}, std::size_t{4} * snapshot.mActors.size()));
            for (auto& item : snapshot.mActors)
            {
                auto* const actor = actors.at(item.mId);
                const ActorProfile profile{item.mId, item.mKind, item.mPlane,
                    item.mCurrentCell.mOnBridge};
                CellClass* origin = nullptr;
                int sourceZone = -1;
                std::size_t acceptedInBand = 0;
                std::unordered_map<std::size_t, bool>* knownStatic = nullptr;
                if (profile.mPlane == OccupancyPlane::Ground)
                {
                    origin = FindCell(map, item.mCurrentCell);
                    if (!origin) { return false; }
                    sourceZone = MovementZoneIndex(map, origin,
                        actor->GetTechnoType()->MovementZone, profile.mOnBridge);
                    if (sparse)
                    {
                        if (sourceZone < 0) return false;
                        const auto* type = actor->GetTechnoType();
                        const StaticProfileKey key{static_cast<int>(type->SpeedType),
                            static_cast<int>(type->MovementZone), profile.mOnBridge, sourceZone};
                        knownStatic = &staticPassability[key];
                    }
                }
                const auto capture = [&](std::size_t index)
                {
                    const auto& candidate = cells[index];
                    const Cell destination{candidate.mCell.mX, candidate.mCell.mY,
                        profile.mOnBridge};
                    auto& actorQueries = cachedQueries[item.mId];
                    const auto cached = sparse ? actorQueries.find(index) : actorQueries.end();
                    std::uint8_t available = 0;
                    if (cached != actorQueries.end()) available = cached->second;
                    else
                    {
                        if (knownStatic)
                        {
                            const auto [entry, inserted] = knownStatic->try_emplace(index, false);
                            if (inserted)
                                entry->second = HasTerrainSpeed(actor, candidate.mNativeCell, profile.mOnBridge)
                                    && SameStaticZone(map, actor, origin, candidate.mNativeCell, profile.mOnBridge,
                                        &sourceZone, &zoneCache, nullptr);
                            if (!entry->second) return true;
                        }
                        if (++queries > MAX_NATIVE_QUERIES) { BudgetDiagnostic(); return false; }
                        if (sparse && ++queriesByActor[item.mId] > auto_formation::LARGE_FORMATION_QUERIES_PER_ACTOR)
                        { BudgetDiagnostic(); return false; }
                        available = CanOccupy(map, actor, profile, destination,
                            &participants, origin, candidate.mNativeCell,
                            profile.mPlane == OccupancyPlane::Ground ? &sourceZone : nullptr,
                            &zoneCache, &airContents, nullptr, knownStatic != nullptr);
                        if (sparse) actorQueries.emplace(index, available);
                    }
                    if (available == 0) return true;
                    ++acceptedInBand;
                    if (sparse && !acceptedIndices[item.mId].insert(index).second) return true;
                    item.mCandidates.push_back(destination);
                    if (item.mKind == ActorKind::Infantry && available < 3)
                    {
                        item.mCapacityLimits.push_back({destination, available});
                    }
                    return true;
                };
                if (sparse)
                {
                    for (const auto index : bandByActor.at(item.mId)->mCellIndices)
                    {
                        if (!capture(index)) return false;
                        if (acceptedInBand >= auto_formation::LARGE_FORMATION_ACCEPTED_CANDIDATES) break;
                    }
                    if (item.mCandidates.empty()) return false;
                }
                else
                {
                    for (std::size_t index = 0; index < cells.size(); ++index)
                    {
                        if (!capture(index)) return false;
                        if (stopAtPerActorLimit && item.mCandidates.size() >= perActor) break;
                    }
                }
            }
            return true;
        }

        bool FillCandidates(auto_formation::Snapshot& snapshot,
            const std::map<ActorId, FootClass*>& actors,
            const std::unordered_set<ActorId>& participants,
            const std::vector<OrderedCell>& cells, bool stopAtPerActorLimit,
            std::size_t& sparseQueries,
            std::unordered_map<ActorId, std::size_t>& queriesByActor,
            const std::vector<auto_formation::PlaneLayout>& planes = {},
            std::uint8_t infantryCapacity = 3)
        {
            const bool sparse = snapshot.mActors.size() > auto_formation::LEGACY_FORMATION_ACTORS;
            std::size_t legacyQueries = 0;
            auto& queries = sparse ? sparseQueries : legacyQueries;
            std::unordered_map<ActorId, std::unordered_map<std::size_t, std::uint8_t>> cachedQueries;
            std::unordered_map<ActorId, std::unordered_set<std::size_t>> acceptedIndices;
            for (int pass = 0; pass < 3; ++pass)
            {
                if (!FillCandidatePass(snapshot, actors, participants, cells, stopAtPerActorLimit,
                        planes, infantryCapacity, queries, queriesByActor, cachedQueries, acceptedIndices)) return false;
                if (!sparse || !planes.empty()) return true;
                // 保留较紧凑的首轮图，仅在某演员所有已采候选都降容时补充更大的足迹。
                // 同次快照按 actor+cell 去重真实查询；不把占用结果跨演员复用。
                auto capacity = infantryCapacity;
                for (const auto& actor : snapshot.mActors)
                    if (actor.mKind == ActorKind::Infantry && actor.mCapacityLimits.size() == actor.mCandidates.size())
                    {
                        std::uint8_t maximum = 0;
                        for (const auto& limit : actor.mCapacityLimits) maximum = std::max(maximum, limit.mAvailable);
                        capacity = std::min(capacity, maximum);
                    }
                if (capacity >= infantryCapacity) return true;
                infantryCapacity = capacity;
            }
            return true;
        }
    }

    AutoFormationGameAdapter::AutoFormationGameAdapter(commands::ClickedMissionDispatcher& dispatcher)
        : mDispatcher(dispatcher) { }

    bool AutoFormationGameAdapter::IsSessionActive() const
    {
        return mDispatcher.IsSessionActive() && IsGameSessionReady() &&
            MapClass::Instance.get() && !DisplayClass::Instance->PlanningMode;
    }

    std::uint32_t AutoFormationGameAdapter::Epoch() const { return mDispatcher.Epoch(); }
    std::uint32_t AutoFormationGameAdapter::CurrentFrame() const { return GetCurrentGameFrame(); }

    void AutoFormationGameAdapter::PrepareCandidateQueryBudget() const
    {
        const auto epoch = Epoch(), frame = CurrentFrame();
        if (epoch != mCandidateQueryEpoch || frame != mCandidateQueryFrame)
        {
            mCandidateQueryEpoch = epoch;
            mCandidateQueryFrame = frame;
            mCandidateQueries = 0;
            mCandidateQueriesByActor.clear();
        }
    }

    std::vector<ActorProfile> AutoFormationGameAdapter::CaptureSelectedActorProfiles() const
    {
        std::vector<ActorProfile> profiles;
        if (!IsSessionActive()) { return profiles; }
        const auto* const selected = &ObjectClass::CurrentObjects.get();
        if (!selected->IsInitialized || selected->Count < 0 || selected->Count > 100000 ||
            selected->Count > selected->Capacity ||
            (selected->Count > 0 && !selected->Items)) { return profiles; }
        const auto frame = CurrentFrame();
        const bool trace = !mHasSelectionTrace || mSelectionTraceEpoch != Epoch() ||
            frame < mLastSelectionTraceFrame || frame - mLastSelectionTraceFrame >= 60;
        std::size_t rawSelected = 0, nativeStopped = 0, rejectedCore = 0,
            rejectedTransport = 0, rejectedAirOrLayer = 0, rejectedDeactivated = 0,
            rejectedImmobilized = 0, rejectedLocomotorAttack = 0,
            rejectedNoLocomotor = 0, rejectedMovementProfile = 0,
            excludedAircraft = 0, airborne = 0, infantry = 0;
        for (int index = 0; index < selected->Count; ++index)
        {
            auto* const object = selected->Items[index];
            if (!object || !object->IsSelected) { continue; }
            const auto kind = object->WhatAmI();
            if (kind == AbstractType::Aircraft) { ++excludedAircraft; continue; }
            if (kind != AbstractType::Unit && kind != AbstractType::Infantry) { continue; }
            auto* const actor = static_cast<TechnoClass*>(object);
            if (actor->Owner != HouseClass::Player.get()) { continue; }
            ++rawSelected;
            auto* const foot = static_cast<FootClass*>(actor);
            if (trace && foot->IsStandingStill()) { ++nativeStopped; }
            const auto profile = DescribeActor(actor);
            if (profile)
            {
                profiles.push_back(*profile);
                airborne += profile->mPlane == OccupancyPlane::Air;
                infantry += profile->mKind == ActorKind::Infantry;
            }
            else if (trace)
            {
                // 仅热键采集的低频汇总，各谓词计数可重叠，不另改资格规则。
                const auto* const type = actor->GetTechnoType();
                rejectedCore += !type || !actor->UniqueID || !actor->IsAlive ||
                    !actor->IsOnMap || actor->InLimbo || !actor->IsInPlayfield || actor->IsDead();
                rejectedTransport += actor->Transporter != nullptr;
                rejectedNoLocomotor += !foot->Locomotor.get();
                rejectedAirOrLayer += foot->Locomotor.get() &&
                    !foot->InAir && foot->InWhichLayer() != Layer::Ground &&
                    foot->InWhichLayer() != Layer::Surface;
                rejectedDeactivated += actor->Deactivated;
                rejectedImmobilized += actor->IsImmobilized;
                rejectedLocomotorAttack += foot->IsAttackedByLocomotor;
                if (type)
                {
                    const int speed = static_cast<int>(type->SpeedType);
                    const int zone = static_cast<int>(type->MovementZone);
                    rejectedMovementProfile += speed < 0 || speed > 7 || zone < 0 || zone > 12;
                }
            }
        }
        std::sort(profiles.begin(), profiles.end(), [](const ActorProfile& a, const ActorProfile& b)
        {
            return a.mId < b.mId;
        });
        profiles.erase(std::unique(profiles.begin(), profiles.end(),
            [](const ActorProfile& a, const ActorProfile& b) { return a.mId == b.mId; }),
            profiles.end());
        if (trace)
        {
            mHasSelectionTrace = true;
            mLastSelectionTraceFrame = frame;
            mSelectionTraceEpoch = Epoch();
            std::ostringstream event;
            event << "selection rawLocalSelected=" << rawSelected << " eligible=" << profiles.size()
                << " infantry=" << infantry << " air=" << airborne
                << " aircraftExcluded=" << excludedAircraft
                << " nativeStopped=" << nativeStopped << " rejectedCore=" << rejectedCore
                << " rejectedTransport=" << rejectedTransport << " rejectedAirOrLayer=" << rejectedAirOrLayer
                << " rejectedDeactivated=" << rejectedDeactivated << " rejectedImmobilized=" << rejectedImmobilized
                << " rejectedLocomotorAttack=" << rejectedLocomotorAttack
                << " rejectedNoLocomotor=" << rejectedNoLocomotor
                << " rejectedMovementProfile=" << rejectedMovementProfile;
            TraceAutoFormation(event.str());
        }
        return profiles;
    }

    std::vector<ActorId> AutoFormationGameAdapter::CaptureSelectedActorIds() const
    {
        const auto profiles = CaptureSelectedActorProfiles();
        std::vector<ActorId> ids;
        ids.reserve(profiles.size());
        for (const auto& profile : profiles) { ids.push_back(profile.mId); }
        return ids;
    }

    bool AutoFormationGameAdapter::TryCaptureSnapshot(const std::vector<ActorId>& ids,
        std::optional<auto_formation::Center> fixedCenter, auto_formation::Snapshot& outSnapshot) const
    {
        if (!IsSessionActive()) { return false; }
        auto_formation::Snapshot snapshot;
        std::map<ActorId, FootClass*> actors;
        std::unordered_set<ActorId> participants;
        if (!CaptureActors(ids, snapshot, actors, participants)) { return false; }
        snapshot.mCenter = fixedCenter.value_or(auto_formation::CalculateCenter(snapshot.mActors));
        const auto center = snapshot.mCenter;
        if (!std::isfinite(center.mX) || !std::isfinite(center.mY) ||
            center.mX < 0.0 || center.mX >= AXIS_LIMIT || center.mY < 0.0 || center.mY >= AXIS_LIMIT)
        {
            return false;
        }
        std::vector<OrderedCell> cells;
        auto* const map = MapClass::Instance.get();
        const int lowX = (std::max)(0, static_cast<int>(std::ceil(center.mX - SEARCH_RADIUS)));
        const int highX = (std::min)(AXIS_LIMIT - 1, static_cast<int>(std::floor(center.mX + SEARCH_RADIUS)));
        const int lowY = (std::max)(0, static_cast<int>(std::ceil(center.mY - SEARCH_RADIUS)));
        const int highY = (std::min)(AXIS_LIMIT - 1, static_cast<int>(std::floor(center.mY + SEARCH_RADIUS)));
        for (int y = lowY; y <= highY; ++y)
        {
            for (int x = lowX; x <= highX; ++x)
            {
                const double dx = x - center.mX, dy = y - center.mY;
                const double distance = dx * dx + dy * dy;
                if (distance <= SEARCH_RADIUS * SEARCH_RADIUS)
                {
                    const Cell cell{x, y, false};
                    if (auto* const nativeCell = FindCell(map, cell))
                    {
                        cells.push_back({cell, distance, nativeCell});
                    }
                }
            }
        }
        std::sort(cells.begin(), cells.end(), [](const OrderedCell& a, const OrderedCell& b)
        {
            if (a.mDistance != b.mDistance) { return a.mDistance < b.mDistance; }
            if (a.mCell.mY != b.mCell.mY) { return a.mCell.mY < b.mCell.mY; }
            return a.mCell.mX < b.mCell.mX;
        });
        PrepareCandidateQueryBudget();
        if (!FillCandidates(snapshot, actors, participants, cells, true,
                mCandidateQueries, mCandidateQueriesByActor)) { return false; }
        outSnapshot = std::move(snapshot);
        return true;
    }

    bool AutoFormationGameAdapter::TryCaptureSnapshotForCells(
        const std::vector<ActorId>& ids, const std::vector<Cell>& cells,
        auto_formation::Snapshot& outSnapshot,
        const std::vector<auto_formation::PlaneLayout>& planes,
        std::uint8_t infantryCapacity) const
    {
        constexpr std::size_t MAX_REQUESTED_CELLS = auto_formation::MAX_FORMATION_RESOURCE_CELLS;
        if (!IsSessionActive() || ids.empty() || ids.size() > MAX_ACTORS ||
            cells.empty() || cells.size() > MAX_REQUESTED_CELLS ||
            (ids.size() <= auto_formation::LEGACY_FORMATION_ACTORS &&
                ids.size() * cells.size() > MAX_NATIVE_QUERIES))
        {
            return false;
        }
        auto_formation::Snapshot snapshot;
        std::unordered_set<ActorId> participants;
        std::map<ActorId, FootClass*> actors;
        if (!CaptureActors(ids, snapshot, actors, participants)) { return false; }
        auto* const map = MapClass::Instance.get();
        std::map<std::pair<int, int>, CellClass*> requested;
        for (const auto cell : cells)
        {
            if (auto* const nativeCell = FindCell(map, {cell.mX, cell.mY, false}))
            {
                requested.emplace(std::pair{cell.mX, cell.mY}, nativeCell);
            }
        }
        std::vector<OrderedCell> requestedCells;
        requestedCells.reserve(requested.size());
        for (const auto& [coordinate, nativeCell] : requested)
        {
            requestedCells.push_back({{coordinate.first, coordinate.second, false}, 0.0, nativeCell});
        }
        PrepareCandidateQueryBudget();
        if (!FillCandidates(snapshot, actors, participants, requestedCells, false,
                mCandidateQueries, mCandidateQueriesByActor, planes, infantryCapacity)) { return false; }
        snapshot.mCenter = auto_formation::CalculateCenter(snapshot.mActors);
        outSnapshot = std::move(snapshot);
        return true;
    }

    void AutoFormationGameAdapter::BeginPlan(
        const std::vector<auto_formation::Assignment>& assignments)
    {
        Reset();
        if (!IsSessionActive() || assignments.empty() || assignments.size() > MAX_ACTORS) { return; }
        for (const auto& assignment : assignments)
        {
            ActorProfile profile;
            if (!ResolveActor(assignment.mActor, &profile) ||
                profile.mKind != assignment.mKind || profile.mPlane != assignment.mPlane ||
                profile.mOnBridge != assignment.mDestination.mOnBridge ||
                assignment.mSeat >= (assignment.mKind == ActorKind::Infantry ? 3 : 1) ||
                !mParticipants.insert(assignment.mActor).second)
            {
                mParticipants.clear();
                mPlanAssignments.clear();
                return;
            }
            mPlanAssignments.emplace(assignment.mActor, assignment);
        }
        mPlanEpoch = Epoch();
    }

    bool AutoFormationGameAdapter::TryGetActorState(ActorId actor,
        auto_formation::ActorObservation& outState) const
    {
        if (!IsSessionActive()) { return false; }
        ActorProfile profile;
        auto* const foot = ResolveActor(actor, &profile);
        if (!foot) { return false; }
        outState = {CurrentCell(foot, profile), profile.mKind, profile.mPlane};
        return true;
    }

    bool AutoFormationGameAdapter::SubmitMove(const auto_formation::Assignment& assignment)
    {
        const ActorId actor = assignment.mActor;
        const Cell destination = assignment.mDestination;
        const auto authorized = mPlanAssignments.find(actor);
        if (!IsSessionActive() || mPlanEpoch != Epoch() ||
            authorized == mPlanAssignments.end() || authorized->second != assignment)
        {
            return false;
        }
        ActorProfile profile;
        auto* const foot = ResolveActor(actor, &profile);
        if (!foot || profile.mKind != assignment.mKind ||
            profile.mPlane != assignment.mPlane || profile.mOnBridge != destination.mOnBridge)
        {
            return false;
        }
        if (CurrentCell(foot, profile) == destination && IsStopped(foot, profile)) { return true; }
        const auto existing = mMoves.find(actor);
        RejectDiagnostic diagnostic;
        const bool sample = mLoggedRejectedActors.size() < MAX_REJECT_SAMPLES &&
            !mLoggedRejectedActors.contains(actor);
        const auto airContents = profile.mPlane == OccupancyPlane::Air
            ? CaptureAirContents(&mParticipants) : AirContentsByCell{};
        const auto available = CanOccupy(MapClass::Instance.get(), foot, profile, destination,
            &mParticipants, nullptr, nullptr, nullptr, nullptr, &airContents,
            sample ? &diagnostic : nullptr);
        if (!CanUseGoalCapacity(mPlanAssignments, assignment, available))
        {
            if (sample)
            {
                if (available != 0) { diagnostic.mReason = "goal-capacity"; }
                mLoggedRejectedActors.insert(actor);
                TraceRejectedMove(actor, destination, CurrentCell(foot, profile),
                    profile.mKind, profile.mPlane, true, diagnostic);
            }
            return false;
        }
        if (mDispatcher.HasPendingActor(commands::ClickedMissionProducer::AutoFormation,
            reinterpret_cast<std::uintptr_t>(foot), foot->UniqueID))
        {
            return existing != mMoves.end() && existing->second.mDestination == destination;
        }
        commands::ClickedMissionIntent intent;
        intent.Actor = CaptureIdentity(foot, Epoch());
        intent.Producer = commands::ClickedMissionProducer::AutoFormation;
        intent.Supersession = commands::ClickedMissionSupersession::ReplaceSameProducerActorMission;
        intent.Mission = static_cast<std::int32_t>(Mission::Move);
        intent.DestinationCell = commands::CellCoordinate{destination.mX, destination.mY};
        intent.Epoch = Epoch();
        intent.CreatedFrame = CurrentFrame();
        intent.FrameSendRate = GetGameFrameSendRate();
        const auto result = mDispatcher.Submit(intent);
        if (result != commands::ClickedMissionEnqueueResult::Enqueued &&
            result != commands::ClickedMissionEnqueueResult::Replaced &&
            result != commands::ClickedMissionEnqueueResult::Duplicate) { return false; }
        mMoves[actor] = {intent.Actor, destination, 0, false, false};
        return true;
    }

    auto_formation::MoveState AutoFormationGameAdapter::ObserveMove(
        const auto_formation::Assignment& assignment) const
    {
        using auto_formation::MoveState;
        if (!IsSessionActive()) { return MoveState::Invalid; }
        const ActorId actor = assignment.mActor;
        const Cell destination = assignment.mDestination;
        const auto authorized = mPlanAssignments.find(actor);
        ActorProfile profile;
        auto* const foot = ResolveActor(actor, &profile);
        if (mPlanEpoch != Epoch() || authorized == mPlanAssignments.end() ||
            authorized->second != assignment || !foot ||
            profile.mKind != assignment.mKind || profile.mPlane != assignment.mPlane ||
            profile.mOnBridge != destination.mOnBridge)
        {
            return MoveState::Invalid;
        }
        if (CurrentCell(foot, profile) == destination && IsStopped(foot, profile))
        {
            return MoveState::Arrived;
        }
        const auto found = mMoves.find(actor);
        if (found == mMoves.end()) { return MoveState::Waiting; }
        auto& pending = found->second;
        if (pending.mIdentity.Epoch != Epoch() || pending.mDestination != destination)
        {
            return MoveState::Invalid;
        }
        if (mDispatcher.HasPendingActor(commands::ClickedMissionProducer::AutoFormation,
                pending.mIdentity.Address, pending.mIdentity.UniqueId))
        {
            // 点击入口已调用仍可能在扩容传输队列中；以真正离开待发队列的时刻开始
            // 观察原生落地宽限，避免排队时反复复制同一目标。
            pending.mIssuedFrame = CurrentFrame();
            return MoveState::Queued;
        }
        if (!pending.mHasIssued)
        {
            const bool queued = mDispatcher.HasPendingActor(commands::ClickedMissionProducer::AutoFormation,
                pending.mIdentity.Address, pending.mIdentity.UniqueId);
            return *auto_formation::ObservePendingMovePhase(false, queued, false, 0, 0);
        }
        auto* const expectedCell = FindCell(MapClass::Instance.get(), destination);
        const bool expectedOrder = expectedCell && foot->Destination == expectedCell &&
            (IsMoveMission(foot->CurrentMission) || IsMoveMission(foot->QueuedMission));
        if (expectedOrder)
        {
            pending.mHasObservedDestination = true;
            return auto_formation::ObserveIssuedGoalState(false, IsStopped(foot, profile),
                IsMoveMission(foot->CurrentMission) || IsMoveMission(foot->QueuedMission));
        }
        // ClickedMission 只排原生事件，模拟字段可能稍后才更新；原有 Move 的正常
        // 推进、结束或任务切换不能判为覆盖。显式新命令由 Hook 删除追踪记录。
        const auto applyGrace = static_cast<std::uint32_t>((std::clamp)(GetGameFrameSendRate() * 2, 30, 120));
        if (const auto phase = auto_formation::ObservePendingMovePhase(true, false,
            pending.mHasObservedDestination, CurrentFrame() - pending.mIssuedFrame, applyGrace))
        {
            return *phase;
        }
        // 原生可能改为附近目标、保留旧 Target 或恢复 Guard，均不是人工命令证据。
        // 保留服务的固定格：仍走路就继续等，停错格就重试。人工命令由 Hook 取消。
        return auto_formation::ObserveIssuedGoalState(false, IsStopped(foot, profile),
            IsMoveMission(foot->CurrentMission) || IsMoveMission(foot->QueuedMission));
    }

    bool AutoFormationGameAdapter::ValidateIntent(const commands::ClickedMissionIntent& intent) const
    {
        if (!IsSessionActive() || intent.Producer != commands::ClickedMissionProducer::AutoFormation ||
            intent.Mission != static_cast<std::int32_t>(Mission::Move) || !intent.DestinationCell ||
            intent.Target || intent.TargetCell || intent.Nearest || intent.Epoch != Epoch() ||
            intent.Actor.Epoch != intent.Epoch ||
            (intent.Actor.Kind != static_cast<std::uint32_t>(AbstractType::Unit) &&
                intent.Actor.Kind != static_cast<std::uint32_t>(AbstractType::Infantry)))
        {
            return false;
        }
        const ActorId id = (static_cast<ActorId>(intent.Actor.Address) << 32) | intent.Actor.UniqueId;
        const auto found = mMoves.find(id);
        const auto authorized = mPlanAssignments.find(id);
        ActorProfile profile;
        auto* const foot = ResolveActor(id, &profile);
        if (mPlanEpoch != Epoch() || found == mMoves.end() ||
            authorized == mPlanAssignments.end() || !foot ||
            found->second.mIdentity != intent.Actor || found->second.mHasIssued ||
            profile.mKind != authorized->second.mKind ||
            profile.mPlane != authorized->second.mPlane ||
            profile.mOnBridge != authorized->second.mDestination.mOnBridge ||
            intent.Actor.Kind != static_cast<std::uint32_t>(profile.mKind == ActorKind::Infantry
                ? AbstractType::Infantry : AbstractType::Unit))
        {
            return false;
        }
        const auto destination = found->second.mDestination;
        if (destination != authorized->second.mDestination ||
            destination.mX != intent.DestinationCell->X ||
            destination.mY != intent.DestinationCell->Y) { return false; }
        RejectDiagnostic diagnostic;
        const bool sample = mLoggedRejectedActors.size() < MAX_REJECT_SAMPLES &&
            !mLoggedRejectedActors.contains(id);
        const auto airContents = profile.mPlane == OccupancyPlane::Air
            ? CaptureAirContents(&mParticipants) : AirContentsByCell{};
        const auto available = CanOccupy(MapClass::Instance.get(), foot, profile, destination,
            &mParticipants, nullptr, nullptr, nullptr, nullptr, &airContents,
            sample ? &diagnostic : nullptr);
        if (!CanUseGoalCapacity(mPlanAssignments, authorized->second, available))
        {
            if (sample)
            {
                if (available != 0) { diagnostic.mReason = "goal-capacity"; }
                mLoggedRejectedActors.insert(id);
                TraceRejectedMove(id, destination, CurrentCell(foot, profile),
                    profile.mKind, profile.mPlane, true, diagnostic);
            }
            return false;
        }
        return true;
    }

    void AutoFormationGameAdapter::AttemptIntent(const commands::ClickedMissionIntent& intent)
    {
        if (!ValidateIntent(intent)) { return; }
        const ActorId id = (static_cast<ActorId>(intent.Actor.Address) << 32) | intent.Actor.UniqueId;
        ActorProfile profile;
        auto* const foot = ResolveActor(id, &profile);
        auto& pending = mMoves.at(id);
        auto* const cell = FindCell(MapClass::Instance.get(), pending.mDestination);
        if (CurrentCell(foot, profile) == pending.mDestination && IsStopped(foot, profile)) { return; }
        // 中央适配器也有 scope；这里使窄测试/直接处理器调用遵守同一重入边界。
        PluginOrderScope scope;
        (void)foot->ClickedMission(Mission::Move, nullptr, cell, nullptr);
        pending.mHasIssued = true;
        pending.mIssuedFrame = CurrentFrame();
    }

    void AutoFormationGameAdapter::CancelPending(ActorId actor)
    {
        const auto pending = mMoves.find(actor);
        const auto assigned = mPlanAssignments.find(actor);
        if (pending != mMoves.end())
        {
            (void)mDispatcher.CancelByProducerAndActor(
                commands::ClickedMissionProducer::AutoFormation, pending->second.mIdentity);
        }
        else if (assigned != mPlanAssignments.end())
        {
            (void)mDispatcher.CancelByProducerAndActor(
                commands::ClickedMissionProducer::AutoFormation,
                Identity(actor, assigned->second.mKind, Epoch()));
        }
        mMoves.erase(actor);
        mParticipants.erase(actor);
        mPlanAssignments.erase(actor);
    }

    void AutoFormationGameAdapter::Reset()
    {
        (void)mDispatcher.CancelByProducer(commands::ClickedMissionProducer::AutoFormation);
        mMoves.clear();
        mParticipants.clear();
        mPlanAssignments.clear();
        mLoggedRejectedActors.clear();
        mPlanEpoch = 0;
    }
}
