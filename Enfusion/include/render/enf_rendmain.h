//-----------------------------------------------------------------------------
// File: render/enf_rendmain.h
//
// Desc: Main render class
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_RENDERMAIN_H
#define ENF_RENDERMAIN_H

#include "enf_config.h"
#include "render/enf_rendertypes.h"
#include "Render/enf_renderenums.h"
#include "engine/enf_module.h"
#include "common/enf_mathlib.h"
#include "common/enf_mathlib2D.h"
#include "common/enf_color.h"

#if defined(ENF_OGL)
	#include <GL/glew.h>
	#ifdef WIN32
		#include <GL/wglew.h>
	#endif
	#include <cg/cg.h>
	#include <cg/cgGL.h>
#endif


/*!
Legacy managed-resource restrictions are not used by the DX11 client.
*/
#define ENF_DISABLE_MANAGED_RESOURCES

/*!
if defined, no check for redundant set of render states, filter states, textures etc... is done
*/
//#define NO_REDUNDANT_RS_CHECK

namespace enf
{

class COutputStream;
class CInputStream;
class Renderer;
struct Define;
class Texture;

/** @defgroup Renderer Render device
@{*/

extern PUBLIC_API Renderer* g_pRender;

ENF_INLINEFUNC Renderer* GetRender()
{
	return g_pRender;
}

PUBLIC_API Renderer* CreateRenderer();


#define ENF_RENDSTATIC



const uint RENDER_ONE_INSTANCE_DATA = 1;


//-----------------------------------------------------------------------
enum RenderQueryType
{
	QUERYTYPE_OCCLUSION	= 1,
	QUERYTYPE_EVENT		= 2,
};

//-----------------------------------------------------------------------
enum RenderCmpFunc
{
	CMP_NEVER			= 0,
	CMP_LESS				= 1,
	CMP_EQUAL			= 2,
	CMP_LESSEQUAL		= 3,
	CMP_GREATER			= 4,
	CMP_NOTEQUAL		= 5,
	CMP_GREATEREQUAL	= 6,
	CMP_ALWAYS			= 7
};

//-----------------------------------------------------------------------
enum RenderStencilOp
{
	STENCILOP_KEEP		= 0,
	STENCILOP_ZERO		= 1,
	STENCILOP_REPLACE	= 2,
	STENCILOP_INCRSAT	= 3,
	STENCILOP_DECRSAT	= 4,
	STENCILOP_INVERT	= 5,
	STENCILOP_INCR		= 6,
	STENCILOP_DECR		= 7,
};

//-----------------------------------------------------------------------
class DepthStencilStateDef
{
public:
	union
	{
		struct  
		{
			uint		DepthEnable							:1;
			uint		DepthWriteEnable				:1;
			uint		DepthFunc								:4;
			uint		StencilEnable						:1;
			uint		TwoSidedStencilEnable		:1;
			uint		FrontStencilFailOp			:4;
			uint		FrontStencilDepthFailOp	:4;
			uint		FrontStencilPassOp			:4;
			uint		FrontStencilFunc				:4;

			uint		BackStencilFailOp				:4;
			uint		BackStencilDepthFailOp	:4;
			uint		BackStencilPassOp				:4;
			uint		BackStencilFunc					:4;
			uint		StencilReadMask					:8;
			uint		StencilWriteMask				:8;
		};
		uint64	_Value;
	};

	DepthStencilStateDef(): _Value(0) 
	{}

	//!Set depth state with disabled stencil
	DepthStencilStateDef(bool depthEnable, bool depthWrite, RenderCmpFunc depthFunc);

	//!Set depth and front-sided stencil state
	DepthStencilStateDef(bool depthEnable, bool depthWrite, RenderCmpFunc depthFunc,
			RenderStencilOp frontStencilFailOp,
			RenderStencilOp frontStencilDepthFailOp,
			RenderStencilOp fontStencilPassOp,
			RenderCmpFunc frontStencilFunc,
			ubyte stencilReadMask = 0xff,
			ubyte stencilWriteMask = 0xff);

	//!Set depth and double-sided stencil state
	DepthStencilStateDef(bool depthEnable, bool depthWrite, RenderCmpFunc depthFunc,
			RenderStencilOp frontStencilFailOp,
			RenderStencilOp frontStencilDepthFailOp,
			RenderStencilOp fontStencilPassOp,
			RenderCmpFunc frontStencilFunc,
			RenderStencilOp backStencilFailOp,
			RenderStencilOp backStencilDepthFailOp,
			RenderStencilOp backStencilPassOp,
			RenderCmpFunc backStencilFunc,
			ubyte stencilReadMask = 0xff,
			ubyte stencilWriteMask = 0xff);
};

const DepthStencilStateHandle DSS_NONE(0);
const DepthStencilStateHandle DSS_ZWRITE(1);
const DepthStencilStateHandle DSS_ZENABLE_LE(2);
const DepthStencilStateHandle DSS_ZENABLE_LE_ZWRITE(3);
const DepthStencilStateHandle DSS_ZWRITE_SHADOWRECEIVESTENCIL(4);
const DepthStencilStateHandle DSS_ZENABLE_LE_SHADOWRECEIVESTENCIL(5);
const DepthStencilStateHandle DSS_ZENABLE_LE_ZWRITE_SHADOWRECEIVESTENCIL(6);
const DepthStencilStateHandle DSS_ZENABLE_L_SHADOWCASTSTENCIL(7);
const DepthStencilStateHandle DSS_ZENABLE_L(8);
const DepthStencilStateHandle DSS_ZENABLE_LE_DEPTHCOMPLEX(9);
const DepthStencilStateHandle DSS_ZENABLE_LE_ZWRITE_DEPTHCOMPLEX(10);
const DepthStencilStateHandle DSS_ZENABLE_E(11);
const DepthStencilStateHandle DSS_ZENABLE_L_ZWRITE(12);


//!Optional feature. Shouldn't be enabled unless necessary!
//#define ENF_SEPARATE_ALPHABLEND_ENABLE


//-----------------------------------------------------------------------
/*
Supported renderstates
*/
//-----------------------------------------------------------------------
enum RenderState
{
	RS_FILLMODE = 1,
	RS_ALPHATESTENABLE,
	RS_CULLMODE,
	RS_ALPHAREF,
	RS_ALPHAFUNC,
	RS_POINTSIZE,
	RS_POINTSIZE_MIN,
	RS_POINTSPRITEENABLE,
	RS_MULTISAMPLEANTIALIAS,
	RS_MULTISAMPLEMASK,
	RS_POINTSIZE_MAX,
	RS_COLORWRITEENABLE,
	RS_SCISSORTESTENABLE,
	RS_SLOPESCALEDEPTHBIAS,
	RS_COLORWRITEENABLE1,
	RS_COLORWRITEENABLE2,
	RS_COLORWRITEENABLE3,
	RS_BLENDFACTOR,
	RS_DEPTHBIAS,
	RS_BADVALUE = 0xffffffff
};

//-----------------------------------------------------------------------
enum RenderFillMode
{
	FILL_POINT			= 1,
	FILL_WIREFRAME	= 2,
	FILL_SOLID			= 3,
};

//-----------------------------------------------------------------------
enum RenderCullMode
{
	CULL_NONE			= 1,
	CULL_CW				= 2,
	CULL_CCW			= 3,
};

//-----------------------------------------------------------------------
enum RenderSamplerStateType
{
	SAMP_MIPMAPLODBIAS  = 8,
	SAMP_MAXMIPLEVEL    = 9,
	SAMP_MAXANISOTROPY  = 10,
	SAMP_SRGBTEXTURE    = 11,
};

//-----------------------------------------------------------------------
enum RenderBlendFactor
{
	BLEND_ZERO          = 1,
	BLEND_ONE           = 2,
	BLEND_SRCCOLOR      = 3,
	BLEND_INVSRCCOLOR   = 4,
	BLEND_SRCALPHA      = 5,
	BLEND_INVSRCALPHA   = 6,
	BLEND_DESTALPHA			= 7,
	BLEND_INVDESTALPHA  = 8,
	BLEND_DESTCOLOR     = 9,
	BLEND_INVDESTCOLOR  = 10,
	BLEND_SRCALPHASAT   = 11,
	BLEND_BLENDFACTOR   = 14,
	//BLEND_INVBLENDFACTOR  = 15
};

//-----------------------------------------------------------------------
enum RenderBlendOperator
{
	BLENDOP_ADD         = 1,
	BLENDOP_SUBTRACT    = 2,
	BLENDOP_REVSUBTRACT = 3,
	BLENDOP_MIN         = 4,
	BLENDOP_MAX         = 5
};

//-----------------------------------------------------------------------
enum RenderTextureFilterState
{
	TEXF_NONE					= 0,
	TEXF_POINT,
	TEXF_LINEAR,
	TEXF_ANISOTROPIC,
};

//-----------------------------------------------------------------------
enum RenderTextureAddress
{
	TADDRESS_WRAP				= 1,
	TADDRESS_MIRROR			= 2,
	TADDRESS_CLAMP			= 3,
	TADDRESS_BORDER			= 4,
	TADDRESS_MIRRORONCE	= 5,
};


#pragma pack(push, 4)

//-----------------------------------------------------------------------
/*
Blend state definition. Common with XBox360
*/
//-----------------------------------------------------------------------
struct BlendState
{
	union
	{
		struct 
		{
			uint SrcBlend				: 5;
			uint BlendOp				: 3;
			uint DestBlend			: 5;
			uint								: 3;
			uint SrcBlendAlpha	: 5;
			uint BlendOpAlpha		: 3;
			uint DestBlendAlpha : 5;
			uint								: 3;
		};
		uint	Value;
	};

	ENF_INLINE bool operator==(const BlendState& state) const 
	{ 
		return Value == state.Value; 
	}

	ENF_INLINE BlendState():
		SrcBlend(BLEND_ONE), BlendOp(BLEND_ZERO), DestBlend(BLENDOP_ADD),
		SrcBlendAlpha(BLEND_ONE), BlendOpAlpha(BLEND_ZERO), DestBlendAlpha(BLENDOP_ADD)
	{}

	ENF_INLINE BlendState(uint blendValue): Value(blendValue) {}

	ENF_INLINE BlendState(RenderBlendFactor srcBlend, RenderBlendOperator blendOp, RenderBlendFactor destBlend):
		SrcBlend(srcBlend), BlendOp(blendOp), DestBlend(destBlend),
		SrcBlendAlpha(srcBlend), BlendOpAlpha(blendOp), DestBlendAlpha(destBlend)
	{
		srcBlend = srcBlend;
	}

