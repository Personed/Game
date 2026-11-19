//-----------------------------------------------------------------------------
// File: enf_scriptthread.h
//
// Desc: Main script context, holds and manage compiled image
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_SCRIPTTHREAD_H
#define ENF_SCRIPTTHREAD_H

#include "enf_config.h"

namespace enf
{
/** @defgroup EnforceScript Enforce script
@{*/

const uint	VMSTACK_SIZE			= 64;
const uint	VMSTACK_DATA_SIZE		= 1024;

class SerializeOutputAdapter;
class SerializeInputAdapter;
class ScriptContext;
class ScriptModule;
class Instance;
class Funct;
struct Stat;

//-----------------------------------------------------------------------
//! Virtual machine stack record
//-----------------------------------------------------------------------
struct VMStack
{
	//every
	const Stat*		m_psStat;		///<Current statement
	const Funct*	m_pfFunc;		///<Current function
	Instance*		m_peInstance;	///<Current instance

	//push/pop only
	int				m_iStored;		///<data stack offset
	int				m_iStoredOff;	///<data stack offset
	
#ifdef ENF_DEVELOPER
	TimeStamp		m_Time;			///<Profiler related
#endif
	void*				m_pStackPointer; ///<just for debug purposes

	uint GetDebugOffset() const;
	uint GetDebugLine() const;
};

//-----------------------------------------------------------------------
//! Script thread
//-----------------------------------------------------------------------
class PUBLIC_API ScriptThread
{
	friend class ScriptContext;
	friend class ScriptModule;

public:
	//!Status of thread
	enum Status
	{
		TF_NONWAITABLE = 1,		///<running, can't be stopped
		TF_RUNNING,					///<running, can be stopped
		TF_WTIME,					///<sleeping for time
		TF_WCOND,					///<sleeping for condition
		TF_KILL						///<killed
	};

	intptr_t			m_iID;					///<User ID
	Instance*		m_pOwner;
	ScriptThread*	m_pUserNext;			///<User chain
	ScriptThread**	m_pUserPrev;
	static intptr_t m_iDeserializeID;

	ENF_DECLARE_MMOBJECT(ScriptThread);

	void Serialize(SerializeOutputAdapter* );
	void Serialize(SerializeInputAdapter* );

	ENF_INLINE int GetStackPos() const
	{
		return m_iStackPos;
	}

	ENF_INLINE const VMStack* GetStack(int pos) const
	{
		return &m_sStack[pos];
	}
	void Kill();							///<Kill thread

	ENF_INLINE void SetState(Status st)
	{
		m_sState = st;
	}

	ENF_INLINE const ScriptThread* GetNext() const { return m_pNext; }
	ENF_INLINE ScriptThread* GetNext() { return m_pNext; }
	ENF_INLINE ScriptContext* GetScriptContext() const
	{
		return m_pContext;
	}
private:

	//!Current thread before starting this one. 'Stack' for recursive thread creation
	ScriptThread*	m_pPrevCurrent;
	ScriptThread*	m_pNext;		///<Next thread in chain
	int		m_iBStackPos;	///<beginning in stack

	float		m_fTime;			///<When sleeping for time
	Status	m_sState;		///<Status
	ScriptContext*	m_pContext;
	
	uint		m_iStackPos;						///<position
	int		m_iDataOff;							///<Data stack offset

	int		m_iData[VMSTACK_DATA_SIZE];	///<Data stack
	VMStack	m_sStack[VMSTACK_SIZE];			///<stack for this thread

	ENF_INLINE void PushData(uint data) ///<Push data on stack
	{
		enf_assert(m_iDataOff < VMSTACK_DATA_SIZE);
		m_iData[m_iDataOff++] = data;
	}

	ENF_INLINE uint PopData()				///<Pop data from stack
	{
		enf_assert(m_iDataOff > 0);
		return m_iData[--m_iDataOff];
	}

	//!Push new stack record
	VMStack* PushStack(Funct* fn, Instance* inst);

	//!Pop stack record
	VMStack* PopStack();
};


//@}

}
#endif //ENF_SCRIPTTHREAD_H
