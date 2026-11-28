//-----------------------------------------------------------------------------
// File: enf_texture.h
//
// Desc: Texture abstraction
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_TEXTURE_H
#define ENF_TEXTURE_H

#include "enf_config.h"
#include "render/enf_renderresources.h"
#include "resource/enf_resource.h"
#include "render/enf_renderenums.h"


namespace enf
{
struct FColor;
enum FSAAType;
class ResourceManager;
class IResourceManager;
class ImageData;
class MemoryImageData;
class COutputStream;
struct LockedImageRect;

/** @addtogroup Renderer
@{*/


//-----------------------------------------------------------------------------
struct SurfaceDesc
{
	TextureFormat	Format;
	uint				Usage;
	uint				Pool;
	uint				Width;
	uint				Height;
};

struct BitmapInfo
{
	uint		Size;
	uint		Width;
	uint		Height;
	ushort	Planes;
	ushort	BitCount;
	uint		Compression;
	uint		SizeImage;
	uint		XPelsPerMeter;
	uint		YPelsPerMeter;
	uint		ClrUsed;
	uint		ClrImportant;
};

enum TextureFilter
{
	TFF_NONE,
	TFF_POINT,
	TFF_LINEAR,
	TFF_BOX,
	TFF_MIRROR = BITF(8)
};

//-----------------------------------------------------------------------
class PUBLIC_API Texture: public DeviceResource, public ResourceRef
{
	friend class Renderer;
	friend class IRenderer;
	friend class IResourceManager;

protected:

	bool Flush();
	virtual void _Touch(uint level) const;

	~Texture();

	CStr					m_strName;
	ushort				m_iWidth, m_iHeight;
	uint					m_iFlags;
	ushort				m_iMipLevels;
	ushort/*TextureUsage*/	m_Usage;
	TextureFormat		m_Format;
	MemoryImageData*	m_pStagingData;

	Texture(IRenderer *dev, CStr name, uint width, uint height, uint flags);
	Texture(IRenderer *dev, CStr name);
	Texture(IRenderer *dev);

	ERESULT Allocate();
	void Free();

public:
	ENF_DECLARE_ITEM(Texture, DeviceResource);

	bool IsCube() const;

	ENF_INLINE TextureUsage GetUsage() const
	{
		return (TextureUsage)m_Usage;
	}
	ENF_INLINE uint GetFlags() const
	{
		return m_iFlags;
	}
	ENF_INLINE bool IsLoaded() const
	{
		return (m_iFlags & TEXTR_MANAGED) == 0 || (m_iFlags & TEXTR_LOADED) != 0;
	}
	ENF_INLINE bool IsFlagSet(uint flags) const
	{
		return (m_iFlags & flags) != 0;
	}
	ENF_INLINE bool IsAllocated() const
	{
		return true;
	}

	ENF_INLINE uint SetFlags(uint flags)
	{
		uint oldFlags = m_iFlags;
		m_iFlags |= flags;
		return oldFlags;
	}

	ENF_INLINE uint ClearFlags(uint flags)
	{
		uint oldFlags = m_iFlags;
		m_iFlags &= ~flags;
		return oldFlags;
	}

	//!Returns U coordinate shifted by 0.5 of texel size. Necessary for exact texel->pixel mapping
	ENF_INLINE float GetU(float u) const
	{
		return u + ((1.0f / (float)m_iWidth) * 0.5f);
	}

	//!Returns V coordinate shifted by 0.5 of texel size. Necessary for exact texel->pixel mapping
	ENF_INLINE float GetV(float v) const
	{
		return v + ((1.0f / (float)m_iHeight) * 0.5f);
	}

	//!Returns internal texture objects. Do not increments refcount
	#if defined(ENF_OGL)
		GLuint GetGLTexture() const
		{
			return m_iTexture;
		}
	#endif

	//!Returns name of texture
	CStr GetName() const
	{
		return m_strName;
	}

	//!Returns width
	ENF_INLINE uint GetWidth() const
	{
		return m_iWidth;
	}

	//!Returns height
	ENF_INLINE uint GetHeight() const
	{
		return m_iHeight;
	}

	//!Returns format
	ENF_INLINE TextureFormat GetFormat() const
	{
		return m_Format;
	}

	//!Returns number of mip-levels
	uint GetNumLevels() const;

	/*!
	get number of mipmaps for current width, height and depth

	*/
	static uint GetMaxMipmaps(uint width, uint height, uint depth);

	/*!
	Filters lower mip-levels
	\param level
	From what level 
	\param flags 
	Filter flags, See TextureFilter
	\return
	EE_OK when success
	*/
	ERESULT FilterMipMaps(uint level, uint flags);

	bool Lock(uint level, const RRect *rect, LockedImageRect *lrect, SurfaceDesc *desc, bool readOnly = false);
	void Unlock(uint level);

	bool LockCubeFace(CubeMapFaces face, uint level, const RRect *rect, LockedImageRect *lrect, SurfaceDesc *desc, bool readOnly = false);
	void UnlockCubeFace(CubeMapFaces face, uint level);

	/*!
	Copies RAW data in FColor format into texture. It handles conversions
	into all supported TextureFormat.
	\param level
	Mip level to be updated
	\param xpos
	Left corner, where data are being copied
	\param ypos
	Top corner, where data are being copied
	\param ptr
	Source data
	\param width
	Width of source data
	\param height
	Height of source data
	\param rect
	Rectangle of source data to be copied
	\return
	EE_OK when success
	*/
	ERESULT CopyRGBAData(uint level, uint xpos, uint ypos, const FColor* ptr, uint width, uint height, const RRect& rect);