	#ifdef ENF_SEPARATE_ALPHABLEND_ENABLE
		ENF_INLINE BlendState(RenderBlendFactor srcBlend, RenderBlendOperator blendOp, RenderBlendFactor destBlend, RenderBlendFactor alphaSrcBlend, RenderBlendOperator alphaBlendOp, RenderBlendFactor alphaDestBlend):
			SrcBlend(srcBlend),
			BlendOp(blendOp),
			DestBlend(destBlend),
			SrcBlendAlpha(alphaSrcBlend),
			BlendOpAlpha(alphaBlendOp),
			DestBlendAlpha(alphaDestBlend)
		{}
	#endif
};

#pragma pack(pop)

const BlendState BS_SRCALPHA_ADD_INVSRCALPHA(BLEND_SRCALPHA, BLENDOP_ADD, BLEND_INVSRCALPHA);
const BlendState BS_SRCALPHA_ADD_ONE(BLEND_SRCALPHA, BLENDOP_ADD, BLEND_ONE);
const BlendState BS_SRCALPHA_ADD_ZERO(BLEND_SRCALPHA, BLENDOP_ADD, BLEND_ZERO);
const BlendState BS_ZERO_ADD_SRCCOLOR(BLEND_ZERO, BLENDOP_ADD, BLEND_SRCCOLOR);
const BlendState BS_ONE_ADD_ONE(BLEND_ONE, BLENDOP_ADD, BLEND_ONE);
const BlendState BS_ONE_ADD_INVSRCALPHA(BLEND_ONE, BLENDOP_ADD, BLEND_INVSRCALPHA);
const BlendState BS_ONE_ADD_ZERO(BLEND_ONE, BLENDOP_ADD, BLEND_ZERO);


//-----------------------------------------------------------------------
/*
Definition replacing native sampler states
*/
//-----------------------------------------------------------------------
struct SamplerFilterState
{
	union
	{
		struct
		{
			uint MinFilter : 4;
			uint MagFilter : 4;
			uint MipFilter : 4;
			uint MaxAniso  : 4;
		};
		uint	Value;
	};

	SamplerFilterState(uint value): Value(value) 
	{}

	SamplerFilterState(uint minFilter, uint magFilter, uint mipFilter, uint maxAniso): MinFilter(minFilter), MagFilter(magFilter), MipFilter(mipFilter), MaxAniso(maxAniso - 1)
	{
		enf_assert(maxAniso >= 1 && maxAniso <= 16);
	}
};


const SamplerFilterStateHandle SFS_POINT_POINT_NONE(SamplerFilterState(TEXF_POINT, TEXF_POINT, TEXF_NONE, 1).Value);
const SamplerFilterStateHandle SFS_LINEAR_LINEAR_NONE(SamplerFilterState(TEXF_LINEAR, TEXF_LINEAR, TEXF_NONE, 1).Value);
const SamplerFilterStateHandle SFS_LINEAR_POINT_NONE(SamplerFilterState(TEXF_LINEAR, TEXF_POINT, TEXF_NONE, 1).Value);
const SamplerFilterStateHandle SFS_POINT_LINEAR_NONE(SamplerFilterState(TEXF_POINT, TEXF_LINEAR, TEXF_NONE, 1).Value);
const SamplerFilterStateHandle SFS_LINEAR_LINEAR_LINEAR(SamplerFilterState(TEXF_LINEAR, TEXF_LINEAR, TEXF_LINEAR, 1).Value);
const SamplerFilterStateHandle SFS_POINT_LINEAR_LINEAR(SamplerFilterState(TEXF_POINT, TEXF_LINEAR, TEXF_LINEAR, 1).Value);		///default RSX state


//-----------------------------------------------------------------------
/*
Definition replacing native sampler states
*/
//-----------------------------------------------------------------------
struct SamplerAddressState
{
	static const uint SAS_FLAG_3D			= 1;
	static const uint SAS_FLAG_BORDER	= 2;

	union
	{
		struct 
		{
			uint	Flags			: 4; //! SAS_FLAG_3D = 3D, SAS_FLAG_BORDER = BORDER
			uint	UAddress	: 4;
			uint	VAddress	: 4;
			uint	WAddress	: 4;
			uint	Dummy			: 16;

			uint	BorderColor;
		};
		int64 Value;
	};

	SamplerAddressState(): Flags(0), UAddress(0), VAddress(0), WAddress(0), Dummy(0), BorderColor(0) 
	{}

	SamplerAddressState(uint uAddress, uint vAddress, uint borderColor);
	SamplerAddressState(uint uAddress, uint vAddress, uint wAddress, uint borderColor);
};


const SamplerAddressStateHandle SAS_CLAMP_CLAMP(0);
const SamplerAddressStateHandle SAS_CLAMP_CLAMP_CLAMP(1);
const SamplerAddressStateHandle SAS_WRAP_WRAP(2);
const SamplerAddressStateHandle SAS_WRAP_WRAP_WRAP(3);
const SamplerAddressStateHandle SAS_BORDER_BORDER_ONE(4);
const SamplerAddressStateHandle SAS_BORDER_BORDER_ZERO(5);
const SamplerAddressStateHandle SAS_WRAP_WRAP_CLAMP(6);
const SamplerAddressStateHandle SAS_CUBE_MAP(SAS_WRAP_WRAP_WRAP);


//-----------------------------------------------------------------------
enum ColorWriteEnable
{
	COLORWRITEENABLE_RED		= (1L<<0),
	COLORWRITEENABLE_GREEN	= (1L<<1),
	COLORWRITEENABLE_BLUE		= (1L<<2),
	COLORWRITEENABLE_ALPHA	= (1L<<3),
	COLORWRITEENABLE_ALL		= 0xf
};


const uint MAX_RENDER_TARGETS		= 4;
const uint MAX_TEXTURES					= 16;
const uint MAX_VERTEX_TEXTURES	= 4;
const uint MAX_STREAMS					= 16;


//-----------------------------------------------------------------------
/*
Rendering stats. See Renderer::GetStats
*/
//-----------------------------------------------------------------------
struct RenderStats
{
	uint	NumTriangles;
	uint	NumVertexes;
	uint	NumPrimitives;
	uint	BSChanges;
	uint	DSSChanges;
	uint	RSChanges;
	uint	SSChanges;
	uint	TexChanges;
	uint	VBChanges;
	uint	IBChanges;
	uint	PSChanges;
	uint	VSChanges;
	uint	AllocVertices;
	uint	BeginVerticesCount;

	ENF_INLINE void Clear()
	{
		NumTriangles = 0;
		NumVertexes = 0;
		NumPrimitives = 0;
		BSChanges = 0;
		DSSChanges = 0;
		RSChanges = 0;
		SSChanges = 0;
		TexChanges = 0;
		VBChanges = 0;
		IBChanges = 0;
		PSChanges = 0;
		VSChanges = 0;
		AllocVertices = 0;
		BeginVerticesCount = 0;
	}

	RenderStats()
	{
		Clear();
	}

	ENF_INLINE void AddIndexedPrimitive(uint NumVertices, uint PrimitiveCount)
	{
		NumVertexes += NumVertices;
		NumTriangles += PrimitiveCount;
		NumPrimitives++;
	}

	ENF_INLINE void AddVertices(PrimitiveType type, uint NumVertices)
	{
		//TODO: should get PrimitiveType and count number of triangles from vertices
		NumVertexes += NumVertices;
		NumPrimitives++;
	}

	ENF_INLINE void AddPrimitive(PrimitiveType Type, uint PrimitiveCount)
	{
		NumPrimitives++;
		switch(Type)
		{
		case PT_POINTLIST:
			NumVertexes += PrimitiveCount;
			break;
		case PT_LINELIST:
			NumVertexes += PrimitiveCount * 2;
			break;
		case PT_LINESTRIP:
			NumVertexes += PrimitiveCount + 1;
			break;
		case PT_TRIANGLESTRIP:
			NumVertexes += PrimitiveCount + 2;
			NumTriangles += PrimitiveCount;
			break;
		case PT_TRIANGLEFAN:
			NumVertexes += PrimitiveCount + 2;
			NumTriangles += PrimitiveCount;
			break;
		case PT_TRIANGLELIST:
			NumVertexes += PrimitiveCount * 3;
			NumTriangles += PrimitiveCount;
			break;
#ifdef ENF_RENDERQUADS
		case PT_QUADLIST:
			NumVertexes += PrimitiveCount * 3;
			NumTriangles += PrimitiveCount * 2;
			break;
#endif
		}
	}
};


//-----------------------------------------------------------------------
/*!
Defines what parts of RenderTarget are cleared. See Renderer::ClearRenderTarget()
*/
//-----------------------------------------------------------------------
enum RTClearFlags
{
	RTCLEAR_ZBUFFER	= BITF(0),
	RTCLEAR_STENCIL	= BITF(1),
	RTCLEAR_TARGET	= BITF(2)
};

//-----------------------------------------------------------------------
enum VertexDeclDataType
{
	VSDT_FLOAT1			= 0,	// 1D float expanded to (value, 0., 0., 1.)
	VSDT_FLOAT2			= 1,	// 2D float expanded to (value, value, 0., 1.)
	VSDT_FLOAT3			= 2,	// 3D float expanded to (value, value, value, 1.)
	VSDT_FLOAT4			= 3,	// 4D float
	VSDT_COLOR			= 4,	// 4D packed unsigned bytes mapped to 0. to 1. range
	VSDT_UBYTE4			= 5,	// 4D unsigned byte
	VSDT_SHORT2			= 6,	// 2D signed short expanded to (value, value, 0., 1.)
	VSDT_SHORT4			= 7,	// 4D signed short
	VSDT_UBYTE4N		= 8,  // Each of 4 bytes is normalized by dividing to 255.0
	VSDT_SHORT2N		= 9,  // 2D signed short normalized (v[0]/32767.0,v[1]/32767.0,0,1)
	VSDT_SHORT4N		= 10,  // 4D signed short normalized (v[0]/32767.0,v[1]/32767.0,v[2]/32767.0,v[3]/32767.0)
	VSDT_USHORT2N		= 11,  // 2D unsigned short normalized (v[0]/65535.0,v[1]/65535.0,0,1)
	VSDT_USHORT4N		= 12,  // 4D unsigned short normalized (v[0]/65535.0,v[1]/65535.0,v[2]/65535.0,v[3]/65535.0)
	VSDT_UDEC3			= 13,  // 3D unsigned 10 10 10 format expanded to (value, value, value, 1)
	VSDT_DEC3N			= 14,  // 3D signed 10 10 10 format normalized and expanded to (v[0]/511.0, v[1]/511.0, v[2]/511.0, 1)
	VSDT_FLOAT16_2	= 15,  // Two 16-bit floating point values, expanded to (value, value, 0, 1)
	VSDT_FLOAT16_4	= 16,  // Four 16-bit floating point values
};

//-----------------------------------------------------------------------
enum VertexDeclUsage
{
	VSDU_POSITION,
	VSDU_NORMAL,
	VSDU_TANGENT,
	VSDU_BINORMAL,
	VSDU_COLOR,
	VSDU_TEXCOORD,
	VSDU_WEIGHTS,
	VSDU_INDICES,
	VSDU_PSIZE
};


//-----------------------------------------------------------------------
/*!
Encapsulation of VertexDeclaration. here you can define elements, types 
and strides of vertex streams.
*/
//-----------------------------------------------------------------------
class PUBLIC_API VertexDeclaration
{
	friend class Renderer;
	friend class IRenderer;
	friend class VertexShader;
	
