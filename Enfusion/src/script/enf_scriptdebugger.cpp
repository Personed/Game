//-----------------------------------------------------------------------------
// File: enf_scriptdebugger.cpp
//
// Desc: Interface to remote script debugger
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"

#include "script/enf_scriptmain.h"
#include "script/enf_scriptinternal.h"
#include "script/enf_scriptthread.h"
#include "common/enf_staticbuffer.h"
#include "script/enf_scriptapi.h"
#include "script/enf_scriptdebugger.h"
#include "script/enf_scriptdebuginfo.h"

#include "common/enf_mathlib.h"
#include "resource/enf_vobject.h"
#include "engine/enf_engine.h"
#include "system/enf_vfilesystem.h"
#include "system/enf_thread.h"

#ifdef __APPLE__
	#include <sys/types.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

namespace enf
{
//-----------------------------------------------------------------------
uint VMStack::GetDebugOffset() const
{
	return (const ubyte*)m_psStat - (const ubyte*)m_pfFunc->GetModule()->GetStaticBuffer()->GetData();
}

//-----------------------------------------------------------------------
uint VMStack::GetDebugLine() const
{
	return m_pfFunc->GetModule()->GetDebugInfo()->GetLineByOffset(GetDebugOffset());
}

//-----------------------------------------------------------------------
class ScriptDebugger: public DbgConnection
{
	bool	m_bConnected;

	struct DebugVariable
	{
		ENF_DECLARE_SMALLOBJECT(DebugVariable);
		ScriptModule*	m_pModule;
		Variable*		m_pVar;
		IVariable*		m_pIVar;
		Instance**		m_pInstVar;
		int				m_iParent;
		bool				m_bChanged;
		DebugVariable(ScriptModule* mod, Variable* var = NULL, IVariable* ivar = NULL, Instance** pinst = NULL): m_pModule(mod), m_pVar(var), m_pIVar(ivar), m_pInstVar(pinst), m_iParent(-1), m_bChanged(true)
		{
		}
	};

	Array<DebugVariable*>	m_Variables;

	void FreeVariables()
	{
		ForEach<DeleteItem>(m_Variables);
		m_Variables.Clear();
	}

	struct Breakpoint
	{
		uint m_iIndex;
		ScriptModule* m_pModule;

		Stat* GetStatement() const
		{
			const char* mod;
			uint sline;
			m_pModule->GetDebugInfo()->GetLineInfo(m_iIndex, &mod, &sline);

			if(m_pModule->GetDebugInfo()->m_Lines.GetCardinality() > m_iIndex)
			{
				DebugInfo::Line& line = m_pModule->GetDebugInfo()->m_Lines[m_iIndex];

				return (Stat*)((char*)m_pModule->GetStaticBuffer()->GetData() + line.m_iOffset);
			}

			return NULL;
		}
	};

	Array<Breakpoint>	m_Breakpoints;

	ScriptContext*	m_pContext;
	Set<ScriptModule*> m_Modules;

public:

	int				m_iLastVarPos;
	int				m_iLastStackPos;
	const ScriptThread*	m_psLastThread;

	int UpdateVariableTree(SDebugVariable* tmp);
	int AddVarToList(int level, int parent, DebugVariable* dv);
	void ChangeVar(ScriptModule* mod, IVariable* ivar, bool modify);

	void InvalidateVariables()
	{
		for(uint n = 0; n < m_Variables.GetCardinality(); n++)
			m_Variables[n]->m_bChanged = true;
	}

	bool IsConnected() const
	{
		return m_bConnected;
	}

	ScriptDebugger(ScriptContext* context): DbgConnection(),
		m_pContext(context),
		m_bConnected(false),
		m_iLastVarPos(-1),
		m_iLastStackPos(-1),
		m_psLastThread(NULL)
	{
	}

	void SetBreakpoints()
	{
		m_pContext->ClearBreakpoints();
		for(uint n = 0; n < m_Breakpoints.GetCardinality(); n++)
		{
			Stat* st = m_Breakpoints[n].GetStatement();
			if(st)
			{
				if(st->GetOpcode() == OP_DEBUG)
					st->SetOffset((st->GetOffset() & ~DBGF_MASK) | DBGF_BREAK);
				else
					m_pContext->AddBreakpoint(st, DBGF_BREAK);
			}
		}
	}

