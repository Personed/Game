//---------------------------------------------		--------------------------------
// File: enf_rendmain.cpp
//
// Desc: Renderer class
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "render/enf_irendmain.h"
#include "render/enf_texture.h"
#include "system/enf_profiler.h"
#include "tinyxml/tinyxml.h"
#include "common/enf_diag.h"
#include "common/enf_quicksort.h"
#include "enf_main.h"
#include "engine/enf_engine.h"


#ifdef ENF_PS3
	#include "../../shaders/registers.h"
	#include <cell/gcm/gcm_method_data.h>
	#include "common/enf_mathrect.h"
	#include "common/enf_quicksort.h"
	#include <cell/resc.h>

	/*!
	log to console the same content going to GPAD (when RSX falls, it helps to distinguish where)
	*/
	//#define PS3_GPU_MARKER_LOG

	#ifdef PS3_GPU_MARKER_LOG
		static const char levelMarkerSpace[] = "          ";
		static int levelMarker = 0;
	#endif

	/*!
	if defined, debug information from patch buffer are putted out to console
	*/
	//#define DEBUG_PATCH_BUFFER


	/*!
	copy mem function to copy data to command buffer
	*/
	#define GCM_MEMCPY CELL_GCM_MEMCPY 
#endif

#ifdef __APPLE__
	#include <gl/glfw.h>
#endif

namespace enf
{
Renderer* g_pRender = NULL;

#if defined(ENF_DX9) || defined(ENF_OGL) || defined(ENF_PS3)
ENF_ALIGN16(ShaderVec)	Renderer::m_PSConstants[Renderer::MAX_PS_CONSTANTS];
uint Renderer::m_iStartPSConstants = -1;
uint Renderer::m_iNumPSConstants = 0;
ENF_ALIGN16(ShaderVec)	Renderer::m_VSConstants[Renderer::MAX_VS_CONSTANTS];
uint Renderer::m_iStartVSConstants = -1;
uint Renderer::m_iNumVSConstants = 0;
#endif

#ifdef ENF_OGL
	VertexShader* g_pScreenspace = NULL;
	PixelShader* g_pPColor = NULL;
	IVertexDeclaration g_pScreenQuadVD = NULL;
	GLuint m_iCurrentFB = 0;
	GLuint m_iCurrentRB = 0;
	GLuint m_iCurrentDB = 0;
	GLuint m_iPrimaryDepthBuffer = 0;
	GLuint m_iPrimaryFrameBuffer = 0;
	GLuint m_PrimaryRenderBuffer = 0;
#endif

#ifdef ENF_PS3
	int Renderer::GCMFinishRefValue = 0;
	const void* Renderer::DISABLE_VERTEX_OFFSET = (const void*) 0xFFFFFFFF;
	const uint Renderer::AVAILABLE_MRT_RT[] = 
	{
		CELL_GCM_SURFACE_TARGET_0, 
		CELL_GCM_SURFACE_TARGET_MRT1, 
		CELL_GCM_SURFACE_TARGET_MRT2, 
		CELL_GCM_SURFACE_TARGET_MRT3
	};	

	void GCMSetNullTexture(uint texUnit);
	void GCMSetNullVertexTexture(uint texUnit);

	//-----------------------------------------------------------------------------
	/*!
	buffer for shaders online patching
	*/
	//-----------------------------------------------------------------------------
	class ShaderPatchBuffer
	{
		static const uint SHADER_RING_BUFFER_SIZE		= 1*1024*1024;		//size of ring buffer for one frame
		static const uint SHADER_RING_BUFFER_ALIGN	= 64;					//align must be to 64 bytes
		static const uint SHADER_RSX_PREFETCH			= 1024;				//RSX prefetches some shader data, take care not to overlap
		static const uint SHADER_BUFFERS					= 2;					//number of buffers, should correspond with engine frame buffers

		//-----------------------------------------------------------------------------
		struct LocalBuffer
		{
			uchar* m_pBuffer;
			uint m_iBufferOffset;
			uchar* m_pCurrent;
			
			#ifdef DEBUG_PATCH_BUFFER
				uint m_iNum;
				uint m_iMaxMem;
				uint m_iMissing;
			#endif

			//-----------------------------------------------------------------------------
			void Init(uchar *current)
			{
				m_pCurrent = m_pBuffer = current;
				m_iBufferOffset = MemoryManager::LocalOffsetFromPtr(m_pBuffer);

				#ifdef DEBUG_PATCH_BUFFER
					m_iNum = 0;
					m_iMaxMem = 0;
					m_iMissing = 0;
				#endif
			}

			//-----------------------------------------------------------------------------
			ENF_INLINE uchar* Alloc(uint size, uint &offset)
			{
				#ifdef DEBUG_PATCH_BUFFER
					m_iNum++;
				#endif

				if (m_pCurrent + size >= m_pBuffer + SHADER_RING_BUFFER_SIZE - SHADER_RSX_PREFETCH)
				{
					#ifdef DEBUG_PATCH_BUFFER
						m_iMissing += size;
					#endif

					return NULL;
				}

				uchar* curr = m_pCurrent;
				m_pCurrent = (uchar*) enf_align((uint) m_pCurrent + size, SHADER_RING_BUFFER_ALIGN);

				offset = m_iBufferOffset + curr - m_pBuffer;
				return curr;
			}

			//-----------------------------------------------------------------------------
			ENF_INLINE bool HasEnoughSpace(uint size)
			{
				return m_pCurrent + size < m_pBuffer + SHADER_RING_BUFFER_SIZE - SHADER_RSX_PREFETCH;
			}

			//-----------------------------------------------------------------------------
			void OnFlip()
			{
				#ifdef DEBUG_PATCH_BUFFER
					uint curr = m_pCurrent - m_pBuffer;
					m_iMaxMem = enf_max(m_iMaxMem, curr);

					if (m_iMissing)
					{
						LogF(LL_NORMAL, "PatchBuffer full,  %dKB missing, the rest is rendering in normal way", m_iMissing/1024);	
					}
					else
					{
						LogF(LL_NORMAL, "PatchBuffer used %dKB (%2.2fperc), num of patched shaders %d, maximum frame size %d", curr/1024, (float) 100.0f*curr/SHADER_RING_BUFFER_SIZE, m_iNum, m_iMaxMem);
					}

					m_iNum = 0;
					m_iMissing = 0;
				#endif

				m_pCurrent = m_pBuffer;
			}
		};

		uchar*		m_pMainBuffer; 
		LocalBuffer m_LocalBuffers[SHADER_BUFFERS];
		uint			m_iBufferIndex;

	public:
		//-----------------------------------------------------------------------------
		ShaderPatchBuffer()
		{
			uint localBufferSize = enf_align(SHADER_RING_BUFFER_SIZE, SHADER_RING_BUFFER_ALIGN);
			uint fullSize = SHADER_BUFFERS*localBufferSize;

			m_pMainBuffer = (uchar*) LOCAL_ALLOC(fullSize, SHADER_RING_BUFFER_ALIGN);
			m_iBufferIndex = 0;

			for (uint i = 0; i < SHADER_BUFFERS; i++)
			{
				m_LocalBuffers[i].Init(m_pMainBuffer + i*localBufferSize);
			}
		}

		//-----------------------------------------------------------------------------
		~ShaderPatchBuffer()
		{
			LOCAL_FREE(m_pMainBuffer);			
		}

		//-----------------------------------------------------------------------------
		bool HasEnoughSpace(uint size)
		{
			return m_LocalBuffers[m_iBufferIndex].HasEnoughSpace(size);
		}

		//-----------------------------------------------------------------------------
		ENF_INLINE uint AllocAndCopy(void* origShader, uint size)
		{
			uint offset;
			uchar* dst = m_LocalBuffers[m_iBufferIndex].Alloc(size, offset);

			if (!dst)
				return 0xFFFFFFFF;

			memcpy(dst, origShader, size);

			return offset;
		}

