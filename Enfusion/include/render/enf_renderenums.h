//-----------------------------------------------------------------------------
// File: render/enf_rendenums.h
//
// Desc: Main render class
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_RENDENUMS_H
#define ENF_RENDENUMS_H

#include "enf_config.h"
#include "common/enf_inttypes.h"


namespace enf
{
/** @addtogroup Renderer
@{*/

/*!
maximum allowed texture mipmap and its related maximum size, 13 = 4096
*/
static const uint MAX_TEXTURE_MIPMAPS = 13;
static const uint MAX_TEXTURE_SIZE		= 1 << (MAX_TEXTURE_MIPMAPS - 1);


//!Geometry primitive type for Renderer::DrawXXX methods
enum PrimitiveType
{
	PT_BAD = 0,
	PT_POINTLIST      = 1,
	PT_LINELIST       = 2,
	PT_LINESTRIP      = 3,
	PT_TRIANGLELIST   = 4,
	PT_TRIANGLESTRIP  = 5,
	PT_TRIANGLEFAN    = 6,
	PT_QUADLIST				= 7,
	PT_FORCE_DWORD    = 0x7fffffff /* force 32-bit size enum */
};

enum QueryCommand
{
	QC_ISSUE_END		= (1 << 0),
	QC_ISSUE_BEGIN		= (1 << 1),
};

const uint QC_GETDATA_FLUSH = (1 << 0);

enum CubeMapFaces
{
	CUBEMAP_FACE_POSITIVE_X	= 0,
	CUBEMAP_FACE_NEGATIVE_X,
	CUBEMAP_FACE_POSITIVE_Y,
	CUBEMAP_FACE_NEGATIVE_Y,
	CUBEMAP_FACE_POSITIVE_Z,
	CUBEMAP_FACE_NEGATIVE_Z,
};

/*!
Texture creation flags. Most of flags are hints for
picking up proper format (for creation methods without
exact format), and hints for loader of dumb formats (TGA)
how to handle mipmaps and formats.
*/
enum TextureCreationFlags
{
/** \name Allocation hints
*/
//@{
	//!Use dynamic texture
	TEXTR_DYNAMIC					= BITF(0),
	//!Force texture to system memory
	TEXTR_SYSMEM					= BITF(1),
	//!Do not create mipmap chain
	TEXTR_NOMIPMAP				= BITF(2),
	//!For XBox/PS3 implementation
	TEXTR_LINEAR					= BITF(3),
	//!For PS3 implementation
	TEXTR_VERTEXTEXTURE		= BITF(4),
	//!Forces default pool
	TEXTR_DEFAULTPOOL			= BITF(5),
	//!Do not allocate memory at all (only console hardware)
	TEXTR_USERMEMORY			= BITF(6),
	//!Don't keep unreferenced
	TEXTR_NOCACHE					= BITF(8),
//@}