	void RemoveBreakpoints()
	{
		for(uint n = 0; n < m_Breakpoints.GetCardinality(); n++)
		{
			Stat* st = m_Breakpoints[n].GetStatement();
			if(st)
			{
				if(st->GetOpcode() == OP_DEBUG)
					st->SetOffset(st->GetOffset() & ~DBGF_MASK);
				else
					m_pContext->RemoveBreakpoint(st);
			}
		}
		m_Breakpoints.Clear();
	}

	int Break(const Funct* fn, Variable* clas, int line);
	bool Init(const char* addr, int portno);
};

//-----------------------------------------------------------------------
//#ifdef ENF_BUILD
void ScriptContext::TouchVar(ScriptThread* currentThread, ScriptModule* mod, IVariable *var, bool modify)
{
	if(m_DebugMode != SDM_NONE && currentThread->m_iStackPos == m_iDebugStackPos)
		m_pDebugger->ChangeVar(mod, var, modify);
#ifdef ENF_DEVELOPER
	else if(!m_Watch.IsEmpty())
	{
		var = mod->_GetVariable(var);
		uint dindex = m_Watch.GetIndexOf(TWatch((const Variable*)var));
		if(dindex != INDEX_NOT_FOUND && (modify || m_Watch[dindex].Data & 1))
		{
			StartDebugger(currentThread->m_sStack[currentThread->m_iStackPos].m_pfFunc, m_pvThis, currentThread->m_sStack[currentThread->m_iStackPos].GetDebugLine());
		}
	}
#endif
}

//-----------------------------------------------------------------------
#ifdef WIN32

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	char title[256];

	if(!GetWindowText(hwnd, title, 256))
		return true;

	if(!strncmp(title, "Enforce Debugger", 16))
	{
		/*if(lParam)
		{
			PostMessage(hwnd, WM_KEYDOWN, VK_F6, 0);
		}
		else*/
		{
			DWORD dwProcessID;
			::GetWindowThreadProcessId(hwnd, &dwProcessID);
			::AllowSetForegroundWindow(dwProcessID);
		}

		return false;
	}

	return true;
}
#endif
//-----------------------------------------------------------------------
void ScriptContext::InitDebugger(const char *ipaddress, int port)
{
	if(m_pDebugger == NULL)
	{
#ifdef WIN32
		BOOL res = EnumWindows(EnumWindowsProc, false);

		if(res)
		{
			STARTUPINFO si;
			PROCESS_INFORMATION pi;

			GetStartupInfo(&si);
#ifdef _DEBUG
			CreateProcess(NULL, "scriptdebugger_d.exe", NULL, NULL, false, CREATE_DEFAULT_ERROR_MODE, NULL, NULL, &si, &pi);
#else
			CreateProcess(NULL, "scriptdebugger.exe", NULL, NULL, false, CREATE_DEFAULT_ERROR_MODE, NULL, NULL, &si, &pi);
#endif
			WaitForInputIdle(pi.hProcess, 10000); //10sec

			AllowSetForegroundWindow(pi.dwProcessId);
		}

		EnumWindows(EnumWindowsProc, true);
#endif

		m_pDebugger = ENF_NEW ScriptDebugger(this);
		m_pDebugger->Init(ipaddress, port);
	}
}

//-----------------------------------------------------------------------
void ScriptContext::RemoveDebugger()
{
	if(m_pDebugger)
	{
		if(m_pDebugger->IsConnected())
			m_pDebugger->SendCommand(DBGCMD_DISCONNECT);

		SAFE_DELETE(m_pDebugger);
	}
}

