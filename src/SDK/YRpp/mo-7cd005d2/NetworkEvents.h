#pragma once

#include <GeneralDefinitions.h>
#include <ObjectClass.h>
#include <Unsorted.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

class BuildingClass;
class AircraftClass;
class UnitClass;
class InfantryClass;
class CellClass;
class TeamClass;
class AnimClass;
class BulletClass;
class TerrainClass;
class TeamTypeClass;
class TriggerTypeClass;
class TechnoTypeClass;
class HouseClass;
class TriggerClass;
class FootClass;
class ObjectClass;
class TechnoClass;
class AbstractClass;
class TagClass;
class TagTypeClass;
class AbstractTypeClass;

#pragma pack(push, 8)
#pragma pack(1)
struct NetworkEvent {
	NetworkEvents Kind;
	byte Unused;
	byte HouseIndex;
	DWORD Timestamp;
	DWORD Checksum;
	WORD CommandCount;  //fuck,this is CommandOutStartCount
	byte Delay;
	byte ExtraData[0x61];

	NetworkEvent() {
		memset(this, 0, sizeof(*this));
	}

	NetworkEvent * FillEvent_ProduceAbandonSuspend(int PlayerNumber, NetworkEvents eventKind, AbstractType abstractId, int idx, int isNaval)
		{ JMP_THIS(0x4C6970); }

	NetworkEvent * FillEvent_SellCell(int dwUnk, NetworkEvents eventKind, CellStruct *Coords)
		{ JMP_THIS(0x4C6650); }

	NetworkEvent * FillEvent_Noopt(int PlayerNumber, NetworkEvents eventKind)
		{ JMP_THIS(0x4C66C0); }

	NetworkEvent * FillEvent_PlayerBased(int a2, NetworkEvents eventKind, int a4)
		{ JMP_THIS(0x4C6720); }

	NetworkEvent * FillEvent_Waypoints(int PlayerNumber, NetworkEvents eventKind, int a4, char a5, int a6, char a7)
		{ JMP_THIS(0x4C6780); }

	NetworkEvent * FillEvent_Animation(int PlayerNumber, NetworkEvents eventKind, int a4, int a5)
		{ JMP_THIS(0x4C6800); }

	NetworkEvent * FillEvent_Place(int a2, NetworkEvents eventKind, int a4, int a5, int a6, CellStruct *loc)
		{ JMP_THIS(0x4C6AE0); }

	NetworkEvent * FillEvent_SWPlace(int PlayerNumber, NetworkEvents eventKind, int swTypeIdx, CellStruct *loc)
		{ JMP_THIS(0x4C6B60); }
};

struct NetID {
	DWORD RTTI_ID;
	byte WhatAmI;

	NetID() {
		this->RTTI_ID = this->WhatAmI = 0;
	}

#define UNPACK(T, addr) \
	T ## Class* Unpack ## T () { JMP_THIS(addr); }

	UNPACK(Building, 0x6E7A80);
	UNPACK(Aircraft, 0x6E7B50);
	UNPACK(Unit, 0x6E79B0);
	UNPACK(Infantry, 0x6E78E0);

	UNPACK(Cell, 0x6E7C20);
	UNPACK(Team, 0x6E7810);
	UNPACK(Anim, 0x6E7740);
	UNPACK(Bullet, 0x6E7670);
	UNPACK(Terrain, 0x6E75A0);
	UNPACK(TeamType, 0x6E74D0);
	UNPACK(TriggerType, 0x6E7400);
	UNPACK(TechnoType, 0x6E7330);
	UNPACK(House, 0x6E7260);
	UNPACK(Trigger, 0x6E7190);
	UNPACK(Foot, 0x6E70C0);
	UNPACK(Object, 0x6E6FF0);
	UNPACK(Techno, 0x6E6F20);
	UNPACK(Abstract, 0x6E6E20);
	UNPACK(Tag, 0x6E6C80);
	UNPACK(TagType, 0x6E6D50);
	UNPACK(AbstractType, 0x6E6BB0);

