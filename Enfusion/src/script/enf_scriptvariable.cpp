//-----------------------------------------------------------------------------
// File: enf_scriptvariable.cpp
//
// Desc: Script variable class
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "common/enf_types.h"
#include "script/enf_scriptmain.h"
#include "script/enf_scriptcontext.h"
#include "script/enf_scriptinternal.h"
#include "common/enf_staticbuffer.h"
#include "common/enf_parser.h"

namespace enf
{

//-----------------------------------------------------------------------
VarType* Variable::GetType() const
{
	return Module->GetVarType(TypeIndex);
}

//-----------------------------------------------------------------------
void Variable::SetType(const VarType* vt)
{
	TypeType = vt->Type;
	TypeIndex = vt->Index;
	Module = vt->GetModule();
}
//-----------------------------------------------------------------------
void Variable::Clear(IVariable* cvar) const
{
	enf_assert(cvar);

	void* data = 0;

	if(GetVType() == VARTYPE_STRING)
		data = (void*)Module->GetContext()->NullString;

	if(IsPtr())
	{
		int dim = ArraySize();
		if(dim != 0)
			FillMemory32(cvar->Pointer, dim, (int)data);
	}
	else
	{
		cvar->Pointer = data;
	}
}

//-----------------------------------------------------------------------
void Variable::Zero(IVariable* cvar) const
{
	enf_assert(cvar);


	if(GetVType() == VARTYPE_STRING)
	{
		char** ptr;
		int dim;

		if(IsPtr())
		{
			dim = ArraySize();
			ptr = (char**)cvar->Pointer;
		}
		else
		{
			dim = 1;
			ptr = (char**)&cvar->Pointer;
		}

		for(int n = 0; n < dim; n++)
		{
			if(Module->GetContext()->IsStatic(ptr[n]))
				ptr[n] = (char*)Module->GetContext()->NullString;
			else
				ptr[n][0] = 0;
		}
	}
	else
	{
		if(IsPtr())
		{
			int dim;
			if((dim = ArraySize()) != 0)
				ZeroMem32(cvar->Pointer, dim);
		}
		else
			cvar->Int = 0;
	}
}

//-----------------------------------------------------------------------
Variable* ScriptModule::AllocVariable()
{
	Variable* var = (Variable*)m_pStatic->AllocData(sizeof(Variable));
	var->Dim[0] = var->Dim[1] = 0;
	var->Name = NULL;
	var->Int = 0;
	var->TypeType = VARTYPE_VOID;
	var->TypeIndex = -1;
	var->Spec = 0;
	var->Pos = -1;
	var->Module = this;
	return var;
}

//-----------------------------------------------------------------------
Variable* ScriptModule::AllocVariable(const VarType* type, int dim, bool alloc, ushort spec)
{
	enf_assert(type);

	Variable* var = AllocVariable();

	if(type->Type == VARTYPE_VECTOR)
	{
		var->Spec = spec | VSPEC_PTR;
		var->Dim[0] = VEC_SIZE;
		var->Dim[1] = dim;
	}
	else
	{
		var->Spec = spec;
		var->Dim[0] = dim;
		var->Dim[1] = 1;
	}
	var->SetType(type);

	if(alloc)
	{
		if(var->Spec & VSPEC_PTR)
		{
			var->Pointer = m_pStatic->AllocData(type->Sizeof * var->ArraySize());
		}
		var->Clear(var);
	}

	var->Pos = -1;
	return var;
}

//-----------------------------------------------------------------------
void ScriptContext::Copy(const Variable* src, Variable* dest)
{
	enf_assert(src);
	enf_assert(dest);

	if(dest->IsPtr())
	{
		//exception for array of vectors
		if(dest->GetVType() != VARTYPE_VECTOR || src->Dim[1] != 0)
		{
			enf_assert(src->IsPtr());

			dest->Pointer = src->Pointer;
			dest->Dim[0] = src->Dim[0];
			dest->Dim[1] = src->Dim[1];
			return;
		}
	}

	switch(src->GetVType())
	{
	case VARTYPE_STRING:
		CopyString(src->String, dest, &dest->String);
		break;

	case VARTYPE_VECTOR:
		((int*)dest->Pointer)[0] = ((const int*)src->Pointer)[0];
		((int*)dest->Pointer)[1] = ((const int*)src->Pointer)[1];
		((int*)dest->Pointer)[2] = ((const int*)src->Pointer)[2];
		break;

	case VARTYPE_CLASS:
		//Do RT
		dest->InstancePtr = src->InstancePtr;
		break;

	default:
		dest->Pointer = src->Pointer;
	}
}

//-----------------------------------------------------------------------
bool Variable::Set(const TToken* init, IVariable* ivar, int x) const
{
	enf_assert(Module);
	enf_assert(init);
	enf_assert(ivar);

	switch(GetVType())
	{
	case VARTYPE_STRING:
	case VARTYPE_VOID:
		//allow only quoted strings
		if(init->Type != TT_QUOTE)
			return false;

		{
			char** sp = IsPtr() ? (char **)ivar->Pointer : (char **)&ivar->Pointer;
			sp[x] = (char*)Module->AddString(init->Text);
		}
		return true;

	case VARTYPE_FLOAT:
		if(init->Type != TT_NUMBER)
			return false;
		{
			float* fp = IsPtr() ? (float *)ivar->Pointer : &ivar->Float;
			fp[x] = (float)CStr(init->Text).ToFloat();
		}
		return true;

	case VARTYPE_INT:
	case VARTYPE_USER0:
	case VARTYPE_USER1:
	case VARTYPE_USER2:
	case VARTYPE_USER3:
	case VARTYPE_USER4:
	case VARTYPE_USER5:
	case VARTYPE_USER6:
	case VARTYPE_USER7:
		{
			int* ip = IsPtr() ? (int *)ivar->Pointer : &ivar->Int;

			if(init->Text[0] == '#')
				return ((ip[x] = (int)Module->FindType(&init->Text[1])) != 0);

			if(init->Type == TT_NUMBER)
				return (sscanf(init->Text, "%d", &ip[x]) == 1);

			if(init->Type == TT_HEXA)
				return (sscanf(init->Text, "%x", &ip[x]) == 1);
		}
		return false;

	case (VARTYPE_VECTOR):
		if(init->Type != TT_QUOTE || ivar->Pointer == NULL)
			return false;

		{
			float* fp = (float *)ivar->Pointer + x * VEC_SIZE;

			double vec[3];
			if(sscanf(init->Text, "%lf %lf %lf", &vec[0], &vec[1], &vec[2]) != 3)
				return false;

			fp[0] = (float)vec[0];
			fp[1] = (float)vec[1];
			fp[2] = (float)vec[2];
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------
bool Variable::Set(const char* value, IVariable* ivar, int x) const
{
	enf_assert(Module);
	enf_assert(value);
	enf_assert(ivar);

	switch(GetVType())
	{
	case VARTYPE_STRING:
	case VARTYPE_VOID:
		{
			char** sp = IsPtr() ? (char **)ivar->Pointer : (char **)&ivar->Pointer;
			sp[x] = (char*)Module->AddString(value);
		}
		return true;

	case VARTYPE_FLOAT:
		{
			float* fp = IsPtr() ? (float *)ivar->Pointer : &ivar->Float;
			fp[x] = (float)CStr(value).ToFloat();
		}
		return true;

	case VARTYPE_INT:
	case VARTYPE_USER0:
	case VARTYPE_USER1:
	case VARTYPE_USER2:
	case VARTYPE_USER3:
	case VARTYPE_USER4:
	case VARTYPE_USER5:
	case VARTYPE_USER6:
	case VARTYPE_USER7:

		int *ip;
		if(IsPtr())
			ip = (int *)ivar->Pointer;
		else
			ip = &ivar->Int;

		if(value[0] == '#')
			return ((ip[x] = (int)Module->FindType(value + 1)) != 0);

		if(value[0] == '0' && value[1] == 'x')
			return (sscanf(value, "%x", &ip[x]) == 1);
		else
			return (sscanf(value, "%d", &ip[x]) == 1);
		return false;

	case (VARTYPE_VECTOR):
		{
			float* fp = (float *)ivar->Pointer + x * VEC_SIZE;

			double vec[3];
			if(sscanf(value, "%lf %lf %lf", &vec[0], &vec[1], &vec[2]) != 3)
				return false;

			fp[0] = (float)vec[0];
			fp[1] = (float)vec[1];
			fp[2] = (float)vec[2];
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------
void Variable::Cleanup(IVariable* v)
{
	enf_assert(Module);

	if(GetVType() != VARTYPE_STRING)
		return;

	if(Spec & VSPEC_OWNED)
		return;

	ScriptContext* sc = Module->GetContext();

	if(v == NULL)
		v = this;

	if(!IsPtr())
	{
		if(!sc->IsStatic(v->Pointer))
		{
			sc->FreeString(v->String);
			v->String = (char*)sc->NullString;
		}
	}
	else
	{
		char **ptr = (char **)v->Pointer;
		int dim = ArraySize();

		for(int m = 0; m < dim; m++)
		{
			if(!sc->IsStatic(ptr[m]))
			{
				sc->FreeString(ptr[m]);
				ptr[m] = (char*)sc->NullString;
			}
		}
	}
}

}