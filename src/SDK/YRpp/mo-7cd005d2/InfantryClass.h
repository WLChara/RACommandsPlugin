/*
	Infantry
*/

#pragma once

#include <FootClass.h>
#include <InfantryTypeClass.h>

class NOVTABLE InfantryClass : public FootClass
{
public:
	static const AbstractType AbsID = AbstractType::Infantry;

	//Static
	static constexpr constant_ptr<DynamicVectorClass<InfantryClass*>, 0xA83DE8u> const Array{};

	//IPersist
	virtual HRESULT __stdcall GetClassID(CLSID* pClassID) R0;

	//Destructor
	virtual ~InfantryClass() RX;

	//AbstractClass
	virtual AbstractType WhatAmI() const RT(AbstractType);
	virtual int	Size() const R0;

	//InfantryClass
	virtual bool IsDeployed() const R0;
	virtual bool PlayAnim(Sequence index, bool force = false, bool randomStartFrame = false) R0;

	//Constructor
	InfantryClass(InfantryTypeClass* pType, HouseClass* pOwner) noexcept
		: InfantryClass(noinit_t())
	{ JMP_THIS(0x517A50); }


// ******************* START OPERATION FUNCTIONS ******************* //

	//operation deploy
	//部署一次
	bool Deploy() {
		return this->ClickedEvent(NetworkEvents::Deploy);
	}

	//operation undeploy
	//如果已经部署，撤销部署
	bool Undeploy() {
		if (this->IsDeployed()) {
			return this->Deploy();
		}
		return false;
	}

	//operation deploy and undeploy
	//部署一次然后撤销部署，别问为什么，纯好玩
	bool DeployAndUndeploy() {
		this->Deploy();
		this->Undeploy();
		return true;
	}

	//挺好玩的，点一下部署然后点一下散开，美国大兵舞团，lol
	bool DeployDance() {
		this->Deploy();
		this->Scatter();
		this->Undeploy();
		return true;
	}


// ******************* END OPERATION FUNCTIONS ******************* //

protected:
	explicit __forceinline InfantryClass(noinit_t) noexcept
		: FootClass(noinit_t())
	{ }

	//===========================================================================
	//===== Properties ==========================================================
	//===========================================================================

public:

	InfantryTypeClass* Type;
	Sequence SequenceAnim; //which is currently playing
	TimerStruct unknown_Timer_6C8;
	DWORD          PanicDurationLeft; // set in ReceiveDamage on panicky units
	bool           PermanentBerzerk; // set by script action, not cleared anywhere
	bool           Technician;
	bool           unknown_bool_6DA;
	bool           Crawling;
	bool           unknown_bool_6DC;
	bool           unknown_bool_6DD;
	DWORD          unknown_6E0;
	bool           ShouldDeploy;
	int            unknown_int_6E8;
	PROTECTED_PROPERTY(DWORD, unused_6EC); //??
};
