//-----------------------------------------------------------------------------
// File: enf_scriptlisting.cpp
//
// Desc: Script listing class. Prints out compiled code
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
//#include <stdio.h>
#include "common/enf_types.h"
#include "common/enf_staticbuffer.h"
#include "script/enf_scriptmain.h"
#include "script/enf_scriptcontext.h"
#include "script/enf_scriptcompiler.h"
#include "script/enf_scriptdebuginfo.h"
#include "system/enf_vfilesystem.h"

namespace enf
{
//-----------------------------------------------------------------------
void*						ScriptListing::m_fFile;
const ScriptModule*	ScriptListing::m_pModule;
const CCompiler*		ScriptListing::m_pCompiler;
uint	ScriptListing::m_iDebugOffset = 0;

//-----------------------------------------------------------------------
bool ScriptListing::Init(const CCompiler* compiler, const char* name)
{
	char aname[MAX_FILENAME_LENGTH];

	if(GetFileSystem())
	{
		VFile* vf = GetFileSystem()->Open(name, FILEMODE_WRITE);
		if(vf) vf->Close();

		if(!GetFileSystem()->GetAbsoluteName(name, aname))
			return false;

		name = aname;
	}

	m_fFile = fopen(name, "wt");

	m_pCompiler = compiler;
	m_pModule = compiler->m_pModule;

	return (m_fFile != NULL);
}

//-----------------------------------------------------------------------
void ScriptListing::Close()
{
/*
		Add("\n\nClass memory consumption:\n");
		for(uint n = 0; n < m_VarTypes.GetCardinality(); n++)
		{
			VarType* vt = m_VarTypes.GetElement(n);

			if(vt->Type == VARTYPE_CLASS)
			{
				Add(vt->Name);
				Add("\t");
				Add((float)((Class*)vt)->VSizeof);
				Add("\n");
			}
		}
*/
	if(m_fFile)
	{
		fclose((FILE*)m_fFile);
		m_fFile = NULL;
	}
}
//-----------------------------------------------------------------------
void ScriptListing::AddSourceLine(uint line)
{
	static uint prevline = -1;

	if(prevline == line)
		return;

	prevline = line;
	
	char tmp[1024] = "//\n";

	if(m_pModule->m_pDebugInfo)
	{
		const char *mod;
		uint srcline;

		m_pModule->m_pDebugInfo->GetLineInfo(line, &mod, &srcline);
		sprintf(tmp, "//%s %d \n", mod, srcline);
	}

	fputs(tmp, (FILE*)m_fFile);

}

//-----------------------------------------------------------------------
void ScriptListing::Add(const Class* cl, const Funct* fn)
{
	if(cl)
		fprintf((FILE*)m_fFile,"%s::%s()\n", cl->GetName(), fn->GetName());
	else
		fprintf((FILE*)m_fFile,"%s()\n", fn->GetName());

	m_iDebugOffset = fn->DebugOffset;
}

//-----------------------------------------------------------------------
void ScriptListing::Add(int num)
{
	fprintf((FILE*)m_fFile,"0x%06x", num);
}

//-----------------------------------------------------------------------
void ScriptListing::Add(float num)
{
	fprintf((FILE*)m_fFile,"%.5lf", (double)num);
}

//-----------------------------------------------------------------------
void ScriptListing::Add(const char *str)
{
	if(str)
		fputs(str, (FILE*)m_fFile);
}

//-----------------------------------------------------------------------
#define JMPOFF (int)((char*)((int *)st + st->GetOffset()) - (char*)m_pModule->m_pStatic->GetData())

//-----------------------------------------------------------------------
const Variable* ScriptListing::GetVar(const void *v)
{
	if((int)v >= 0x10000)
		return (Variable*)v;
	else
	{
		return m_pCompiler->m_pModule->m_Refs[(int)v].m_pVar;
	}
}

//-----------------------------------------------------------------------
void ScriptListing::DumpFunctionCall(const Class* cl, const Funct* fn, const void** parms)
{
	if(fn == NULL)
		return;

	if(cl)
	{
		Add(cl->GetName());
		Add("::");
	}
	Add(fn->GetName());
	Add("(");
	for(uint p = 0; p < (int)fn->GetNumParameters(); p++)
	{
		Add(GetVar(parms[p]));
		if((fn->GetNumParameters() > 1) && p < (fn->GetNumParameters() - 1))
			Add(", ");
	}
	Add(")");
}

//-----------------------------------------------------------------------
void ScriptListing::Add(const Stat *st)
{
static uint llline = 0;
static const char *lastm = NULL;
static ushort ptl = 0;

	if(st->GetOpcode() == OP_DEBUG)
	{
		if(ptl != st->GetOffset())
		{
			ptl = st->GetOffset();

			const char *module;

			if(m_pModule->m_pDebugInfo->GetLineInfo((ptl & 0x0fff) + m_iDebugOffset, &module, &llline))
			{
				if(lastm != module)
				{
					lastm = module;

					Add("//");
					Add(module);
					Add("\n");
				}
				//AddSourceLine(ptl);
			}
		}
	}


	Funct *fn;


	OpCode op = st->GetOpcode();

	if(op != OP_DEBUG)
	{
		if(llline >= 0)
			fprintf((FILE*)m_fFile, "%06d:", llline);
		fprintf((FILE*)m_fFile, "0x%08x:\t", (int)((char*)st - (char*)m_pModule->m_pStatic->GetData()));
	}

	switch(op)
	{
	case OP_DEBUG:
		m_pModule->m_pDebugInfo->GetLineInfo((ptl & 0x0fff) + m_iDebugOffset, NULL, &llline);
		break;

	case OP_WAIT:
		fputs("wait\t", (FILE*)m_fFile);
		if(st->Op[0])
			Add(GetVar(st->Op[0]));
		break;
	case OP_GOTO:
		fputs("jump\t", (FILE*)m_fFile);
		Add(JMPOFF);
		break;
	case OP_BGE:
		fputs("bge\t", (FILE*)m_fFile);
		Add(JMPOFF);
		break;
	case OP_BGT:
		fputs("bgt\t", (FILE*)m_fFile);
		Add(JMPOFF);
		break;
	case OP_BLE:
		fputs("ble\t", (FILE*)m_fFile);
		Add(JMPOFF);
		break;
	case OP_BLT:
		fputs("blt\t", (FILE*)m_fFile);
		Add(JMPOFF);
		break;
	case OP_BEQ:
		fputs("beq\t", (FILE*)m_fFile);
		Add(JMPOFF);
		break;
	case OP_BNE:
		fputs("bne\t", (FILE*)m_fFile);
		Add(JMPOFF);
		break;

	case OP_NCALL:
		fputs("ncall\t", (FILE*)m_fFile);
		{
			const ScriptModule::CallRef& ref = m_pModule->m_FunctRefs[(int)(st->GetOffset() & 0x1fff)];
			fn = ref.Module->m_Functions[ref.FunctionIndex - 1];
			DumpFunctionCall(NULL, fn, (const void**)st->Op);
		}
		break;

	case OP_NCALLI:
		fputs("ncalli\t", (FILE*)m_fFile);
		{
			const ScriptModule::CallRef& ref = m_pModule->m_FunctRefs[(int)(st->GetOffset() & 0x1fff)];
			const Class* cl = (const Class*)ref.Module->m_VarTypes[ref.TypeIndex];
			fn = cl->GetMethod(ref.FunctionIndex - 1);
			DumpFunctionCall(cl, fn, (const void**)st->Op);
		}
		break;

	case OP_ICALL:
		fputs("icall\t", (FILE*)m_fFile);
		{
			const ScriptModule::CallRef& ref = m_pModule->m_FunctRefs[(int)(st->GetOffset() & 0x1fff)];
			const Class* cl;
			if(ref.pInstance == NULL)
			{
				fn = ref.Module->m_Functions[ref.FunctionIndex - 1];
				cl = NULL;
			}
			else
			{
				cl = (const Class*)ref.Module->m_VarTypes[ref.TypeIndex];
				fn = cl->GetMethod(ref.FunctionIndex - 1);
			}
			DumpFunctionCall(cl, fn, (const void**)st->Op);
		}
		break;

	case OP_TCALL:
	case OP_CALL:
		if(op == OP_TCALL)
			fputs("tcall\t", (FILE*)m_fFile);
		else
			fputs("call\t", (FILE*)m_fFile);
		{
			const ScriptModule::CallRef& ref = m_pModule->m_FunctRefs[(int)(st->GetOffset() & 0x1fff)];
			const Class* cl;
			if(ref.pInstance == NULL)
			{
				fn = ref.Module->m_Functions[ref.FunctionIndex - 1];
				cl = NULL;
			}
			else
			{
				cl = (const Class*)ref.Module->m_VarTypes[ref.TypeIndex];
				fn = cl->GetMethod(ref.FunctionIndex - 1);
			}
			DumpFunctionCall(cl, fn, (const void**)&st->Op[1]);
		}
		break;

	case OP_RTS:
		fputs("rts", (FILE*)m_fFile);
		break;


	case OP_NEW:
		fputs("new\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		{
			ScriptModule* mod = (ScriptModule*)st->Op[0];
			Add(mod->GetVarType(st->GetOffset())->GetName());
		}
		//TODO: parms
		break;

	case OP_DELETE:
		fputs("delete\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
		break;

	case OP_INC:
		fputs("inc\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
	break;

	case OP_DEC:
		fputs("dec\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
	break;

	case OP_PUSH:
		fputs("push\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
	break;

	case OP_PUSHC:
		fputs("pushc\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
	break;

	case OP_PUSHS:
		fputs("pushs\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
	break;

	case OP_PUSHV:
		fputs("pushv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
	break;

	case OP_POP:
		fputs("pop\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
	break;

	case OP_POPC:
		fputs("popc\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
	break;

	case OP_POPS:
		fputs("pops\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
	break;

	case OP_POPV:
		fputs("popv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
	break;

	case OP_IMOVE:
		fputs("imove\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		if(GetVar(st->Op[1])->GetVType() == VARTYPE_FLOAT)
			Add(*(float*)&st->Op[0]);
		else
			Add((int)st->Op[0]);
	break;

	case OP_MOVE:
		fputs("move\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		break;
	case OP_MOVEI:
		fputs("movei\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_MOVETR:
		fputs("movetr\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_MOVEFR:
		fputs("movefr\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_MOVES:
		fputs("moves\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_MOVEV:
		fputs("movev\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_MOVEA:
		fputs("movea\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_CITF:
		fputs("citf\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_CFTI:
		fputs("cfti\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_CMPI:
		fputs("cmpi\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_CMPF:
		fputs("cmpf\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_CMPV:
		fputs("cmpv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_CMPS:
		fputs("cmps\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_SIZEOF:
		fputs("sizeof\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
	break;

	case OP_FFV:
		fputs("ffv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add("[");
		Add(GetVar(st->Op[2]));
		Add("]");
	break;
	case OP_FTV:
		fputs("ftv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add("[");
		Add(GetVar(st->Op[2]));
		Add("], ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_IFV:
		fputs("ifv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add("[");
		Add(st->GetOffset());
		Add("]");
	break;
	case OP_ITV:
		fputs("itv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add("[");
		Add(st->GetOffset());
		Add("], ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_CLAMPF:
		fputs("clampf\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[2]));
	break;
	case OP_CLAMPI:
		fputs("clampi\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[2]));
	break;
	case OP_CFV:
		fputs("cfv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add("[");
		Add(GetVar(st->Op[2]));
		Add("]");
	break;
	case OP_CTV:
		fputs("ctv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[1]));
		Add("[");
		Add(GetVar(st->Op[2]));
		Add("], ");
		Add(GetVar(st->Op[0]));
	break;
	case OP_ADDI:
		fputs("addi\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_ADDF:
		fputs("addf\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_ADDV:
		fputs("addv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_ADDS:
		fputs("adds\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_ANDI:
		fputs("andi\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_ORI:
		fputs("ori\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_EORI:
		fputs("eori\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_LSLI:
		fputs("lsli\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_ASRI:
		fputs("asri\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_MODI:
		fputs("modi\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_SUBI:
		fputs("subi\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_SUBF:
		fputs("subf\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_SUBV:
		fputs("subv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_MULI:
		fputs("muli\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_MULF:
		fputs("mulf\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_CROSSV:
		fputs("crossv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_SCALEV:
		fputs("scalev\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_DOTV:
		fputs("dotv\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_DIVI:
		fputs("divi\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_DIVF:
		fputs("divf\t", (FILE*)m_fFile);
		Add(GetVar(st->Op[2]));
		Add(", ");
		Add(GetVar(st->Op[0]));
		Add(", ");
		Add(GetVar(st->Op[1]));
	break;
	case OP_NONE:
		fputs("nop\n", (FILE*)m_fFile);
		break;
	default:
		fputs("error\n", (FILE*)m_fFile);
	}

	if(op != OP_DEBUG)
		fputc('\n', (FILE*)m_fFile);
}

//-----------------------------------------------------------------------
void ScriptListing::Add(const Variable *v)
{
	if(v == NULL)
		return;

	if(v->GetName() != NULL && m_pModule->GetContext()->IsStatic(v->GetName()))
		fputs(v->GetName(), (FILE*)m_fFile);
	else
	{
		if(v->Spec & VSPEC_CONST)
		{
			switch(v->GetVType())
			{
			case VARTYPE_VECTOR:
				fputs("_v", (FILE*)m_fFile);
				{
					float *p = (float *)v->Pointer;
					fprintf((FILE*)m_fFile,"<%lf,%lf,%lf>", (double)p[0], (double)p[1], (double)p[2]);
				}
				break;

			case VARTYPE_FLOAT:
				fputs("_f", (FILE*)m_fFile);
				fprintf((FILE*)m_fFile, "%lf", (double)v->Float);
				break;

			case VARTYPE_INT:
				fputs("_i", (FILE*)m_fFile);
				fprintf((FILE*)m_fFile, "%d", v->Int);
				break;

			case VARTYPE_STRING:
				fputs("_s", (FILE*)m_fFile);
				fprintf((FILE*)m_fFile,"'%s'", v->String);
				break;

			default:
				fputs("_c", (FILE*)m_fFile);
				fprintf((FILE*)m_fFile,"%x", v->Int);
				break;
			}
		}
		else
			fprintf((FILE*)m_fFile,"[0x%x]", (unsigned int)v);
	}
}
}