	static const uint MAX_DECLARATION = 16;
	static const uint DECL_END				= 0xff;

	struct VertexElement
	{
		ushort	Stream;
		ushort	Offset;
		ubyte		Type;
		ubyte		Method;
		ubyte		Usage;
		ubyte		UsageIndex;
	};

	VertexElement	Declaration[MAX_DECLARATION];

	uint Stride[MAX_STREAMS];

	uint HashCode;
	uint Hash();
	void Append(const VertexElement& elem);

public:
	ENF_DECLARE_MMOBJECT(VertexDeclaration);

	VertexDeclaration();
	~VertexDeclaration();

	void Begin();
	void End();

	VertexDeclaration operator=(const VertexDeclaration& other);
	bool IsEqual(const VertexDeclaration& other) const;
	void Append(uint stream, VertexDeclDataType type, VertexDeclUsage usage, uint index);
	void Seek(uint stream, int bytes);

	uint GetStride(uint stream) const
	{
		enf_assert(stream < MAX_STREAMS);
		return Stride[stream];
	}
};


//-----------------------------------------------------------------------
/*
//!Structure for holding information about a display mode
*/
//-----------------------------------------------------------------------
struct RenderModeInfo
{
	uint					Width;      // Screen width in this mode
	uint					Height;     // Screen height in this mode
	uint					dwBehavior; // Hardware / Software / Mixed vertex processing
	TextureFormat	Format;     // Pixel format in this mode
	TextureFormat	DepthStencilFormat; // Which depth/stencil format to use with this mode
	bool					Custom;
	bool					AllowWindowed;	//allow to run in window mode
};


//-----------------------------------------------------------------------
/*!
Structure for holding information about a Direct3D device, including
a list of modes compatible with this device
*/
//-----------------------------------------------------------------------
struct RenderDeviceInfo
{
	// Device data
	const char*			m_strDesc;         // Name of this device
	// Current state
	uint					m_iCurrentMode;

	bool					m_bCanDoWindowed;  // Whether this device can work in windowed mode
	bool					m_bWindowed;
};


//-----------------------------------------------------------------------
/*!
Structure for holding information about an adapter,
including a list of devices available on this adapter
*/
//-----------------------------------------------------------------------
struct RenderAdapterInfo
{

	uint m_iCurrentDevice;
};


//-----------------------------------------------------------------------
struct FSAADef
{
	FSAAType	Type;
	uint			MaxQuality;
	FSAADef(): Type(FSAATYPE_NONE), MaxQuality(0) {}
};

//-----------------------------------------------------------------------
/*!
Initial setup, used for Render::Create, or Render::Reset
Similar to D3DPRESENT_PARAMETERS
*/
//-----------------------------------------------------------------------
struct RenderSetup
{
	uint					m_iBackBufferWidth;
	uint					m_iBackBufferHeight;
	uint					m_iBackBufferCount;
	TextureFormat	m_BackBufferFormat;
	TextureFormat	m_DepthBufferFormat;
	FSAAType			m_FSAAType;
	uint					m_iFSAAQuality;
	uint					m_SwapEffect;
	uint					m_iRefreshRateInHz;
	uint					m_PresentationInterval;
	uint					m_iFlags;
	bool					m_bWindowed;
	RenderSetup();
};

//-----------------------------------------------------------------------
/*!
Flags for Renderer::PushRenderTarget
*/
//-----------------------------------------------------------------------
enum RenderTargetFlags
{
	//! Instead render-target surface from supplied RenderTarget, previous one is used
	RTF_KEEP_RENDERTARGET				= BITF(0),
	//! Instead of depth/stencil surface from supplied RenderTarget, previous one is used
	RTF_KEEP_DEPTHSTENCILTARGET	= BITF(1),
	RTF_KEEP_VIEWPORT						= BITF(2),
};


#ifdef ENF_OGL

#ifdef _DEBUG
	void ReportGLError(GLenum err, const char* file, int line);
	void ReportGLCall(const char* function, const char* file, int line);
	void ReportGLFBOStatus(GLenum fbo, const char* file, int line);
	
	ENF_INLINEFUNC void CheckGLError(const char* function, const char* file, int line)
	{
		GLenum err = glGetError();
		ReportGLCall(function, file, line);
		if(err != GL_NO_ERROR)
		{
			ReportGLError(err, file, line);
		}
	}

	#define GL_ERROR(func)		CheckGLError(func, __FILE__, __LINE__)
	#define GL_ERROR_CLEAR()	glGetError()
	#define GLCALL(a)					{a; GL_ERROR(#a);}	
	#define GL_CHECKFBO(a)		ReportGLFBOStatus(a, __FILE__, __LINE__)
#else
	#define GL_ERROR()				((void)0)
	#define GL_ERROR_CLEAR()  ((void)0)
	#define GLCALL(a)					a
	#define GL_CHECKFBO(a)		((void)0)
#endif

//-----------------------------------------------------------------------
ENF_INLINE GLenum GLBlendFactor(RenderBlendFactor fac)
{
	switch(fac)
	{
	case BLEND_ZERO:
		return GL_ZERO;
	case BLEND_ONE:
		return GL_ONE;
	case BLEND_SRCCOLOR:
		return GL_SRC_COLOR;
	case BLEND_INVSRCCOLOR:
		return GL_ONE_MINUS_SRC_COLOR;
	case BLEND_SRCALPHA:
		return GL_SRC_ALPHA;
	case BLEND_INVSRCALPHA:
		return GL_ONE_MINUS_SRC_ALPHA;
	case BLEND_DESTALPHA:
		return GL_DST_ALPHA;
	case BLEND_INVDESTALPHA:
		return GL_ONE_MINUS_DST_ALPHA;
	case BLEND_DESTCOLOR:
		return GL_DST_COLOR;
	case BLEND_INVDESTCOLOR:
		return GL_ONE_MINUS_DST_COLOR;
	case BLEND_SRCALPHASAT:
		return GL_SRC_ALPHA_SATURATE;
	case BLEND_BLENDFACTOR:
		return GL_CONSTANT_COLOR;
		//case BLEND_INVBLENDFACTOR:
	};
	return GL_ONE;
}

//-----------------------------------------------------------------------
ENF_INLINE GLenum GLBlendOp(RenderBlendOperator op)
{
	switch(op)
	{
	case BLENDOP_ADD:
		return GL_FUNC_ADD;
	case BLENDOP_SUBTRACT:
		return GL_FUNC_SUBTRACT;
	case BLENDOP_REVSUBTRACT:
		return GL_FUNC_REVERSE_SUBTRACT;
	};
	return GL_FUNC_ADD;
}

//-----------------------------------------------------------------------
ENF_INLINE GLenum GLFunc(RenderCmpFunc func)
{
	switch(func)
	{
	case CMP_NEVER:
		return GL_NEVER;
	case CMP_LESS:
		return GL_LESS;
	case CMP_EQUAL:
		return GL_EQUAL;
	case CMP_LESSEQUAL:
		return GL_LEQUAL;
	case CMP_GREATER:
		return GL_GREATER;
	case CMP_NOTEQUAL:
		return GL_NOTEQUAL;
	case CMP_GREATEREQUAL:
		return GL_GEQUAL;
	case CMP_ALWAYS:
		return GL_ALWAYS;
	}
	return GL_NEVER;
}

//-----------------------------------------------------------------------
ENF_INLINE GLenum GLFill(RenderFillMode mode)
{
	switch(mode)
	{
	case FILL_POINT:
		return GL_POINT;
	case FILL_SOLID:
		return GL_FILL;
	case FILL_WIREFRAME:
		return GL_LINE;
	}
	return GL_FILL;
}

//-----------------------------------------------------------------------
ENF_INLINE GLenum GLOp(RenderStencilOp op)
{
	switch(op)
	{
	case STENCILOP_KEEP:
		return GL_KEEP;
	case STENCILOP_ZERO:
		return GL_ZERO;
	case STENCILOP_REPLACE:
		return GL_REPLACE;
	case STENCILOP_INCRSAT:
		return GL_INCR;
	case STENCILOP_DECRSAT:
		return GL_DECR;
	case STENCILOP_INVERT:
		return GL_INVERT;
	case STENCILOP_INCR:
		return GL_INCR_WRAP;
	case STENCILOP_DECR:
		return GL_DECR_WRAP;
	}
	return GL_KEEP;
}

GLint TexFormatToGLFormat(TextureFormat fmt, bool rt = false);
GLint TexFormatToGLFormatEx(TextureFormat fmt, GLenum& dataFormat, GLenum& dataType, bool rt = false);

#endif

/*!
definition of shader constant
*/
typedef float ShaderVec[4];

//-----------------------------------------------------------------------
enum StreamFreqOp
{
	SFO_NONE,
	SFO_DIVIDE,
	SFO_MODULO
};


//-----------------------------------------------------------------------
/*!
Encapsulation of Direct3DDevice
*/
//-----------------------------------------------------------------------
class PUBLIC_API Renderer: public Module
{
	friend class DeviceResource;
	friend class Texture;
	friend class RenderShader;
	friend class VertexShader;
	friend class PixelShader;
	friend class VertexBuffer;
	friend class IndexBuffer;
	friend class RenderTarget;
	friend class PushDepthStencilState;

protected:
	//-----------------------------------------------------------------------
	struct StreamState
	{
		void* VB;

		uint				Offset;
		uint				Stride;

		#ifdef ENF_HWINSTANCING
			uint					Freq;
			StreamFreqOp	Op;
		#endif

		StreamState(): VB(NULL), Offset(0), Stride(0)
		{
			#ifdef ENF_HWINSTANCING
				Freq = 1;
				Op = SFO_NONE;
			#endif
		}
	};

	//device specific
	Array<VertexDeclaration*>	m_pVertexDeclarations;

	IL_INT32			m_iLock;
	IL_INT32			m_ThreadLock;

	#if defined(ENF_OGL)
		#ifdef WIN32
			HGLRC		m_glContext;
			HDC			m_HDC;
		#endif
		CGcontext	m_cgContext;
		GLuint		m_iCurrentFB;
		GLuint		m_iCurrentRB;
		GLuint		m_iCurrentDB;
		GLuint		m_iPrimaryDepthBuffer;
		GLuint		m_iPrimaryFrameBuffer;
		GLuint		m_PrimaryRenderBuffer;
	#endif

