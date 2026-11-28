//-----------------------------------------------------------------------------
// File: enf_imageloader.cpp
//
// Desc: Image loader & TGA/DDS decoder
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "common/enf_streams.h"
#include "enf_main.h"
#include "resource/enf_ddsformat.h"
#include "resource/enf_imageloader.h"
#include "resource/enf_resourcemanager.h"
#include "render/enf_texture.h"
#include "resource/enf_ddsformat.h"
#include "common/enf_memorybuffer.h"

//#include "zlib/zlib.h"
//#include "lzo/lzo1x.h"

#ifdef LZX_COMPRESSION
	#ifdef ENF_XENON
		#include "xcompress.h"
	#else
		#include "C:/Program Files/Microsoft Xbox 360 SDK/include/win32/xcompress.h"
	#endif
#endif

namespace enf
{

//-----------------------------------------------------------------------------
class DDSLoader: public ImageFormatLoader
{
protected:

//-----------------------------------------------------------------------------
	void LoadSurface(LockedImageRect& lrect, const ImageInfo& imginfo, int width, int height, CInputStream& instream)
	{
		switch(imginfo.Format)
		{
		case TEXFMT_CTX1:
			{
				size_t size = GetDXTBlocks(width) * GetDXTBlocks(height) * 8;

				instream.Read((unsigned char*)lrect.pBits, size);
			}
			break;

		case TEXFMT_DXT1:
			{
				size_t size = GetDXTBlocks(width) * GetDXTBlocks(height) * 8;

				instream.Read((unsigned char*)lrect.pBits, size);
#if !defined(ENF_PS3) && !defined(EXPORT_IN_PLATFORM_ORDER)
				LE_SHORT_ARRAY((ushort*)lrect.pBits, size / 2);
#endif
			}
			break;

		case TEXFMT_DXT3:
		case TEXFMT_DXT5:
			{
				size_t size = GetDXTBlocks(width) * GetDXTBlocks(height) * 16;
				//TODO: respect stride!!!
				instream.Read((unsigned char*)lrect.pBits, size);
#if !defined(ENF_PS3) && !defined(EXPORT_IN_PLATFORM_ORDER)
				LE_SHORT_ARRAY((ushort*)lrect.pBits, size / 2);
#endif
			}
			break;

		case TEXFMT_A8:
		case TEXFMT_L8:
			for(int v = 0; v < height; v++)
			{
				instream.Read((unsigned char*)lrect.pBits + lrect.Pitch * v, width);
			}
			break;

		case TEXFMT_A8R8G8B8:
		case TEXFMT_X8R8G8B8:
			{
				for(int v = 0; v < height; v++)
				{
					unsigned char* pdest = (unsigned char*)lrect.pBits + lrect.Pitch * v;

					if(imginfo.Depth == 24)
					{
						for(int h = 0; h < width; h++)
						{
							instream.Read(pdest + h * 4, 3);
							pdest[h * 4 + 3] = 0xff;
						}
					}
					else
					{
						instream.Read(pdest, width * 4);
					}

					#if !defined(EXPORT_IN_PLATFORM_ORDER)
						LE_INT_ARRAY((uint*)pdest, width);
					#endif
				}
			}
			break;

		case TEXFMT_A32B32G32R32F:
			for(int v = 0; v < height; v++)
			{
				instream.Read((unsigned char*)lrect.pBits + lrect.Pitch * v, width * 16);
			}

			#if !defined(EXPORT_IN_PLATFORM_ORDER)
				LE_FLOAT_ARRAY((float*) lrect.pBits, width*height);
			#endif
			break;

		case TEXFMT_A8L8:
			for(int v = 0; v < height; v++)
			{
				instream.Read((unsigned char*)lrect.pBits + lrect.Pitch * v, width * 2);

				#if !defined(EXPORT_IN_PLATFORM_ORDER)
					LE_SHORT_ARRAY((ushort*)((unsigned char*)lrect.pBits + lrect.Pitch * v), width);
				#endif
			}
			break;

		case TEXFMT_A4R4G4B4:
			for(int v = 0; v < height; v++)
			{
				instream.Read((unsigned char*)lrect.pBits + lrect.Pitch * v, width * 2);
			}
			break;

		default:
			enf_assert(!"Unsupported format");
			break;
		}
	}

public:
	ENF_DECLARE_SMALLOBJECT(DDSLoader);

	DDSLoader() {}

