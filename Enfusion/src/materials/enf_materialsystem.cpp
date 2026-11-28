//-----------------------------------------------------------------------------
// File: enf_materialsystem.cpp
//
// Desc: External class for material system
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "materials/enf_imaterialsystem.h"
#include "common/enf_diag.h"
#include "pipeline/enf_pipeline.h"
#include "common/enf_staticbuffer.h"

namespace enf
{
MaterialSystem* g_pMaterialSystem = NULL;

//-----------------------------------------------------------------------
PUBLIC_API MaterialSystem* CreateMaterialSystem(Renderer* device, ScriptContext* script, bool editormode)
{
	g_pMaterialSystem = ENF_NEW IMaterialSystem(device, script, editormode);
	return g_pMaterialSystem;
}

//-----------------------------------------------------------------------
MaterialSystem::MaterialSystem(): m_iMaterialSortVersion(0), m_iLastValidRender(-1)
{
}

//-----------------------------------------------------------------------
uint MaterialSystem::AddVariable(CStr name, DataVarType type, uint arraySize)
{
	uint index = GetVariableIndex(name);
	if(index == INDEX_NOT_FOUND)
	{
		VarDef def;
		def.Name = g_pStatic->AddString(name.cstr());
		def.Type = type;
		def.ArraySize = arraySize;

		index = m_Variables.Insert(def);
		switch(type)
		{
		case DVT_SCALAR:
			m_VariableIndices.Insert(m_ScalarVariables.GetCardinality());
			for(uint n = 0; n < arraySize; n++) m_ScalarVariables.Insert(float(0.0f));
			break;
		case DVT_VECTOR:
			m_VariableIndices.Insert(m_Vector4Variables.GetCardinality());
			for(uint n = 0; n < arraySize; n++) m_Vector4Variables.Insert(Vector4(0.0f));
			break;
		case DVT_MATRIX:
			m_VariableIndices.Insert(m_Matrix44Variables.GetCardinality());
			for(uint n = 0; n < arraySize; n++) m_Matrix44Variables.Insert(Matrix44(true));
			break;
		case DVT_MATRIX34:
			m_VariableIndices.Insert(m_Matrix34Variables.GetCardinality());
			for(uint n = 0; n < arraySize; n++) m_Matrix34Variables.Insert(Matrix34(true));
			break;
		case DVT_COLOR:
			m_VariableIndices.Insert(m_Vector4Variables.GetCardinality());
			for(uint n = 0; n < arraySize; n++) m_Vector4Variables.Insert(Vector4(0.0f));
			break;
		case DVT_INTEGER:
			m_VariableIndices.Insert(m_IntegerVariables.GetCardinality());
			for(uint n = 0; n < arraySize; n++) m_IntegerVariables.Insert(int(0));
			break;
		case DVT_BOOLEAN:
			m_VariableIndices.Insert(m_IntegerVariables.GetCardinality());
			for(uint n = 0; n < arraySize; n++) m_IntegerVariables.Insert(int(0));
			break;
		case DVT_TEXTURE:
			m_VariableIndices.Insert(m_TextureVariables.GetCardinality());
			for(uint n = 0; n < arraySize; n++) m_TextureVariables.Insert(NULL);
			break;
		default:
			enf_assert(!"Not implemented");
			break;
		}

	}
	return index;
}

//-----------------------------------------------------------------------
uint MaterialSystem::GetVariableIndex(CStr name) const
{
	//TODO: add hashtable
	const char* cname = g_pStatic->FindString(name.cstr());
	if(cname != NULL)
	{
		for(uint n = 0; n < m_Variables.GetCardinality(); n++)
		{
			if(m_Variables[n].Name == cname)
				return n;
		}
	}

	return INDEX_NOT_FOUND;
}

//-----------------------------------------------------------------------
void MaterialSystem::SetTexture(uint variable, Texture* texture)
{
	enf_assert(m_Variables[variable].Type == DVT_TEXTURE);
	Texture* prev = m_TextureVariables[m_VariableIndices[variable]];
	if(prev) prev->Release();
	m_TextureVariables[m_VariableIndices[variable]] = texture;
	if(texture) texture->AddRef();
}

//-----------------------------------------------------------------------
void MaterialSystem::SetMatrix(uint variable, const Matrix44& value, uint index)
{
	enf_assert(m_Variables[variable].Type == DVT_MATRIX);
	enf_assert(m_Variables[variable].ArraySize > index);
	m_Matrix44Variables[m_VariableIndices[variable] + index] = value;
}

//-----------------------------------------------------------------------
void MaterialSystem::SetMatrix34(uint variable, const Matrix34& value, uint index)
{
	enf_assert(m_Variables[variable].Type == DVT_MATRIX34);
	enf_assert(m_Variables[variable].ArraySize > index);
	m_Matrix34Variables[m_VariableIndices[variable] + index] = value;
}

//-----------------------------------------------------------------------
void MaterialSystem::SetVector(uint variable, const Vector4& value)
{
	enf_assert(m_Variables[variable].Type == DVT_VECTOR);
	m_Vector4Variables[m_VariableIndices[variable]] = value;
}

//-----------------------------------------------------------------------
void MaterialSystem::SetScalar(uint variable, float value)
{
	enf_assert(m_Variables[variable].Type == DVT_SCALAR);
	m_ScalarVariables[m_VariableIndices[variable]] = value;
}

//-----------------------------------------------------------------------
void MaterialSystem::SetBoolean(uint variable, bool value)
{
	enf_assert(m_Variables[variable].Type == DVT_BOOLEAN);
	m_IntegerVariables[m_VariableIndices[variable]] = value ? 1 : 0;
}

//-----------------------------------------------------------------------
void MaterialSystem::SetInteger(uint variable, int value)
{
	enf_assert(m_Variables[variable].Type == DVT_INTEGER);
	m_IntegerVariables[m_VariableIndices[variable]] = value;
}

//-----------------------------------------------------------------------
void MaterialSystem::DrawIndexedInstancedMesh(const IMesh* mesh, uint numInstances, bool dirty)
{
	if(ENF_DIAG_RBOOL(DM_REND_NORENDER))
	{
#if defined(ENF_XENON)
		g_pRender->DrawIndexedPrimitive((PrimitiveType)mesh->m_Type,
			mesh->m_iFirstVertex,
			0,
			0,
			mesh->m_iFirstIndex,
			mesh->m_iPrimitiveCount * numInstances);
#elif defined(ENF_PS3)
		g_pRender->SetStreamSourceFreqOp(0, SFO_MODULO, mesh->m_iNumVertices);
		g_pRender->SetStreamSourceFreqOp(1, SFO_DIVIDE, mesh->m_iNumVertices);

		g_pRender->DrawIndexedPrimitive(
			(PrimitiveType) mesh->m_Type,
			mesh->m_iFirstVertex,
			0,
			mesh->m_iNumVertices,
			mesh->m_iFirstIndex,
			mesh->m_iPrimitiveCount * numInstances);
#endif
	}

	if(dirty) //has to be refreshed!
		m_iLastValidRender = -1;
}

//-----------------------------------------------------------------------
void MaterialSystem::DrawInstancedMesh(const IMesh* mesh, uint numInstances, bool dirty)
{
	if(ENF_DIAG_RBOOL(DM_REND_NORENDER))
	{
		enf_assert(mesh->m_pIB == NULL);
#if defined(ENF_XENON)
		g_pRender->DrawPrimitive((PrimitiveType)mesh->m_Type, mesh->m_iFirstVertex, mesh->m_iPrimitiveCount * numInstances);
#elif defined(ENF_PS3)
		g_pRender->SetStreamSourceFreqOp(0, SFO_MODULO, mesh->m_iNumVertices);
		g_pRender->SetStreamSourceFreqOp(1, SFO_DIVIDE, mesh->m_iNumVertices);

		g_pRender->DrawPrimitive(
			(PrimitiveType) mesh->m_Type, 
			mesh->m_iFirstVertex, 
			mesh->m_iPrimitiveCount * numInstances);
#endif
	}

	if(dirty) //has to be refreshed!
		m_iLastValidRender = -1;
}

//-----------------------------------------------------------------------
void MaterialSystem::DrawIndexedMesh(const IMesh* mesh, bool dirty)
{
	if(ENF_DIAG_RBOOL(DM_REND_NORENDER))
	{
		g_pRender->DrawIndexedPrimitive((PrimitiveType)mesh->m_Type,
			mesh->m_iFirstVertex,
			0,
			mesh->m_iNumVertices,
			mesh->m_iFirstIndex,
			mesh->m_iPrimitiveCount);
	}

	if(dirty) //has to be refreshed!
		m_iLastValidRender = -1;
}

//-----------------------------------------------------------------------
void MaterialSystem::DrawMesh(const IMesh* mesh, bool dirty)
{
	if(ENF_DIAG_RBOOL(DM_REND_NORENDER))
	{
		enf_assert(mesh->m_pIB == NULL);
		g_pRender->DrawPrimitive((PrimitiveType)mesh->m_Type, mesh->m_iFirstVertex, mesh->m_iPrimitiveCount);
	}

	if(dirty) //has to be refreshed!
		m_iLastValidRender = -1;
}


}