	ENF_RENDSTATIC RenderStats	m_Stats;

	//-----------------------------------------------------------------------
	// States
	//-----------------------------------------------------------------------
	static const uint MAX_PS_CONSTANTS = 256;
	static const uint MAX_VS_CONSTANTS = 256;
	
	ENF_RENDSTATIC uint m_iRenderStateValue[256] = {};
	ENF_RENDSTATIC void _SetRenderState(RenderState state, uint value)
	{
		m_iRenderStateValue[(int)state] = value;
	}

	#if defined(ENF_OGL)
		static ENF_ALIGN16(ShaderVec)	m_PSConstants[MAX_PS_CONSTANTS];
		static uint m_iStartPSConstants;
		static uint m_iNumPSConstants;
		static ENF_ALIGN16(ShaderVec)	m_VSConstants[MAX_VS_CONSTANTS];
		static uint m_iStartVSConstants;
		static uint m_iNumVSConstants;
	#endif

	ENF_RENDSTATIC void _SetBlendState(uint rtindex, BlendState blendState);
	ENF_RENDSTATIC void _SetDepthStencilState(DepthStencilStateHandle state, uint stencilRef);

	#if defined(ENF_OGL)
		static uint64 m_PSConstChanged;
		static uint64 m_VSConstChanged;
		
		//-----------------------------------------------------------------------
		ENF_RENDSTATIC ENF_INLINE void DirtyPSConst(uint startRegister, uint count = 1)
		{
			enf_assert(count >= 1);
			uint start = startRegister >> 2;
			uint end = ((startRegister + count - 1) >> 2) + 1;
			uint64 mask = ((ULONG64(1) << (end - start)) - 1) << start;
			uint64 oldMask = m_PSConstChanged;
			uint64 newMask = oldMask | mask;
			if(newMask != oldMask)
			{
				m_PSConstChanged = newMask;
				m_iCommitMask |= GLCM_PIXELSHADER;
			}
		}

		//-----------------------------------------------------------------------
		ENF_RENDSTATIC ENF_INLINE void DirtyVSConst(uint startRegister, uint count = 1)
		{
			enf_assert(count >= 1);
			uint start = startRegister >> 2;
			uint end = ((startRegister + count - 1) >> 2) + 1;
			uint64 mask = ((ULONG64(1) << (end - start)) - 1) << start;
			uint64 oldMask = m_VSConstChanged;
			uint64 newMask = oldMask | mask;
			if(newMask != oldMask)
			{
				m_VSConstChanged = newMask;
				m_iCommitMask |= GLCM_VERTEXSHADER;
			}
		}

		//-----------------------------------------------------------------------
		ENF_RENDSTATIC ENF_INLINE void GLDrawArrays(PrimitiveType Type, uint StartVertex, uint PrimitiveCount)
		{
			GLenum mode;
			GLsizei count;
			if(Type == PT_TRIANGLELIST)
			{
				count = PrimitiveCount * 3;
				mode = GL_TRIANGLES;
			}
			else if(Type == PT_TRIANGLESTRIP)
			{
				count = PrimitiveCount + 2;
				mode = GL_TRIANGLE_STRIP;
			}
			else if(Type == PT_TRIANGLEFAN)
			{
				count = PrimitiveCount + 2;
				mode = GL_TRIANGLE_FAN;
			}
			else if(Type == PT_LINELIST)
			{
				count = PrimitiveCount * 2;
				mode = GL_LINES;
			}
			else if(Type == PT_LINESTRIP)
			{
				count = PrimitiveCount + 1;
				mode = GL_LINE_STRIP;
			}
			else if(Type == PT_POINTLIST)
			{
				count = PrimitiveCount;
				mode = GL_POINTS;
			}
			else if(Type == PT_QUADLIST)
			{
				count = PrimitiveCount * 4;
				mode = GL_QUADS;
			}
			GLCALL(glDrawArrays(mode, StartVertex, count));
		}
	#endif

	#if defined(ENF_OGL)
		enum CommitMask
		{
			GLCM_BLEND							= BITF(0),
			GLCM_DEPTHSTENCIL				= BITF(1),
			GLCM_STENCILREF					= BITF(2),
			GLCM_SAMPLERFILTER			= BITF(3),
			GLCM_SAMPLERFILTERV			= BITF(4),
			GLCM_SAMPLERADDRESS			= BITF(5),
			GLCM_SAMPLERADDRESSV		= BITF(6),
			GLCM_STREAMS						= BITF(7),
			GLCM_VERTEXDECLARATION	= BITF(8),
			GLCM_TEXTURES						= BITF(9),
			GLCM_TEXTURESV					= BITF(10),
			GLCM_PIXELSHADER				= BITF(11),
			GLCM_VERTEXSHADER				= BITF(12),
			GLCM_INDEXBUFFER				= BITF(13),
			GLCM_ALPHATEST					= BITF(14),
			GLCM_VSB								= BITF(16),
			GLCM_PSB								= BITF(17),
			GLCM_FBO								= BITF(18),
		};

		ENF_RENDSTATIC uint m_iCommitMask;
		ENF_RENDSTATIC uint m_iShadowVertexDeclaration;
		ENF_RENDSTATIC uint m_iVertexDeclaration;
		ENF_RENDSTATIC uint m_iVSConstantsB;
		ENF_RENDSTATIC uint m_iPSConstantsB;
		ENF_RENDSTATIC uint m_iPrevBaseVertexIndex;
		ENF_RENDSTATIC bool m_bEnableAlphaTest;

		ENF_RENDSTATIC DepthStencilStateHandle	m_iDepthStencilShadowState;
		ENF_RENDSTATIC uint						m_iStencilShadowRef;

		ENF_RENDSTATIC Texture*				m_pTextures[MAX_TEXTURES];
		ENF_RENDSTATIC Texture*				m_pVertexTextures[MAX_VERTEX_TEXTURES];
		ENF_RENDSTATIC CGprogram 			m_iVShader;
		ENF_RENDSTATIC CGprogram 			m_iPShader;
		ENF_RENDSTATIC IndexBuffer*		m_pIndexBuffer;
		ENF_RENDSTATIC GLenum					m_IndexBufferFormat;

		ENF_RENDSTATIC Texture*				m_pShadowTextures[MAX_TEXTURES];
		ENF_RENDSTATIC Texture*				m_pShadowVertexTextures[MAX_VERTEX_TEXTURES];
		ENF_RENDSTATIC CGprogram 			m_iShadowVShader;
		ENF_RENDSTATIC CGprogram 			m_iShadowPShader;

		ENF_RENDSTATIC void CommitStates(uint commitMask, const void* userPointer, uint vertexOffset, uint stride);

		//-----------------------------------------------------------------------
		ENF_RENDSTATIC ENF_INLINE void DirtyMask(uint mask)
		{
			m_iCommitMask |= mask;
		}

		//-----------------------------------------------------------------------
		ENF_RENDSTATIC ENF_INLINE void EnableAlphaTest(bool enable)
		{
			if(m_bEnableAlphaTest != enable)
			{
				m_bEnableAlphaTest = enable;
				DirtyMask(GLCM_ALPHATEST);
			}
		}
	#endif

	ENF_RENDSTATIC BlendState									m_iBlendStates[MAX_RENDER_TARGETS];
	ENF_RENDSTATIC DepthStencilStateHandle		m_iDepthStencilState;
	ENF_RENDSTATIC uint												m_iStencilRef;

	ENF_RENDSTATIC SamplerFilterStateHandle		m_iSamplerFilterStateValue[MAX_TEXTURES];
	ENF_RENDSTATIC SamplerFilterStateHandle		m_iSamplerFilterStateValue2[MAX_VERTEX_TEXTURES];
	ENF_RENDSTATIC SamplerAddressStateHandle	m_iSamplerAddressStateValue[MAX_TEXTURES];
	ENF_RENDSTATIC SamplerAddressStateHandle	m_iSamplerAddressStateValue2[MAX_VERTEX_TEXTURES];
	ENF_RENDSTATIC StreamState								m_Streams[MAX_STREAMS];
	
	ENF_RENDSTATIC SArray<SamplerAddressState, 64>	m_SamplerAddressStates;
	ENF_RENDSTATIC SArray<DepthStencilStateDef, 64> m_DepthStencilStateDefs;

	ENF_RENDSTATIC uint m_iRTStackDepth[MAX_RENDER_TARGETS];

	RenderViewport	m_Viewport;
	uint				m_iTargetWidth;
	uint				m_iTargetHeight;

	static const bool	m_bFloatingPointDepthBuffer = false;

	uint				m_iFrame;

	bool				m_bInScene;
	bool				m_bDisablePrimaryRT;


	uint				m_iAdapter;
	uint				m_uiAvailMem;

	//! Present  parameters for Create/Reset
	RenderSetup		m_Setup;
	uint				m_iSkipPresent;

	//! String to hold D3D device info
	SString<>		m_DeviceInfo;

	bool m_bATICrap; //because this cripple is not capable of autogenmipmapping regardless what they claim!
	uint m_iGPUCount;

	/*! 
	GPU vendor
	*/
	enum GPUVendor
	{
		GPU_UNKNOWN	= 0x0000,
		GPU_XENON		= 0xFFFF,
		GPU_PS3			= 0xFFFE,
		GPU_NVIDIA	= 0x10DE,
		GPU_ATI			= 0x1002,
		GPU_INTEL		= 0x8086
	} m_GPUVendor;

	virtual void FindGPUInfo()
	{
		m_iGPUCount  = 1;
		m_GPUVendor  = GPU_UNKNOWN;
	}

	#ifdef ENF_STEREO_VISION
		struct StereoInfo
		{
		private: 
			bool m_bRebuild;
			uint m_iLastID;

		public:
			void* Handle;
			float EyeSeparation;
			float Separation;
			union { float Convergence; float MinDistance; };
			bool Enabled;
			bool Available;

			RenderTarget* StereoToMonoTex;

			StereoInfo()
			{
				m_bRebuild = false;
				m_iLastID = -1;

				Handle = NULL;
				Enabled = Available = false;
				EyeSeparation = Separation = Convergence = 0.0f;
				StereoToMonoTex = NULL;
			}

			bool IsInvalid();
			void ForceRebuild()	{ m_bRebuild = true; }
			void Rebuilded()		{ m_bRebuild = false; }
		};
		StereoInfo m_StereoInfo;

		virtual void FindStereoInfo()=0;
		virtual void DisableStereo()=0;
	#endif

	virtual void BeforeReset()=0;
	virtual void AfterReset()=0;

	virtual void ReloadShaders(const char* srcshader)=0;

