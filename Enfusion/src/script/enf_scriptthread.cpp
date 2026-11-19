//-----------------------------------------------------------------------------
// File: enf_scriptthread.cpp
//
// Desc: Script thread class
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "common/enf_types.h"
#include "common/enf_staticbuffer.h"
#include "script/enf_scriptmain.h"
#include "script/enf_scriptcontext.h"
#include "script/enf_scriptinternal.h"
#include "script/enf_scriptthread.h"
#include "common/enf_parser.h"
#include "system/enf_profiler.h"
#include "common/enf_serialize.h"

namespace enf
{
intptr_t ScriptThread::m_iDeserializeID = 0;

//-----------------------------------------------------------------------
void SerializeStackObjPtr(ScriptContext* ctx, SerializeOutputAdapter* adapter, Instance* inst)
{
	if(inst == NULL)
	{
		adapter->WriteReference(0, NULL);
		return;
	}

	Variable var;

	if(inst != NULL)
		var.SetType(inst->GetClass());
	else
		var.SetType(ctx->m_vtClassType);

	uint type;
	if(ctx->DoSerializeVariable(&var, type))
		adapter->WriteReference(type, inst);
	else
		adapter->WriteReference(0, NULL);
}

//-----------------------------------------------------------------------
VMStack* ScriptThread::PushStack(Funct* fn, Instance* inst)
{
	uint stackPos = m_iStackPos + 1;
	if(stackPos == VMSTACK_SIZE)
		return NULL;
	
	m_iStackPos = stackPos;
	enf_assert(fn->FirstStat);
	m_sStack[stackPos].m_pfFunc = fn;
	m_sStack[stackPos].m_psStat = fn->FirstStat;
	m_sStack[stackPos].m_peInstance = inst;
	m_sStack[stackPos].m_iStored = 0;
	m_sStack[stackPos].m_iStoredOff = m_iDataOff;
	return &m_sStack[stackPos];
}

//-----------------------------------------------------------------------
void ScriptThread::Serialize(SerializeOutputAdapter* adapter)
{
	adapter->WriteUInt16((ushort)m_sState);
	adapter->WriteFloat(m_fTime);
	adapter->WriteUInt16(m_iStackPos);
	adapter->WriteUInt16(m_iBStackPos);
	adapter->WriteReference(0, m_pOwner);
	//write callstack
	for(uint s = 1; s <= m_iStackPos; s++)
	{
		SerializeStackObjPtr(m_pContext, adapter, m_sStack[s].m_peInstance);
		adapter->WriteString(m_sStack[s].m_pfFunc->GetName());
		adapter->WriteInt16(m_sStack[s].m_iStored);
		adapter->WriteInt16(m_sStack[s].m_iStoredOff);
		adapter->WriteUInt32((uint)((const char*)m_sStack[s].m_psStat - (const char*)m_sStack[s].m_pfFunc->FirstStat));
	}

	adapter->WriteUInt16(m_iDataOff);
	//write datastack
	for(int d = 0; d < m_iDataOff; )
	{
		uint enc = m_iData[d++];
		VType vt = (VType)(enc >> 16);
		uint count = enc & 0xffff;

		adapter->WriteUInt16(vt);
		adapter->WriteUInt16(count);

		for(uint c = 0; c < count; c++)
		{
			switch(vt)
			{
			case VARTYPE_INT:
				adapter->WriteUInt32(m_iData[d++]);
				break;

			case VARTYPE_FLOAT:
				adapter->WriteFloat(*(float*)&m_iData[d++]);
				break;

			case VARTYPE_CLASS:
				adapter->WriteReference(0, (void*)m_iData[d++]);
				break;

			case VARTYPE_CLASSREF:
				{
					Variable* var = (Variable*)m_iData[d++];
					if(var)
						adapter->WriteString(var->GetName());
					else
						adapter->WriteString(NULL);
				}
				break;

			case VARTYPE_STRING:
				adapter->WriteString((const char*)m_iData[d++]);
				break;

			case VARTYPE_VECTOR:
				adapter->WriteFloat(*(const float*)&m_iData[d++]);
				adapter->WriteFloat(*(const float*)&m_iData[d++]);
				adapter->WriteFloat(*(const float*)&m_iData[d++]);
				break;
			}
		}
	}
}

//-----------------------------------------------------------------------
void ScriptThread::Serialize(SerializeInputAdapter* adapter)
{
	m_iID = m_iDeserializeID;
	m_sState = (ScriptThread::Status)adapter->ReadUInt16();
	m_fTime = adapter->ReadFloat();
	m_iStackPos = adapter->ReadUInt16();
	m_iBStackPos = adapter->ReadUInt16();
	m_pOwner = (enf::Instance*)adapter->ReadReference(0);

	//read callstack
	for(uint s = 1; s <= m_iStackPos; s++)
	{ 
		m_sStack[s].m_peInstance = (Instance*)adapter->ReadReference(0);
		const char* funcName = adapter->ReadString();
		const Funct* func;
		if(m_sStack[s].m_peInstance)
			func = m_sStack[s].m_peInstance->GetClass()->GetMethod(funcName);
		else
			func = ScriptContext::m_pDeserializeModule->GetFunction(funcName);
		m_sStack[s].m_iStored = adapter->ReadInt16();
		m_sStack[s].m_iStoredOff = adapter->ReadInt16();
		m_sStack[s].m_psStat = (Stat*)((char*)func->FirstStat + adapter->ReadUInt32());
		m_sStack[s].m_pfFunc = func;
	}

	//check integrity of callstack:
	for(uint s = 1; s <= m_iStackPos; s++)
	{
		const Funct* thisFunc = m_sStack[s].m_pfFunc;
		const Stat* storedSt = m_sStack[s].m_psStat;
		const Stat* st = thisFunc->FirstStat;
		const Stat* endstat = (Stat*)((char*)st + thisFunc->Length);
		const Funct* parentFunc = (s != m_iStackPos) ? m_sStack[s + 1].m_pfFunc : NULL;

		int bestOff = INT_MAX;
		const Stat* bestSt = NULL;
		const Stat* prevStat = NULL;
		while(st < endstat)
		{
			OpCode op = st->GetOpcode();

			if(st == storedSt)
			{
				OpCode prevOp = prevStat->GetOpcode();
				st=st;
			}
			prevStat = st;
			if(parentFunc)
			{
				//we expect function call to the function above us
				if(op == OP_CALL)
				{
					ScriptModule* cmod = thisFunc->Module;
					const ScriptModule::CallRef& ref = cmod->m_FunctRefs[st->GetOffset() & 0x1fff];
					
					Funct* fn = NULL;

					if(ref.pInstance)
					{
						Instance* inst;
						const Class* cl = NULL;
						if((inst = *ref.pInstance) != NULL)
						{
							if(cl == NULL)
								cl = inst->GetClass();

							enf_assert(cl);

							fn = cl->GetMethod(ref.FunctionIndex - 1);
						}
					}
					else
					{
						fn = ref.Module->GetFunction(ref.FunctionIndex - 1);
					}
					if(fn == parentFunc)
					{
						int diff;
						if(st < storedSt)
							diff = (intptr_t)storedSt - (intptr_t)st;
						else
							diff = (intptr_t)st - (intptr_t)storedSt;

						if(diff < bestOff)
						{
							bestOff = diff;
							bestSt = st;
						}
						if(diff == 0)
						{
							//correct position
							break;
						}
					}
				}
			}
			else
			{
				if(op == OP_WAIT)
				{
					const Stat* testSt = (Stat*)((char*)st + st->GetSizeof());
					int diff;
					if(testSt < storedSt)
						diff = (intptr_t)storedSt - (intptr_t)testSt;
					else
						diff = (intptr_t)testSt - (intptr_t)st;

					if(diff < bestOff)
					{
						bestOff = diff;
						bestSt = st;
					}
					if(diff == 0)
					{
						//correct position
						break;
					}
				}
			}
			st = (const Stat*)((char *)st + st->GetSizeof());
		}

		if(bestOff != INT_MAX)
		{
			m_sStack[s].m_psStat = bestSt;
		}
	}

	m_iDataOff = adapter->ReadUInt16();

	//Instance* inst = m_sStack[m_iStackPos].m_peInstance;
	//LogF(LL_DEBUG, "Deserialize thread instance %s func %s", inst ? inst->GetClass()->GetName() : "null", m_sStack[m_iStackPos].m_pfFunc->GetName());

	//write datastack
	for(int d = 0; d < m_iDataOff; )
	{
		VType vt = (VType)adapter->ReadUInt16();
		uint count = adapter->ReadUInt16();

		m_iData[d++] = (vt << 16) | count;

		for(uint c = 0; c < count; c++)
		{
			switch(vt)
			{
			case VARTYPE_INT:
				m_iData[d++] = adapter->ReadUInt32();
				break;

			case VARTYPE_FLOAT:
				*((float*)&m_iData[d++]) = adapter->ReadFloat();
				break;

			case VARTYPE_CLASS:
				m_iData[d++] = (uint)adapter->ReadReference(0);
				break;

			case VARTYPE_CLASSREF:
				{
					const char* str = adapter->ReadString();
					if(str)
					{
						const Variable* var = ScriptContext::m_pDeserializeModule->GetVariable(str);

				/*if(var)
				{
					LogF(LL_DEBUG, "Deserialize stack ref %s, val %x", var->GetName(), var->InstancePtr);
				}
				else
				{
					LogF(LL_DEBUG, "Deserialize stack nullref", var->GetName());
				}*/

						m_iData[d++] = (int)var;
					}
					else
						m_iData[d++] = 0;
				}
				break;
			case VARTYPE_STRING:
				*((char**)&m_iData[d]) = (char*)ScriptContext::m_pDeserializeModule->GetContext()->NullString;
				ScriptContext::m_pDeserializeModule->GetContext()->CopyString(adapter->ReadString(), (char**)&m_iData[d++]);
				break;

			case VARTYPE_VECTOR:
				*(float*)&m_iData[d++] = adapter->ReadFloat();
				*(float*)&m_iData[d++] = adapter->ReadFloat();
				*(float*)&m_iData[d++] = adapter->ReadFloat();
				break;
			}
		}
	}
	if(m_pOwner == NULL)
		GetScriptContext()->StoreThread(m_pOwner, this);
}

//-----------------------------------------------------------------------
void ScriptThread::Kill()
{
	m_sState = TF_KILL;

	for(int off = 0; off < m_iDataOff;)
	{
		void* data;
		VType vt = (VType)m_iData[off++];

		switch(vt)
		{
		case VARTYPE_VECTOR:
			off += 3;
			break;

		case VARTYPE_STRING:
			data = (void*)m_iData[off++];
			if(!m_pContext->IsStatic(data))
				m_pContext->FreeMem(data);
			break;

		default:
			off++;
			break;
		}
	}

	//if userlink, unlink it
	if(m_pUserPrev)
	{
		if(m_pUserNext)
			m_pUserNext->m_pUserPrev = m_pUserPrev;

		*m_pUserPrev = m_pUserNext;
	}
}

//-----------------------------------------------------------------------
void ScriptContext::CleanupThreads()
{
	ScriptThread *next;
	for(ScriptThread *st = m_psFirstFree; st; st = next)
	{
		if(st == m_psCurrent)
			m_psCurrent = NULL;

		next = st->m_pNext;
		FreeMem(st);
	}
	m_psFirstFree = NULL;

	for(ScriptThread* st = m_psFirst; st; st = next)
	{
		if(st == m_psCurrent)
			m_psCurrent = NULL;

		next = st->m_pNext;
		FreeMem(st);
	}
	m_psFirst = NULL;

	if(m_psCurrent)
	{
		FreeMem(m_psCurrent);
		m_psCurrent = NULL;
	}
}

//-----------------------------------------------------------------------
void ScriptContext::FlushThreads()
{
	ScriptThread** tprev = &m_psFirst;
	ScriptThread* th = m_psFirst;

	while(th)
	{
		ScriptThread* tnext = th->m_pNext;

		if(th->m_sState == ScriptThread::TF_KILL)
		{
			*tprev = tnext;
			th->m_pNext = m_psFirstFree;
			m_psFirstFree = th;
			th = tnext;
			continue;
		}
		tprev = &th->m_pNext;
		th = tnext;
	}
	th=th;
}

//-----------------------------------------------------------------------
void ScriptContext::ProcessThreads(float time, intptr_t id)
{
	m_Stats.m_iNumThreads = 0;

#ifdef _DEBUG
	ScriptThread *tth;
	for(tth = m_psFirst; tth; tth = tth->m_pNext)
		enf_assert(m_psCurrent != tth);

	for(tth = m_psFirstFree; tth; tth = tth->m_pNext)
		enf_assert(m_psCurrent != tth);
#endif

	ScriptThread *next, *pcurrent = m_psCurrent;
	ScriptThread *st = m_psFirst;
	m_psFirst = NULL;

	if(st == NULL)
		return;

	ENF_BEGIN_PROFILE(PROFILE_PROCESSTHREADS);
	while(st)
	{
		next = st->m_pNext;

		if(st->m_iID == id)
		{
			m_Stats.m_iNumThreads++;

			enf_assert(st->m_sState == ScriptThread::TF_KILL || st->m_sState == ScriptThread::TF_WCOND || st->m_sState == ScriptThread::TF_WTIME);

			switch(st->m_sState)
			{
			case ScriptThread::TF_KILL:
				st->m_pNext = m_psFirstFree;
				m_psFirstFree = st;
				break;

			case ScriptThread::TF_WCOND:
				st->m_sState = ScriptThread::TF_RUNNING;
				m_psCurrent = st;
#ifdef ENF_DEVELOPER
				if(m_bProfile)
				{
					st->m_sStack[st->m_iStackPos].m_Time = Timer::GetTicks();
				}
#endif
#ifdef _DEBUG
				if(st->m_sStack[st->m_iStackPos].m_peInstance)
				{
					CMutexLock lock(m_InstMutex);
					if(!ScriptContext::m_pCurrent->m_Instances.Contains(st->m_sStack[st->m_iStackPos].m_peInstance))
						ScriptContext::m_pCurrent->Error(ScriptContext::RTE_BadInstancePointer);
				}
#endif

				Run(st, &st->m_sStack[st->m_iStackPos], st->m_iBStackPos);
				QuitThread();
				break;

			case ScriptThread::TF_WTIME:
				st->m_fTime -= time;
				if(st->m_fTime <= 0)
				{
					st->m_sState = ScriptThread::TF_RUNNING;
					m_psCurrent = st;
#ifdef ENF_DEVELOPER
					if(m_bProfile)
					{
						st->m_sStack[st->m_iStackPos].m_Time = Timer::GetTicks();
					}
#endif
#ifdef _DEBUG
					if(st->m_sStack[st->m_iStackPos].m_peInstance)
					{
						CMutexLock lock(m_InstMutex);
						if(!ScriptContext::m_pCurrent->m_Instances.Contains(st->m_sStack[st->m_iStackPos].m_peInstance))
							ScriptContext::m_pCurrent->Error(ScriptContext::RTE_BadInstancePointer);
					}
#endif
					Run(st, &st->m_sStack[st->m_iStackPos], st->m_iBStackPos);
					QuitThread();
				}
				else
				{
					//add to sleeping threads
					st->m_pNext = m_psFirst;
					m_psFirst = st;
				}
				break;
			}
		}
		else
		{
			//add to sleeping threads
			st->m_pNext = m_psFirst;
			m_psFirst = st;
		}

		st = next;
	}
	m_psCurrent = pcurrent;
#ifdef _DEBUG
	for(tth = m_psFirst; tth; tth = tth->m_pNext)
		enf_assert(m_psCurrent != tth);

	for(tth = m_psFirstFree; tth; tth = tth->m_pNext)
		enf_assert(m_psCurrent != tth);
#endif
	ENF_END_PROFILE();
}

//-----------------------------------------------------------------------
bool ScriptContext::QuitThread()
{
	enf_assert(m_psCurrent);
	enf_assert(m_psCurrent->m_sState == ScriptThread::TF_RUNNING || m_psCurrent->m_sState == ScriptThread::TF_NONWAITABLE || m_psCurrent->m_sState == ScriptThread::TF_KILL || m_psCurrent->m_sState == ScriptThread::TF_WCOND || m_psCurrent->m_sState == ScriptThread::TF_WTIME);

	//if thread is not sleeping
	if(!(m_psCurrent->m_sState == ScriptThread::TF_WTIME || m_psCurrent->m_sState == ScriptThread::TF_WCOND))
	{
		//remove from user link
		m_psCurrent->Kill();

		//add to free threads
		m_psCurrent->m_pNext = m_psFirstFree;
		m_psFirstFree = m_psCurrent;

		m_psCurrent = m_psCurrent->m_pPrevCurrent;
		return true;
	}

	m_psCurrent = m_psCurrent->m_pPrevCurrent;
	return false;
}

//-----------------------------------------------------------------------
void ScriptContext::StopThread()
{
	enf_assert(m_psCurrent);

#ifdef ENF_DEVELOPER
	if(m_bProfile && m_psCurrent->m_iStackPos > 0)
	{
		VMStack* stack = &m_psCurrent->m_sStack[m_psCurrent->m_iStackPos];

		TimeStamp dtime = Timer::Sub(Timer::GetTicks(), stack->m_Time);
		if(stack->m_peInstance)
			stack->m_peInstance->m_pcClass->Time += dtime;

		stack->m_pfFunc->Time += dtime;
	}
#endif
	//add to sleeping threads
	m_psCurrent->m_pNext = m_psFirst;
	m_psFirst = m_psCurrent;
}

//-----------------------------------------------------------------------
ScriptThread *ScriptContext::CreateThread(bool waitable)
{
	//get new thread from free list
	ScriptThread *th = m_psFirstFree;
	if(th == NULL)
	{
		th = (ScriptThread*)AllocMem(sizeof(ScriptThread), NULL, __FILE__, __LINE__);
		th->m_iID = 0;
		th->m_pNext = NULL;
		th->m_iBStackPos = 0;
		th->m_fTime = 0;
		PLACEMENT_NEW(th) ScriptThread;
	}
	else
	{
		m_psFirstFree = th->m_pNext;
	}

	enf_assert(th);
	
	th->m_pUserPrev = NULL;
	th->m_pUserNext = NULL;
	th->m_iStackPos = 0;
	th->m_iDataOff = 0;
	th->m_sState = waitable ? ScriptThread::TF_RUNNING : ScriptThread::TF_NONWAITABLE;
	th->m_pContext = this;
	th->m_pOwner = NULL;

	//store current thread
	th->m_pPrevCurrent = m_psCurrent;

	//set new current thread
	m_psCurrent = th;
	return th;
}

//-----------------------------------------------------------------------
VMStack* ScriptContext::PushCall(ScriptModule* mod, ScriptThread* thread, Instance** pinst, const Class* cl, int func)
{
	Funct* fn;
	Instance* inst;

	m_Stats.m_iCalls++;

	if(pinst)
	{
		if((inst = *pinst) == NULL)
			Error(RTE_NullPointerError);

#ifdef _DEBUG
		{
			CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
			if(!m_Instances.Contains(inst))
				Error(RTE_BadInstancePointer);
		}
#endif

		if(cl == NULL)
			cl = inst->m_pcClass;

		enf_assert(cl);

		if((uint)func > cl->GetNumMethods())
		{
			SString<> tmp;
			tmp.format("Calling nonexistent function on class '%s'", cl->Name);
			Error(tmp.cstr());
		}

		fn = cl->GetMethod(func - 1);
	}
	else
	{
		fn = mod->GetFunction(func - 1);
		inst = NULL;
	}

	enf_assert(fn);

	VMStack* stack = thread->PushStack(fn, inst);

	if(stack == NULL)
		Error("Stack overflow");

#ifdef ENF_DEVELOPER
	if(m_bProfile)
	{
		stack->m_Time = Timer::GetTicks();
		fn->Profile++;
	}
#endif
	return stack;
}

//-----------------------------------------------------------------------
VMStack* ScriptContext::IPushCall(ScriptThread* thread, Instance* inst, const Class* cl, int func)
{
	Funct* fn = cl->GetMethod(func - 1);

	enf_assert(fn);

#ifdef _DEBUG
	{
		CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
		if(!m_Instances.Contains(inst))
			Error(RTE_BadInstancePointer);
	}
#endif
	VMStack* stack = thread->PushStack(fn, inst);

	if(stack == NULL)
		Error("Stack overflow");

#ifdef ENF_DEVELOPER
	if(m_bProfile)
	{
		stack->m_Time = Timer::GetTicks();
		fn->Profile++;
	}
#endif
	return stack;
}

//-----------------------------------------------------------------------
VMStack* ScriptContext::PopCall(ScriptThread* thread)
{
const VMStack* stack = &thread->m_sStack[thread->m_iStackPos];

	int sreg = stack->m_iStored;
	if(sreg)
	{
		int off = stack->m_iStoredOff;
		//int registers
		for(int c = 0; c < (sreg & 0xf); c++)
		{
			enf_assert(thread->m_iData[off] == (int)VARTYPE_INT);
			off++;
			m_vRegs[c].Int = thread->m_iData[off++];
		}
		sreg >>= 4;

		//float registers
		for(int c = 0; c < (sreg & 0xf); c++)
		{
			enf_assert(thread->m_iData[off] == (int)VARTYPE_FLOAT);
			off++;
			m_vFRegs[c].Int = thread->m_iData[off++];
		}
		sreg >>= 4;

		//class registers
		for(int c = 0; c < (sreg & 0xf); c++)
		{
			enf_assert(thread->m_iData[off] == (int)VARTYPE_CLASS);
			off++;
			m_vCRegs[c].Int = thread->m_iData[off++];
		}
		sreg >>= 4;

		//string registers
		for(int c = 0; c < (sreg & 0xf); c++)
		{
			enf_assert(thread->m_iData[off] == (int)VARTYPE_STRING);
			off++;
			CopyString((const char*)thread->m_iData[off], &m_vSRegs[c], &m_vSRegs[c].String);

			if(!IsStatic((void*)thread->m_iData[off]))
				FreeMem((void*)thread->m_iData[off]);

			off++;
		}
		sreg >>= 4;

		//vector registers
		for(int c = 0; c < (sreg & 0xf); c++)
		{
			enf_assert(thread->m_iData[off] == (int)VARTYPE_VECTOR);
			off++;
			int* fp = (int *)m_vVRegs[c].Pointer;
			fp[0] = thread->m_iData[off++];
			fp[1] = thread->m_iData[off++];
			fp[2] = thread->m_iData[off++];
		}
	}
	thread->m_iDataOff = stack->m_iStoredOff;

#ifdef ENF_DEVELOPER
	if(m_bProfile)
	{
		TimeStamp dtime = Timer::Sub(Timer::GetTicks(), stack->m_Time);
		if(stack->m_peInstance)
			stack->m_peInstance->m_pcClass->Time += dtime;

		stack->m_pfFunc->Time += dtime;
	}
#endif
#ifdef _DEBUG
	if(thread->m_iStackPos == 0)
		Error("Stack underflow");

	//make stack more readable
	thread->m_sStack[thread->m_iStackPos].m_pfFunc = NULL;
#endif


	thread->m_iStackPos--;

	//FIXME: kdyz vytrasujeme na 0 pozici zasobniku, tak muzem prepsat pointer na instanci nejakym smetim
	// PopStack -> m_pvThis->Instance

	if(thread->m_iStackPos)
		SetThis(thread->m_sStack[thread->m_iStackPos].m_peInstance);

	return &thread->m_sStack[thread->m_iStackPos];
}

}