	NetID * Pack(AbstractClass * toPack)
		{ JMP_THIS(0x6E6AB0); }

	NetID * Pack(CellStruct * toPack)
		{ JMP_THIS(0x6E6B20); }

	NetID * Pack(Point2D * toPack)
		{ JMP_THIS(0x6E6B70); }
};
#pragma pack(pop)

namespace NetworkEventDebug
{
	static constexpr int ShortFrameInfoHeaderSize = 0x0E;

	inline const char* GetEventName(NetworkEvents kind)
	{
		switch (kind)
		{
		case NetworkEvents::Empty: return "Empty";
		case NetworkEvents::PowerOn: return "PowerOn";
		case NetworkEvents::PowerOff: return "PowerOff";
		case NetworkEvents::Ally: return "Ally";
		case NetworkEvents::MegaMission: return "MegaMission";
		case NetworkEvents::MegaMissionF: return "MegaMissionF";
		case NetworkEvents::Idle: return "Idle";
		case NetworkEvents::Scatter: return "Scatter";
		case NetworkEvents::Destruct: return "Destruct";
		case NetworkEvents::Deploy: return "Deploy";
		case NetworkEvents::Detonate: return "Detonate";
		case NetworkEvents::Place: return "Place";
		case NetworkEvents::Options: return "Options";
		case NetworkEvents::GameSpeed: return "GameSpeed";
		case NetworkEvents::Produce: return "Produce";
		case NetworkEvents::Suspend: return "Suspend";
		case NetworkEvents::Abandon: return "Abandon";
		case NetworkEvents::Primary: return "Primary";
		case NetworkEvents::SpecialPlace: return "SpecialPlace";
		case NetworkEvents::Exit: return "Exit";
		case NetworkEvents::Animation: return "Animation";
		case NetworkEvents::Repair: return "Repair";
		case NetworkEvents::Sell: return "Sell";
		case NetworkEvents::SellCell: return "SellCell";
		case NetworkEvents::Special: return "Special";
		case NetworkEvents::FrameSync: return "FrameSync";
		case NetworkEvents::Message: return "Message";
		case NetworkEvents::ResponseTime: return "ResponseTime";
		case NetworkEvents::FrameInfo: return "FrameInfo";
		case NetworkEvents::SaveGame: return "SaveGame";
		case NetworkEvents::Archive: return "Archive";
		case NetworkEvents::AddPlayer: return "AddPlayer";
		case NetworkEvents::Timing: return "Timing";
		case NetworkEvents::ProcessTime: return "ProcessTime";
		case NetworkEvents::PageUser: return "PageUser";
		case NetworkEvents::RemovePlayer: return "RemovePlayer";
		case NetworkEvents::LatencyFudge: return "LatencyFudge";
		case NetworkEvents::MegaFrameInfo: return "MegaFrameInfo";
		case NetworkEvents::PacketTiming: return "PacketTiming";
		case NetworkEvents::AboutToExit: return "AboutToExit";
		case NetworkEvents::FallbackHost: return "FallbackHost";
		case NetworkEvents::AddressChange: return "AddressChange";
		case NetworkEvents::PlanConnect: return "PlanConnect";
		case NetworkEvents::PlanCommit: return "PlanCommit";
		case NetworkEvents::PlanNodeDelete: return "PlanNodeDelete";
		case NetworkEvents::AllCheer: return "AllCheer";
		case NetworkEvents::AbandonAll: return "AbandonAll";
		default: return "Unknown";
		}
	}

	inline const char* GetAbstractTypeName(byte whatAmI)
	{
		const char* name = AbstractClass::GetClassName(static_cast<AbstractType>(whatAmI));
		return name ? name : "Unknown";
	}