	bool FindDepthStencilFormat( uint iAdapter, uint DeviceType, TextureFormat TargetFormat, TextureFormat* pDepthStencilFormat );

	void ReleaseReferences();
	void ReleaseReferences(DeviceResource* res);

	~Renderer();

public:
	ENF_DECLARE_ITEM(Renderer, Module);

	Renderer();

	bool OnChangeSettings(TiXmlElement* xmlnode);
	virtual void GenerateOfflineShaders()=0;

	/*!
	soft lock of renderer, just increment lock count
	*/
	void Lock()
	{
		IL_INCREMENT(m_iLock);
	}

	/*!
	return if is locked
	*/
	ENF_INLINE bool IsLocked()
	{
		return (IL_READ(m_iLock) != 0);
	}

	/*!
	soft unlock of renderer, just decrement lock count
	*/
	void Unlock()
	{
		enf_assert(m_iLock);
		IL_DECREMENT(m_iLock);
	}

	bool CheckThread();
	void ThreadLock();
	void ThreadUnlock();

	/*!
	set current renderer
	*/
	void SetCurrent()
	{
		g_pRender = this;
	}

	/*!
	Returns current device capabilities, 	TODO: replace with our non-native version!
	*/

	/*!
	return maximum size of point 
	*/
	float GetMaxPointSize() const
	{
		return 1.0f;
	}

	//!Resets per-frame statistics
	void ResetStats();

	//!Returns statistics
	const RenderStats& GetStats() const
	{
		return m_Stats;
	}

	/** \name Device setup
	Device mode setup and initialization
	*/

	//@{
	/*!
	Gets info about DX adapter
	\param adapter Number of adapter
	\param adapterinfo Returned info about adapter
	\return True when adapter exists, False otherwise
	*/
	virtual bool GetAdapter(uint adapter, RenderAdapterInfo *adapterinfo)=0;

	/*!
	Gets info about DX DEvice
	\param adapter Number of adapter
	\param device Number of device
	\param deviceinfo Returned info about device
	\return True when device exists, False otherwise
	*/
	virtual bool GetDevice(uint adapter, uint device, RenderDeviceInfo *deviceinfo)=0;

	/*!
	get GPU count
	*/
	ENF_INLINE uint GetGPUCount() const
	{
		return m_iGPUCount;
	}

	/*!
	get current GPU count -> in windowed mode is used just one GPU, in fullscreen m_iGPUCount
	*/
	ENF_INLINE uint GetCurrentGPUCount() const
	{
		return m_Setup.m_bWindowed ? 1 : m_iGPUCount;
	}

	/*!
	get GPU vendor
	*/
	ENF_INLINE GPUVendor GetGPUVendor() const
	{
		return m_GPUVendor;
	}

	#ifdef ENF_STEREO_VISION
		/*!
		return stereo info from driver
		*/
		ENF_INLINE const StereoInfo& GetStereoInfo() const
		{
			return m_StereoInfo;
		}

		/*!
		is enabled
		*/
		ENF_INLINE bool IsStereoEnabled() const
		{
			return m_StereoInfo.Enabled;
		}

		virtual void CaptureStereoImage()=0;
	#endif

	/*!
	Sets current adapter/device/mode. Must be called prior to Renderer::Create()
	\param adapter Adapter we want to use
	\param device Device we want to use
	\param mode Mode we want to use
	\param windowed If it has to be windowed, or fullscreen
	*/
	virtual bool SetMode(uint adapter, uint device, uint mode, bool windowed)=0;

	virtual uint GetNumFSAADefs() const=0;
	virtual const FSAADef& GetFSAADef(uint index) const=0;

	/*!
	Sets current FSAA. Must be called prior to Renderer::Create()
	\param fsaa			Index of FSAA type See Renderer::GetNumFSAADefs and Renderer::GetFSAADef
	\param quality	The quality. Must be between 0 and FSAADef::Quality-1
	\return					True when successfully set
	*/
	virtual bool SetFSAA(uint fsaa, uint quality)=0;

	/*!
	When we want set own resolution instead of enumerated from device. It's allowed only for windowed mode!!
	\param adapter	Adapter we want to use
	\param device		Device we want to use
	\param width		Width
	\param height		Height
	\return					Index of new mode
	*/
	virtual uint AddMode(uint adapter, uint device, uint width, uint height)=0;

	/*!
	Returns info about mode.
	\param adapter	Adapter we want to use
	\param device		Device we want to use
	\param mode			Mode we want to use
	\param modeinfo Returned info about mode
	\return					True when mode exists, false otherwise
	*/
	virtual bool GetMode(uint adapter, uint device, uint mode, RenderModeInfo *modeinfo)=0;

	//!Basic initialization
	virtual bool Init(void* hwnd)=0;

	/*!
	Enables usage of primary render target. When disabled, Renderer do not Clear/Present nor use it in any way.
	Used by tools which render only through	SwapChain.
	\param enable		When true, it uses primary RT
	\return					Previous state
	*/
	bool EnablePrimaryRT(bool enable);

	//!Creates Direct3DDevice according to set Adapter/Device/Mode
	virtual ERESULT Create()=0;

	/*!
	Resets device
	\param setup	Optional setup parameters. When NULL, none parameters are changed
	\return				EE_OK when OK
	*/
#ifdef ENF_DONE
	ERESULT Reset(const RenderSetup* setup);
#else
	ERESULT Reset(const RenderSetup* setup) { return EE_OK; }
#endif

	void SetBrightness(int gamma, int contrast = 50);
	//@}

	//!Returns initial device setup
	ENF_INLINE const RenderSetup& GetSetup() const
	{
		return m_Setup;
	}

	//!Returns current back buffer format
	ENF_INLINE TextureFormat GetSetupFormat() const
	{
		return m_Setup.m_BackBufferFormat;
	}

	//!Returns width of initial device setup. See GetSetup
	ENF_INLINE uint GetSetupWidth() const
	{
		return m_Setup.m_iBackBufferWidth;
	}

	//!Returns height of initial device setup. See GetSetup
	ENF_INLINE uint GetSetupHeight() const
	{
		return m_Setup.m_iBackBufferHeight;
	}

	//!Returns current render target width. Can be called between BeginScene/EndScene
	ENF_INLINE uint GetTargetWidth() const
	{
		enf_assert(m_bInScene == true);
		return m_iTargetWidth;
	}

	//!Returns current render target height. Can be called between BeginScene/EndScene
	ENF_INLINE uint GetTargetHeight() const
	{
		enf_assert(m_bInScene == true);
		return m_iTargetHeight;
	}


	//!Returns description of current device
	const char* GetDescription() const
	{
		return m_DeviceInfo.cstr();
	}

	/*!
	Makes screenshot of current render target
	\param rendertarget	The rendertarget you want to make screenshot of. When NULL, it takes last used one
	\param outstream		Output stream where is screenshot put
	*/
	#if defined(ENF_OGL)
		GLuint GetCurrentFrameBuffer() const { return m_iCurrentFB; }
		GLuint GetCurrentRenderBuffer() const { return m_iCurrentRB; }
		GLuint GetCurrentDepthBuffer() const { return m_iCurrentDB; }

		void MakeScreenshot(GLint rendertarget, COutputStream* outstream);
	#endif

	#if defined(ENF_OGL)
		CGcontext GetCGContext() const
		{
			return m_cgContext;
		}
	#endif

	//!Clears primary rendertarget
	void ClearScreen();

	uint GetAvailableMemory();
	uint GetAllocatedMemory();

	//!Sets viewport
	void SetViewport(const RenderViewport& viewport);

	//!Returns current viewport
	ENF_INLINE const RenderViewport& GetViewport() const
	{
		return m_Viewport;
	}

	//!Must be called prior to time demanding tasks to satisfy TCR022
	virtual void Suspend()=0;
	//!Must be called after time demanding tasks
	virtual void Resume()=0;
	//!Must be called prior to any Renderer use
	virtual void BeginUse()=0;
	//!Must be called after Renderer use
	virtual void EndUse()=0;

	//!Begins scene rendering
	virtual ERESULT BeginScene()=0;

	//!Ends scene rendering
	virtual void EndScene()=0;

	bool IsInScene() const
	{
		return m_bInScene;
	}

	/*!
	Presents current render target. When it's fullscreen, it just swaps	front/back buffer, when it's windowed, 
	or swapchain, it copies backbuffer to desktop window.
	*/
	virtual void Present()=0;
	void SkipNextPresent();

	uint SkippingNextPresent() const
	{
		return m_iSkipPresent;
	}

	bool Begin();
	void End();

	//-----------------------------------------------------------------------
	/** \name Device states
	Setting and getting of device states
	*/
	//@{

	/*!
	Creates handle to filter state, that is used by SetSamplerFilterState,
	which replaces according sampler states
	\param state Filter state
	\return Handle
	*/
	SamplerFilterStateHandle CreateSamplerFilterState(const SamplerFilterState& state);

	/*!
	Creates handle to address state, that is used by SetSamplerAddressState,
	which replaces native sampler states
	\param state Address state
	\return Handle
	*/
	SamplerAddressStateHandle CreateSamplerAddressState(const SamplerAddressState& state);

	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void SetSamplerFilterState(uint index, SamplerFilterStateHandle stateHandle)
	{
		enf_assert(index < MAX_TEXTURES);

		#ifndef NO_REDUNDANT_RS_CHECK
			if(m_iSamplerFilterStateValue[index] == stateHandle)
				return;
		#endif

		m_iSamplerFilterStateValue[index] = stateHandle;

		const SamplerFilterState& state = *(SamplerFilterState*)&stateHandle;
		#if defined(ENF_OGL)
			DirtyMask(GLCM_SAMPLERFILTER);
		#endif

		#ifdef ENF_DEVELOPER
			m_Stats.SSChanges++;
		#endif
	}

	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void SetVertexSamplerFilterState(uint index, SamplerFilterStateHandle stateHandle)
	{
		enf_assert(index < MAX_VERTEX_TEXTURES);

		#ifndef NO_REDUNDANT_RS_CHECK
			if(m_iSamplerFilterStateValue2[index] == stateHandle)
				return;
		#endif
		m_iSamplerFilterStateValue2[index] = stateHandle;

		const SamplerFilterState& state = *(SamplerFilterState*)&stateHandle;
		#if defined(ENF_OGL)
			DirtyMask(GLCM_SAMPLERFILTERV);
		#endif

		#ifdef ENF_DEVELOPER
			m_Stats.SSChanges++;
		#endif
	}


	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void SetSamplerAddressState(uint index, SamplerAddressStateHandle stateHandle)
	{
		enf_assert(index < MAX_TEXTURES);
		#ifndef NO_REDUNDANT_RS_CHECK
			if(stateHandle == m_iSamplerAddressStateValue[index])
				return;
		#endif
		m_iSamplerAddressStateValue[index] = stateHandle;

		const SamplerAddressState& state = m_SamplerAddressStates[stateHandle];

		#if defined(ENF_OGL)
			DirtyMask(GLCM_SAMPLERADDRESS);
		#endif

		#ifdef ENF_DEVELOPER
			m_Stats.SSChanges++;
		#endif
	}

	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void SetVertexSamplerAddressState(uint index, SamplerAddressStateHandle stateHandle)
	{
		enf_assert(index < MAX_VERTEX_TEXTURES);

		#ifndef NO_REDUNDANT_RS_CHECK
			if(stateHandle == m_iSamplerAddressStateValue2[index])
				return;
		#endif
		m_iSamplerAddressStateValue2[index] = stateHandle;

		const SamplerAddressState& state = m_SamplerAddressStates[stateHandle];

		#if defined(ENF_OGL)
				DirtyMask(GLCM_SAMPLERADDRESSV);
		#endif

		#ifdef ENF_DEVELOPER
			m_Stats.SSChanges++;
		#endif
	}