	ERESULT Load(CInputStream& instream, const ImageInfo& _imginfo, uint miplevels, uint flags, uint maxsize, ImageData& imageData)
	{
		ImageInfo imginfo = _imginfo;
		uint skiplevels = 0;

		uint minsize = IsDXTFormat(_imginfo.Format) ? 4 : 0;

		while(skiplevels < (miplevels - 1) &&
				(imginfo.Width > minsize &&
				 imginfo.Height > minsize &&
				 imginfo.Width > maxsize ||
				 imginfo.Height > maxsize))
		{
			imginfo.Width = enf_max<uint>(imginfo.Width >> 1, 1U);
			imginfo.Height = enf_max<uint>(imginfo.Height >> 1, 1U);
			skiplevels++;
		}

		ERESULT res = imageData.Create(imginfo.Width, imginfo.Height, miplevels - skiplevels, imginfo.Format, flags | (imginfo.Cube ? TEXTR_CUBE : 0), MAP_UNKNOWN);

		enf_assert(res == EE_OK);
		if(res != EE_OK)
			return res;

		uint levels = enf_min(imageData.GetNumLevels(), imginfo.MipLevels);

		for(int face = 0; face < (imginfo.Cube ? 6 : 1); face++)
		{
			int width = _imginfo.Width;
			int height = _imginfo.Height;

			//skip unwanted miplevels
			uint mip = 0;
			if(skiplevels)
			{
				size_t seek = 0;
				for(; mip < skiplevels; mip++)
				{
					seek += GetSourceSurfaceSize(imginfo.Format, imginfo.Depth, width, height);

					width = enf_max(width / 2, 1);
					height = enf_max(height / 2, 1);
				}
				instream.Skip(seek);
			}

			for(; mip < (levels + skiplevels); mip++)
			{
				LockedImageRect lrect;

				uint size = GetSourceSurfaceSize(imginfo.Format, imginfo.Depth, width, height);
				if(size == 0 || instream.GetSize() < size)
					break;

				if(imageData.Lock(face, mip - skiplevels, lrect))
				{
					LoadSurface(lrect, imginfo, width, height, instream);
					imageData.Unlock(face, mip - skiplevels);
				}
				width = enf_max(width / 2, 1);
				height = enf_max(height / 2, 1);
			}
		}
		return EE_OK;
	}
};



//-----------------------------------------------------------------------------
ENF_INLINE void RepackData(const ImageInfo& imginfo, uchar *data, uint size)
{
	#ifdef EXPORT_IN_PLATFORM_ORDER
		return;
	#endif

	switch(imginfo.Format)
	{
		//case TEXFMT_CTX1:	/*no not swap CTX1, they are already in correct order*/
		case TEXFMT_DXT1:
		case TEXFMT_DXT3:
		case TEXFMT_DXT5:			
#ifndef ENF_PS3
			LE_SHORT_ARRAY((ushort*)data, size / 2); 
#endif
			break;

		case TEXFMT_A8R8G8B8:	
		case TEXFMT_X8R8G8B8:
			LE_INT_ARRAY((uint*) data , size/4);
			break;

		case TEXFMT_A8L8:
			LE_SHORT_ARRAY((ushort*)data, size/2);
			break;

		case TEXFMT_L8:
		case TEXFMT_A8:
		case TEXFMT_A32B32G32R32F:
			LE_FLOAT_ARRAY((ushort*)data, size/4);
			break;

		default:
			enf_assert(!"Unsupported format");
			break;
	}
}

//-----------------------------------------------------------------------------
ERESULT PUBLIC_API GetDDSImageInfo(const XSURFACEDESC &desc, ImageInfo& info)
{
	if(desc.dwFlags & XDDSD_MIPMAPCOUNT)
		info.MipLevels = desc.dwMipMapCount;
	else
		info.MipLevels = 1;

	if(desc.dwFlags & XDDSD_WIDTH && desc.dwFlags & XDDSD_HEIGHT)
	{
		info.Width = desc.dwWidth;
		info.Height = desc.dwHeight;
	}
	else
		return EE_ERROR; //what else?

	info.Cube = false;

	if(desc.dwFlags & XDDSD_CAPS)
	{
		if(desc.ddsCaps.dwCaps & XDDSCAPS_COMPLEX)
		{
			if(desc.ddsCaps.dwCaps2 & XDDSCAPS2_CUBEMAP)
			{
				info.Cube = true;
			}
		}
	}

	//there is always at least one
	if(info.MipLevels == 0)
		info.MipLevels = 1;

	if(desc.ddpfPixelFormat.dwFlags & XDDPF_FOURCC)
	{
		switch(desc.ddpfPixelFormat.dwFourCC)
		{
		case ENF_MAKEFOURCC_CTX1:
			info.Compressed = true;
			info.Format = TEXFMT_CTX1;
			info.Depth = 16;
			return EE_OK;

		case ENF_MAKEFOURCC('D', 'X', 'T', '1'):
			info.Compressed = true;
			info.Format = TEXFMT_DXT1;
			info.Depth = 16;
			return EE_OK;

		case ENF_MAKEFOURCC('D', 'X', 'T', '2'):
			info.Compressed = true;
			info.Format = TEXFMT_DXT2;
			info.Depth = 32;
			return EE_OK;

		case ENF_MAKEFOURCC('D', 'X', 'T', '3'):
			info.Compressed = true;
			info.Format = TEXFMT_DXT3;
			info.Depth = 32;
			return EE_OK;

		case ENF_MAKEFOURCC('D', 'X', 'T', '4'):
			info.Compressed = true;
			info.Format = TEXFMT_DXT4;
			info.Depth = 32;
			return EE_OK;

		case ENF_MAKEFOURCC('D', 'X', 'T', '5'):
			info.Compressed = true;
			info.Format = TEXFMT_DXT5;
			info.Depth = 32;
			return EE_OK;

		case 116: //cannot use value from headers as it differs on xbox/win
			info.Compressed = false;
			info.Format = TEXFMT_A32B32G32R32F;
			info.Depth = 128;
			return EE_OK;
		}
		return EE_ERROR;
	}

	if(desc.ddpfPixelFormat.dwFlags & (XDDPF_RGB|2))
	{
		switch(desc.ddpfPixelFormat.dwRGBBitCount)
		{
		case 8:
			if(desc.ddpfPixelFormat.dwRGBAlphaBitMask == 0x00 &&
				desc.ddpfPixelFormat.dwRBitMask == 0xff &&
				desc.ddpfPixelFormat.dwGBitMask == 0x00 &&
				desc.ddpfPixelFormat.dwBBitMask == 0x00)
			{
				info.Compressed = false;
				info.Format = TEXFMT_L8;
				info.Depth = 8;
				return EE_OK;
			}

			if(desc.ddpfPixelFormat.dwRGBAlphaBitMask == 0xff &&
				desc.ddpfPixelFormat.dwRBitMask == 0x00 &&
				desc.ddpfPixelFormat.dwGBitMask == 0x00 &&
				desc.ddpfPixelFormat.dwBBitMask == 0x00)
			{
				info.Compressed = false;
				info.Format = TEXFMT_A8;
				info.Depth = 8;
				return EE_OK;
			}
			break;

		case 16:
			if(desc.ddpfPixelFormat.dwRGBAlphaBitMask == 0xf000 &&
				desc.ddpfPixelFormat.dwRBitMask == 0x0f00 &&
				desc.ddpfPixelFormat.dwGBitMask == 0x00f0 &&
				desc.ddpfPixelFormat.dwBBitMask == 0x000f)
			{
				info.Compressed = false;
				info.Format = TEXFMT_A4R4G4B4;
				info.Depth = 16;
				return EE_OK;
			}
			break;

		case 24:
			if(desc.ddpfPixelFormat.dwRBitMask == 0x00ff0000 &&
				desc.ddpfPixelFormat.dwGBitMask == 0x0000ff00 &&
				desc.ddpfPixelFormat.dwBBitMask == 0x000000ff)
			{
				info.Compressed = false;
				info.Format = TEXFMT_X8R8G8B8;
				info.Depth = 24;
				return EE_OK;
			}
			break;

		case 32:
			//if((desc.ddpfPixelFormat.dwFlags & XDDPF_ALPHAPIXELS) == 0) ???
			if(desc.ddpfPixelFormat.dwRGBAlphaBitMask == 0x00000000 &&
				desc.ddpfPixelFormat.dwRBitMask == 0x00ff0000 &&
				desc.ddpfPixelFormat.dwGBitMask == 0x0000ff00 &&
				desc.ddpfPixelFormat.dwBBitMask == 0x000000ff)
			{
				info.Compressed = false;
				info.Format = TEXFMT_X8R8G8B8;
				info.Depth = 32;
				return EE_OK;
			}

			if(desc.ddpfPixelFormat.dwRGBAlphaBitMask == 0xff000000 &&
				desc.ddpfPixelFormat.dwRBitMask == 0x00ff0000 &&
				desc.ddpfPixelFormat.dwGBitMask == 0x0000ff00 &&
				desc.ddpfPixelFormat.dwBBitMask == 0x000000ff)
			{
				info.Compressed = false;
				info.Format = TEXFMT_A8R8G8B8;
				info.Depth = 32;
				return EE_OK;
			}

			if(desc.ddpfPixelFormat.dwRGBAlphaBitMask == 0xff000000 &&
				desc.ddpfPixelFormat.dwRBitMask == 0x000000ff &&
				desc.ddpfPixelFormat.dwGBitMask == 0x0000ff00 &&
				desc.ddpfPixelFormat.dwBBitMask == 0x00ff0000)
			{
				info.Compressed = false;
				info.Format = TEXFMT_A8R8G8B8;
				info.Depth = 32;
				return EE_OK;
			}
			break;
		}
	}
	else if(desc.ddpfPixelFormat.dwFlags & XDDPF_LUMINANCE)
	{
		if(desc.ddpfPixelFormat.dwFlags & XDDPF_ALPHAPIXELS)
		{
			switch(desc.ddpfPixelFormat.dwRGBBitCount)
			{
			case 16:
				if(desc.ddpfPixelFormat.dwRGBAlphaBitMask == 0xff00 &&
					desc.ddpfPixelFormat.dwRBitMask == 0xff &&
					desc.ddpfPixelFormat.dwGBitMask == 0x00 &&
					desc.ddpfPixelFormat.dwBBitMask == 0x00)
				{
					info.Compressed = false;
					info.Format = TEXFMT_A8L8;
					info.Depth = 16;
					return EE_OK;
				}
			}	
		}
		else
		{
			switch(desc.ddpfPixelFormat.dwRGBBitCount)
			{
			case 8:
				if(desc.ddpfPixelFormat.dwRGBAlphaBitMask == 0x00 &&
					desc.ddpfPixelFormat.dwRBitMask == 0xff &&
					desc.ddpfPixelFormat.dwGBitMask == 0x00 &&
					desc.ddpfPixelFormat.dwBBitMask == 0x00)
				{
					info.Compressed = false;
					info.Format = TEXFMT_L8;
					info.Depth = 8;
					return EE_OK;
				}
			}	
		}
	}

	return EE_ERROR;
}


//-----------------------------------------------------------------------------
class DDSLoaderHandler: public ImageLoaderHandler
{
protected:
	//-----------------------------------------------------------------------------
	ERESULT LoadDDSHeader(CInputStream& instream, XSURFACEDESC &desc)
	{
		if(instream.GetSize() < (4 + sizeof(XSURFACEDESC)))
			return EE_UNSUPPORTED;

		unsigned char head[4];
		uint read = instream.Read(head, 4);
		if(read != 4 || memcmp(head, "DDS ", 4) != 0)
		{
			instream.Seek(0);
			return EE_UNSUPPORTED;
		}

		if((instream.GetSize() - instream.GetCurrentPosition()) < sizeof(XSURFACEDESC))
			return EE_UNSUPPORTED;

		instream.Read((unsigned char*)&desc, sizeof(desc));
		desc.dwSize = LE_INT(desc.dwSize);
		desc.dwFlags = LE_INT(desc.dwFlags);
		desc.dwHeight = LE_INT(desc.dwHeight);
		desc.dwWidth = LE_INT(desc.dwWidth);
		desc.lPitch = LE_INT(desc.lPitch);
		desc.dwDepth = LE_INT(desc.dwDepth);
		desc.dwMipMapCount = LE_INT(desc.dwMipMapCount);
		desc.dwAlphaBitDepth = LE_INT(desc.dwAlphaBitDepth);
		desc.dwReserved = LE_INT(desc.dwReserved);
		desc.ddpfPixelFormat.dwSize = LE_INT(desc.ddpfPixelFormat.dwSize);
		desc.ddpfPixelFormat.dwFlags = LE_INT(desc.ddpfPixelFormat.dwFlags);
		desc.ddpfPixelFormat.dwFourCC = LE_INT(desc.ddpfPixelFormat.dwFourCC);
		desc.ddpfPixelFormat.dwRGBBitCount = LE_INT(desc.ddpfPixelFormat.dwRGBBitCount);
		desc.ddpfPixelFormat.dwRBitMask = LE_INT(desc.ddpfPixelFormat.dwRBitMask);
		desc.ddpfPixelFormat.dwGBitMask = LE_INT(desc.ddpfPixelFormat.dwGBitMask);
		desc.ddpfPixelFormat.dwBBitMask = LE_INT(desc.ddpfPixelFormat.dwBBitMask);
		desc.ddpfPixelFormat.dwRGBAlphaBitMask = LE_INT(desc.ddpfPixelFormat.dwRGBAlphaBitMask);
		desc.ddsCaps.dwCaps = LE_INT(desc.ddsCaps.dwCaps);
		desc.ddsCaps.dwCaps2 = LE_INT(desc.ddsCaps.dwCaps2);
		desc.ddsCaps.dwCaps3 = LE_INT(desc.ddsCaps.dwCaps3);
		desc.ddsCaps.dwCaps4 = LE_INT(desc.ddsCaps.dwCaps4);

		if(desc.dwSize != sizeof(XSURFACEDESC))
		{
			instream.Seek(0);
			return EE_UNSUPPORTED;
		}

		return EE_OK;	
	}