	/*!
	Copies data to texture. the data must be in texture format, no conversion is made!
	No compressed formats supported ATM!!!!!
	\param level		Mipmap level
	\param data			Pointer to upper/left corner of data
	\param dest			Destination position and dimensions. Dimensions and position must be inside texture
									and must match with width of source data. No clipping is done!
	*/
	ERESULT CopyData(uint level, const void* data, const RRect& dest);
	ERESULT CopyData(ImageData& imageData, uint flags);

	ERESULT FillColor(uint color, uint sx, uint sy, uint w, uint h);
	ERESULT FillColor(const FColor& color, uint sx, uint sy, uint w, uint h);


	//!Returns total size in memory including mip-maps
	size_t GetMemorySize() const;
	size_t GetSize() const
	{
		return GetMemorySize();
	}


	int Release();

	virtual size_t Flush(uint level);
	/*!
	Exchanges content of this Texture with another one
	\param other
	Other texture which content is set to this one and vice versa
	\return
	True when success, false otherwise
	*/
	bool Swap(Texture* other);
};



//-----------------------------------------------------------------------
class PUBLIC_API RenderTarget: public Texture
{
	friend class Renderer;
	friend class IRenderer;

private:

	#if defined(ENF_OGL)
		GLuint		m_iDepthBuffer;
		GLuint		m_iFrameBuffer;
	#endif

	TextureFormat	m_DepthFormat;
	FSAAType			m_FSAAType;
	uint				m_iUsage;

	ERESULT Allocate();
	void Free();

public:
	ENF_DECLARE_ITEM(RenderTarget, Texture);

	RenderTarget(IRenderer *dev, uint width, uint height, TextureFormat rtfmt, TextureFormat depthfmt, uint edramoffset);

	uint GetEDRAMBegin() const 
	{ 
		return 0; 
	}
	uint GetEDRAMEnd() const 
	{ 
		return 0; 
	}


	#if defined(ENF_OGL)
		GLuint GetDepthBuffer() const
		{
			return m_iDepthBuffer;
		}
	#endif

	/*!
	Needs to be called immediately after rendering into the RenderTarget, to provide copy 
	to Texture representation. It's dummy on Win32, but necessary on XBox360
	Probably used on win32 too, when we need FSAA on surfaces...
	\param rt
	Render target index we want to resolve
	\param rect
	Rectangle to be resolved, NULL is default for full rect
	\param point 
	destination point 
	*/
	void Resolve(uint rt, const RRect* rect = NULL, const RPoint* point = NULL);

	size_t GetMemorySize() const;
};





//-----------------------------------------------------------------------------
/*!
return size of nearest RT to avoid reallocations of different sizes, on XBOX 
must be dividable by 8
\param width			wanted width
\param height			wanted height
\param targetwidth	target width
\param targetheight	target height
\param reswidth		computed width
\param resheight		computed height
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC void GetNearestRTSize(uint width, uint height, uint targetwidth, uint targetheight, uint& reswidth, uint &resheight)
{
	//TODO: for bigger resolution, but do we need it?
	enf_assert(width <= targetwidth && height <= targetheight);

	uint nearestx = 1 << GetHighestBit(targetwidth/width);
	uint nearesty = 1 << GetHighestBit(targetheight/height);

	reswidth  = targetwidth/nearestx;
	resheight = targetheight/nearesty;
}


//-----------------------------------------------------------------------------
// Texture creation and deletion functions
//-----------------------------------------------------------------------------
/*!
Tries to load texture with exact name+extension
\param dev
Renderer device
\param name
Name of texture file
\param flags
Flags. See enf::TextureCreationFlags
\param usage
Usage hint
\return
Texture, or NULL when not found
*/
PUBLIC_API Texture* LoadTexture(Renderer *dev, CStr name, uint flags, TextureUsage usage);

enum ImageFileFormat
{
	//!Unknown
	IFF_UNKNOWN,
	//!Targa
	IFF_TGA,
	//!Standard DDS
	IFF_DDS,
	//!DDS compressed
	IFF_DDSC,
	
	//!end of engine supported formats
	IFF_USER_FORMAT
};


PUBLIC_API bool SaveTextureToFile(CStr name, Texture* tex, ImageFileFormat iff = IFF_UNKNOWN);
PUBLIC_API bool SaveTextureToStream(COutputStream& outStream, Texture* tex, ImageFileFormat iff);


struct CompressedDDSDesc;

PUBLIC_API int SaveCompressedDDS(ImageData& image, COutputStream& outstream, const CompressedDDSDesc& compdesc);
PUBLIC_API int SaveDDSTexture(COutputStream& outStream, MemoryImageData& memdata, bool bigendian);
PUBLIC_API size_t GetSurfaceSize(uint width, uint heigth, TextureFormat fmt);
PUBLIC_API size_t GetFormatStride(TextureFormat fmt);
PUBLIC_API TextureFormat ComputeFormat(uint dwFlags, ushort* dwMipLevels);
PUBLIC_API size_t GetSourceSurfaceSize(TextureFormat fmt, uint srcDepth, uint width, uint height);

PUBLIC_API void DecodeChannels(TextureFormat fmt, uint p, ubyte& r, ubyte& g, ubyte& b, ubyte& a);
//@}
}

#endif // ENF_TEXTURE_H