	/*!
	Creates handle to Depth-stencil state, that is used by SetDepthStencilState,
	which replaces native render states
	\param state Depth-Stencil state
	\return Handle
	*/
	DepthStencilStateHandle CreateDepthStencilState(const DepthStencilStateDef& def);

	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void SetDepthStencilState(DepthStencilStateHandle state, uint stencilRef = 0)
	{
		DepthStencilStateDef& def = m_DepthStencilStateDefs[(int)state];

		#ifndef NO_REDUNDANT_RS_CHECK
			if(m_iDepthStencilState != state || m_iStencilRef != stencilRef)
		#endif
		{
			_SetDepthStencilState(state, stencilRef);
		}
	}

	//-----------------------------------------------------------------------
	/*!
	Sets render state
	\param state State See: D3DRENDERSTATETYPE
	\param value Value of state
	\return EE_OK when success
	*/
	ENF_RENDSTATIC ENF_INLINE void SetRenderState(RenderState state, uint value)
	{
		#ifndef NO_REDUNDANT_RS_CHECK
			if(m_iRenderStateValue[(int)state] == value)
				return;
		#endif

		_SetRenderState(state, value);
	}

	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void SetClipPlane(uint planeIndex, const Vector4* plane)
	{
		#if defined(ENF_OGL)
			//glClipPlanef 
		#endif
	}

	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void SetBlendState(uint rtindex, ebool enable)
	{
		enf_assert2(enable == false, "True is not enabled. Use parametrized method instead!");

		#ifndef NO_REDUNDANT_RS_CHECK
			if(m_iBlendStates[rtindex].Value == 0xffffffff)
				return;
		#endif

		m_iBlendStates[rtindex].Value = 0xffffffff;

		#if defined(ENF_OGL)
			GLCALL(glDisable(GL_BLEND));
		#endif

		#ifdef ENF_DEVELOPER
			m_Stats.BSChanges++;
		#endif
	}

	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void SetAlphaKill(uint stage, bool enable)
	{
		enf_assert(stage < 16);
	}

	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void SetBlendState(uint rtindex, BlendState blendState)
	{
		enf_assert(rtindex < MAX_RENDER_TARGETS);

		#ifndef NO_REDUNDANT_RS_CHECK
			if(m_iBlendStates[rtindex] == blendState)
				return;
		#endif

		_SetBlendState(rtindex, blendState);
	}

	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE BlendState GetBlendState(uint rtindex)
	{
		enf_assert(rtindex < MAX_RENDER_TARGETS);
		return m_iBlendStates[rtindex];
	}

	//-----------------------------------------------------------------------
	/*!
	Gets render state
	\param state State See: D3DRENDERSTATETYPE
	\param value Returned value of state
	\return EE_OK when success
	*/
	ENF_RENDSTATIC ENF_INLINE void GetRenderState(RenderState state, uint *value)
	{
		*value = m_iRenderStateValue[(int)state];
	}

	//@}

	//-----------------------------------------------------------------------
	/*!
	Gets or loads shader.
	\param name				Name of shader
	\param profile		Profile for what we compile
	\param defines		Array of defines or the shader variant
	\param numdefines	Length of array
	*/
	virtual RenderShader* GetShader(CStr name, ProfileType profile, const Define* defines = NULL, uint numdefines = 0)=0;

	ENF_INLINE PixelShader* GetPShader(CStr name, ProfileType profile, const Define* defines = NULL, uint numdefines = 0)
	{
		return (PixelShader*) GetShader(name, profile, defines, numdefines);
	}

	ENF_INLINE VertexShader* GetVShader(CStr name, ProfileType profile, const Define* defines = NULL, uint numdefines = 0)
	{
		return (VertexShader*) GetShader(name, profile, defines, numdefines);
	}

	//!Optimization for shader-batch compilation
	virtual void BeginShaderCompile()=0;

	//!End of batch compilation
	virtual void EndShaderCompile()=0;

	//-----------------------------------------------------------------------
	/** \name Vertex shaders
	Setting and getting of vertex shader states
	*/
	//@{

	/*!
	Sets VertexShader.
	\param vsh VertexShader
	*/
	void SetVertexShader(const VertexShader* vsh);

	/*!
	Sets array of vertex shader integer constants
	\param index		Start register index
	\param value		Array of values (at least 4 of them!)
	\param vec4size	Number of 4-element vectors
	*/
	ENF_RENDSTATIC ENF_INLINE void SetVertexShaderConstantI(int index, const int* value, uint vec4size)
	{
	}

	/*!
	Sets array of vertex shader float constants
	\param index		Start register index
	\param value		Array of values (at least 4 of them!)
	\param vec4size	Number of 4-element vectors
	*/
	ENF_RENDSTATIC ENF_INLINE void SetVertexShaderConstantF(int index, const float *value, uint vec4size)
	{
		#ifdef _DEBUG
			for(uint n = 0; n < (vec4size * 4); n++)
				enf_assert2(!BadFloat(value[n]), "Trying to set NAN/INF to vertex shader constant");
		#endif

		#if defined(ENF_OGL)
			DirtyVSConst(index, vec4size);
			memcpy(&m_VSConstants[index], value, vec4size * sizeof(ShaderVec));
		#endif
	}

	ENF_RENDSTATIC ENF_INLINE void SetVertexShaderConstant(int index, const Vector3& value)
	{
		Vector4 value4 = value;
		SetVertexShaderConstantF(index, value4.v, 1);
	}

	ENF_RENDSTATIC ENF_INLINE void SetVertexShaderConstant(int index, const Vector2& value)
	{
		Vector4 value4(value.x, value.y, 0.0f, 0.0f);
		SetVertexShaderConstantF(index, value4.v, 1);
	}

	ENF_RENDSTATIC ENF_INLINE void SetVertexShaderConstant(int index, const Vector4& value)
	{
		SetVertexShaderConstantF(index, value.v, 1);
	}

	ENF_RENDSTATIC ENF_INLINE void SetVertexShaderConstant(int index, const FColor& value)
	{
		SetVertexShaderConstantF(index, value.colors, 1);
	}

	/*!
	Sets array of vertex shader bool constants
	\param index	Start register index
	\param value	Array of values
	\param size		Number of values
	*/
	ENF_RENDSTATIC ENF_INLINE void SetVertexShaderConstantB(int index, const int *value, uint size)
	{
		#if defined(ENF_OGL)
			uint mask = 0;
			uint val = 0;
			for(uint n = 0; n < size; n++)
			{
				uint bit = (1 << (n + index));
				mask |= bit;
				val |= value[n] ? bit : 0;
			}

			if((m_iVSConstantsB & mask) != val)
			{
				m_iVSConstantsB = (m_iVSConstantsB & ~mask) | val;
				m_iCommitMask |= GLCM_VSB;
			}
		#endif
	}

	/*!
	Sets vertex shader bool constant
	\param index
	Start register index
	\param value
	The value
	*/
	ENF_RENDSTATIC ENF_INLINE void SetVertexShaderConstantB(int index, bool value)
	{
		int ival = value ? 1 : 0;

		#if defined(ENF_OGL)
			uint mask = 1 << index;
			uint val = value ? mask : 0;
			if((m_iVSConstantsB & mask) != val)
			{
				m_iVSConstantsB = (m_iVSConstantsB & ~mask) | val;
				m_iCommitMask |= GLCM_VSB;
			}
		#endif
	}
	//@}

	//-----------------------------------------------------------------------
	/** \name Pixel shaders
	Setting and getting of pixel shader states
	*/
	//@{

	/*!
	Sets PixelShader.
	\param psh PixelShader
	\return EE_OK when success
	*/
	void SetPixelShader(const PixelShader *psh);

	/*!
	Sets array of pixel shader float constants
	\param index		Start register index
	\param value		Array of values (at least 4 of them!)
	\param vec4size	Number of 4-element vectors
	*/
	ENF_RENDSTATIC ENF_INLINE void SetPixelShaderConstantF(int index, const float* value, uint vec4size)
	{
		#ifdef _DEBUG
			for(uint n = 0; n < (vec4size * 4); n++)
				enf_assert2(!BadFloat(value[n]), "Trying to set NAN/INF to pixel shader constant");
		#endif

		#if defined(ENF_OGL)
			DirtyPSConst(index, vec4size);
			memcpy(&m_PSConstants[index], value, vec4size * sizeof(ShaderVec));
		#endif
	}

	ENF_RENDSTATIC ENF_INLINE void SetPixelShaderConstant(int index, const Vector4& value)
	{
		SetPixelShaderConstantF(index, value.v, 1);
	}

	ENF_RENDSTATIC void SetPixelShaderConstant(int index, const Vector3& value)
	{
		Vector4 value4 = value;
		SetPixelShaderConstantF(index, value4.v, 1);
	}

	/*!
	Sets array of pixel shader integer constants
	\param index		Start register index
	\param value		Array of values (at least 4 of them!)
	\param vec4size	Number of 4-element vectors
	*/
	ENF_RENDSTATIC ENF_INLINE void SetPixelShaderConstantI(int index, const int *value, uint vec4size)
	{
	}

	/*!
	Sets array of pixel shader bool constants
	\param index	Start register index
	\param value	Array of values
	\param size		Number of values
	*/
	ENF_RENDSTATIC ENF_INLINE void SetPixelShaderConstantB(int index, const int *value, uint size)
	{
		#if defined(ENF_OGL)
			uint mask = 0;
			uint val = 0;
			for(uint n = 0; n < size; n++)
			{
				uint bit = (1 << (n + index));
				mask |= bit;
				val |= value[n] ? bit : 0;
			}

			if((m_iPSConstantsB & mask) != val)
			{
				m_iPSConstantsB = (m_iPSConstantsB & ~mask) | val;
				m_iCommitMask |= GLCM_PSB;
			}
		#endif
	}

#if defined(ENF_OGL)