		//-----------------------------------------------------------------------------
		void OnFlip()
		{
			m_LocalBuffers[m_iBufferIndex].OnFlip();

			m_iBufferIndex = (m_iBufferIndex + 1)%SHADER_BUFFERS;
		}
	};
#endif

#ifdef ENF_RENDISSTATIC
_Direct3DDevice*				Renderer::m_pd3dDevice;
LPDIRECT3DVERTEXSHADER9		Renderer::m_pVShader;
LPDIRECT3DPIXELSHADER9		Renderer::m_pPShader;
RenderStats						Renderer::m_Stats;
LPDIRECT3DBASETEXTURE9		Renderer::m_pTextures[MAX_TEXTURES];
LPDIRECT3DBASETEXTURE9		Renderer::m_pVertexTextures[MAX_VERTEX_TEXTURES];
BlendState						Renderer::m_iBlendStates[MAX_RENDER_TARGETS];
SamplerFilterStateHandle	Renderer::m_iSamplerFilterStateValue[MAX_TEXTURES];
SamplerFilterStateHandle	Renderer::m_iSamplerFilterStateValue2[MAX_VERTEX_TEXTURES];
SamplerAddressStateHandle	Renderer::m_iSamplerAddressStateValue[MAX_TEXTURES];
SamplerAddressStateHandle	Renderer::m_iSamplerAddressStateValue2[MAX_VERTEX_TEXTURES];
IDirect3DIndexBuffer9*		Renderer::m_pIndexBuffer;
Renderer::StreamState		Renderer::m_Streams[MAX_STREAMS];
SArray<SamplerAddressState,64>	Renderer::m_SamplerAddressStates;
SArray<DepthStencilStateDef,64> Renderer::m_DepthStencilStateDefs;
DepthStencilStateHandle		Renderer::m_iDepthStencilState = -1;
uint								Renderer::m_iStencilRef = 0;
uint								Renderer::m_iRTStackDepth[MAX_RENDER_TARGETS];

#if defined(ENF_OGL) || defined(ENF_DX9)
uint								Renderer::m_iRenderStateValue[256];
#endif

#ifdef ENF_OGL
DepthStencilStateHandle		Renderer::m_iDepthStencilShadowState;
uint								Renderer::m_iStencilShadowRef;
uint								Renderer::m_iVSConstantsB;
uint								Renderer::m_iPSConstantsB;
SamplerFilterStateHandle	Renderer::m_iSamplerFilterShadowStateValue[];
SamplerFilterStateHandle	Renderer::m_iSamplerFilterShadowStateValue2[];
SamplerAddressStateHandle	Renderer::m_iSamplerAddressShadowStateValue[];
SamplerAddressStateHandle	Renderer::m_iSamplerAddressShadowStateValue2[];
StreamState						Renderer::m_ShadowStreams[];
uint								Renderer::m_iCommitMask;
bool								Renderer::m_bEnableAlphaTest;
uint								Renderer::m_iShadowVertexDeclaration;
uint								Renderer::m_iVertexDeclaration;
Texture*							Renderer::m_pTextures[];
Texture*							Renderer::m_pVertexTextures[];
CGprogram 						Renderer::m_iVShader;
CGprogram 						Renderer::m_iPShader;
GLint								Renderer::m_iIndexBuffer;
Texture*							Renderer::m_pShadowTextures[];
Texture*							Renderer::m_pShadowVertexTextures[];

ENF_INLINEFUNC GLenum GLDPType(PrimitiveType pt)
{
	switch(pt)
	{
	case PT_POINTLIST:
		return GL_POINTS;
	case PT_LINELIST:
		return GL_LINES;
	case PT_LINESTRIP:
		return GL_LINE_STRIP;
	case PT_TRIANGLELIST:
		return GL_TRIANGLES;
	case PT_TRIANGLESTRIP:
		return GL_TRIANGLE_STRIP;
	case PT_TRIANGLEFAN:
		return GL_TRIANGLE_FAN;
	case PT_QUADLIST:
		return GL_QUADS;
	}
	return GL_POINTS;
}

#endif
#endif

#if defined(ENF_OGL) || defined(ENF_PS3)
	uint64 Renderer::m_PSConstChanged = 0;
	uint64 Renderer::m_VSConstChanged = 0;
#endif

#if defined(ENF_OGL)
bool g_bGLReport = false;
void ReportGLError(GLenum err, const char* file, int line)
{
	const char* desc = "";
if(line == 4614)
line=line;
	switch(err)
	{
	case GL_INVALID_ENUM:
		desc = "An unacceptable value is specified for an enumerated argument";
		break;
	case GL_INVALID_VALUE:
		desc = "A numeric argument is out of range.";
		break;
	case GL_INVALID_OPERATION:
		desc = "The specified operation is not allowed in the current state.";
		break;
	case GL_STACK_OVERFLOW:
		desc = "This command would cause a stack overflow.";
		break;
	case GL_STACK_UNDERFLOW:
		desc = "This command would cause a stack underflow.";
		break;
	case GL_OUT_OF_MEMORY:
		desc = "There is not enough memory left to execute the command.";
		break;
	case GL_INVALID_FRAMEBUFFER_OPERATION:
		desc = "Invalid framebuffer operation.";
		break;
	}

	LogF(LL_ERROR, "%s(%d) GL Error %x %s\n", file, line, err, desc);
	g_bGLReport = false;
}

void ReportGLCall(const char* function, const char* file, int line)
{
	if(g_bGLReport)
	{
	//	LogF(LL_NORMAL, "%s(%d) GL call %s\n", file, line, function);
	}
}

void ReportGLFBOStatus(GLenum fbo, const char* file, int line)
{
	GLenum status = glCheckFramebufferStatus(fbo);

	if(status == GL_FRAMEBUFFER_COMPLETE) // Everything's OK
		return;

	CStr desc;

	switch(status) 
	{                                          
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
		desc = "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
		desc = "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT:
		desc = "GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS";
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT:
		desc = "GL_FRAMEBUFFER_INCOMPLETE_FORMATS\n";
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER:
		desc = "GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER";
		break;
	case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER:
		desc = "GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER";
		break;
	case GL_FRAMEBUFFER_UNSUPPORTED:
		desc = "GL_FRAMEBUFFER_UNSUPPORTED";
		break;
	default:
		desc = "Unknown ERROR";
	};
	
	
	LogF(LL_ERROR, "%s(%d) GL FBO Error %x %s\n", file, line, status, desc.cstr());
}
#endif



//----------------------------------------------------------------------
Renderer* CreateRenderer()
{
	return ENF_NEW IRenderer();
}

//----------------------------------------------------------------------
#ifdef ENF_DX9
uint Renderer::GetAvailableMemory()
{
	return m_pd3dDevice->GetAvailableTextureMem();
}

uint Renderer::GetAllocatedMemory()
{
	return m_uiAvailMem - m_pd3dDevice->GetAvailableTextureMem();
}
#else
uint Renderer::GetAvailableMemory()
{
	return 0;
}

uint Renderer::GetAllocatedMemory()
{
	return 0;
}
#endif

//----------------------------------------------------------------------
bool Renderer::OnChangeSettings(TiXmlElement* xmlnode)
{
	return true;
}

//----------------------------------------------------------------------
#if defined(ENF_DEVELOPER) || defined(ENF_DX9)

ENF_INLINEFUNC CStr GetFormatName(TextureFormat format)
{
	switch (format)
	{
		case TEXFMT_A8R8G8B8             : return "A8R8G8B8";
		case TEXFMT_X8R8G8B8             : return "X8R8G8B8";
		case TEXFMT_R5G6B5               : return "R5G6B5";
		case TEXFMT_X1R5G5B5             : return "X1R5G5B5";
		case TEXFMT_A1R5G5B5             : return "A1R5G5B5";
		case TEXFMT_A4R4G4B4             : return "A4R4G4B4";
		case TEXFMT_A8                   : return "A8";
		case TEXFMT_X4R4G4B4             : return "X4R4G4B4";
		case TEXFMT_G16R16               : return "G16R16";
		case TEXFMT_A2R10G10B10          : return "A2R10G10B10";
		case TEXFMT_L8                   : return "L8";
		case TEXFMT_A8L8                 : return "A8L8";
		case TEXFMT_V8U8                 : return "V8U8";
		case TEXFMT_L6V5U5               : return "L6V5U5";
		case TEXFMT_X8L8V8U8             : return "X8L8V8U8";
		case TEXFMT_Q8W8V8U8             : return "Q8W8V8U8";
		case TEXFMT_V16U16               : return "V16U16";
		case TEXFMT_A2W10V10U10          : return "A2W10V10U10";
		case TEXFMT_UYVY                 : return "UYVY";
		case TEXFMT_YUY2                 : return "YUY2";
		case TEXFMT_DXT1                 : return "DXT1";
		case TEXFMT_DXT2                 : return "DXT2";
		case TEXFMT_DXT3                 : return "DXT3";
		case TEXFMT_DXT4                 : return "DXT4";
		case TEXFMT_DXT5                 : return "DXT5";
		case TEXFMT_CTX1						: return "CTX1"; 
		case TEXFMT_D32                  : return "D32";
		case TEXFMT_D24S8                : return "D24S8";
		case TEXFMT_D24X8                : return "D24X8";
		case TEXFMT_D16                  : return "D16";
		case TEXFMT_D24FS8               : return "D24FS8";
		case TEXFMT_L16                  : return "L16";
		case TEXFMT_R16F						: return "R16F";
		case TEXFMT_G16R16F					: return "G16R16F";
		case TEXFMT_A16B16G16R16F			: return "A16B16G16R16F";
		case TEXFMT_R32F						: return "R32F";
		case TEXFMT_G32R32F					: return "G32R32F";
		case TEXFMT_A32B32G32R32F			: return "A32B32G32R32F";
	}

	return "unknown";
}
#endif

//----------------------------------------------------------------------
struct ResItem
{
	const DeviceResource*	resource;
	size_t				size;
};

struct ResType
{
	const char* type;
	size_t		size;
	uint			count;
	ResType():type(NULL), size(0), count(0) {}
	ResType(const char* t, size_t s = 0): type(t), size(s), count(1) {}
	bool operator<(const ResType& other) const
	{
		return type < other.type;
	}
	bool operator>(const ResType& other) const
	{
		return type > other.type;
	}
	bool operator==(const ResType& other) const
	{
		return type == other.type;
	}
};

class SortItems
{
public:
	inline int operator()(const ResItem& e1, const ResItem& e2) const
	{
		if(e1.size > e2.size) return -1;
		if(e1.size < e2.size) return 1;
		return e1.resource->GetName().cmp(e2.resource->GetName());
	}
};
//----------------------------------------------------------------------
void IRenderer::DumpResources() const
{
#if defined(ENF_DEVELOPER) || defined(ENF_DX9)
	Array<ResItem> resources;

	for(const DeviceResource *res = m_pResources; res != NULL; res = res->m_pNext)
	{
		size_t size = res->GetMemorySize();
		if(size == 0)
			continue;
		ResItem& item = resources.Append();
		item.resource = res;
		item.size = size;
	}

	QuickSortCmp<ResItem, SortItems>(resources.GetCardinality(), resources.GetArray());

	Set<ResType> types;

	size_t totalSize = 0;
	for(uint n = 0; n < resources.GetCardinality(); n++)
	{
		const DeviceResource* res = resources[n].resource;
		CStr typeName = res->GetTypeName();
		CStr name = res->GetName();
		uint size = (uint)res->GetMemorySize();
		totalSize += size;

		if (name.IsEmpty())
			name = "unknown";

		uint typeIndex = types.GetIndexOf(ResType(typeName.cstr()));
		if(typeIndex != INDEX_NOT_FOUND)
		{
			types[typeIndex].size += size;
			types[typeIndex].count++;
		}
		else
			types.Insert(ResType(typeName.cstr(), size));

		const Texture* tex = Texture::SafeCast(res);
		if(tex)
		{
			LogF(LL_NORMAL, "%08d %s '%s %dx%d %s mips %d %s'", 
				size, 
				name.cstr(), 
				typeName.cstr(), 
				tex->GetWidth(), 
				tex->GetHeight(), 
				GetFormatName(tex->GetFormat()), 
				tex->GetNumLevels(), 
				tex->IsCube() ? "cube" : "");
		}
		else
		{
			const VertexBuffer* vb = VertexBuffer::SafeCast(res);

			if (vb)
			{
				LogF(LL_NORMAL, "%08d %s '%s vertices %d stride %d'", 
					size, 
					name.cstr(), 
					typeName.cstr(), 
					vb->GetNumVerts(), 
					vb->GetSize()/vb->GetNumVerts());
			}
			else
			{
				const IndexBuffer* ib = IndexBuffer::SafeCast(res);

				if (ib)
				{
					LogF(LL_NORMAL, "%08d %s '%s indices %d format %s'", 
						size, 
						name.cstr(), 
						typeName.cstr(), 
						ib->GetSize(),
						size/ib->GetSize() == 2 ? "16bit" : "32bit");
				}
				else
				{
					LogF(LL_NORMAL, "%08d %s '%s'", size, name.cstr(), typeName.cstr());
				}
			}
		}
	}
	LogF(LL_NORMAL, "Totals:");
	for(uint n = 0; n < types.GetCardinality(); n++)
	{
		LogF(LL_NORMAL, "Type %s count %d %08d bytes", types[n].type, types[n].count, types[n].size);
	}

	LogF(LL_NORMAL, "Total %08d bytes", totalSize);
#endif
}

//----------------------------------------------------------------------
uint Renderer::GetBaseEDRAMOffset() const
{
#ifdef ENF_XENON
	return XGSurfaceSize(m_Setup.m_iBackBufferWidth, m_Setup.m_iBackBufferHeight, (D3DFORMAT)m_Setup.m_BackBufferFormat, (D3DMULTISAMPLE_TYPE)m_Setup.m_FSAAType) +
			XGSurfaceSize(m_Setup.m_iBackBufferWidth, m_Setup.m_iBackBufferHeight, (D3DFORMAT)m_Setup.m_DepthBufferFormat, D3DMULTISAMPLE_NONE);
#else
	return 0;
#endif
}

//----------------------------------------------------------------------
TextureFormat Renderer::GetBestDepthTextureFormat() const
{
#ifdef ENF_XENON
	return TEXFMT_R32F;
#elif defined(ENF_DX9)
	TextureFormat depthfmt[] = 
	{
		TEXFMT_R32F,
		TEXFMT_R16F,
		TEXFMT_G16R16F
	};

	for(uint f = 0; f < 3; f++)
	{
		if(CheckDeviceFormat(D3DUSAGE_RENDERTARGET, TextureTypeID, depthfmt[f])==EE_OK)
		{
			return depthfmt[f];
		}
	}
	return TEXFMT_UNKNOWN;
#elif defined(ENF_PS3)
	return TEXFMT_R32F;
#elif defined(ENF_OGL)
	return TEXFMT_R32F;
#endif
}

//----------------------------------------------------------------------
bool Renderer::EnablePrimaryRT(bool enable)
{
	bool prev = !m_bDisablePrimaryRT;
	m_bDisablePrimaryRT = !enable;
	return prev;
}

//----------------------------------------------------------------------
ERESULT Renderer::CheckDeviceFormat(uint usage, ResourceType resourcetype, TextureFormat format) const
{
#if defined(ENF_DX9) || defined(ENF_XENON)
	D3DRESOURCETYPE rtype;
	uint dxusage = 0;
	switch(resourcetype)
	{
	case VertexBufferTypeID:
		rtype = D3DRTYPE_VERTEXBUFFER;
		break;
	case IndexBufferTypeID:
		rtype = D3DRTYPE_INDEXBUFFER;
		break;
	case TextureTypeID:
		rtype = D3DRTYPE_TEXTURE;
		break;
	case RenderTargetTypeID:
		rtype = D3DRTYPE_TEXTURE;
		dxusage |= D3DUSAGE_RENDERTARGET;
		break;
	}
	dxusage |= usage;

	
	D3DFORMAT devformat;

	//Isn't it bug in DX??? It fails with TEXFMT_A8R8G8B8, but it's possible to create it anyway.
	devformat = m_Setup.m_BackBufferFormat == TEXFMT_A8R8G8B8 ? D3DFMT_X8R8G8B8 : (D3DFORMAT)m_Setup.m_BackBufferFormat;

	HRESULT hr = m_pD3D->CheckDeviceFormat(m_iAdapter, D3DDEVTYPE_HAL, devformat, dxusage, rtype, (D3DFORMAT)format);

	if(hr == D3D_OK)
		return EE_OK;
	if(hr == D3DERR_NOTAVAILABLE)
		return EE_UNSUPPORTED;
	if(hr == D3DERR_INVALIDCALL )
		return EE_INVALIDPARAM;

#elif defined(ENF_PS3) 
	if(usage & RES_USAGE_QUERY_VERTEXTEXTURE)
	{
		if(resourcetype == TextureTypeID)
		{
			if(format == TEXFMT_R32F || format == TEXFMT_A32B32G32R32F )
				return EE_OK;
		}
		return EE_ERROR;
	}
	return EE_OK;
#elif defined(ENF_OGL)
	//GLTODO
	return EE_OK;
#endif
	return EE_ERROR;
}


//----------------------------------------------------------------------
#if defined(ENF_DX9) || defined(ENF_XENON)
_D3DSurface* Renderer::GetRenderTargetSurface(uint rtindex)
{
	_D3DSurface* surf = NULL;
	
	HRESULT hr = m_pd3dDevice->GetRenderTarget(rtindex, &surf);

	return (hr == D3D_OK) ? surf : NULL;
}
#endif
//----------------------------------------------------------------------
RenderQuery::RenderQuery(IRenderer *dev, uint type): DeviceResource(dev)
{
	m_iQueryType = type;
#if defined(ENF_DX9) || defined(ENF_XENON)
	m_pQuery = NULL;
#elif defined(ENF_OGL) 
	m_iQuery = 0;
#elif defined(ENF_PS3)
	m_iQuery = 0xFFFFFFFF;
#endif

#ifdef ENF_DX9
	m_bManaged = false;
#endif
	m_bIssued = false;
	Allocate();
}

//---------------------------------------------------------------------
ERESULT RenderQuery::Allocate()
{
	m_bIssued = false;
#if defined(ENF_DX9) || defined(ENF_XENON)
	if(GetDevice()->GetDXDevice()->CreateQuery((D3DQUERYTYPE)m_iQueryType, &m_pQuery) != D3D_OK)
	{
		enf_assert(m_pQuery);
		m_pQuery = NULL;
		return EE_ERROR;
	}
#elif defined(ENF_PS3)
	if(m_iQueryType == QUERYTYPE_OCCLUSION)
	{
		m_iQuery = reinterpret_cast<IRenderer*>(g_pRender)->AllocQuery();
	}
#elif defined(ENF_OGL)
	if(m_iQueryType == QUERYTYPE_OCCLUSION)
	{
		glGenQueriesARB(1, &m_iQuery);
	}
#endif
	return EE_OK;
}

//----------------------------------------------------------------------
void RenderQuery::Free()
{
	int ref = -1;

#ifdef ENF_XENON
	if(m_pQuery)
	{
		switch(m_iQueryType)
		{
		case D3DQUERYTYPE_OCCLUSION:
			DWORD qdata;
			m_pQuery->GetData(&qdata, sizeof(qdata), D3DGETDATA_FLUSH);
			break;

		case D3DQUERYTYPE_EVENT:
			BOOL edata;
			m_pQuery->GetData(&edata, sizeof(edata), D3DGETDATA_FLUSH);
			break;
		}
		ref = m_pQuery->Release();
		m_pQuery = NULL;
		enf_assert(ref == 0);
	}
#elif defined(ENF_DX9)
	if(m_pQuery)
	{
		m_pQuery->GetData(NULL, 0, D3DGETDATA_FLUSH);
		ref = m_pQuery->Release();
		m_pQuery = NULL;
		enf_assert(ref == 0);
	}
#elif defined(ENF_PS3)
	if(m_iQuery && m_iQueryType == QUERYTYPE_OCCLUSION)
	{
		reinterpret_cast<IRenderer*>(g_pRender)->FreeQuery(m_iQuery);
		m_iQuery = 0xFFFFFFFF;
	}
#elif defined(ENF_OGL)
	if(m_iQuery && m_iQueryType == QUERYTYPE_OCCLUSION)
	{
		glDeleteQueriesARB(1, &m_iQuery);
		m_iQuery = 0;
	}
#endif
}

//----------------------------------------------------------------------
void RenderQuery::Issue(QueryCommand cmd)
{
#if defined(ENF_DX9) || defined(ENF_XENON)
	if(m_pQuery)
	{
		if(cmd == D3DISSUE_BEGIN)
			m_bIssued = false;
		else if(cmd == D3DISSUE_END)
			m_bIssued = true;

		m_pQuery->Issue((DWORD)cmd);
	}
#elif defined(ENF_PS3)
	enf_assert(m_iQuery != 0xFFFFFFFF);

	if(m_iQueryType == QUERYTYPE_OCCLUSION)
	{
		if(cmd == QC_ISSUE_BEGIN)
		{
			GCM_CALL(cellGcmSetZpassPixelCountEnable)(gCellGcmCurrentContext, CELL_GCM_TRUE); 
			GCM_CALL(cellGcmSetClearReport)(gCellGcmCurrentContext, CELL_GCM_ZPASS_PIXEL_CNT);
			
			m_bIssued = false;
		}
		else if(cmd == QC_ISSUE_END)
		{
			GCM_CALL(cellGcmSetReport)(gCellGcmCurrentContext, CELL_GCM_ZPASS_PIXEL_CNT, m_iQuery);
			GCM_CALL(cellGcmSetZpassPixelCountEnable)(gCellGcmCurrentContext, CELL_GCM_FALSE);
			
			m_bIssued = true;
		}
	}
#elif defined(ENF_OGL)
	if(m_iQueryType == QUERYTYPE_OCCLUSION)
	{
		if(cmd == QC_ISSUE_BEGIN)
		{
			glBeginQueryARB(GL_SAMPLES_PASSED_ARB, m_iQuery);
			m_bIssued = false;
		}
		else if(cmd == QC_ISSUE_END)
		{
			glEndQueryARB(GL_SAMPLES_PASSED_ARB);
			m_bIssued = true;
		}
	}
#endif
}

//----------------------------------------------------------------------
ERESULT RenderQuery::GetData(void* data, uint size, uint flags)
{
#if defined(ENF_DX9) || defined(ENF_XENON)
	if(m_pQuery && m_pQuery->GetData(data, size, flags) == S_OK)
		return EE_OK;
#elif defined(ENF_PS3)
	enf_assert(m_iQuery != 0xFFFFFFFF);

	if(m_iQueryType == QUERYTYPE_OCCLUSION)
	{
		*(uint*)data = cellGcmGetReport(CELL_GCM_ZPASS_PIXEL_CNT, m_iQuery);
		return EE_OK;
	}
#elif defined(ENF_OGL)

	if(m_iQueryType == QUERYTYPE_OCCLUSION)
	{
		GLuint tmpData;
		glGetQueryObjectuivARB(m_iQuery, GL_QUERY_RESULT_AVAILABLE_ARB, &tmpData);
		if(tmpData == GL_TRUE)
		{
			glGetQueryObjectuivARB(m_iQuery, GL_QUERY_RESULT_ARB, &tmpData);
			*(uint*)data = tmpData;
			return EE_OK;
		}
	}
#endif
	return EE_ERROR;
}

#ifdef ENF_PS3

//----------------------------------------------------------------------
IRenderer::CQueryResources::CQueryResources()
{
	for (uint i = 0; i < MAX_QUERIES; i++)
	{
		QueryID.Insert(i);
	}
}

//----------------------------------------------------------------------
uint IRenderer::CQueryResources::Alloc()
{
	if (!QueryID.IsEmpty())	
	{
		return QueryID.Pop();
	}

	#ifdef ENF_DEVELOPER
		else
		{
			LogF(LL_NORMAL, "IRenderer::CQueryResources: query list empty");
		}
	#endif

	return 0xFFFFFFFF;
}

//----------------------------------------------------------------------
void IRenderer::CQueryResources::Free(uint id)
{
	if (id != 0xFFFFFFFF)
	{
		QueryID.Insert(id);
	}

	#ifdef ENF_DEVELOPER
	else
	{
		LogF(LL_NORMAL, "IRenderer::CQueryResources: bad query ID");
	}
	#endif
}

#endif

//----------------------------------------------------------------------
RenderQuery* IRenderer::CreateQuery(RenderQueryType type)
{
	enf_rlsassert(CheckThread());
	enf_assert(CanBeUsed());
	RenderQuery* query = ENF_NEW RenderQuery(this, type);
	AddResource(query);

	return query;
}

//----------------------------------------------------------------------
ERESULT DeviceResource::Allocate()
{
	return EE_OK;
}

//----------------------------------------------------------------------
#ifdef ENF_DX9
ERESULT DeviceResource::AfterReset()
{
	if(!m_bManaged)
	{
		if(HasType(SwapChainTypeID))
		{
			LogF(LL_NORMAL, "SwapChain::AfterReset");
		}
		return Allocate();
	}

	return EE_OK;
}
#endif
//----------------------------------------------------------------------
int DeviceResource::Release()
{
	if(Unref() == 0)
	{
		if(!m_pDevice->CheckThread())
		{
			m_pDevice->DelayRelease(this);
		}
		else
		{
			Free();
			m_pDevice->RemoveResource(this);
			delete this;
		}
		return 0;
	}
	return RefCount();
}

//-----------------------------------------------------------------------
IRenderer::IRenderer():
	m_pResources(NULL),
	m_iFSAA(0L),
	m_iFSAAQuality(0L),
	m_bNoShaderGen(false),
#ifdef ENF_HWCURSOR
	m_bCursorVisible(false),
	m_iCurrentCursor(0),
#endif
	m_iLimiterBottom(0),
	m_iLimiterTop(0),
	m_iShaderCompile(0),
	m_iSuspended(0),
	m_iUsed(0),
	m_iCancelSuspend(0),
	m_bReady(false)
{
	#ifdef ENF_PS3
		m_pCommandBuffer = NULL;


	#endif
	
	memset(m_pLimiterQueries, 0, sizeof(m_pLimiterQueries));
	//init variables
	for(uint r = 0; r < MAX_RENDER_TARGETS; r++)
		m_iRTStackDepth[r] = 0;
}

//-----------------------------------------------------------------------
Renderer::Renderer():
#if defined(ENF_DX9) || defined(ENF_XENON)
	m_hWnd(NULL),
	m_pD3D(NULL),
#endif
	m_uiAvailMem(0),
	m_iLock(0),
	m_iAdapter(0L),
	m_iTargetWidth(0),
	m_iTargetHeight(0),
	m_bInScene(false),
#ifdef ENF_DX9
	m_pd3dDevice(NULL),
	m_pIndexBuffer(NULL),
	m_pVShader(NULL),
	m_pPShader(NULL),
#endif
#ifdef ENF_PS3
	m_pIndexBuffer(NULL),
	m_iCommitMask(0),
	m_iAlphaKillMask(0),
	m_bEnableAlphaTest(true),
	m_FreeTiles(0x7fff),
	m_iSamplerAddressMask(0),
	m_iSamplerFilterMask(0),
	m_iVSamplerAddressMask(0),
	m_iTextureMask(0),
	m_iVTextureMask(0),
	m_iPSConstantsB(0),
	m_pActiveBranch(NULL),
	m_iVSConstantsB(0),
	#ifdef ENF_SHADER_COMPILER
		m_cgcContext(NULL),
	#endif
	m_pShaderPatchBuffer(NULL),
#endif
#ifdef ENF_OGL
	m_pIndexBuffer(NULL),
	m_iVShader(0),
	m_iPShader(0),
	m_iVSConstantsB(0),
	m_iPSConstantsB(0),
	m_iCommitMask(0),
	m_bEnableAlphaTest(true),
	m_iPrevBaseVertexIndex(0xffffffff),
#endif
	m_bDisablePrimaryRT(false),
	m_iSkipPresent(1),
	m_iFrame(1),
	m_bATICrap(false),
	m_iGPUCount(1),
	m_ThreadLock(0),
	#ifdef ENF_XENON
		m_GPUVendor(GPU_XENON)
	#elif defined(ENF_PS3)
		m_GPUVendor(GPU_PS3)
	#else
		m_GPUVendor(GPU_UNKNOWN)
	#endif
{
	m_iStencilRef = 0;
	m_iDepthStencilState = -1;

#if defined(ENF_DX9) || defined(ENF_XENON)
	ZeroMem(&m_d3dCaps, sizeof(m_d3dCaps));
#endif
	ZeroMem(&m_Viewport, sizeof(m_Viewport));
#ifdef ENF_DX9
	ZeroMem(m_iRenderStateValue, sizeof(m_iRenderStateValue));
	ZeroMem(m_pTextures, sizeof(m_pTextures));
	ZeroMem(m_pVertexTextures, sizeof(m_pVertexTextures));
#endif

#if defined(ENF_OGL) || defined(ENF_PS3)
	ZeroMem(m_iRenderStateValue, sizeof(m_iRenderStateValue));
	ZeroMem(m_pTextures, sizeof(m_pTextures));
	ZeroMem(m_pVertexTextures, sizeof(m_pVertexTextures));
#endif

#if defined(ENF_OGL) 
	ZeroMem(m_pShadowVertexTextures, sizeof(m_pShadowVertexTextures));
	ZeroMem(m_pShadowTextures, sizeof(m_pShadowTextures));
#endif

	ZeroMem(m_iSamplerFilterStateValue, sizeof(m_iSamplerFilterStateValue));
	ZeroMem(m_iSamplerFilterStateValue2, sizeof(m_iSamplerFilterStateValue2));
	ZeroMem(m_iSamplerAddressStateValue, sizeof(m_iSamplerAddressStateValue));
	ZeroMem(m_iSamplerAddressStateValue2, sizeof(m_iSamplerAddressStateValue2));

	{
		SamplerAddressStateHandle handle;
		handle = CreateSamplerAddressState(SamplerAddressState(TADDRESS_CLAMP, TADDRESS_CLAMP, 0));
		enf_assert(handle == SAS_CLAMP_CLAMP);
		handle = CreateSamplerAddressState(SamplerAddressState(TADDRESS_CLAMP, TADDRESS_CLAMP, TADDRESS_CLAMP, 0));
		enf_assert(handle == SAS_CLAMP_CLAMP_CLAMP);
		handle = CreateSamplerAddressState(SamplerAddressState(TADDRESS_WRAP, TADDRESS_WRAP, 0));
		enf_assert(handle == SAS_WRAP_WRAP);
		handle = CreateSamplerAddressState(SamplerAddressState(TADDRESS_WRAP, TADDRESS_WRAP, TADDRESS_WRAP, 0));
		enf_assert(handle == SAS_WRAP_WRAP_WRAP);
		handle = CreateSamplerAddressState(SamplerAddressState(TADDRESS_BORDER, TADDRESS_BORDER, 0xffffffff));
		enf_assert(handle == SAS_BORDER_BORDER_ONE);
		handle = CreateSamplerAddressState(SamplerAddressState(TADDRESS_BORDER, TADDRESS_BORDER, 0));
		enf_assert(handle == SAS_BORDER_BORDER_ZERO);
		handle = CreateSamplerAddressState(SamplerAddressState(TADDRESS_WRAP, TADDRESS_WRAP, TADDRESS_CLAMP, 0));
		enf_assert(handle == SAS_WRAP_WRAP_CLAMP);
		
		#ifdef ENF_PS3
			handle = CreateSamplerAddressState(SamplerAddressState(TADDRESS_CLAMP_EGDE, TADDRESS_CLAMP_EGDE, 0));
			enf_assert (handle == SAS_CUBE_MAP);
		#endif
	}

	{
		DepthStencilStateHandle handle;
		handle = CreateDepthStencilState(DepthStencilStateDef(false, false, CMP_ALWAYS));
		enf_assert(handle == DSS_NONE);
		handle = CreateDepthStencilState(DepthStencilStateDef(false, true, CMP_ALWAYS));
		enf_assert(handle == DSS_ZWRITE);
		handle = CreateDepthStencilState(DepthStencilStateDef(true, false, CMP_LESSEQUAL));
		enf_assert(handle == DSS_ZENABLE_LE);
		handle = CreateDepthStencilState(DepthStencilStateDef(true, true, CMP_LESSEQUAL));
		enf_assert(handle == DSS_ZENABLE_LE_ZWRITE);
		
		handle = CreateDepthStencilState(DepthStencilStateDef(false, false, CMP_NEVER));
		enf_assert(handle == DSS_ZWRITE_SHADOWRECEIVESTENCIL);
		
		handle = CreateDepthStencilState(DepthStencilStateDef(true, false, CMP_LESSEQUAL, STENCILOP_KEEP, STENCILOP_KEEP, STENCILOP_KEEP, CMP_GREATER));
		enf_assert(handle == DSS_ZENABLE_LE_SHADOWRECEIVESTENCIL);
		handle = CreateDepthStencilState(DepthStencilStateDef(true, true, CMP_LESSEQUAL, STENCILOP_KEEP, STENCILOP_KEEP, STENCILOP_KEEP, CMP_GREATER));
		enf_assert(handle == DSS_ZENABLE_LE_ZWRITE_SHADOWRECEIVESTENCIL);
//		SET_RS(RS_STENCILREF, 1);

		handle = CreateDepthStencilState(DepthStencilStateDef(true, false, CMP_LESS,
				STENCILOP_KEEP, STENCILOP_DECR, STENCILOP_KEEP, CMP_ALWAYS,
				STENCILOP_KEEP, STENCILOP_INCR, STENCILOP_KEEP, CMP_ALWAYS));
		enf_assert(handle == DSS_ZENABLE_L_SHADOWCASTSTENCIL);
		handle = CreateDepthStencilState(DepthStencilStateDef(true, false, CMP_LESS));
		enf_assert(handle == DSS_ZENABLE_L);

		handle = CreateDepthStencilState(DepthStencilStateDef(true, false, CMP_LESSEQUAL, STENCILOP_KEEP, STENCILOP_INCRSAT, STENCILOP_INCRSAT, CMP_ALWAYS));
		enf_assert(handle == DSS_ZENABLE_LE_DEPTHCOMPLEX);
		handle = CreateDepthStencilState(DepthStencilStateDef(true, true, CMP_LESSEQUAL, STENCILOP_KEEP, STENCILOP_INCRSAT, STENCILOP_INCRSAT, CMP_ALWAYS));
		enf_assert(handle == DSS_ZENABLE_LE_ZWRITE_DEPTHCOMPLEX);

		handle = CreateDepthStencilState(DepthStencilStateDef(true, false, CMP_EQUAL));
		enf_assert(handle == DSS_ZENABLE_E);
		handle = CreateDepthStencilState(DepthStencilStateDef(true, true, CMP_LESS));
		enf_assert(handle == DSS_ZENABLE_L_ZWRITE);
	}
}

//-----------------------------------------------------------------------
void Renderer::InvalidateGPUCache(DeviceResource* res)
{
#ifdef ENF_XENON
	if(res->HasType(VertexBufferTypeID))
		m_pd3dDevice->InvalidateResourceGpuCache(&VertexBuffer::Cast(res)->m_VB, 0);
	else if(res->HasType(IndexBufferTypeID))
		m_pd3dDevice->InvalidateResourceGpuCache(&IndexBuffer::Cast(res)->m_IB, 0);
	else if(res->HasType(TextureTypeID))
		m_pd3dDevice->InvalidateResourceGpuCache(&Texture::Cast(res)->m_Texture, 0);
#elif defined(ENF_PS3)
	if(res->HasType(VertexBufferTypeID))
		GCM_CALL(cellGcmSetInvalidateVertexCache)(gCellGcmCurrentContext);
	else if(res->HasType(IndexBufferTypeID))
		GCM_CALL(cellGcmSetInvalidateVertexCache)(gCellGcmCurrentContext);
	else if(res->HasType(TextureTypeID))
		GCM_CALL(cellGcmSetInvalidateTextureCache)(gCellGcmCurrentContext, CELL_GCM_INVALIDATE_TEXTURE);
#endif
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC double ApplyImageCurve(float x, float gamma, float contrast)
{
/*
	works GOOD, enhances contrast using S-curve around center

	//apply contrast - from <0..1> to <-1, 1>
	double y  = (x - 0.5f)*2.0f;

	//s-curve around zero
	double c = enf_signfloat(y)*pow(abs(y), 1.0/contrast);

	//back to <0..1>
	c = c*0.5 + 0.5;

	//apply gamma
	return pow(c, (double) gamma);
*/
	
	//apply only gamma
	return pow((double) x, (double) gamma);
}


//-----------------------------------------------------------------------
void Renderer::SetBrightness(int gamma, int contrast)
{
	#if defined(ENF_DX9) || defined(ENF_XENON)
		//<0..50..100> -> <3..1..0.33>
		float g = enf_limit(gamma, 0, 100) - 50;
		float eGamma = gamma >= 50 ? (1.0f - g/75.0f) : (1.0f + abs(g)/25.0f);

		//<0..50..100> -> <0.5..1..2>
		float c = enf_limit(contrast, 0, 100) - 50;
		c = contrast >= 50 ? (1.0f + c/50.0f) : (1.0f - abs(c)/100.0f);

		#ifdef ENF_DX9
			if(m_pd3dDevice == NULL)
				return;

			D3DGAMMARAMP ramp;
			float step = 1 / 255.0f;

			for(int n = 0; n < 256; n++)
			{
				float x01 = n * step;
				double fx = ApplyImageCurve(x01, eGamma, c);

				ramp.red[n] = ramp.green[n] = ramp.blue[n] = enf_limit(enf_toint(fx * 65535.0), 0, 65535);
			}

			//this should work for 8bit XBOX360 RT too
			m_pd3dDevice->SetGammaRamp(0, (m_d3dCaps.Caps2 & D3DCAPS2_CANCALIBRATEGAMMA ) ? D3DSGR_CALIBRATE : D3DSGR_NO_CALIBRATION, &ramp);

		#elif defined(ENF_XENON)
			if(m_pd3dDevice == NULL)
				return;

			D3DPWLGAMMA ramp;

			float step = 1 / 128.0f;
			double next = ApplyImageCurve(0.0f, eGamma, c);
			int nextI = enf_limit(enf_toint(next * 65535.0), 0, 65535);

			for(int n = 0; n < 128; n++)
			{
				next = ApplyImageCurve(n*step, eGamma, c);

				int currI = nextI;
				nextI = enf_limit(enf_toint(next * 65535.0), 0, 65535);

				ramp.red[n].Base  = ramp.green[n].Base  = ramp.blue[n].Base  = currI;
				ramp.red[n].Delta = ramp.green[n].Delta = ramp.blue[n].Delta = nextI - currI;
			}

			//this is just for 10bit RT for XBOX360
			m_pd3dDevice->SetPWLGamma(0, &ramp);
		#endif
	#elif defined(ENF_PS3)
		//<0..50..100> -> <1.2..1.0..0.8>
		float newGamma = 1.2f - 0.4*(enf_limit(gamma, 0, 100)*0.01f);

		//<0.8..1.2>
		cellVideoOutSetGamma(CELL_VIDEO_OUT_PRIMARY, newGamma);
	#else 
		LogF(LL_ERROR, "Don't know hot to set gamma correction");			
	#endif
}

#ifdef ENF_PS3
	VertexShader* g_pScreenspace = NULL;
	PixelShader* g_pPColor = NULL;
	Vector4* g_pScreenRSX = NULL;
	IVertexDeclaration g_pScreenQuadVD = NULL;
#endif

//-----------------------------------------------------------------------
void Renderer::ClearRenderTarget(uint flags, const RRect* rect, uint color, float depth, uint stencil)
{
#if defined(ENF_DX9) || defined(ENF_XENON)
	D3DRECT r, *pr = NULL;
	uint nr = 0;

#ifdef ENF_XENON
	m_pd3dDevice->GpuDisown(D3DTAG_BLENDCONTROL0);
#endif

	if(rect)
	{
		r.x1 = rect->Left;
		r.y1 = rect->Top;
		r.x2 = rect->Right;
		r.y2 = rect->Bottom;
		pr = &r;
		nr = 1;
	}

	if(m_bFloatingPointDepthBuffer)
		depth = 1.0f - depth;

	if(flags)
	{
		uint d3df = 0;

		if(flags & RTCLEAR_ZBUFFER)
			d3df |= D3DCLEAR_ZBUFFER;

		if(flags & RTCLEAR_STENCIL)
			d3df |= D3DCLEAR_STENCIL;

		if(flags & RTCLEAR_TARGET)
			d3df |= D3DCLEAR_TARGET;

		m_pd3dDevice->Clear(nr, pr, d3df, color, depth, stencil);
	}
	else
	{
		//because of fast-clear feature is always better to clear zbuffer together with stencil
		m_pd3dDevice->Clear(nr, pr, D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL, color, depth, stencil);
	}
#ifdef ENF_XENON
	m_pd3dDevice->GpuOwn(D3DTAG_BLENDCONTROL0);
	for(uint n = 0; n < MAX_RENDER_TARGETS; n++)
		m_iBlendStates[n] = 0xffffffff;
#endif
#elif defined(ENF_PS3)
	uint clearFlag = 0;
	if(flags == 0 && m_pCurrentDepth)
	{
		flags = RTCLEAR_ZBUFFER | RTCLEAR_STENCIL;
	}

	//clearing affects pixels inside the scissor area, not viewport!
	uint rectX			= m_Viewport.X;
	uint rectY			= m_Viewport.Y;
	uint rectWidth		= m_Viewport.Width;
	uint rectHeight	= m_Viewport.Height;

	if (rect)
	{
		RectangleInt rect1(rectX, rectY, rectX + rectWidth, rectY + rectHeight);
		RectangleInt rect2(rect->Left, rect->Top, rect->Right, rect->Bottom);

		rect1.IntersectWith(rect2);

		rectX			= rect1.GetMinX();
		rectY			= rect1.GetMinY();
		rectWidth	= rect1.GetWidth() > 0 ? rect1.GetWidth() : 0;
		rectHeight	= rect1.GetHeight() > 0 ? rect1.GetHeight() : 0;
	}

	GCM_CALL(cellGcmSetScissor)(gCellGcmCurrentContext, rectX, rectY, rectWidth, rectHeight);

	if(m_pCurrentDepth)
	{
		if(m_CurrentFB.depthFormat == CELL_GCM_SURFACE_Z24S8)
		{
			uint depthStencil = 0;
		
			if(flags & RTCLEAR_ZBUFFER)
			{
				// clamp argument
				enf_assert(depth <= 1.0f && depth >= 0.0f);
				depthStencil = ((uint)( depth * 0xffffff )) << 8; 
				clearFlag |= CELL_GCM_CLEAR_Z;
			}

			if(flags & RTCLEAR_STENCIL)
			{
				depthStencil |= ((stencil*255u) & 0xff);
				clearFlag |= CELL_GCM_CLEAR_S;
			}

			GCM_CALL(cellGcmSetClearDepthStencil)(gCellGcmCurrentContext, depthStencil);
		}
		else if(flags & RTCLEAR_ZBUFFER)
		{
		  uint depthStencil = (uint)( depth * 0xffff ); 
		  clearFlag |= CELL_GCM_CLEAR_Z;

		  GCM_CALL(cellGcmSetClearDepthStencil)(gCellGcmCurrentContext, depthStencil);
		}
	}
	
	if(flags & RTCLEAR_TARGET)
	{
		if(m_CurrentFB.colorFormat < CELL_GCM_SURFACE_F_W16Z16Y16X16 || m_CurrentFB.colorFormat > CELL_GCM_SURFACE_F_X32)
		{
			GCM_CALL(cellGcmSetClearColor)(gCellGcmCurrentContext, color);
			clearFlag |= (CELL_GCM_CLEAR_R| CELL_GCM_CLEAR_G| CELL_GCM_CLEAR_B | CELL_GCM_CLEAR_A);
		}
		else
		{
			/////backup previous states
			ENF_BEGIN_EVENT(EC_BLUE, "Clear float target");
			if(g_pScreenspace == NULL)
			{
				g_pScreenspace = g_pRender->GetVShader("vs_screenspace", VSDEF(PRF_VS2a), &Define("OUT_TEX", 0), 1);
				g_pPColor = g_pRender->GetPShader("ps_color", PSDEF(PRF_PS2a), NULL, 0);

				g_pScreenRSX = (Vector4*) LOCAL_ALLOC(4*sizeof(Vector4), 16);
				g_pScreenRSX[0] = Vector4(-1.0f, 1.0f, 1.0f, 0.0f);
				g_pScreenRSX[1] = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				g_pScreenRSX[2] = Vector4(1.0f, -1.0f, 1.0f, 2.0f);
				g_pScreenRSX[3] = Vector4(-1.0f, -1.0f, 1.0f, 3.0f);
				
				VertexDeclaration decl;
				decl.Begin();
				decl.Append(0, VSDT_FLOAT3, VSDU_POSITION, 0);
				decl.Append(0, VSDT_FLOAT1, VSDU_INDICES, 0);
				decl.End();
				g_pScreenQuadVD = g_pRender->CreateVertexDeclaration(decl);
				
			}
		
			g_pRender->SetPixelShader(g_pPColor);
			g_pRender->SetVertexShader(g_pScreenspace);

			g_pRender->SetVertexDeclaration(g_pScreenQuadVD);
			SET_PSH_CONST(CP_MAT_DIFFUSE_COLOR, FColor(color));

			PushRenderState s1(RS_ALPHATESTENABLE, false);
			PushRenderState s2(RS_COLORWRITEENABLE, COLORWRITEENABLE_ALL);
			PushRenderState s3(RS_CULLMODE, CULL_NONE);
			PushRenderState s4(RS_FILLMODE, FILL_SOLID);
			PushDepthStencilState d1(DSS_NONE);
			PushBlendState b1(0, false);

			if(m_iCommitMask)
			{
				g_pRender->CommitStates(g_pScreenRSX, 0, sizeof(Vector4));
			}
			
			GCM_CALL(cellGcmSetDrawArrays)(gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_QUADS, 0, 4);

			//to disable the warning in GPAD,but is it really necessary? the manual says it' safer and faster to set te size to 0 when not using later
			GCM_CALL(cellGcmSetVertexDataArray)(gCellGcmCurrentContext, 0, 0, 0, 0, CELL_GCM_VERTEX_F, CELL_GCM_LOCATION_MAIN, 0);	//VSDU_POSITION
			GCM_CALL(cellGcmSetVertexDataArray)(gCellGcmCurrentContext, 7, 0, 0, 0, CELL_GCM_VERTEX_F, CELL_GCM_LOCATION_MAIN, 0);	//VSDU_INDICES

			ENF_END_EVENT();
		}
	}
	
	uint prevMask = m_iRenderStateValue[RS_COLORWRITEENABLE];

	if(clearFlag)
		GCM_CALL(cellGcmSetClearSurface)(gCellGcmCurrentContext, clearFlag);
		
	m_iRenderStateValue[RS_COLORWRITEENABLE] = 0xffffffff;
	SetRenderState(RS_COLORWRITEENABLE, prevMask);

	GCM_CALL(cellGcmSetScissor)(gCellGcmCurrentContext, 0, 0, 4095, 4095);

#elif defined(ENF_OGL)

	GLbitfield mask = 0;

	if(flags == 0)
		flags = RTCLEAR_ZBUFFER|RTCLEAR_STENCIL;

	if(flags & RTCLEAR_TARGET)
	{
		FColor c = color;
		GLCALL(glClearColor(c.r, c.g, c.b, c.a));
		mask |= GL_COLOR_BUFFER_BIT;
	}
	if(flags & RTCLEAR_STENCIL)
	{
		GLCALL(glClearStencil(stencil));
		mask |= GL_STENCIL_BUFFER_BIT;
	}
	if(flags & RTCLEAR_ZBUFFER)
	{
		GLCALL(glClearDepth(depth));
		mask |= GL_DEPTH_BUFFER_BIT;
	}

	GLCALL(glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE));
	uint prevMask = m_iRenderStateValue[RS_COLORWRITEENABLE];

	if(rect)
	{
		GLCALL(glScissor(rect->Left, rect->Top, rect->Right - rect->Left, rect->Bottom - rect->Top));
		GLCALL(glEnable(GL_SCISSOR_TEST));
	}
	else
	{
		GLCALL(glDisable(GL_SCISSOR_TEST));
	}

	GLCALL(glClear (mask));
	m_iRenderStateValue[RS_COLORWRITEENABLE] = 0xffffffff;
	SetRenderState(RS_COLORWRITEENABLE, prevMask);
#endif
}

//-----------------------------------------------------------------------------
void IRenderer::BeginUse()
{
	if(m_iSuspended > 0 && m_iUsed == 0)
	{
		AssertMainThread();
		//wait for suspended thread to respond
		//
#ifdef ENF_XENON
		//m_pd3dDevice->Resume();
#endif
	}

	m_iUsed++;
}

//-----------------------------------------------------------------------------
void IRenderer::EndUse()
{
	enf_assert(m_iUsed > 0);

	if(--m_iUsed == 0 && m_iSuspended > 0)
	{
		AssertMainThread();
#ifdef ENF_XENON
		//m_pd3dDevice->SetShaderGPRAllocation(0, 0, 0);
		//m_pd3dDevice->Suspend();
#endif
	}
}

//-----------------------------------------------------------------------------
void IRenderer::Suspend()
{
	enf_assert(m_bInScene == false);
	enf_assert(m_iUsed == 0);

	if(m_iSuspended == 0)
	{
#ifdef ENF_XENON
		//m_pd3dDevice->SetShaderGPRAllocation(0, 0, 0);
		//m_pd3dDevice->Suspend();
#endif
	}
	m_iSuspended++;
}

//-----------------------------------------------------------------------------
void IRenderer::Resume()
{
	enf_assert(m_iSuspended > 0);
	enf_assert(m_iUsed == 0);
	if(--m_iSuspended == 0)
	{
#ifdef ENF_XENON
		//m_pd3dDevice->Resume();
#endif
	}
}

//-----------------------------------------------------------------------------
ERESULT IRenderer::BeginScene()
{
	if(IsLocked())
		return EE_ERROR;

	BeginUse();

	FlushDelayedResources();

	if(m_bInScene)
		return EE_OK;

#ifdef ENF_DX9
	ERESULT hres = m_pd3dDevice->TestCooperativeLevel();

	if(hres == D3DERR_DEVICELOST || hres == D3DERR_DEVICENOTRESET)
	{
		Lock();
		hres = Reset(NULL);
		Unlock();
		if(hres != D3D_OK)
			return hres;
	}
#endif

	m_bInScene = true;
	ERESULT res = EE_OK;


	#ifdef ENF_STEREO_VISION
		FindStereoInfo();

		if (ENF_DIAG_VALUE(DM_REND_STEREO_INFO))
		{
			if (m_StereoInfo.Available) 
			{
				LogF(LL_NORMAL, "NVDIA Stereo driver found, options (stereo %s): separation %f, eye separation %f, convergence %f", m_StereoInfo.Enabled ? "on" : "off", m_StereoInfo.Separation, m_StereoInfo.EyeSeparation, m_StereoInfo.Convergence);
			}

			ENF_DIAG_SET(DM_REND_STEREO_INFO, 0);
		}
	#endif

#if defined(ENF_DX9) || defined(ENF_XENON)
	// Render the scene as normal
	res = m_pd3dDevice->BeginScene();

	D3DSURFACE_DESC desc;
	LPDIRECT3DSURFACE9 pBackBuffer;
	m_pd3dDevice->GetRenderTarget(0, &pBackBuffer );
	pBackBuffer->GetDesc( &desc );
	pBackBuffer->Release();

	m_iTargetWidth = desc.Width;
	m_iTargetHeight = desc.Height;

	for(uint n = 0; n < MAX_TEXTURES; n++)
	{
		m_iSamplerFilterStateValue[n] = 0xffffffff;
		m_iSamplerAddressStateValue[n] = 0xffffffff;
	}
	for(uint n = 0; n < MAX_VERTEX_TEXTURES; n++)
	{
		m_iSamplerFilterStateValue2[n] = 0xffffffff;
		m_iSamplerAddressStateValue2[n] = 0xffffffff;
	}

#ifndef ENF_XENON
	if(res == D3D_OK)
	{
#define GETRS(a) m_pd3dDevice->GetRenderState((D3DRENDERSTATETYPE)a, (DWORD*)&m_iRenderStateValue[(int)a]);
		GETRS(RS_FILLMODE);
		GETRS(RS_ALPHATESTENABLE);
		GETRS(RS_CULLMODE);
		GETRS(RS_ALPHAREF);
		GETRS(RS_ALPHAFUNC);
		GETRS(RS_POINTSIZE_MIN);
		GETRS(RS_POINTSPRITEENABLE);
		GETRS(RS_MULTISAMPLEANTIALIAS);
		GETRS(RS_MULTISAMPLEMASK);
		GETRS(RS_POINTSIZE_MAX);
		GETRS(RS_COLORWRITEENABLE);
		GETRS(RS_COLORWRITEENABLE1);
		GETRS(RS_COLORWRITEENABLE2);
		GETRS(RS_COLORWRITEENABLE3);
		GETRS(RS_BLENDFACTOR);
		GETRS(RS_DEPTHBIAS);
		GETRS(RS_SCISSORTESTENABLE);
		GETRS(RS_SLOPESCALEDEPTHBIAS);
		GETRS(RS_SRGBWRITEENABLE);
		GETRS(RS_CLIPPING);
	}
#endif

#ifdef ENF_XENON
	m_pd3dDevice->GpuOwn(D3DTAG_BLENDCONTROL0);
	for(uint n = 0; n < MAX_RENDER_TARGETS; n++)
		m_iBlendStates[n] = 0xffffffff;
#endif

	float val = 0.0f;
	#ifdef ENF_DEVELOPER
		uint bias = ((uint) ENF_DIAG_VALUE(DM_REND_MIPMAPBIAS));
		val = bias < 13 ? bias : bias < 14 ? -1.0f: bias < 15 ? -0.75 : bias < 16 ? -0.5 : -0.25;	//0..12, -1, -0.75, -0.5, -0.25
	#endif
		
	DWORD mipMapBias = *(DWORD*)&val;
	DWORD maxMipLevel = ENF_DIAG_VALUE(DM_REND_MAXMIPMAP);

	for(uint n = 0; n < MAX_TEXTURES; n++)
	{
		m_pd3dDevice->SetSamplerState(n, D3DSAMP_MIPMAPLODBIAS, mipMapBias);
		m_pd3dDevice->SetSamplerState(n, D3DSAMP_MAXMIPLEVEL , maxMipLevel);
	}
	m_pVShader = NULL;
	m_pPShader = NULL;

#elif defined(ENF_PS3)
	//it cannot be created in constructor because when constructor is called,
	//the engine still doesn't have local memory allocator
	if (!m_pShaderPatchBuffer)
		m_pShaderPatchBuffer = ENF_NEW ShaderPatchBuffer;

	//cannot use cellGcmGetDisplayInfo() because it returns native resolution of display
	//const CellGcmDisplayInfo *pInfo = cellGcmGetDisplayInfo();
	//m_iTargetWidth = pInfo->width;
	//m_iTargetHeight = pInfo->height;

	m_iTargetWidth = m_Setup.m_iBackBufferWidth;
	m_iTargetHeight = m_Setup.m_iBackBufferHeight;

	//states must be set in this way -> after a flip is performed by a RSX, 
	//the state are set to it's default values, as written in PS3 manual,
	//5. 3 Resetting the RSX Context -> the context is automatically reset!
	//so do not need to call these functions one by one
	
	m_iOldFrequencyDivider = 0xFFFFFFFF;
	m_iSamplerAddressMask = 0;
	m_iSamplerFilterMask = 0;
	m_iTextureMask = 0;

	for(uint n = 0; n < MAX_TEXTURES; n++)
	{
		m_iSamplerFilterStateValue[n]  = SFS_POINT_LINEAR_LINEAR;
		m_iSamplerAddressStateValue[n] = SAS_WRAP_WRAP_CLAMP;
		m_pTextures[n] = NULL;

		//LOD calculation for 2 axes is a mode which exists for the purpose of maintaining compatibility with OpenGL. Normally, CELL_GCM_TEXTURE_ISO_LOW does not need to be specified
		GCM_CALL(cellGcmSetTextureOptimization)(gCellGcmCurrentContext, n, 0x8, CELL_GCM_TEXTURE_ISO_HIGH, CELL_GCM_TEXTURE_ANISO_LOW);

		GCMSetNullTexture(n);
	}

	m_iVSamplerAddressMask = 0;
	m_iVSamplerFilterMask = 0;
	m_iVTextureMask = 0;
	for(uint n = 0; n < MAX_VERTEX_TEXTURES; n++)
	{
		m_iSamplerFilterStateValue2[n]  = SFS_POINT_POINT_NONE;
		m_iSamplerAddressStateValue2[n] = SAS_WRAP_WRAP;
		m_pVertexTextures[n] = NULL;
		
		GCMSetNullVertexTexture(n);
	}

	//this hangs RSX ;-)
	/*
	for (uint i = 0; i < MAX_RENDER_TARGETS; i++)
	{
		m_iBlendStates[i] = 0xFFFFFFFF;
	}
	*/

	m_iVSConstantsB = 0;
	m_iPSConstantsB = 0;
	m_pActiveBranch = NULL;
	m_iAlphaKillMask = 0;

	float pointSize = 1.0f;

	m_iRenderStateValue[RS_FILLMODE]					= FILL_SOLID;
	m_iRenderStateValue[RS_ALPHATESTENABLE]		= false;
	m_iRenderStateValue[RS_ALPHAREF]					= 0;
	m_iRenderStateValue[RS_ALPHAFUNC]				= CMP_ALWAYS;
	m_iRenderStateValue[RS_CULLMODE]					= CULL_NONE;
	m_iRenderStateValue[RS_POINTSIZE]				= *((uint*)&pointSize);
	m_iRenderStateValue[RS_POINTSIZE_MIN]			= 0;				//undef on GCM
	m_iRenderStateValue[RS_POINTSPRITEENABLE]		= false;
	m_iRenderStateValue[RS_MULTISAMPLEANTIALIAS]	= 0;
	m_iRenderStateValue[RS_MULTISAMPLEMASK]		= 0;
	m_iRenderStateValue[RS_POINTSIZE_MAX]			= 0;				//undef on GCM
	m_iRenderStateValue[RS_COLORWRITEENABLE]		= COLORWRITEENABLE_ALL;
	m_iRenderStateValue[RS_SCISSORTESTENABLE]		= false;
	m_iRenderStateValue[RS_SLOPESCALEDEPTHBIAS]	= 0;
	m_iRenderStateValue[RS_COLORWRITEENABLE1]		= 0;
	m_iRenderStateValue[RS_COLORWRITEENABLE2]		= 0;
	m_iRenderStateValue[RS_COLORWRITEENABLE3]		= 0;
	m_iRenderStateValue[RS_BLENDFACTOR]				= 0;
	m_iRenderStateValue[RS_DEPTHBIAS]				= 0;

	m_iCommitMask = 0xffffffff;
	m_iOldAttribMask = 0xFFFF;

	m_PSConstChanged = 0;
	m_VSConstChanged = 0;
	m_iVertexDeclaration = 0xFFFFFFFF;
	m_pIndexBuffer = NULL;
	m_pFShader = NULL;
	m_pVShader = NULL;
	m_iDepthStencilState = DSS_ZENABLE_L_ZWRITE;

	GCM_CALL(cellGcmSetFrontFace)(gCellGcmCurrentContext, CELL_GCM_CW);
	GCM_CALL(cellGcmSetZcullEnable)(gCellGcmCurrentContext, CELL_GCM_TRUE, CELL_GCM_TRUE);
	GCM_CALL(cellGcmSetCullFaceEnable)(gCellGcmCurrentContext, CELL_GCM_FALSE);
	GCM_CALL(cellGcmSetScissor)(gCellGcmCurrentContext, 0, 0, 4095,4095);
	GCM_CALL(cellGcmSetStencilMask)(gCellGcmCurrentContext, 0xff);
	GCM_CALL(cellGcmSetStencilFunc)(gCellGcmCurrentContext, CELL_GCM_ALWAYS, 0, 0xff);
	GCM_CALL(cellGcmSetDitherEnable)(gCellGcmCurrentContext, CELL_GCM_FALSE);

	SET_RS(RS_DEPTHBIAS, F2DW(0));
	SET_RS(RS_SLOPESCALEDEPTHBIAS, F2DW(0));

	m_SelectedViewPort.Init();

	ClearStreamSource(0);
	ClearStreamSource(1);

#elif defined(ENF_OGL)
#ifdef __APPLE__
	//if(!glMakeCurrent())
		//return false;
#else
	if(!wglMakeCurrent(m_HDC, m_glContext))
		return false;
#endif
	m_iTargetWidth = m_Setup.m_iBackBufferWidth;
	m_iTargetHeight = m_Setup.m_iBackBufferHeight;

	for(uint n = 0; n < MAX_TEXTURES; n++)
	{
		m_iSamplerFilterStateValue[n] = 0xffffffff;
		m_iSamplerAddressStateValue[n] = 0xffffffff;
	}
	for(uint n = 0; n < MAX_VERTEX_TEXTURES; n++)
	{
		m_iSamplerFilterStateValue2[n] = 0xffffffff;
		m_iSamplerAddressStateValue2[n] = 0xffffffff;
	}

	m_iRenderStateValue[RS_FILLMODE] = FILL_SOLID;
	m_iRenderStateValue[RS_ALPHATESTENABLE] = false;
	m_iRenderStateValue[RS_CULLMODE] = CULL_NONE;
	GLCALL(glDisable(GL_CULL_FACE));
	m_iRenderStateValue[RS_ALPHAREF] = 0;
	m_iRenderStateValue[RS_ALPHAFUNC] = CMP_ALWAYS;
	m_iCommitMask = 0xffffffff;

	/*m_iRenderStateValue[RS_POINTSIZE_MIN] = ;
	m_iRenderStateValue[RS_POINTSPRITEENABLE] = ;
	m_iRenderStateValue[RS_MULTISAMPLEANTIALIAS] = ;
	m_iRenderStateValue[RS_MULTISAMPLEMASK] = ;
	m_iRenderStateValue[RS_POINTSIZE_MAX] = ;
	m_iRenderStateValue[RS_COLORWRITEENABLE] = ;
	m_iRenderStateValue[RS_COLORWRITEENABLE1] = ;
	m_iRenderStateValue[RS_COLORWRITEENABLE2] = ;
	m_iRenderStateValue[RS_COLORWRITEENABLE3] = ;
	m_iRenderStateValue[RS_BLENDFACTOR] = 0;
	m_iRenderStateValue[RS_DEPTHBIAS] = ;
	m_iRenderStateValue[RS_SCISSORTESTENABLE] = ;
	m_iRenderStateValue[RS_SLOPESCALEDEPTHBIAS] = ;
	m_iRenderStateValue[RS_SRGBWRITEENABLE] = ;
	m_iRenderStateValue[RS_CLIPPING] = ;*/

	GLCALL(glScissor(m_Viewport.X, m_Viewport.Y, m_Viewport.Width, m_Viewport.Height));
	GLCALL(glStencilMask(0xff));
	GLCALL(glStencilFunc(GL_ALWAYS, 0, 0xff));

#endif
	return res;
}

//-----------------------------------------------------------------------
void IRenderer::EndScene()
{
	if(m_bInScene)
	{
#if defined(ENF_DX9) || defined(ENF_XENON)
		m_pd3dDevice->EndScene();
#endif
		m_bInScene = false;
		m_iFrame++;
	}
	
#ifdef ENF_XENON
	m_pd3dDevice->GpuDisown(D3DTAG_BLENDCONTROL0);
#endif

	ReleaseReferences();

	EndUse();
}

//-----------------------------------------------------------------------
void IRenderer::Present()
{
	if(IsLocked() || !m_bReady)
		return;

	if(m_bDisablePrimaryRT)
		return;

	if(m_iSkipPresent > 0)
	{
		m_iSkipPresent--;
		return;
	}

	ENF_BEGIN_PROFILE(PROFILE_PRESENT);

#if defined(ENF_DX9) || defined(ENF_XENON)
	if(m_pd3dDevice)
	{

#ifdef ENF_XENON
		m_pd3dDevice->SetRenderState(D3DRS_HIGHPRECISIONBLENDENABLE, FALSE);
		m_pd3dDevice->SetShaderGPRAllocation(0, 0, 0);
#else
		uint issued;
		if(m_iLimiterTop < m_iLimiterBottom)
			issued = m_iLimiterTop + ((MAX_LIMIT_FRAMES + 1) - m_iLimiterBottom);
		else
			issued = m_iLimiterTop - m_iLimiterBottom;

		if(issued == MAX_LIMIT_FRAMES)
		{
			BOOL data;
			m_pLimiterQueries[m_iLimiterBottom]->GetData(&data, sizeof(data), D3DGETDATA_FLUSH);
			m_iLimiterBottom = (m_iLimiterBottom + 1) % (MAX_LIMIT_FRAMES + 1);
		}
		m_pLimiterQueries[m_iLimiterTop]->Issue(QC_ISSUE_END);
		m_iLimiterTop = (m_iLimiterTop + 1) % (MAX_LIMIT_FRAMES + 1);
#endif

		// Show the frame on the primary surface.
		m_pd3dDevice->Present( NULL, NULL, NULL, NULL );

	}

#elif defined(ENF_PS3)
	m_pVShader = NULL;
	m_pFShader = NULL;

	Flip();

#elif defined(ENF_OGL)
	if(g_pScreenspace == NULL)
	{
		Define outTexDef("OUT_TEX", 1);
		Define passThruDef("PASSTHRU");
		g_pScreenspace = g_pRender->GetVShader("vs_screenspace", VSDEF(PRF_VS2a), &outTexDef, 1);
		g_pPColor = g_pRender->GetPShader("ps_copycolor", PSDEF(PRF_PS2a), &passThruDef, 1);
		
		VertexDeclaration decl;
		
		decl.Begin();
		decl.Append(0, VSDT_FLOAT3, VSDU_POSITION, 0);
		decl.Append(0, VSDT_FLOAT1, VSDU_INDICES, 0);
		decl.End();
		g_pScreenQuadVD = g_pRender->CreateVertexDeclaration(decl);
	}

	GLCALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));
	ReportGLFBOStatus(GL_FRAMEBUFFER, __FILE__, __LINE__);
	SetViewport(RenderViewport(0, 0, m_Setup.m_iBackBufferWidth, m_Setup.m_iBackBufferHeight, 0, 1));
	//ClearRenderTarget(RTCLEAR_TARGET, NULL, 0xff00ffff, 0, 0);

	
	//copy rendertarget texture to framebuffer
	Texture tex(this);
	tex.m_iTexture = m_PrimaryRenderBuffer;

	SET_RS(RS_ALPHATESTENABLE, false);
	SET_RS(RS_CULLMODE, CULL_NONE);
	SET_RS(RS_COLORWRITEENABLE, COLORWRITEENABLE_ALL);
	SET_RS(RS_FILLMODE, FILL_SOLID);
	SET_BLEND(0, false);
	SET_DSS(DSS_NONE, 0);

	SET_TEX(0, &tex);

	// Set render states for drawing a rectangle that covers the viewport.
	// The color of the rectangle will be passed in PS constant
	SetVertexShader(g_pScreenspace);
	SetPixelShader(g_pPColor);
	SetVertexDeclaration( g_pScreenQuadVD );

	Vector4 m_pBigSquareVB[4] =
	{
		Vector4(-1.0f, 1.0f, 1.0f, 0.0f ),
		Vector4(1.0f, 1.0f, 1.0f, 1.0f ),
		Vector4( 1.0f, -1.0f, 1.0f, 2.0f ),
		Vector4( -1.0f, -1.0f, 1.0f, 3.0f )
	};

	float ucoords[4][4] = {
		0,0,0,0,
		1,0,0,0,
		1,0,0,0,
		0,0,0,0
	};
	float vcoords[4][4] = {
		1,0,0,0,
		1,0,0,0,
		0,0,0,0,
		0,0,0,0
	};

	GLCALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

	SET_VSH_CONST(0, ucoords[0], 4);
	SET_VSH_CONST(4, vcoords[0], 4);
	DrawPrimitiveUP( PT_QUADLIST, 1, m_pBigSquareVB, sizeof(Vector4));
	DirtyMask(~0);
	