	//-----------------------------------------------------------------------------
	ERESULT GetImageInfo(CInputStream& instream, ImageInfo& info, bool littleEndian)
	{
		ERESULT res;
		XSURFACEDESC desc;
		
		if ((res = LoadDDSHeader(instream, desc)) != EE_OK)
		{
			return res;
		}

		//this isn't necessary if DDSCompLoader is registered before DDSLoader
/*		if (desc.dwReserved == ENF_MAKEFOURCC('E', 'N', 'F', '1'))
		{
			instream.Seek(0);
			return EE_UNSUPPORTED;
		}
*/		
		return GetDDSImageInfo(desc, info);
	}


public:
	DDSLoaderHandler() {}
	ERESULT GetLoader(CInputStream& instream, ImageFormatLoader** loader, ImageInfo& info)
	{
		ERESULT res = GetImageInfo(instream, info, true);
		if(res == EE_OK)
		{
			*loader = ENF_NEW DDSLoader();
		}
		return res;
	}

	virtual const char* GetExtension() const
	{
		return ".dds";
	}
};


#ifdef ENF_DONE
extern int lzo1x_decompress_stream ( CByteInputStream<>& stream, uchar* out, uint* out_len);


//-----------------------------------------------------------------------------
class DDSCompLoader: public DDSLoader
{
protected:

	/*!
	surface header in file
	*/
	struct ENFSurfaceHeader
	{
		enum
		{
			EFS_NONE = ENF_MAKEFOURCC('C','O','P','Y'),
			EFS_ZIP = ENF_MAKEFOURCC('Z','L','I','B'),
			EFS_LZO = ENF_MAKEFOURCC('L','Z','O',' '),
			#ifdef LZX_COMPRESSION
				EFS_LZX = ENF_MAKEFOURCC('L', 'Z', 'X', ' ')
			#endif
		};
		uint		m_iFlags;
		uint		m_iSize;	
	};

public:
	ENF_DECLARE_SMALLOBJECT(DDSCompLoader);