	template<class T> T* BeginPixelShaderConstant(uint startRegister, uint count = 1)
	{
		enf_assert(m_iStartPSConstants == -1);
		m_iStartPSConstants = startRegister;
		uint countRegs = (count * sizeof(T)) / sizeof(ShaderVec);
		enf_assert((startRegister + countRegs) < MAX_PS_CONSTANTS);
		m_iNumPSConstants = countRegs;
		DirtyPSConst(startRegister, (count * sizeof(T)) / sizeof(ShaderVec));
		return (T*)&m_PSConstants[startRegister];
	}
	void EndPixelShaderConstant()
	{
		enf_assert(m_iStartPSConstants != -1);
		m_iStartPSConstants = -1;
	}

	template<class T> T* BeginVertexShaderConstant(uint startRegister, uint count = 1)
	{
		enf_assert(m_iStartVSConstants == -1);
		m_iStartVSConstants = startRegister;
		uint countRegs = (count * sizeof(T)) / sizeof(ShaderVec);
		enf_assert((startRegister + countRegs) < MAX_VS_CONSTANTS);
		m_iNumVSConstants = countRegs;
		DirtyVSConst(startRegister, countRegs);
		return (T*)&m_VSConstants[startRegister];
	}
	void EndVertexShaderConstant()
	{
		enf_assert(m_iStartVSConstants != -1);
		m_iStartVSConstants = -1;
	}
#endif

	//@}

	ENF_RENDSTATIC void SetIndices(IndexBuffer* indexdata);


	//-----------------------------------------------------------------------
	/** \name Draw methods
	Rendering of indexed and non-indexed primitives
	*/
	//@{
	
	/*!
	Draw indexed primitive stored in IndexBuffer and VertexBuffer
	\param Type Primitive type
	\param BaseVertexIndex Value added to each value read from IndexBuffer
	\param MinIndex Minimal value in IndexBuffer
	\param NumVertices Number of vertices addressed by IndexBuffer
	\param StartIndex Offset from beginning of IndexBuffer
	\param PrimitiveCount Number of primitives
	*/
	ENF_RENDSTATIC void DrawIndexedPrimitive(PrimitiveType Type, int BaseVertexIndex, uint MinIndex, uint NumVertices, uint StartIndex, uint PrimitiveCount);

	/*!
	Draw primitive stored in VertexBuffer
	\param Type Primitive type
	\param StartVertex Offset from beginning of VertexBuffer
	\param PrimitiveCount Number of primitives
	*/
	ENF_RENDSTATIC void DrawPrimitive(PrimitiveType Type, uint StartVertex, uint PrimitiveCount);


	/*!
	Draw primitive stored in user buffer
	\param Type Primitive type
	\param PrimitiveCount Number of primitives
	\param PrimitiveData User buffer with data
	\param Stride Size of each vertex element
	*/
	ENF_RENDSTATIC ENF_INLINE void DrawPrimitiveUP(PrimitiveType type, uint primitiveCount, const void* primitiveData, uint stride)
	{
		#ifdef ENF_DEVELOPER
			m_Stats.AddPrimitive(type, primitiveCount);
		#endif

		#if defined(ENF_OGL)
			uint commitMask = m_iCommitMask;

			if(commitMask)
				CommitStates(commitMask, PrimitiveData, 0, Stride);

			GLDrawArrays(Type, 0, PrimitiveCount);
		#endif

		m_Streams[0].VB = NULL;
	}

	/*!
	Draw Quad stored in VertexBuffer
	\param StartVertex Offset from beginning of VertexBuffer
	*/
	ENF_RENDSTATIC void DrawQuad(uint StartVertex);

	ENF_RENDSTATIC void DrawQuads(uint StartVertex, uint PrimitiveCount);
//@}

	ENF_RENDSTATIC void SetStreamSourceFreqOp(uint stream, StreamFreqOp op, uint freq);

	ENF_RENDSTATIC void SetStreamSource(uint stream, VertexBuffer *vertexbuffer, uint offset, uint stride);
	
	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void SetStreamSourceInternal(uint stream, void* pvb, uint offset, uint stride)
	{
		if(m_Streams[stream].VB == pvb &&
			m_Streams[stream].Offset == offset &&
			m_Streams[stream].Stride == stride)
		{
			return;
		}

		m_Streams[stream].VB = pvb;
		m_Streams[stream].Offset = offset;
		m_Streams[stream].Stride = stride;

		#if defined(ENF_OGL)
			m_iCommitMask |= GLCM_STREAMS;
		#endif

		#ifdef ENF_DEVELOPER
			m_Stats.VBChanges++;
		#endif
	}
	
	//-----------------------------------------------------------------------
	ENF_RENDSTATIC ENF_INLINE void ClearStreamSource(uint stream)
	{
		enf_assert(stream < MAX_STREAMS);
		if(m_Streams[stream].VB == 0)
			return;

		#if defined(ENF_OGL)
			m_iCommitMask |= GLCM_STREAMS;
		#endif

		m_Streams[stream].VB = NULL;

		#ifdef ENF_DEVELOPER
			m_Stats.VBChanges++;
		#endif
	}

	//-----------------------------------------------------------------------
	/** \name Render targets
	*/
	//@{
	/*!
	Creates new rendertarget
	\param width		Width of rendertarget
	\param height		Height of rendertarget
	\param rtfmt		Format of render target
	\param depthfmt Format of depthbuffer
	\param fsaa			Multisampling type
	\param usage		Usage flags. See enf::ResourceUsageFlags
	\param edramoffset
	Because the Xbox360 do not manage allocation of EDRAM, we need to do it by hand.
	When you're sure, that you don't overwrite anything, put a 0 here. Otherwise, you need
	to specify offset to keep it safely apart.
	See: GetBaseEDRAMOffset()	and enf::RenderTarget::GetEDRAMSize()
	Use it on win32+DX9 too, it's just ignored
	\param texFlags	See TextureFlags
	*/
	virtual RenderTarget* CreateRenderTarget(uint width, uint height, TextureFormat rtfmt, TextureFormat depthfmt, FSAAType fsaa, uint usage, uint edramoffset, uint texFlags, CStr name = "")=0;

	uint GetBaseEDRAMOffset() const;


	virtual void SetRenderTarget(uint rtindex, RenderTarget *rt, uint face = 0, uint flags = 0)=0;

	/*!
	Pushes new RenderTarget into device. If it's rtindex==0 it sets ViewPort
	to the size of RenderTarget
	\param rtindex Index of rendertarget 0..MAX_RENDER_TARGETS-1
	\param rt RenderTarget
	\param face Cube face, when RenderTarget is cube
	\param flags Flags, see enf::RenderTargetFlags
	*/
	virtual void PushRenderTarget(uint rtindex, RenderTarget *rt, uint face = 0, uint flags = 0)=0;

	/*!
	Pops last RenderTarget out of device. If it's rtindex==0 it restores ViewPort too
	\param rtindex Index of rendertarget 0..MAX_RENDER_TARGETS-1
	*/
	virtual void PopRenderTarget(uint rtindex)=0;

	/*!
	Clears current rendertarget
	\param flags See enf::RTClearFlags
	\param rect Optional region to be cleared
	\param color Optional init color of render target
	\param depth Optional init depth of depth buffer
	\param stencil Optional init value of stencil buffer
	*/
	void ClearRenderTarget(uint flags = 0, const RRect* rect = NULL, uint color = 0, float depth = 1.0f, uint stencil = 0);
	//@}

	//-----------------------------------------------------------------------
	/** \name Textures
	*/
	//@{

	/*!
	Sets texture.
	\param index Index of texture
	\param tex Texture
	*/
	void SetTexture(uint index, const Texture* tex);

	/*!
	Sets vertex texture.
	\param index Index of texture
	\param tex Texture
	*/
	void SetVertexTexture(uint index, const Texture* tex);

	virtual Texture* CreateTexture(CStr strName, uint dwWidth, uint dwHeight, uint dwFlags)=0;
	virtual Texture* CreateTexture(CStr strName, Texture* clone, uint dwFlags)=0;

	/*!
	Creates texture with certain format
	\param name Name of texture
	\param width Width
	\param height Height
	\param format Format See TextureFormat
	\param flags Flags See TextureCreationFlags
	*/
	virtual Texture* CreateTextureWithFormat(CStr name, uint width, uint height, TextureFormat format, uint flags, TextureUsage usage)=0;
	virtual Texture* CreateTextureFromImage(CStr name, CInputStream& instream, uint flags)=0;

	//@}

	//stride must be specified.
	virtual VertexBuffer* CreateVertexBuffer(uint usage, uint len, uint stride, CStr name = "")=0;
	virtual IndexBuffer* CreateIndexBuffer(IndexBufferFormat fmt, uint usage, uint len, CStr name = "")=0;
	void InvalidateGPUCache(DeviceResource* res);

	virtual RenderQuery* CreateQuery(RenderQueryType type)=0;

	void SetVertexDeclaration(IVertexDeclaration pdecl);

	virtual IVertexDeclaration	CreateVertexDeclaration(const VertexDeclaration& decl)=0;

	/** \name Validation
	Validation methods for various states and values
	*/

	//@{
	ERESULT CheckDeviceFormat(uint usage, ResourceType resourcetype, TextureFormat format) const;

	TextureFormat GetBestDepthTextureFormat() const;

	void SetShaderAllocation(uint vs, uint ps);
	void GetShaderAllocation(uint& vs, uint& ps) const;
	//@}

	static ENF_INLINE bool IsFloatingPointDepthBuffer()
	{
		return m_bFloatingPointDepthBuffer;
	}

	static ENF_INLINE RenderCmpFunc GetDepthFunc(RenderCmpFunc func)
	{
		return func;
	}

	virtual uint LoadCursorImage(Texture* tex, const RRect& srcRect, int hotspotX, int hotspotY)=0;
	virtual uint SetCursor(uint cursorImage)=0;
	virtual void SetCursorPos(uint x, uint y)=0;
	virtual void ResetCursor()=0;
	virtual bool ShowCursor(bool show, bool force = false)=0;
	virtual bool IsCursorVisible() const=0;

	virtual void DumpResources() const=0;
};


//-----------------------------------------------------------------------
/*!
Convenient method how to temporarily set RenderState and put it back when you
leave scope. Redundancy is handled too. Usage:

void RenderSomething()
{
	PushRenderState rs0(RS_ZENABLE, false);
	DrawPrimitives(...);
	//RenderState RS_ENABLE is set to previous value upon exit
}

*/
//-----------------------------------------------------------------------
class PushRenderState
{
	RenderState	State;
	uint			Value;

public:
	ENF_INLINE PushRenderState(RenderState state, uint value)
	{
		g_pRender->GetRenderState(state, &Value);
		if(Value == value)
		{
			State = RS_BADVALUE;
			return;
		}
		State = state;
		Value = value;
		g_pRender->SetRenderState(state, value);
	}