//-----------------------------------------------------------------------
int ScriptContext::StartDebugger(const Funct *fn, Variable *clas, uint line)
{
	if(m_pDebugger == NULL)
	{
#ifdef ENF_XENON
		InitDebugger("192.168.1.187", 1000);
#else
		InitDebugger("localhost", 1000);
#endif
	}

	int res = 0;

	if(m_pDebugger && m_pDebugger->IsConnected())
	{
		if(m_bFlushDebugVars)
			m_pDebugger->m_iLastStackPos = -1;
		res = m_pDebugger->Break(fn, clas, line);
	}

	if(res == 3)
		GetEngine()->ShowWindow();

	switch(res)
	{
	case 0:
		m_bEnableDebug = false;
		break;

	case 1:
		m_bEnableDebug = true;
		break;

	case -1:
#ifdef WIN32
		__asm int 0x03;
#elif defined(ENF_XENON) && defined(ENF_DEVELOPER)
		DmCrashDump(false);
#endif
		break;
	}

	return res;
}

//#endif ///to ENF_BUILD
//-----------------------------------------------------------------------
int ScriptDebugger::UpdateVariableTree(SDebugVariable* tmparray)
{
	char tmp[8192];
	int numv = 0;

	for(uint n = 0; n < m_Variables.GetCardinality(); n++)
	{
		//if(mvVariables[n]->m_bChanged)
		{
			m_Variables[n]->m_bChanged = false;
			
			if(m_Variables[n]->m_pInstVar)
			{
				const Instance* inst = *m_Variables[n]->m_pInstVar;

				if(inst == NULL)
					continue;

				uint vpos = m_Variables[n]->m_pVar->Pos;

				//create fake variable from class variable def and instance variable
				if(vpos < inst->GetClass()->GetNumVariables())
				{
					Variable lvar;

					lvar = *inst->GetClass()->GetVariable(vpos);
					lvar.Pointer = inst->GetIVariable(vpos)->Pointer;
					Variable2Text(&lvar, tmp, true, true);
				}
			}
			else
				Variable2Text(m_Variables[n]->m_pVar, tmp, true, true);

			int llen = strlen(tmp);
			
			if(llen > 127)
			{
				CopyMem(tmparray[numv].m_strValue, tmp, 128);
				strcpy(&tmparray[numv].m_strValue[124], "...");
			}
			else
				strcpy(tmparray[numv].m_strValue, tmp);

			tmparray[numv].m_eType = (VType)LE_INT((uint)m_Variables[n]->m_pVar->GetVType());
			tmparray[numv].m_iItem = LE_INT(n);
			tmparray[numv].m_iParent = LE_INT(m_Variables[n]->m_iParent);
			numv++;
		}
	}

	return numv;
}

//-----------------------------------------------------------------------
void ScriptDebugger::ChangeVar(ScriptModule* mod, IVariable *ivar, bool modify)
{
	Instance** inst = NULL;
	Variable* var;

	ivar = mod->_GetVariable3(ivar, var, inst);

	if(var->GetName() == NULL || var->GetName()[0] == '@' || var->Spec & VSPEC_CONST)
		return;

	for(uint n = 0; n < m_Variables.GetCardinality(); n++)
	{
		if(m_Variables[n]->m_pVar == var && m_Variables[n]->m_pIVar == ivar)
		{
			m_Variables[n]->m_bChanged = true;

			//if array, change pointers for this memory
			if(m_Variables[n]->m_pVar->IsPtr())
			{
				for(uint a = 0; a < m_Variables.GetCardinality(); a++)
				{
					if(m_Variables[a]->m_pVar->IsPtr() &&  m_Variables[a]->m_pVar->Pointer == var->Pointer)
						m_Variables[a]->m_bChanged = true;
				}
			}
			return;
		}
	}

	//don't found. Add it!
	AddVarToList(0, -1, ENF_NEW DebugVariable(mod, var, ivar, inst));
}

//-----------------------------------------------------------------------
void ScriptContext::ClearBreakpoints()
{
	m_Breapoints.Clear();
}

//-----------------------------------------------------------------------
void ScriptContext::AddBreakpoint(const Stat* st, uint flags)
{
	uint index = m_Breapoints.GetIndexOf(Breakpoint(st, flags));
	if(index == INDEX_NOT_FOUND)
		m_Breapoints.Insert(Breakpoint(st, flags));
	else
		m_Breapoints[index].Data = flags;
}