	//!Create cube texture
	TEXTR_CUBE						= BITF(9),
	//!Forces autogenmipmap texture
	TEXTR_AUTOMIPMAP			= BITF(10),
	//!Texture will be used as rendertarget
	TEXTR_RENDERTARGET		= BITF(11),
	//!Restricts texture loader from downsizing them
	TEXTR_NORESIZE				= BITF(12),

#ifdef ENF_PS3
	//!for render target only. decide if we want to use tiled memory for this one
	TEXTR_TILED						= BITF(13),
	//! swizzled RT, better when rendering once but reading more
	TEXTR_SWIZZLED				= BITF(14),
#else
	TEXTR_TILED						= 0,
#endif

/** \name Material hints
*/
//@{
	//!Material hint.
	TEXTR_CLAMPU				= BITF(15),
	//!Material hint
	TEXTR_CLAMPV				= BITF(16),
	//!Material hint
	TEXTR_NOFILTER			= BITF(17),
	//!Material hint
	TEXTR_SPECIAL				= BITF(18),

#ifdef ENF_STEREO_VISION
	//!stereo surface
	TEXTR_STEREO 				= BITF(19),
#endif
//@}
	
/** \name Internal flags
*/
//@{
	//!Content is cloned
	TEXTR_CLONED				= BITF(24),
	//!Content is clone
	TEXTR_CLONE					= BITF(25),
	//!Dynamicaly managed
	TEXTR_MANAGED				= BITF(26),
	//!Hint for ResourceManager
	TEXTR_RELOAD				= BITF(27),
	//!Managed textures must not be loaded all the time
	TEXTR_LOADED				= BITF(28),
//@}
};

//! Shader profile
enum ProfileType
{
	PRF_NONE,
	PRF_VS20,
	PRF_VS2a,
	PRF_VS30,
	PRF_PS20,
	PRF_PS2a,
	PRF_PS30
};

/*!
TextureUsage is hint for MaterialClass, how to setup filtering.
When changed, you must change MaterialClass::TextureUsage2Filter too!!!
Also used as hint for ResourceManager
*/
enum TextureUsage
{
	//!Used as diffuse texture
	MAP_DIFFUSE,
	//!Used as lightmap texture
	MAP_LIGHTMAP,
	//!Used as normal-map texture
	MAP_NORMAL,
	//!Used as DXT compressed normal-map texture
	MAP_NORMAL_DXT,
	//!Used as refraction texture (DuDv)
	MAP_REFRACT,
	//!Used as environment texture
	MAP_ENVIRONMENT,
	//!Used as height-map for parallax mapping
	MAP_HEIGHT,
	//!Used for editor only
	MAP_EDITOR_ONLY,
	//!Used as 2D image
	MAP_IMAGE,
	MAP_UNKNOWN,
	MAP_SIZEOF
};


/*!
some microsoft format
*/
#define ENF_MAKEFOURCC_CTX1	ENF_MAKEFOURCC(0x7C, 0x00, 0x20, 0x1A)


enum TextureFormat
{
	TEXFMT_UNKNOWN              =  0,
	TEXFMT_R8G8B8               = 20, //FIXME: remove this shit.
	TEXFMT_A8R8G8B8             = 21,
	TEXFMT_X8R8G8B8             = 22,
	TEXFMT_R5G6B5               = 23,
	TEXFMT_X1R5G5B5             = 24,
	TEXFMT_A1R5G5B5             = 25,
	TEXFMT_A4R4G4B4             = 26,
	TEXFMT_A8                   = 28,
	TEXFMT_X4R4G4B4             = 30,
	TEXFMT_G16R16               = 34,
	TEXFMT_A2R10G10B10          = 35,
	TEXFMT_L8                   = 50,
	TEXFMT_A8L8                 = 51,
	TEXFMT_V8U8                 = 60,
	TEXFMT_L6V5U5               = 61,
	TEXFMT_X8L8V8U8             = 62,
	TEXFMT_Q8W8V8U8             = 63,
	TEXFMT_V16U16               = 64,
	TEXFMT_A2W10V10U10          = 67,
	TEXFMT_UYVY                 = ENF_MAKEFOURCC('U', 'Y', 'V', 'Y'),
	TEXFMT_YUY2                 = ENF_MAKEFOURCC('Y', 'U', 'Y', '2'),
	TEXFMT_DXT1                 = ENF_MAKEFOURCC('D', 'X', 'T', '1'),
	TEXFMT_DXT2                 = ENF_MAKEFOURCC('D', 'X', 'T', '2'),
	TEXFMT_DXT3                 = ENF_MAKEFOURCC('D', 'X', 'T', '3'),
	TEXFMT_DXT4                 = ENF_MAKEFOURCC('D', 'X', 'T', '4'),
	TEXFMT_DXT5                 = ENF_MAKEFOURCC('D', 'X', 'T', '5'),
	TEXFMT_CTX1									= ENF_MAKEFOURCC_CTX1,
	TEXFMT_D32                  = 71,
	TEXFMT_D24S8                = 75,
	TEXFMT_D24X8                = 77,
	TEXFMT_D16                  = 80,
	TEXFMT_D24FS8               = 83,
	TEXFMT_L16                  = 81,

	TEXFMT_R16F									= 111,
	TEXFMT_G16R16F							= 112,
	TEXFMT_A16B16G16R16F				= 113,