	DDSCompLoader() {}

	ERESULT Load(CInputStream& instream, const ImageInfo& imginfo, uint miplevels, uint flags, uint maxsize, ImageData& imageData)
	{
		if (!miplevels)
			miplevels = imginfo.MipLevels;

		if (flags & TEXTR_AUTOMIPMAP)
			miplevels = 1;

		//miplevels is 1 when needed just one mipmap, otherwise number of mipmaps = imginfo.MipLevels
		uint maxmip = imginfo.MipLevels;
				
		//find height, width of texture and um of mipmaps
		uint minsize = IsDXTFormat(imginfo.Format) ? 4 : 1;
		uint width	 = enf_max(minsize, imginfo.Width);
		uint height  = enf_max(minsize, imginfo.Height);
		
		//update size and neede mipmaps according the maximum size defined by engine and number of wanted mipmaps in miplevels
		maxsize = enf_max(maxsize, 4u);

		if (miplevels != 1)
		{
			while (width > maxsize || height > maxsize)	
			{
				if (width <= 4 || height <= 4)
				{
					break;
				}

				width  = width >> 1;
				height = height >> 1;

				maxmip--;
			} 
		}

		miplevels = enf_min(miplevels, maxmip);
		enf_assert(maxmip <= imginfo.MipLevels);

		//create texture	
		ERESULT res = imageData.Create(width, height, miplevels, imginfo.Format, flags | (imginfo.Cube ? TEXTR_CUBE : 0), MAP_UNKNOWN);

		enf_assert(res == EE_OK);
		if(res != EE_OK)
		{
			#ifdef ENF_DEVELOPER
				LogF (LL_NORMAL, "Cannot create image data %dx%d (%d)", width, height, imginfo.Format);
			#endif

			return res;
		}
	
		uint DXT = 
			(imginfo.Format == TEXFMT_CTX1 ||
			 imginfo.Format == TEXFMT_DXT1 ||
			 #ifdef ENF_XENON
				imginfo.Format == TEXFMT_DXT2 ||
				imginfo.Format == TEXFMT_DXT4 ||
			 #endif
			 imginfo.Format == TEXFMT_DXT3 ||		 
			 imginfo.Format == TEXFMT_DXT5) ? 4 : 1;		
	
		//load surfaces header
		enf_assert(instream.GetCurrentPosition() == sizeof(XSURFACEDESC) + 4 && imginfo.MipLevels < 15);

		ENFSurfaceHeader* header = ALLOCA(ENFSurfaceHeader, imginfo.MipLevels);
		instream.Read((uchar*) header, sizeof(ENFSurfaceHeader)*imginfo.MipLevels);
		for(uint n = 0; n < imginfo.MipLevels; n++)
		{
			header[n].m_iFlags = LE_INT(header[n].m_iFlags);
			header[n].m_iSize = LE_INT(header[n].m_iSize);
		}

		//alloc memory for decompression, max available size of maximum mipmap
		uint surfacesize = GetSourceSurfaceSize(imginfo.Format, imginfo.Depth, width, height);
		uint maxdatasize = (imginfo.Cube ? 6 : 1) * surfacesize + 128;
		
		uchar* compdata = NULL;
		uchar* decdata  = NULL;
	
		//load necessary surfaces	
		uint maxfaces = imginfo.Cube ? 6 : 1;
		
		//where to begin with reading
		uint skipmipmaps = maxmip - miplevels;
		
		//seek into another starting mipmap
		uint skip = 0;
		if (skipmipmaps)
		{
			for (uint i = 0; i < skipmipmaps; i++)
			{
				skip += header[i].m_iSize;
			}

			instream.Skip(skip);
		}

		//need to read minimally one mipmap
		enf_assert(skipmipmaps < maxmip);

		bool loadedok = false;

		//read all necessary mipmaps
		for (uint level = skipmipmaps; level < maxmip; level++)
		{
			//unpack mipmap header
			uint headerFlags = header[level].m_iFlags;
			uint headerSize = header[level].m_iSize;

			#ifdef LZX_COMPRESSION
				enf_assert(
					headerFlags == ENFSurfaceHeader::EFS_NONE ||
					headerFlags == ENFSurfaceHeader::EFS_ZIP ||
					headerFlags == ENFSurfaceHeader::EFS_LZO ||	
					headerFlags == ENFSurfaceHeader::EFS_LZX);
			#else
				enf_assert(
					headerFlags == ENFSurfaceHeader::EFS_NONE ||
					headerFlags == ENFSurfaceHeader::EFS_ZIP ||
					headerFlags == ENFSurfaceHeader::EFS_LZO);
			#endif
		
			uint actmip = maxmip - 1 - level;
			
			uint currwidth  = enf_max(1u, width >> actmip);
			uint currheight = enf_max(1u, height >> actmip);
			
			//lock data	
			LockedImageRect lrect;
			uint rowbytes;
			uchar *datastart;

			//computing size of surface for test if to copy directly into texture
			uint currsurfacesize = GetSourceSurfaceSize(imginfo.Format, imginfo.Depth, currwidth, currheight);
			uint directrowbytes = currsurfacesize*DXT/currheight;

			#define LOCK(face)	if (!imageData.Lock(face, actmip, lrect))	{ instream.Skip(headerSize); loadedok = false; continue; } rowbytes = lrect.Pitch; datastart = (uchar*) lrect.pBits;
			#define UNLOCK(face) imageData.Unlock(face, actmip);

			//type of compression
			switch (headerFlags)
			{
				//-----------------------------------------------------------------------------
				// ZLIB compression - not highly optimized, should be better, but
				// due to worse decompression time we will not use it
				//-----------------------------------------------------------------------------	
				case ENFSurfaceHeader::EFS_ZIP :
				{

#define USE_Z_STREAM

#ifdef USE_Z_STREAM
					CByteInputStream<> byteInStream(instream, headerSize);
#else
					if (!compdata)
					{
						compdata = (uchar*) ENF_NEW uchar[maxdatasize];
					}
				
					//shit, copying into another buffer
					instream.Read(compdata, headerSize);
#endif

					//for RGB format, decompress into temporal buffer
					if (imginfo.Depth == 24)
					{
						//decompress into another
						uint destlen = maxdatasize;				
						
						if (!decdata)
						{
							decdata = (uchar*) ENF_NEW uchar[maxdatasize];
						}
						
						int res = uncompress((Bytef*) decdata, (uLongf*) &destlen, compdata, headerSize);
						loadedok = res == Z_OK && destlen == currsurfacesize;	

						enf_assert(loadedok);
	
						if (loadedok)
						{
							//load data into memory
							uint facesize = destlen/maxfaces;
							enf_assert (facesize == destlen);
							
							for (uint i = 0; i < maxfaces; i++)		
							{
								LOCK(i)							
								CBufferInputStream inStream(decdata + facesize*i, facesize);
								LoadSurface(lrect, imginfo, currwidth, currheight, inStream);
								UNLOCK(i)
							}
						}
					}
					else
					{
						LOCK(0)

						bool fullsurface = rowbytes == directrowbytes;
						
						if (fullsurface)
						{
							rowbytes = currsurfacesize;
						}

						z_stream stream;
#ifdef USE_Z_STREAM
						stream.next_in  = (Bytef *) &byteInStream;
#else
						stream.next_in  = (Bytef*) compdata;
#endif

						stream.avail_in = headerSize;

						stream.next_out  = (Bytef*) datastart;
						stream.avail_out = rowbytes;

						stream.zalloc = (alloc_func)0;
						stream.zfree = (free_func)0;

						if (inflateInit(&stream) != Z_OK)
						{
							UNLOCK(0)
							loadedok = false;

							break;
						}


						uint currrow = 0;
						uint face = 0;
						int res = 0;
											
						loadedok = true;
						while (stream.avail_in && loadedok)
						{
#ifdef USE_Z_STREAM
							res = inflate_stream(&stream, Z_SYNC_FLUSH);
#else
							res = inflate(&stream, Z_SYNC_FLUSH);
#endif

							loadedok = (res == Z_OK && stream.avail_out == 0) || (res == Z_STREAM_END && stream.avail_in == 0) || res == 0;
							enf_assert(loadedok);

							if (loadedok)
							{
								RepackData(imginfo, datastart, rowbytes);

								currrow++;

								if (imginfo.Cube && (fullsurface || currrow == currheight))
								{
									if (face >= 5)
									{
										break;
									}
								
									UNLOCK(face);
									
									currrow = 0;
									face++;						
									
									LOCK(face)

									if (fullsurface)
									{
										rowbytes = currsurfacesize;
									}
								}

								stream.next_out  = (Bytef*) datastart + rowbytes*currrow;
								stream.avail_out = rowbytes;
							}
						};		
						enf_assert(!loadedok || (loadedok && stream.avail_in == 0));
								
						inflateEnd(&stream);
						
						UNLOCK(face);
					}
				}
				break;

				//-----------------------------------------------------------------------------
				// LZO compression 
				//-----------------------------------------------------------------------------	
				case ENFSurfaceHeader::EFS_LZO:
				{
					//can I go exactly into surface? lock surface
					LOCK(0)
					
					CByteInputStream<> byteInStream(instream, headerSize);
					//take pitch, test for direct copy
					if (rowbytes == directrowbytes && maxfaces == 1)
					{
						uint destlen = rowbytes*currheight;
						enf_assert(headerSize <= instream.GetSize());
	
						res = lzo1x_decompress_stream(byteInStream, datastart, &destlen);			

						uint surfaceSize = GetSourceSurfaceSize(imginfo.Format, imginfo.Depth, currwidth, currheight);
						loadedok = res == LZO_E_OK && destlen == surfaceSize;
						enf_assert(loadedok);

						if (loadedok)
						{
							RepackData(imginfo, datastart, destlen);
						}

						UNLOCK(0)												
					}
					else
					{
						//decompress into another
						uint destlen = maxdatasize;				
						
						if (!decdata)
						{
							decdata = (uchar*) ENF_NEW uchar[maxdatasize];
						}

						int res = lzo1x_decompress_stream(byteInStream, decdata, &destlen);

						loadedok = res == LZO_E_OK && destlen == GetSourceSurfaceSize(imginfo.Format, imginfo.Depth, currwidth, currheight)*maxfaces;
						enf_assert(loadedok);

						if (loadedok)
						{
							//load data into memory
							uint facesize = destlen/maxfaces;

							for (uint i = 0; i < maxfaces; i++)		
							{
								if (i) LOCK(i)
								
									CBufferInputStream inStream(decdata + facesize*i, facesize);
									LoadSurface(lrect, imginfo, currwidth, currheight, inStream);
								UNLOCK(i)
							}
						}
						else
						{
							UNLOCK(0)
						}
					}
				}
				break;

				//-----------------------------------------------------------------------------
				// LZX compression, currently implementation from XBOX360
				//-----------------------------------------------------------------------------	
#ifdef LZX_COMPRESSION

				case ENFSurfaceHeader::EFS_LZX:
				{	
					//can I go exactly into surface? lock surface
					LOCK(0)

					XMEMDECOMPRESSION_CONTEXT lzxcontext = NULL;

					// Create a decompression context.
					if (FAILED(XMemCreateDecompressionContext(XMEMCODEC_DEFAULT, NULL, 0, &lzxcontext)))
					{
						UNLOCK(0)
						loadedok = false;
						break;
					}

					//read packed data from stream into temporally buffer
					if (!compdata)
					{
						compdata = (uchar*) ENF_NEW uchar[maxdatasize];
					}

					if (instream.Read(compdata, headerSize) != headerSize)
					{
						UNLOCK(0)
						loadedok = false;
						break;
					}

					//take pitch, test for direct copy
					if (rowbytes == directrowbytes && maxfaces == 1)
					{
						uint destlen = currsurfacesize;

						loadedok = XMemDecompress(lzxcontext, datastart, (SIZE_T*) &destlen, compdata, headerSize) >= 0 && destlen == currsurfacesize;
						enf_assert(loadedok);

						if (loadedok)
						{
							RepackData(imginfo, datastart, destlen);
						}

						UNLOCK(0)												
					}
					else
					{
						//decompress into another
						uint destlen = maxdatasize;				

						if (!decdata)
						{
							decdata = (uchar*) ENF_NEW uchar[maxdatasize];
						}

						loadedok = SUCCEEDED(XMemDecompress(lzxcontext, datastart, (SIZE_T*) &destlen, compdata, headerSize)) && destlen == GetSourceSurfaceSize(imginfo.Format, imginfo.Depth, currwidth, currheight)*maxfaces;
						enf_assert(loadedok);

						if (loadedok)
						{
							//load data into memory
							uint facesize = destlen/maxfaces;

							for (uint i = 0; i < maxfaces; i++)		
							{
								if (i) LOCK(i)

									CBufferInputStream inStream(decdata + facesize*i, facesize);
								LoadSurface(lrect, imginfo, currwidth, currheight, inStream);
								UNLOCK(i)
							}
						}
						else
						{
							UNLOCK(0)
						}
					}

					if (lzxcontext)
					{
						XMemDestroyDecompressionContext( lzxcontext );
					}
				}
				break;
#endif
				
				//-----------------------------------------------------------------------------
				// no compression 
				//-----------------------------------------------------------------------------	
				case ENFSurfaceHeader::EFS_NONE:
				{	
					loadedok = headerSize/maxfaces == GetSourceSurfaceSize(imginfo.Format, imginfo.Depth, currwidth, currheight);

					enf_assert(loadedok);

					if (loadedok)
					{
						for (uint i = 0; i < maxfaces; i++)
						{
							LOCK(i)
								LoadSurface(lrect, imginfo, currwidth, currheight, instream);
							UNLOCK(i)				
						}
					}
				}
			}

			//test for load correctness
			if (!loadedok)
			{
				break;
			}
		}	

		//all read
		enf_assert(!loadedok || (loadedok && ((maxmip < imginfo.MipLevels) ||(imginfo.MipLevels == maxmip && instream.GetSize() == 0))));
		
		#ifdef ENF_DEVELOPER
			if (!loadedok)
			{
				LogF(LL_WARNING, "Texture data were not correctly loaded");
			}
		#endif

		SAFE_DELETE_ARRAY(decdata);
		SAFE_DELETE_ARRAY(compdata);

//		return instream.GetSize() == 0 ? EE_OK : EE_ERROR;
		return loadedok ? EE_OK : EE_ERROR;
	}
	
};

//-----------------------------------------------------------------------------
class DDSCompLoaderHandler: public DDSLoaderHandler
{
protected:
	//-----------------------------------------------------------------------------
	ERESULT GetImageInfo(CInputStream& instream, ImageInfo& info)
	{
		ERESULT res;
		XSURFACEDESC desc;

		if ((res = LoadDDSHeader(instream, desc)) != EE_OK)
		{
			return res;
		}
		
		//compressed dds texture
		if (desc.dwReserved != ENF_MAKEFOURCC('E', 'N', 'F', '1'))
		{
			instream.Seek(0);
			return EE_UNSUPPORTED;
		}

		return GetDDSImageInfo(desc, info);
	}
	
public:
	DDSCompLoaderHandler() {}

