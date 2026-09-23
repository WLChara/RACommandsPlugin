#pragma once

#include <GenericList.h>
#include <ArrayClasses.h>

#include <Helpers/CompileTime.h>

struct MixHeaderData
{
	DWORD ID;
	DWORD Offset;
	DWORD Size;
};

class MixFileClass : public Node<MixFileClass*>
{
	struct GenericMixFiles
	{
		MixFileClass* RA2MD;
		MixFileClass* RA2;
		MixFileClass* LANGUAGE;
		MixFileClass* LANGMD;
		MixFileClass* THEATER_TEMPERAT;
		MixFileClass* THEATER_TEMPERATMD;
		MixFileClass* THEATER_TEM;
		MixFileClass* GENERIC;
		MixFileClass* GENERMD;
		MixFileClass* THEATER_ISOTEMP;
		MixFileClass* THEATER_ISOTEM;
		MixFileClass* ISOGEN;
		MixFileClass* ISOGENMD;
		MixFileClass* MOVIES02D;
		MixFileClass* UNKNOWN_1;
		MixFileClass* MAIN;
		MixFileClass* CONQMD;
		MixFileClass* CONQUER;
		MixFileClass* CAMEOMD;
		MixFileClass* CAMEO;
		MixFileClass* CACHEMD;
		MixFileClass* CACHE;
		MixFileClass* LOCALMD;
		MixFileClass* LOCAL;
		MixFileClass* NTRLMD;
		MixFileClass* NEUTRAL;
		MixFileClass* MAPSMD02D;
		MixFileClass* MAPS02D;
		MixFileClass* UNKNOWN_2;
		MixFileClass* UNKNOWN_3;
		MixFileClass* SIDEC02DMD;
		MixFileClass* SIDEC02D;
	};

public:
	static constexpr reference<List<MixFileClass*>, 0xABEFD8u> const MIXes{};

	static constexpr reference<DynamicVectorClass<MixFileClass*>, 0x884D90u> const Array{};
	static constexpr reference<DynamicVectorClass<MixFileClass*>, 0x884DC0u> const Array_Alt{};
	static constexpr reference<DynamicVectorClass<MixFileClass*>, 0x884DA8u> const Maps{};
	static constexpr reference<DynamicVectorClass<MixFileClass*>, 0x884DE0u> const Movies{};

	static constexpr reference<MixFileClass, 0x884DD8u> const MULTIMD{};
	static constexpr reference<MixFileClass, 0x884DDCu> const MULTI{};

	static constexpr reference<GenericMixFiles, 0x884DF8u> const Generics{};

	static void Bootstrap()
		{ JMP_THIS(0x5301A0); }

	virtual ~MixFileClass() RX;

	MixFileClass(const char* pFileName)
		: Node<MixFileClass*>(noinit_t())
	{
		PUSH_IMM(0x886980);
		PUSH_VAR32(pFileName);
		THISCALL(0x5B3C20);
	}

	static void* Retrieve(const char* pFileName, bool forceShapeCache = false)
		{ JMP_STD(0x5B40B0); }

	static bool __fastcall Offset(const char* pFileName, void*& pData,
		MixFileClass*& pMixFile, int& offset, int& size)
		{ JMP_STD(0x5B4430); }

	static void DestroyCache()
		{ CALL(0x5B4310); }

protected:
	/*PROPERTY(MixFileClass*, Next);
	MixFileClass* Prev;*/

public:

	const char* FileName;
	union
	{
		bool IsDigest;
		bool Blowfish;
	};
	union
	{
		bool IsEncrypted;
		bool Encryption;
	};
	int CountFiles;
	union
	{
		int DataSize;
		int FileSize;
	};
	int FileStartOffset;
	MixHeaderData* Headers;
	union
	{
		void* Data;
		int field_24;
	};
};

static_assert(sizeof(MixFileClass) == 0x28, "Invalid size for MixFileClass.");