	TEXFMT_R32F									= 114,
	TEXFMT_G32R32F							= 115,
	TEXFMT_A32B32G32R32F				= 116
};

ENF_INLINEFUNC bool IsDXTFormat(TextureFormat fmt)
{
	return (fmt == TEXFMT_DXT1 || fmt == TEXFMT_DXT2 || fmt == TEXFMT_DXT3 || fmt == TEXFMT_DXT4 || fmt == TEXFMT_DXT5 || fmt == TEXFMT_CTX1);
}

ENF_INLINEFUNC bool IsFloatFormat(TextureFormat fmt)
{
	return (fmt == TEXFMT_R16F ||
		fmt == TEXFMT_G16R16F ||
		fmt == TEXFMT_A16B16G16R16F ||
		fmt == TEXFMT_R32F ||
		fmt == TEXFMT_G32R32F ||
		fmt == TEXFMT_A32B32G32R32F);
}

ENF_INLINEFUNC bool IsFloat32Format(TextureFormat fmt)
{
	return (fmt == TEXFMT_R32F ||
		fmt == TEXFMT_G32R32F ||
		fmt == TEXFMT_A32B32G32R32F);
}

#ifdef ENF_PS3
	ENF_INLINEFUNC bool IsFloat32SurfaceFormat(int fmt)
	{
		return 
			fmt == CELL_GCM_SURFACE_F_W32Z32Y32X32 ||
			fmt == CELL_GCM_SURFACE_F_X32;
	}	
#endif


//-----------------------------------------------------------------------
//!Format of IndexBuffer See Renderer::CreateIndexBuffer()
enum IndexBufferFormat
{
	IBFMT_UNKNOWN		= 0xffffffff,
	IBFMT_INDEX16		= 1,
	IBFMT_INDEX32		= 2
};

//!Multisampling types
enum FSAAType
{
	FSAATYPE_NONE				= 0,
	FSAATYPE_2_SAMPLES = 2,
	FSAATYPE_4_SAMPLES = 4,
	FSAATYPE_8_SAMPLES = 8,
	FSAATYPE_16_SAMPLES = 16,
};

enum ResourceType
{
	DeviceResourceTypeID		= 1,
	RenderBufferTypeID,
	VertexBufferTypeID,
	IndexBufferTypeID,
	TextureTypeID,
	RenderTargetTypeID,
	RenderShaderTypeID,
	VertexShaderTypeID,
	PixelShaderTypeID,
	SwapChainTypeID,
	RenderQueryTypeID,
};

enum ResourceUsageFlags
{
	RES_USAGE_RENDERTARGET				= 0x00000001L,
	RES_USAGE_WRITEONLY						= 0x00000008L,
	RES_USAGE_SOFTWAREPROCESSING	= 0x00000010L,
	RES_USAGE_POINTS							= 0x00000040L,
	RES_USAGE_DYNAMIC							= 0x00000200L,
	RES_USAGE_AUTOGENMIPMAP				= 0x00000400L,
	RES_USAGE_QUERY_VERTEXTEXTURE	= 0x00100000L,

#ifdef ENF_PS3
	RES_USAGE_MEMORY_BUFFER				= 0x80000000L
#endif
};

//!Flags for resource locking
enum LockFlags
{
	RES_LOCK_READONLY									= 0x00000010L,
#ifdef ENF_OGL
	RES_LOCK_WRITEONLY								= 0x00000010L,
#else
	RES_LOCK_WRITEONLY								= 0x00000000L,
#endif
	RES_LOCK_DISCARD									= 0x00002000L,
	RES_LOCK_NOOVERWRITE							= 0x00001000L,
	RES_LOCK_NOSYSLOCK								= 0x00000800L,
	RES_LOCK_DONOTWAIT								= 0x00004000L,
	RES_LOCK_NO_DIRTY_UPDATE					= 0x00008000L,
	RES_LOCK_NO_GPU_CACHE_INVALIDATE	= 0

};

/*!
Renderer related error codes
*/
enum RenderErrorCodes
{
	EE_OK										= 0,
	EE_INVALIDPARAM					= 1,
	EE_VS_UNSUPPORTED				= 2,
	EE_ERROR								= 3,
	EE_UNSUPPORTED					= 4,

	EE_NOWINDOW							= 0x82000002,
	EE_NOCOMPATIBLEDEVICES	= 0x82000003,
	EE_NOWINDOWABLEDEVICES	= 0x82000004
};

#define EE_FAIL(a) ((a) != EE_OK)
#define EE_PASSED(a) ((a) == EE_OK)


//@}
}
#endif //ENF_RENDENUMS_H