	ERESULT GetLoader(CInputStream& instream, ImageFormatLoader** loader, ImageInfo& info)
	{
		ERESULT res = GetImageInfo(instream, info);
		if(res == EE_OK)
		{
			*loader = ENF_NEW DDSCompLoader();
		}
		return res;
	}
};
#endif

//-----------------------------------------------------------------------------
class TGALoader: public ImageFormatLoader
{
protected:
	friend class TGALoaderHandler;
	uint m_uBytes;
	bool m_bRLE;
	bool m_bTopToBottom;
	bool m_bLeftToRight;

public:
	ENF_DECLARE_SMALLOBJECT(TGALoader);
	TGALoader(): m_uBytes(0), m_bRLE(false), m_bTopToBottom(false), m_bLeftToRight(false) {}
	
	ERESULT Load(CInputStream& instream, const ImageInfo& imginfo, uint miplevels, uint flags, uint maxsize, ImageData& imageData)
	{
		uint twidth = imginfo.Width;
		uint theight = imginfo.Height;

		uint m_Pitch   = twidth * m_uBytes;

		enf_assert(!(flags & TEXTR_CUBE));

		if(imageData.Create(imginfo.Width, imginfo.Height, miplevels, imginfo.Format, flags, MAP_UNKNOWN) != EE_OK)
			return EE_ERROR;

		LockedImageRect lrect;

		if(!imageData.Lock(0, 0, lrect))
			return EE_ERROR;

		ubyte* rowData = ALLOCA(ubyte, twidth * 4);

		CByteInputStream<> bytestream (instream);

		if(!m_bRLE && m_bLeftToRight )
		{
			for(uint v = 0; v < theight; v++)
			{
				ubyte* dest;

				if(m_bTopToBottom)
					dest = ((ubyte*)lrect.pBits + v * lrect.Pitch);
				else
					dest = ((ubyte*)lrect.pBits + (theight - v - 1) * lrect.Pitch);

				if(imginfo.Depth == 24)
				{
				//convert from 24-bit R8G8B8 to 32-bit X8R8G8B8
					bytestream.Read(rowData, twidth * 3);
					for(uint h = 0; h < twidth; h++)
					{
#if ENF_INTEGER_BIG_ENDIAN
						dest[h * 4 + 0] = 0xff;
						dest[h * 4 + 1] = rowData[h * 3 + 2];
						dest[h * 4 + 2] = rowData[h * 3 + 1];
						dest[h * 4 + 3] = rowData[h * 3 + 0];
#else
						dest[h * 4 + 0] = rowData[h * 3 + 0];
						dest[h * 4 + 1] = rowData[h * 3 + 1];
						dest[h * 4 + 2] = rowData[h * 3 + 2];
						dest[h * 4 + 3] = 0xff;
#endif
					}
				}
				else
				{
					bytestream.Read(dest, twidth * 4);
					LE_INT_ARRAY((uint*)dest, twidth);
				}
			}
		}
		else
		{
			// Image data is compressed, or does not have origin at top-left
			ubyte* pData = (ubyte*)lrect.pBits;
			ubyte* pbDestY = m_bTopToBottom ? pData : (pData + (theight - 1) * lrect.Pitch);

			for(uint uY = 0; uY < theight; uY++)
			{
				ubyte* pbDestX = m_bLeftToRight ? pbDestY : (pbDestY + lrect.Pitch - m_uBytes);

				for(uint uX = 0; uX < twidth; )
				{
					bool bRunLength;
					uint uCount;
					ubyte rleData[4];

					if(m_bRLE)
					{
						//if(bytestream.Read(rleData, 1) < 1)
						//	break;
						rleData[0] = bytestream.GetUChar();

						bRunLength = (rleData[0] & 0x80) != 0;
						uCount = (rleData[0] & 0x7f) + 1;
						if(bRunLength)
						{
							if(bytestream.Read(rleData, m_uBytes) < m_uBytes)
								break;
							if(imginfo.Depth == 24)
							{
#if ENF_INTEGER_BIG_ENDIAN
								rleData[3] = rleData[0];
								rleData[0] = 0xff;
								enf_swap(rleData[1], rleData[2]);
#else
								rleData[3] = 0xff;
#endif
							}
							else
							{
								LE_INT_ARRAY((uint*)rleData, 1);
							}
						}
					}
					else
					{
						uCount = twidth;
					}
					uX += uCount;
					
					if(bRunLength)
					{
						while(uCount--)
						{
							for(uint b = 0; b < m_uBytes; b++)
								pbDestX[b] = rleData[b];
							pbDestX = m_bLeftToRight ? (pbDestX + m_uBytes) : (pbDestX - m_uBytes);
						}
					}
					else
					{
						if(m_bLeftToRight && imginfo.Depth == 32)
						{
							if(bytestream.Read(pbDestX, m_uBytes * uCount) < (m_uBytes * uCount))
								break;
							LE_INT_ARRAY((uint*)pbDestX, uCount);
							pbDestX += m_uBytes * uCount;
							uCount = 0;
						}
						else
						{
							while(uCount--)
							{
								if(bytestream.Read(pbDestX, m_uBytes) < m_uBytes)
									break;

								pbDestX = m_bLeftToRight ? (pbDestX + m_uBytes) : (pbDestX - m_uBytes);
							}
						}
					}
				}

				pbDestY = m_bTopToBottom ? (pbDestY + m_Pitch) : (pbDestY - m_Pitch);
			}
		}

		imageData.Unlock(0, 0);
		imageData.Finnish();
		return EE_OK;
	}
};

//-----------------------------------------------------------------------------
class TGALoaderHandler: public ImageLoaderHandler
{
protected:
	ERESULT GetTGAImageInfo(CInputStream& instream, ImageFormatLoader** loader, ImageInfo& info)
	{
		#pragma pack(1)
		struct TGAHEADER
		{
			ubyte IDLength;
			ubyte ColormapType;
			ubyte ImageType;

			ushort wColorMapIndex;
			ushort wColorMapLength;
			ubyte bColorMapBits;

			ushort wXOrigin;
			ushort wYOrigin;
			ushort wWidth;
			ushort wHeight;
			ubyte PixelDepth;
			ubyte ImageDescriptor;
		};
		#pragma pack()

		TGAHEADER FH;

		if( instream.GetSize() < sizeof(FH) )
			return EE_UNSUPPORTED;

		if( instream.Read((unsigned char*)&FH, sizeof(FH)) != sizeof(FH))
			return EE_UNSUPPORTED;

		if( FH.ColormapType & ~0x01 )
			return EE_UNSUPPORTED;

		if( FH.ImageType & ~0x0b )
			return EE_UNSUPPORTED;

		if( !FH.wWidth || !FH.wHeight )
			return EE_UNSUPPORTED;

		// Colormap size and format
		uint uColorMapBytes = ((uint) FH.bColorMapBits + 7) >> 3;
		info.Format = TEXFMT_UNKNOWN;
		info.Cube = false;

		switch(FH.ImageType & 0x03)
		{
		case 1:
			if(FH.ColormapType)
			{
				switch(FH.bColorMapBits)
				{
				case 15:
					info.Depth = 15;
					info.Format = TEXFMT_X1R5G5B5;
					break;

				case 16:
					info.Depth = 16;
					info.Format = TEXFMT_A1R5G5B5;
					break;

				case 24:
					info.Depth = 24;
					info.Format = TEXFMT_X8R8G8B8;
					break;

				case 32:
					info.Depth = 32;
					info.Format = TEXFMT_A8R8G8B8;
					break;

				default:
					return EE_UNSUPPORTED;
				}
			}
			else
				return EE_UNSUPPORTED;

			break;

		case 2:
			switch(FH.PixelDepth)
			{
			case 15:
				info.Format = TEXFMT_X1R5G5B5;
				info.Depth = 16;
				break;

			case 16:
				info.Format = TEXFMT_A1R5G5B5;
				info.Depth = 16;
				break;
			
			case 24:
				info.Format = TEXFMT_X8R8G8B8;
				info.Depth = 24;
				break;
			
			case 32:
				info.Format = TEXFMT_A8R8G8B8;
				info.Depth = 32;
				break;
			
			default:
				return EE_UNSUPPORTED;
			}
		break;

		case 3:
			switch(FH.PixelDepth)
			{
			case 8:
				info.Format = TEXFMT_L8;
				break;

			default:
				return EE_UNSUPPORTED;
			}
			break;

			default:
				return EE_UNSUPPORTED;
		}

		info.Width = LE_SHORT(FH.wWidth);
		info.Height = LE_SHORT(FH.wHeight);
		info.MipLevels = 1;
		info.Compressed = false;

		//skip header
		if(FH.IDLength)
		{
			if(instream.GetCurrentPosition() + FH.IDLength > instream.GetSize())
				return EE_ERROR;

			instream.Skip(FH.IDLength);
		}

		TGALoader* ploader = ENF_NEW TGALoader();

		ploader->m_uBytes = ((uint) FH.PixelDepth + 7) >> 3;
		ploader->m_bRLE         = (FH.ImageType & 0x08) != 0;
		ploader->m_bTopToBottom = 0x20 == (FH.ImageDescriptor & 0x20);
		ploader->m_bLeftToRight = 0x10 != (FH.ImageDescriptor & 0x10);
		*loader = ploader;

		return EE_OK;
	}

public:
	TGALoaderHandler() {}
	ERESULT GetLoader(CInputStream& instream, ImageFormatLoader** loader, ImageInfo& info)
	{
		ERESULT res = GetTGAImageInfo(instream, loader, info);
		if(res == EE_OK)
			return res;
		instream.Seek(0);
		return res;
	}