//-----------------------------------------------------------------------
void ScriptContext::RemoveBreakpoint(const Stat* st)
{
	m_Breapoints.Remove(Breakpoint(st, 0));
}

//-----------------------------------------------------------------------
bool ValidateInstancePtr(Instance* inst)
{
	if((inst->GetISizeof() & 0xfff00003) != 0 || ((uint)inst->GetClass() & 0x03) != 0 || (uint)inst->GetClass() <= 0xffff)
		return false;

#if defined(WIN32)
	if(IsBadReadPtr(inst, sizeof(Instance)) || IsBadReadPtr(inst->GetClass(), sizeof(Class)))
		return false;
#endif
	return true;
}

//-----------------------------------------------------------------------
void Instance2Text(char* buff, Instance* inst)
{
	if(inst == NULL)
		strcpy(buff, "NULL");
	else if(!ValidateInstancePtr(inst))
		strcpy(buff, "INVALID");
	else
		sprintf(buff, "%s<%x>", inst->GetClass()->GetName(), (unsigned int)inst);
}

//-----------------------------------------------------------------------
int ScriptDebugger::AddVarToList(int level, int parent, DebugVariable* dv)
{
	enf_assert(dv);

	dv->m_iParent = parent;
	int item = m_Variables.Insert(dv);

	if(level > 1)
		return -1;

	if(!dv->m_pVar->IsPtr() && dv->m_pVar->GetVType() == VARTYPE_CLASS)
	{
		Instance* inst = dv->m_pVar->InstancePtr;
		if(inst)
		{
			if(ValidateInstancePtr(inst))
			{
				if(dv->m_pModule->IsClass(inst->GetClass()))
				{
					for(uint m = 0; m < inst->GetClass()->GetNumVariables(); m++)
					{
						Variable* var = (Variable*)inst->GetClass()->GetVariable(m);
						DebugVariable* newdv = ENF_NEW DebugVariable(dv->m_pModule, var, inst->GetIVariable(m), &dv->m_pVar->InstancePtr);
						AddVarToList(level + 1, item, newdv);
					}
				}
			}
		}
	}
	return item;
}

