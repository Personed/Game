//-----------------------------------------------------------------------------
// File: enf_vertexdeclaration.cpp
//
// Desc: Renderer class, vertex declaration abstraction
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "render/enf_irendmain.h"

namespace enf
{

//-----------------------------------------------------------------------
#define END_ELEMENT(elem) (elem.Stream == VertexDeclaration::DECL_END)

//-----------------------------------------------------------------------
void VertexDeclaration::Begin()
{
	Declaration[0].Stream = DECL_END;
	HashCode = 0;
	Stride[0] = Stride[1] = Stride[2] = Stride[3] = 0;

	#ifdef ENF_PS3
		//not needed, for tests now
		for(uint n = 0; n < VertexDeclaration::MAX_DECLARATION; n++)
		{
			Declaration[n].GCMStruct.GCMVertexDecl = 0xFFFFFFFF;
		}
	#endif
}

//-----------------------------------------------------------------------
void Renderer::SetVertexDeclaration(IVertexDeclaration pdecl)
{
#if defined(ENF_PS3)
	if(m_iVertexDeclaration != (uint)pdecl)
	{
		m_iCommitMask |= GCMCM_VERTEXDECLARATION;
		m_iVertexDeclaration = (uint)pdecl;
	}
#elif defined(ENF_OGL)
	if(m_iVertexDeclaration != (uint)pdecl)
	{
		m_iCommitMask |= GLCM_VERTEXDECLARATION;
		m_iVertexDeclaration = (uint)pdecl;
	}
#endif
}

//-----------------------------------------------------------------------
uint Length(VertexDeclDataType type)
{
	switch(type)
	{
	case VSDT_FLOAT1:
		return 4;
	case VSDT_FLOAT2:
		return 4 * 2;
	case VSDT_FLOAT3:
		return 4 * 3;
	case VSDT_FLOAT4:
		return 4 * 4;
	case VSDT_COLOR:
		return 4;
	case VSDT_UBYTE4:
		return 4;
	case VSDT_SHORT2:
		return 2 * 2;
	case VSDT_SHORT4:
		return 2 * 4;

	case VSDT_UBYTE4N:
		return 4;
	case VSDT_SHORT2N:
		return 2 * 2;
	case VSDT_SHORT4N:
		return 2 * 4;
	case VSDT_USHORT2N:
		return 2 * 2;
	case VSDT_USHORT4N:
		return 2 * 4;
	case VSDT_UDEC3:
		return 4;
	case VSDT_DEC3N:
		return 4;
	case VSDT_FLOAT16_2:
		return 2 * 2;
	case VSDT_FLOAT16_4:
		return 2 * 4;
	}
	return 0;
}

//-----------------------------------------------------------------------
IVertexDeclaration IRenderer::CreateVertexDeclaration(const VertexDeclaration& decl)
{
	for(uint n = 0; n < m_pVertexDeclarations.GetCardinality(); n++)
	{
		if(m_pVertexDeclarations[n]->IsEqual(decl))
			return reinterpret_cast<IVertexDeclaration>(static_cast<uint64>(n + 1));
	}

	VertexDeclaration* newdecl = NULL;

#if defined(ENF_PS3)
	newdecl = ENF_NEW VertexDeclaration();

	//copy declaration
	*newdecl = decl;

	for(uint n = 0; n < VertexDeclaration::MAX_DECLARATION; n++)
	{
		VertexDeclaration::VertexElement& elem = newdecl->Declaration[n];

		if(END_ELEMENT(elem))
		{
			break;
		}

		elem.GCMStruct.Normalized = 0;
		switch(elem.Type)
		{
		case VSDT_FLOAT1:
		case VSDT_FLOAT2:
		case VSDT_FLOAT3:
		case VSDT_FLOAT4:
			elem.GCMStruct.Type = CELL_GCM_VERTEX_F;
			elem.GCMStruct.ElemsCount = elem.Type - VSDT_FLOAT1 + 1;
			break;
		case VSDT_COLOR:	// 4D packed unsigned bytes mapped to 0. to 1. range
		case VSDT_UBYTE4N:  // Each of 4 bytes is normalized by dividing to 255.0
			elem.GCMStruct.Type = CELL_GCM_VERTEX_UB;
			elem.GCMStruct.Normalized = 1;
			elem.GCMStruct.ElemsCount = 4;
			break;
		case VSDT_UBYTE4:	// 4D unsigned byte
			elem.GCMStruct.Type = CELL_GCM_VERTEX_UB256;
			elem.GCMStruct.ElemsCount = 4;
			break;
		case VSDT_SHORT2:	// 2D signed short expanded to (value, value, 0., 1.)
			elem.GCMStruct.Type = CELL_GCM_VERTEX_S32K;
			elem.GCMStruct.ElemsCount = 2;
			break;
		case VSDT_SHORT4:	// 4D signed short
			elem.GCMStruct.Type = CELL_GCM_VERTEX_S32K;
			elem.GCMStruct.ElemsCount = 4;
			break;
		case VSDT_SHORT2N:  // 2D signed short normalized (v[0]/32767.0,v[1]/32767.0,0,1)
			elem.GCMStruct.Type = CELL_GCM_VERTEX_S1;
			elem.GCMStruct.Normalized = 1;
			elem.GCMStruct.ElemsCount = 2;
			break;
		case VSDT_SHORT4N:  // 4D signed short normalized (v[0]/32767.0,v[1]/32767.0,v[2]/32767.0,v[3]/32767.0)
			elem.GCMStruct.Type = CELL_GCM_VERTEX_S1;
			elem.GCMStruct.Normalized = 1;
			elem.GCMStruct.ElemsCount = 4;
			break;
		case VSDT_USHORT2N:  // 2D unsigned short normalized (v[0]/65535.0,v[1]/65535.0,0,1)
			enf_assert2(0, "VSDT_USHORT2N not implemented");
			elem.GCMStruct.Type = CELL_GCM_VERTEX_S1;
			elem.GCMStruct.Normalized = 1;
			elem.GCMStruct.ElemsCount = 2;
			break;
		case VSDT_USHORT4N:  // 4D unsigned short normalized (v[0]/65535.0,v[1]/65535.0,v[2]/65535.0,v[3]/65535.0)
			enf_assert2(0, "VSDT_USHORT4N not implemented");
			elem.GCMStruct.Type = CELL_GCM_VERTEX_S1;
			elem.GCMStruct.Normalized = 1;
			elem.GCMStruct.ElemsCount = 4;
			break;
		case VSDT_FLOAT16_2:  // Two 16-bit floating point values, expanded to (value, value, 0, 1)
			elem.GCMStruct.Type = CELL_GCM_VERTEX_SF;
			elem.GCMStruct.ElemsCount = 2;
			break;
		case VSDT_FLOAT16_4:  // Four 16-bit floating point values
			elem.GCMStruct.Type = CELL_GCM_VERTEX_SF;
			elem.GCMStruct.ElemsCount = 4;
			break;
		default:
			enf_assert2(0, "Not implemented");
		};

		switch(elem.Usage)
		{
		case VSDU_POSITION:
			elem.GCMStruct.Usage = 0;
			break;
		case VSDU_NORMAL:
			elem.GCMStruct.Usage = 2;
			break;
		case VSDU_TANGENT:
			elem.GCMStruct.Usage = 14;
			break;
		case VSDU_BINORMAL:
			elem.GCMStruct.Usage = 15;
			break;
		case VSDU_COLOR:
			elem.GCMStruct.Usage = 3 + elem.UsageIndex;
			break;
		case VSDU_TEXCOORD:
			elem.GCMStruct.Usage = 8 + elem.UsageIndex;
			break;
		case VSDU_WEIGHTS:
			elem.GCMStruct.Usage = 1;
			break;
		case VSDU_INDICES:
			elem.GCMStruct.Usage = 7;
			break;
		case VSDU_PSIZE:
			elem.GCMStruct.Usage = 6;
			break;
		};
	}

	m_pVertexDeclarations.Insert(newdecl);

	return reinterpret_cast<IVertexDeclaration>(static_cast<uint64>(m_pVertexDeclarations.GetCardinality()));
#elif defined(ENF_OGL)
	newdecl = ENF_NEW VertexDeclaration();

	//copy declaration
	*newdecl = decl;
	m_pVertexDeclarations.Insert(newdecl);

	return reinterpret_cast<IVertexDeclaration>(static_cast<uint64>(m_pVertexDeclarations.GetCardinality()));
#endif
}

//-----------------------------------------------------------------------
bool VertexDeclaration::IsEqual(const VertexDeclaration& other) const
{
	if(HashCode != other.HashCode)
		return false;

	
	for(uint s = 0; Stride[s] != 0 && s < MAX_STREAMS; s++)
	{
		if(Stride[s] != other.Stride[s])
			return false;
	}

	for(uint n = 0; n < MAX_DECLARATION; n++)
	{
		if(END_ELEMENT(Declaration[n]) && END_ELEMENT(other.Declaration[n]))
			return true;
		if(memcmp(&Declaration[n], &other.Declaration[n], sizeof(VertexElement)) != 0)
			break;
	}
	return false;
}

//-----------------------------------------------------------------------
void VertexDeclaration::Seek(uint stream, int bytes)
{
	enf_assert(stream < MAX_STREAMS);
	Stride[stream] += bytes;
	HashCode += bytes << 16;
}

//-----------------------------------------------------------------------
void VertexDeclaration::Append(uint stream, VertexDeclDataType type, VertexDeclUsage usage, uint index)
{
	VertexElement elem;

	enf_assert(stream < MAX_STREAMS);
	elem.Stream = (ubyte)stream;
	elem.Type = (ubyte)type;
	elem.Method = 0;
	elem.Usage = (ubyte)usage;
	elem.UsageIndex = (ubyte)index;
	elem.Offset = Stride[stream];
	Append(elem);

	Stride[stream] += Length(type);

	HashCode = 0;
}

//-----------------------------------------------------------------------
void VertexDeclaration::Append(const VertexElement& elem)
{
	for(uint n = 0; n < MAX_DECLARATION; n++)
	{
		if(END_ELEMENT(Declaration[n]))
		{
			Declaration[n] = elem;
			Declaration[n + 1].Stream = DECL_END;
			break;
		}
	}
}

//-----------------------------------------------------------------------
uint VertexDeclaration::Hash()
{
	HashCode = 0;

	for(uint n = 0; n < MAX_DECLARATION; n++)
	{
		if(END_ELEMENT(Declaration[n]))
			break;

		HashCode += Declaration[n].Stream;
		HashCode += Declaration[n].Type;
		HashCode += Declaration[n].Method;
		HashCode += Declaration[n].Usage;
		HashCode += Declaration[n].UsageIndex;
	}

	return HashCode;
}

//-----------------------------------------------------------------------
void VertexDeclaration::End()
{
	Hash();
}
//-----------------------------------------------------------------------
VertexDeclaration VertexDeclaration::operator=(const VertexDeclaration& other)
{
	for(uint n = 0; n < MAX_DECLARATION; n++)
	{
		Declaration[n] = other.Declaration[n];

		if(END_ELEMENT(Declaration[n]))
			break;
	}

	HashCode = other.HashCode;
	for(uint n = 0; n < MAX_STREAMS; n++)
		Stride[n] = other.Stride[n];
	return *this;
}

//-----------------------------------------------------------------------
VertexDeclaration::VertexDeclaration()
{
	Begin();
}

//-----------------------------------------------------------------------
VertexDeclaration::~VertexDeclaration()
{
}

//-----------------------------------------------------------------------

}