	ENF_INLINE PushRenderState(RenderState state)
	{
		g_pRender->GetRenderState(state, &Value);
		State = state;
	}

	ENF_INLINE ~PushRenderState()
	{
		if(State != RS_BADVALUE)
		{
			g_pRender->SetRenderState(State, Value);
		}
	}
};


//-----------------------------------------------------------------------
class PushDepthStencilState
{
	DepthStencilStateHandle	State;
	uint			StencilRef;

public:
	ENF_INLINE PushDepthStencilState(DepthStencilStateHandle state, uint stencilRef = 0)
	{
		if(g_pRender->m_iDepthStencilState == state && g_pRender->m_iStencilRef == stencilRef)
		{
			State = (DepthStencilStateHandle)0xffffffff;
			return;
		}
		State = g_pRender->m_iDepthStencilState;
		StencilRef = g_pRender->m_iStencilRef;//stencilRef;
		g_pRender->SetDepthStencilState(state, stencilRef);
	}

	ENF_INLINE ~PushDepthStencilState()
	{
		if(State != (DepthStencilStateHandle)0xffffffff)
		{
			g_pRender->SetDepthStencilState(State, StencilRef);
		}
	}
};

//-----------------------------------------------------------------------
class PushBlendState
{
	uint RT;
	BlendState BlendRef;

public:
	ENF_INLINE PushBlendState(uint rt, BlendState blendState)
	{
		if(g_pRender->GetBlendState(rt).Value == blendState.Value)
		{
			RT = 0xFFFFFFFF;
			return;
		}
		RT = rt;
		BlendRef = g_pRender->GetBlendState(rt);
		g_pRender->SetBlendState(rt, blendState);
	}
	
	ENF_INLINE PushBlendState(uint rt, bool enabled)
	{
		enf_assert2(enabled == false, "Use of PushBlendState with enabled == true is forbidden, use parametrized method instead");
		if(g_pRender->GetBlendState(rt).Value == 0xFFFFFFFF)
		{
			RT = 0xFFFFFFFF;
			return;
		}
		RT = rt;
		BlendRef = g_pRender->GetBlendState(rt);
		g_pRender->SetBlendState(rt, enabled);
	}

	ENF_INLINE ~PushBlendState()
	{
		if(RT != 0xFFFFFFFF)
		{
			if (BlendRef == 0xFFFFFFFF)
			{
				g_pRender->SetBlendState(RT, false);
			}
			else
			{
				g_pRender->SetBlendState(RT, BlendRef);
			}
		}
	}
};

//-----------------------------------------------------------------------
class PushViewport
{
	RenderViewport	Value;

public:
	ENF_INLINE PushViewport(const RenderViewport& vp)
	{
		Value = g_pRender->GetViewport();
		g_pRender->SetViewport(vp);
	}

	ENF_INLINE ~PushViewport()
	{
		g_pRender->SetViewport(Value);
	}
};

//-----------------------------------------------------------------------
/*!
Helper macros for quick implementation
*/
//-----------------------------------------------------------------------
#define SET_RS(state, value) g_pRender->SetRenderState(state, value)

ENF_INLINEFUNC void SET_DSS(DepthStencilStateHandle handle, uint stencilRef = 0)
{
	g_pRender->SetDepthStencilState(handle, stencilRef);
}

ENF_INLINEFUNC void SET_BLEND(uint index, BlendState blendState)
{
	g_pRender->SetBlendState(index, blendState);
}

ENF_INLINEFUNC void SET_BLEND(uint index, ebool enable)
{
	g_pRender->SetBlendState(index, enable);
}

#define SET_SAS(stage, stateHandle) enf::g_pRender->SetSamplerAddressState(stage, stateHandle)
#define SET_VSAS(stage, stateHandle) enf::g_pRender->SetVertexSamplerAddressState(stage, stateHandle)

#define SET_SFS(stage, filterStateHandle) enf::g_pRender->SetSamplerFilterState(stage, filterStateHandle)
#define SET_VSFS(stage, filterStateHandle) enf::g_pRender->SetVertexSamplerFilterState(stage, filterStateHandle)

#define SET_TEX(stage, tex) enf::g_pRender->SetTexture(stage, tex)
#define SET_ALPHAKILL(stage, enable) enf::g_pRender->SetAlphaKill(stage, enable)
#define SET_VTEX(stage, tex) enf::g_pRender->SetVertexTexture(stage, tex)

#define SET_PSH(sh) g_pRender->SetPixelShader(sh)

ENF_INLINEFUNC void SET_PSH_CONST(int reg, const float *value, uint size = 1)
{
	g_pRender->SetPixelShaderConstantF(reg, value, size);
}
ENF_INLINEFUNC void SET_PSH_CONST(int reg, const Vector3& value)
{
	g_pRender->SetPixelShaderConstant(reg, value);
}
ENF_INLINEFUNC void SET_PSH_CONST(int reg, const Vector4& value)
{
	g_pRender->SetPixelShaderConstant(reg, value);
}
ENF_INLINEFUNC void SET_PSH_CONST(int reg, const FColor& value)
{
	g_pRender->SetPixelShaderConstantF(reg, value.colors, 1);
}

ENF_INLINEFUNC void SET_PSH_CONSTI(int reg, const int *value, uint size = 1)
{
	g_pRender->SetPixelShaderConstantI(reg, value, size);
}
ENF_INLINEFUNC void SET_PSH_CONSTI(int reg, int i)
{
	g_pRender->SetPixelShaderConstantI(reg, &i, 1);
}

ENF_INLINEFUNC void SET_PSH_CONSTB(int reg, const int *value, uint size = 1)
{
	g_pRender->SetPixelShaderConstantB(reg, value, size);
}
ENF_INLINEFUNC void SET_PSH_CONSTB(int reg, bool b)
{
	int bval = b ? 1 : 0;
	g_pRender->SetPixelShaderConstantB(reg, &bval, 1);
}

#define SET_VSH(sh) g_pRender->SetVertexShader(sh)

ENF_INLINEFUNC void SET_VSH_CONST(int reg, const Vector2& value)
{
	g_pRender->SetVertexShaderConstant(reg, value);
}

ENF_INLINEFUNC void SET_VSH_CONST(int reg, const Vector3& value)
{
	g_pRender->SetVertexShaderConstant(reg, value);
}

ENF_INLINEFUNC void SET_VSH_CONST(int reg, const Vector4& value)
{
	g_pRender->SetVertexShaderConstant(reg, value);
}

ENF_INLINEFUNC void SET_VSH_CONST(int reg, const FColor& value)
{
	g_pRender->SetVertexShaderConstant(reg, value);
}

ENF_INLINEFUNC void SET_VSH_CONST(int reg, const float* value, uint size)
{
	g_pRender->SetVertexShaderConstantF(reg, value, size);
}

ENF_INLINEFUNC void SET_VSH_MATRIX(int reg, const Matrix44& value)
{
	g_pRender->SetVertexShaderConstantF(reg, &value._11, 4);
}

ENF_INLINEFUNC void SET_VSH_MATRIX(int reg, const Matrix34& value)
{
	g_pRender->SetVertexShaderConstantF(reg, &value._11, 3);
}

ENF_INLINEFUNC void SET_VSH_CONSTB(int reg, const int *value, uint size = 1)
{
	g_pRender->SetVertexShaderConstantB(reg, value, size);
}

ENF_INLINEFUNC void SET_VSH_CONSTB(int reg, ebool b)
{
	if(sizeof(ebool) == sizeof(int))
		g_pRender->SetVertexShaderConstantB(reg, (const int*)&b, 1);
	else
	{
		int bval = b ? 1 : 0;
		g_pRender->SetVertexShaderConstantB(reg, &bval, 1);
	}
}

ENF_INLINEFUNC void SET_VSH_CONSTI(int reg, const int *value, uint size = 1)
{
	g_pRender->SetVertexShaderConstantI(reg, value, size);
}

ENF_INLINEFUNC void SET_VSH_CONSTI(int reg, int value)
{
	int val[4] = {value, 0, 0, 0};
	g_pRender->SetVertexShaderConstantI(reg, val, 1);
}

//@}
ENF_INLINEFUNC PixelShader* GET_PSH(CStr name, ProfileType profile, const Define* defines = NULL, uint numdefs = 0)
{
	return g_pRender->GetPShader(name, profile, defines, numdefs);
}
ENF_INLINEFUNC VertexShader* GET_VSH(CStr name, ProfileType profile, const Define* defines = NULL, uint numdefs = 0)
{
	return g_pRender->GetVShader(name, profile, defines, numdefs);
}


//-----------------------------------------------------------------------
/*!
profile events
*/
//-----------------------------------------------------------------------
#if defined(ENF_DEVELOPER) && !defined(ENF_DUMMYRENDER)
	void PUBLIC_API _D3DPERF_BeginEvent(uint color, const wchar_t* name);
	void PUBLIC_API _D3DPERF_BeginEvent(uint color, const char* name, ...);
	void PUBLIC_API _D3DPERF_SetMarker(uint color, const char* name, ...);
	void PUBLIC_API _D3DPERF_EndEvent();
	#define ENF_BEGIN_EVENT(color, name) enf::_D3DPERF_BeginEvent(color, L ## name);
	#define ENF_BEGIN_EVENT_NC(color, name) enf::_D3DPERF_BeginEvent(color, name);
	#define ENF_BEGIN_EVENT2(color, name, value) enf::_D3DPERF_BeginEvent(color, name, value);
	#define ENF_BEGIN_EVENT3(color, name, value, value2) enf::_D3DPERF_BeginEvent(color, name, value, value2);
	#define ENF_SET_MARKER(color, name) enf::_D3DPERF_SetMarker(color, name);
	#define ENF_SET_MARKER2(color, name, value) enf::_D3DPERF_SetMarker(color, name, value);
	#define ENF_END_EVENT() enf::_D3DPERF_EndEvent();
#else
	#define ENF_BEGIN_EVENT(color, name)
	#define ENF_BEGIN_EVENT_NC(color, name)
	#define ENF_SET_MARKER(color, name)
	#define ENF_SET_MARKER2(color, name, value)
	#define ENF_BEGIN_EVENT2(color, name, value)
	#define ENF_BEGIN_EVENT3(color, name, value, value2)
	#define ENF_END_EVENT()
#endif


#define EC_BLACK 0x00000000
#define EC_GRAY 0x7f7f7f7f
#define EC_RED 0xffff0000
#define EC_GREEN 0xff00ff00
#define EC_BLUE 0xff0000ff
}
#endif //ENF_RENDERMAIN_H