//-----------------------------------------------------------------------
int ScriptDebugger::Break(const Funct* fn, Variable* clas, int line)
{
	ScriptModule* mod = fn->GetModule();

	//is it first time, we are debugging this module?
	if(!m_Modules.Contains(mod))
	{
		m_Modules.Insert(mod);

		const DebugInfo* di = mod->GetDebugInfo();

		//collect breakpoints in code
		Array<int> breakpoints;

		for(uint line = 0; line < di->m_Lines.GetCardinality(); line++)
		{
			const DebugInfo::Line& dline = di->m_Lines[line];

			const Stat *st = (const Stat*)((char*)mod->GetStaticBuffer()->GetData() + dline.m_iOffset);

			if(st->GetOpcode() == OP_DEBUG)
			{
				if((st->GetOffset() & DBGF_MASK) == DBGF_BREAK)
				{
					breakpoints.Insert(line);
				}
			}
		}

		SendCommand(DBGCMD_ADDMODULE);
		SendInt((int)mod);
		SendInt(di->m_Modules.GetCardinality());
		SendInt(di->m_Lines.GetCardinality());
		SendInt(breakpoints.GetCardinality());

		for(uint n = 0; n < di->m_Modules.GetCardinality(); n++)
		{
			char absname[MAX_FILENAME_LENGTH];
			GetFileSystem()->GetAbsoluteName(di->m_Modules[n].m_strName, absname);
			SendString(absname, strlen(absname));
		}

		SendData(di->m_Lines.GetArray(), di->m_Lines.GetCardinality() * sizeof(DebugInfo::Line));
		SendData(breakpoints.GetArray(), breakpoints.GetCardinality() * sizeof(int));
	}


	DbgBreak brk;

	brk.Module = fn->GetModule();
	brk.Line = line;
	brk.StackSize = 0;

	//add this breakpoint, if not exist (probably debug command in source code)
	const char* modname;
	uint sline;
			
	mod->GetDebugInfo()->GetLineInfo(line, &modname, &sline);

	const ScriptThread* sthread = m_pContext->m_psCurrent;
	for(int n = sthread->GetStackPos(); n > 0; n--)
	{
		const VMStack* stack = sthread->GetStack(n);

		ZeroMem(&brk.Stack[brk.StackSize], sizeof(CallStack));
		
		brk.Stack[brk.StackSize].Module = stack->m_pfFunc->GetModule();
		brk.Stack[brk.StackSize].Line = stack->GetDebugLine();
		if(stack->m_peInstance)
			strcpy(brk.Stack[brk.StackSize].ClassName, stack->m_peInstance->m_pcClass->GetName());

		strcpy(brk.Stack[brk.StackSize].FunctionName, stack->m_pfFunc->GetName());
		brk.StackSize++;
	}

	brk.ResetVars = false;

	if(m_iLastStackPos != sthread->GetStackPos() || m_psLastThread != sthread)
	{
		m_iLastStackPos = sthread->GetStackPos();
		m_psLastThread = sthread;

		brk.ResetVars = true;

		//store global variables!!!!
		Array<DebugVariable*> tmpvar;
		for(uint n = 0; n < m_Variables.GetCardinality();)
		{	
			if(m_Variables[n]->m_pVar)
			{
				if((m_Variables[n]->m_pVar->Spec & VSPEC_GLOBAL) && m_Variables[n]->m_pInstVar == NULL)
				{
					tmpvar.Insert(m_Variables[n]);
					m_Variables.RemoveElement(n);
					continue;
				}
			}
			n++;
		}
		FreeVariables();

		for(uint n = 0; n < fn->GetNumVariables(); n++)
		{
			AddVarToList(0, -1, ENF_NEW DebugVariable(fn->GetModule(), fn->GetVariable(n), fn->GetVariable(n), NULL));
		}

		//move back globals
		for(uint n = 0; n < tmpvar.GetCardinality(); n++)
			AddVarToList(0, -1, tmpvar[n]);
	}

	//send packet
	SendCommand(DBGCMD_BREAK, &brk, (char*)&brk.Stack[brk.StackSize] - (char*)&brk);

	SDebugVariable dv[1024];
	int numdv = UpdateVariableTree(dv);

	SendCommand(DBGCMD_VARIABLES, dv, numdv * sizeof(SDebugVariable));

	char buff[65536];
	int size;
	while(true)
	{
		switch(ReceiveCommand(&size))
		{
		case DBGCMD_ERROR:
			m_bConnected = false;
			return 0;

		case DBGCMD_BREAKPOINTS:
			RemoveBreakpoints();

			{
				int numBreakpoints;
				if(!ReceiveInt(numBreakpoints))
					return 0;

				m_Breakpoints.SetSize(numBreakpoints);
				for(int n = 0; n < numBreakpoints; n++)
				{
					int module;
					int line;
					ReceiveInt(module);
					ReceiveInt(line);
					m_Breakpoints[n].m_pModule = (ScriptModule*)module;
					m_Breakpoints[n].m_iIndex = line;
				}
				SetBreakpoints();
			}
			break;

		case DBGCMD_DEBUG:
			return -1;

		case DBGCMD_STEPINTO:
			ScriptContext::m_DebugMode = SDM_STEP_INTO;
			ScriptContext::m_iDebugStackPos = m_pContext->m_psCurrent->GetStackPos();
			ScriptContext::m_pLastDebugThread = m_pContext->m_psCurrent;
			return 1;

		case DBGCMD_STEPOVER:
			ScriptContext::m_DebugMode = SDM_STEP_OVER;
			ScriptContext::m_iDebugStackPos = m_pContext->m_psCurrent->GetStackPos();
			ScriptContext::m_pLastDebugThread = m_pContext->m_psCurrent;
			return 1;

		case DBGCMD_STEPOUT:
			ScriptContext::m_DebugMode = SDM_STEP_OUT;
			ScriptContext::m_iDebugStackPos = m_pContext->m_psCurrent->GetStackPos() - 1;
			ScriptContext::m_pLastDebugThread = m_pContext->m_psCurrent;
			return 1;

		case DBGCMD_STEPTO:
			ScriptContext::m_DebugMode = SDM_STEP_TO;
			ScriptContext::m_iDebugStackPos = m_pContext->m_psCurrent->GetStackPos() - 1;
			ScriptContext::m_pLastDebugThread = m_pContext->m_psCurrent;
			{
				DbgConnection::DbgStepTo to;
				ReceiveData(&to, sizeof(to));
				
				if(to.Module->GetDebugInfo())
				{
					Stat* st = NULL;
					const char* mod;
					uint sline;
					to.Module->GetDebugInfo()->GetLineInfo(to.Line, &mod, &sline);

					if((int)to.Module->GetDebugInfo()->m_Lines.GetCardinality() > to.Line)
					{
						DebugInfo::Line& line = to.Module->GetDebugInfo()->m_Lines[to.Line];

						st = (Stat*)((char*)to.Module->GetStaticBuffer()->GetData() + line.m_iOffset);
					}

					if(st)
					{
						if(st->GetOpcode() == OP_DEBUG)
							st->SetOffset((st->GetOffset() & ~DBGF_MASK) | DBGF_TOCURSOR);
						else
							m_pContext->AddBreakpoint(st, DBGF_TOCURSOR);
					}
				}
			}
			return 1;

		case DBGCMD_STEPCONTINUE:
			ScriptContext::m_DebugMode = SDM_NONE;
			return 3;

		case DBGCMD_RUN:
			ScriptContext::m_DebugMode = SDM_NONE;
			return 0;
		default:
			if(size >= 0)
				ReceiveData(buff, size);
		}
	}
}