	inline const char* GetMissionName(byte mission)
	{
		switch (static_cast<Mission>(mission))
		{
		case Mission::Sleep: return "Sleep";
		case Mission::Attack: return "Attack";
		case Mission::Move: return "Move";
		case Mission::QMove: return "QMove";
		case Mission::Retreat: return "Retreat";
		case Mission::Guard: return "Guard";
		case Mission::Sticky: return "Sticky";
		case Mission::Enter: return "Enter";
		case Mission::Capture: return "Capture";
		case Mission::Eaten: return "Eaten";
		case Mission::Harvest: return "Harvest";
		case Mission::Area_Guard: return "Area_Guard";
		case Mission::Return: return "Return";
		case Mission::Stop: return "Stop";
		case Mission::Ambush: return "Ambush";
		case Mission::Hunt: return "Hunt";
		case Mission::Unload: return "Unload";
		case Mission::Sabotage: return "Sabotage";
		case Mission::Construction: return "Construction";
		case Mission::Selling: return "Selling";
		case Mission::Repair: return "Repair";
		case Mission::Rescue: return "Rescue";
		case Mission::Missile: return "Missile";
		case Mission::Harmless: return "Harmless";
		case Mission::Open: return "Open";
		case Mission::Patrol: return "Patrol";
		case Mission::ParadropApproach: return "ParadropApproach";
		case Mission::ParadropOverfly: return "ParadropOverfly";
		case Mission::Wait: return "Wait";
		case Mission::AttackMove: return "AttackMove";
		case Mission::SpyplaneApproach: return "SpyplaneApproach";
		case Mission::SpyplaneOverfly: return "SpyplaneOverfly";
		default: return "Unknown";
		}
	}

	inline int GetCompressedPayloadSize(NetworkEvents kind)
	{
		switch (kind)
		{
		case NetworkEvents::Empty:
		case NetworkEvents::Destruct:
		case NetworkEvents::Options:
		case NetworkEvents::Exit:
		case NetworkEvents::FrameSync:
		case NetworkEvents::Message:
		case NetworkEvents::SaveGame:
		case NetworkEvents::PageUser:
		case NetworkEvents::AboutToExit:
		case NetworkEvents::PlanCommit:
			return 0;
		case NetworkEvents::ResponseTime:
			return 1;
		case NetworkEvents::ProcessTime:
			return 2;
		case NetworkEvents::Ally:
		case NetworkEvents::GameSpeed:
		case NetworkEvents::SellCell:
		case NetworkEvents::Special:
		case NetworkEvents::AddPlayer:
		case NetworkEvents::RemovePlayer:
		case NetworkEvents::LatencyFudge:
		case NetworkEvents::FallbackHost:
		case NetworkEvents::AllCheer:
			return 4;
		case NetworkEvents::PowerOn:
		case NetworkEvents::PowerOff:
		case NetworkEvents::Idle:
		case NetworkEvents::Scatter:
		case NetworkEvents::Deploy:
		case NetworkEvents::Detonate:
		case NetworkEvents::Primary:
		case NetworkEvents::Repair:
		case NetworkEvents::Sell:
		case NetworkEvents::Timing:
		case NetworkEvents::AddressChange:
		case NetworkEvents::PlanNodeDelete:
			return 5;
		case NetworkEvents::FrameInfo:
			return 7;
		case NetworkEvents::SpecialPlace:
			return 8;
		case NetworkEvents::Archive:
		case NetworkEvents::PlanConnect:
			return 10;
		case NetworkEvents::Produce:
		case NetworkEvents::Suspend:
		case NetworkEvents::Abandon:
		case NetworkEvents::AbandonAll:
			return 12;
		case NetworkEvents::Place:
		case NetworkEvents::Animation:
			return 16;
		case NetworkEvents::MegaMission:
			return 23;
		case NetworkEvents::MegaMissionF:
			return 24;
		case NetworkEvents::PacketTiming:
			return 64;
		case NetworkEvents::MegaFrameInfo:
			return 104;
		default:
			return -1;
		}
	}

	inline void AppendFormat(std::string& out, const char* format, ...)
	{
		char buffer[1024]{};
		va_list args;
		va_start(args, format);
		const int written = vsnprintf(buffer, sizeof(buffer), format, args);
		va_end(args);

		if (written > 0)
		{
			out.append(buffer);
		}
	}