#ifdef WIN32
//	wglSwapIntervalEXT(m_Setup.m_PresentationInterval == 1);
	BOOL b = ::SwapBuffers(m_HDC);
	b = b;
#elif defined(__MACOSX__)
	glfwSwapBuffers();
	GLCALL(glBindFramebuffer(GL_FRAMEBUFFER, m_iPrimaryFrameBuffer));
	ClearRenderTarget(RTCLEAR_TARGET|RTCLEAR_ZBUFFER|RTCLEAR_STENCIL, NULL, 0x00000000, 0, 0);
#endif

#endif
	ENF_END_PROFILE();
}

//-----------------------------------------------------------------------
void Renderer::ClearScreen()
{
	if(m_bDisablePrimaryRT)
		return;

#if defined(ENF_DX9) || defined(ENF_XENON)
	D3DSURFACE_DESC desc;
	LPDIRECT3DSURFACE9 pBackBuffer;
	m_pd3dDevice->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &pBackBuffer );
	pBackBuffer->GetDesc( &desc );
	pBackBuffer->Release();

	SetViewport(RenderViewport(0, 0, desc.Width, desc.Height, 0.0f, 1.0f));

	if(m_bInScene)
		m_pd3dDevice->EndScene();

	for(uint n = 0; n < m_Setup.m_iBackBufferCount + 1; n++)
	{
		if(m_pd3dDevice->BeginScene() == D3D_OK)
		{
			m_pd3dDevice->Clear(0, NULL, D3DCLEAR_STENCIL | D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
			m_pd3dDevice->EndScene();
		}
		m_pd3dDevice->Present(NULL, NULL, NULL, NULL);
	}

	if(m_bInScene)
		m_pd3dDevice->BeginScene();
#endif
}


//-----------------------------------------------------------------------
ERESULT IRenderer::RemoveResource(DeviceResource *res)
{
#if defined(ENF_DEVELOPER) || defined(ENF_DX9)
	if(res)
	{
		CMutexLock lock(m_Mutex);
		for(DeviceResource **prev = &m_pResources; *prev; prev = &(*prev)->m_pNext)
		{
			if(*prev == res)
			{
				*prev = res->m_pNext;
				return EE_OK;
			}
		}			
	}

	enf_assert(!"RemoveResource failed!");
	return EE_ERROR;
#else
	return EE_OK;
#endif
}
//-----------------------------------------------------------------------
ERESULT IRenderer::AddResource(DeviceResource *res)
{
#if defined(ENF_DEVELOPER) || defined(ENF_DX9)
	if(res)
	{
		CMutexLock lock(m_Mutex);
		res->m_pNext = m_pResources;
		m_pResources = res;
		return EE_OK;
	}
	return EE_ERROR;
#else
	return EE_OK;
#endif
}

//-----------------------------------------------------------------------
bool IRenderer::ExistsResource(const DeviceResource* checkRes) const
{
#if defined(ENF_DEVELOPER) || defined(ENF_DX9)
	CMutexLock lock(m_Mutex);
	for(const DeviceResource* res = m_pResources; res; res = res->m_pNext)
	{
		if(res == checkRes)
			return true;
	}

	return false;
#else
	return true;
#endif
}

//-----------------------------------------------------------------------
void IRenderer::DelayRelease(DeviceResource* res)
{
	CMutexLock lock(m_DelayReleaseMutex);
	m_DelayReleaseResources.Insert(res);
}

//-----------------------------------------------------------------------
void IRenderer::FlushDelayedResources()
{
	if(!CheckThread())
		return;

	CMutexLock lock(m_DelayReleaseMutex);
	
	for(uint n = 0; n < m_DelayReleaseResources.GetCardinality(); n++)
	{
		m_DelayReleaseResources[n]->Free();
		RemoveResource(m_DelayReleaseResources[n]);
		delete m_DelayReleaseResources[n];
	}
}

//-----------------------------------------------------------------------
void IRenderer::BeforeReset()
{
	CMutexLock lock(m_Mutex);
	GetEngine()->BeforeReset();
	FlushDelayedResources();

#ifdef ENF_DX9
	for(DeviceResource *res = m_pResources; res != NULL; res = res->m_pNext)
		res->BeforeReset();
#endif
}

//-----------------------------------------------------------------------
void IRenderer::AfterReset()
{
	CMutexLock lock(m_Mutex);
#ifdef ENF_DX9
	for(DeviceResource *res = m_pResources; res != NULL; res = res->m_pNext)
	{
		if(res->AfterReset() != EE_OK)
		{
			res->AfterReset();
			InternalError("Can't restore resource %s", res->GetTypeName());
		}
	}
#endif
#ifdef ENF_HWCURSOR
	if(m_iCurrentCursor != 0)
	{
		//refresh cursor
		uint curr = m_iCurrentCursor;
		m_iCurrentCursor = -1;
		SetCursor(curr);
	}
#endif
	GetEngine()->AfterReset();
}

//-----------------------------------------------------------------------
void Renderer::SetStreamSourceFreqOp(uint stream, StreamFreqOp op, uint freq)
{
#ifdef ENF_DX9
	m_pd3dDevice->SetStreamSourceFreq(stream, freq | (uint)op);
#elif defined(ENF_PS3) && defined (ENF_HWINSTANCING)
	if(m_Streams[stream].Freq != freq ||
		m_Streams[stream].Op != op)
	{
		m_Streams[stream].Freq = freq;
		m_Streams[stream].Op = op;
		DirtyMask(Renderer::GLCM_STREAMS);
	}
	
#endif
}

//-----------------------------------------------------------------------
void Renderer::SetTexture(uint index, const Texture *tex)
{
	enf_assert(index < MAX_TEXTURES);

#if defined(ENF_DX9) || defined(ENF_XENON)
	LPDIRECT3DBASETEXTURE9 pTex = NULL;
	if(tex)
	{
		tex->Touch(0);
		pTex = tex->GetD3DTexture();
	}

	#ifndef NO_REDUNDANT_RS_CHECK
		if(m_pTextures[index] == pTex)
			return;
	#endif

	m_pTextures[index] = pTex;

	m_pd3dDevice->SetTexture(index, pTex);
#elif defined(ENF_PS3)

	#ifndef NO_REDUNDANT_RS_CHECK
		if(m_pTextures[index] == tex)
			return;
	#endif

	m_pTextures[index] = (Texture*)tex;

	m_iCommitMask |= GCMCM_TEXTURES;
	m_iTextureMask |= (1 << index);

#elif defined(ENF_OGL)
	if(tex != m_pShadowTextures[index])
	{
		m_iCommitMask |= GLCM_TEXTURES;
		m_pTextures[index] = (Texture*)tex;
	}
#endif

#ifdef ENF_DEVELOPER
	m_Stats.TexChanges++;
#endif
}

//-----------------------------------------------------------------------
void Renderer::SetVertexTexture(uint index, const Texture *tex)
{
/*	if(ENF_DIAG_VALUE (DM_REND_SHOWTERRSHADOWMAP) == 5)
	{
		return;
	}
*/
	enf_assert(index < MAX_VERTEX_TEXTURES);

#if defined(ENF_DX9) || defined(ENF_XENON)
	LPDIRECT3DBASETEXTURE9 pTex = NULL;
	if(tex)
	{
		tex->Touch(0);
		pTex = tex->GetD3DTexture();
	}

	#ifndef NO_REDUNDANT_RS_CHECK
		if(m_pVertexTextures[index] == pTex)
			return;
	#endif

	m_pVertexTextures[index] = pTex;

	m_pd3dDevice->SetTexture(index + D3DVERTEXTEXTURESAMPLER0, pTex);
#elif defined(ENF_PS3)

	#ifndef NO_REDUNDANT_RS_CHECK
		if(m_pVertexTextures[index] == tex)
			return;
	#endif

	m_pVertexTextures[index] = (Texture*)tex;

	m_iVTextureMask |= (1 << index);
	m_iCommitMask |= GCMCM_TEXTURESV;

#elif defined(ENF_OGL)
	if(tex != m_pShadowVertexTextures[index])
	{
		m_iCommitMask |= GLCM_TEXTURESV;
		m_pVertexTextures[index] = (Texture*)tex;
	}
#endif

#ifdef ENF_DEVELOPER
	m_Stats.TexChanges++;
#endif
}

//-----------------------------------------------------------------------
void IRenderer::OnEvent(const Event& event)
{
	Module::OnEvent(event);

	if(event.HasType(ReloadShadersEventTypeID))
	{
		ReloadShaders(NULL);
	}
	else if(event.HasType(TerminationEventTypeID))
	{
#ifdef ENF_PS3
		StopRSX(gCellGcmCurrentContext);
#endif
	}
}

//-----------------------------------------------------------------------
Renderer::~Renderer()
{
	#ifdef ENF_PS3
		SAFE_DELETE(m_pShaderPatchBuffer);
	#endif
}

//-----------------------------------------------------------------------
void Renderer::ReleaseReferences()
{
#if defined(ENF_DX9) || defined(ENF_XENON)
	if(m_pd3dDevice == NULL)
		return;
#elif defined(ENF_OGL)
	if(m_glContext == NULL)
		return;
#endif

	SetIndices(NULL);
	SetPixelShader(NULL);
	SetVertexShader(NULL);
	for(uint n = 0; n < MAX_STREAMS; n++)
		ClearStreamSource (n);

	for(uint n = 0; n < MAX_TEXTURES; n++)
		SetTexture(n, NULL);

	if(ENF_DIAG_VALUE (DM_REND_SHOWTERRSHADOWMAP) != 5)
		for(uint n = 0; n < MAX_VERTEX_TEXTURES; n++)
			SetVertexTexture(n, NULL);
}

//-----------------------------------------------------------------------
void Renderer::ReleaseReferences(DeviceResource* res)
{
	enf_assert(res != NULL);

#ifdef ENF_XENON
	if(res->HasType(IndexBufferTypeID))
	{
		if(m_pIndexBuffer == &IndexBuffer::Cast(res)->m_IB)
			SetIndices(NULL);
	}
	else if(res->HasType(VertexBufferTypeID))
	{
		D3DVertexBuffer* vb = &VertexBuffer::Cast(res)->m_VB;
		for(uint n = 0; n < MAX_STREAMS; n++)
		{
			if(m_Streams[n].VB == vb)
				ClearStreamSource (n);
		}
	}
	else if(res->HasType(TextureTypeID))
	{
		LPDIRECT3DBASETEXTURE9 tex = &Texture::Cast(res)->m_Texture;
		for(uint n = 0; n < MAX_TEXTURES; n++)
		{
			if(m_pTextures[n] == tex)
				SetTexture(n, NULL);
		}
		for(uint n = 0; n < MAX_VERTEX_TEXTURES; n++)
		{
			if(m_pVertexTextures[n] == tex)
				SetVertexTexture(n, NULL);
		}
	}
	else if(res->HasType(VertexShaderTypeID))
	{
		if(m_pVShader == VertexShader::Cast(res)->m_pShader)
			SetVertexShader(NULL);
	}
	else if(res->HasType(PixelShaderTypeID))
	{
		if(m_pPShader == PixelShader::Cast(res)->m_pShader)
			SetPixelShader(NULL);
	}
#endif
}

//-----------------------------------------------------------------------
// Renderer
//-----------------------------------------------------------------------
IRenderer::~IRenderer()
{
	enf_rlsassert(CheckThread());

	#ifdef ENF_PS3
		if (UsingSWRescaler())
		{
			m_bRescaler = false;
			cellRescExit();
		}
	#endif

	End();

	#ifdef ENF_STEREO_VISION
		DisableStereo();
	#endif

	FlushDelayedResources();
	for(uint n = 0; n <= MAX_LIMIT_FRAMES; n++)
	{
		SAFE_RELEASE(m_pLimiterQueries[n]);
	}

	for(uint d = 0; d < m_pVertexDeclarations.GetCardinality(); d++)
	{
		delete m_pVertexDeclarations[d];
	}

	ForEach<ReleaseItem>(m_Shaders);
	m_Shaders.Clear();
	m_ShadersHash.Clear();

	ReleaseReferences();
	//set back
	SetBrightness(50);
	
	for(DeviceResource *res = m_pResources; res != NULL; res = m_pResources)
	{
		res->Release();
	}

#if defined(ENF_HWCURSOR)
	for(uint n = 0; n < m_Cursors.GetCardinality(); n++)
	{
		m_Cursors[n].surface->Release();
	}
	m_Cursors.Clear();

	if( m_pd3dDevice )
	{
		int c = m_pd3dDevice->Release();

		while(c != 0)
			c = m_pd3dDevice->Release();

		m_pd3dDevice = NULL;
	}

	if(m_pD3D)
	{
		int c = m_pD3D->Release();
		while(c != 0)
			c = m_pD3D->Release();
		m_pD3D = NULL;
	}
#endif
	for(uint a = 0; a < m_Adapters.GetCardinality(); a++)
	{		
		delete m_Adapters[a];
	}

	if(g_pRender == this)
		g_pRender = NULL;
}

//-----------------------------------------------------------------------
void IRenderer::PopRenderTarget(uint rtindex)
{
	enf_assert(rtindex < MAX_RENDER_TARGETS);
	enf_assert(m_iRTStackDepth[rtindex] > 0);
	if(m_iRTStackDepth[rtindex] == 0)
		return;

	RTStack* rts = &m_RTStack[rtindex][--m_iRTStackDepth[rtindex]];

#if defined(ENF_DX9) || defined(ENF_XENON)
	m_pd3dDevice->SetRenderTarget(rtindex, rts->m_pRender);

	if(rtindex == 0)
	{
		m_pd3dDevice->SetDepthStencilSurface(rts->m_pDepth);
		SetViewport(rts->m_Viewport);

		m_iTargetWidth = rts->m_iRTWidth;
		m_iTargetHeight = rts->m_iRTHeight;
	}

	SAFE_RELEASE(rts->m_pRender);
	SAFE_RELEASE(rts->m_pDepth);
#elif defined(ENF_PS3)
	m_CurrentFB = rts->m_FB;

	GCM_CALL(cellGcmSetSurfaceWindow)(gCellGcmCurrentContext, &m_CurrentFB, CELL_GCM_WINDOW_ORIGIN_TOP, PS3_DIRECTX_COMPATIBLE_PIXEL_CENTER);
	
	BlendState blend = rts->m_BlendState;
	if(blend.Value != 0xffffffff)
		SetBlendState(rtindex, blend);
	else
		SetBlendState(rtindex, false);
	
	EnableAlphaTest(rts->m_bEnableAlphaTest);
	
	if(rtindex == 0)
	{
		SetViewport(rts->m_Viewport);
		m_iTargetWidth = rts->m_iRTWidth;
		m_iTargetHeight = rts->m_iRTHeight;
	}

	m_pCurrentDepth = rts->m_pDepth;
	m_pCurrentRender = rts->m_pRender;

#elif defined(ENF_OGL)
	return;
	if(m_iCurrentFB != m_iPrimaryFrameBuffer)
		GLCALL(glDeleteFramebuffers(1, &m_iCurrentFB));

	m_iCurrentFB = rts->m_iFB;
	if(rts->m_iRB != 0xffffffff)
		m_iCurrentRB = rts->m_iRB;
	if(rts->m_iDB != 0xffffffff)
		m_iCurrentDB = rts->m_iDB;

	GLCALL(glBindFramebuffer(GL_FRAMEBUFFER, rts->m_iFB));

	if(rtindex == 0)
	{
		SetViewport(rts->m_Viewport);
		m_iTargetWidth = rts->m_iRTWidth;
		m_iTargetHeight = rts->m_iRTHeight;
	}
#endif
}