//-----------------------------------------------------------------------
bool ScriptDebugger::Init(const char *addr, int portno)
{
	struct sockaddr_in serv_addr;

#if defined(ENF_XENON) && defined(_DEBUG)
	XNetStartupParams parms;

	parms.cfgSizeOfStruct = sizeof(parms);
	parms.cfgFlags = XNET_STARTUP_BYPASS_SECURITY;
	XNetStartup(&parms);
#endif

#if defined(WIN32) || defined(ENF_XENON)
	WSADATA wsaData;

	if(WSAStartup(MAKEWORD(2, 0), &wsaData) != 0)
		return false;

	if (LOBYTE(wsaData.wVersion)!=2 || HIBYTE(wsaData.wVersion)!=0)
	{
		WSACleanup();
		return false;
	}
#endif

	ZeroMem(&serv_addr, sizeof(serv_addr));

#ifdef ENF_XENON
	return false;
	newsockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (newsockfd == INVALID_SOCKET ) 
		return false;

	serv_addr.sin_addr.s_addr = inet_addr("192.168.2.169");

	XNADDR xnaddr;
	XNetGetTitleXnAddr(&xnaddr);

#elif defined(WIN32)
	newsockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (newsockfd < 0) 
		return false;

	struct hostent *server;

	server = gethostbyname(addr);
	if (server == NULL)
		return false;

	CopyMem((char *)&serv_addr.sin_addr.s_addr, (char *)server->h_addr, server->h_length);

	int buffsize = 65536;
	setsockopt(newsockfd, 0, SO_RCVBUF, (const char*)&buffsize, sizeof(buffsize));
	setsockopt(newsockfd, 0, SO_SNDBUF, (const char*)&buffsize, sizeof(buffsize));

#elif defined(ENF_PS3)
	newsockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (newsockfd < 0) 
		return false;
#endif

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = (unsigned short)htons((unsigned short)portno);

	uint retry = 5;
	while(retry--)
	{
		if (connect(newsockfd, (const sockaddr*)&serv_addr, sizeof(serv_addr)) == 0)
			break;

#if defined(WIN32) || defined(ENF_XENON)
		CThread::Sleep(100);
#endif
	}

	if(retry == 0)
	{
#ifdef ENF_PS3
		socketclose(newsockfd);
#elif defined(__APPLE__)
		close(newsockfd);
#else
		closesocket(newsockfd);
#endif

#if defined(WIN32) || defined(ENF_XENON)
		int err = WSAGetLastError();
#endif
		return false;
	}

	m_bConnected = true;


#ifdef WIN32
	uint pid = ::GetCurrentProcessId();
#else
	uint pid = 0;
#endif
	SendCommand(DBGCMD_CONNECT, &pid, sizeof(pid));
	return true;
}


