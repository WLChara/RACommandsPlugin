#include "Commands/AutoFormationCommand/AutoFormationGameAdapter.h"
#include "Commands/AutoFormationCommand/AutoFormationMoveState.h"
#include "Commands/AutoFormationCommand/NativeCellOccupancy.h"
#include "Commands/AutoFormationCommand/AutoFormationDiagnostics.h"

#include "ClickedMission/ClickedMissionDispatcher.h"
#include "Game/GameObjectAccess.h"
#include "Game/PluginOrderScope.h"

#include <YRPPCore.h>
#include <CellClass.h>
#include <DisplayClass.h>
#include <HouseClass.h>
#include <MapClass.h>
#include <UnitClass.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_set>

namespace ra_commands::game
{
    namespace
    {
        using auto_formation::ActorId;
        using auto_formation::Cell;
        constexpr std::size_t MAX_ACTORS = 256;
        constexpr std::size_t MAX_EDGES = 262144;
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
            bool authorized, const RejectDiagnostic& diagnostic) noexcept
        {
            try
            {
            std::ostringstream event;
            event << "submit-rejected actor=" << actor << " target=" << target.mX << ',' << target.mY
                << " layer=" << (target.mOnBridge ? "bridge" : "ground")
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

        ActorId EncodeActor(const UnitClass* unit)
        {
            static_assert(sizeof(void*) == sizeof(std::uint32_t));
            return (static_cast<ActorId>(reinterpret_cast<std::uintptr_t>(unit)) << 32) |
                unit->UniqueID;
        }

        bool IsEligible(const UnitClass* unit)
        {
            // SDK FrozenStill(+0x6B6) 实为普通停步状态：Unit vtbl+0xC0 的
            // 0x41C070 IsStandingStill 直接返回该字节。Foot ctor 初值1，Drive
            // 0x4B161A 走动清0、0x4B1FEF 停步置1，不能将它当作失效资格。
            if (!unit || !unit->Type || !unit->UniqueID ||
                unit->Owner != HouseClass::Player.get() || !unit->IsAlive ||
                !unit->IsOnMap || unit->InLimbo || !unit->IsInPlayfield || unit->IsDead() ||
                unit->Transporter || unit->InAir ||
                !auto_formation::CanAcceptFormationMove(unit->Deactivated,
                    unit->IsImmobilized, unit->IsAttackedByLocomotor) ||
                !unit->Locomotor.get() || unit->InWhichLayer() != Layer::Ground)
            {
                return false;
            }
            const int speed = static_cast<int>(unit->Type->SpeedType);
            const int zone = static_cast<int>(unit->Type->MovementZone);
            return speed >= 0 && speed <= 7 && speed != static_cast<int>(SpeedType::Winged) &&
                zone >= 0 && zone <= 12 && zone != static_cast<int>(MovementZone::Fly);
        }

        UnitClass* ResolveActor(ActorId id)
        {
            auto* const techno = FindLiveTechno(static_cast<std::uint32_t>(id));
            if (!techno || reinterpret_cast<std::uintptr_t>(techno) !=
                    static_cast<std::uintptr_t>(id >> 32) || techno->WhatAmI() != AbstractType::Unit)
            {
                return nullptr;
            }
            auto* const unit = static_cast<UnitClass*>(techno);
            return IsEligible(unit) ? unit : nullptr;
        }

        commands::ClickedMissionIdentity Identity(ActorId id, std::uint32_t epoch)
        {
            return {static_cast<std::uintptr_t>(id >> 32), static_cast<std::uint32_t>(id),
                static_cast<std::uint32_t>(AbstractType::Unit), epoch};
        }

        Cell CurrentCell(const UnitClass* unit)
        {
            const auto coords = unit->GetMapCoords();
            return {coords.X, coords.Y, unit->OnBridge};
        }

        CellClass* FindCell(MapClass* map, Cell cell)
        {
            if (!map || cell.mX < 0 || cell.mX >= AXIS_LIMIT ||
                cell.mY < 0 || cell.mY >= AXIS_LIMIT)
            {
                return nullptr;
            }
            const CellStruct coords{static_cast<short>(cell.mX), static_cast<short>(cell.mY)};
            if (!map->CoordinatesLegal(coords) || !map->IsWithinUsableArea(coords, false))
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

        bool HasTerrainSpeed(const UnitClass* unit, const CellClass* cell, bool bridge)
        {
            // 0x73FAB5 的终点检查读取 0x89EA40 的 float[LandType * 9 + SpeedType]。
            // 桥面路径绕过底层地形倍率，桥下则仍采用底层（例如水面）规则。
            if (bridge) { return cell->ContainsBridge(); }
            const int land = static_cast<int>(cell->LandType);
            const int speed = static_cast<int>(unit->Type->SpeedType);
            if (land < 0 || land > 11 || speed < 0 || speed > 7) { return false; }
            const auto* const costs = reinterpret_cast<const float*>(0x89EA40u);
            const float cost = costs[land * 9 + speed];
            return std::isfinite(cost) && cost > 0.0f;
        }

        bool SameStaticZone(MapClass* map, const UnitClass* unit,
            CellClass* origin, CellClass* destination, bool bridge,
            const int* knownSourceZone, std::map<std::uint32_t, int>* zoneCache,
            RejectDiagnostic* diagnostic)
        {
            const auto zone = unit->Type->MovementZone;
            const int sourceZone = knownSourceZone ? *knownSourceZone :
                MovementZoneIndex(map, origin, zone, unit->OnBridge);
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

        bool AllowedContents(const UnitClass* unit, const CellClass* cell, bool bridge,
            const std::unordered_set<ActorId>* participants, bool& hasParticipant)
        {
            hasParticipant = false;
            auto* object = bridge ? cell->AltObject : cell->FirstObject;
            const auto& storedFlags = bridge ? cell->AltOccupationFlags : cell->OccupationFlags;
            // Cell ctor 0x47BBF0 只清首 BYTE；IsCellOccupied 同样只用低字节。
            // 整 DWORD 的高位可能非零，但这些字节并不是占用信息。
            const auto flags = auto_formation::ReadNativeOccupationByte(
                reinterpret_cast<const std::uint8_t*>(&storedFlags));
            // 步兵、物体、建筑和无法归属的预约位不当作本队可释放格。
            if (!auto_formation::OccupationAllowsVehicleContents(flags, true)) { return false; }
            bool hasUnit = false;
            for (int count = 0; object && count < MAX_CONTENT; ++count)
            {
                if (object->WhatAmI() != AbstractType::Unit || object->OnBridge != bridge)
                {
                    return false;
                }
                const auto* const occupant = static_cast<UnitClass*>(object);
                hasUnit = true;
                if (occupant != unit)
                {
                    if (!participants || !participants->contains(EncodeActor(occupant)) ||
                        !IsEligible(occupant))
                    {
                        return false;
                    }
                    hasParticipant = true;
                }
                object = object->NextObject;
            }
            return !object && auto_formation::OccupationAllowsVehicleContents(flags, hasUnit);
        }

        bool HasKnownGroupHeadOnBlock(const UnitClass* unit, const CellClass* cell,
            bool bridge, const std::unordered_set<ActorId>* participants)
        {
            // 只放行已取证的普通 Clear 临占场景，不通配 No，也不复制整套原生规则。
            if (bridge || cell->LandType != LandType::Clear || cell->OverlayTypeIndex != -1 ||
                (static_cast<int>(unit->Type->MovementRestrictedTo) != -1 &&
                    unit->Type->MovementRestrictedTo != cell->LandType) || !participants)
            {
                return false;
            }
            const auto actorFacing = static_cast<std::uint16_t>(unit->Facing.current().value());
            auto* object = cell->FirstObject;
            for (int count = 0; object && count < MAX_CONTENT; ++count, object = object->NextObject)
            {
                if (object == unit || object->WhatAmI() != AbstractType::Unit) { continue; }
                const auto* const occupant = static_cast<UnitClass*>(object);
                if (participants->contains(EncodeActor(occupant)) && IsEligible(occupant) &&
                    auto_formation::IsNativeHeadOnBlock(actorFacing,
                        static_cast<std::uint16_t>(occupant->Facing.current().value()),
                        static_cast<double>(occupant->Location.X) - unit->Location.X,
                        static_cast<double>(occupant->Location.Y) - unit->Location.Y,
                        static_cast<double>(occupant->Location.Z) - unit->Location.Z,
                        occupant->Locomotor.get()->Is_Moving()))
                {
                    return true;
                }
            }
            return false;
        }

        bool CanOccupy(MapClass* map, const UnitClass* unit, Cell destination,
            const std::unordered_set<ActorId>* participants = nullptr,
            CellClass* knownOrigin = nullptr, CellClass* knownDestination = nullptr,
            const int* knownSourceZone = nullptr, std::map<std::uint32_t, int>* zoneCache = nullptr,
            RejectDiagnostic* diagnostic = nullptr)
        {
            if (unit->OnBridge != destination.mOnBridge)
            {
                if (diagnostic) { diagnostic->mReason = "bridge-layer"; }
                return false;
            }
            auto* const origin = knownOrigin ? knownOrigin : FindCell(map, CurrentCell(unit));
            auto* const cell = knownDestination ? knownDestination : FindCell(map, destination);
            if (!origin || !cell)
            {
                if (diagnostic) { diagnostic->mReason = "map-cell"; }
                return false;
            }
            if (diagnostic)
            {
                const auto& storedFlags = destination.mOnBridge ? cell->AltOccupationFlags : cell->OccupationFlags;
                diagnostic->mOccupation = auto_formation::ReadNativeOccupationByte(
                    reinterpret_cast<const std::uint8_t*>(&storedFlags));
            }
            if (!HasTerrainSpeed(unit, cell, destination.mOnBridge))
            {
                if (diagnostic)
                {
                    diagnostic->mReason = "terrain-speed";
                    // 只对这次有限拒绝样本补读 Zone，合法原生查询仍不改业务判定。
                    (void)SameStaticZone(map, unit, origin, cell, destination.mOnBridge,
                        knownSourceZone, zoneCache, diagnostic);
                }
                return false;
            }
            if (!SameStaticZone(map, unit, origin, cell, destination.mOnBridge,
                knownSourceZone, zoneCache, diagnostic))
            {
                if (diagnostic) { diagnostic->mReason = "static-zone"; }
                return false;
            }
            bool hasParticipant = false;
            if (!AllowedContents(unit, cell, destination.mOnBridge, participants, hasParticipant))
            {
                if (diagnostic) { diagnostic->mReason = "content-or-reservation"; }
                return false;
            }
            // ObjectClass.h 的五参虚函数；Unit vtbl+0x1AC=0x73F0A0，ret 14h。
            // facing=-1 为任意远终点查询，跳过相邻方向坡层检查；level 指定目标层。
            // sourceCell 仍提供合法源格，alt=false 避开 Foot 的 locomotor 查询。
            // 此处只查询，不移除占用位、不写游戏状态，也不声称整条路线可达。
            const int level = cell->Level + (destination.mOnBridge ? CellClass::BridgeLevels : 0);
            const auto result = unit->IsCellOccupied(cell, -1, level, origin, false);
            if (diagnostic)
            {
                diagnostic->mNativeMove = static_cast<int>(result);
                diagnostic->mReason = "native-endpoint";
            }
            // 规划与发送采用相同组授权：队友停止临占返回 Temp=6，移动临占可返回2。
            // 0x73FA10..0x73FA26 的近距离迎面移动分支也返回 No=7；只开放上述
            // 普通 Clear 子集。目标仍交给 native Move/避碰处理，不改占用位或坐标。
            const bool headOnBlock = result == Move::No && hasParticipant &&
                HasKnownGroupHeadOnBlock(unit, cell, destination.mOnBridge, participants);
            return auto_formation::NativeMoveAllowsGroupDestination(
                static_cast<int>(result), hasParticipant, headOnBlock);
        }

        bool IsStopped(const UnitClass* unit)
        {
            return unit->IsStandingStill() && !unit->Locomotor.get()->Is_Moving();
        }

        bool IsMoveMission(Mission mission)
        {
            return mission == Mission::Move || mission == Mission::AttackMove;
        }

        void BudgetDiagnostic()
        {
            OutputDebugStringA("[RACommandsPlugin] AutoFormation snapshot budget exceeded; whole snapshot rejected\n");
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

    std::vector<ActorId> AutoFormationGameAdapter::CaptureSelectedActorIds() const
    {
        std::vector<ActorId> ids;
        if (!IsSessionActive()) { return ids; }
        const auto frame = CurrentFrame();
        const bool trace = !mHasSelectionTrace || mSelectionTraceEpoch != Epoch() ||
            frame < mLastSelectionTraceFrame || frame - mLastSelectionTraceFrame >= 60;
        std::size_t rawSelected = 0, nativeStopped = 0, rejectedCore = 0,
            rejectedTransport = 0, rejectedAirOrLayer = 0, rejectedDeactivated = 0,
            rejectedImmobilized = 0, rejectedLocomotorAttack = 0,
            rejectedNoLocomotor = 0, rejectedMovementProfile = 0;
        const auto* const units = UnitClass::Array.get();
        for (int index = 0; index < units->Count; ++index)
        {
            auto* const unit = units->Items[index];
            if (!unit || !unit->IsSelected || unit->Owner != HouseClass::Player.get()) { continue; }
            ++rawSelected;
            if (trace && unit->IsStandingStill()) { ++nativeStopped; }
            if (IsEligible(unit)) { ids.push_back(EncodeActor(unit)); }
            else if (trace)
            {
                // 仅热键采集的低频汇总，各谓词计数可重叠，不另改资格规则。
                rejectedCore += !unit->Type || !unit->UniqueID || !unit->IsAlive ||
                    !unit->IsOnMap || unit->InLimbo || !unit->IsInPlayfield || unit->IsDead();
                rejectedTransport += unit->Transporter != nullptr;
                rejectedNoLocomotor += !unit->Locomotor.get();
                rejectedAirOrLayer += unit->InAir ||
                    (unit->Locomotor.get() && unit->InWhichLayer() != Layer::Ground);
                rejectedDeactivated += unit->Deactivated;
                rejectedImmobilized += unit->IsImmobilized;
                rejectedLocomotorAttack += unit->IsAttackedByLocomotor;
                if (unit->Type)
                {
                    const int speed = static_cast<int>(unit->Type->SpeedType);
                    const int zone = static_cast<int>(unit->Type->MovementZone);
                    rejectedMovementProfile += speed < 0 || speed > 7 ||
                        speed == static_cast<int>(SpeedType::Winged) || zone < 0 || zone > 12 ||
                        zone == static_cast<int>(MovementZone::Fly);
                }
            }
        }
        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        if (trace)
        {
            mHasSelectionTrace = true;
            mLastSelectionTraceFrame = frame;
            mSelectionTraceEpoch = Epoch();
            std::ostringstream event;
            event << "selection rawLocalSelected=" << rawSelected << " eligible=" << ids.size()
                << " nativeStopped=" << nativeStopped << " rejectedCore=" << rejectedCore
                << " rejectedTransport=" << rejectedTransport << " rejectedAirOrLayer=" << rejectedAirOrLayer
                << " rejectedDeactivated=" << rejectedDeactivated << " rejectedImmobilized=" << rejectedImmobilized
                << " rejectedLocomotorAttack=" << rejectedLocomotorAttack
                << " rejectedNoLocomotor=" << rejectedNoLocomotor
                << " rejectedMovementProfile=" << rejectedMovementProfile;
            TraceAutoFormation(event.str());
        }
        return ids;
    }

    bool AutoFormationGameAdapter::TryCaptureSnapshot(const std::vector<ActorId>& ids,
        std::optional<auto_formation::Center> fixedCenter, auto_formation::Snapshot& outSnapshot) const
    {
        if (!IsSessionActive()) { return false; }
        std::unordered_set<ActorId> participants(ids.begin(), ids.end());
        if (participants.size() > MAX_ACTORS) { BudgetDiagnostic(); return false; }
        auto_formation::Snapshot snapshot;
        std::map<ActorId, UnitClass*> actors; // 只活在本次采集调用，不跨帧保留指针。
        const auto* const units = UnitClass::Array.get();
        for (int index = 0; index < units->Count; ++index)
        {
            auto* const unit = units->Items[index];
            if (IsEligible(unit) && participants.contains(EncodeActor(unit)))
            {
                actors.emplace(EncodeActor(unit), unit);
            }
        }
        participants.clear();
        for (const auto& [id, unit] : actors)
        {
            participants.insert(id);
            // Cell2Coord 格中心为 X/Y*256+128，因此连续格坐标要减去半格。
            snapshot.mActors.push_back({id, CurrentCell(unit),
                {unit->Location.X / 256.0 - 0.5, unit->Location.Y / 256.0 - 0.5}, {}});
        }
        snapshot.mCenter = fixedCenter.value_or(auto_formation::CalculateCenter(snapshot.mActors));
        const auto center = snapshot.mCenter;
        if (!std::isfinite(center.mX) || !std::isfinite(center.mY) ||
            center.mX < 0.0 || center.mX >= AXIS_LIMIT || center.mY < 0.0 || center.mY >= AXIS_LIMIT)
        {
            return false;
        }
        struct OrderedCell { Cell mCell; double mDistance; CellClass* mNativeCell; };
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
        const std::size_t perActor = (std::min)(std::size_t{4096},
            (std::max)(std::size_t{64}, std::size_t{4} * snapshot.mActors.size()));
        std::size_t edges = 0;
        std::map<std::uint32_t, int> zoneCache; // 本次采集的值缓存，不跨帧。
        for (auto& actor : snapshot.mActors)
        {
            const auto* const unit = actors.at(actor.mId);
            auto* const origin = FindCell(map, actor.mCurrentCell);
            if (!origin) { continue; }
            const int sourceZone = MovementZoneIndex(map, origin, unit->Type->MovementZone, unit->OnBridge);
            for (const auto& candidate : cells)
            {
                auto cell = candidate.mCell;
                cell.mOnBridge = unit->OnBridge;
                if (CanOccupy(map, unit, cell, &participants, origin,
                    candidate.mNativeCell, &sourceZone, &zoneCache))
                {
                    if (++edges > MAX_EDGES) { BudgetDiagnostic(); return false; }
                    actor.mCandidates.push_back(cell);
                    if (actor.mCandidates.size() == perActor) { break; }
                }
            }
        }
        outSnapshot = std::move(snapshot);
        return true;
    }

    void AutoFormationGameAdapter::BeginPlan(const std::vector<ActorId>& assignedActors)
    {
        Reset();
        if (!IsSessionActive() || assignedActors.size() > MAX_ACTORS) { return; }
        mPlanEpoch = Epoch();
        for (const auto id : assignedActors)
        {
            if (ResolveActor(id)) { mParticipants.insert(id); }
        }
    }

    bool AutoFormationGameAdapter::TryGetActorCell(ActorId actor, Cell& outCell) const
    {
        if (!IsSessionActive()) { return false; }
        auto* const unit = ResolveActor(actor);
        if (!unit) { return false; }
        outCell = CurrentCell(unit);
        return true;
    }

    bool AutoFormationGameAdapter::SubmitMove(ActorId actor, Cell destination)
    {
        if (!IsSessionActive() || mPlanEpoch != Epoch() || !mParticipants.contains(actor)) { return false; }
        auto* const unit = ResolveActor(actor);
        if (!unit || unit->OnBridge != destination.mOnBridge) { return false; }
        if (CurrentCell(unit) == destination && IsStopped(unit)) { return true; }
        const auto existing = mMoves.find(actor);
        RejectDiagnostic diagnostic;
        const bool sample = mLoggedRejectedActors.size() < MAX_REJECT_SAMPLES &&
            !mLoggedRejectedActors.contains(actor);
        if (!CanOccupy(MapClass::Instance.get(), unit, destination, &mParticipants,
            nullptr, nullptr, nullptr, nullptr, sample ? &diagnostic : nullptr))
        {
            if (sample)
            {
                mLoggedRejectedActors.insert(actor);
                TraceRejectedMove(actor, destination, CurrentCell(unit), mParticipants.contains(actor), diagnostic);
            }
            return false;
        }
        if (mDispatcher.HasPendingActor(commands::ClickedMissionProducer::AutoFormation,
            reinterpret_cast<std::uintptr_t>(unit), unit->UniqueID))
        {
            return existing != mMoves.end() && existing->second.mDestination == destination;
        }
        commands::ClickedMissionIntent intent;
        intent.Actor = CaptureIdentity(unit, Epoch());
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

    auto_formation::MoveState AutoFormationGameAdapter::ObserveMove(ActorId actor, Cell destination) const
    {
        using auto_formation::MoveState;
        if (!IsSessionActive()) { return MoveState::Invalid; }
        auto* const unit = ResolveActor(actor);
        if (!unit || unit->OnBridge != destination.mOnBridge) { return MoveState::Invalid; }
        if (CurrentCell(unit) == destination && IsStopped(unit)) { return MoveState::Arrived; }
        const auto found = mMoves.find(actor);
        if (found == mMoves.end()) { return MoveState::Waiting; }
        auto& pending = found->second;
        if (pending.mIdentity.Epoch != Epoch() || pending.mDestination != destination)
        {
            return MoveState::Invalid;
        }
        if (!pending.mHasIssued)
        {
            const bool queued = mDispatcher.HasPendingActor(commands::ClickedMissionProducer::AutoFormation,
                pending.mIdentity.Address, pending.mIdentity.UniqueId);
            return *auto_formation::ObservePendingMovePhase(false, queued, false, 0, 0);
        }
        auto* const expectedCell = FindCell(MapClass::Instance.get(), destination);
        const bool expectedOrder = expectedCell && unit->Destination == expectedCell &&
            (IsMoveMission(unit->CurrentMission) || IsMoveMission(unit->QueuedMission));
        if (expectedOrder)
        {
            pending.mHasObservedDestination = true;
            return auto_formation::ObserveIssuedGoalState(false, IsStopped(unit),
                IsMoveMission(unit->CurrentMission) || IsMoveMission(unit->QueuedMission));
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
        return auto_formation::ObserveIssuedGoalState(false, IsStopped(unit),
            IsMoveMission(unit->CurrentMission) || IsMoveMission(unit->QueuedMission));
    }

    bool AutoFormationGameAdapter::ValidateIntent(const commands::ClickedMissionIntent& intent) const
    {
        if (!IsSessionActive() || intent.Producer != commands::ClickedMissionProducer::AutoFormation ||
            intent.Mission != static_cast<std::int32_t>(Mission::Move) || !intent.DestinationCell ||
            intent.Target || intent.TargetCell || intent.Nearest || intent.Epoch != Epoch() ||
            intent.Actor.Epoch != intent.Epoch || intent.Actor.Kind != static_cast<std::uint32_t>(AbstractType::Unit))
        {
            return false;
        }
        const ActorId id = (static_cast<ActorId>(intent.Actor.Address) << 32) | intent.Actor.UniqueId;
        const auto found = mMoves.find(id);
        auto* const unit = ResolveActor(id);
        if (mPlanEpoch != Epoch() || !mParticipants.contains(id) || found == mMoves.end() ||
            !unit || found->second.mIdentity != intent.Actor ||
            found->second.mHasIssued)
        {
            return false;
        }
        const auto destination = found->second.mDestination;
        if (destination.mX != intent.DestinationCell->X || destination.mY != intent.DestinationCell->Y) { return false; }
        RejectDiagnostic diagnostic;
        const bool sample = mLoggedRejectedActors.size() < MAX_REJECT_SAMPLES &&
            !mLoggedRejectedActors.contains(id);
        if (!CanOccupy(MapClass::Instance.get(), unit, destination, &mParticipants,
            nullptr, nullptr, nullptr, nullptr, sample ? &diagnostic : nullptr))
        {
            if (sample)
            {
                mLoggedRejectedActors.insert(id);
                TraceRejectedMove(id, destination, CurrentCell(unit), mParticipants.contains(id), diagnostic);
            }
            return false;
        }
        return true;
    }

    void AutoFormationGameAdapter::AttemptIntent(const commands::ClickedMissionIntent& intent)
    {
        if (!ValidateIntent(intent)) { return; }
        const ActorId id = (static_cast<ActorId>(intent.Actor.Address) << 32) | intent.Actor.UniqueId;
        auto* const unit = ResolveActor(id);
        auto& pending = mMoves.at(id);
        auto* const cell = FindCell(MapClass::Instance.get(), pending.mDestination);
        if (CurrentCell(unit) == pending.mDestination && IsStopped(unit)) { return; }
        // 中央适配器也有 scope；这里使窄测试/直接处理器调用遵守同一重入边界。
        PluginOrderScope scope;
        (void)unit->ClickedMission(Mission::Move, nullptr, cell, nullptr);
        pending.mHasIssued = true;
        pending.mIssuedFrame = CurrentFrame();
    }

    void AutoFormationGameAdapter::CancelPending(ActorId actor)
    {
        (void)mDispatcher.CancelByProducerAndActor(commands::ClickedMissionProducer::AutoFormation,
            Identity(actor, Epoch()));
        mMoves.erase(actor);
        mParticipants.erase(actor);
    }

    void AutoFormationGameAdapter::Reset()
    {
        (void)mDispatcher.CancelByProducer(commands::ClickedMissionProducer::AutoFormation);
        mMoves.clear();
        mParticipants.clear();
        mLoggedRejectedActors.clear();
        mPlanEpoch = 0;
    }
}