//-----------------------------------------------------------------------
void IRenderer::SetRenderTarget(uint rtindex, RenderTarget *rt, uint face, uint flags)
{
	enf_assert(m_iRTStackDepth[rtindex] > 0);
#if defined(ENF_DX9) || defined(ENF_XENON)
	if(!(flags & RTF_KEEP_RENDERTARGET))
	{
		if(rt->IsCube())
		{
			IDirect3DCubeTexture9* ctex = (IDirect3DCubeTexture9*)rt->GetD3DTexture();
			if(ctex)
			{
				IDirect3DSurface9* surf;
				ctex->GetCubeMapSurface((D3DCUBEMAP_FACES)face, 0, &surf);
				m_pd3dDevice->SetRenderTarget(rtindex, surf);
				surf->Release();
			}
		}
		else
		{
			m_pd3dDevice->SetRenderTarget(rtindex, rt->GetSurface());
		}
	}

	if(rtindex == 0)
	{
		if(!(flags & RTF_KEEP_DEPTHSTENCILTARGET))
			m_pd3dDevice->SetDepthStencilSurface(rt->GetDepthSurface());

		if(!(flags & RTF_KEEP_VIEWPORT))
		{
			SetViewport(RenderViewport(0, 0, rt->GetWidth(), rt->GetHeight(), 0, 1));
		}
		else
		{
			SetViewport(GetViewport());
		}
		m_iTargetWidth = rt->GetWidth();
		m_iTargetHeight = rt->GetHeight();
	}
#elif defined(ENF_PS3)
	g_pRender->CheckCommandBuffer(512);

	CellGcmSurface surf = rt->GetSurface();
	
	if(!(flags & RTF_KEEP_RENDERTARGET)  || rtindex > 0)
	{
		if(rt->GetRenderBuffer())
		{
			m_pCurrentRender = rt->GetRenderBuffer();
		
			m_CurrentFB.colorOffset[rtindex] = surf.colorOffset[0];
			if (rt->IsCube())
			{
				m_CurrentFB.colorOffset[rtindex] += rt->GetGCMFaceOffset(face);
			}

			m_CurrentFB.colorLocation[rtindex] = surf.colorLocation[0];
			m_CurrentFB.colorPitch[rtindex] = surf.colorPitch[0];

			m_CurrentFB.colorTarget = AVAILABLE_MRT_RT[rtindex];

			if (rtindex == 0)
			{
				m_CurrentFB.colorFormat = surf.colorFormat;
				m_CurrentFB.width = surf.width;
				m_CurrentFB.height = surf.height;
			}	
		}
		EnableAlphaTest(!IsFloat32Format(rt->GetFormat()));
		if(IsFloat32Format(rt->GetFormat()))
			SetBlendState(rtindex, false);	
	}
	
	if(rtindex == 0)
	{
		
		if(!(flags & RTF_KEEP_DEPTHSTENCILTARGET))
		{
			if(rt->GetDepthBuffer())
			{
				m_pCurrentDepth = rt->GetDepthBuffer();
			
				m_CurrentFB.depthOffset = surf.depthOffset;
				m_CurrentFB.depthLocation = surf.depthLocation;
				m_CurrentFB.depthPitch = surf.depthPitch;
				m_CurrentFB.depthFormat = surf.depthFormat;

				m_CurrentFB.width = surf.width;
				m_CurrentFB.height = surf.height;
			}
		}
		
		if(!(flags & RTF_KEEP_VIEWPORT))
		{
			SetViewport(RenderViewport(0, 0, rt->GetWidth(), rt->GetHeight(), 0.f, 1.f));
		}

		m_iTargetWidth = rt->GetWidth();
		m_iTargetHeight = rt->GetHeight();

	}
	
	GCM_CALL(cellGcmSetSurfaceWindow)(gCellGcmCurrentContext, &m_CurrentFB, CELL_GCM_WINDOW_ORIGIN_TOP, PS3_DIRECTX_COMPATIBLE_PIXEL_CENTER);

#elif defined(ENF_OGL)
	return;
	if(!(flags & RTF_KEEP_RENDERTARGET))
	{
		//TODO: cube face!
		GLCALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + rtindex, GL_TEXTURE_2D, rt->GetGLTexture(), 0));
		m_iCurrentRB = rt->GetGLTexture();
	}
	else
	{
		GLCALL(glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + rtindex, GL_TEXTURE_2D, m_iCurrentRB, 0));
	}
	if(rtindex == 0)
	{
		if(!(flags & RTF_KEEP_DEPTHSTENCILTARGET))
		{
			if(rt->GetDepthBuffer() != 0)
			{
				GLCALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt->GetDepthBuffer()));
				m_iCurrentDB = rt->GetDepthBuffer();
			}
			else
			{
				m_iCurrentDB = 0xffffffff;
			}
		}
		else
		{
			GLCALL(glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_iCurrentDB));
		}
	}

#endif
	
}


//-----------------------------------------------------------------------
ENF_INLINE bool IRenderer::ValidateMRT() const
{
	//all MRT have to have the same width and height and bit depth (unless D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS is set)
	// other related caps bits: D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING, blending, alpha test.. (IDirect3D9::CheckDeviceFormat 
	// with the USAGE_QUERY_POSTPIXELSHADER_BLENDING for exact test for specific format)
	 
	//this really shouldn't happen
	int currDepth  = m_iRTStackDepth[0] - 1;
	if (currDepth < 0)
		return false;

#ifdef ENF_DX9

	uint needWidth  = m_RTStack[0][currDepth].m_iRTWidth;
	uint needHeight = m_RTStack[0][currDepth].m_iRTHeight;

	D3DSURFACE_DESC desc;
	m_RTStack[0][currDepth].m_pRender->GetDesc(&desc);
	uint needStride = GetFormatStride(static_cast<TextureFormat>(desc.Format));

	//test for format
	int mrtIndex = 1;
	for (; mrtIndex < MAX_RENDER_TARGETS; mrtIndex++)		
	{
		currDepth = m_iRTStackDepth[mrtIndex] - 1;
		if (currDepth < 0)
			break;

		if (!m_RTStack[mrtIndex][currDepth].m_pRender)
			return false;

		m_RTStack[mrtIndex][currDepth].m_pRender->GetDesc(&desc);
		uint stride = GetFormatStride(static_cast<TextureFormat>(desc.Format));

		if (needWidth  != m_RTStack[mrtIndex][currDepth].m_iRTWidth || 
			 needHeight != m_RTStack[mrtIndex][currDepth].m_iRTHeight || 
			 needStride != stride)
		{
			return false;			
		}
	}

	//test for gaps in MRTs
	mrtIndex++;
	for (; mrtIndex < MAX_RENDER_TARGETS; mrtIndex++)		
	{
		currDepth = m_iRTStackDepth[mrtIndex] - 1;
		if (currDepth >= 0)
		{
			if (m_RTStack[mrtIndex][currDepth].m_pRender)
				return false;
		}
	}

#endif

	return true;
}


//-----------------------------------------------------------------------
void IRenderer::PushRenderTarget(uint rtindex, RenderTarget *rt, uint face, uint flags)
{
	enf_assert(rtindex < MAX_RENDER_TARGETS);
	enf_assert(m_iRTStackDepth[rtindex] < MAX_RT_DEPTH);
	enf_assert(rt != NULL);

	RTStack* rts = &m_RTStack[rtindex][m_iRTStackDepth[rtindex]++];
	
#if defined(ENF_DX9) || defined(ENF_XENON)
	m_pd3dDevice->GetViewport((D3DVIEWPORT9*)&rts->m_Viewport);
	if(m_bFloatingPointDepthBuffer)
		enf_swap(rts->m_Viewport.MinZ, rts->m_Viewport.MaxZ);

	if(rtindex == 0)
		m_pd3dDevice->GetDepthStencilSurface(&rts->m_pDepth);
	else
		rts->m_pDepth = NULL;

	if (m_iRTStackDepth[rtindex] > 1 || rtindex == 0)
	{
		m_pd3dDevice->GetRenderTarget(rtindex, &rts->m_pRender);
	} else
	{
		rts->m_pDepth	= NULL;
		rts->m_pRender = NULL;
	}

	if(!(flags & RTF_KEEP_RENDERTARGET))
	{
		if(rt->IsCube())
		{
			IDirect3DCubeTexture9* ctex = (IDirect3DCubeTexture9*)rt->GetD3DTexture();
			if(ctex)
			{
				IDirect3DSurface9* surf;
				ctex->GetCubeMapSurface((D3DCUBEMAP_FACES)face, 0, &surf);
				m_pd3dDevice->SetRenderTarget(rtindex, surf);
				surf->Release();
			}
		}
		else
		{
			m_pd3dDevice->SetRenderTarget(rtindex, rt->GetSurface());
		}
	}

	if(rtindex == 0)
	{
		rts->m_iRTWidth = m_iTargetWidth;
		rts->m_iRTHeight = m_iTargetHeight;

		if(!(flags & RTF_KEEP_DEPTHSTENCILTARGET))
			m_pd3dDevice->SetDepthStencilSurface(rt->GetDepthSurface());

		if(!(flags & RTF_KEEP_VIEWPORT))
		{
			SetViewport(RenderViewport(0, 0, rt->GetWidth(), rt->GetHeight(), 0, 1));
		}
		else
		{
			SetViewport(rts->m_Viewport);
		}
		m_iTargetWidth = rt->GetWidth();
		m_iTargetHeight = rt->GetHeight();
	}
#elif defined(ENF_PS3)
	//TODO: not good, I just need to keep buffers and later create surface on the fly for cellgcmSetSurface, because 
	//surface has 4 buffers for each MRT and can have just one format
	g_pRender->CheckCommandBuffer(256);

	rts->m_FB = m_CurrentFB;
	
	if(rtindex == 0)
		rts->m_pDepth = m_pCurrentDepth;
	else
		rts->m_pDepth = NULL;

	if (m_iRTStackDepth[rtindex] > 1 || rtindex == 0)
	{
		rts->m_pRender = m_pCurrentRender;
	} 
	else
	{
		rts->m_pDepth	= NULL;
		rts->m_pRender = NULL;
	}

	rts->m_bEnableAlphaTest = m_bEnableAlphaTest;
	rts->m_BlendState = m_iBlendStates[rtindex];
	
	const CellGcmSurface& surf = rt->GetSurface();

	if(!(flags & RTF_KEEP_RENDERTARGET) || rtindex > 0)
	{
		if(rt->GetRenderBuffer())
		{
			m_pCurrentRender = rt->GetRenderBuffer();
		
			m_CurrentFB.colorOffset[rtindex] = surf.colorOffset[0];
			if (rt->IsCube())
			{
				m_CurrentFB.colorOffset[rtindex] +=  rt->GetGCMFaceOffset(face);
			}

			m_CurrentFB.colorLocation[rtindex] = surf.colorLocation[0];
			m_CurrentFB.colorPitch[rtindex] = surf.colorPitch[0];

			m_CurrentFB.colorTarget = AVAILABLE_MRT_RT[rtindex];

			if (rtindex == 0)
			{
				m_CurrentFB.colorFormat = surf.colorFormat;
				m_CurrentFB.width = surf.width;
				m_CurrentFB.height = surf.height;
				m_CurrentFB.type = surf.type;
			}	
		}

		EnableAlphaTest(!IsFloat32SurfaceFormat(surf.colorFormat));
		if(IsFloat32SurfaceFormat(surf.colorFormat))
			SetBlendState(rtindex, false);	
	}
	
	if(rtindex == 0)
	{
		
		if(!(flags & RTF_KEEP_DEPTHSTENCILTARGET))
		{
			if(rt->GetDepthBuffer())
			{
				m_pCurrentDepth = rt->GetDepthBuffer();//->GetRef();
			
				m_CurrentFB.depthOffset = surf.depthOffset;
				m_CurrentFB.depthLocation = surf.depthLocation;
				m_CurrentFB.depthPitch = surf.depthPitch;
				m_CurrentFB.depthFormat = surf.depthFormat;

				m_CurrentFB.width = surf.width;
				m_CurrentFB.height = surf.height;
			}
		}
		/*else
		{
			cellGcmSetClearDepthStencil(gCellGcmCurrentContext, 0xFFFFFF00);
		}*/
		
		//store & set viewport
		rts->m_Viewport = GetViewport();
		
		rts->m_iRTWidth = m_iTargetWidth;
		rts->m_iRTHeight = m_iTargetHeight;

		if(!(flags & RTF_KEEP_VIEWPORT))
		{
			SetViewport(RenderViewport(0, 0, rt->GetWidth(), rt->GetHeight(), 0.f, 1.f));
		}

		m_iTargetWidth = rt->GetWidth();
		m_iTargetHeight = rt->GetHeight();
	}
	
	GCM_CALL(cellGcmSetSurfaceWindow)(gCellGcmCurrentContext, &m_CurrentFB, CELL_GCM_WINDOW_ORIGIN_TOP, PS3_DIRECTX_COMPATIBLE_PIXEL_CENTER);
	
#elif defined(ENF_OGL)
	return;
	
	if(rtindex == 0)
		rts->m_iDB = m_iCurrentDB;
	else
		rts->m_iDB = 0xffffffff;

	if (m_iRTStackDepth[rtindex] > 1 || rtindex == 0)
	{
		rts->m_iRB = m_iCurrentRB;
	}
	else
	{
		rts->m_iDB = 0xffffffff;
		rts->m_iRB = 0xffffffff;
	}

	rts->m_iFB = m_iCurrentFB;

	GLuint fid;
	GLCALL(glGenFramebuffers(1, &fid));

	m_iCurrentFB = fid;
	GLCALL(glBindFramebuffer(GL_FRAMEBUFFER, fid));

	if(!(flags & RTF_KEEP_RENDERTARGET))
	{
		//TODO: cube face!
		GLCALL(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + rtindex, GL_TEXTURE_2D, rt->GetGLTexture(), 0));
		m_iCurrentRB = rt->GetGLTexture();
	}
	else
	{
		GLCALL(glFramebufferTexture2DEXT(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + rtindex, GL_TEXTURE_2D, m_iCurrentRB, 0));
	}
	rts->m_Viewport = m_Viewport;
	if(rtindex == 0)
	{
		rts->m_iRTWidth = m_iTargetWidth;
		rts->m_iRTHeight = m_iTargetHeight;

		if(!(flags & RTF_KEEP_DEPTHSTENCILTARGET))
		{
			if(rt->GetDepthBuffer() != 0)
			{
				GLCALL(glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rt->GetDepthBuffer()));
				m_iCurrentDB = rt->GetDepthBuffer();
			}
			else
			{
				m_iCurrentDB = 0xffffffff;
			}
		}
		else
		{
			GLCALL(glFramebufferRenderbufferEXT(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_iCurrentDB));
		}

		if(!(flags & RTF_KEEP_VIEWPORT))
		{
			SetViewport(RenderViewport(0, 0, rt->GetWidth(), rt->GetHeight(), 0, 1));
		}
		else
		{
			SetViewport(rts->m_Viewport);
		}
		m_iTargetWidth = rt->GetWidth();
		m_iTargetHeight = rt->GetHeight();
	}
	GL_CHECKFBO(GL_FRAMEBUFFER);

#endif
}

//-----------------------------------------------------------------------
#ifdef ENF_DX9
void IRenderer::PushRenderTarget(uint rtindex, const SwapChain* sw)
{
	enf_assert(rtindex < MAX_RENDER_TARGETS);
	enf_assert(m_iRTStackDepth[rtindex] < MAX_RT_DEPTH);
	enf_assert(sw != NULL);

	RTStack* rts = &m_RTStack[rtindex][m_iRTStackDepth[rtindex]++];
	m_pd3dDevice->GetViewport((D3DVIEWPORT9*)&rts->m_Viewport);
	if(rtindex == 0)
		m_pd3dDevice->GetDepthStencilSurface(&rts->m_pDepth);
	else
		rts->m_pDepth = NULL;

	if (m_iRTStackDepth[rtindex] > 1 || rtindex == 0)
	{
		m_pd3dDevice->GetRenderTarget(rtindex, &rts->m_pRender);
	}
	else
	{
		rts->m_pDepth	= NULL;
		rts->m_pRender = NULL;
	}

	ComPtr<_D3DSurface> surf;
	sw->m_pSwapChain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, surf.Init());

	D3DSURFACE_DESC desc;
	surf->GetDesc(&desc);

	m_pd3dDevice->SetRenderTarget(rtindex, surf);

	if(rtindex == 0)
	{
		rts->m_iRTWidth = m_iTargetWidth;
		rts->m_iRTHeight = m_iTargetHeight;
		m_pd3dDevice->SetDepthStencilSurface(sw->m_pDepthBuffer);
		D3DVIEWPORT9 vp = {0, 0, sw->m_iWidth, sw->m_iHeight, 0, 1.0f};
		m_pd3dDevice->SetViewport(&vp);
		m_iTargetWidth = sw->GetWidth();
		m_iTargetHeight = sw->GetHeight();
	}
}
#endif

//-----------------------------------------------------------------------
void Renderer::SetVertexShader(const VertexShader* vsh)
{
#if defined(ENF_DX9) || defined(ENF_XENON)
	LPDIRECT3DVERTEXSHADER9 pVsh = vsh == NULL ? NULL : (LPDIRECT3DVERTEXSHADER9)vsh->m_pShader;

	if(m_pVShader == pVsh)
		return;
	m_pVShader = pVsh;

	m_pd3dDevice->SetVertexShader(pVsh);
#elif defined(ENF_PS3)
	CellCgbProgram* sh = vsh ? (CellCgbProgram*)vsh->m_pShader : 0;
	if(m_pVShader == sh )
	{
		return;
	}

	m_pVShader = sh;
	if(m_pVShader)
	{
		//printf("set vertex shader: %s\n", vsh->GetName().cstr());
		m_iCommitMask |= GCMCM_VERTEXSHADER;
		//m_VSConstChanged = 0;//~ULONG64(0);
	}
#elif defined(ENF_OGL)
	CGprogram sh = vsh ? (CGprogram)vsh->m_pShader : 0;
	if(m_iVShader == sh)
		return;
	m_iVShader = sh;
	m_iCommitMask |= GLCM_VERTEXSHADER;
	m_VSConstChanged = ~ULONG64(0);
#endif

#ifdef ENF_DEVELOPER
	m_Stats.VSChanges++;
#endif
}

//-----------------------------------------------------------------------
void Renderer::SetPixelShader(const PixelShader *psh)
{
#if defined(ENF_DX9) || defined(ENF_XENON)
	LPDIRECT3DPIXELSHADER9 pPsh = (psh == NULL ? NULL : (LPDIRECT3DPIXELSHADER9)psh->m_pShader);

	if(m_pPShader == pPsh)
		return;
	m_pPShader = pPsh;

	m_pd3dDevice->SetPixelShader(pPsh);
#elif defined(ENF_PS3)

	if(m_pFShader == psh)
		return;

	m_pFShader = (PixelShader*) psh;
	m_iCommitMask |= GCMCM_PIXELSHADER;
	m_PSConstChanged = ~ULONG64(0);

#elif defined(ENF_OGL)
	CGprogram sh = psh ? (CGprogram)psh->m_pShader : 0;
	if(m_iPShader == sh)
		return;
	m_iPShader = sh;
	m_iCommitMask |= GLCM_PIXELSHADER;
	m_PSConstChanged = ~ULONG64(0);
#endif

#ifdef ENF_DEVELOPER
	m_Stats.PSChanges++;
#endif
}

//-----------------------------------------------------------------------
void Renderer::SetStreamSource(uint stream, VertexBuffer* vertexbuffer, uint offset, uint stride)
{
	enf_assert(stream < MAX_STREAMS);
	if(vertexbuffer == NULL)
	{
		ClearStreamSource(stream);
		return;
	}
#ifdef ENF_DX9
	SetStreamSourceInternal(stream, vertexbuffer->m_pVB, offset, stride);
#elif defined(ENF_XENON)
	SetStreamSourceInternal(stream, &vertexbuffer->m_VB, offset, stride);
#elif defined(ENF_PS3)
	SetStreamSourceInternal(stream, vertexbuffer->m_pVB, vertexbuffer->GetBufferLocation(), offset, stride);//, vertexbuffer->GetID());
#elif defined(ENF_OGL)
	SetStreamSourceInternal(stream, vertexbuffer->m_iBuffer, offset, stride);
#endif
}

//-----------------------------------------------------------------------
void Renderer::SetIndices(IndexBuffer* indexdata)
{
#ifdef ENF_XENON
	IDirect3DIndexBuffer9* ib = indexdata ? &indexdata->m_IB : NULL;
	if(m_pIndexBuffer == ib)
		return;
	m_pIndexBuffer = ib;
	m_pd3dDevice->SetIndices(ib);
#elif defined(ENF_DX9)
	IDirect3DIndexBuffer9* ib = indexdata ? indexdata->m_pIB : NULL;
	if(m_pIndexBuffer == ib)
		return;
	m_pIndexBuffer = ib;
	m_pd3dDevice->SetIndices(ib);
#elif defined(ENF_PS3)
	if(m_pIndexBuffer != indexdata)
	{
		m_pIndexBuffer = indexdata;
		//m_iCommitMask |= GCMCM_INDEXBUFFER;
	}
#elif defined(ENF_OGL)
	if(m_pIndexBuffer != indexdata)
	{
		m_pIndexBuffer = indexdata;
		m_IndexBufferFormat = (indexdata && indexdata->GetFormat() == IBFMT_INDEX16) ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
		m_iCommitMask |= GLCM_INDEXBUFFER;
	}
#endif

#ifdef ENF_DEVELOPER
	m_Stats.IBChanges++;
#endif
}

//-----------------------------------------------------------------------
void Renderer::ResetStats()
{
	m_Stats.Clear();
}

//-----------------------------------------------------------------------
bool Renderer::Begin()
{
	m_iFrame++;

	return true;
}

//-----------------------------------------------------------------------
void Renderer::End()
{
	ReleaseReferences();
}

//-----------------------------------------------------------------------
void Renderer::SetViewport(const RenderViewport& vp)
{
	m_Viewport.Width = vp.Width;
	m_Viewport.Height = vp.Height;
	m_Viewport.X = vp.X;
	m_Viewport.Y = vp.Y;
	m_Viewport.MaxZ = enf_limit(vp.MaxZ, 0.0f, 1.0f);
	m_Viewport.MinZ = enf_limit(vp.MinZ, 0.0f, m_Viewport.MaxZ);
#if defined(ENF_DX9) || defined(ENF_XENON)
	D3DVIEWPORT9 d3dvp;
	d3dvp.Width = vp.Width;
	d3dvp.Height = vp.Height;
	d3dvp.X = vp.X;
	d3dvp.Y = vp.Y;

	if(m_bFloatingPointDepthBuffer)
	{
		d3dvp.MinZ = m_Viewport.MaxZ;
		d3dvp.MaxZ = m_Viewport.MinZ;
	}
	else
	{
		d3dvp.MinZ = m_Viewport.MinZ;
		d3dvp.MaxZ = m_Viewport.MaxZ;
	}
	m_pd3dDevice->SetViewport(&d3dvp);
#elif defined(ENF_PS3)

	//this is quite strange - both documentations, the RSX and PS3 have error in the computation of transformation matrix
	//from NDC to viewport, thus from <-1, 1>x<-1, 1> to viewport coordinates. 
	//
	// [WPx, WPy, WPz] = | Sx  0  0 Ox | | NDCx |
	//							|  0 Sy  0 Oy | | NDCy |
	//							|  0  0 Sz Oz | | NDCz |
	//												 |    1 |
	//
	// to be compatible with DirectX render, we must add to offset 0.5 in each direction !

	if (vp.X != m_SelectedViewPort.X || 
		 vp.Y != m_SelectedViewPort.Y ||
		 vp.Width != m_SelectedViewPort.Width ||
		 vp.Height != m_SelectedViewPort.Height ||
		 vp.MinZ != m_SelectedViewPort.MinZ ||
		 vp.MaxZ != m_SelectedViewPort.MaxZ)
	{
		m_SelectedViewPort = vp;

		//! DirectX style
		float scale[4] = {
			vp.Width * 0.5f, 
			vp.Height * -0.5f, 
			vp.MaxZ - vp.MinZ,
			0.0};

		float offset[4] = {
			0.5f + vp.X + vp.Width * 0.5f, 
			0.5f + vp.Y + vp.Height * 0.5f, 
			vp.MinZ,
			0.0};

		GCM_CALL(cellGcmSetViewport)(gCellGcmCurrentContext, vp.X, vp.Y, vp.Width, vp.Height, vp.MinZ, vp.MaxZ, scale, offset);
	}

#elif defined(ENF_OGL)
	GLCALL(glViewport(vp.X, vp.Y, vp.Width, vp.Height));
	GLCALL(glDepthRange(vp.MinZ, vp.MaxZ));
	GLCALL(glScissor(vp.X, vp.Y, vp.Width, vp.Height));
#endif
}

//----------------------------------------------------------------------
DepthStencilStateDef::DepthStencilStateDef(bool depthEnable, bool depthWrite, RenderCmpFunc depthFunc): _Value(0)
{
	DepthEnable = depthEnable;
	DepthWriteEnable = depthWrite;
	DepthFunc = depthFunc;
	StencilEnable = false;
	TwoSidedStencilEnable = false;
	FrontStencilFailOp = STENCILOP_KEEP;
	FrontStencilDepthFailOp = STENCILOP_KEEP;
	FrontStencilPassOp = STENCILOP_KEEP;
	FrontStencilFunc = CMP_NEVER;
	BackStencilFailOp = STENCILOP_KEEP;
	BackStencilDepthFailOp = STENCILOP_KEEP;
	BackStencilPassOp = STENCILOP_KEEP;
	BackStencilFunc = CMP_NEVER;
	StencilReadMask = 0xff;
	StencilWriteMask = 0xff;
}

DepthStencilStateDef::DepthStencilStateDef(bool depthEnable, bool depthWrite, RenderCmpFunc depthFunc,
			RenderStencilOp frontStencilFailOp,
			RenderStencilOp frontStencilDepthFailOp,
			RenderStencilOp fontStencilPassOp,
			RenderCmpFunc frontStencilFunc,
			ubyte stencilReadMask,
			ubyte stencilWriteMask): _Value(0)
{
	DepthEnable = depthEnable;
	DepthWriteEnable = depthWrite;
	DepthFunc = depthFunc;
	StencilEnable = true;
	TwoSidedStencilEnable = false;
	FrontStencilFailOp = frontStencilFailOp;
	FrontStencilDepthFailOp = frontStencilDepthFailOp;
	FrontStencilPassOp = fontStencilPassOp;
	FrontStencilFunc = frontStencilFunc;
	BackStencilFailOp = STENCILOP_KEEP;
	BackStencilDepthFailOp = STENCILOP_KEEP;
	BackStencilPassOp = STENCILOP_KEEP;
	BackStencilFunc = CMP_NEVER;
	StencilReadMask = stencilReadMask;
	StencilWriteMask = stencilWriteMask;
}

DepthStencilStateDef::DepthStencilStateDef(bool depthEnable, bool depthWrite, RenderCmpFunc depthFunc,
			RenderStencilOp frontStencilFailOp,
			RenderStencilOp frontStencilDepthFailOp,
			RenderStencilOp fontStencilPassOp,
			RenderCmpFunc frontStencilFunc,
			RenderStencilOp backStencilFailOp,
			RenderStencilOp backStencilDepthFailOp,
			RenderStencilOp backStencilPassOp,
			RenderCmpFunc backStencilFunc,
			ubyte stencilReadMask,
			ubyte stencilWriteMask): _Value(0)
{
	DepthEnable = depthEnable;
	DepthWriteEnable = depthWrite;
	DepthFunc = depthFunc;
	StencilEnable = true;
	TwoSidedStencilEnable = false;
	FrontStencilFailOp = frontStencilFailOp;
	FrontStencilDepthFailOp = frontStencilDepthFailOp;
	FrontStencilPassOp = fontStencilPassOp;
	FrontStencilFunc = frontStencilFunc;
	BackStencilFailOp = backStencilFailOp;
	BackStencilDepthFailOp = backStencilDepthFailOp;
	BackStencilPassOp = backStencilPassOp;
	BackStencilFunc = backStencilFunc;
	StencilReadMask = stencilReadMask;
	StencilWriteMask = stencilWriteMask;
}

//----------------------------------------------------------------------
DepthStencilStateHandle Renderer::CreateDepthStencilState(const DepthStencilStateDef& def)
{
	for(uint n = 0; n < m_DepthStencilStateDefs.GetCardinality(); n++)
	{
		if(m_DepthStencilStateDefs[n]._Value == def._Value)
			return n;
	}
	return m_DepthStencilStateDefs.Insert(def);
}

//-----------------------------------------------------------------------
SamplerFilterStateHandle Renderer::CreateSamplerFilterState(const SamplerFilterState& state)
{
	enf_assert(sizeof(SamplerFilterStateHandle) == sizeof(SamplerFilterState));
	return *(SamplerFilterStateHandle*)&state;
}

//-----------------------------------------------------------------------
#ifdef ENF_PS3
	SamplerFilterState::SamplerFilterState(uint minFilter, uint magFilter, uint mipFilter, uint maxAniso)
	{
		enf_assert(maxAniso >= 1 && maxAniso <= 16);

		Value = 0;
		Convolution = CELL_GCM_TEXTURE_CONVOLUTION_QUINCUNX;

		MagFilter = magFilter > TEXF_POINT ? CELL_GCM_TEXTURE_LINEAR : CELL_GCM_TEXTURE_NEAREST;

		if (mipFilter > TEXF_NONE)
		{
			if (mipFilter > TEXF_POINT)
			{
				MinFilter = minFilter < TEXF_LINEAR ? CELL_GCM_TEXTURE_NEAREST_LINEAR : CELL_GCM_TEXTURE_LINEAR_LINEAR;
			}
			else
			{
				MinFilter = minFilter < TEXF_LINEAR ? CELL_GCM_TEXTURE_NEAREST_NEAREST : CELL_GCM_TEXTURE_LINEAR_NEAREST;
			}
		}
		else
		{
			MinFilter = minFilter > TEXF_POINT ? CELL_GCM_TEXTURE_LINEAR : CELL_GCM_TEXTURE_NEAREST;
		}

		if (magFilter != TEXF_ANISOTROPIC || maxAniso <= 1)
		{
			MaxAniso_MipLODBias = CELL_GCM_TEXTURE_MAX_ANISO_1;
		}
		else
		{
			if (maxAniso < 4)
				MaxAniso_MipLODBias = CELL_GCM_TEXTURE_MAX_ANISO_2;
			else if (maxAniso < 6)
				MaxAniso_MipLODBias = CELL_GCM_TEXTURE_MAX_ANISO_4;
			else if (maxAniso < 8)
				MaxAniso_MipLODBias = CELL_GCM_TEXTURE_MAX_ANISO_6;
			else if (maxAniso  < 10)
				MaxAniso_MipLODBias = CELL_GCM_TEXTURE_MAX_ANISO_8;
			else if (maxAniso < 12)
				MaxAniso_MipLODBias = CELL_GCM_TEXTURE_MAX_ANISO_10;
			else if (maxAniso < 15)
				MaxAniso_MipLODBias = CELL_GCM_TEXTURE_MAX_ANISO_12;
			else
				MaxAniso_MipLODBias = CELL_GCM_TEXTURE_MAX_ANISO_16;
		}
	}
