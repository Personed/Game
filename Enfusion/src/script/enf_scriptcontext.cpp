//-----------------------------------------------------------------------------
// File: enf_scriptcontext.cpp
//
// Desc: Script Context. Holds compiled image
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "system/enf_timer.h"
#include "enf_main.h"
#include "common/enf_mathlib.h"
#include "script/enf_scriptmain.h"
#include "script/enf_scriptcontext.h"
#include "script/enf_scriptthread.h"
#include "script/enf_scriptinternal.h"
#include "common/enf_parser.h"
#include "common/enf_staticbuffer.h"
#include "script/enf_scriptdebuginfo.h"
#include "system/enf_profiler.h"
#include "common/enf_diag.h"
#include "common/enf_serialize.h"

#ifdef ENF_BUILD
	#include "BaseWorld/enf_entity.h"
#endif

namespace enf
{
//-----------------------------------------------------------------------
Class* ScriptModule::m_pClassPtr = NULL;
ScriptThread*		ScriptContext::m_pLastDebugThread = NULL;
uint			ScriptContext::m_iDebugStackPos;
DebugMode	ScriptContext::m_DebugMode = SDM_NONE;
ScriptContext* ScriptContext::m_pCurrent = NULL;
ScriptModule* ScriptContext::m_pDeserializeModule = NULL;

const uint ScriptContext::m_iMaxStringCount[] =
{
	32, 32, 64, 24, 16, 12, 8, 8, 8, 6, 5, 4, 4, 4
};

//-----------------------------------------------------------------------
VarType::VarType(ScriptModule* mod, VType type, const char* name): Module(mod), Type(type), Index(0), Sizeof(0)
{
	Name = mod->AddString(name);
	Dim[0] = Dim[1] = 0;
}

//-----------------------------------------------------------------------
Class::Class(ScriptModule* mod, const char* name, uint instancesize, Class* super):
	VarType(mod, VARTYPE_CLASS, name), 
	Super(super),
	ISizeof(instancesize),
	VSizeof(0),
	Constructor(0),
	Destructor(0),
	m_iFlags(0),
	Cached(NULL),
	NumCached(0),
	MaxCached(0)
{
#ifdef ENF_DEVELOPER
	Time = 0;
#endif
	Sizeof = sizeof(Instance*);
	if(super)
	{
		m_Variables = super->m_Variables;
		m_Functions = super->m_Functions;
		VSizeof = super->VSizeof;
	}
	if(super)
	{
		while(super->GetSuper()) super = super->GetSuper();
		TopSuper = super;
	}
	else
		TopSuper = this;
}

//-----------------------------------------------------------------------
Class* Class::NewInheritedClass(ScriptModule* mod, const char* name)
{
	return ENF_NEW Class(mod, name, sizeof(Instance), this);
}

//-----------------------------------------------------------------------
Instance* Class::New(const void* init) const
{
	Instance* inst = GetModule()->GetContext()->AllocInstance(this);
	Initialize(inst, init);
	return inst;
}

//-----------------------------------------------------------------------
void Class::Initialize(Instance* inst, const void *extra) const
{
	//init at least vfptr!
	PLACEMENT_NEW(inst) Instance(this);
}

//-----------------------------------------------------------------------
void Class::AddVariable(Variable* variable)
{
	enf_assert(!m_VariablesHash.Contains(variable->GetName()));

	int size = sizeof(class IVariable);
	if(variable->IsPtr())
		size += (variable->ArraySize() * variable->GetType()->Sizeof);
	variable->Pos = GetNumVariables();

	m_VariablesHash.Insert(variable->GetName(), GetNumVariables());
	m_Variables.Insert(variable);
	VSizeof += size;
}

//-----------------------------------------------------------------------
void Class::AddMethod(Funct* method)
{
	enf_assert(!m_MethodsHash.Contains(method->GetName()));

	m_MethodsHash.Insert(method->GetName(), GetNumMethods());
	m_Functions.Insert(method);
}

//-----------------------------------------------------------------------
Variable* Class::GetVariable(const char* name) const
{
	enf_assert(name != NULL);

	for(const Class* cl = this; cl; cl = cl->Super)
	{
		uint index;
		if(cl->m_VariablesHash.Find(name, index))
			return cl->m_Variables[index];
	}

	return NULL;
}

//-----------------------------------------------------------------------
bool Class::IsInherited(const VarType* vt) const
{
	enf_assert(vt != NULL);

	for(const Class *cl = this; cl; cl = cl->Super)
	{
		if(cl == vt)
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------
bool Class::IsInherited(const char* cname) const
{
	enf_assert(cname != NULL);

	for(const Class *cl = this; cl; cl = cl->Super)
	{
		if(CStr(cname) == cl->GetName())
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------
uint Class::GetMethodIndex(const char* name) const
{
	enf_assert(name != NULL);

	for(const Class* cl = this; cl; cl = cl->Super)
	{
		uint index;
		if(cl->m_MethodsHash.Find(name, index))
		{
			return index;
		}
	}

	return SCRIPT_INDEX_NOT_FOUND;
}

//-----------------------------------------------------------------------
void Instance::Init(const Class* cl)
{
	enf_assert(cl != NULL);
	m_iISizeof = cl->ISizeof;
	m_pcClass = cl;
	m_pNextCached = NULL;
}

//-----------------------------------------------------------------------
#ifdef _DEBUG
void ScriptContext::RegisterInstance(Instance* inst)
{
	if(m_pCurrent)
	{
		CMutexLock lock(m_pCurrent->m_InstMutex);
		m_pCurrent->m_Instances.Insert(inst);
	}
}

void ScriptContext::UnregisterInstance(Instance* inst)
{
	if(m_pCurrent)
	{
		CMutexLock lock(m_pCurrent->m_InstMutex);
		m_pCurrent->m_Instances.Insert(inst);
	}
}
#endif

//-----------------------------------------------------------------------
Instance::Instance(): m_pcClass(NULL), m_iISizeof(0), m_psThread(NULL)
{
	ScriptContext::RegisterInstance(this);
#ifdef _DEBUG
	if(ScriptContext::m_pCurrent)
	{
		CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
		ScriptContext::m_pCurrent->m_Instances.Insert(this);
	}
#endif
}

//-----------------------------------------------------------------------
Instance::~Instance()
{
	ScriptContext::UnregisterInstance(this);
#ifdef _DEBUG
	if(ScriptContext::m_pCurrent)
	{
		CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
		ScriptContext::m_pCurrent->m_Instances.Remove(this);
	}
#endif
}

//-----------------------------------------------------------------------
Instance::Instance(const Class* cl)
{
#ifdef _DEBUG
	if(ScriptContext::m_pCurrent)
	{
		CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
		ScriptContext::m_pCurrent->m_Instances.Insert(this);
	}
#endif
}

//-----------------------------------------------------------------------
void Instance::Delete()
{
	enf_assert(m_pcClass != NULL);
	GetClass()->Delete(this);
}

//-----------------------------------------------------------------------
bool Instance::InstanceOf(const Class* clss) const
{
	return GetClass()->IsInherited(clss);
}

//-----------------------------------------------------------------------
IVariable* Instance::GetIVariable(const char* name)
{
	enf_assert(name != NULL);
	uint index = m_pcClass->GetVariableIndex(name);

	if(index == SCRIPT_INDEX_NOT_FOUND)
		return NULL;

	return GetIVariable(index);
}

//-----------------------------------------------------------------------
const IVariable* Instance::GetIVariable(const char* name) const
{
	enf_assert(name != NULL);
	uint index = m_pcClass->GetVariableIndex(name);

	if(index == SCRIPT_INDEX_NOT_FOUND)
		return NULL;

	return GetIVariable(index);
}

//-----------------------------------------------------------------------
void Instance::Serialize(SerializeOutputAdapter* adapter)
{
	const Class* cl = GetClass();

	struct VarIndex
	{
		uint	index;
		uint	type;
		uint	size;
	};

	VarIndex* varIndex = ALLOCA(VarIndex, cl->GetNumVariables());

	uint count = 0;

	const ScriptContext* sc = cl->GetModule()->GetContext();
	for(uint n = 0; n < cl->GetNumVariables(); n++)
	{
		uint type;
		if(sc->DoSerializeVariable(cl->GetVariable(n), type))
		{
			uint size = sc->SerializeVariableSize(cl->GetVariable(n), GetIVariable(n));
			if(size > 0)
			{
				varIndex[count].index = n;
				varIndex[count].type = type;
				varIndex[count].size = size;
				count++;
			}
		}
	}

	//we have to write number of variables first!
   adapter->WriteUInt16(count);
	for(uint n = 0; n < count; n++)
	{
		const Variable* var = cl->GetVariable(varIndex[n].index);
		const IVariable* ivar = GetIVariable(varIndex[n].index);

		sc->SerializeVariable(adapter, var, ivar, varIndex[n].size, varIndex[n].type);
	}


	uint threadCount = 0;
	for(ScriptThread* th = m_psThread; th; th = th->m_pUserNext)
		threadCount++;

	adapter->WriteUInt16(threadCount);
	for(ScriptThread* th = m_psThread; th; th = th->m_pUserNext)
	{
		adapter->WriteReference(ENF_MAKEFOURCC('S', 'T', 'H', 'R' ), th);
		adapter->BeginObject(ENF_MAKEFOURCC('S', 'T', 'H', 'R' ), NULL, th);
		th->Serialize(adapter);
		adapter->EndObject();
	}
}

//-----------------------------------------------------------------------
void Instance::Serialize(SerializeInputAdapter* adapter)
{
	uint numvars = adapter->ReadUInt16();

	const Class* cl = GetClass();

	ScriptContext* sc = cl->GetModule()->GetContext();
	for(uint n = 0; n < numvars; n++)
	{
		const char* varname = adapter->ReadString();

		uint vindex = cl->GetVariableIndex(varname);
		if(vindex == INDEX_NOT_FOUND)
		{
			LogF(LL_ERROR, "Cannot deserialize variable '%s' in class '%s'", varname, cl->GetName());
		}
		enf_assert(vindex != INDEX_NOT_FOUND);
		uint serializeType;
	
		if(!sc->DoSerializeVariable(cl->GetVariable(vindex), serializeType))
		{
			continue; //WTF?
		}

		sc->SerializeVariable(adapter, cl->GetVariable(vindex), GetIVariable(vindex), serializeType);
	}

	uint threadCount = adapter->ReadUInt16();
	ScriptThread** prev = &m_psThread;
	for(uint t = 0; t < threadCount; t++)
	{
		ScriptThread* th = (ScriptThread*)adapter->ReadReference(ENF_MAKEFOURCC('S', 'T', 'H', 'R' ));

		th->m_pUserNext = *prev;
		if(*prev)
			(*prev)->m_pUserPrev = &th->m_pUserNext;
		
		*prev = th;
		th->m_pUserPrev = prev;
	}
}

//-----------------------------------------------------------------------
Funct::Funct(uint flags, const char* name):
	Flags(flags),
	ReturnValue(NULL),
	FirstStat(NULL),
	ParmNum(0),
	Length(0),
	Name(name), 
	DebugOffset(0)
{
#ifdef ENF_DEVELOPER
	Profile = 0;
	Time = 0;
#endif

}

//-----------------------------------------------------------------------
Funct::~Funct()
{
	for(uint n = 0; n < GetNumVariables(); n++)
	{
		if(!(Variables[n]->Spec & VSPEC_CONST))
			Variables[n]->Cleanup(NULL);
	}

	ReturnValue->Cleanup(NULL);
}

//-----------------------------------------------------------------------
uint Funct::AddVariable(Variable* var)
{
	return Variables.Insert(var);
}

//-----------------------------------------------------------------------
bool ScriptContext::DoSerializeVariable(const Variable* var, uint& serializeType) const
{
	if(m_pCustomSerializeHandler)
	{
		return m_pCustomSerializeHandler->DoSerializeVariable(var, serializeType);
	}
	serializeType = 0;
	return true;
}

//-----------------------------------------------------------------------
void ScriptContext::AddWatchVariable(const Variable* var, uint flags)
{
	m_Watch.Insert(TWatch(var, 0));
}

//-----------------------------------------------------------------------
const Variable* ScriptContext::GetParmDef(uint p) const
{
	const Variable* v = m_pParms[p];

	if((int)v < 0x10000)
	{
		//we are looking into module of calling function, thus -1
		const VMStack* stack = m_psCurrent->GetStack(m_psCurrent->GetStackPos());
		if((stack->m_pfFunc->Flags & FN_SCRIPT) == 0)
			stack = m_psCurrent->GetStack(m_psCurrent->GetStackPos() - 1);

		ScriptContext::IRef& ref = stack->m_pfFunc->Module->m_Refs[(int)v];
		Instance **inst = ref.m_pInst;
		v = (const Variable*)(*inst)->GetClass()->GetVariable(ref.m_pVar->Pos);
	}
	return v;
}

//-----------------------------------------------------------------------
void ScriptModule::TCall(int fn, Instance **inst)
{
	ScriptThread* th = m_pContext->CreateThread();
	Execute(th, fn, inst, inst ? (*inst)->GetClass() : NULL);
	m_pContext->QuitThread();
}

//-----------------------------------------------------------------------
void ScriptModule::Execute(ScriptThread* thread, int func, Instance** inst, const Class* cl)
{
	ScriptContext::m_pCurrent = m_pContext;

	int bstack = thread->m_iStackPos;

	VMStack* stack = m_pContext->PushCall(this, thread, inst, (Class*)cl, func);

	if(stack != NULL)
		m_pContext->Run(thread, stack, bstack);
}

//-----------------------------------------------------------------------
void ScriptModule::IExecute(ScriptThread* thread, int func, Instance* inst, const Class* cl)
{
	ScriptContext::m_pCurrent = m_pContext;

	int bstack = thread->m_iStackPos;
	VMStack* stack = m_pContext->IPushCall(thread, inst, (Class*)cl, func);
	if(stack != NULL)
		m_pContext->Run(thread, stack, bstack);
}

//-----------------------------------------------------------------------
//int Call(class ent, string function, void parm)
//-----------------------------------------------------------------------
SCAPI(ScriptModule::_Call)
{
	Funct* fn;
	const Class* cl;
	uint index;

	result->Int = false;

	ScriptContext* ctx = GetContext();

	if(parm[0]->InstancePtr != NULL)
	{
		cl = parm[0]->InstancePtr->GetClass();

		index = cl->GetMethodIndex(parm[1]->String);

		if(index == SCRIPT_INDEX_NOT_FOUND)
			return;

		fn = cl->GetMethod(index);
	}
	else
	{
		cl = NULL;

		index = GetFunctionIndex(parm[1]->String);

		if(index == SCRIPT_INDEX_NOT_FOUND)
			return;

		fn = GetFunction(index);
	}

	//pass parameter if any	
	if(fn->GetNumParameters() >= 1 && ctx->GetParmDef(2)->GetVType() == fn->GetVariable(0)->GetVType())
		ctx->Copy((Variable*)parm[2], fn->GetVariable(0));

	Instance *ins = parm[0]->InstancePtr;

	ScriptThread *rt = ctx->CreateThread(true);
	Execute(rt, index + 1, ins ? &parm[0]->InstancePtr : NULL, ins ? ins->GetClass() : NULL);
	if(!ctx->QuitThread())
		ctx->StoreThread(parm[0]->InstancePtr, rt);

	result->Int = true;
	return;
}

//-----------------------------------------------------------------------
//int CallFunction(class ent, string function, void returnVal, void parm)
//-----------------------------------------------------------------------
SCAPI(ScriptModule::_CallFunction)
{
	Funct* fn;
	const Class* cl;
	uint index;

	result->Int = false;

	ScriptContext* ctx = GetContext();

	if(parm[0]->InstancePtr != NULL)
	{
		cl = parm[0]->InstancePtr->GetClass();

		index = cl->GetMethodIndex(parm[1]->String);

		if(index == SCRIPT_INDEX_NOT_FOUND)
			return;

		fn = cl->GetMethod(index);
	}
	else
	{
		cl = NULL;

		index = GetFunctionIndex(parm[1]->String);

		if(index == SCRIPT_INDEX_NOT_FOUND)
			return;

		fn = GetFunction(index);
	}

	//pass parameter if any	
	if(fn->GetNumParameters() >= 1 && ctx->GetParmDef(3)->GetVType() == fn->GetVariable(0)->GetVType())
		ctx->Copy((Variable*)parm[3], fn->GetVariable(0));

	Instance *ins = parm[0]->InstancePtr;

	
	const Variable* returnVar = ctx->GetParmDef(2);

	ScriptThread *rt = ctx->GetCurrThread();
	Execute(rt, index + 1, ins ? &parm[0]->InstancePtr : NULL, ins ? ins->GetClass() : NULL);
	
	if(!returnVar->IsConst() && returnVar->GetVType() == fn->GetReturnVariable()->GetVType())
	{
		ctx->Copy(fn->GetReturnVariable(), (Variable*)parm[2]);
	}

	result->Int = true;
	return;
}

//-----------------------------------------------------------------------
uint DebugInfo::AddDebugLine(const char *mod, uint line, uint offset)
{
	uint ol = m_Lines.GetCardinality();

	Line newline;

	//the same as last module. Reuse
	if(m_iLastModule != -1 && m_Modules[m_iLastModule].m_strName == mod)
		newline.m_iModule = m_iLastModule;
	else
	{
		//Different, look for already existing
		uint n;
		for(n = 0; n < m_Modules.GetCardinality(); n++)
		{
			//found it
			if(m_Modules[n].m_strName == mod)
			{
				break;
			}
		}

		//don't found it. Add new one
		if(n == m_Modules.GetCardinality())
		{
			Module& newmod = m_Modules.Append();
			newmod.m_strName = mod;
		}

		m_iLastModule = n;
		newline.m_iModule = n;
	}

	newline.m_iOffset = offset;
	newline.m_iSourceLine = line;
	m_Lines.Insert(newline);

	return ol;
}

//-----------------------------------------------------------------------
void ScriptContext::Cleanup()
{
	//free register strings
	if(m_vSRegs)
	{
		for(uint n = 0; n < MAX_REGS; n++)
			m_vSRegs[n].Cleanup(NULL);
	}

	//cleanup threads
	CleanupThreads();

	RemoveDebugger();

	ScriptModule::Cleanup();

	for(uint n = 0; n < (sizeof(m_pStringPool) / sizeof(m_pStringPool[0])); n++)
	{
		struct string
		{
			string* next;
		};

		string* next = NULL;
		for(string* str = (string*)m_pStringPool[n]; str; str = next)
		{
			next = str->next;
			FreeMem(str);
		}
		m_pStringPool[n] = NULL;
	}

}

//-----------------------------------------------------------------------
ScriptContext::~ScriptContext()
{
	for(uint n = 0; n < m_Modules.GetCardinality(); )
	{
		if(m_Modules[n] != this)
		{
			if(m_Modules[n]->Release() == 0)
				continue;
		}
		n++;
	}
	m_Modules.Clear();

	Cleanup();
	if(m_pCurrent == this)
		m_pCurrent = NULL;
}

//-----------------------------------------------------------------------
uint ScriptContext::GetCallStack(char* callStack, uint maxLength)
{
	enf_assert(callStack && maxLength > 0);

	if(m_psCurrent == NULL)
		return 0;

	int stackPos = m_psCurrent->GetStackPos();
	if(stackPos == 0)
	{
		callStack[0] = 0;
		return 0;
	}

	SString<32768>text;
	SString<32768>tmp;


	//Handle no script stack!
	{
		if(m_pvThis->InstancePtr)
		{
			tmp.format("\nClass:      '%s'",
				m_pvThis->InstancePtr->GetClass()->GetName());
			text += tmp;

			#ifdef ENF_BUILD
				Class* cl = FindClass("_entity");
				if(cl && m_pvThis->InstancePtr->InstanceOf(cl))
				{
					Entity* ent = static_cast<Entity*>(m_pvThis->InstancePtr);
					const char* name = ent->GetName();
					if(name)
						tmp.format("\nEntity name:'%s'\n", name);
					else
						tmp.format("\nEntity id:%d\n", name, ent->GetID());
					text += tmp;
				}
			#endif
		}

		const VMStack* stack = m_psCurrent->GetStack(stackPos);
		if(stack->m_pfFunc)
		{
			tmp.format("\nFunction: '%s'", stack->m_pfFunc->Name);
			if(stack->m_pfFunc->Flags & FN_INTERN)
				stackPos--;
		}

		text += tmp;

		const char *module;
		uint srcline;

		if(stackPos > 0)
		{
			text +="\nStack trace:";
			for(int sp = stackPos; sp >= 1; sp--)
			{
				const VMStack* stack = m_psCurrent->GetStack(sp);
				//broken linkage?
				if(stack->m_pfFunc == NULL)
					continue;

				const DebugInfo* dinfo = stack->m_pfFunc->Module->GetDebugInfo();

				if(dinfo && dinfo->GetLineInfoByOffset(stack->GetDebugOffset(), &module, &srcline))
				{
					tmp.format("\n%s:%d", module, srcline);
					text += tmp;
				}
				else
				{
					tmp.format("\nFunction %s, No debug info", stack->m_pfFunc->Name);
					text += tmp;
				}
			}
		}
	}
	uint length = enf_min(text.Length(), (maxLength - 1));
	CopyMem(callStack, text.cstr(), length);
	callStack[length] = 0;
	return length;
}

//-----------------------------------------------------------------------
void ScriptContext::Error(const char* reason)
{
	if(m_pVMErrorHandler == NULL || m_bIgnoreCrash)
		return;

	SString<32768>text;
	char callStack[32768];
	text = reason;
	if(GetCallStack(callStack, 32768) > 0)
	{
		text += callStack;
	}
#ifndef ENF_DEVELOPER
	ErrorAction act = EA_ABORT;
#else
	ErrorAction act = m_pVMErrorHandler->HandleError(text.cstr());
#endif
	if(m_bNoCrash && act != EA_ABORT)
	{
		if(act == EA_IGNORE)
			m_bIgnoreCrash = true;

		m_iNumCrashes++;
#ifdef ENF_EXCEPTION
		//if(m_iNumCrashes < m_iMaxCrashes)
			throw VMError();	
#endif
		return;
	}
	Error2("VM Error", text.cstr());
}

//-----------------------------------------------------------------------
Variable* ScriptModule::CallFunction(const char *name, ...)
{
	va_list argptr;
	va_start (argptr, name);

	enf_assert(name != NULL);


	ENF_BEGIN_PROFILE(PROFILE_SCRIPTEXECUTE);
	Variable* res = _CallVarArgs(name, false, argptr);
	ENF_END_PROFILE();

	va_end (argptr);

	return res;
}

//-----------------------------------------------------------------------
Variable* ScriptModule::CallTFunction(const char *name, ...)
{
	va_list argptr;
	va_start (argptr, name);

	enf_assert(name != NULL);

	ENF_BEGIN_PROFILE(PROFILE_SCRIPTEXECUTE);
	Variable* res = _CallVarArgs(name, true, argptr);
	ENF_END_PROFILE();
	va_end (argptr);

	return res;
}

//-----------------------------------------------------------------------
void _SetFunctionArgs(ScriptContext* sc, const Funct* func, va_list args)
{
	enf_assert(func != NULL);
	enf_assert(args != NULL);

	for(uint p = 0; p < func->GetNumParameters(); p++)
	{
		if(func->GetVariable(p)->IsPtr() && func->GetVariable(p)->GetVType() != VARTYPE_VECTOR)
		{
			func->GetVariable(p)->Pointer = va_arg(args, void*);
			continue;
		}

		switch(func->GetVariable(p)->GetVType())
		{
		case VARTYPE_STRING:
			sc->CopyString(va_arg(args, const char*), func->GetVariable(p), &func->GetVariable(p)->String);
			break;

		case VARTYPE_FLOAT:
			func->GetVariable(p)->Float = (float)va_arg(args, double);
			break;

		case VARTYPE_VECTOR:
			{
				const float* ptr = va_arg(args, float*);
				float* dptr = (float*)func->GetVariable(p)->Pointer;
				dptr[0] = ptr[0];
				dptr[1] = ptr[1];
				dptr[2] = ptr[2];
			}
			break;

		default:
			func->GetVariable(p)->Int = va_arg(args, int);
			break;
		}
	}
}

//-----------------------------------------------------------------------
Variable* ScriptModule::_CallVarArgs(const char *name, bool waitable, va_list list)
{
	enf_assert(name != NULL);

	uint index = GetFunctionIndex(name);

	if(index == SCRIPT_INDEX_NOT_FOUND)
		return NULL;


	if(ENF_DIAG_VALUE(DM_STATS_SLOWSCRIPT))
		Timer::PushTime();

	Funct *func = GetFunction(index);

	_SetFunctionArgs(m_pContext, func, list);

	{
		CMutexLock lock(m_pContext->GetMutex());

		ScriptThread* th = m_pContext->CreateThread(waitable);
		Execute(th, index + 1);
		m_pContext->QuitThread();
	}

	if(ENF_DIAG_VALUE(DM_STATS_SLOWSCRIPT))
	{
		double rtime = Timer::PopTime(Timer::RESOLUTION_MILLISECONDS);
		static const int limits[] = {0, 1, 2, 5, 10, 100, 1000};
		if(rtime > limits[ENF_DIAG_VALUE(DM_STATS_SLOWSCRIPT)])
		{
			enf::LogF(LL_ERROR, "Script '%s' (%s) took %lf milliseconds", func->GetName(), func->GetModule()->m_Name, rtime);
		}
	}

	return func->ReturnValue;
}

//-----------------------------------------------------------------------
const Variable* Instance::_CallMethod(uint index, va_list list)
{
	enf_assert2(index != INDEX_NOT_FOUND, "Calling unknown method");

	const Variable* res = NULL;

	const Funct* func = m_pcClass->GetMethod(index);

	if(func != NULL)
	{
		if(ENF_DIAG_VALUE(DM_STATS_SLOWSCRIPT))
			Timer::PushTime();
		ENF_PROFILE(PROFILE_SCRIPTEXECUTE);

		enf_assert2(func->Flags & FN_SCRIPT, "Trying to call native method");
		ScriptContext* ctx = GetClass()->GetModule()->GetContext();
		_SetFunctionArgs(ctx, func, list);

		{
			CMutexLock lock(ctx->GetMutex());

			ScriptThread* th = ctx->CreateThread(true);
			ctx->IExecute(th, index + 1, this, GetClass());
			if(!ctx->QuitThread())
				ctx->StoreThread(this, th);

			res = func->GetReturnVariable();
		}

		if(ENF_DIAG_VALUE(DM_STATS_SLOWSCRIPT))
		{
			double rtime = Timer::PopTime(Timer::RESOLUTION_MILLISECONDS);
			static const int limits[] = {0, 1, 2, 5, 10, 100, 1000};
			if(rtime > limits[ENF_DIAG_VALUE(DM_STATS_SLOWSCRIPT)])
			{
				enf::LogF(LL_ERROR, "Script '%s::%s' (%s) took %lf milliseconds", GetClass()->GetName(), func->GetName(), func->GetModule()->GetName().cstr(), rtime);
			}
		}
	}

	return res;
}

//-----------------------------------------------------------------------
const Variable* Instance::CallMethod(const char* name, ...)
{
	va_list list;
	va_start (list, name);

	enf_assert(name != NULL);
	uint index = m_pcClass->GetMethodIndex(name);
	const Variable* res = index == INDEX_NOT_FOUND ? NULL : _CallMethod(index, list);
	
	va_end (list);
	return res;
}

//-----------------------------------------------------------------------
const Variable* Instance::CallMethod(uint methodIndex, ...)
{
	va_list list;
	va_start (list, methodIndex);

	const Variable* res = _CallMethod(methodIndex, list);
	
	va_end (list);
	return res;
}

//-----------------------------------------------------------------------
ScriptModule* ScriptContext::CreateModule(const char* name, ScriptModule* parent, size_t staticSize)
{
	CMutexLock lock(m_Mutex);

	if(parent == NULL)
		parent = this;

	StaticBuffer* ss = CreateStaticBuffer(staticSize, false);
	name = ss->AddString(name);
	ScriptModule* mod = ENF_NEW ScriptModule(name, parent, ss);
	parent->AddRef();
	mod->m_pContext = this;
	RegisterModule(mod);
	return mod;
}

//-----------------------------------------------------------------------
void ScriptContext::RegisterModule(ScriptModule* mod)
{
	TStaticPair pair((intptr_t)mod->GetStaticBuffer()->GetData(), mod->GetStaticBuffer());

	enf_assert(!m_Modules.Contains(mod));
	m_Modules.Insert(mod);
	m_StaticBuffers.Insert(pair);
}

//-----------------------------------------------------------------------
void ScriptContext::UnregisterModule(ScriptModule* mod)
{
	TStaticPair pair((intptr_t)mod->GetStaticBuffer()->GetData(), mod->GetStaticBuffer());

	enf_assert(m_Modules.Contains(mod));
	if(m_Modules.Remove(mod))
		m_StaticBuffers.Remove(TStaticPair((intptr_t)mod->GetStaticBuffer()->GetData(), NULL));
}

//-----------------------------------------------------------------------
const StaticBuffer* ScriptContext::GetStaticOwner(const void* ptr) const
{
	int index = m_StaticBuffers.FindGE(TStaticPair((intptr_t)ptr, NULL));
	if(index >= 0)
	{
		if(m_StaticBuffers[index].Data->IsAllocated(ptr))
			return m_StaticBuffers[index].Data;
	}
	return NULL;
}


//-----------------------------------------------------------------------
void ScriptContext::FreeMem(void *ptr)
{
	enf_assert(ptr);
	enf_assert(!IsStatic(ptr));

	int *mptr = (int *)ptr;
	mptr--;

	m_Stats.m_iAllocatedMemory -= (*mptr + sizeof(int));
	MemoryManager::Free(mptr);
}

//-----------------------------------------------------------------------
void *ScriptContext::AllocMem(uint size, const void *ptr, const char* file, int line)
{
	enf_assert(size > 0);

	char *cptr = (char*)MemoryManager::Alloc(size + sizeof(int), file, line);

	int *mptr = (int *)cptr;

	enf_assert(mptr != NULL);

	*mptr++ = size;

	if(ptr) CopyMem(mptr, ptr, size);

	m_Stats.m_iAllocatedMemory += (size + sizeof(int));

	return (void *)mptr;
}

//-----------------------------------------------------------------------
char* ScriptContext::AllocString(uint length, const void* ptr)
{
	int hash = (length >> 4);
	hash = enf_min(hash, 15);

	struct string
	{
		string* next;
	};

	char* str;

	//is there string in pool?
	if(m_pStringPool[hash] != NULL)
	{
		string** prev = (string**)&m_pStringPool[hash];

		while(*prev)
		{
			string* astr = *prev;

			if(MemSize(astr) >= length)
			{
				m_iStringCount[hash]--;

				*prev = astr->next;
				str = (char*)astr;
				if(ptr) memcpy(astr, ptr, length);
				return (char*)astr;
			}

			prev = &astr->next; //read next
		}
	}

	uint alloc = enf_max(length, 8u);
		
	str = (char*)AllocMem(alloc, NULL, __FILE__, __LINE__);

	if(ptr) memcpy(str, ptr, length);

	return str;
}

//-----------------------------------------------------------------------
void ScriptContext::FreeString(char* ptr)
{
	enf_assert(ptr != NULL);

	if(ptr == NullString || IsStatic(ptr))
		return;

	int hash = (MemSize(ptr) >> 4);
	hash = enf_min(hash, 15);

	if(m_iStringCount[hash] < m_iMaxStringCount[hash])
	{
		m_iStringCount[hash]++;
		struct string
		{
			string* next;
		};

		string* astr = (string*)ptr;

		astr->next = (string*)m_pStringPool[hash];
		m_pStringPool[hash] = (char*)astr;
		return;
	}

	FreeMem(ptr);
}

//-----------------------------------------------------------------------
void ScriptContext::CopyString(const char *v1, char** v2, bool forceCopy)
{
	//this method suppose, that we never have NULL pointer to string.
	//all strings must come from either m_pStatic or from memory heap
	//or ScriptContext::NullString.

	enf_assert(v1 != NULL);
	enf_assert(v2 != NULL);
	enf_assert(*v2 != NULL);

	const StaticBuffer* srcBuffer = GetStaticOwner(v1);

	if(srcBuffer && !forceCopy)
	{
		if(!IsStatic(*v2))
			FreeString(*v2);

		*v2 = (char*)v1;
	}
	else
	{
		uint l = strlen((char *)v1) + 1;

		if(IsStatic(*v2))
			*v2 = (char*)AllocString(l, (void*)v1);
		else
		{
			if(l > MemSize(*v2))
			{
				FreeString(*v2);
				*v2 = (char*)AllocString(l, (void*)v1);
			}
			else
			{
				memcpy(*v2, (void*)v1, l);
			}
		}
	}
}

//-----------------------------------------------------------------------
void ScriptContext::CopyString(const char *v1, const Variable* var2, char** v2, bool forceCopy)
{
	//this method suppose, that we never have NULL pointer to string.
	//all strings must come from either m_pStatic or from memory heap
	//or ScriptContext::NullString.

	enf_assert(v1 != NULL);
	enf_assert(v2 != NULL);
	enf_assert(*v2 != NULL);

	const StaticBuffer* srcBuffer = GetStaticOwner(v1);
	if(!forceCopy && srcBuffer && srcBuffer != m_pStatic && srcBuffer != var2->Module->m_pStatic)
	{
		forceCopy = true;
	}

	if(srcBuffer && !forceCopy)
	{
		if(!IsStatic(*v2))
			FreeString(*v2);

		*v2 = (char*)v1;
	}
	else
	{
		uint l = strlen((char *)v1) + 1;

		if(IsStatic(*v2))
			*v2 = (char*)AllocString(l, (void*)v1);
		else
		{
			if(l > MemSize(*v2))
			{
				FreeString(*v2);
				*v2 = (char*)AllocString(l, (void*)v1);
			}
			else
			{
				memcpy(*v2, (void*)v1, l);
			}
		}
	}
}

//-----------------------------------------------------------------------
void ScriptContext::CallConstructors(const Class* cl,Instance* ent, Variable** parms)
{
	enf_assert(cl != NULL);
	enf_assert(ent != NULL);

	SArray<const Class*, 64> classes;

	for(const Class* cc = cl; cc; cc = cc->GetSuper())
	{
		if(cc->Constructor)
			classes.Insert(cc);
	}

	if(!classes.IsEmpty())
	{
		CMutexLock lock(m_Mutex);
		do
		{
			const Class* cc = classes.Pop();

			Funct* fn = cc->GetMethod(cc->Constructor - 1);

			if(fn->ParmNum)
			{
				if(parms)
					_CopyParameters(parms, fn);
				else
					ClearParameters(fn);
			}
			IExecute(m_psCurrent, cc->Constructor, ent, cc);
		} while(!classes.IsEmpty());
	}
}

//-----------------------------------------------------------------------
void ScriptContext::CallDestructors(const Class* cl, Instance* ent)
{
	enf_assert(cl != NULL);
	enf_assert(ent != NULL);

	SArray<const Class *, 64> classes;

	for(const Class* cc = cl; cc; cc = cc->GetSuper())
	{
		if(cc->Destructor)
			classes.Insert(cc);
	}

	if(!classes.IsEmpty())
	{
		CMutexLock lock(m_Mutex);
		do
		{
			const Class* cc = classes.Pop();
			IExecute(m_psCurrent, cc->Destructor, ent, cc);
		} while(!classes.IsEmpty());
	}
}


//int hzd = 0;
//int strikeback=0;
//int assault=0;

//-----------------------------------------------------------------------
void Class::Delete(Instance* inst) const
{
	enf_assert(inst != NULL);
/*	if(CStr(inst->GetClass()->GetName()) == "HitZoneDistributor")
	{
		hzd--;
		LogF(LL_NORMAL, "~HitZoneDistributor %d", hzd);
		inst=inst;
	}
	if(CStr(inst->GetClass()->GetName()) == "StrikeBackTactic")
	{
		strikeback--;
		LogF(LL_NORMAL, "~StrikeBackTactic %d", hzd);
		inst=inst;
	}

	if(CStr(inst->GetClass()->GetName()) == "CarrierAssault")
	{
		assault--;
		LogF(LL_NORMAL, "~CarrierAssault %d", hzd);
		inst=inst;
	}
*/
	GetModule()->GetContext()->CallDestructors(this, inst);
	GetModule()->GetContext()->FreeInstance(inst, true);
}

//-----------------------------------------------------------------------
Instance* ScriptModule::New(const Class* cl, Variable** parms)
{
	enf_assert(cl != NULL);
	enf_assert(HasModule(cl->Module) || cl->Module->HasModule(this));

/*	if(CStr(cl->GetName()) == "HitZoneDistributor")
	{
		hzd++;
		LogF(LL_NORMAL, "HitZoneDistributor %d", hzd);
		cl=cl;
	}
	if(CStr(cl->GetName()) == "StrikeBackTactic")
	{
		strikeback++;
		LogF(LL_NORMAL, "StrikeBackTactic %d", strikeback);
		cl=cl;
	}
	if(CStr(cl->GetName()) == "CarrierAssault")
	{
		assault++;
		LogF(LL_NORMAL, "CarrierAssault %d", assault);
		cl=cl;
	}
*/
	Instance* inst = cl->New(NULL);

	Variable* pparms[MAX_FUNCTION_PARMS];

	//copy parameters
	//find 1st class with constructor function
	for(const Class* ccl = cl; ccl; ccl = ccl->GetSuper())
	{
		if(ccl->Constructor)
		{
			Funct* cfn = ccl->GetMethod(ccl->Constructor - 1);

			if(cfn->GetNumParameters() && parms) //avoid crash on map-instanced entities
			{
				for(uint np = 0; np < cfn->GetNumParameters(); np++)
					pparms[np] = (Variable*)_GetVariable(parms[np]);

				parms = pparms;
			}
			//Call constructors
			m_pContext->CallConstructors(ccl, inst, parms);
			break;
		}
	}

	return inst;
}

//-----------------------------------------------------------------------
Instance* ScriptModule::New(const Class* cl, bool constructor, const void* ext)
{
	enf_assert(cl != NULL);

/*	if(CStr(cl->GetName()) == "HitZoneDistributor")
	{
		hzd++;
		LogF(LL_NORMAL, "HitZoneDistributor %d", hzd);
		cl=cl;
	}
	if(CStr(cl->GetName()) == "StrikeBackTactic")
	{
		strikeback++;
		cl=cl;
	}
	if(CStr(cl->GetName()) == "CarrierAssault")
	{
		assault++;
		cl=cl;
	}
*/
	Instance* inst = cl->New(ext);

	if(constructor)
	{
		//copy parameters
		//find 1st class with constructor function
		for(const Class* ccl = cl; ccl; ccl = ccl->GetSuper())
		{
			if(ccl->Constructor)
			{
				Funct* cfn = ccl->GetMethod(ccl->Constructor - 1);
				//Call constructors
				m_pContext->CallConstructors(ccl, inst, NULL);
				break;
			}
		}
	}

	return inst;
}

//-----------------------------------------------------------------------
Instance* ScriptModule::New(const char* classname, bool constructor, const void* ext)
{
	enf_assert(classname != NULL);
	Class* cl = FindClass(classname);
	
	if(cl == NULL)
		return NULL;

	return m_pContext->New(cl, constructor, ext);
}

//-----------------------------------------------------------------------
bool ScriptModule::CheckLinking(CParser* parser)
{
	//parser->BeginLine(0);
	bool res = true;
	for(uint n = 0; n < m_Functions.GetCardinality(); n++)
	{
		Funct* fn = m_Functions[n];

		if(fn && fn->Flags & FN_USED && fn->FirstStat == NULL)
		{
			if(parser)
				parser->LError("Linker error: Function '%s' is not linked", fn->GetName());
			res = false;
		}
	}

	for(uint c = 0; c < m_VarTypes.GetCardinality(); c++)
	{
		Class* cl = (Class*)m_VarTypes[c];
		if(cl->Type != VARTYPE_CLASS)
			continue;

		for(uint n = 0; n < cl->GetNumMethods(); n++)
		{
			Funct* fn = cl->GetMethod(n);

			if(fn && fn->Flags & FN_USED && fn->FirstStat == NULL)
			{
				if(parser)
					parser->LError("Linker error: Function '%s::%s' is not linked", cl->GetName(), fn->GetName());
				res = false;
			}
		}
	}
	if(!res)
		return false;

	if(GetParentModule())
		return GetParentModule()->CheckLinking(parser);
	
	return true;
}

//-----------------------------------------------------------------------
void ScriptContext::ZeroInstance(const Class* cl, Instance* inst)
{
	inst->m_psThread = NULL;

	char* mem = (char *)inst;

	ZeroMem(mem + sizeof(Instance), cl->ISizeof - sizeof(Instance));
	mem += cl->ISizeof;

	IVariable* ivar = (IVariable*)mem;

	mem += cl->GetNumVariables() * sizeof(IVariable);

	for(uint n = 0; n < cl->GetNumVariables(); n++, ivar++)
	{
		const Variable* v = cl->GetVariable(n);
		if(v->IsPtr())
		{
			ivar->Pointer = mem; //'cause array pointers could be overwritten
			mem += (v->ArraySize() * v->GetType()->Sizeof);
		}
		v->Zero(ivar);
	}
}

//-----------------------------------------------------------------------
Instance* ScriptContext::AllocInstance(const Class* cl)
{
	enf_assert(cl != NULL);
	Instance* inst;

	//reuse cached instance. Variables are just zeroed
	if(cl->NumCached)
	{
		inst = cl->Cached;
		cl->NumCached--;
		cl->Cached = inst->m_pNextCached;
		ZeroInstance(cl, inst);
	}
	else
	{
		IVariable *ivar;
		//alloc in one block for faster process
		char* mem = (char *)AllocMem((int)cl->VSizeof + cl->ISizeof, NULL, __FILE__, __LINE__);
		if(!mem)
		{
			Error(RTE_NewClass);
			return NULL;
		}

		inst = (Instance *)mem;

		ZeroMem(mem + sizeof(Instance), cl->ISizeof - sizeof(Instance));

		mem += cl->ISizeof;

		ivar = (IVariable *)mem;
		inst->m_pcClass = (Class*)cl;
		inst->m_psThread = NULL;
		inst->m_iISizeof = cl->ISizeof;

		mem += cl->GetNumVariables() * sizeof(IVariable);

		for(uint n = 0; n < cl->GetNumVariables(); n++, ivar++)
		{
			const Variable* v = cl->GetVariable(n);

			if(v->IsPtr())
			{
				ivar->Pointer = mem;
				mem += (v->ArraySize() * v->GetType()->Sizeof);
			}

			v->Clear(ivar);
		}
	}

	m_Stats.m_iNumInstances++;
	cl->GetModule()->AddRef();
	return inst;
}

//-----------------------------------------------------------------------
void ScriptContext::FreeInstance(Instance* inst, bool keepcached)
{
	enf_assert(inst != NULL);

	ScriptModule* pmod = inst->GetClass()->GetModule();

	inst->~Instance();
	if(keepcached && inst->m_pcClass->NumCached < inst->m_pcClass->MaxCached)
	{
		inst->m_pNextCached = inst->m_pcClass->Cached;
		inst->m_pcClass->Cached = inst;
		inst->m_pcClass->NumCached++;
#ifdef _DEBUG
		ubyte* mem = (ubyte*)inst;
		FillMemory32(mem + sizeof(Instance), (inst->m_pcClass->ISizeof - sizeof(Instance)) / 4, 0xfeeefeee);
#endif
	}
	else
	{
		const Class* cl = inst->GetClass();
		for(uint n = 0; n < cl->GetNumVariables(); n++)
			((Variable*)cl->GetVariable(n))->Cleanup(inst->GetIVariable(n));

		inst->m_iISizeof = 0xffffffff;
		FreeMem(inst);
	}
	pmod->Release();
	m_Stats.m_iNumInstances--;
}

//-----------------------------------------------------------------------
uint ScriptContext::SerializeVariableSize(const Variable* var, const IVariable* ivar) const
{
	const void *const*lptr;

	//get array size and value pointer
	int asize;
	if(!var->IsPtr())
	{
		asize = 1;
		lptr = &ivar->Pointer;
	}
	else
	{
		asize = var->ArraySize();
		if(var->GetVType() == VARTYPE_VECTOR)
			asize /= 3;
		lptr = (const void *const*)ivar->Pointer;
	}

	//count necessary values
	int maxsize = 0;
	if(var->GetVType() == VARTYPE_STRING)
	{
		const char* nullString = NullString;
		for(int s = asize - 1; s >= 0; s--)
		{
			if(lptr[s] != nullString || ((const char**)lptr)[s][0] != '\0')
			{
				maxsize = s + 1;
				break;
			}
		}
	}
	else if(var->GetVType() == VARTYPE_VECTOR)
	{
		for(int s = asize - 1; s >= 0; s--)
		{
			if(!((const Vector3*)lptr)[s].IsZero())
			{
				maxsize = s + 1;
				break;
			}
		}
	}
	else if(var->GetVType() == VARTYPE_FUNCPTR)
	{
		//TODO: implement this!
		for(int s = asize - 1; s >= 0; s--)
		{
			//((int*)lptr)[0] = (int)m_pObj;
			//((int*)lptr)[1] = m_iFunctionIndex;
		}
	}
	else if(var->GetVType() == VARTYPE_FLOAT)
	{
		for(int s = asize - 1; s >= 0; s--)
		{
			if(((const float*)lptr)[s] != 0.0f)
			{
				maxsize = s + 1;
				break;
			}
		}
	}
	else if(var->GetVType() == VARTYPE_INT)
	{
		for(int s = asize - 1; s >= 0; s--)
		{
			if(((const int*)lptr)[s] != 0)
			{
				maxsize = s + 1;
				break;
			}
		}
	}
	else
	{
		for(int s = asize - 1; s >= 0; s--)
		{
			if(lptr[s] != NULL)
			{
				maxsize = s + 1;
				break;
			}
		}
	}
	return maxsize;
}

//-----------------------------------------------------------------------
void ScriptContext::SerializeVariable(SerializeOutputAdapter* adapter, const Variable* var, const IVariable* ivar, uint serializeType) const
{
	const void *const*lptr;

	//get array size and value pointer
	int asize;
	if(!var->IsPtr())
	{
		asize = 1;
		lptr = &ivar->Pointer;
	}
	else
	{
		asize = var->ArraySize();
		if(var->GetVType() == VARTYPE_VECTOR)
			asize /= 3;
		lptr = (const void *const*)ivar->Pointer;
	}

	//count necessary values
	int maxsize = 0;
	if(var->GetVType() == VARTYPE_STRING)
	{
		const char* nullString = NullString;
		for(int s = asize - 1; s >= 0; s--)
		{
			if(lptr[s] != nullString || ((const char**)lptr)[s][0] != '\0')
			{
				maxsize = s + 1;
				break;
			}
		}
	}
	else if(var->GetVType() == VARTYPE_VECTOR)
	{
		for(int s = asize - 1; s >= 0; s--)
		{
			if(!((const Vector3*)lptr)[s].IsZero())
			{
				maxsize = s + 1;
				break;
			}
		}
	}
	else if(var->GetVType() == VARTYPE_FUNCPTR)
	{
		//TODO: implement this!
		for(int s = asize - 1; s >= 0; s--)
		{
			//((int*)lptr)[0] = (int)m_pObj;
			//((int*)lptr)[1] = m_iFunctionIndex;
		}
	}
	else if(var->GetVType() == VARTYPE_FLOAT)
	{
		for(int s = asize - 1; s >= 0; s--)
		{
			if(((const float*)lptr)[s] != 0.0f)
			{
				maxsize = s + 1;
				break;
			}
		}
	}
	else if(var->GetVType() == VARTYPE_INT)
	{
		for(int s = asize - 1; s >= 0; s--)
		{
			if(((const int*)lptr)[s] != 0)
			{
				maxsize = s + 1;
				break;
			}
		}
	}
	else
	{
		for(int s = asize - 1; s >= 0; s--)
		{
			if(lptr[s] != NULL)
			{
				maxsize = s + 1;
				break;
			}
		}
	}

	adapter->WriteString(var->GetName());
	adapter->WriteUInt16(maxsize);

	if(serializeType)
	{
		for(int s = 0; s < maxsize; s++)
		{
			adapter->WriteReference(serializeType, (void**)lptr[s]);
		}
	}
	else
	{
		if(var->GetVType() == VARTYPE_STRING)
		{
			for(int s = 0; s < maxsize; s++)
				adapter->WriteString(((const char**)lptr)[s]);
		}
		else if(var->GetVType() == VARTYPE_VECTOR)
		{
			for(int s = 0; s < maxsize; s++)
				adapter->WriteVector3(((Vector3*)lptr)[s]);
		}
		else if(var->GetVType() == VARTYPE_FLOAT)
		{
			for(int s = 0; s < maxsize; s++)
				adapter->WriteFloat(((float*)lptr)[s]);
		}
		else if(var->GetVType() == VARTYPE_INT)
		{
			for(int s = 0; s < maxsize; s++)
				adapter->WriteInt32(((int*)lptr)[s]);
		}
		else if(var->GetVType() == VARTYPE_CLASSREF)
		{
			for(int s = 0; s < maxsize; s++)
			{
				Variable* v = ((Variable**)lptr)[s];
				if(v)
				{
					//LogF(LL_DEBUG, "Serialize Var %s ref %s, val %x", var->GetName(), v->GetName(), v->InstancePtr);
					adapter->WriteString(v->GetName());
				}
				else
				{
					//LogF(LL_DEBUG, "Serialize Var %s nullref", var->GetName());
					adapter->WriteString(NULL);
				}
			}
		}
		else if(var->GetVType() == VARTYPE_FUNCPTR)
		{
		}
		else
		{
			for(int s = 0; s < maxsize; s++)
			{
				void* pi = ((void**)lptr)[s];
/*				if(pi && !adapter->IsStored(pi))
				{
					LogF(LL_NORMAL, "Object ptr cannot be stored. Var %s.%s = %x, type %s",
						inst->GetClass()->GetName(), var->GetName(), pi, pi->GetClass()->GetName());
				}*/
				adapter->WriteReference(serializeType, pi);
			}
		}
	}
}

//-----------------------------------------------------------------------
void ScriptContext::SerializeVariable(SerializeOutputAdapter* adapter, const Variable* var, const IVariable* ivar, uint maxsize, uint serializeType) const
{
	adapter->WriteString(var->GetName());
	adapter->WriteUInt16(maxsize);

	const void *const*lptr;

	if(!var->IsPtr())
		lptr = &ivar->Pointer;
	else
		lptr = (const void *const*)ivar->Pointer;

	if(serializeType)
	{
#ifdef _DEBUG
//		CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
#endif
		const Instance*const* iptr = (const Instance*const*)lptr;

		for(uint s = 0; s < maxsize; s++)
		{
#ifdef _DEBUG
//			if(!ScriptContext::m_pCurrent->m_Instances.Contains((Instance*)((const Instance*const*)lptr)[s]))
//				ScriptContext::m_pCurrent->Error(ScriptContext::RTE_BadInstancePointer);
#endif
			adapter->WriteReference(serializeType, (void**)lptr[s]);
		}
	}
	else
	{
		if(var->GetVType() == VARTYPE_STRING)
		{
			for(uint s = 0; s < maxsize; s++)
				adapter->WriteString(((const char**)lptr)[s]);
		}
		else if(var->GetVType() == VARTYPE_VECTOR)
		{
			for(uint s = 0; s < maxsize; s++)
				adapter->WriteVector3(((Vector3*)lptr)[s]);
		}
		else if(var->GetVType() == VARTYPE_FLOAT)
		{
			for(uint s = 0; s < maxsize; s++)
				adapter->WriteFloat(((float*)lptr)[s]);
		}
		else if(var->GetVType() == VARTYPE_INT)
		{
			for(uint s = 0; s < maxsize; s++)
				adapter->WriteInt32(((int*)lptr)[s]);
		}
		else if(var->GetVType() == VARTYPE_CLASSREF)
		{
			for(uint s = 0; s < maxsize; s++)
			{
				Variable* v = ((Variable**)lptr)[s];
				if(v)
				{
					//LogF(LL_DEBUG, "Serialize Var %s ref %s, val %x", var->GetName(), v->GetName(), v->InstancePtr);
					adapter->WriteString(v->GetName());
				}
				else
				{
					//LogF(LL_DEBUG, "Serialize Var %s nullref", var->GetName());
					adapter->WriteString(NULL);
				}
			}
		}
		else if(var->GetVType() == VARTYPE_FUNCPTR)
		{
		}
		else
		{
#ifdef _DEBUG
//			CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
#endif
			for(uint s = 0; s < maxsize; s++)
			{
				void* pi = ((void**)lptr)[s];
#ifdef _DEBUG
//				if(!ScriptContext::m_pCurrent->m_Instances.Contains((Instance*)pi))
//					ScriptContext::m_pCurrent->Error(ScriptContext::RTE_BadInstancePointer);
#endif
				adapter->WriteReference(serializeType, pi);
			}
		}
	}
}

//-----------------------------------------------------------------------
void ScriptContext::SerializeVariable(SerializeInputAdapter* adapter, const Variable* var, IVariable* ivar, uint serializeType)
{
	void** lptr =  var->IsPtr() ? (void **)ivar->Pointer : &ivar->Pointer;
	uint maxsize = adapter->ReadUInt16();

	if(serializeType)
	{
		if(var->GetVType() == VARTYPE_CLASS)
		{
#ifdef _DEBUG
			CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
#endif
			for(uint s = 0; s < maxsize; s++)
			{
				lptr[s] = (void*)adapter->ReadReference(serializeType);
#ifdef _DEBUG
				if(lptr[s])
				{
					if(!ScriptContext::m_pCurrent->m_Instances.Contains((Instance*)lptr[s]))
						ScriptContext::m_pCurrent->Error(ScriptContext::RTE_BadInstancePointer);
				}
#endif
			}
		}
		else
		{
			for(uint s = 0; s < maxsize; s++)
			{
				lptr[s] = (void*)adapter->ReadReference(serializeType);
			}
		}
	}
	else
	{
		if(var->GetVType() == VARTYPE_STRING)
		{
			for(uint s = 0; s < maxsize; s++)
			{
				const char* str = adapter->ReadString();
				enf_assert(str != NULL);
				CopyString(str, var, &((char**)lptr)[s]);
			}
		}
		else if(var->GetVType() == VARTYPE_VECTOR)
		{
			for(uint s = 0; s < maxsize; s++)
			{
				((Vector3*)lptr)[s] = adapter->ReadVector3();
			}
		}
		else if(var->GetVType() == VARTYPE_FLOAT)
		{
			for(uint s = 0; s < maxsize; s++)
				((float*)lptr)[s] = adapter->ReadFloat();
		}
		else if(var->GetVType() == VARTYPE_INT)
		{
			for(uint s = 0; s < maxsize; s++)
				((int*)lptr)[s] = adapter->ReadInt32();
		}
		else if(var->GetVType() == VARTYPE_CLASSREF)
		{
			for(uint s = 0; s < maxsize; s++)
			{
				const char* name = adapter->ReadString();
				if(name)
				{
					((Variable**)lptr)[s] = ScriptContext::m_pDeserializeModule->GetVariable(name);
					/*Variable* v = ((Variable**)lptr)[s];
					if(v)
						LogF(LL_DEBUG, "Deserialize Var %s ref %s val %x", var->GetName(), v->GetName(), v->InstancePtr);
					else
						LogF(LL_DEBUG, "Deserialize Var %s nullref", var->GetName());*/
				}
				else
				{
					//LogF(LL_DEBUG, "DeSerialize Var %s null ref", var->GetName());
					((Variable**)lptr)[s] = NULL;
				}
			}
		}
		else if(var->GetVType() == VARTYPE_CLASS)
		{
#ifdef _DEBUG
			CMutexLock lock(ScriptContext::m_pCurrent->m_InstMutex);
#endif
			for(uint s = 0; s < maxsize; s++)
			{
				lptr[s] = (void*)adapter->ReadReference(serializeType);
#ifdef _DEBUG				
				if(lptr[s])
				{
					if(!ScriptContext::m_pCurrent->m_Instances.Contains((Instance*)lptr[s]))
						ScriptContext::m_pCurrent->Error(ScriptContext::RTE_BadInstancePointer);
				}
#endif
			}
		}
	}
}
//-----------------------------------------------------------------------
void ScriptContext::InitCommonConstants()
{
//init common constants
	char buff[4] = "0\0";
	static const TToken tt = {buff, TT_NUMBER, 0};

	//inits float 0.0
	m_pvZeroFloat = AddConstant(m_vtFloatType, &tt, false);

	//inits float 1.0
	buff[0] = '1';
	m_pvOneFloat = AddConstant(m_vtFloatType, &tt, false);

	//inits int 0...9
	for(int n = 0; n < 10; n++)
	{
		buff[0] = n + '0';
		m_pvNumbersInt[n] = AddConstant(m_vtIntType, &tt, false);
	}

	//inits string ""
	static const TToken ttnull = {"", TT_QUOTE, 0};
	m_pvZeroString = AddConstant(m_vtStringType, &ttnull, false);

	//inits vector "0 0 0"
	static const TToken ttzvec = {"0 0 0", TT_QUOTE, 0};
	m_pvZeroVector = AddConstant(m_vtVectorType, &ttzvec, false);
}

//-----------------------------------------------------------------------
ScriptStats::ScriptStats():
	m_iFoldedConstant(0),
	m_iNumVariable(0),
	m_iAllocated(0),
	m_iNumFunction(0),
	m_iNumClass(0),
	m_iExecuted(0),
	m_iCalls(0),
	m_iStats(0),
	m_iNumThreads(0),
	m_iNumInstances(0),
	m_iAllocatedMemory(0)
{
}

//-----------------------------------------------------------------------
ScriptContext::ScriptContext(StaticBuffer* stat):
	ScriptModule(NULL, NULL, stat),
	m_bProfile(false),
	m_pDebugger(NULL),
	m_psCurrent(NULL),
	m_psFirst(NULL),
	m_psFirstFree(NULL),
	m_bFlushDebugVars(false),
	m_bEnableDebug(true),
	m_iNumCrashes(0),
	m_iMaxCrashes(0),
	m_bNoCrash(false),
	m_bIgnoreCrash(false),
	m_iRecursion(0),
	m_pCustomSerializeHandler(NULL)
{
	enf_assert(stat != NULL);

	m_pContext = this;
	ScriptContext::m_pCurrent = this;

	//register ourselves
	RegisterModule(this);

//init base types
	m_vtIntType = AddType("int", VARTYPE_INT, sizeof(int));
	m_vtFloatType = AddType("float", VARTYPE_FLOAT, sizeof(float));
	m_vtVectorType = AddType("vector", VARTYPE_VECTOR, sizeof(float));
	m_vtStringType = AddType("string", VARTYPE_STRING, sizeof(char *));
	m_vtVoidType = AddType("void", VARTYPE_VOID, sizeof(void *));
	m_vtClassType = AddType("class", VARTYPE_CLASS, sizeof(void *));
	m_vtClassRefType = AddType("class&", VARTYPE_CLASSREF, sizeof(void *));
	m_vtFuncType = AddType("func", VARTYPE_FUNCPTR, sizeof(void *));
	//m_vtArrayType = AddType("array", VARTYPE_ARRAY, sizeof(Array<int>));

	m_pvThis = AddVariable("this", m_vtClassType, 0, VSPEC_PROTECT);
	m_pvVoid = AddVariable("__void", m_vtVoidType, 0, VSPEC_CONST);

	NullString = (char *)m_pStatic->AllocData(1);

	//Init registers
	m_vRegs = (Variable*)m_pStatic->AllocData(sizeof(Variable) * MAX_REGS);
	m_vVRegs = (Variable*)m_pStatic->AllocData(sizeof(Variable) * MAX_REGS);
	m_vSRegs = (Variable*)m_pStatic->AllocData(sizeof(Variable) * MAX_REGS);
	m_vCRegs = (Variable*)m_pStatic->AllocData(sizeof(Variable) * MAX_REGS);
	m_vFRegs = (Variable*)m_pStatic->AllocData(sizeof(Variable) * MAX_REGS);

	//alloc it in one piece to minimize cache misses
	float* vregbuff = (float*)m_pStatic->AllocData(MAX_REGS * VEC_SIZE * sizeof(float));

	for(uint n = 0; n < MAX_REGS; n++)
	{
		SString<32> tmp;

		m_vVRegs[n].Module = this;
		m_vVRegs[n].SetType(m_vtVectorType);
		m_vVRegs[n].Spec = VSPEC_PTR;
		m_vVRegs[n].Pointer = vregbuff;
		m_vVRegs[n].Dim[0] = VEC_SIZE;
		m_vVRegs[n].Dim[1] = 0;
		m_vVRegs[n].Name = m_pStatic->AddString(tmp.format("@v%02d", n));
		m_vVRegs[n].Pos = -1;
		vregbuff += VEC_SIZE;

		m_vRegs[n].Module = this;
		m_vRegs[n].SetType(m_vtIntType);
		m_vRegs[n].Int = 0;
		m_vRegs[n].Name = m_pStatic->AddString(tmp.format("@m%02d", n));
		m_vRegs[n].Pos = -1;

		m_vSRegs[n].Module = this;
		m_vSRegs[n].SetType(m_vtStringType);
		m_vSRegs[n].Pointer = (void*)NullString;
		m_vSRegs[n].Name = m_pStatic->AddString(tmp.format("@s%02d", n));
		m_vSRegs[n].Pos = -1;

		m_vFRegs[n].Module = this;
		m_vFRegs[n].SetType(m_vtFloatType);
		m_vFRegs[n].Float = 0.0f;
		m_vFRegs[n].Name = m_pStatic->AddString(tmp.format("@f%02d", n));
		m_vFRegs[n].Pos = -1;

		m_vCRegs[n].Module = this;
		m_vCRegs[n].SetType(m_vtClassType);
		m_vCRegs[n].Pointer = NULL;
		m_vCRegs[n].Name = m_pStatic->AddString(tmp.format("@c%02d", n));
		m_vCRegs[n].Pos = -1;
	}

	ZeroMem(m_pStringPool, sizeof(m_pStringPool));
	ZeroMem(m_iStringCount, sizeof(m_iStringCount));

	InitCommonConstants();
	CreateThread(false);
}

}