	virtual const char* GetExtension() const
	{
		return ".tga";
	}
};

//-----------------------------------------------------------------------------
class IImageLoader: public ImageLoader
{
protected:
	Array<ImageLoaderHandler*> m_Handlers;
	Set<ImageLoaderHandler*> m_HandlersSet;

	void UpdateExtensionsString();

public:
	IImageLoader();
	~IImageLoader();
	
	void RegisterHandler(ImageLoaderHandler* handler);
	void UnregisterHandler(ImageLoaderHandler* handler);
	
	ERESULT Load(CInputStream& instream, ImageInfo& imginfo, uint minMipLevel, uint maxMiplevels, uint flags, ImageData& imageData);
};

//-----------------------------------------------------------------------------
ERESULT IImageLoader::Load(CInputStream& instream, ImageInfo& imginfo, uint minMiplevel, uint maxMiplevels, uint flags, ImageData& imageData)
{
	ERESULT res = EE_UNSUPPORTED;
	for(uint n = 0; n < m_Handlers.GetCardinality(); n++)
	{
		ImageFormatLoader* loader;
		ImageInfo info;
		res = m_Handlers[n]->GetLoader(instream, &loader, info);
		if(res == EE_OK)
		{
			uint tmax = enf_max<uint>(info.Width, info.Height);
			uint maxAllowed		= MAX_TEXTURE_SIZE;
			uint maxSupported = MAX_TEXTURE_SIZE;

#ifdef ENF_CONSOLE
			if(!IsDXTFormat(info.Format))
				maxAllowed = 256;
#endif

			uint maxsize = (flags & TEXTR_NORESIZE) ? maxSupported : enf_max(enf_min((tmax >> minMiplevel), maxAllowed), 16U);
			if(maxMiplevels == 0)
				maxMiplevels = info.MipLevels;

			res = loader->Load(instream, info, maxMiplevels, flags, maxsize, imageData);
			if(res == EE_OK)
			{
				imginfo = info;
			}
			delete loader;
			break;
		}
	}

	/*
	if (res != 0)
	{
		Sleep(0);
	}*/

	return res;
}

//-----------------------------------------------------------------------------
IImageLoader::IImageLoader()
{
#ifdef ENF_DONE
	RegisterHandler(ENF_NEW DDSCompLoaderHandler());
#endif
	RegisterHandler(ENF_NEW DDSLoaderHandler());
	RegisterHandler(ENF_NEW TGALoaderHandler());
}

//-----------------------------------------------------------------------------
IImageLoader::~IImageLoader()
{
	ForEach<DeleteItem>(m_Handlers);

	m_Handlers.Clear();
	m_HandlersSet.Clear();
}

//-----------------------------------------------------------------------------
void IImageLoader::UpdateExtensionsString()
{
	SAFE_DELETE_ARRAY(m_sExtString);
	m_sExtString = ENF_NEW const char*[(m_Handlers.GetCardinality() + 1)];

	ZeroMem(m_sExtString, (m_Handlers.GetCardinality() + 1)*sizeof(char*));

	int i = 0;
	for (; i < (int) m_Handlers.GetCardinality(); i++)
	{
		const char* currExt = m_Handlers[i]->GetExtension();
		enf_assert(currExt);

		int j = 0;
		for (;; j++)
		{
			if (!m_sExtString[j] || strcmp(m_sExtString[j], currExt) == 0)
			{
				break;				
			}
		}

		if (!m_sExtString[j]) 
			m_sExtString[j] = currExt;
	}
}

//-----------------------------------------------------------------------------
void IImageLoader::RegisterHandler(ImageLoaderHandler* handler)
{
	if(m_HandlersSet.Contains(handler))
		return;

	m_Handlers.Insert(handler);
	m_HandlersSet.Insert(handler);

	UpdateExtensionsString();
}

//-----------------------------------------------------------------------------
void IImageLoader::UnregisterHandler(ImageLoaderHandler* handler)
{
	if(!m_HandlersSet.Contains(handler))
		return;
	m_HandlersSet.Remove(handler);
	m_Handlers.Remove(handler);

	UpdateExtensionsString();
}

//-----------------------------------------------------------------------------
size_t MemoryImageData::GetDataSize() const
{
	return m_iDataSize;
}

//-----------------------------------------------------------------------------
ERESULT MemoryImageData::Create(uint width, uint height, uint levels, TextureFormat format, uint flags, TextureUsage usage)
{
	enf_assert(width != 0);
	enf_assert(height != 0);
	enf_assert(levels != 0);
	enf_assert(format != TEXFMT_UNKNOWN);

	if(m_iNumFaces && m_iLevels)
		Free();

	uint ssize = 0;
	uint lwidth = width;
	uint lheight = height;

	for(uint l = 0; l < levels; l++)
	{
		if(l > 0)
			m_iMipOffsets[l - 1] = ssize;

		ssize += GetSourceSurfaceSize(format, 32, lwidth, lheight);

		lwidth = enf_max(lwidth >> 1, 1U);
		lheight = enf_max(lheight >> 1, 1U);
	}

	uint nfaces = (flags & TEXTR_CUBE) ? 6 : 1;
	for(uint f = 0; f < nfaces; f++)
	{
		m_iUsedMipmaps[f] = 0;
		m_pData[f] = MemoryManager::Alloc(ssize, __FILE__, __LINE__);
	}
	m_iDataSize = ssize * nfaces;
	m_iWidth = width;
	m_iHeight = height;
	m_iLevels = levels;
	m_Format = format;
	m_iNumFaces = nfaces;
	return EE_OK;
}

//-----------------------------------------------------------------------------
bool MemoryImageData::Lock(uint face, uint level, LockedImageRect& info)
{
	enf_assert(face < m_iNumFaces);
	enf_assert(level < m_iLevels);

	if(face >= m_iNumFaces)
		return false;
	if(level >= m_iLevels)
		return false;

	if(m_pData[face] == NULL)
		return false;

	m_iUsedMipmaps[face] |= (1 << level);

	info.pBits = ((ubyte*)m_pData[face] + ((level == 0) ? 0 : m_iMipOffsets[level - 1]));
	info.Pitch = GetSurfaceSize(enf_max((uint)(m_iWidth >> level), 1U), 1, m_Format);

	return true;
}

//-----------------------------------------------------------------------------
void MemoryImageData::Unlock(uint face, uint level)
{
	enf_assert(face < 6);
}

//-----------------------------------------------------------------------------
void MemoryImageData::Finnish()
{
}

//-----------------------------------------------------------------------------
MemoryImageData::MemoryImageData(): m_Format(TEXFMT_UNKNOWN), m_iWidth(0), m_iHeight(0), m_iLevels(0), m_iNumFaces(0), m_iDataSize(0)
{
#ifdef _DEBUG
	ZeroMem(m_pData, sizeof(m_pData));
#endif
}

//-----------------------------------------------------------------------------
void MemoryImageData::Free()
{
	for(uint f = 0; f < m_iNumFaces; f++)
	{
		if(m_pData[f])
		{
			MemoryManager::Free(m_pData[f]);
			m_pData[f] = NULL;
		}
	}

	m_iLevels = 0;
	m_iNumFaces = 0;
}

//-----------------------------------------------------------------------------
MemoryImageData::~MemoryImageData()
{
	Free();
};

//-----------------------------------------------------------------------------
ERESULT MemoryBufferImageData::Create(uint width, uint height, uint levels, TextureFormat format, uint flags, TextureUsage usage, const MemoryBuffer& data, uint offset)
{
	enf_assert(width != 0);
	enf_assert(height != 0);
	enf_assert(levels != 0);
	enf_assert(format != TEXFMT_UNKNOWN);

	uint ssize = 0;
	uint lwidth = width;
	uint lheight = height;

	for(uint l = 0; l < levels; l++)
	{
		if(l > 0)
			m_iMipOffsets[l - 1] = ssize;

		uint size = GetSourceSurfaceSize(format, 32, lwidth, lheight);
		ssize += size;

		lwidth = enf_max(lwidth >> 1, 1U);
		lheight = enf_max(lheight >> 1, 1U);
	}

	uint nfaces = (flags & TEXTR_CUBE) ? 6 : 1;

	for(uint f = 0; f < nfaces; f++)
	{
		m_iUsedMipmaps[f] = 0;
		m_pData[f] = (uchar*) data.GetData() + offset + f*ssize;
	}

	enf_assert(offset + nfaces*ssize <= (uint) data.GetLength());

	m_iDataSize = ssize * nfaces;
	m_iWidth = width;
	m_iHeight = height;
	m_iLevels = levels;
	m_Format = format;
	m_iNumFaces = nfaces;
	return EE_OK;
}

//-----------------------------------------------------------------------------
void MemoryBufferImageData::Free()
{
	for(uint f = 0; f < m_iNumFaces; f++)
	{
		m_pData[f] = NULL;
	}

	m_iLevels = 0;
	m_iNumFaces = 0;
}

//-----------------------------------------------------------------------------
ImageLoader* CreateImageLoader()
{
	return ENF_NEW IImageLoader();
}

}