//-----------------------------------------------------------------------
void Variable2Text(const Variable *var, char *tmp, bool type, bool name)
{
	if(type)
	{
		strcpy(tmp, var->GetType()->GetName());
		strcat(tmp, " ");
	}
	else
	{
		tmp[0] = 0;
	}

	int dim = var->ArraySize();

	if(var->GetVType() == VARTYPE_VECTOR)
		dim /= VEC_SIZE;

	if(var->GetName() && var->GetName()[0] != '@')
		sprintf(&tmp[strlen(tmp)],"%s = ", var->GetName());

	if(!var->IsPtr())
		dim = 1;
	else
	{
		if(dim > 1)
			strcat(tmp, "{");
	}
	int l = strlen(tmp);

	if(type && l < 14)
	{
		FillMem(&tmp[l], 14 - l, ' ');
		tmp[14] = 0;
	}

	int n;
	for(n = 0; n < dim; n++)
	{
		char num[8192];

		switch(var->GetVType())
		{
		case VARTYPE_VECTOR:
			float *p;
			p = (float *)var->Pointer;
			sprintf(num,"<%g,%g,%g>", (double)p[n * VEC_SIZE + 0], (double)p[n * VEC_SIZE + 1], (double)p[n * VEC_SIZE + 2]);
//			sprintf(num,"<%lf,%lf,%lf>", (double)p[n * VEC_SIZE + 0], (double)p[n * VEC_SIZE + 1], (double)p[n * VEC_SIZE + 2]);
			break;

		case VARTYPE_CLASSREF:
			{
			Variable** ptr;
			if(var->IsPtr())
				ptr = (Variable**)var->Pointer;
			else
				ptr = (Variable**)&var->Pointer;


			if(ptr == NULL)
				strcpy(num,"NULL");
			else
				Instance2Text(num, ptr[n]->InstancePtr);
			}
			break;

		case VARTYPE_CLASS:
			if(!var->IsPtr())
			{
				Instance2Text(num, var->InstancePtr);
			}
			else
			{
				Instance **pent = (Instance**)var->Pointer;				
				Instance2Text(num, pent[n]);
			}
			break;

		case VARTYPE_FLOAT:
			if(var->IsPtr())
				sprintf(num, "%g", (double)((float *)var->Pointer)[n]);
			else
				sprintf(num, "%g", (double)var->Float);
			break;

		case VARTYPE_VOBJECT:
			ObjectResource *vob;

			if(var->IsPtr())
				vob = ((ObjectResource**)var->Pointer)[n];
			else
				vob = (ObjectResource*)var->Pointer;
				
			if(vob)
				sprintf(num, "'%s'", vob->GetName().cstr());
			else
				strcpy(num, "NULL");
			break;

		case VARTYPE_INT:
			if(var->IsPtr())
				sprintf(num, "%d", ((int *)var->Pointer)[n]);
			else
				sprintf(num, "%d", var->Int);
			break;

		case VARTYPE_STRING:
			if(var->IsPtr())
				sprintf(num,"'%s'", ((char **)var->Pointer)[n]);
			else
				sprintf(num,"'%s'", var->String);
			break;

		default:
			if(var->IsPtr())
				sprintf(num,"'%x'", ((int *)var->Pointer)[n]);
			else
				sprintf(num,"'%x'", (unsigned int)var->Pointer);
			break;
		}

		strcat(tmp, num);
		if(n < (dim - 1))
			strcat(tmp, ",");

		if(strlen(tmp) > 1000) break;
	}

	if(var->IsPtr() && dim > 1)
	{
		if(n == dim)
			strcat(tmp, "}");
		else
			strcat(tmp, "...}");
	}
}

}