	inline std::string WideToUtf8(const wchar_t* text)
	{
		if (!text || !text[0])
		{
			return {};
		}

		const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);

		if (size <= 1)
		{
			return {};
		}

		std::string result(static_cast<std::size_t>(size), '\0');
		WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
		result.resize(static_cast<std::size_t>(size - 1));
		return result;
	}

	inline bool TryRead(const BYTE* payload, int payloadSize, int offset, void* out, int size)
	{
		if (!payload || !out || offset < 0 || size < 0 || offset + size > payloadSize)
		{
			return false;
		}

		std::memcpy(out, payload + offset, static_cast<std::size_t>(size));
		return true;
	}

	inline bool TryReadNetID(const BYTE* payload, int payloadSize, int offset, NetID& out)
	{
		return TryRead(payload, payloadSize, offset, &out, sizeof(out));
	}

	inline ObjectClass* TryUnpackObject(NetID& id)
	{
		ObjectClass* object = nullptr;

		__try
		{
			switch (static_cast<AbstractType>(id.WhatAmI))
			{
			case AbstractType::Building:
				object = reinterpret_cast<ObjectClass*>(id.UnpackBuilding());
				break;
			case AbstractType::Aircraft:
				object = reinterpret_cast<ObjectClass*>(id.UnpackAircraft());
				break;
			case AbstractType::Unit:
				object = reinterpret_cast<ObjectClass*>(id.UnpackUnit());
				break;
			case AbstractType::Infantry:
				object = reinterpret_cast<ObjectClass*>(id.UnpackInfantry());
				break;
			default:
				break;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			object = nullptr;
		}

		return object;
	}

	inline const wchar_t* TryGetObjectUIName(ObjectClass* object)
	{
		const wchar_t* uiName = nullptr;

		__try
		{
			uiName = object ? object->GetUIName() : nullptr;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			uiName = nullptr;
		}

		return uiName;
	}

	inline void AppendNetID(std::string& out, NetID id)
	{
		AppendFormat(out, "ObjectRuntimeId = %lu ; ObjectType = %s",
			static_cast<unsigned long>(id.RTTI_ID), GetAbstractTypeName(id.WhatAmI));

		if (!id.RTTI_ID)
		{
			return;
		}

		ObjectClass* object = TryUnpackObject(id);

		if (!object)
		{
			return;
		}

		const std::string name = WideToUtf8(TryGetObjectUIName(object));

		if (!name.empty())
		{
			AppendFormat(out, " ; ObjectName = %s", name.c_str());
		}
	}

	inline void AppendCellPayload(std::string& out, const BYTE* payload, int payloadSize, int offset = 0)
	{
		short x = 0;
		short y = 0;

		if (TryRead(payload, payloadSize, offset, &x, 2) &&
			TryRead(payload, payloadSize, offset + 2, &y, 2))
		{
			AppendFormat(out, "CellX = %d ; CellY = %d", static_cast<int>(x), static_cast<int>(y));
		}
	}

	inline void AppendTargetPayload(std::string& out, const BYTE* payload, int payloadSize)
	{
		NetID id{};

		if (TryReadNetID(payload, payloadSize, 0, id))
		{
			AppendNetID(out, id);
		}
	}

	inline void AppendProductionPayload(std::string& out, const BYTE* payload, int payloadSize)
	{
		DWORD type = 0;
		int heapId = 0;
		int isNaval = 0;

		if (TryRead(payload, payloadSize, 0, &type, 4) &&
			TryRead(payload, payloadSize, 4, &heapId, 4) &&
			TryRead(payload, payloadSize, 8, &isNaval, 4))
		{
			AppendFormat(out, "AbstractType = %s ; HeapID = %d ; IsNaval = %d",
				GetAbstractTypeName(static_cast<byte>(type)), heapId, isNaval);
		}
	}

	inline void AppendMegaMissionPayload(std::string& out, const BYTE* payload, int payloadSize)
	{
		NetID actor{};
		NetID target{};
		NetID destination{};
		byte mission = 0;

		if (TryReadNetID(payload, payloadSize, 0, actor))
		{
			out += "Actor { ";
			AppendNetID(out, actor);
			out += " }";
		}

		if (TryRead(payload, payloadSize, 5, &mission, 1))
		{
			AppendFormat(out, " ; Mission = %s(%u)", GetMissionName(mission), static_cast<unsigned int>(mission));
		}

		if (TryReadNetID(payload, payloadSize, 6, target))
		{
			out += " ; Target { ";
			AppendNetID(out, target);
			out += " }";
		}

		if (TryReadNetID(payload, payloadSize, 11, destination))
		{
			out += " ; Destination { ";
			AppendNetID(out, destination);
			out += " }";
		}
	}

	inline void AppendPayloadDetails(std::string& out, NetworkEvents kind, const BYTE* payload, int payloadSize)
	{
		switch (kind)
		{
		case NetworkEvents::PowerOn:
		case NetworkEvents::PowerOff:
		case NetworkEvents::Idle:
		case NetworkEvents::Scatter:
		case NetworkEvents::Deploy:
		case NetworkEvents::Detonate:
		case NetworkEvents::Primary:
		case NetworkEvents::Repair:
		case NetworkEvents::Sell:
		case NetworkEvents::PlanNodeDelete:
			AppendTargetPayload(out, payload, payloadSize);
			break;

		case NetworkEvents::Ally:
		case NetworkEvents::GameSpeed:
		case NetworkEvents::RemovePlayer:
		case NetworkEvents::FallbackHost:
		case NetworkEvents::LatencyFudge:
		case NetworkEvents::AllCheer:
		case NetworkEvents::Special:
		{
			DWORD value = 0;
			if (TryRead(payload, payloadSize, 0, &value, 4))
			{
				if (kind == NetworkEvents::Ally)
				{
					AppendFormat(out, "HouseID = %lu", static_cast<unsigned long>(value));
				}
				else if (kind == NetworkEvents::GameSpeed)
				{
					AppendFormat(out, "NewSpeed = %lu", static_cast<unsigned long>(value));
				}
				else
				{
					AppendFormat(out, "Value = %lu", static_cast<unsigned long>(value));
				}
			}
			break;
		}

		case NetworkEvents::MegaMission:
		case NetworkEvents::MegaMissionF:
			AppendMegaMissionPayload(out, payload, payloadSize);
			break;

		case NetworkEvents::Produce:
		case NetworkEvents::Suspend:
		case NetworkEvents::Abandon:
		case NetworkEvents::AbandonAll:
			AppendProductionPayload(out, payload, payloadSize);
			break;

		case NetworkEvents::Place:
			AppendProductionPayload(out, payload, payloadSize);
			out += " ; ";
			AppendCellPayload(out, payload, payloadSize, 12);
			break;

		case NetworkEvents::SpecialPlace:
		{
			DWORD superWeaponId = 0;
			if (TryRead(payload, payloadSize, 0, &superWeaponId, 4))
			{
				AppendFormat(out, "SuperWeaponID = %lu ; ", static_cast<unsigned long>(superWeaponId));
				AppendCellPayload(out, payload, payloadSize, 4);
			}
			break;
		}

		case NetworkEvents::Animation:
		{
			DWORD animId = 0;
			DWORD houseId = 0;
			int x = 0;
			int y = 0;
			if (TryRead(payload, payloadSize, 0, &animId, 4) &&
				TryRead(payload, payloadSize, 4, &houseId, 4) &&
				TryRead(payload, payloadSize, 8, &x, 4) &&
				TryRead(payload, payloadSize, 12, &y, 4))
			{
				AppendFormat(out, "AnimID = %lu ; HouseID = %lu ; X = %d ; Y = %d",
					static_cast<unsigned long>(animId), static_cast<unsigned long>(houseId), x, y);
			}
			break;
		}

		case NetworkEvents::SellCell:
			AppendCellPayload(out, payload, payloadSize);
			break;

		case NetworkEvents::ResponseTime:
		{
			byte delay = 0;
			if (TryRead(payload, payloadSize, 0, &delay, 1))
			{
				AppendFormat(out, "ResponseDelay = %u", static_cast<unsigned int>(delay));
			}
			break;
		}

		case NetworkEvents::Archive:
		case NetworkEvents::PlanConnect:
		{
			NetID first{};
			NetID second{};
			if (TryReadNetID(payload, payloadSize, 0, first))
			{
				out += "First { ";
				AppendNetID(out, first);
				out += " }";
			}
			if (TryReadNetID(payload, payloadSize, 5, second))
			{
				out += " ; Second { ";
				AppendNetID(out, second);
				out += " }";
			}
			break;
		}

		case NetworkEvents::Timing:
		{
			WORD targetFPS = 0;
			WORD frameDelay = 0;
			byte frameSendRate = 0;
			if (TryRead(payload, payloadSize, 0, &targetFPS, 2) &&
				TryRead(payload, payloadSize, 2, &frameDelay, 2) &&
				TryRead(payload, payloadSize, 4, &frameSendRate, 1))
			{
				AppendFormat(out, "TargetFPS = %u ; FrameDelay = %u ; FrameSendRate = %u",
					static_cast<unsigned int>(targetFPS),
					static_cast<unsigned int>(frameDelay),
					static_cast<unsigned int>(frameSendRate));
			}
			break;
		}

		case NetworkEvents::ProcessTime:
		{
			WORD processTime = 0;
			if (TryRead(payload, payloadSize, 0, &processTime, 2))
			{
				AppendFormat(out, "ProcessTime = %u", static_cast<unsigned int>(processTime));
			}
			break;
		}

		case NetworkEvents::AddressChange:
		{
			byte playerIndex = 0;
			DWORD rawAddress = 0;
			if (TryRead(payload, payloadSize, 0, &playerIndex, 1) &&
				TryRead(payload, payloadSize, 1, &rawAddress, 4))
			{
				AppendFormat(out, "PlayerIndex = %u ; RawAddress = %lu",
					static_cast<unsigned int>(playerIndex), static_cast<unsigned long>(rawAddress));
			}
			break;
		}

		case NetworkEvents::MegaFrameInfo:
			out += "MegaFrameInfoRecords = blob";
			break;

		case NetworkEvents::PacketTiming:
			out += "PacketTimingRecords = blob";
			break;

		default:
			if (payloadSize > 0)
			{
				AppendFormat(out, "PayloadSize = %d ; Payload = blob", payloadSize);
			}
			break;
		}
	}

	inline std::string FrameInfoPacketToString(
		const NetworkEvent* frameInfo,
		int frameInfoPacketSize,
		bool skipEmptyPackets = false)
	{
		std::string out;

		if (!frameInfo)
		{
			return "FrameInfoPacket: <null> ;\n";
		}

		AppendFormat(out,
			"FrameInfoPacket: HouseIndex = %u ; Timestamp = %lu ; Checksum = %lu ; CommandCount = %u ; Delay = %u ;\nIncluded Packet:\n",
			static_cast<unsigned int>(frameInfo->HouseIndex),
			static_cast<unsigned long>(frameInfo->Timestamp),
			static_cast<unsigned long>(frameInfo->Checksum),
			static_cast<unsigned int>(frameInfo->CommandCount),
			static_cast<unsigned int>(frameInfo->Delay));

		if (frameInfo->Kind != NetworkEvents::FrameInfo)
		{
			out += "<not a FrameInfo packet>\n";
			return out;
		}

		if (frameInfoPacketSize < ShortFrameInfoHeaderSize)
		{
			out += "<truncated FrameInfo header>\n";
			return out;
		}

		const auto* bytes = reinterpret_cast<const BYTE*>(frameInfo);
		int offset = ShortFrameInfoHeaderSize;
		int index = 1;

		while (offset < frameInfoPacketSize)
		{
			const byte rawKind = bytes[offset++];
			const auto kind = static_cast<NetworkEvents>(rawKind);
			const int payloadSize = GetCompressedPayloadSize(kind);

			if (payloadSize < 0)
			{
				AppendFormat(out, "%d : Unknown Packet : Kind = 0x%02X ;\n", index, static_cast<unsigned int>(rawKind));
				break;
			}

			if (skipEmptyPackets && kind == NetworkEvents::Empty)
			{
				offset += payloadSize;
				continue;
			}

			if (kind == NetworkEvents::MegaMission)
			{
				if (offset >= frameInfoPacketSize)
				{
					AppendFormat(out, "%d : MegaMission Packet : <missing count> ;\n", index);
					break;
				}

				const byte count = bytes[offset++];
				const int totalPayloadSize = 23 + (static_cast<int>(count) - 1) * 5;

				if (count == 0 || offset + totalPayloadSize > frameInfoPacketSize)
				{
					AppendFormat(out, "%d : MegaMission Packet : Count = %u ; <truncated> ;\n",
						index, static_cast<unsigned int>(count));
					break;
				}

				AppendFormat(out, "%d : MegaMission Packet : HouseIndex = %u ; TimeStamp = %lu ; Count = %u ; ",
					index,
					static_cast<unsigned int>(frameInfo->HouseIndex),
					static_cast<unsigned long>(frameInfo->Timestamp),
					static_cast<unsigned int>(count));

				AppendMegaMissionPayload(out, bytes + offset, 23);

				for (int i = 1; i < static_cast<int>(count); ++i)
				{
					NetID actor{};
					if (TryReadNetID(bytes + offset + 23, totalPayloadSize - 23, (i - 1) * 5, actor))
					{
						AppendFormat(out, " ; Actor%d { ", i + 1);
						AppendNetID(out, actor);
						out += " }";
					}
				}

				out += " ;\n";
				offset += totalPayloadSize;
				++index;
				continue;
			}

			if (kind == NetworkEvents::AddPlayer)
			{
				if (offset + 4 > frameInfoPacketSize)
				{
					AppendFormat(out, "%d : AddPlayer Packet : <truncated length> ;\n", index);
					break;
				}

				DWORD len = 0;
				std::memcpy(&len, bytes + offset, 4);
				const int dynamicSize = 4 + static_cast<int>(len);

				if (len > 0x10000 || offset + dynamicSize > frameInfoPacketSize)
				{
					AppendFormat(out, "%d : AddPlayer Packet : Length = %lu ; <truncated> ;\n",
						index, static_cast<unsigned long>(len));
					break;
				}

				AppendFormat(out, "%d : AddPlayer Packet : HouseIndex = %u ; TimeStamp = %lu ; Length = %lu ; Data = blob ;\n",
					index,
					static_cast<unsigned int>(frameInfo->HouseIndex),
					static_cast<unsigned long>(frameInfo->Timestamp),
					static_cast<unsigned long>(len));

				offset += dynamicSize;
				++index;
				continue;
			}

			if (offset + payloadSize > frameInfoPacketSize)
			{
				AppendFormat(out,
					"%d : %s Packet : HouseIndex = %u ; TimeStamp = %lu ; <truncated payload, need %d bytes> ;\n",
					index,
					GetEventName(kind),
					static_cast<unsigned int>(frameInfo->HouseIndex),
					static_cast<unsigned long>(frameInfo->Timestamp),
					payloadSize);
				break;
			}

			AppendFormat(out, "%d : %s Packet : HouseIndex = %u ; TimeStamp = %lu",
				index,
				GetEventName(kind),
				static_cast<unsigned int>(frameInfo->HouseIndex),
				static_cast<unsigned long>(frameInfo->Timestamp));

			if (payloadSize > 0)
			{
				out += " ; ";
				AppendPayloadDetails(out, kind, bytes + offset, payloadSize);
			}

			out += " ;\n";
			offset += payloadSize;
			++index;
		}

		if (index == 1)
		{
			out += "<empty>\n";
		}

		return out;
	}
}