#endif

//-----------------------------------------------------------------------
SamplerAddressState::SamplerAddressState(uint uAddress, uint vAddress, uint wAddress, uint borderColor)
{
	#ifdef ENF_PS3
		cbValue = 0;
	#else
		Dummy = 0;
	#endif

	UAddress = uAddress;
	VAddress = vAddress;
	WAddress = wAddress;
	BorderColor = borderColor;

	Flags = 1;
	if(uAddress == TADDRESS_BORDER || vAddress == TADDRESS_BORDER || wAddress == TADDRESS_BORDER)
		Flags |= 2;
}

//-----------------------------------------------------------------------
SamplerAddressState::SamplerAddressState(uint uAddress, uint vAddress, uint borderColor)
{
	#ifdef ENF_PS3
		cbValue = 0;
	#else
		Dummy = 0;
	#endif

	UAddress = uAddress;
	VAddress = vAddress;
	WAddress = 0;
	BorderColor = borderColor;

	Flags = 0;
	if(uAddress == TADDRESS_BORDER || vAddress == TADDRESS_BORDER)
		Flags |= 2;

	#ifdef ENF_PS3
		WAddress = CELL_GCM_TEXTURE_WRAP;
	#endif
}

//-----------------------------------------------------------------------
SamplerAddressStateHandle Renderer::CreateSamplerAddressState(const SamplerAddressState& state)
{
	for(uint n = 0; n < m_SamplerAddressStates.GetCardinality(); n++)
	{
		if(m_SamplerAddressStates[n].Value == state.Value)
			return n;
	}
	SamplerAddressState& newState = m_SamplerAddressStates.Append();
	newState = state;
	return m_SamplerAddressStates.GetCardinality() - 1;
}

//-----------------------------------------------------------------------
void Renderer::GetShaderAllocation(uint& vs, uint& ps) const
{
#ifdef ENF_XENON
	DWORD flags, dvs, dps;
	m_pd3dDevice->GetShaderGPRAllocation(&flags, &dvs, &dps);
	vs = dvs;
	ps = dps;
#endif

}

//-----------------------------------------------------------------------
void Renderer::SetShaderAllocation(uint vs, uint ps)
{
#ifdef ENF_XENON
	m_pd3dDevice->SetRenderState(D3DRS_HIGHPRECISIONBLENDENABLE, FALSE);
	m_pd3dDevice->SetShaderGPRAllocation(0, vs, ps);
#endif
}

//-----------------------------------------------------------------------
RenderSetup::RenderSetup():
m_iBackBufferWidth(0),
m_iBackBufferHeight(0),
m_iBackBufferCount(0),
m_BackBufferFormat(TEXFMT_UNKNOWN),
m_DepthBufferFormat(TEXFMT_UNKNOWN),
m_FSAAType(FSAATYPE_NONE),
m_iFSAAQuality(0),
m_SwapEffect(0),
m_iRefreshRateInHz(0),
m_PresentationInterval(0),
m_iFlags(0),
m_bWindowed(false)
{
}

//-----------------------------------------------------------------------
uint IRenderer::GetNumFSAADefs() const
{
	return m_FSAADefs.GetCardinality();
}

//-----------------------------------------------------------------------
const FSAADef& IRenderer::GetFSAADef(uint index) const
{
	return m_FSAADefs[index];
}

//-----------------------------------------------------------------------
bool IRenderer::SetFSAA(uint fsaa, uint quality)
{
	enf_assert(m_FSAADefs.GetCardinality() > fsaa);
	enf_assert(m_FSAADefs[fsaa].MaxQuality > quality);

	//TODO: validate this?
	m_iFSAA = fsaa;
	m_iFSAAQuality = quality;
	return true;
}

//-----------------------------------------------------------------------
bool IRenderer::GetAdapter(uint adapter, RenderAdapterInfo *adapterinfo)
{
	if(adapter >= m_Adapters.GetCardinality())
		return false;

	if(adapterinfo)
		*adapterinfo = *m_Adapters[adapter];

	return true;
}

//-----------------------------------------------------------------------
bool IRenderer::GetDevice(uint adapter, uint device, RenderDeviceInfo *deviceinfo)
{
	if(adapter >= m_Adapters.GetCardinality())
		return false;

	RenderAdapter* pAdapterInfo = m_Adapters[adapter];

	if(pAdapterInfo->m_Devices.GetCardinality() <= device)
		return false;

	if(deviceinfo)
		*deviceinfo = pAdapterInfo->m_Devices[device];

	return true;
}

//-----------------------------------------------------------------------
uint IRenderer::AddMode(uint adapter, uint device, uint width, uint height)
{
	if(adapter >= m_Adapters.GetCardinality())
		return INDEX_NOT_FOUND;

	m_iAdapter = adapter;
	RenderAdapter* pAdapter = m_Adapters[adapter];

	if(device >= pAdapter->m_Devices.GetCardinality())
		return INDEX_NOT_FOUND;

	RenderDevice*  pDevice  = &pAdapter->m_Devices[device];

	//is this mode already present?
	for(uint m = 0; m < pDevice->m_Modes.GetCardinality(); m++)
	{
		if(pDevice->m_Modes[m].Width == width &&
			pDevice->m_Modes[m].Height == height)
			return m;
	}

	//no, make copy of another and insert it
	RenderModeInfo mode;
	if(pDevice->m_Modes.IsEmpty())
	{
		mode.Format = TEXFMT_UNKNOWN;
		mode.DepthStencilFormat = TEXFMT_UNKNOWN;
	}
	else
		mode = pDevice->m_Modes[0];

	mode.Width = width;
	mode.Height = height;
	mode.Custom = true;
	pDevice->m_Modes.Insert(mode);

	return pDevice->m_Modes.GetCardinality() - 1;
}

//-----------------------------------------------------------------------------
bool IRenderer::GetMode(uint adapter, uint device, uint mode, RenderModeInfo *modeinfo)
{
	if(adapter >= m_Adapters.GetCardinality())
		return false;

	//fill API modes string
	RenderAdapter* pAdapterInfo = m_Adapters[adapter];

	if(device == INDEX_NOT_FOUND)
		device = pAdapterInfo->m_iCurrentDevice;

	RenderDevice*  pDevice  = &pAdapterInfo->m_Devices[device];

	if(mode >= pDevice->m_Modes.GetCardinality())
		return false;

	if(modeinfo)
		*modeinfo = pDevice->m_Modes[mode];

	return true;
}

//-----------------------------------------------------------------------
uint IRenderer::LoadCursorImage(Texture* tex, const RRect& srcRect, int hotspotX, int hotspotY)
{
#ifndef ENF_HWCURSOR
	return 0;
#else
	enf_assert(tex != NULL);
	LockedRect lrect;
	SurfaceDesc desc;

	uint width = srcRect.Right - srcRect.Left;
	uint height = srcRect.Bottom - srcRect.Top;

	if(width > 32 || height > 32)
		return 0;

	uint width2 = 32;//GetNearestPower2(width);
	uint height2 = 32;//GetNearestPower2(height);

	uint cursorID = 0;

	if(tex->Lock(0, &srcRect, &lrect, &desc, true))
	{
		ComPtr<IDirect3DSurface9> surf;

		if(m_pd3dDevice->CreateOffscreenPlainSurface(width2, height2, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, surf.Init(), NULL) == D3D_OK)
		{
			D3DLOCKED_RECT destRect;

			if(surf->LockRect(&destRect, NULL, 0) == D3D_OK)
			{
				if(width2 > width || height2 > height)
				{
					//clear first
					for(uint h = 0; h < height2; h++)
					{
						uint* destPtr = (uint*)((ubyte*)destRect.pBits + h * destRect.Pitch);
						for(uint w = 0; w < width2; w++)
							destPtr[w] = 0;
					}
				}

				if(IsDXTFormat(desc.Format))
				{
					for(uint h = 0; h < height; h++)
					{
						const uint* srcPtr = (uint*)((ubyte*)lrect.pBits + h * lrect.Pitch);
						uint* destPtr = (uint*)((ubyte*)destRect.pBits + h * destRect.Pitch);
						for(uint w = 0; w < width; w++)
						{
							uint p = ((w & 2) == 0 && (h & 2) == 0) ? 0xffffffff : 0;
							destPtr[w] = p;
						}
					}
				}
				else if(GetFormatStride(desc.Format) == 4)
				{
					for(uint h = 0; h < height; h++)
					{
						const uint* srcPtr = (uint*)((ubyte*)lrect.pBits + h * lrect.Pitch);
						uint* destPtr = (uint*)((ubyte*)destRect.pBits + h * destRect.Pitch);
						for(uint w = 0; w < width; w++)
						{
							ubyte a, r, g, b;
							DecodeChannels(desc.Format, srcPtr[w], r, g, b, a);

							//make alpha discrete
							a = a ? 0xff : 0;
							destPtr[w] = (a << 24) | (r << 16) | (g << 8) | b;
						}
					}
				}
				else if(GetFormatStride(desc.Format) == 2)
				{
					for(uint h = 0; h < height; h++)
					{
						const ushort* srcPtr = (ushort*)((ubyte*)lrect.pBits + h * lrect.Pitch);
						uint* destPtr = (uint*)((ubyte*)destRect.pBits + h * destRect.Pitch);
						for(uint w = 0; w < width; w++)
						{
							ubyte a, r, g, b;
							DecodeChannels(desc.Format, srcPtr[w], r, g, b, a);

							//make alpha discrete
							a = a ? 0xff : 0;
							a=r=g=b=0xff;
							destPtr[w] = (a << 24) | (r << 16) | (g << 8) | b;
						}
					}
				}
				surf->UnlockRect();
			}

			surf.AddRef();
			cursorID = m_Cursors.GetCardinality() + 1;
			CursorInfo& info = m_Cursors.Append();
			info.x = enf_limit(hotspotX, 0, (int)(width2 - 1));
			info.y = enf_limit(hotspotY, 0, (int)(height2 - 1));
			info.surface = surf;
		}
		tex->Unlock(0);
	}
	return cursorID;
#endif
}

//-----------------------------------------------------------------------
void IRenderer::ResetCursor()
{
#ifdef ENF_HWCURSOR
	uint curr = m_iCurrentCursor;
	m_iCurrentCursor = -1;
	SetCursor(curr);
#endif
}

//-----------------------------------------------------------------------
uint IRenderer::SetCursor(uint cursorImage)
{
#ifndef ENF_HWCURSOR
	return 0;
#else
	uint prevCursor = m_iCurrentCursor;
	if(cursorImage && cursorImage != prevCursor)
	{
		m_iCurrentCursor = cursorImage;
		const CursorInfo& ci = m_Cursors[cursorImage - 1];
		HRESULT hres = m_pd3dDevice->SetCursorProperties(ci.x, ci.y, ci.surface);
		enf_assert(hres == D3D_OK);
	}
	return prevCursor;
#endif
}

//-----------------------------------------------------------------------
bool IRenderer::ShowCursor(bool show, bool force)
{
#ifndef ENF_HWCURSOR
	return false;
#else
	if(force || m_bCursorVisible != show)
	{
		m_bCursorVisible = show;
		return m_pd3dDevice->ShowCursor(show) != 0;
	}
	
	return show;
#endif
}

//-----------------------------------------------------------------------
void IRenderer::SetCursorPos(uint x, uint y)
{
#ifdef ENF_HWCURSOR
	if(m_Setup.m_bWindowed)
	{
		TITLEBARINFO titleBar;
		titleBar.cbSize = sizeof(titleBar);
		::GetTitleBarInfo(m_hWnd, &titleBar);
		y += (titleBar.rcTitleBar.bottom - titleBar.rcTitleBar.top);
	}
	LogF(LL_NORMAL, "%d %d", x,y);
	m_pd3dDevice->SetCursorPosition(x, y, 0);
#endif
}

//-----------------------------------------------------------------------
#if !defined(ENF_DUMMYRENDER)

#if defined(ENF_DX9) 
void _D3DPERF_BeginEvent(uint color, const char* name, ...)
{
	if(D3DPERF_GetStatus() == 0)
		return;

	char buff[256];
	ushort wbuff[256];
	va_list argptr;

	va_start (argptr, name);
	vsprintf (buff, name, argptr);
	va_end (argptr);
	for(uint n = 0; ; n++)
	{
		wbuff[n] = (short)buff[n];
		if(buff[n] == 0)
			break;
	}
	D3DPERF_BeginEvent(color, (LPCWSTR)wbuff);
}


//-----------------------------------------------------------------------
void _D3DPERF_BeginEvent(uint color, const wchar_t* name)
{
	if(D3DPERF_GetStatus() == 0)
		return;

	D3DPERF_BeginEvent(color, (LPCWSTR)name);
}

//-----------------------------------------------------------------------
void _D3DPERF_SetMarker(uint color, const char* name, ...)
{
	if(D3DPERF_GetStatus() == 0)
		return;

	char buff[256];
	ushort wbuff[256];
	va_list argptr;

	va_start (argptr, name);
	vsprintf (buff, name, argptr);
	va_end (argptr);
	for(uint n = 0; ; n++)
	{
		wbuff[n] = (short)buff[n];
		if(buff[n] == 0)
			break;
	}
	D3DPERF_SetMarker(color, (LPCWSTR)wbuff);
}

//-----------------------------------------------------------------------
void _D3DPERF_EndEvent()
{
	if(D3DPERF_GetStatus() == 0)
		return;
	D3DPERF_EndEvent();
}

#elif defined(ENF_PS3)
//-----------------------------------------------------------------------
void _D3DPERF_BeginEvent(uint color, const char* name, ...)
{
	va_list argptr;

	char temp[1024];

	va_start (argptr,name);
	vsprintf (temp, name, argptr);
	va_end (argptr);

	cellGcmSetPerfMonPushMarker(gCellGcmCurrentContext, temp);//"Army Rendering");

	#ifdef PS3_GPU_MARKER_LOG
		LogF(LL_NORMAL, "%s%s", levelMarkerSpace + 10 - enf_limit(levelMarker++, 0, 9),  temp);
	#endif
}

//-----------------------------------------------------------------------
void _D3DPERF_BeginEvent(uint color, const wchar_t* name)
{
	unsigned char temp[1024];
	int n = 0;
	do
	{
		temp[n] = (unsigned char)name[n];
	} while(name[n++] != 0);
	
	#ifdef PS3_GPU_MARKER_LOG
		LogF(LL_NORMAL, "%s%s", levelMarkerSpace + 10 - enf_limit(levelMarker++, 0, 9),  temp);
	#endif
	
	cellGcmSetPerfMonPushMarker(gCellGcmCurrentContext, (const char*)temp);//"Army Rendering");
}

//-----------------------------------------------------------------------
void _D3DPERF_SetMarker(uint color, const char* name, ...)
{
	va_list argptr;

	char temp[1024];

	va_start (argptr,name);
	vsprintf (temp, name, argptr);
	va_end (argptr);

	#ifdef PS3_GPU_MARKER_LOG
		LogF(LL_NORMAL, "%s%s", levelMarkerSpace + 10 - enf_limit(levelMarker, 0, 9), temp);
	#endif

	cellGcmSetPerfMonMarker(gCellGcmCurrentContext, temp);
}

//-----------------------------------------------------------------------
void _D3DPERF_EndEvent()
{
	cellGcmSetPerfMonPopMarker(gCellGcmCurrentContext);//, "Army Rendering");

	#ifdef PS3_GPU_MARKER_LOG
		levelMarker--;
	#endif
}

#elif defined(ENF_OGL)
//-----------------------------------------------------------------------
void _D3DPERF_BeginEvent(uint color, const char* name, ...)
{
}

//-----------------------------------------------------------------------
void _D3DPERF_BeginEvent(uint color, const wchar_t* name)
{
}

//-----------------------------------------------------------------------
void _D3DPERF_SetMarker(uint color, const char* name, ...)
{
}

//-----------------------------------------------------------------------
void _D3DPERF_EndEvent()
{
}
#endif
#endif

#ifdef ENF_PS3



//-----------------------------------------------------------------------
uint64 IRenderer::GetLastFlipTime() const
{
	if (UsingSWRescaler())
	{
		return cellRescGetLastFlipTime();
	}
	else
	{
		return cellGcmGetLastFlipTime();
	}
}

//-----------------------------------------------------------------------
void Renderer::WaitForRSX()
{
	static const uint RSX_FINISHED_LABEL = 65;

	static uint labelVal = 0; 
	volatile uint* label = cellGcmGetLabelAddress(RSX_FINISHED_LABEL);
	*label = labelVal++;

	GCM_CALL(cellGcmSetWriteBackEndLabel)(gCellGcmCurrentContext, RSX_FINISHED_LABEL, labelVal);
	GCM_CALL(cellGcmFlush)(gCellGcmCurrentContext);

	while (*label != labelVal)
	{
		sys_timer_usleep(30);
	}
}

//-----------------------------------------------------------------------
void Renderer::LimitRSXBufferedFrames(uint numFrames)
{
	static const uint FRAME_LIMITER_LABEL = 64;

	volatile uint* finishedFrame = cellGcmGetLabelAddress(FRAME_LIMITER_LABEL);

	static bool inited = false;
	if (!inited)
	{
		*finishedFrame = 1;
		inited = true;
	}

	cellGcmSetWriteCommandLabel(gCellGcmCurrentContext, FRAME_LIMITER_LABEL, m_iFrame);
	GCM_CALL(cellGcmFlush)(gCellGcmCurrentContext);

	//Timer::PushTime();
	//bool sleeping = false;	

	while (m_iFrame - *finishedFrame > numFrames)
	{
		sys_timer_usleep(30);	
		//sleeping = true;
	}

	/*	
	float time = Timer::PopTime();
	if (sleeping)
		LogF(LL_DEBUG, "Waiting for %2.2fms for RSX to complete unfinished command buffer (frame %d, last rendered frame %d)", time, m_iFrame, *finishedFrame);
	*/
}


//----------------------------------------------------------------------
void IRenderer::StopRSX(CellGcmContextData* con)
{
	cellGcmSetWaitFlip(gCellGcmCurrentContext);
	GCM_CALL(cellGcmFinish)(gCellGcmCurrentContext, GCMFinishRefValue++);
}

//-----------------------------------------------------------------------
void Renderer::CheckCommandBuffer(uint reserveSize)
{
	uint currAddr = (uint) (gCellGcmCurrentContext->current) + reserveSize;
	uint endAddr  = (uint) (gCellGcmCurrentContext->end);

	if(currAddr >= endAddr)
	{
		IRenderer::ChangeCommandBuffer(gCellGcmCurrentContext, 0);
	}
}


//-----------------------------------------------------------------------
bool IRenderer::CheckCommandBufferAddress(CellGcmContextData* con)
{
	uchar *endBuff = (uchar*) gCellGcmCurrentContext->current;

	uint sizeLess = (m_iCurrCommandBuffer == ENF_COMMAND_BUFFERS - 1) ? 0x1000 : 0;

	// wait for RSX to finish all commands
	const uchar* startAddress = (uchar*) m_pCmdBufAddress[m_iCurrCommandBuffer];
	const uchar* endAddress   = startAddress + ENF_COMMAND_BUFFER_SIZE - 32 - sizeLess;

	return endBuff >= startAddress && endBuff < endAddress;
}



//-----------------------------------------------------------------------------
/*!
structures for search in CGB fragment shaders
*/
//-----------------------------------------------------------------------------
enum ENF_CELL_CGB_HEADER_ENUM
{
	CELL_CGB_CONSTANT_TABLE	= 1,
	CELL_CGB_LOOKUP_TABLE	= 2,
	CELL_CGB_PARAMETER_INFO	= 4,
};


struct __PACKED__ ENF_CellCgbHeader
{
	unsigned int fourcc;					//should be 'C' 'G' 'B' '\0'
	unsigned short format_version;	//0 for now
	unsigned short compiler_version;
	unsigned short ucode_size;			//size of the ucode in bytes
	unsigned char profile;				//CellCgbVertexProfile (0) or CellCgbFragmentProfile (1) as defined in cgb.h
	unsigned char content;				// combination of CELL_CGB_CONSTANT_TABLE,CELL_CGB_LOOKUP_TABLE and CELL_CGB_PARAMETER_TREE
};


struct __PACKED__ ENF_CellCgbMapEntry
{
	unsigned int name_offset;
	unsigned short parent;
	unsigned short resource;
};


struct __PACKED__ ENF_CellCgbParameterInfo
{
	unsigned short cg_type;
	unsigned short cg_resource;
	unsigned short flags;
	unsigned short value_count;
} ;


struct __PACKED__ ENF_CellCgbLevelAHeader
{
	unsigned short block_size;
	unsigned short constant_count;
};

struct __PACKED__ ENF_CellCgbLevelBHeader
{
	unsigned short block_size;
	unsigned short entry_count;
	unsigned short fp_offset_count;
};


struct FPResource 
{
	ushort _register;
	ushort count;
	ushort offset;
};


struct ENF_CellCgbProgram 
{ 
	const ENF_CellCgbHeader *header; 

	const char *ucode; 

	size_t constantCount; 
	const unsigned short *constantResources; 
	const float *constantValues; 

	size_t mapEntryCount; 
	const ENF_CellCgbMapEntry *mapEntries; 
	size_t fpOffsetCount; 
	const unsigned short *fpOffsets; 
	const char *stringTable; 

	unsigned int uniformCount; 

	const ENF_CellCgbParameterInfo* parameterInfos; 
};


//#define PARSE_CGB

#ifdef PARSE_CGB
	static bool test_cgb = false;
#endif


//-----------------------------------------------------------------------------
/*!
Return number of fake BOOL registers from CGB source program, then array of the 
registers and its offset in source code and if needed, the same for float registers.

The CGB format contains all used resources in fragment program thus it's possible 
to extract all information from it. Each resource has unique number, if is less
than 1024 it's a texture, the number is then the sampler. If is above, it's a constant
register and in another array the register name and offsets are. 

If the register is 65535 then it's a register defined by bool type and thus not usable
in PS3 fragment shader. For this kind we use PS_BREGISTER{0,1,2,3} which are for PS3 
shaders defined as float type in registers 44,45,46 and 47. This function distinguishes
according these registers the bool types.

CARE -> one register can be contained more times in microcode, but for BOOL registers
we don't care now. E.g.

const float4 AmbientSpecular : register(c0);
...
{
	...
	color += AmbientSpecular.rgb;								-> one usage and thus one offset in microcode
	color += GetSpecular(AmbientSpecular.rgb)				-> second usage and another offset of the same register
	...
}

\param origUCode		original CGB information from file
\param boolRegArray	array of found bools, can be NULL, then just number of bool is returned
\param floatRegArray array of found floats, can be NULL, then just number of bool is returned
\param numFloatRegs	number of used float registers
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC uint GetUCodeFakeBoolRegisters(CellCgbProgram* origUCode, FragRegister* boolRegArray, FragRegister* floatRegArray, uint& numFloatRegs)
{
	const ENF_CellCgbProgram* program = (const ENF_CellCgbProgram*) origUCode;
	const ENF_CellCgbHeader *header = program->header; 

	if (header->content & CELL_CGB_LOOKUP_TABLE == 0)
	{
		return 0;
	}

	const ENF_CellCgbLevelAHeader *headerA = (header->content & CELL_CGB_CONSTANT_TABLE) ? (const ENF_CellCgbLevelAHeader *) ((const char*) program->ucode + (header->ucode_size)) : NULL;

	const ENF_CellCgbLevelBHeader *headerB = (const ENF_CellCgbLevelBHeader *) (headerA ? 
		((const char*) headerA + headerA->block_size) :
		((const char*) program->ucode + (header->ucode_size)));

	const ushort* resources =(const ushort*) ((uchar*) (program->mapEntries) + headerB->entry_count*8);

	if (!resources)
	{
		return 0;
	}

	uint regCount = program->mapEntryCount;
	uint boolCount = 0;
	uint maxBoolReg = 0;

	numFloatRegs = 0;

	for (uint i = 0; i < regCount; i++)
	{
		int resNum = program->mapEntries[i].resource - 1024;

		#ifdef PARSE_CGB
			if (test_cgb)		
			{
				const FPResource *res = (const FPResource*) (resources + resNum);
				char name[1024];
				uint size = 1024;
				cellCgbMapGetName((CellCgbProgram*) program, i, name, &size);

				LogF(LL_NORMAL, "%s = %s %d, lookup %d, name offset %d, parent %d, resource id %d, count %d, offset %d", 
					name, 
					resNum < 0 ? "sampler" : "register",
					resNum < 0 ? resNum + 1024: res->_register, 
					cellCgbMapLookup((CellCgbProgram*) program, name), 
					program->mapEntries[i].name_offset, 
					program->mapEntries[i].parent, 
					resNum,
					res->count, 
					res->offset);
			}
		#endif

		if (resNum < 0 || resNum > 256 )
		{
			continue;
		}

		const FPResource *res = (const FPResource*) (resources + resNum);

		//test float registers
		if (res->_register < PS3BranchUCodeCache::BOOL_REGISTER_START)
		{
			if (floatRegArray)
			{
				ushort *offsets = (ushort*) &res->offset;
				for (uint r = 0; r < res->count; r++, numFloatRegs++)
				{
					floatRegArray[numFloatRegs].m_iRegister = res->_register;
					floatRegArray[numFloatRegs].m_iOffset   = offsets[r];					
				}
			}
			else
			{
				numFloatRegs += res->count;
			}
			
			continue;
		}
		
		//is fakeBool -> is BE than REGISTER_START, if res < 0, it can be e.g. sampler
		if (res->_register == 65535)
		{
			char name[1024];
			uint size = 1024;
			cellCgbMapGetName((CellCgbProgram*) program, i, name, &size);

			LogF(LL_ERROR, "%s is incorrectly defined bool value, probably defined for DX version not for PS3 (use PS_BREGISTER0-3)", name);
			continue;
		}

		//currently not implemented the multiply usage of one bool
		enf_assert(res->count == 1);
		if (boolRegArray)
		{			
			boolRegArray[boolCount].m_iRegister = res->_register;
			boolRegArray[boolCount].m_iOffset   = res->offset;
		}

		maxBoolReg = enf_max(maxBoolReg, (uint) (res->_register - PS3BranchUCodeCache::BOOL_REGISTER_START + 1));

		boolCount++;
	}

	//end of array
	if (boolRegArray)
	{
		boolRegArray[boolCount].m_iRegister = 0xFFFF;
		boolRegArray[boolCount].m_iOffset   = 0xFFFF;
	}
	
	if (boolCount != maxBoolReg)
	{
		LogF(LL_WARNING, "GetUCodeFakeBoolRegisters:: Boolean registers are not addressed from zero in one queue, the remmaping table will be bigger than neccessary -> used registers %d, max register %d", boolCount, maxBoolReg);
		boolCount = maxBoolReg;
	}

	return boolCount;
}


//-----------------------------------------------------------------------
int PS3BranchUCodeCache::GetUCodeBranchesCount(CellCgbProgram* origUCode, FragRegister* boolRegs, FragRegister* floatRegs, uint& floatCount)
{
	return GetUCodeFakeBoolRegisters(origUCode, boolRegs, floatRegs, floatCount);
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC uint GetUniqueNumber(uint regNum)
{
	enf_assert(regNum < 255);
	return 0xC0DECD00 + regNum;
}

//-----------------------------------------------------------------------
ENF_INLINEFUNC uint IsUniqueNumber(uint num)
{
	if ((num & 0xFFFFFF00) == 0xC0DECD00)
	{
		return num & 0xFF;
	}

	return 0xFFFFFFFF;
}


//-----------------------------------------------------------------------------
/*!
Extract one concrete uCode from original code defined by bool registers in mask.
Before the extraction each "bool" register is filled with 0.0 or 1.0 constant 
depending on bool value from mask, each float register is filled with unique 
number with the number of register.

After the extraction, the code contains only the needed variant of ucode without 
any IF..ENDIF ucode constructs. Because the offsets of float registers are different,
the new code is parsed and the register number are extracted based on the unique 
number.

\param origCGBCode	original code
\param mask				mask of bool regs
*/
//-----------------------------------------------------------------------------
#ifdef ENF_GCM_FRAGMENT_SHADER_FORMAT
const PS3BranchUCodeCache::BranchCode* PS3BranchUCodeCache::ExtractBranch(PixelShader* ps, int mask)
{
	uint numFloats = ps->m_iFloatRegCount;
	uint numBools  = ps->m_iBoolRegCount;

	FragRegister* boolRegs  = ps->m_pRegisters + numFloats;
	FragRegister* floatRegs = ps->m_pRegisters;

	enf_assert(1 << numBools == m_iNumBranches);
	enf_assert(numFloats < 256 && numBools < 64);
	enf_assert(!m_pBranches[mask].m_pUCode);

	uchar* uCode   = (uchar*) ps->m_pUCode;
	uint uCodeSize = ps->m_iUCodeSize;

#else
const PS3BranchUCodeCache::BranchCode* PS3BranchUCodeCache::ExtractBranch(CellCgbProgram* origCGBCode, int mask)
{
	FragRegister boolRegs[64];
	FragRegister floatRegs[256];
	
	//read all bool and float register offsets
	uint numFloats = 0;
	uint numBools = GetUCodeFakeBoolRegisters(origCGBCode, boolRegs, floatRegs, numFloats);

	enf_assert(1 << numBools == m_iNumBranches);
	enf_assert(numFloats < 256 && numBools < 64);
	enf_assert(!m_pBranches[mask].m_uCode);

	ENF_CellCgbProgram* program = (ENF_CellCgbProgram*) origCGBCode;

	uchar* uCode   = (uchar*) program->ucode;
	uint uCodeSize = program->header->ucode_size;

#endif

	//set bool registers
	static const float swappedOne  = cellGcmSwap16Float32(1.0f);
	static const float swappedZero = cellGcmSwap16Float32(0.0f);

	for (uint i = 0; boolRegs[i].m_iRegister != 0xFFFF; i++)
	{
		float *pointer = (float*) (uCode + boolRegs[i].m_iOffset);

		uint maskBit = 1 << (boolRegs[i].m_iRegister - PS3BranchUCodeCache::BOOL_REGISTER_START);

		*pointer = mask & maskBit ? swappedOne : swappedZero;
	}

	//set float registers with unique numbers to find them later in updated code
	for (uint i = 0; i < numFloats; i++)
	{
		uint *pointer = (uint*) (uCode + floatRegs[i].m_iOffset);

		*pointer = GetUniqueNumber(floatRegs[i].m_iRegister);
	}

	//remove branches
	uchar* outUCode = ALLOCA16 (uchar, uCodeSize);
	uint outUCodeSize;

	int res = cellGcmCgStripBranchesFromFragmentUCode(uCode, uCodeSize, outUCode, &outUCodeSize);

	#ifdef PARSE_CGB
		LogF(LL_NORMAL, "Extracting branch -> mask %d -> stripping before %d, after %d, res %d", mask, uCodeSize, outUCodeSize, res);

		if (uCodeSize == outUCodeSize)
		{
			LogF(LL_NORMAL, "  This shader is suspicious, probably contains IF construction without BRANCH keyword");
		}
	#endif


	if (res == CELL_OK)
	{
		//copy updated ucode
		m_pBranches[mask].m_iSize = outUCodeSize;
		
		#ifdef ENF_FRAGMENT_CONSTANT_PATCHING
			m_pBranches[mask].m_pUCode = PHYS_ALLOC(outUCodeSize, 64, 0);			
		#else
			m_pBranches[mask].m_pUCode = LOCAL_ALLOC(outUCodeSize, 64);
		#endif

		uint ret = cellGcmAddressToOffset(m_pBranches[mask].m_pUCode, &m_pBranches[mask].m_iUCodeOffset);
		enf_assert(ret == CELL_OK);

		memcpy(m_pBranches[mask].m_pUCode, outUCode, outUCodeSize);

		//find reallocated registers
		FragRegister regs[256];
		uint regsNum = 0;

		uint* codeWord = (uint*) outUCode;

		for (uint i = 0; i < outUCodeSize >> 2; i++, codeWord++)
		{
			uint num = IsUniqueNumber(*codeWord);
			if (num != 0xFFFFFFFF)
			{
				regs[regsNum].m_iOffset		 = i << 2;
				regs[regsNum++].m_iRegister = num;
			}
		}
		
		if (regsNum)
		{
			m_pBranches[mask].m_iRegCount = regsNum;
			m_pBranches[mask].m_Registers = ENF_NEW FragRegister[regsNum]; 

			memcpy(m_pBranches[mask].m_Registers, regs, regsNum*sizeof(FragRegister));
		}
	}

	return m_pBranches + mask;
}


//-----------------------------------------------------------------------
PS3BranchUCodeCache::~PS3BranchUCodeCache()
{
	for (uint i = 0; i < m_iNumBranches; i++)
	{
		if (m_pBranches[i].m_pUCode)
		{
			#ifdef ENF_FRAGMENT_CONSTANT_PATCHING
				PHYS_FREE(m_pBranches[i].m_pUCode);
			#else
				LOCAL_FREE(m_pBranches[i].m_pUCode);
			#endif
		}

		SAFE_DELETE_ARRAY(m_pBranches[i].m_Registers);
	}
	SAFE_DELETE_ARRAY (m_pBranches);
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC void GCMSetVertexProgramConstants(uint index, uint numBatches, const ShaderVec* values)
{
	uint numFloats = numBatches << 4;

	gCellGcmCurrentContext->current[0] = CELL_GCM_METHOD_HEADER_VERTEX_CONSTANT_LOAD(1 + numFloats);
	gCellGcmCurrentContext->current[1] = CELL_GCM_METHOD_DATA_VERTEX_CONSTANT_LOAD(index);
	GCM_MEMCPY(&gCellGcmCurrentContext->current[2], values[index], sizeof(float)*numFloats);
	gCellGcmCurrentContext->current += numFloats + 2;
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC void GCMSetVertexProgramParameters(uint64& mask, const ShaderVec* values)
{
	while (mask)
	{
		uint64 firstBit = GetLowestBit64(mask);
		uint64 andMask = 1LL << firstBit;
		uint64 orMask = andMask;

		//can upload 8 vector4s at once, which is maximum for RSX
		andMask <<= 1LL;
		uint numBatches = 1;
		if (firstBit != 63 && (mask & andMask))
		{
			orMask |= andMask;
			numBatches = 2;
		}

		mask &= ~orMask;

		GCMSetVertexProgramConstants(firstBit << 2, numBatches, values);
	}
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC void GCMSetFragmentProgramParameters(uint64 mask, const ShaderVec* values, CellCgbProgram* program, uint progOffset)
{
	GCM_CALL(cellGcmSetTransferLocation)(gCellGcmCurrentContext, CELL_GCM_LOCATION_LOCAL);
	
	//enf_assert((mask & 0xfffffffffffffff0) == 0);
	uint param_count = cellCgbMapGetLength(program);
	for(uint j = 0; j < param_count; j++)
	{
		//which reg to set
		ushort reg;
		cellCgbMapGetFragmentUniformRegister(program, j, &reg);
		if(reg >= PS3BranchUCodeCache::BOOL_REGISTER_START)
		{
			continue;
		}
		
		//is that reg in mask
		uint start = reg >> 2;
		enf_assert(start < 64); 
		uint64 regmask = 1LL << start;

		if(regmask & mask)
		{
			uint offCount = 32;
			ushort parameterOff[32];
		
			cellCgbMapGetFragmentUniformOffsets(program, j, parameterOff, &offCount);

			float *dst, *src = (float*) values[reg];
			for (int k = 0; k < offCount; ++k) 
			{
				GCM_CALL(cellGcmSetInlineTransferPointer)(gCellGcmCurrentContext, progOffset + parameterOff[k], 4, (void**) &dst);

				//TODO: some vector instruction to swap it all together
				dst[0] = cellGcmSwap16Float32(src[0]);
				dst[1] = cellGcmSwap16Float32(src[1]);
				dst[2] = cellGcmSwap16Float32(src[2]);
				dst[3] = cellGcmSwap16Float32(src[3]);
			}
		}
	}
}

//-----------------------------------------------------------------------
ENF_INLINEFUNC void GCMSetFragmentProgramParameters(uint64 mask, const ShaderVec* values, const FragRegister* fragRegs, uint count, uint codeOffset, uint shaderLocation = CELL_GCM_LOCATION_LOCAL)
{
	if (!count)
		return; 

	GCM_CALL(cellGcmSetTransferLocation)(gCellGcmCurrentContext, shaderLocation);

	for(uint j = 0; j < count; j++)
	{
		uint reg = fragRegs[j].m_iRegister;

		uint start = reg >> 2;
		enf_assert(start < 64); 
		uint64 regMask = 1 << start;

		if(mask & regMask)
		{
			float *src = (float*) values[reg];

			float *dst;
			GCM_CALL(cellGcmSetInlineTransferPointer)(gCellGcmCurrentContext, codeOffset + fragRegs[j].m_iOffset, 4, (void**) &dst);

			//TODO: some vector instruction to swap it all together
			dst[0] = cellGcmSwap16Float32(src[0]);
			dst[1] = cellGcmSwap16Float32(src[1]);
			dst[2] = cellGcmSwap16Float32(src[2]);
			dst[3] = cellGcmSwap16Float32(src[3]);
		}
	}	
}

//-----------------------------------------------------------------------
ENF_INLINEFUNC void GCMSetFragmentProgramParametersPatch(uint64 mask, const ShaderVec* values, const FragRegister* fragRegs, uint count, void* origUCode, uint size)
{
	if (!count)
		return; 

	for (uint j = 0; j < count; j++)
	{
		uint reg = fragRegs[j].m_iRegister;

		uint start = reg >> 2;
		enf_assert(start < 64); 
		uint64 regMask = 1LL << start;

		float *src = (float*) values[reg];
		float *dst = (float*) ((uchar*) origUCode + fragRegs[j].m_iOffset);

		enf_assert((uint) dst - (uint) origUCode < size);

		dst[0] = cellGcmSwap16Float32(src[0]);
		dst[1] = cellGcmSwap16Float32(src[1]);
		dst[2] = cellGcmSwap16Float32(src[2]);
		dst[3] = cellGcmSwap16Float32(src[3]);
	}	
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC void GCMSetNullTexture(uint texUnit)
{
	gCellGcmCurrentContext->current[0] = CELL_GCM_METHOD_HEADER_TEXTURE_CONTROL0(texUnit, 1);
	gCellGcmCurrentContext->current[1] = 0;
	gCellGcmCurrentContext->current += 2;
}

//-----------------------------------------------------------------------
ENF_INLINEFUNC void GCMSetNullVertexTexture(uint texUnit)
{
	gCellGcmCurrentContext->current[0] = CELL_GCM_METHOD_HEADER_VERTEX_TEXTURE_CONTROL0(texUnit, 1);
	gCellGcmCurrentContext->current[1] = 0;
	gCellGcmCurrentContext->current += 2;
}

//-----------------------------------------------------------------------
ENF_INLINEFUNC void GCMSetTextureFilter(uint texUnit, SamplerFilterState fstate)
{	
	ushort bias = 0;
	
	#ifdef ENF_DEVELOPER
		bias = ((uint) ENF_DIAG_VALUE(DM_REND_MIPMAPBIAS));
		bias = bias < 13 ? bias << 8 : bias < 14 ? 0x1F00 : bias < 15 ? 0x1FC0 : bias < 16 ? 0x1F80 : 0x1F40;	//0..12, -1, -0.75, -0.5, -0.25
	#endif

	gCellGcmCurrentContext->current[0] = CELL_GCM_METHOD_HEADER_TEXTURE_FILTER(texUnit, 1);
	gCellGcmCurrentContext->current[1] = fstate.GetGCMFilterStatePS(bias);
	gCellGcmCurrentContext->current += 2;
}

//-----------------------------------------------------------------------
ENF_INLINEFUNC void GCMSetTextureAddress(uint texUnit, const SamplerAddressState& astate)
{
	gCellGcmCurrentContext->current[0] = CELL_GCM_METHOD_HEADER_TEXTURE_ADDRESS(texUnit, 1);
	gCellGcmCurrentContext->current[1] = astate.GetGCMAddressStatePS(); 			
	gCellGcmCurrentContext->current += 2;

	if(astate.Flags & 2)
	{
		gCellGcmCurrentContext->current[0] = CELL_GCM_METHOD_HEADER_TEXTURE_BORDER_COLOR(texUnit, 1);
		gCellGcmCurrentContext->current[1] = CELL_GCM_METHOD_DATA_TEXTURE_BORDER_COLOR(astate.BorderColor);
		gCellGcmCurrentContext->current += 2;
	}
}

//-----------------------------------------------------------------------
ENF_INLINEFUNC void GCMVertexProgramLoad(const CellCgbVertexProgramConfiguration *conf, const void *ucode)
{
	CellGcmContextData* thisContext = gCellGcmCurrentContext;
	 
	const uint *rawData = (const uint*)ucode;
	uint instCount = conf->instructionCount;
	uint instStart = conf->instructionSlot;

	// download program to slots [instIndex,instIndex+len-1]
	thisContext->current[0] = CELL_GCM_METHOD_HEADER_VERTEX_PROGRAM_LOAD(2);
	thisContext->current[1] = CELL_GCM_METHOD_DATA_VERTEX_PROGRAM_LOAD(instStart);
	thisContext->current[2] = CELL_GCM_METHOD_DATA_VERTEX_PROGRAM_START(instStart);
	thisContext->current += 3;

	// upload the ucode in the instruction registers, each instruction takes 128 bits
	const uint maxSlots = 32;

	uint loadPasses = instCount;
	uint uintsSlots = instCount*4;
	
	uint batches = uintsSlots/maxSlots;
	uint rest    = uintsSlots - batches*maxSlots; 

	for (uint i = 0; i < batches; i++, rawData += maxSlots)
	{
		thisContext->current[0] = CELL_GCM_METHOD_HEADER_VERTEX_PROGRAM(maxSlots);
		GCM_MEMCPY(&thisContext->current[1], &rawData[0], sizeof(uint)*maxSlots);

		thisContext->current += (1 + maxSlots);
	}

	if (rest)
	{
		thisContext->current[0] = CELL_GCM_METHOD_HEADER_VERTEX_PROGRAM(rest);
		GCM_MEMCPY(&thisContext->current[1], &rawData[0], sizeof(uint)*rest);

		thisContext->current += (1 + rest);
	}

	//set the attribute input mask
	CELL_GCM_METHOD_SET_VERTEX_ATTRIB_INPUT_MASK(thisContext->current, (conf->attributeInputMask));

	// how many registers does our vshader use ?
	thisContext->current[0] = CELL_GCM_METHOD_HEADER_VERTEX_TIMEOUT(1);
	thisContext->current[1] = conf->registerCount <= 32 ? CELL_GCM_METHOD_DATA_VERTEX_TIMEOUT(0xFFFF,32) : thisContext->current[1] = CELL_GCM_METHOD_DATA_VERTEX_TIMEOUT(0xFFFF,48);
	thisContext->current += 2;
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC bool IsFloatingTextureFilterStateCorrect(uint format, SamplerFilterState fstate)
{
	format = format & ~(CELL_GCM_TEXTURE_LN | CELL_GCM_TEXTURE_UN);

	if(format >= CELL_GCM_TEXTURE_W16_Z16_Y16_X16_FLOAT && format <= CELL_GCM_TEXTURE_X32_FLOAT || format == CELL_GCM_TEXTURE_Y16_X16_FLOAT)  
	{
		return (fstate.MinFilter == CELL_GCM_TEXTURE_NEAREST || fstate.MinFilter == CELL_GCM_TEXTURE_NEAREST_NEAREST) && fstate.MagFilter == CELL_GCM_TEXTURE_NEAREST && fstate.MaxAniso_MipLODBias == 0;
	}

	return true;
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC bool IsVertexTextureFormatCorrect(uint format)
{
	format = format & ~(CELL_GCM_TEXTURE_LN | CELL_GCM_TEXTURE_UN);

	return 
		format == CELL_GCM_TEXTURE_W32_Z32_Y32_X32_FLOAT || 
		format == CELL_GCM_TEXTURE_X32_FLOAT;
}



volatile CellGcmContextData* currContext;
volatile bool enableAK = false;


//-----------------------------------------------------------------------
void Renderer::CommitStates(const void* userPointer, uint vertexOffset, uint stride)
{
	ENF_PROFILE(PROFILE_COMMITSTATE);

	#define BEGIN_MASK_LOOP(mask) while (mask) { uint bit = GetLowestBit(mask); uint andBit = (1 << bit); uint remBit = ~andBit; mask &= remBit;
	#define END_MASK_LOOP } 

	currContext = gCellGcmCurrentContext;
	const uint cmdReserve = 12*1024;
	CheckCommandBuffer(cmdReserve);
	
	{
	ENF_PROFILE(PROFILE_FILLCB);

	#ifdef ENF_DEVELOPER
		uint *beginBuff = gCellGcmCurrentContext->current;
	#endif

	if(m_iCommitMask & GCMCM_INVALIDATEVXBUF)
	{
		GCM_CALL(cellGcmSetInvalidateVertexCache)(gCellGcmCurrentContext);
	}

	if(m_iCommitMask & GCMCM_VERTEXSHADER)
	{
		///get shader configuration
		CellCgbVertexProgramConfiguration shaderConf;
		int confRet = cellCgbGetVertexConfiguration(m_pVShader, &shaderConf);
		enf_assert( confRet == CELL_CGB_OK );
		
		const void* ucode = cellCgbGetUCode(m_pVShader);
		enf_assert(ucode);

		//load shader
		GCMVertexProgramLoad(&shaderConf, ucode);

		//load default values
		uint count = cellCgbGetVertexConstantCount(m_pVShader);

		//load values -> this coud be precomputed after load into tables to load more registers at once, but
		//the format is not good -> e.g one shader with 4 count has reg = [467, 466, 465, 464] but the values
		//addresses are [0x3B159C90, 0x3B159CA0, 0x3B159CB0, 0x3B159CC0]
		for (uint i = 0; i<count; ++i)
		{
			const float *value = NULL;
			ushort reg = 0;

			//get the constant value and the register it needs to go to
			cellCgbGetVertexConstantValues(m_pVShader,i,&reg,&value);

			//set the register to the value
			gCellGcmCurrentContext->current[0] = CELL_GCM_METHOD_HEADER_VERTEX_CONSTANT_LOAD(5);	
			gCellGcmCurrentContext->current[1] = CELL_GCM_METHOD_DATA_VERTEX_CONSTANT_LOAD(reg);

			GCM_MEMCPY(&gCellGcmCurrentContext->current[2], value, sizeof(float)*4);
			gCellGcmCurrentContext->current += 6;
		}

		m_iCommitMask |= GCMCM_VERTEXPARAM;
	}

	if	(m_iCommitMask & GCMCM_VERTEXPARAM)
	{
		GCMSetVertexProgramParameters(m_VSConstChanged, m_VSConstants);
	}


	if(m_iCommitMask & GCMCM_PIXELSHADER)
	{
		m_pActiveBranch = NULL;
		uint location = CELL_GCM_LOCATION_LOCAL;
		GCM_CALL(cellGcmSetInvalidateTextureCache)(gCellGcmCurrentContext, CELL_GCM_INVALIDATE_TEXTURE);

		//do patching
		if (m_pFShader->HasBranches())
		{
			const PS3BranchUCodeCache::BranchCode* branch = m_pFShader->GetBranch(m_iPSConstantsB);

			if (branch)
			{
				m_pActiveBranch = branch;

				#ifdef ENF_FRAGMENT_CONSTANT_PATCHING
					if (m_pShaderPatchBuffer->HasEnoughSpace(branch->m_iSize))
					{
						GCMSetFragmentProgramParametersPatch(m_PSConstChanged, m_PSConstants, branch->m_Registers, branch->m_iRegCount, branch->m_pUCode, branch->m_iSize);
						m_ActiveFShaderUCode = m_pShaderPatchBuffer->AllocAndCopy(branch->m_pUCode, branch->m_iSize);			
					}
					else
					{
						location = CELL_GCM_LOCATION_MAIN;
						m_ActiveFShaderUCode = branch->m_iUCodeOffset;
						GCMSetFragmentProgramParameters(m_PSConstChanged, m_PSConstants, branch->m_Registers, branch->m_iRegCount, m_ActiveFShaderUCode, CELL_GCM_LOCATION_MAIN);
					}
				#else
					m_ActiveFShaderUCode = branch->m_iUCodeOffset;
					GCMSetFragmentProgramParameters(m_PSConstChanged, m_PSConstants, branch->m_Registers, branch->m_iRegCount, m_ActiveFShaderUCode);
				#endif
			}
			else
			{
				LogF(LL_ERROR, "Cannot compile branch variant of pixel shader");
			}
		}
		
		if (!m_pActiveBranch)
		{
			#ifdef ENF_GCM_FRAGMENT_SHADER_FORMAT
				#ifdef ENF_FRAGMENT_CONSTANT_PATCHING
					if (m_pShaderPatchBuffer->HasEnoughSpace(m_pFShader->m_iUCodeSize))
					{
						GCMSetFragmentProgramParametersPatch(m_PSConstChanged, m_PSConstants, m_pFShader->m_pRegisters, m_pFShader->m_iFloatRegCount, m_pFShader->m_pUCode, m_pFShader->m_iUCodeSize);
						m_ActiveFShaderUCode = m_pShaderPatchBuffer->AllocAndCopy(m_pFShader->m_pUCode, m_pFShader->m_iUCodeSize);
					}
					else
					{
						location = CELL_GCM_LOCATION_MAIN;
						m_ActiveFShaderUCode = m_pFShader->m_ShaderConf.offset;
						GCMSetFragmentProgramParameters(m_PSConstChanged, m_PSConstants, m_pFShader->m_pRegisters, m_pFShader->m_iFloatRegCount, m_ActiveFShaderUCode, CELL_GCM_LOCATION_MAIN);
					}
				#else
					m_ActiveFShaderUCode = m_pFShader->m_ShaderConf.offset;
					GCMSetFragmentProgramParameters(m_PSConstChanged, m_PSConstants, m_pFShader->m_pRegisters, m_pFShader->m_iFloatRegCount, m_ActiveFShaderUCode);
				#endif
			#else
				m_ActiveFShaderUCode = m_pFShader->m_ShaderOffset;
				GCMSetFragmentProgramParameters(m_PSConstChanged, m_PSConstants, m_pFShader->m_pShader, m_ActiveFShaderUCode);
			#endif
		}

		CellCgbFragmentProgramConfiguration shaderConf;
		#ifdef ENF_GCM_FRAGMENT_SHADER_FORMAT
			shaderConf = m_pFShader->m_ShaderConf;
		#else
			cellCgbGetFragmentConfiguration(m_pFShader->m_pShader, &shaderConf);

			//disable back color mask, which are always enabled when COLOR01is used -> not neccessary but saves performance
			shaderConf.attributeInputMask &= ~(CELL_GCM_ATTRIB_OUTPUT_MASK_BACKDIFFUSE | CELL_GCM_ATTRIB_OUTPUT_MASK_BACKSPECULAR);
		#endif

		if (m_iRenderStateValue[RS_POINTSPRITEENABLE])
		{
			shaderConf.attributeInputMask |= CELL_GCM_ATTRIB_OUTPUT_MASK_POINTSIZE;
		}

		shaderConf.offset = m_ActiveFShaderUCode;
		GCM_CALL(cellGcmSetFragmentProgramLoadLocation)(gCellGcmCurrentContext, &shaderConf, location);

		m_PSConstChanged = 0;
		m_iPSConstantsB  = 0;
	}
	else if(m_PSConstChanged)
	{
		GCM_CALL(cellGcmSetInvalidateTextureCache)(gCellGcmCurrentContext, CELL_GCM_INVALIDATE_TEXTURE);
		uint location = CELL_GCM_LOCATION_LOCAL;

		if (m_pActiveBranch)
		{
			const PS3BranchUCodeCache::BranchCode* branch = (const PS3BranchUCodeCache::BranchCode*) m_pActiveBranch;

			#ifdef ENF_FRAGMENT_CONSTANT_PATCHING
				if (m_pShaderPatchBuffer->HasEnoughSpace(branch->m_iSize))
				{
					GCMSetFragmentProgramParametersPatch(m_PSConstChanged, m_PSConstants, branch->m_Registers, branch->m_iRegCount, branch->m_pUCode, branch->m_iSize);
					m_ActiveFShaderUCode = m_pShaderPatchBuffer->AllocAndCopy(branch->m_pUCode, branch->m_iSize);
				}
				else
				{
					location = CELL_GCM_LOCATION_MAIN;
					m_ActiveFShaderUCode = branch->m_iUCodeOffset;
					GCMSetFragmentProgramParameters(m_PSConstChanged, m_PSConstants, branch->m_Registers, branch->m_iRegCount, m_ActiveFShaderUCode, CELL_GCM_LOCATION_MAIN);
				}
			#else
				GCMSetFragmentProgramParameters(m_PSConstChanged, m_PSConstants, branch->m_Registers, branch->m_iRegCount, m_ActiveFShaderUCode);
			#endif
		}
		else
		{
			#ifdef ENF_GCM_FRAGMENT_SHADER_FORMAT
				#ifdef ENF_FRAGMENT_CONSTANT_PATCHING
					if (m_pShaderPatchBuffer->HasEnoughSpace(m_pFShader->m_iUCodeSize))
					{
						GCMSetFragmentProgramParametersPatch(m_PSConstChanged, m_PSConstants, m_pFShader->m_pRegisters, m_pFShader->m_iFloatRegCount, m_pFShader->m_pUCode, m_pFShader->m_iUCodeSize);
						m_ActiveFShaderUCode = m_pShaderPatchBuffer->AllocAndCopy(m_pFShader->m_pUCode, m_pFShader->m_iUCodeSize);
					}
					else
					{
						location = CELL_GCM_LOCATION_MAIN;
						m_ActiveFShaderUCode = m_pFShader->m_ShaderConf.offset;
						GCMSetFragmentProgramParameters(m_PSConstChanged, m_PSConstants, m_pFShader->m_pRegisters, m_pFShader->m_iFloatRegCount, m_ActiveFShaderUCode, CELL_GCM_LOCATION_MAIN);
					}
				#else
					GCMSetFragmentProgramParameters(m_PSConstChanged, m_PSConstants, m_pFShader->m_pRegisters, m_pFShader->m_iFloatRegCount, m_ActiveFShaderUCode);
				#endif
			#else
				GCMSetFragmentProgramParameters(m_PSConstChanged, m_PSConstants, m_pFShader->m_pShader, m_ActiveFShaderUCode);
			#endif
		}
		
		GCM_CALL(cellGcmSetUpdateFragmentProgramParameterLocation)(gCellGcmCurrentContext, m_ActiveFShaderUCode, location);
		m_PSConstChanged = 0;
	}

	
	if(m_iCommitMask & GCMCM_VSB)
	{
		GCM_CALL(cellGcmSetTransformBranchBits)(gCellGcmCurrentContext, m_iVSConstantsB);
	}


	uint mask = m_iVSamplerAddressMask;
	BEGIN_MASK_LOOP(mask)
		if (!m_pVertexTextures[bit])
			continue;

		const SamplerAddressState& astate = m_SamplerAddressStates[(uint)m_iSamplerAddressStateValue2[bit]];
		enf_assert2((uint)m_iSamplerAddressStateValue2[bit] != 0xFFFFFFFF, "Invalid vertex sampler address state");

		gCellGcmCurrentContext->current[0] = CELL_GCM_METHOD_HEADER_VERTEX_TEXTURE_ADDRESS(bit, 1);
		gCellGcmCurrentContext->current[1] = astate.GetGCMAddressStateVS();
		gCellGcmCurrentContext->current += 2;

		m_iVSamplerAddressMask &= remBit;
	END_MASK_LOOP


	BEGIN_MASK_LOOP(m_iVTextureMask)
		CellGcmTexture *tex = m_pVertexTextures[bit] ? m_pVertexTextures[bit]->GetGCMTexture() : NULL;

		if(tex)
		{
			#ifdef ENF_DEVELOPER
				SamplerFilterState fstate = SamplerFilterState((uint) m_iSamplerFilterStateValue2[bit]);
				enf_assert2((uint) m_iSamplerFilterStateValue2[bit] != 0xffffffff, "Invalid filter state");
				
				enf_assert2(IsFloatingTextureFilterStateCorrect(tex->format, fstate), "Float texture has bad sampler filter state");
				enf_assert2(IsVertexTextureFormatCorrect(tex->format), "Vertex texture format must be float4 or float");
			#endif

			GCM_CALL(cellGcmSetVertexTexture)(gCellGcmCurrentContext, bit, tex);
			GCM_CALL(cellGcmSetVertexTextureControl)(gCellGcmCurrentContext, bit, CELL_GCM_TRUE, 0, (tex->mipmap)<<8);
		}
		else
		{
			GCMSetNullVertexTexture(bit);
		}
	END_MASK_LOOP


	mask = m_iSamplerFilterMask; 
	BEGIN_MASK_LOOP(mask) 	//if(m_iCommitMask & GCMCM_SAMPLERFILTER)
		if (!m_pTextures[bit])
			continue;

		SamplerFilterState fstate = SamplerFilterState((uint) m_iSamplerFilterStateValue[bit]);
		enf_assert2((uint) m_iSamplerFilterStateValue[bit] != 0xffffffff, "Invalid filter state");

		//floating texture was set sometimes previously in different material class 
		//but the sampler state was set here to some other state than 
		//floating filter format -> this is invalid for RSX, we have to check it.
		CellGcmTexture *tex = m_pTextures[bit]->GetGCMTexture();

		if (tex && !IsFloatingTextureFilterStateCorrect(tex->format, fstate))
		{
				//LogF(LL_ERROR, "Invalid texture filter (sampler %d)",bit);
			continue;
		}

		m_iSamplerFilterMask &= remBit;
		GCMSetTextureFilter(bit, fstate);
	END_MASK_LOOP


	mask = m_iSamplerAddressMask;
	BEGIN_MASK_LOOP(mask)
		if (!m_pTextures[bit])
			continue;

		enf_assert2((uint) m_iSamplerAddressStateValue[bit] != 0xffffffff, "Invalid sampler address state");
		GCMSetTextureAddress(bit, m_SamplerAddressStates[(uint) m_iSamplerAddressStateValue[bit]]);

		m_iSamplerAddressMask &= remBit;
	END_MASK_LOOP


	BEGIN_MASK_LOOP(m_iTextureMask) //if(m_iCommitMask & GCMCM_TEXTURES)
		CellGcmTexture *tex = m_pTextures[bit] ? m_pTextures[bit]->GetGCMTexture() : NULL;
			
		if(tex)
		{
			SamplerFilterState fstate = SamplerFilterState((uint) m_iSamplerFilterStateValue[bit]);
			enf_assert2((uint) m_iSamplerFilterStateValue[bit] != 0xffffffff, "Invalid filter state");

			ushort minLOD = 0;
			#ifdef ENF_DEVELOPER
				minLOD = ((uint) ENF_DIAG_VALUE(DM_REND_MAXMIPMAP)) << 8;

				//test for swizzled textures
				if (tex->format & CELL_GCM_TEXTURE_SZ)
				{
					enf_assert2(IsPowerOfTwo(tex->width) && IsPowerOfTwo(tex->height), "Texture is swizzled but is not power 2");
				}
			#endif


			GCM_CALL(cellGcmSetTexture)(gCellGcmCurrentContext, bit, tex);

			if (enableAK)
			{
				GCM_CALL(cellGcmSetTextureControlAlphaKill)(gCellGcmCurrentContext, bit, CELL_GCM_TRUE, minLOD, tex->mipmap << 8, fstate.MaxAniso_MipLODBias, m_iAlphaKillMask & andBit); 
			}
			else
			{
				GCM_CALL(cellGcmSetTextureControlAlphaKill)(gCellGcmCurrentContext, bit, CELL_GCM_TRUE, minLOD, tex->mipmap << 8, fstate.MaxAniso_MipLODBias, 0); 
			}
		}
		else
		{
			GCMSetNullTexture(bit);
		}
	END_MASK_LOOP


	enf_assert(m_iVertexDeclaration != 0);
	if (m_iCommitMask & (GCMCM_VERTEXDECLARATION|GLCM_STREAMS) || userPointer != NULL)
	{
		uint divider = 0, currAttribs = 0;
		
		#ifdef ENF_DEVELOPER
			int previousUsage = -1;
		#endif

		const VertexDeclaration& vd = *m_pVertexDeclarations[m_iVertexDeclaration - 1];
		for(uint n = 0; VertexDeclaration::MAX_DECLARATION; n++)
		{
			uint stream = vd.Declaration[n].Stream;

			if(stream == DECL_END)
				break;

			enf_assert(vd.Declaration[n].GCMStruct.GCMVertexDecl != 0xFFFFFFFF);

			uint offset = vd.Declaration[n].Offset;
			const VertexDeclaration::VertexElement::GCMStructType desc = vd.Declaration[n].GCMStruct;

			uint dataOffset, streamStride, freq = 0, location = CELL_GCM_LOCATION_MAIN;
			if(userPointer)
			{
				if (userPointer == DISABLE_VERTEX_OFFSET)
				{
					dataOffset = 0;
					location = CELL_GCM_LOCATION_LOCAL;
					streamStride = 0;

					#ifdef ENF_DEVELOPER
						//when rendering using begin and end, the usage must be increasing from ATTR0 to ATTR15
						enf_assert((int) desc.Usage > previousUsage);

						previousUsage = (int) desc.Usage;
					#endif
				}
				else
				{
					streamStride = stride;
					dataOffset = MemoryManager::PhysicalOffsetFromPtr((uchar*) userPointer + offset);
				}
			}
			else
			{
				location = m_Streams[stream].Location;
				streamStride = m_Streams[stream].Stride;

				enf_assert(m_Streams[stream].VB);
				uint startOffset = location == CELL_GCM_LOCATION_LOCAL ?
					MemoryManager::LocalOffsetFromPtr(m_Streams[stream].VB) :
					MemoryManager::PhysicalOffsetFromPtr(m_Streams[stream].VB);

				dataOffset = startOffset + m_Streams[stream].Offset + offset + streamStride * vertexOffset;
				
				#ifdef ENF_HWINSTANCING
					freq = m_Streams[stream].Freq;

					if(m_Streams[stream].Op == SFO_MODULO)
						divider |= (CELL_GCM_FREQUENCY_MODULO << desc.Usage);
							

					/*
					not needed, as CELL_GCM_FREQUENCY_DIVIDE is 0, thus this is default behavior

					if(m_Streams[stream].Op == SFO_DIVIDE)
						divider |= (CELL_GCM_FREQUENCY_DIVIDE << desc.Usage);		
					*/
				#endif


			}

			//enf_assert(freq == 0);
			enf_assert(desc.ElemsCount > 0);
			GCM_CALL(cellGcmSetVertexDataArray)(gCellGcmCurrentContext, desc.Usage, freq, streamStride, desc.ElemsCount, desc.Type, location, dataOffset);
			
			currAttribs |= 1 << desc.Usage;
		}
		
		#ifdef ENF_HWINSTANCING
			if (m_iOldFrequencyDivider != divider)
			{
				GCM_CALL(cellGcmSetFrequencyDividerOperation)(gCellGcmCurrentContext, divider);
				m_iOldFrequencyDivider = divider;
			}
		#endif
		
		//clear currently not used data in vertex streams
		uint clearAttribs = m_iOldAttribMask & ~currAttribs;

		while (clearAttribs)
		{
			uint bit = GetLowestBit(clearAttribs);
			GCM_CALL(cellGcmSetVertexDataArray)(gCellGcmCurrentContext, bit, 0, 0, 0, CELL_GCM_VERTEX_F, 0, 0);

			clearAttribs &= ~(1 << bit);
		}

		m_iOldAttribMask = currAttribs;
	}
	
	
	if(m_iCommitMask & GCMCM_ALPHATEST)
	{
		if(m_iRenderStateValue[RS_ALPHATESTENABLE] && m_bEnableAlphaTest)
		{
			GCM_CALL(cellGcmSetAlphaTestEnable)(gCellGcmCurrentContext, CELL_GCM_TRUE);
			GCM_CALL(cellGcmSetAlphaFunc)(gCellGcmCurrentContext, GCMFunc((RenderCmpFunc)m_iRenderStateValue[RS_ALPHAFUNC]), m_iRenderStateValue[RS_ALPHAREF]);
		}
		else
		{
			GCM_CALL(cellGcmSetAlphaTestEnable)(gCellGcmCurrentContext, CELL_GCM_FALSE);
		}
	}

	m_iCommitMask = 0;

	#undef BEGIN_MASK_LOOP
	#undef END_MASK_LOOP

	#ifdef ENF_DEVELOPER
		if (!IRenderer::CheckCommandBufferAddress(gCellGcmCurrentContext))
		{
			LogF(LL_DEBUG, "CB is out of bounds");
		}
	#endif

	}

}

//-----------------------------------------------------------------------
void Renderer::DrawIndexedPrimitive(PrimitiveType type, int baseVertexIndex, uint minIndex, uint numVertices, uint startIndex, uint primitiveCount)
{
	#ifdef ENF_DEVELOPER
		m_Stats.AddIndexedPrimitive(numVertices, primitiveCount);
	#endif

	enf_assert(m_pIndexBuffer);

	if(baseVertexIndex != m_iPrevBaseVertexIndex)
	{
		m_iCommitMask |= GLCM_STREAMS;
	}
	m_iPrevBaseVertexIndex = baseVertexIndex;

	if (m_iCommitMask)
		CommitStates(NULL, baseVertexIndex, 0);

	uint mode   = GetPrimitiveType(type);
	uint count = GetPrimitivesCount(type, primitiveCount);

	uint ibFormat	= (m_pIndexBuffer->GetFormat() == IBFMT_INDEX16) ? CELL_GCM_DRAW_INDEX_ARRAY_TYPE_16 : CELL_GCM_DRAW_INDEX_ARRAY_TYPE_32;
	uint shift		= startIndex << ((ibFormat == CELL_GCM_DRAW_INDEX_ARRAY_TYPE_16) ? 1 : 2);
	uint location	= m_pIndexBuffer->GetBufferLocation();
	uint offset		= m_pIndexBuffer->GetBufferOffset();
	
	GCM_CALL(cellGcmSetDrawIndexArray)(gCellGcmCurrentContext, mode, count, ibFormat, location, offset + shift); 
	GCM_CALL(cellGcmFlush)(gCellGcmCurrentContext);
}


#elif defined(ENF_OGL)


//-----------------------------------------------------------------------
ENF_INLINE void GLSetTextureFilter(GLenum target, SamplerFilterState fstate)
{
	if(fstate.MipFilter == TEXF_NONE)
	{
		switch(fstate.MinFilter)
		{
		case TEXF_POINT:
			GLCALL(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST));
			break;
		case TEXF_LINEAR:
			GLCALL(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR));
			break;
		}
		switch(fstate.MagFilter)
		{
		case TEXF_POINT:
			GLCALL(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
			break;
		case TEXF_LINEAR:
			GLCALL(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
			break;
		case TEXF_ANISOTROPIC:
			GLCALL(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
			GLCALL(glTexParameteri(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1));
			break;
		}
	}
	else if(fstate.MipFilter == TEXF_LINEAR)
	{
		switch(fstate.MinFilter)
		{
		case TEXF_POINT:
			GLCALL(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
			break;
		case TEXF_LINEAR:
			GLCALL(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
			break;
		}
		switch(fstate.MagFilter)
		{
		case TEXF_POINT:
			GLCALL(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
			GLCALL(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
			break;
		case TEXF_LINEAR:
			GLCALL(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
			GLCALL(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
			break;
		case TEXF_ANISOTROPIC:
			GLCALL(glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
			GLCALL(glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
			GLCALL(glTexParameteri(target, GL_TEXTURE_MAX_ANISOTROPY_EXT, fstate.MaxAniso));
			break;
		}
	}
}

//-----------------------------------------------------------------------
ENF_INLINE void GLSetTextureAddress(GLenum texTarget, GLenum str, GLint state)
{
	switch(state)
	{
	case TADDRESS_WRAP:
		GLCALL(glTexParameteri(texTarget, str, GL_REPEAT));
		break;
	case TADDRESS_MIRROR:
		GLCALL(glTexParameteri(texTarget, str, GL_MIRRORED_REPEAT));
		break;
	case TADDRESS_CLAMP:
		GLCALL(glTexParameteri(texTarget, str, GL_CLAMP));
		break;
	case TADDRESS_BORDER:
//		GLCALL(glTexParameteri(texTarget, str, GL_CLAMP_TO_BORDER));
		GLCALL(glTexParameteri(texTarget, str, GL_CLAMP_TO_EDGE));
		break;

	case TADDRESS_MIRRORONCE:
		GLCALL(glTexParameteri(texTarget, str, GL_MIRROR_CLAMP_EXT));
		break;
	}
}

//-----------------------------------------------------------------------
ENF_INLINE void GLSetTextureAddress(GLenum texTarget, const SamplerAddressState& astate)
{
	GLSetTextureAddress(texTarget, GL_TEXTURE_WRAP_S, astate.UAddress);
	GLSetTextureAddress(texTarget, GL_TEXTURE_WRAP_T, astate.VAddress);
	if(astate.Flags & 1)
		GLSetTextureAddress(texTarget, GL_TEXTURE_WRAP_R, astate.WAddress);
	if(astate.Flags & 2)
	{
		float color[4];
		color[0] = (float)((astate.BorderColor >> 16) & 0xff) / 255.0f;
		color[1] = (float)((astate.BorderColor >> 8) & 0xff) / 255.0f;
		color[2] = (float)(astate.BorderColor & 0xff) / 255.0f;
		color[3] = (float)((astate.BorderColor >> 24) & 0xff) / 255.0f;
		GLCALL(glTexParameterfv(texTarget, GL_TEXTURE_BORDER_COLOR, color));
	}
}

ENF_INLINE void GLSetProgramParameters(const GLenum target, uint64 mask, const ShaderVec* values)
{
	for(uint i = 0; mask && i < 2; i++, mask >>= 32)
	{
		uint mask32 = (uint)mask;
		for(uint b = 0; mask32 && b < 8; b++, mask32 >>= 8)
		{
			uint mask8 = mask32 & 0xff;
			uint baseIndex = (b * 8 + i * 32) * 4;
			for(uint n = 0; mask8 && n < 8; n++, mask8 >>= 1)
			{
				if(mask8 & 1)
				{
					uint index = n * 4 + baseIndex;
//					GLCALL(glProgramUniform4fv(target, index, 4, values[index]));
					GLCALL(glProgramLocalParameter4fvARB(target, index + 0, values[(index + 0)]));
					GLCALL(glProgramLocalParameter4fvARB(target, index + 1, values[(index + 1)]));
					GLCALL(glProgramLocalParameter4fvARB(target, index + 2, values[(index + 2)]));
					GLCALL(glProgramLocalParameter4fvARB(target, index + 3, values[(index + 3)]));
				}
			}
		}
	}
}

//-----------------------------------------------------------------------
void Renderer::CommitStates(uint cm, const void* userPointer, uint vertexOffset, uint stride)
{
	//m_VSConstChanged = 0xffffffff;
	//m_PSConstChanged = 0xffffffff;
	if(cm & (GLCM_TEXTURESV|GLCM_SAMPLERADDRESSV|GLCM_SAMPLERFILTERV))
	{
		for(uint n = 0; n < MAX_VERTEX_TEXTURES; n++)
		{
			if(cm & GLCM_TEXTURESV)
			{
				Texture* tex = m_pVertexTextures[n];
				if(tex != m_pShadowVertexTextures[n])
				{
					m_pShadowVertexTextures[n] = tex;

					if(tex == NULL)
					{
						GLCALL(glActiveTexture(GL_TEXTURE0 + 16 + n));
						GLCALL(glBindTexture(GL_TEXTURE_2D, 0));
					}
					else
					{
/*						cm |= GLCM_TEXTURES|GLCM_SAMPLERFILTER|GLCM_SAMPLERADDRESS;
						glActiveTexture(GL_TEXTURE0);
						glBindTexture(GL_TEXTURE_2D, tex->GetGLTexture());
						glSetTextureFilter(GL_TEXTURE_2D, fstate);
						glSetTextureAddress(GL_TEXTURE_2D, fstate);
*/
						GLCALL(glActiveTexture(GL_TEXTURE0 + 16 + n));
						glBindTexture(GL_TEXTURE_2D, 0);
/*
						uint fstateIndex = (uint)m_iSamplerFilterStateValue2[n];
						if(fstateIndex != 0xffffffff)
						{
							SamplerFilterState fstate = SamplerFilterState(fstateIndex);
							glSetTextureFilter(texTarget, fstate);
						}

						uint astateIndex = (uint)m_iSamplerAddressStateValue2[n];
						if(astateIndex != 0xffffffff)
						{
							const SamplerAddressState& astate = m_SamplerAddressStates[astateIndex];
							glSetTextureAddress(texTarget, astate);
						}*/
					}
				}
			}
		}
	}
	if(cm & GLCM_VERTEXSHADER)
	{
		//GLCALL(glBindProgramARB(GL_VERTEX_PROGRAM_ARB, cgGLGetProgramID(m_iVShader)));
		cgGLBindProgram(m_iVShader);

		GLSetProgramParameters(GL_VERTEX_PROGRAM_ARB, m_VSConstChanged, m_VSConstants);
		m_VSConstChanged = 0;
	}


	if(cm & GLCM_VSB)
	{

	}

	if(cm & (GLCM_TEXTURES|GLCM_SAMPLERADDRESS|GLCM_SAMPLERFILTER))
	{
		for(uint n = 0; n < MAX_TEXTURES; n++)
		{
			GLCALL(glActiveTexture(GL_TEXTURE0 + n));
			//if(cm & GLCM_TEXTURES)
			{
				Texture* tex = m_pTextures[n];
				m_pShadowTextures[n] = tex;

				if(tex == NULL)
				{
					GLCALL(glBindTexture(GL_TEXTURE_2D, 0));
					GLCALL(glBindTexture(GL_TEXTURE_CUBE_MAP, 0));
				}
				else
				{
					GLenum texTarget = GL_TEXTURE_2D;
					if(tex && tex->IsCube())
						texTarget = GL_TEXTURE_CUBE_MAP;

					GLCALL(glBindTexture(texTarget, tex->GetGLTexture()));

					uint fstateIndex = (uint)m_iSamplerFilterStateValue[n];
					if(fstateIndex != 0xffffffff)
					{
						SamplerFilterState fstate = SamplerFilterState(fstateIndex);
						GLSetTextureFilter(texTarget, fstate);
					}

					uint astateIndex = (uint)m_iSamplerAddressStateValue[n];
					if(astateIndex != 0xffffffff)
					{
						const SamplerAddressState& astate = m_SamplerAddressStates[astateIndex];
						GLSetTextureAddress(texTarget, astate);
					}
				}
			}
		}
		GLCALL(glActiveTexture(GL_TEXTURE0));
	}


	enf_assert(m_iVertexDeclaration != 0);

	if(cm & (GLCM_VERTEXDECLARATION|GLCM_STREAMS) || userPointer != NULL)
	{
		const VertexDeclaration& vd = *m_pVertexDeclarations[m_iVertexDeclaration - 1];

		GLint boundVB = 0;

		if(userPointer != NULL)
			GLCALL(glBindBuffer(GL_ARRAY_BUFFER, 0));

		uint attribMask = 0xffff;
		for(uint n = 0; VertexDeclaration::MAX_DECLARATION; n++)
		{
			const VertexDeclaration::VertexElement& elem = vd.Declaration[n];
			uint stream = elem.Stream;
			if(stream == DECL_END)
				break;

			if(userPointer == NULL)
			{
				GLint vbId = m_Streams[stream].VB;

				enf_assert(vbId);
				if(vbId != boundVB)
				{
					GLCALL(glBindBuffer(GL_ARRAY_BUFFER, vbId));
					boundVB = vbId;
				}
			}

			uint index;
			uint fsize;
			GLenum type;
			uint elemSizeof;
			GLboolean normalize = false;
			switch(elem.Type)
			{
			case VSDT_FLOAT1:
			case VSDT_FLOAT2:
			case VSDT_FLOAT3:
			case VSDT_FLOAT4:
				fsize = elem.Type - VSDT_FLOAT1 + 1;
				type = GL_FLOAT;
				elemSizeof = 4 * fsize;
				break;

			case VSDT_COLOR:	// 4D packed unsigned bytes mapped to 0. to 1. range
			case VSDT_UBYTE4N:  // Each of 4 bytes is normalized by dividing to 255.0
				fsize = 4;
				type = GL_UNSIGNED_BYTE;
				normalize = true;
				elemSizeof = 4;
				break;
			case VSDT_UBYTE4:	// 4D unsigned byte
				fsize = 4;
				type = GL_UNSIGNED_BYTE;
				elemSizeof = 4;
				break;
			case VSDT_SHORT2:	// 2D signed short expanded to (value, value, 0., 1.)
				fsize = 2;
				type = GL_SHORT;
				elemSizeof = 4;
				break;
			case VSDT_SHORT4:	// 4D signed short
				fsize = 4;
				type = GL_SHORT;
				elemSizeof = 8;
				break;
			case VSDT_SHORT2N:  // 2D signed short normalized (v[0]/32767.0,v[1]/32767.0,0,1)
				fsize = 2;
				type = GL_SHORT;
				normalize = true;
				elemSizeof = 4;
				break;
			case VSDT_SHORT4N:  // 4D signed short normalized (v[0]/32767.0,v[1]/32767.0,v[2]/32767.0,v[3]/32767.0)
				fsize = 4;
				type = GL_SHORT;
				normalize = true;
				elemSizeof = 8;
				break;
			case VSDT_USHORT2N:  // 2D unsigned short normalized (v[0]/65535.0,v[1]/65535.0,0,1)
				fsize = 2;
				type = GL_UNSIGNED_SHORT;
				normalize = true;
				elemSizeof = 4;
				break;
			case VSDT_USHORT4N:  // 4D unsigned short normalized (v[0]/65535.0,v[1]/65535.0,v[2]/65535.0,v[3]/65535.0)
				fsize = 4;
				type = GL_UNSIGNED_SHORT;
				normalize = true;
				elemSizeof = 8;
				break;
			case VSDT_FLOAT16_2:  // Two 16-bit floating point values, expanded to (value, value, 0, 1)
				fsize = 2;
				type = GL_HALF_FLOAT_ARB;
				elemSizeof = 4;
				break;
			case VSDT_FLOAT16_4:  // Four 16-bit floating point values
				fsize = 4;
				type = GL_HALF_FLOAT_ARB;
				elemSizeof = 8;
				break;
			default:
				enf_assert(!"Not implemented");
			};

			switch(elem.Usage)
			{
			case VSDU_POSITION:
				index = 0;
				break;
			case VSDU_NORMAL:
				index = 2;
				break;
			case VSDU_TANGENT:
				index = 14;
				break;
			case VSDU_BINORMAL:
				index = 15;
				break;
			case VSDU_COLOR:
				index = 3 + elem.UsageIndex;
				break;
			case VSDU_TEXCOORD:
				index = 8 + elem.UsageIndex;
				break;
			case VSDU_WEIGHTS:
				index = 1;
				break;
			case VSDU_INDICES:
				index = 7;
				break;
			case VSDU_PSIZE:
				index = 6;
				break;
			};

			GLvoid* dataPointer;
			uint streamStride;

			if(userPointer)
			{
				streamStride = stride;
				dataPointer = (GLvoid*)((ubyte*)userPointer + elem.Offset);

			}
			else
			{
				streamStride = m_Streams[stream].Stride;
				dataPointer = (GLvoid*)(m_Streams[stream].Offset + elem.Offset + streamStride * vertexOffset);
			}

			GLCALL(glEnableVertexAttribArrayARB(index));
			GLCALL(glVertexAttribPointerARB(index, fsize, type, normalize, streamStride, dataPointer));
			GLCALL(glVertexAttrib4fARB(index, 0.0f, 0.0f, 0.0f, 1.0f));
			attribMask &= ~(1 << index);
		}

		for(int n = 15; n >= 0; n--)
		{
			if(attribMask & (1 << n))
			{
				attribMask &= ~(1 << n);
				GLCALL(glDisableVertexAttribArrayARB(n));
			}
		}
	}
	if(cm & GLCM_INDEXBUFFER)
	{
		GLCALL(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_pIndexBuffer ? m_pIndexBuffer->m_iBuffer : 0));
	}

	if(cm & GLCM_ALPHATEST)
	{
		if(m_iRenderStateValue[RS_ALPHATESTENABLE] && m_bEnableAlphaTest)
		{
			GLCALL(glEnable(GL_ALPHA_TEST));
			GLCALL(glAlphaFunc(GLFunc((RenderCmpFunc)m_iRenderStateValue[RS_ALPHAFUNC]), (float)m_iRenderStateValue[RS_ALPHAREF] / 255.0f));
		}
		else
		{
			GLCALL(glDisable(GL_ALPHA_TEST));
		}
	}


	if(cm & GLCM_PIXELSHADER)
	{
//		glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, cgGLGetProgramID(m_iPShader));
		cgGLBindProgram(m_iPShader);

		GLSetProgramParameters(GL_FRAGMENT_PROGRAM_ARB, m_PSConstChanged, m_PSConstants);
		m_PSConstChanged = 0;
	}
	if(cm & GLCM_PSB)
	{
		//cgGLSetBoolFragmentRegisters(m_iVSConstantsB);
	}

	m_iCommitMask = 0;
}
#endif

#ifndef ENF_PS3
void Renderer::DrawIndexedPrimitive(PrimitiveType Type, int BaseVertexIndex, uint MinIndex, uint NumVertices, uint StartIndex, uint PrimitiveCount)
{
#ifdef ENF_DEVELOPER
	m_Stats.AddIndexedPrimitive(NumVertices, PrimitiveCount);
#endif
#if defined(ENF_DX9) || defined(ENF_XENON)
	m_pd3dDevice->DrawIndexedPrimitive((D3DPRIMITIVETYPE)Type, BaseVertexIndex, MinIndex,NumVertices, StartIndex, PrimitiveCount);
#elif defined(ENF_OGL)

	uint commitMask = m_iCommitMask;
	//commitMask = 0xffffffff;
	if((commitMask & GLCM_STREAMS) == 0)
	{
		if(BaseVertexIndex != m_iPrevBaseVertexIndex)
		{
			commitMask |= GLCM_STREAMS;
		}
	}
	m_iPrevBaseVertexIndex = BaseVertexIndex;

	if(commitMask)
		CommitStates(commitMask, NULL, BaseVertexIndex, 0);

	enf_assert(m_pIndexBuffer);
	GLenum ibFormat = m_IndexBufferFormat;
	uint offset = StartIndex << ((ibFormat == GL_UNSIGNED_SHORT) ? 1 : 2);

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
	GLCALL(glDrawElements(mode, count, ibFormat, (void*)offset));
#endif
}
#endif


//-----------------------------------------------------------------------
void Renderer::DrawQuad(uint StartVertex)
{
#ifdef ENF_DEVELOPER
	#ifdef ENF_RENDERQUADS
		m_Stats.AddPrimitive(PT_QUADLIST, 1);
	#else
		m_Stats.AddPrimitive(PT_TRIANGLESTRIP, 2);
	#endif
#endif

#if defined(ENF_DX9) || defined(ENF_XENON)
	#ifdef ENF_RENDERQUADS
		m_pd3dDevice->DrawPrimitive(D3DPT_QUADLIST, StartVertex, 1);
	#else
		m_pd3dDevice->DrawPrimitive(D3DPT_TRIANGLESTRIP, StartVertex, 2);
	#endif
#elif defined(ENF_PS3)
	if(m_iCommitMask)
		CommitStates(NULL, 0, 0);

	#ifdef ENF_RENDERQUADS
		GCM_CALL(cellGcmSetDrawArrays)(gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_QUADS, StartVertex, 4);
	#else
		GCM_CALL(cellGcmSetDrawArrays)(gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_TRIANGLE_STRIP, StartVertex, 4);
	#endif
	
	GCM_FINISH(gCellGcmCurrentContext);

#elif defined(ENF_OGL)

#ifdef ENF_RENDERQUADS
	const PrimitiveType Type = PT_QUADLIST;
#else
	const PrimitiveType Type = PT_TRIANGLESTRIP;
#endif

	uint commitMask = m_iCommitMask;
	//commitMask = 0xffffffff;
	if(commitMask)
		CommitStates(commitMask, NULL, 0, 0);
#ifdef ENF_RENDERQUADS
	GLCALL(glDrawArrays(GL_QUADS, StartVertex, 4));
#else
	GLCALL(glDrawArrays(GL_TRIANGLE_STRIP, StartVertex, 4));
#endif
#endif
}

//-----------------------------------------------------------------------
void Renderer::DrawQuads(uint StartVertex, uint PrimitiveCount)
{
#ifdef ENF_DEVELOPER
	#ifdef ENF_RENDERQUADS
		m_Stats.AddPrimitive(PT_QUADLIST, PrimitiveCount);
	#else
		m_Stats.AddPrimitive(PT_TRIANGLELIST, PrimitiveCount * 2);
	#endif
#endif

#if defined(ENF_DX9) || defined(ENF_XENON)
	#ifdef ENF_RENDERQUADS
		m_pd3dDevice->DrawPrimitive(D3DPT_QUADLIST, StartVertex, PrimitiveCount);
	#else
		m_pd3dDevice->DrawPrimitive(D3DPT_TRIANGLELIST, StartVertex, PrimitiveCount * 2);
	#endif
#elif defined(ENF_PS3)
	if(m_iCommitMask)
		CommitStates(NULL, 0, 0);

	#ifdef ENF_RENDERQUADS
		GCM_CALL(cellGcmSetDrawArrays)(gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_QUADS, StartVertex, PrimitiveCount * 4);
	#else
		GCM_CALL(cellGcmSetDrawArrays)(gCellGcmCurrentContext, CELL_GCM_PRIMITIVE_TRIANGLE_STRIP, StartVertex, PrimitiveCount * 2 * 3);
	#endif

	GCM_FINISH(gCellGcmCurrentContext)

#elif defined(ENF_OGL)

	#ifdef ENF_RENDERQUADS
		const PrimitiveType Type = PT_QUADLIST;
	#else
		const PrimitiveType Type = PT_TRIANGLELIST;
	#endif
	uint commitMask = m_iCommitMask;
	//commitMask = 0xffffffff;
	if(commitMask)
		CommitStates(commitMask, NULL, 0, 0);
	#ifdef ENF_RENDERQUADS
		GLCALL(glDrawArrays(GL_QUADS, StartVertex, PrimitiveCount * 4));
	#else
		GLCALL(glDrawArrays(GL_TRIANGLES, StartVertex, PrimitiveCount * 2 * 3));
	#endif
#endif
}

//-----------------------------------------------------------------------
void Renderer::DrawPrimitive(PrimitiveType Type, uint StartVertex, uint PrimitiveCount)
{
#ifdef ENF_DEVELOPER
	m_Stats.AddPrimitive(Type, PrimitiveCount);
#endif

#if defined(ENF_DX9) || defined(ENF_XENON)
	m_pd3dDevice->DrawPrimitive((D3DPRIMITIVETYPE)Type, StartVertex, PrimitiveCount);
#elif defined(ENF_PS3)
	if(m_iCommitMask)
		CommitStates(NULL, 0, 0);

	GCMDrawArrays(Type, StartVertex, PrimitiveCount);
#elif defined(ENF_OGL)
	uint commitMask = m_iCommitMask;
	//commitMask = 0xffffffff;
	if(commitMask)
		CommitStates(commitMask, NULL, 0, 0);

	GLDrawArrays(Type, StartVertex, PrimitiveCount);
#endif
}

//-----------------------------------------------------------------------
#ifdef ENF_PS3
ENF_INLINEFUNC uint GCMBlendOp(RenderBlendOperator op)
{
	switch(op)
	{
	case BLENDOP_ADD:
		return CELL_GCM_FUNC_ADD;
	case BLENDOP_SUBTRACT:
		return CELL_GCM_FUNC_SUBTRACT;
	case BLENDOP_REVSUBTRACT:
		return CELL_GCM_FUNC_REVERSE_SUBTRACT;
	case BLENDOP_MIN:
		return CELL_GCM_MIN;
	case BLENDOP_MAX:
		return CELL_GCM_MAX;
	};
	return CELL_GCM_FUNC_ADD;
}

ENF_INLINEFUNC uint GCMBlendFactor(RenderBlendFactor fac)
{
	switch(fac)
	{
	case BLEND_ZERO:
		return CELL_GCM_ZERO;
	case BLEND_ONE:
		return CELL_GCM_ONE;
	case BLEND_SRCCOLOR:
		return CELL_GCM_SRC_COLOR;
	case BLEND_INVSRCCOLOR:
		return CELL_GCM_ONE_MINUS_SRC_COLOR;
	case BLEND_SRCALPHA:
		return CELL_GCM_SRC_ALPHA;
	case BLEND_INVSRCALPHA:
		return CELL_GCM_ONE_MINUS_SRC_ALPHA;
	case BLEND_DESTALPHA:
		return CELL_GCM_DST_ALPHA;
	case BLEND_INVDESTALPHA:
		return CELL_GCM_ONE_MINUS_DST_ALPHA;
	case BLEND_DESTCOLOR:
		return CELL_GCM_DST_COLOR;
	case BLEND_INVDESTCOLOR:
		return CELL_GCM_ONE_MINUS_DST_COLOR;
	case BLEND_SRCALPHASAT:
		return CELL_GCM_SRC_ALPHA_SATURATE;
	case BLEND_BLENDFACTOR:
		return CELL_GCM_CONSTANT_COLOR;
		//case BLEND_INVBLENDFACTOR:
	};
	return CELL_GCM_ONE;
}


#endif


#if defined(ENF_DX9) || defined(ENF_OGL) || defined(ENF_PS3)
void Renderer::_SetRenderState(RenderState state, uint value)
{
	m_iRenderStateValue[(int)state] = value;
#ifdef ENF_DEVELOPER
	m_Stats.RSChanges++;
#endif

#if defined(ENF_DX9)

	m_pd3dDevice->SetRenderState((D3DRENDERSTATETYPE)state, value);
#elif defined(ENF_PS3)

	static const uint shiftCW_R = GetLowestBit(COLORWRITEENABLE_RED);
	static const uint shiftCW_G = GetLowestBit(COLORWRITEENABLE_GREEN);
	static const uint shiftCW_B = GetLowestBit(COLORWRITEENABLE_BLUE);
	static const uint shiftCW_A = GetLowestBit(COLORWRITEENABLE_ALPHA);

	switch(state)
	{
	case RS_FILLMODE:
		GCM_CALL(cellGcmSetFrontPolygonMode)(gCellGcmCurrentContext, value);
		GCM_CALL(cellGcmSetBackPolygonMode)(gCellGcmCurrentContext, value);
		break;

	case RS_ALPHATESTENABLE:
	case RS_ALPHAREF:
	case RS_ALPHAFUNC:
		m_iCommitMask |= GCMCM_ALPHATEST;
		break;
	case RS_CULLMODE:

		if(value == CULL_NONE)
		{
			GCM_CALL(cellGcmSetCullFaceEnable)(gCellGcmCurrentContext, CELL_GCM_FALSE);
		}
		else
		{
			//GCM_CALL(cellGcmSetFrontFace)(gCellGcmCurrentContext, value == CULL_CW ? CELL_GCM_CW : CELL_GCM_CCW);
			//GCM_CALL(cellGcmSetCullFace)(gCellGcmCurrentContext, CELL_GCM_FRONT);
			GCM_CALL(cellGcmSetCullFace)(gCellGcmCurrentContext, value == CULL_CW ? CELL_GCM_FRONT : CELL_GCM_BACK);

			GCM_CALL(cellGcmSetCullFaceEnable)(gCellGcmCurrentContext, CELL_GCM_TRUE);
		}
		break;

	case RS_POINTSIZE:
		GCM_CALL(cellGcmSetPointSize)(gCellGcmCurrentContext, *(float*)&value);
		break;

	case RS_POINTSIZE_MIN:
	case RS_POINTSIZE_MAX:
		break;

	case RS_POINTSPRITEENABLE:
		//to be compatible with DX9 and XBOX360 points sprite output coordinates, use texcoord0 (we can choose any of texcoords))
		GCM_CALL(cellGcmSetPointSpriteControl)(gCellGcmCurrentContext, value, CELL_GCM_POINT_SPRITE_RMODE_ZERO, CELL_GCM_POINT_SPRITE_TEX0);
		break;

	case RS_COLORWRITEENABLE:
		GCM_CALL(cellGcmSetColorMask)(gCellGcmCurrentContext, value);
		break;

	case RS_COLORWRITEENABLE1:
		{
			uint mask = CELL_GCM_COLOR_MASK_MRT1_A|CELL_GCM_COLOR_MASK_MRT1_R|CELL_GCM_COLOR_MASK_MRT1_G|CELL_GCM_COLOR_MASK_MRT1_B;
			if (value != COLORWRITEENABLE_ALL)
			{
				mask = 
					(((value & COLORWRITEENABLE_RED) >> shiftCW_R)*CELL_GCM_COLOR_MASK_MRT1_R) |
					(((value & COLORWRITEENABLE_GREEN) >> shiftCW_G)*CELL_GCM_COLOR_MASK_MRT1_G) |
					(((value & COLORWRITEENABLE_BLUE) >> shiftCW_B)*CELL_GCM_COLOR_MASK_MRT1_B) |
					(((value & COLORWRITEENABLE_ALPHA) >> shiftCW_A)*CELL_GCM_COLOR_MASK_MRT1_A);
			}
			GCM_CALL(cellGcmSetColorMaskMrt)(gCellGcmCurrentContext, mask);
		}
		break;

	case RS_COLORWRITEENABLE2:
		{
			uint mask = CELL_GCM_COLOR_MASK_MRT2_A|CELL_GCM_COLOR_MASK_MRT2_R|CELL_GCM_COLOR_MASK_MRT2_G|CELL_GCM_COLOR_MASK_MRT2_B;
			if (value != COLORWRITEENABLE_ALL)
			{
				mask = 
					(((value & COLORWRITEENABLE_RED) >> shiftCW_R)*CELL_GCM_COLOR_MASK_MRT2_R) |
					(((value & COLORWRITEENABLE_GREEN) >> shiftCW_G)*CELL_GCM_COLOR_MASK_MRT2_G) |
					(((value & COLORWRITEENABLE_BLUE) >> shiftCW_B)*CELL_GCM_COLOR_MASK_MRT2_B) |
					(((value & COLORWRITEENABLE_ALPHA) >> shiftCW_A)*CELL_GCM_COLOR_MASK_MRT2_A);
			}
			GCM_CALL(cellGcmSetColorMaskMrt)(gCellGcmCurrentContext, mask);
		}
		break;

	case RS_COLORWRITEENABLE3:
		{
			uint mask = CELL_GCM_COLOR_MASK_MRT3_A|CELL_GCM_COLOR_MASK_MRT3_R|CELL_GCM_COLOR_MASK_MRT3_G|CELL_GCM_COLOR_MASK_MRT3_B;
			if (value != COLORWRITEENABLE_ALL)
			{
				mask = 
					(((value & COLORWRITEENABLE_RED) >> shiftCW_R)*CELL_GCM_COLOR_MASK_MRT3_R) |
					(((value & COLORWRITEENABLE_GREEN) >> shiftCW_G)*CELL_GCM_COLOR_MASK_MRT3_G) |
					(((value & COLORWRITEENABLE_BLUE) >> shiftCW_B)*CELL_GCM_COLOR_MASK_MRT3_B) |
					(((value & COLORWRITEENABLE_ALPHA) >> shiftCW_A)*CELL_GCM_COLOR_MASK_MRT3_A);
			}
			GCM_CALL(cellGcmSetColorMaskMrt)(gCellGcmCurrentContext, mask);
		}	
		break;

	case RS_MULTISAMPLEANTIALIAS:
	case RS_MULTISAMPLEMASK:
	case RS_SCISSORTESTENABLE:
		break;

	case RS_DEPTHBIAS:
	case RS_SLOPESCALEDEPTHBIAS:
		if (m_iRenderStateValue[RS_DEPTHBIAS])
		{
			float slopeFactor = *(float*) &m_iRenderStateValue[RS_SLOPESCALEDEPTHBIAS];
			float bias = *(float*) &m_iRenderStateValue[RS_DEPTHBIAS];

			cellGcmSetPolygonOffset(gCellGcmCurrentContext, slopeFactor, bias);
			cellGcmSetPolygonOffsetFillEnable(gCellGcmCurrentContext, CELL_GCM_TRUE);
		}
		else
		{
			cellGcmSetPolygonOffsetFillEnable(gCellGcmCurrentContext, CELL_GCM_FALSE);
		}

		break;

	case RS_BLENDFACTOR:
		{
			//unused. if you want to use it do it by cellGcmSetBlendColor. You will need information about render target format to do so...
			FColor c = value;
			//GCM_CALL(cellGcmSetBlendEnable)(gCellGcmCurrentContext, CELL_GCM_TRUE);
			if(m_CurrentFB.colorFormat == CELL_GCM_SURFACE_A8R8G8B8)
			{	
				GCM_CALL(cellGcmSetBlendColor)(gCellGcmCurrentContext, value, value);
			}
			else if(m_CurrentFB.colorFormat == CELL_GCM_SURFACE_F_W16Z16Y16X16)
				enf_assert2(0, "16f blending");
			else
			{
				LogF(LL_WARNING, "not possible to blend format 0x%x\n", m_CurrentFB.colorFormat);
				enf_assert(0);
			}

			LogF(LL_WARNING, "Unused function RS_BLENDFACTOR\n");
			//	GLCALL(glBlendColor(c.r, c.g, c.b, c.a))
		}
		break;
	}
#elif defined(ENF_OGL)
	switch(state)
	{
	case RS_FILLMODE:
		GLCALL(glPolygonMode(GL_FRONT_AND_BACK, GLFill((RenderFillMode)value)));
		break;

	case RS_ALPHATESTENABLE:
	case RS_ALPHAREF:
	case RS_ALPHAFUNC:
		m_iCommitMask |= GLCM_ALPHATEST;
		break;
	case RS_CULLMODE:
		if(value == CULL_NONE)
		{
			GLCALL(glDisable(GL_CULL_FACE));
		}
		else
		{
			GLCALL(glEnable(GL_CULL_FACE));
			GLCALL(glFrontFace(value == CULL_CW ? GL_CW : GL_CCW ));
			GLCALL(glCullFace(GL_FRONT));
		}
		break;
	case RS_POINTSIZE:
		GLCALL(glPointSize(*(float*)&value));
		GLCALL(glDisable(GL_VERTEX_PROGRAM_POINT_SIZE_ARB));
		break;
	case RS_POINTSIZE_MIN:
		break;
	case RS_POINTSPRITEENABLE:
		GLCALL(glEnable(GL_VERTEX_PROGRAM_POINT_SIZE_ARB));
		break;
	case RS_COLORWRITEENABLE:
	case RS_COLORWRITEENABLE1:
	case RS_COLORWRITEENABLE2:
	case RS_COLORWRITEENABLE3:
		GLCALL(glColorMask((value &	COLORWRITEENABLE_RED) == 0 ? GL_FALSE : GL_TRUE,
			(value &	COLORWRITEENABLE_GREEN) == 0 ? GL_FALSE : GL_TRUE,
			(value &	COLORWRITEENABLE_BLUE) == 0 ? GL_FALSE : GL_TRUE,
			(value &	COLORWRITEENABLE_ALPHA) == 0 ? GL_FALSE : GL_TRUE));
		break;

	case RS_MULTISAMPLEANTIALIAS:
	case RS_MULTISAMPLEMASK:
	case RS_POINTSIZE_MAX:
	case RS_SCISSORTESTENABLE:
	case RS_SLOPESCALEDEPTHBIAS:
		break;
	case RS_BLENDFACTOR:
		{
			FColor c = value;
			GLCALL(glBlendColor(c.r, c.g, c.b, c.a));
		}
		break;
	case RS_DEPTHBIAS:
		break;
	}
#endif
}
#endif

//-----------------------------------------------------------------------
void Renderer::_SetBlendState(uint rtindex, BlendState blendState)
{
	m_iBlendStates[rtindex] = blendState;
#ifdef ENF_DEVELOPER
	m_Stats.BSChanges++;
#endif

#ifdef ENF_XENON

	BlendState state = blendState;
	state.EnableHighPrec = 0;

	m_pd3dDevice->GpuSetBlendControl(rtindex, &state.XState);

	BOOL highPrecAlpha = ENF_DIAG_RBOOL(DM_XBOX_HIGHPREC_ALPHA);
	m_pd3dDevice->SetRenderState(D3DRS_HIGHPRECISIONBLENDENABLE, blendState.EnableHighPrec && highPrecAlpha);

#elif defined(ENF_DX9)

	m_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, true);

#ifdef ENF_SEPARATE_ALPHABLEND_ENABLE
	m_pd3dDevice->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, false);
#endif

	m_pd3dDevice->SetRenderState(D3DRS_SRCBLEND , blendState.SrcBlend);
	m_pd3dDevice->SetRenderState(D3DRS_BLENDOP , blendState.BlendOp);
	m_pd3dDevice->SetRenderState(D3DRS_DESTBLEND , blendState.DestBlend);

#elif defined(ENF_PS3)
	#ifdef ENF_DEVELOPER
		if(m_CurrentFB.colorFormat == CELL_GCM_SURFACE_F_W32Z32Y32X32 || m_CurrentFB.colorFormat == CELL_GCM_SURFACE_F_X32)
		{
			enf_assert2(0, "32f cant blend\n");
			return;
		}
	#endif

	GCM_CALL(cellGcmSetBlendEnable)(gCellGcmCurrentContext, CELL_GCM_TRUE);

	uint blendOp = GCMBlendOp((RenderBlendOperator)blendState.BlendOp);
	GCM_CALL(cellGcmSetBlendEquation)(gCellGcmCurrentContext, blendOp, blendOp);

	uint srcFact = GCMBlendFactor((RenderBlendFactor)blendState.SrcBlend);
	uint dstFact = GCMBlendFactor((RenderBlendFactor)blendState.DestBlend);
	GCM_CALL(cellGcmSetBlendFunc)(gCellGcmCurrentContext, srcFact, dstFact, srcFact, dstFact);
#elif defined(ENF_OGL)

	GLCALL(glEnable(GL_BLEND));
	GLCALL(glBlendFunc(GLBlendFactor((RenderBlendFactor)blendState.SrcBlend), GLBlendFactor((RenderBlendFactor)blendState.DestBlend)));
	GLCALL(glBlendEquation(GLBlendOp((RenderBlendOperator)blendState.BlendOp)));
#endif

}

//-----------------------------------------------------------------------
void Renderer::_SetDepthStencilState(DepthStencilStateHandle state, uint stencilRef)
{
#ifdef ENF_DEVELOPER
	m_Stats.DSSChanges++;
#endif

	const DepthStencilStateDef& def = m_DepthStencilStateDefs[(int)state];
	
	#ifdef ENF_PS3
		DepthStencilStateDef oldDef;
		oldDef._Value = (uint) m_iDepthStencilState != 0xFFFFFFFF ? m_DepthStencilStateDefs[(int) m_iDepthStencilState]._Value : !def._Value;
	#endif

	m_iDepthStencilState = state;

#if defined(ENF_XENON) || defined(ENF_DX9)
	m_pd3dDevice->SetRenderState_Inline(D3DRS_ZENABLE, def.DepthEnable);
	m_pd3dDevice->SetRenderState_Inline(D3DRS_ZWRITEENABLE, def.DepthWriteEnable);
	m_pd3dDevice->SetRenderState_Inline(D3DRS_ZFUNC, GetDepthFunc((RenderCmpFunc)def.DepthFunc));
	m_pd3dDevice->SetRenderState_Inline(D3DRS_STENCILENABLE, def.StencilEnable);
	if(def.StencilEnable)
	{
		m_pd3dDevice->SetRenderState_Inline(D3DRS_STENCILFAIL, def.FrontStencilFailOp);
		m_pd3dDevice->SetRenderState_Inline(D3DRS_STENCILZFAIL, def.FrontStencilDepthFailOp);
		m_pd3dDevice->SetRenderState_Inline(D3DRS_STENCILPASS, def.FrontStencilPassOp);
		m_pd3dDevice->SetRenderState_Inline(D3DRS_STENCILFUNC, def.FrontStencilFunc);
		if(def.TwoSidedStencilEnable)
		{
			m_pd3dDevice->SetRenderState_Inline(D3DRS_CCW_STENCILFAIL, def.FrontStencilFailOp);
			m_pd3dDevice->SetRenderState_Inline(D3DRS_CCW_STENCILZFAIL, def.FrontStencilDepthFailOp);
			m_pd3dDevice->SetRenderState_Inline(D3DRS_CCW_STENCILPASS, def.FrontStencilPassOp);
			m_pd3dDevice->SetRenderState_Inline(D3DRS_CCW_STENCILFUNC, def.FrontStencilFunc);
		}
		m_pd3dDevice->SetRenderState_Inline(D3DRS_STENCILMASK, def.StencilReadMask);
		m_pd3dDevice->SetRenderState_Inline(D3DRS_STENCILWRITEMASK, def.StencilWriteMask);

		if(m_iStencilRef != stencilRef)
		{
			m_iStencilRef = stencilRef;
			m_pd3dDevice->SetRenderState_Inline(D3DRS_STENCILREF, stencilRef);
		}
	}
#elif defined(ENF_PS3)
	if(def.StencilEnable)
	{
		GCM_CALL(cellGcmSetStencilTestEnable)(gCellGcmCurrentContext, CELL_GCM_TRUE);

		if(def.TwoSidedStencilEnable)
		{
			GCM_CALL(cellGcmSetBackStencilFunc)(gCellGcmCurrentContext, GCMFunc((RenderCmpFunc)def.BackStencilFunc), 0x00, def.StencilReadMask);
			GCM_CALL(cellGcmSetBackStencilMask)(gCellGcmCurrentContext, def.StencilWriteMask);
			GCM_CALL(cellGcmSetBackStencilOp)(gCellGcmCurrentContext, GCMOp((RenderStencilOp)def.BackStencilFailOp), GCMOp((RenderStencilOp)def.BackStencilDepthFailOp), GCMOp((RenderStencilOp)def.BackStencilPassOp));
		}
		GCM_CALL(cellGcmSetStencilFunc)(gCellGcmCurrentContext, GCMFunc((RenderCmpFunc)def.FrontStencilFunc), 0x00, def.StencilReadMask);
		GCM_CALL(cellGcmSetStencilMask)(gCellGcmCurrentContext, def.StencilWriteMask);
		GCM_CALL(cellGcmSetStencilOp)(gCellGcmCurrentContext, GCMOp((RenderStencilOp)def.FrontStencilFailOp), GCMOp((RenderStencilOp)def.FrontStencilDepthFailOp), GCMOp((RenderStencilOp)def.FrontStencilPassOp));

		if (m_iStencilRef != stencilRef)
		{
			if(def.TwoSidedStencilEnable)
				GCM_CALL(cellGcmSetBackStencilFunc)(gCellGcmCurrentContext, GCMFunc((RenderCmpFunc)def.BackStencilFunc), stencilRef, def.StencilReadMask);

			GCM_CALL(cellGcmSetStencilFunc)(gCellGcmCurrentContext, GCMFunc((RenderCmpFunc)def.FrontStencilFunc), stencilRef, def.StencilReadMask);
		}
	}
	else
	{
		if (oldDef.StencilEnable != def.StencilEnable)
			GCM_CALL(cellGcmSetStencilTestEnable)(gCellGcmCurrentContext, CELL_GCM_FALSE);
	}

	if (oldDef.DepthWriteEnable != def.DepthWriteEnable)
		GCM_CALL(cellGcmSetDepthMask)(gCellGcmCurrentContext, def.DepthWriteEnable ? CELL_GCM_TRUE : CELL_GCM_FALSE);

	if (oldDef.DepthEnable != def.DepthEnable)
		GCM_CALL(cellGcmSetDepthTestEnable)(gCellGcmCurrentContext, def.DepthEnable ? CELL_GCM_TRUE : CELL_GCM_FALSE);

	if (oldDef.DepthFunc != def.DepthFunc)
		GCM_CALL(cellGcmSetDepthFunc)(gCellGcmCurrentContext, GCMFunc((RenderCmpFunc)def.DepthFunc));

#elif defined(ENF_OGL)
	if(def.DepthEnable)
	{
		GLCALL(glEnable(GL_DEPTH_TEST));
		GLCALL(glDepthFunc(GLFunc((RenderCmpFunc)def.DepthFunc)));
	}
	else
	{
		GLCALL(glDisable(GL_DEPTH_TEST));
	}

	GLCALL(glDepthMask(def.DepthWriteEnable ? GL_TRUE : GL_FALSE));

	if(def.StencilEnable)
	{
		GLCALL(glEnable(GL_STENCIL_TEST));
		if(def.TwoSidedStencilEnable)
		{
			GLCALL(glEnable(GL_STENCIL_TEST_TWO_SIDE_EXT));
			GLCALL(glActiveStencilFaceEXT(GL_FRONT));
			GLCALL(glStencilFunc(GLFunc((RenderCmpFunc)def.FrontStencilFunc), stencilRef, def.StencilReadMask));
			GLCALL(glStencilMask(def.StencilWriteMask));
			GLCALL(glStencilOp(GLOp((RenderStencilOp)def.FrontStencilFailOp), GLOp((RenderStencilOp)def.FrontStencilDepthFailOp), GLOp((RenderStencilOp)def.FrontStencilPassOp)));
			GLCALL(glActiveStencilFaceEXT(GL_BACK));
			GLCALL(glStencilFunc(GLFunc((RenderCmpFunc)def.BackStencilFunc), stencilRef, def.StencilReadMask));
			GLCALL(glStencilMask(def.StencilWriteMask));
			GLCALL(glStencilOp(GLOp((RenderStencilOp)def.BackStencilFailOp), GLOp((RenderStencilOp)def.BackStencilDepthFailOp), GLOp((RenderStencilOp)def.BackStencilPassOp)));
		}
		else
		{
			GLCALL(glDisable(GL_STENCIL_TEST_TWO_SIDE_EXT));
			GLCALL(glActiveStencilFaceEXT(GL_FRONT));
			GLCALL(glStencilFunc(GLFunc((RenderCmpFunc)def.FrontStencilFunc), stencilRef, def.StencilReadMask));
			GLCALL(glStencilMask(def.StencilWriteMask));
			GLCALL(glStencilOp(GLOp((RenderStencilOp)def.FrontStencilFailOp), GLOp((RenderStencilOp)def.FrontStencilDepthFailOp), GLOp((RenderStencilOp)def.FrontStencilPassOp)));
		}
	}
	else
		GLCALL(glDisable(GL_STENCIL_TEST));
#endif
}


#ifdef ENF_PS3

//-----------------------------------------------------------------------
class _RSXFrameCounter
{
private:
	struct FrameCB
	{
		static const ushort EMPTY = 0xFFFF;
		ushort From, To; 

		FrameCB(): From(EMPTY), To(EMPTY)
		{}

		FrameCB(uint from, uint to): From(from), To(to)
		{}
	};
	Array<FrameCB> m_Frames;
	Set<uint> m_CBOffsets;

	//-----------------------------------------------------------------------
	void Fill()
	{
		m_CBOffsets.Reserve(Renderer::ENF_COMMAND_BUFFERS);
		for (uint i = 0; i < Renderer::ENF_COMMAND_BUFFERS; i++)
			m_CBOffsets.Insert(IRenderer::m_pCmdBufOffset[i]);

		m_CBOffsets.Insert(m_CBOffsets.GetLast() + Renderer::ENF_COMMAND_BUFFER_SIZE);
	}

public:
	//-----------------------------------------------------------------------
	_RSXFrameCounter()
	{
		m_Frames.Reserve(10);
	}

	//-----------------------------------------------------------------------
	void Update(uint rsxGet)
	{
		if (m_CBOffsets.IsEmpty())
			Fill();

		if (m_Frames.IsEmpty())
			return;

		uint currCB = m_CBOffsets.FindGE(rsxGet);
		enf_assert(currCB != FrameCB::EMPTY && currCB <= Renderer::ENF_COMMAND_BUFFERS);

		int i = m_Frames.GetCardinality() - 1;
		if (currCB != m_Frames.GetLast().From)
		{
			i--;

			for (; i >= 0; i--)
			{
				const FrameCB& frame = m_Frames[i];

				if (frame.To < frame.From)
				{
					if (currCB >= frame.From || currCB <= frame.To)
						break;
				}
				else
				{
					if (currCB >= frame.From && currCB <= frame.To)
						break;
				}
			}
		}

		if (i > 0)
			m_Frames.RemoveMultipleElementOrdered(0, i);
	}


	//-----------------------------------------------------------------------
	void Flip(uint frameBufferStart, uint prev)
	{
		if (!m_Frames.IsEmpty())
		{
			enf_assert(m_Frames.GetLast().To == FrameCB::EMPTY);
			m_Frames.GetLast().To = prev;
		}

		m_Frames.Insert(FrameCB(frameBufferStart, FrameCB::EMPTY));
	}

	//-----------------------------------------------------------------------
	uint GetNumFrames()
	{
		return m_Frames.GetCardinality();
	}

};

#ifdef ENF_DEVELOPER
	_RSXFrameCounter RSXFrameCounter;
#endif


//-----------------------------------------------------------------------
bool IRenderer::WaitForRSXInCurrentCB(CellGcmContextData* con)
{
	// wait for RSX to finish all commands
	uint sizeLess = (m_iCurrCommandBuffer == ENF_COMMAND_BUFFERS - 1) ? 0x1000 : 0;

	uint startOffset = m_pCmdBufOffset[m_iCurrCommandBuffer];
	uint endOffset   = m_pCmdBufOffset[m_iCurrCommandBuffer] + ENF_COMMAND_BUFFER_SIZE - 32 - sizeLess;

	bool waiting = false;

	volatile uint rsxGet = cellGcmGetControlRegister()->get;

	//wait until the same buffer finished processing and until RSX finish processing the first 4KB of default command buffer code
	while ((rsxGet >=  startOffset && rsxGet < endOffset) || (rsxGet <  0x1000))
	{
		//RSX is in current buffer but has nothing to do
		if (rsxGet == (uint) ((uchar*) con->current - m_pCommandBuffer))
		{
			break;
		}

		//LogF(LL_DEBUG, "%02d = rsx 0x%X <0x%X, 0x%X>, current FIFO 0x%X", m_iCurrCommandBuffer, rsxGet, startOffset, endOffset, (uint) ((uchar*) con->current - m_pCommandBuffer));

		#ifdef ENF_DEVELOPER
			if (!waiting)
			{
				Timer::PushTime();
			}
		#endif

		waiting = true;

		sys_timer_usleep(30);
		rsxGet = cellGcmGetControlRegister()->get;
	}

	#ifdef ENF_DEVELOPER
		RSXFrameCounter.Update(rsxGet);
	#endif

	#ifdef ENF_DEVELOPER
		if (waiting)
		{
			m_fRSXWaitingTime += Timer::PopTime();
		}
	#endif

	return waiting;
}


//-----------------------------------------------------------------------
int IRenderer::ChangeCommandBuffer(CellGcmContextData* con, uint size)
{ 
	#ifdef ENF_DEVELOPER
		uint sizeinCB = (uint) (con->current) - (uint) m_pCmdBufAddress[m_iCurrCommandBuffer];
		enf_assert(sizeinCB < ENF_COMMAND_BUFFER_SIZE);

		m_iCommandBufferFill += sizeinCB;
	#endif

	// flush first
	GCM_CALL(cellGcmFlush)(con);

	uint prev = m_iCurrCommandBuffer;
	m_iCurrCommandBuffer = (m_iCurrCommandBuffer + 1) % ENF_COMMAND_BUFFERS;

	//last buffer is 0x1000 bytes shorter
	uint sizeLess = (m_iCurrCommandBuffer == ENF_COMMAND_BUFFERS - 1) ? 0x1000 : 0;
	uint sizeBuff = ENF_COMMAND_BUFFER_SIZE - 32 - sizeLess;

	// put jump command to beginning of next buffer
	GCM_CALL(cellGcmSetJumpCommand)(con, m_pCmdBufOffset[m_iCurrCommandBuffer]);
	cellGcmSetCurrentBuffer(m_pCmdBufAddress[m_iCurrCommandBuffer], sizeBuff);

	//if RSX is processing next selected buffer, wait
	WaitForRSXInCurrentCB(con);

	return CELL_OK;
}


//-----------------------------------------------------------------------
void Renderer::Flip(bool fakeFlip)
{
	uint prevCB = IRenderer::GetCurrCMDBuff();
	IRenderer::ChangeCommandBuffer(gCellGcmCurrentContext, 1);

	#ifdef ENF_DEVELOPER
		ENF_PROFILER_SETCOUNTER(PROFILE_CB_FRAME_SIZE, "Command buffer size");
		ENF_ADD_COUNTER(PROFILE_CB_FRAME_SIZE, IRenderer::m_iCommandBufferFill);

		if (IRenderer::m_iCommandBufferFill > ENF_COMMAND_BUFFERS_USER_SIZE)
		{
			LogF(LL_DEBUG, "Renderer::Flip -> command buffer for current flip is too large (%dKB, max %dKB)", IRenderer::m_iCommandBufferFill >> 10, ENF_COMMAND_BUFFERS_USER_SIZE >> 10);
		}

		if (IRenderer::m_fRSXWaitingTime > 0.0f)
		{
			LogF(LL_DEBUG, "Renderer::Flip -> waiting for RSX %0.2f ms", IRenderer::m_fRSXWaitingTime);
		}
	#endif

	if (fakeFlip)
	{
		#ifdef ENF_DEVELOPER
			LogF(LL_DEBUG, "FakeFlip");
		#endif

		return;
	}

	LimitRSXBufferedFrames(2);

	#ifdef ENF_DEVELOPER
		RSXFrameCounter.Update(cellGcmGetControlRegister()->get);
		RSXFrameCounter.Flip(IRenderer::GetCurrCMDBuff(), prevCB);

		ENF_PROFILER_SETCOUNTER(PROFILE_CB_FRAMES, "Command buffer frames");
		ENF_ADD_COUNTER(PROFILE_CB_FRAMES, RSXFrameCounter.GetNumFrames());

		IRenderer::m_iCommandBufferFill = 0;
		IRenderer::m_fRSXWaitingTime = 0.0f;
	#endif

	if (m_pShaderPatchBuffer)
		m_pShaderPatchBuffer->OnFlip();

	#ifdef CELL_GCM_DEBUG
		gCellGcmDebugCallback = NULL;
	#endif
	
	if (static_cast<IRenderer*>(this)->UsingSWRescaler())
	{
		while( cellRescGetFlipStatus() != CELL_OK )
		{
			sys_timer_usleep( 30 );
		}

		cellRescResetFlipStatus();

		//WaitForRSX();
		//Timer::PushTime();
		if (cellRescSetConvertAndFlip (gCellGcmCurrentContext, 0) != CELL_OK)
		{
			LogF(LL_ERROR, "cellRescSetConvertAndFlip failed");
		}

		//WaitForRSX();
		//LogF(LL_DEBUG, "Rescaler took %2.3f ms", Timer::PopTime());

		cellRescSetWaitFlip(gCellGcmCurrentContext);

		
	}
	else
	{
		while( cellGcmGetFlipStatus() != CELL_OK )
		{
			sys_timer_usleep( 30 );
		}

		cellGcmResetFlipStatus();

		cellGcmSetFlip(gCellGcmCurrentContext, m_BufferSwapIndex);
		GCM_CALL(cellGcmFlush)(gCellGcmCurrentContext);
		GCM_CALL(cellGcmSetWaitFlip)(gCellGcmCurrentContext);

		// New render target
		m_BufferSwapIndex = (m_BufferSwapIndex + 1) & 1;
		m_PrimaryFB.colorOffset[0] = m_PrimaryRB[m_BufferSwapIndex].GetOffset();
	}

	#ifdef CELL_GCM_DEBUG
		gCellGcmDebugCallback = cellGcmDebugFinish;
	#endif
	
	GCM_CALL(cellGcmSetSurfaceWindow)(gCellGcmCurrentContext, &m_PrimaryFB, CELL_GCM_WINDOW_ORIGIN_TOP, PS3_DIRECTX_COMPATIBLE_PIXEL_CENTER);

	m_CurrentFB = m_PrimaryFB;
	m_pCurrentRender = &m_PrimaryRB[m_BufferSwapIndex];
	
	GCM_CALL(cellGcmSetInvalidateVertexCache)(gCellGcmCurrentContext);
	GCM_CALL(cellGcmSetInvalidateTextureCache)(gCellGcmCurrentContext, CELL_GCM_INVALIDATE_TEXTURE);

	SetBlendState(0, false);	
	GCM_CALL(cellGcmFlush)(gCellGcmCurrentContext);
}

#endif


//-----------------------------------------------------------------------
bool Renderer::CheckThread()
{
#if defined(WIN32) || defined(ENF_XENON)
	IL_INT32 threadId = m_ThreadLock;
	return (threadId == 0 || threadId == (uint)::GetCurrentThreadId());
#else
	return true;
#endif
}

//-----------------------------------------------------------------------
void Renderer::ThreadLock()
{
#if defined(WIN32) || defined(ENF_XENON)
	IL_INT32 threadId = IL_EXCHANGE(m_ThreadLock, (IL_INT32)::GetCurrentThreadId());
	enf_rlsassert(threadId == 0);
#endif
}

//-----------------------------------------------------------------------
void Renderer::ThreadUnlock()
{
#if defined(WIN32) || defined(ENF_XENON)
	IL_INT32 threadId = IL_EXCHANGE(m_ThreadLock, 0);
	enf_rlsassert(threadId == (IL_INT32)::GetCurrentThreadId());
#endif
}


}