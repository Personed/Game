//-----------------------------------------------------------------------------
// File: enf_texture.cpp
//
// Desc: Texture abstraction
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "render/enf_texture.h"
#include "resource/enf_ddsformat.h"
#include "engine/enf_engine.h"
#include "render/enf_irendmain.h"
#include "resource/enf_resourcemanager.h"
#include "render/enf_textureresloader.h"
#include "common/enf_staticbuffer.h"
#include "common/enf_streams.h"
#include "system/enf_vfilesystem.h"

#ifdef ENF_DX9
	#include <d3dx9.h>
#endif

#include "resource/enf_imageloader.h"
#include "common/enf_color.h"

namespace enf
{


namespace TextureCodec
{

typedef void (* TConvertPixels)(void* out, const void* in, uint count);

typedef ushort SColor[4];
typedef ubyte BColor[4];

ENF_INLINE ushort F16(float f) { return (ushort)(f * 65535.0f); }
ENF_INLINE ubyte F8(float f) { return (ubyte)(f * 255.0f); }
ENF_INLINE ubyte F6(float f) { return (ubyte)(f * 63.0f); }
ENF_INLINE ubyte F5(float f) { return (ubyte)(f * 31.0f); }
ENF_INLINE ubyte F4(float f) { return (ubyte)(f * 15.0f); }
ENF_INLINE ubyte F1(float f) { return (ubyte)f; }

ENF_INLINE ushort S16(ushort f) { return (ushort)f; }
ENF_INLINE ubyte S8(ushort f) { return (ubyte)(f >> 8); }
ENF_INLINE ubyte S6(ushort f) { return (ubyte)(f >> 10); }
ENF_INLINE ubyte S5(ushort f) { return (ubyte)(f >> 11); }
ENF_INLINE ubyte S4(ushort f) { return (ubyte)(f >> 12); }
ENF_INLINE ubyte S1(ushort f) { return (ubyte)(f >> 15); }

ENF_INLINE ushort B16(ubyte f) { return (ushort)(f << 8); }
ENF_INLINE ubyte B8(ubyte f) { return f; }
ENF_INLINE ubyte B6(ubyte f) { return f >> 2; }
ENF_INLINE ubyte B5(ubyte f) { return f >> 3; }
ENF_INLINE ubyte B4(ubyte f) { return f >> 4; }
ENF_INLINE ubyte B1(ubyte f) { return f >> 7; }

ENF_INLINE uint V16U16(const FColor& src)
{
	return (F16(src.r) << 16) | F16(src.g);
}
ENF_INLINE uint X8L8V8U8(const FColor& src)
{
	return (F8(src.r) << 16) | (F8(src.g) << 8) | F8(src.b);
}
ENF_INLINE uint A8R8G8B8(const FColor& src)
{
	return (F8(src.a) << 24) | (F8(src.r) << 16) | (F8(src.g) << 8) | F8(src.b);
}
ENF_INLINE uint X8R8G8B8(const FColor& src)
{
	return (F8(src.r) << 16) | (F8(src.g) << 8) | F8(src.b);
}
ENF_INLINE uint A8B8G8R8(const FColor& src)
{
	return (F8(src.a) << 24) | (F8(src.b) << 16) | (F8(src.g) << 8) | F8(src.r);
}
ENF_INLINE ushort L16(const FColor& src)
{
	return F16(src.r) << 16;
}
ENF_INLINE ushort A8L8(const FColor& src)
{
	return (F8(src.a) << 8) | F8(src.r);
}
ENF_INLINE ushort V8U8(const FColor& src)
{
	return (F8(src.r) << 8) | F8(src.g);
}
ENF_INLINE ushort R5G6B5(const FColor& src)
{
	return (F5(src.r) << 11) | (F6(src.g) << 5) | F5(src.b);
}
ENF_INLINE ushort L6V5U5(const FColor& src)
{
	return (F6(src.r) << 10) | (F5(src.g) << 5) | F5(src.b);
}
ENF_INLINE ushort X1R5G5B5(const FColor& src)
{
	return (F1(src.r) << 15) | (F5(src.g) << 10) | (F5(src.b) << 5);
}
ENF_INLINE ushort A1R5G5B5(const FColor& src)
{
	return (F1(src.a) << 15) | (F5(src.r) << 10) | (F5(src.g) << 5) | F5(src.b);
}
ENF_INLINE ushort A4R4G4B4(const FColor& src)
{
	return (F4(src.a) << 12) | (F4(src.r) << 8) | (F4(src.g) << 4) | F4(src.b);
}
ENF_INLINE ubyte A8(const FColor& src)
{
	return F8(src.a);
}
ENF_INLINE ubyte P8(const FColor& src)
{
	return F8(src.r);
}
ENF_INLINE ubyte L8(const FColor& src)
{
	return F8(src.r);
}
//----------
ENF_INLINE uint V16U16(const SColor& src)
{
	return (S16(src[0]) << 16) | S16(src[1]);
}
ENF_INLINE uint X8L8V8U8(const SColor& src)
{
	return (S8(src[0]) << 16) | (S8(src[1]) << 8) | S8(src[2]);
}
ENF_INLINE uint A8R8G8B8(const SColor& src)
{
	return (S8(src[3]) << 24) | (S8(src[0]) << 16) | (S8(src[1]) << 8) | S8(src[2]);
}
ENF_INLINE uint X8R8G8B8(const SColor& src)
{
	return (S8(src[0]) << 16) | (S8(src[1]) << 8) | S8(src[2]);
}
ENF_INLINE uint A8B8G8R8(const SColor& src)
{
	return (S8(src[3]) << 24) | (S8(src[2]) << 16) | (S8(src[1]) << 8) | S8(src[0]);
}
ENF_INLINE ushort L16(const SColor& src)
{
	return S16(src[0]) << 16;
}
ENF_INLINE ushort A8L8(const SColor& src)
{
	return (S8(src[3]) << 8) | S8(src[0]);
}
ENF_INLINE ushort V8U8(const SColor& src)
{
	return (S8(src[0]) << 8) | S8(src[1]);
}
ENF_INLINE ushort R5G6B5(const SColor& src)
{
	return (S5(src[0]) << 11) | (S6(src[1]) << 5) | S5(src[2]);
}
ENF_INLINE ushort L6V5U5(const SColor& src)
{
	return (S6(src[0]) << 10) | (S5(src[1]) << 5) | S5(src[2]);
}
ENF_INLINE ushort X1R5G5B5(const SColor& src)
{
	return (S1(src[0]) << 15) | (S5(src[1]) << 10) | (S5(src[2]) << 5);
}
ENF_INLINE ushort A1R5G5B5(const SColor& src)
{
	return (S1(src[3]) << 15) | (S5(src[0]) << 10) | (S5(src[1]) << 5) | S5(src[2]);
}
ENF_INLINE ushort A4R4G4B4(const SColor& src)
{
	return (S4(src[3]) << 12) | (S4(src[0]) << 8) | (S4(src[1]) << 4) | S4(src[2]);
}
ENF_INLINE ubyte A8(const SColor& src)
{
	return S8(src[3]);
}
ENF_INLINE ubyte P8(const SColor& src)
{
	return S8(src[0]);
}
ENF_INLINE ubyte L8(const SColor& src)
{
	return S8(src[0]);
}

//------

void FConv32_V16U16(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = V16U16(((const FColor*)in)[n]);
}
void FConv32_X8L8V8U8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = X8L8V8U8(((const FColor*)in)[n]);
}
void FConv32_A8R8G8B8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = A8R8G8B8(((const FColor*)in)[n]);
}
void FConv32_X8R8G8B8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = X8R8G8B8(((const FColor*)in)[n]);
}
void FConv32_A8B8G8R8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = A8B8G8R8(((const FColor*)in)[n]);
}
void FConv16_L16(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = L16(((const FColor*)in)[n]);
}
void FConv16_A8L8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = A8L8(((const FColor*)in)[n]);
}
void FConv16_V8U8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = V8U8(((const FColor*)in)[n]);
}
void FConv16_R5G6B5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = R5G6B5(((const FColor*)in)[n]);
}
void FConv16_L6V5U5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = L6V5U5(((const FColor*)in)[n]);
}
void FConv16_X1R5G5B5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = X1R5G5B5(((const FColor*)in)[n]);
}
void FConv16_A1R5G5B5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = A1R5G5B5(((const FColor*)in)[n]);
}
void FConv16_A4R4G4B4(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = A4R4G4B4(((const FColor*)in)[n]);
}
void FConv8_A8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ubyte*)out)[n] = A8(((const FColor*)in)[n]);
}
void FConv8_L8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ubyte*)out)[n] = L8(((const FColor*)in)[n]);
}
//------
void SConv32_V16U16(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = V16U16(((const FColor*)in)[n]);
}
void SConv32_X8L8V8U8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = X8L8V8U8(((const FColor*)in)[n]);
}
void SConv32_A8R8G8B8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = A8R8G8B8(((const FColor*)in)[n]);
}
void SConv32_X8R8G8B8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = X8R8G8B8(((const FColor*)in)[n]);
}
void SConv32_A8B8G8R8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = A8B8G8R8(((const SColor*)in)[n]);
}
void SConv16_L16(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = L16(((const SColor*)in)[n]);
}
void SConv16_A8L8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = A8L8(((const SColor*)in)[n]);
}
void SConv16_V8U8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = V8U8(((const SColor*)in)[n]);
}
void SConv16_R5G6B5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = R5G6B5(((const SColor*)in)[n]);
}
void SConv16_L6V5U5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = L6V5U5(((const SColor*)in)[n]);
}
void SConv16_X1R5G5B5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = X1R5G5B5(((const SColor*)in)[n]);
}
void SConv16_A1R5G5B5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = A1R5G5B5(((const SColor*)in)[n]);
}
void SConv16_A4R4G4B4(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = A4R4G4B4(((const SColor*)in)[n]);
}
void SConv8_A8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ubyte*)out)[n] = A8(((const SColor*)in)[n]);
}
void SConv8_P8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ubyte*)out)[n] = P8(((const SColor*)in)[n]);
}
void SConv8_L8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ubyte*)out)[n] = L8(((const SColor*)in)[n]);
}

//------
void BConv32_V16U16(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = V16U16(((const BColor*)in)[n]);
}
void BConv32_X8L8V8U8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = X8L8V8U8(((const BColor*)in)[n]);
}
void BConv32_A8R8G8B8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = A8R8G8B8(((const BColor*)in)[n]);
}
void BConv32_X8R8G8B8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = X8R8G8B8(((const BColor*)in)[n]);
}
void BConv32_A8B8G8R8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((uint*)out)[n] = A8B8G8R8(((const BColor*)in)[n]);
}
void BConv16_L16(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = L16(((const BColor*)in)[n]);
}
void BConv16_A8L8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = A8L8(((const BColor*)in)[n]);
}
void BConv16_V8U8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = V8U8(((const BColor*)in)[n]);
}
void BConv16_R5G6B5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = R5G6B5(((const BColor*)in)[n]);
}
void BConv16_L6V5U5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = L6V5U5(((const BColor*)in)[n]);
}
void BConv16_X1R5G5B5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = X1R5G5B5(((const BColor*)in)[n]);
}
void BConv16_A1R5G5B5(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = A1R5G5B5(((const BColor*)in)[n]);
}
void BConv16_A4R4G4B4(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ushort*)out)[n] = A4R4G4B4(((const BColor*)in)[n]);
}
void BConv8_A8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ubyte*)out)[n] = A8(((const BColor*)in)[n]);
}
void BConv8_P8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ubyte*)out)[n] = P8(((const BColor*)in)[n]);
}
void BConv8_L8(void* out, const void* in, uint count)
{
	for(uint n = 0; n < count; n++) ((ubyte*)out)[n] = L8(((const BColor*)in)[n]);
}

TConvertPixels ConvertTable[][16] =
{
	{
		//conversions from FColor format
		FConv32_V16U16, FConv32_X8L8V8U8, FConv32_A8R8G8B8, FConv32_X8R8G8B8,
		FConv16_L16, FConv16_A8L8, FConv16_V8U8,
		FConv16_R5G6B5, FConv16_L6V5U5, FConv16_X1R5G5B5, FConv16_A1R5G5B5,
		FConv16_A4R4G4B4, FConv8_A8, FConv8_L8
	},

	{
		//conversions from short format
		SConv32_V16U16, SConv32_X8L8V8U8, SConv32_A8R8G8B8, SConv32_X8R8G8B8,
		SConv16_L16, SConv16_A8L8, SConv16_V8U8,
		SConv16_R5G6B5, SConv16_L6V5U5, SConv16_X1R5G5B5, SConv16_A1R5G5B5,
		SConv16_A4R4G4B4, SConv8_A8, SConv8_L8
	},

	{
		//conversions from byte format
		BConv32_V16U16, BConv32_X8L8V8U8, BConv32_A8R8G8B8, BConv32_X8R8G8B8,
		BConv16_L16, BConv16_A8L8, BConv16_V8U8,
		BConv16_R5G6B5, BConv16_L6V5U5, BConv16_X1R5G5B5, BConv16_A1R5G5B5,
		BConv16_A4R4G4B4, BConv8_A8, BConv8_L8
	}
};

uint GetFormatIndex(TextureFormat fmt, uint* stride)
{
	if(stride) *stride = 0;

	switch(fmt)
	{
	case TEXFMT_V16U16:
		if(stride) *stride = 4;
		return 0;
	case TEXFMT_X8L8V8U8:
		if(stride) *stride = 4;
		return 1;
	case TEXFMT_A8R8G8B8:
		if(stride) *stride = 4;
		return 2;
	case TEXFMT_X8R8G8B8:
		if(stride) *stride = 4;
		return 3;
	case TEXFMT_L16:
		if(stride) *stride = 2;
		return 4;
	case TEXFMT_A8L8:
		if(stride) *stride = 2;
		return 5;
	case TEXFMT_V8U8:
		if(stride) *stride = 2;
		return 6;
	case TEXFMT_R5G6B5:
		if(stride) *stride = 2;
		return 7;
	case TEXFMT_L6V5U5:
		if(stride) *stride = 2;
		return 8;
	case TEXFMT_X1R5G5B5:
		if(stride) *stride = 2;
		return 9;
	case TEXFMT_A1R5G5B5:
		if(stride) *stride = 2;
		return 10;
	case TEXFMT_A4R4G4B4:
		if(stride) *stride = 2;
		return 11;
	case TEXFMT_A8:
		if(stride) *stride = 1;
		return 12;
	case TEXFMT_L8:
		if(stride) *stride = 1;
		return 14;
	}
	return INDEX_NOT_FOUND;
}

//-----------------------------------------------------------------------------
bool ConvertRect(void* dst, uint dstwidth, uint dstheight,
					  const float* src, uint srcwidth, int srcheight, const RRect& srcrect,
					  TextureFormat fmt)
{
	TConvertPixels tconv = NULL;

	uint stride;
	uint index = GetFormatIndex(fmt, &stride);
	if(index == INDEX_NOT_FOUND)
		return false;

	tconv = ConvertTable[0][index];

	if(tconv == NULL)
		return false;

	uint width = enf_min(dstwidth, srcrect.Right - srcrect.Left);
	uint height = enf_min(dstheight, srcrect.Bottom - srcrect.Top);

	for(uint y = 0; y < height; y++)
	{
		tconv(dst, (const void*)src, width);
		src = (float*)(((FColor*)src) + srcwidth);
		dst = ((ubyte*)dst) + stride * dstwidth;
	}
	return true;
}
}

//-----------------------------------------------------------------------------
size_t GetSurfaceSize(uint width, uint heigth, TextureFormat fmt)
{
	switch(fmt)
	{
	//32bit formats
	case TEXFMT_X8L8V8U8:
	case TEXFMT_Q8W8V8U8:
	case TEXFMT_A8R8G8B8:
	case TEXFMT_X8R8G8B8:
	case TEXFMT_D32:
	case TEXFMT_D24X8:
	case TEXFMT_V16U16:
	case TEXFMT_D24S8:
	case TEXFMT_R32F:
	case TEXFMT_G16R16F:
	case TEXFMT_A2R10G10B10:
	case TEXFMT_A2W10V10U10:
	case TEXFMT_UYVY: //16+16?
	case TEXFMT_YUY2:
	case TEXFMT_D24FS8:
		return width * heigth * 4;

	//16bit formats
	case TEXFMT_R5G6B5:
	case TEXFMT_X1R5G5B5:
	case TEXFMT_A1R5G5B5:
	case TEXFMT_A4R4G4B4:
	case TEXFMT_X4R4G4B4:
	case TEXFMT_A8L8:
	case TEXFMT_V8U8:
	case TEXFMT_L6V5U5:
	case TEXFMT_D16:
	case TEXFMT_R16F:
	case TEXFMT_L16:
		return width * heigth * 2;

	//8bit formats
	case TEXFMT_A8:
	case TEXFMT_L8:
		return width * heigth;

	//compressed formats
	case TEXFMT_CTX1:
	case TEXFMT_DXT1:
		return GetDXTBlocks(width) * GetDXTBlocks(heigth) * 8;
#ifdef ENF_DX9
	case TEXFMT_DXT2:
	case TEXFMT_DXT4:
#endif
	case TEXFMT_DXT3:
	case TEXFMT_DXT5:
		return GetDXTBlocks(width) * GetDXTBlocks(heigth) * 16;
				
	//64bit formats
	case TEXFMT_A16B16G16R16F:
	case TEXFMT_G32R32F:
		return width * heigth * 8;

	//128bit formats
	case TEXFMT_A32B32G32R32F:
		return width * heigth * 16;

	case TEXFMT_UNKNOWN:
		return 0;

	default:
		enf_assert(!"unknown format");
	}
	return 0;
}

//-----------------------------------------------------------------------------
PUBLIC_API void DecodeChannels(TextureFormat fmt, uint p, ubyte& r, ubyte& g, ubyte& b, ubyte& a)
{
	a = (p >> 24) & 0xff;
	r = (p >> 16) & 0xff;
	g = (p >> 8) & 0xff;
	b = p & 0xff;
}

//-----------------------------------------------------------------------------
size_t GetFormatStride(TextureFormat fmt)
{
	switch(fmt)
	{
		//32bit formats
	case TEXFMT_X8L8V8U8:
	case TEXFMT_Q8W8V8U8:
	case TEXFMT_A8R8G8B8:
	case TEXFMT_X8R8G8B8:
	case TEXFMT_D32:
	case TEXFMT_D24X8:
	case TEXFMT_V16U16:
	case TEXFMT_D24S8:
	case TEXFMT_R32F:
	case TEXFMT_G16R16F:
	case TEXFMT_A2R10G10B10:
	case TEXFMT_A2W10V10U10:
	case TEXFMT_UYVY: //16+16?
	case TEXFMT_YUY2:
	case TEXFMT_D24FS8:
		return 4;

		//16bit formats
	case TEXFMT_R5G6B5:
	case TEXFMT_X1R5G5B5:
	case TEXFMT_A1R5G5B5:
	case TEXFMT_A4R4G4B4:
	case TEXFMT_X4R4G4B4:
	case TEXFMT_A8L8:
	case TEXFMT_V8U8:
	case TEXFMT_L6V5U5:
	case TEXFMT_D16:
	case TEXFMT_R16F:
	case TEXFMT_L16:
		return 2;

		//8bit formats
	case TEXFMT_A8:
	case TEXFMT_L8:
		return 1;

		//compressed formats
	case TEXFMT_CTX1:
	case TEXFMT_DXT1:
		return 8;

	case TEXFMT_DXT2:
	case TEXFMT_DXT4:
#ifdef ENF_PS3
	case TEXFMT_DXT3:
	case TEXFMT_DXT5:
#endif
		return 16;

		//64bit formats
	case TEXFMT_A16B16G16R16F:
	case TEXFMT_G32R32F:
		return 8;

		//128bit formats
	case TEXFMT_A32B32G32R32F:
		return 16;

	default:
		enf_assert(!"unknown format");
	}
	return 0;
}

//-----------------------------------------------------------------------------
/*!
calculate hardware mipmap offset -> on PS3 for linear format this is different
than on any other platform, the mipmaped levels are padded according the first 
level width
*/
//-----------------------------------------------------------------------------
ENF_INLINEFUNC uint GetHWMipmapOffset(uint level, const Texture* tex)
{
	uint width  = tex->GetWidth();
	uint height = tex->GetHeight();

	uint offset = 0;

	for(uint i = 0; i < level; ++i)
	{
		offset += GetSurfaceSize(width, height, tex->GetFormat());
		width  = enf_max(1u, width >> 1);
		height = enf_max(1u, height >> 1);
	}

	return offset;
}

//-----------------------------------------------------------------------------
TextureFormat ComputeFormat(uint dwFlags, ushort* dwMipLevels)
{
TextureFormat texFormat;

	if(dwMipLevels)
	{
		if(dwFlags & TEXTR_NOMIPMAP)
			*dwMipLevels = 1;
		else
			*dwMipLevels = 0;
	}

	texFormat = TEXFMT_A8R8G8B8;

	return texFormat;
}

//-----------------------------------------------------------------------------
uint Texture::GetNumLevels() const
{
#if defined(ENF_DX9) 
	enf_assert(GetD3DTexture() != NULL);
	return GetD3DTexture()->GetLevelCount();
#elif defined(ENF_OGL)
	return m_iMipLevels;
#endif
}

//-----------------------------------------------------------------------------
#if defined(ENF_DX9) 
_D3DSurface* Texture::GetSurface(uint level)
{
	enf_rlsassert(GetDevice()->CheckThread());
	enf_assert(GetD3DTexture() != NULL);

	_D3DSurface* psurface;
	if(GetD3DTexture()->GetSurfaceLevel(level, &psurface) == D3D_OK)
		return psurface;

	return NULL;
}
#endif
//-----------------------------------------------------------------------------
size_t Texture::Flush(uint level)
{
	if(!GetDevice()->CheckThread())
		return 0;

	//Texture::Swap is not good here, we are loosing our flags!
	uint flags = m_iFlags;
	if(flags & (TEXTR_CLONE|TEXTR_CLONED))
		return 0;

	//Not added to manager yet (probably in progress)
	if(m_pManager == NULL)
		return 0;

	size_t flushed = GetMemorySize();
	
	//LogF(LL_DEBUG, "Flushed %s, age %d, size %d", GetName().cstr(), GetAge(), flushed);

	Texture* tex = m_pManager->CreatePlaceholderTexture(GetName().cstr(), GetFlags(), GetUsage());
	Swap(tex);
	tex->Release();
	m_iFlags = (flags & ~TEXTR_LOADED) | TEXTR_CLONE;

	return flushed;
}

//-----------------------------------------------------------------------------
void Texture::_Touch(uint level) const
{
	if((m_iFlags & TEXTR_MANAGED) == 0)
		return;

	ResourceRef::_Touch(level);

	if(!IsLoaded())
	{
		if(IsLoading())
			return;

		//not created from file? Ignore it
		if(GetFileHandle() == BAD_FILE_HANDLE)
			return;

		Texture* tex = (Texture*)this;
		//do a load
		LoadTextureResource* ld = ENF_NEW LoadTextureResource(tex, GetFlags(), GetUsage(), tex->m_iPriority);
		GetResources()->AddLoadResource(ld);
		//LogF(LL_DEBUG, "Texture %s, age %d loading", GetName().cstr(), GetAge());
	}
}

//-----------------------------------------------------------------------------
bool Texture::Flush()
{
	if(RefCount() == 0)
	{
		MemStats::m_iDisposedTextures -= GetMemorySize();
		if(!GetDevice()->CheckThread())
		{
			GetDevice()->DelayRelease(this);
			//normally this is in destructor, but it's called too late
			if(m_pManager)
			{
				m_pManager->RemoveManagedResource(this);
				m_pManager->RemoveTexture(this);
				m_pManager = NULL;
			}
		}
		else
		{
			Free();
			GetDevice()->RemoveResource(this);
			delete this;
		}
		return true;
	}
	return false;
}

//-----------------------------------------------------------------------------
bool Texture::IsCube() const
{
	#if defined(ENF_DX9) 
		enf_assert(GetD3DTexture() != NULL);
		return (GetD3DTexture()->GetType() == D3DRTYPE_CUBETEXTURE);
	#elif defined(ENF_OGL)
		return (m_iFlags & TEXTR_CUBE) != 0;
	#endif
}

//-----------------------------------------------------------------------------
// Name: Texture()
// Desc: Constructor for a texture object
//-----------------------------------------------------------------------------
Texture::Texture(IRenderer *dev, CStr name, uint dwWidth, uint dwHeight, uint dwFlags):
	DeviceResource(dev),
	ResourceRef(),
	#ifdef ENF_DX9
		m_pTexture(NULL),
	#ifdef ENF_OGL
		m_iTexture(0),
		m_iLocked(-1),
	#endif
	m_iWidth(dwWidth),
	m_iHeight(dwHeight),
	m_iMipLevels(0),
	m_Usage(MAP_UNKNOWN)
{
	#ifdef ENF_DX9
		if(dwFlags & TEXTR_DYNAMIC && !(dev->m_d3dCaps.Caps2 & D3DCAPS2_DYNAMICTEXTURES))
			dwFlags &= ~(TEXTR_DYNAMIC);
	#endif

	m_iFlags = dwFlags;
	m_Format = ComputeFormat(dwFlags, &m_iMipLevels);
	if(name != NULL)
	{
		m_strName = g_pStatic->AddString(name.cstr());
	}
}

//-----------------------------------------------------------------------------
Texture::Texture(IRenderer *dev, CStr name):
	DeviceResource(dev),
	ResourceRef(),
	#ifdef ENF_DX9
		m_pTexture(NULL),
	#endif
	#ifdef ENF_OGL
		m_iTexture(0),
	#endif
	m_iWidth(0),
	m_iHeight(0),
	m_iFlags(0),
	m_iMipLevels(0)
{
	if(name.IsEmpty())
	{
		m_strName = g_pStatic->AddString(name.cstr());
	}
}

//-----------------------------------------------------------------------------
Texture::Texture(IRenderer *dev):
	DeviceResource(dev),
	ResourceRef(),
	#ifdef ENF_DX9
		m_pTexture(NULL),
	#endif
	#ifdef ENF_OGL
		m_iTexture(0),
	#endif
	m_iWidth(0),
	m_iHeight(0),
	m_iFlags(0),
	m_iMipLevels(0)
{
}

//-----------------------------------------------------------------------------
Texture::~Texture()
{
	if(m_pManager)
	{
		m_pManager->RemoveManagedResource(this);
		m_pManager->RemoveTexture(this);
	}
}

//-----------------------------------------------------------------------------
void Texture::Free()
{
	#ifdef ENF_DX9
		if(m_pTexture)
		{
			MemStats::m_iTextures -= GetMemorySize();
			uint c = m_pTexture->Release();
			if((m_iFlags & (TEXTR_CLONED|TEXTR_CLONE)) == 0)
			{
				enf_assert(c == 0);
			}
			m_pTexture = NULL;
		}
	#elif defined(ENF_OGL)
		if(m_iTexture)
		{
			if(IsCube())
				glBindTexture(GL_TEXTURE_3D, 0);
			else
				glBindTexture(GL_TEXTURE_2D, 0);
			GLCALL(glDeleteTextures(1, &m_iTexture));
			GetDevice()->DirtyMask(Renderer::GLCM_TEXTURES);
			m_iTexture = 0;
		}
	#endif
}

//-----------------------------------------------------------------------------
uint Texture::GetMaxMipmaps(uint width, uint height, uint depth)
{
	//TODO: maximum of GetHighestBit is easier
	uint count = 1;
	while(!(width == 1 && height == 1 && depth == 1))
	{
		width		= enf_max(1U, width >> 1);
		height	= enf_max(1U, height >> 1);
		depth		= enf_max(1U, depth >> 1);
		count ++;
	} 

	return count;
}

//-----------------------------------------------------------------------------

#if defined(ENF_OGL)
GLint TexFormatToGLFormat(TextureFormat fmt, bool rt)
{
	switch(fmt)
	{
	case TEXFMT_A8R8G8B8:
	case TEXFMT_X8R8G8B8:
		return GL_RGBA8;

	case TEXFMT_R32F:
		return GL_LUMINANCE32F_ARB;

	case TEXFMT_G16R16F:
		return (rt) ? GL_RGBA16F_ARB : GL_LUMINANCE_ALPHA16F_ARB;

	case TEXFMT_A2R10G10B10:
	case TEXFMT_A2W10V10U10:
		return GL_RGB10_A2;

	case TEXFMT_D24X8:
	case TEXFMT_D24S8:
	case TEXFMT_D24FS8:
		return GL_DEPTH_COMPONENT24;

	case TEXFMT_X1R5G5B5:
	case TEXFMT_A1R5G5B5:
		return GL_RGB5_A1;

	case TEXFMT_A4R4G4B4:
	case TEXFMT_X4R4G4B4:
		return GL_RGBA4;

	case TEXFMT_A8L8:
		return GL_LUMINANCE8_ALPHA8;

	case TEXFMT_R16F:
		return GL_ALPHA16F_ARB;

	case TEXFMT_L16:
		return GL_LUMINANCE16;

	case TEXFMT_A8:
		return GL_ALPHA8;
	case TEXFMT_L8:
		return GL_INTENSITY8;

		//compressed formats
	case TEXFMT_DXT1:
		return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;

	case TEXFMT_DXT3:
		return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;

	case TEXFMT_DXT5:
		return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;

	case TEXFMT_A16B16G16R16F:
		return GL_RGBA16F_ARB;

	case TEXFMT_G32R32F:
		return GL_LUMINANCE_ALPHA32F_ARB;

	case TEXFMT_A32B32G32R32F:
		return GL_RGBA32F_ARB;

	default:
		enf_assert(!"unknown format");
		return 0;
	};
}

GLint TexFormatToGLFormatEx(TextureFormat fmt, GLenum& dataFormat, GLenum& dataType, bool rt)
{
	dataFormat = GL_RGBA;
	dataType = GL_UNSIGNED_INT_8_8_8_8;
	switch(fmt)
	{
	case TEXFMT_A8R8G8B8:
	case TEXFMT_X8R8G8B8:
		dataFormat = GL_BGRA;
		dataType = GL_UNSIGNED_INT_8_8_8_8_REV;
		return GL_RGBA8;

	case TEXFMT_R32F:
		dataFormat = GL_LUMINANCE;
		dataType = GL_FLOAT;
		return GL_R32F;

	case TEXFMT_G16R16F:
		if(rt)
		{
			dataFormat = GL_RGBA;
			dataType = GL_HALF_FLOAT_ARB;
			return GL_RG16F;
		}
		else
		{
			dataFormat =  GL_RGBA;
			dataType = GL_HALF_FLOAT_ARB;
			return  GL_RG16F;
		}

	case TEXFMT_A2R10G10B10:
	case TEXFMT_A2W10V10U10:
		return GL_RGB10_A2;

	case TEXFMT_D24X8:
	case TEXFMT_D24S8:
	case TEXFMT_D24FS8:
		return GL_DEPTH_COMPONENT24;

	case TEXFMT_X1R5G5B5:
	case TEXFMT_A1R5G5B5:
		return GL_RGB5_A1;

	case TEXFMT_A4R4G4B4:
	case TEXFMT_X4R4G4B4:
		return GL_RGBA4;

	case TEXFMT_A8L8:
		dataFormat = GL_LUMINANCE_ALPHA;
		dataType = GL_UNSIGNED_BYTE;
		return GL_LUMINANCE8_ALPHA8;

	case TEXFMT_R16F:
		dataFormat = GL_ALPHA;
		dataType = GL_HALF_FLOAT;
		return GL_ALPHA16F_ARB;

	case TEXFMT_L16:
		dataFormat = GL_LUMINANCE;
		dataType = GL_UNSIGNED_SHORT;
		return GL_LUMINANCE16;

	case TEXFMT_A8:
		dataFormat = GL_ALPHA;
		dataType = GL_UNSIGNED_BYTE;
		return GL_ALPHA8;

	case TEXFMT_L8:
		dataFormat = GL_LUMINANCE;
		dataType = GL_UNSIGNED_BYTE;
		return GL_INTENSITY8;

		//compressed formats
	case TEXFMT_DXT1:
		return GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;

	case TEXFMT_DXT3:
		return GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;

	case TEXFMT_DXT5:
		return GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;

	case TEXFMT_A16B16G16R16F:
		dataFormat =  GL_RGBA;
		dataType = GL_HALF_FLOAT_ARB;
		return GL_RGBA16F_ARB;

	case TEXFMT_G32R32F:
		dataFormat =  GL_LUMINANCE_ALPHA;
		dataType = GL_FLOAT;
		return GL_LUMINANCE_ALPHA32F_ARB;

	case TEXFMT_A32B32G32R32F:
		return GL_RGBA32F_ARB;

	default:
		enf_assert(!"unknown format");
		return 0;
	};
}
#endif


//-----------------------------------------------------------------------------
ERESULT Texture::Allocate()
{
	enf_assert(m_iWidth != 0 && m_iHeight != 0);

	ERESULT res = EE_ERROR;

#ifdef ENF_DX9
	D3DFORMAT fmt = static_cast<D3DFORMAT>(m_Format);

	#ifdef ENF_DISABLE_MANAGED_RESOURCES
		D3DPOOL pool = D3DPOOL_DEFAULT;
		m_bManaged = false;
	#else 
		D3DPOOL pool = D3DPOOL_MANAGED;
		m_bManaged = true;
	#endif

	uint usage = 0;

	if(m_iFlags & TEXTR_DEFAULTPOOL)
	{
		pool = D3DPOOL_DEFAULT;
		m_bManaged = false;
	}

	if(m_iFlags & TEXTR_AUTOMIPMAP)
	{
		if(GetDevice()->m_d3dCaps.Caps2 & D3DCAPS2_CANAUTOGENMIPMAP)
			usage |= D3DUSAGE_AUTOGENMIPMAP;
		else
			m_iMipLevels = 1;
	}

	if(m_iFlags & TEXTR_DYNAMIC)
	{
		pool = D3DPOOL_DEFAULT;
		usage |= RES_USAGE_DYNAMIC;
		m_bManaged = false;
	}

	if(m_iFlags & TEXTR_SYSMEM)
	{
		pool = D3DPOOL_SYSTEMMEM;
		usage = 0;
	}

	if(m_iFlags & TEXTR_RENDERTARGET)
	{
		pool = D3DPOOL_DEFAULT;
		usage |= D3DUSAGE_RENDERTARGET;
		usage &= ~RES_USAGE_DYNAMIC;
		m_bManaged = false;
	}

	do
	{
		if(m_iFlags & TEXTR_CUBE)
		{
			if(GetDevice()->GetDXDevice()->CreateCubeTexture(
						m_iWidth,
						m_iMipLevels,
						usage,
						fmt,
						pool,
						(_D3DCubeTexture**)&m_pTexture,
						NULL) == D3D_OK)
			{
				res = EE_OK;
				break;
			}
		}
		else
		{
			if(GetDevice()->GetDXDevice()->CreateTexture(
						m_iWidth,
						m_iHeight,
						m_iMipLevels,
						usage,
						fmt,
						pool,
						&m_pTexture,
						NULL) == D3D_OK)
			{
				res = EE_OK;
				break;
			}
		}
	} while(MemoryManager::OutOfMemory(__FILE__, __LINE__, MT_VIDEO, GetMemorySize()));
	if(res == EE_OK)
		MemStats::m_iTextures += GetMemorySize();

#elif defined(ENF_OGL)

	GLCALL(glGenTextures(1, &m_iTexture));

	GLenum dataFormat, dataType;
	GLint internalFormat = TexFormatToGLFormatEx(m_Format, dataFormat, dataType);

	bool compressed = IsDXTFormat(m_Format);

	static const GLenum targets[] = {
		GL_TEXTURE_2D,
		GL_TEXTURE_CUBE_MAP_POSITIVE_X,
		GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
		GL_TEXTURE_CUBE_MAP_POSITIVE_Y,
		GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
		GL_TEXTURE_CUBE_MAP_POSITIVE_Z,
		GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
	};
	uint targetIndex;
	GLenum texTarget;
	uint numFaces;
	if(m_iFlags & TEXTR_CUBE)
	{
		targetIndex = 1;
		texTarget = GL_TEXTURE_CUBE_MAP;
		numFaces = 6;
	}
	else
	{
		targetIndex = 0;
		texTarget = GL_TEXTURE_2D;
		numFaces = 1;
	}

	uint width = m_iWidth;
	uint height = m_iHeight;
	uint maxMipMap;

	if(m_iMipLevels == 0)
	{
		maxMipMap = GetMaxMipmaps(width, height, 1);
		m_iMipLevels = maxMipMap;
	}
	else
		maxMipMap = m_iMipLevels;

	GLCALL(glBindTexture(texTarget, m_iTexture));

	GLCALL(glTexParameteri(texTarget, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
	GLCALL(glTexParameteri(texTarget, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
	GLCALL(glTexParameteri(texTarget, GL_TEXTURE_MAX_LEVEL, maxMipMap - 1));
	GLCALL(glTexParameteri(texTarget, GL_TEXTURE_MAX_LOD, maxMipMap - 1));
	if ( IsCube() )
	{
		GLCALL(glTexParameteri( texTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE ));
		GLCALL(glTexParameteri( texTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE ));
	}
	else
	{
		GLCALL(glTexParameteri( texTarget, GL_TEXTURE_WRAP_S, GL_REPEAT ));
		GLCALL(glTexParameteri( texTarget, GL_TEXTURE_WRAP_T, GL_REPEAT ));
	}


	for(uint n = 0; n < maxMipMap; n++)
	{
		for(uint f = 0; f < numFaces; f++)
		{
			GLenum target = targets[targetIndex + f];

			if(compressed)
			{
				size_t size = GetSurfaceSize(width, height, m_Format);
				GLCALL(glCompressedTexImage2D(target, n, internalFormat, width, height, 0, size, NULL));
/*				GLint ssize;
				GLCALL(glGetTexLevelParameteriv(texTarget,
					n,
					GL_TEXTURE_COMPRESSED_IMAGE_SIZE,
					&ssize));
				size=size;*/
			}
			else
			{
				GLCALL(glTexImage2D(target, n, internalFormat, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL));
			}
		}

		width = enf_max(1U, width >> 1);
		height = enf_max(1U, height >> 1);
	}

	GetDevice()->DirtyMask(Renderer::GLCM_TEXTURES|Renderer::GLCM_SAMPLERFILTER|Renderer::GLCM_SAMPLERFILTER);
	res = EE_OK;
#endif

	return res;
}

//-----------------------------------------------------------------------------
#if defined(ENF_OGL) 

struct LockInfo
{
	ENF_DECLARE_SMALLOBJECT(LockInfo);
	int	nextFreeLock;
	void* pData;
	RRect	rect;
	int	level;
	uint	width;
	uint	height;
	LockInfo(): nextFreeLock(-1), pData(NULL) {}
};

class LockContainer: public enf::FlushableObject
{
	Array<LockInfo*> m_LockInfos;
	int m_iFirstLockInfo;
	CMutex m_Mutex;

public:
	LockContainer(): m_iFirstLockInfo(-1) {}

	void Flush (unsigned int level)
	{
		if(level == ENF_CLEANUP_FLUSHLEVEL)
		{
			for(unsigned int n = 0; n < m_LockInfos.GetCardinality(); n++)
			{
				delete m_LockInfos[n];
			}
			m_LockInfos.Flush(level);
		}
	}

	const LockInfo* GetLockInfo(int index)
	{
		return m_LockInfos[index];
	}

	int AllocLockInfo(LockInfo*& lockInfo)
	{
		CMutexLock lock(m_Mutex);

		if(m_iFirstLockInfo >= 0)
		{
			int index = m_iFirstLockInfo;
			int nextFree = m_LockInfos[index]->nextFreeLock;
			m_iFirstLockInfo = nextFree;
			lockInfo = m_LockInfos[index];
			return index;
		}

		lockInfo = ENF_NEW LockInfo();
		return m_LockInfos.Insert(lockInfo);
	}

	void FreeLockInfo(int index)
	{
		CMutexLock lock(m_Mutex);

		m_LockInfos[index]->nextFreeLock = m_iFirstLockInfo;
		m_iFirstLockInfo = index;
	}
};

//-----------------------------------------------------------------------------
LockContainer lockContainer;
FlushableObjectAgent<LockContainer> lockInfoAgent(&lockContainer);


int AllocLockInfo(LockInfo*& lockInfo)
{
	return lockContainer.AllocLockInfo(lockInfo);
}
#endif



//-----------------------------------------------------------------------------
bool Texture::LockCubeFace(CubeMapFaces face, uint level, const RRect *rect, LockedImageRect *lrect, SurfaceDesc *desc, bool readOnly)
{
	enf_rlsassert(GetDevice()->CheckThread());
#if defined(ENF_DX9) 
	_D3DCubeTexture* cube = (_D3DCubeTexture*)GetD3DTexture();

	//Do not try to lock cube/volume textures!
	if(cube->GetType() != D3DRTYPE_CUBETEXTURE)
		return false;

	D3DLOCKED_RECT d3dlrect;
	D3DSURFACE_DESC d3ddesc;

	while(true)
	{
		HRESULT	ret;

		if((ret = cube->LockRect((D3DCUBEMAP_FACES)face, level, &d3dlrect, (const RECT*)rect, readOnly ? D3DLOCK_READONLY : 0/*D3DLOCK_NO_DIRTY_UPDATE*/)) == D3D_OK) break;
		
		if(ret != D3DERR_NOTAVAILABLE)	//sth failed
			return false;
	}

	lrect->Pitch = d3dlrect.Pitch;
	lrect->pBits = d3dlrect.pBits;
	if(desc)
	{
		if((cube->GetLevelDesc(0, &d3ddesc)) != D3D_OK)
			return false;

		desc->Format = (TextureFormat)d3ddesc.Format;
		desc->Usage = d3ddesc.Usage;
		desc->Pool = d3ddesc.Pool;
		desc->Width = d3ddesc.Width;
		desc->Height = d3ddesc.Height;
	}
#elif defined(ENF_OGL)
	return false;
#endif
	return true;
}

//-----------------------------------------------------------------------------
void Texture::UnlockCubeFace(CubeMapFaces face, uint level)
{
	enf_rlsassert(GetDevice()->CheckThread());
#if defined(ENF_DX9) 
	_D3DCubeTexture* cube = (_D3DCubeTexture*)GetD3DTexture();

	//Do not try to lock cube/volume textures!
	if(cube->GetType() != D3DRTYPE_CUBETEXTURE)
		return;
	cube->UnlockRect((D3DCUBEMAP_FACES)face, level);
#elif defined(ENF_OGL)
#endif

}

//-----------------------------------------------------------------------------
// Name: Lock()
// Desc: Locks surface
//-----------------------------------------------------------------------------
bool Texture::Lock(uint level, const RRect *rect, LockedImageRect *lrect, SurfaceDesc *desc, bool readOnly)
{
	enf_rlsassert(GetDevice()->CheckThread());
	enf_assert(lrect);

#if defined(ENF_DX9) 
	enf_assert(!IsCube());

	//Do not try to lock cube/volume textures!
	if(this->GetD3DTexture()->GetType() != D3DRTYPE_TEXTURE)
		return false;

	D3DLOCKED_RECT d3dlrect;
	D3DSURFACE_DESC d3ddesc;

	_D3DTexture* tex = GetD3DTexture();
	while(true)
	{
		HRESULT	ret;

		if((ret = tex->LockRect(level, &d3dlrect, (const RECT*)rect, readOnly ? D3DLOCK_READONLY : 0/*D3DLOCK_NO_DIRTY_UPDATE*/)) == D3D_OK) break;
		
		if(ret != D3DERR_NOTAVAILABLE)	//sth failed
			return false;
	}

	lrect->Pitch = d3dlrect.Pitch;
	lrect->pBits = d3dlrect.pBits;
	if(desc)
	{
		if((tex->GetLevelDesc(0, &d3ddesc)) != D3D_OK)
			return false;

		desc->Format = (TextureFormat)d3ddesc.Format;
		desc->Usage = d3ddesc.Usage;
		desc->Pool = d3ddesc.Pool;
		desc->Width = d3ddesc.Width;
		desc->Height = d3ddesc.Height;
	}
#elif defined(ENF_OGL)
	enf_assert(!IsCube());
	enf_assert(m_iLocked == -1);
	enf_assert(level < m_iMipLevels);
	RRect rr;
	
	uint width = enf_max((uint)(m_iWidth >> level), 1U);
	uint height = enf_max((uint)(m_iHeight >> level), 1U);

	if(rect == NULL)
	{
		rr.Left = 0;
		rr.Top = 0;
		rr.Right = width;
		rr.Bottom = height;
		rect = &rr;
	}
	else
	{
		enf_assert((rect->Right - rect->Left) <= width);
		enf_assert((rect->Bottom - rect->Top) <= height);
	}

	if(desc)
	{
		desc->Format = m_Format;
		desc->Width = width;
		desc->Height = height;
		desc->Pool = 0;
		desc->Usage = 0;
	}

	size_t size = GetSurfaceSize(width, height, m_Format);

	void* pdata = NULL;
	pdata = MemoryManager::Alloc(size, __FILE__, __LINE__);
	LockInfo* lockInfo;
	int allocIndex = AllocLockInfo(lockInfo);
	lockInfo->rect = *rect;
	lockInfo->pData = pdata;
	lockInfo->level = level;
	lockInfo->width = width;
	lockInfo->height = height;
	m_iLocked = allocIndex;

	lrect->pBits = pdata;
	lrect->Pitch = GetSurfaceSize(width, 1, m_Format);
	GetDevice()->DirtyMask(Renderer::GLCM_TEXTURES);
	return true;
#endif

	return true;
}

extern bool g_bGLReport;

//-----------------------------------------------------------------------------
// Name: Unlock()
// Desc: Unlocks surface
//-----------------------------------------------------------------------------
void Texture::Unlock(uint level)
{
	enf_rlsassert(GetDevice()->CheckThread());
	enf_assert(!IsCube());

#if defined(ENF_DX9)
	GetD3DTexture()->UnlockRect(level);
#elif defined(ENF_OGL)
	enf_assert(m_iLocked >= 0);
	uint allocIndex = m_iLocked;
	m_iLocked = -1;

	GLCALL(glActiveTexture(GL_TEXTURE0));
	GLCALL(glBindTexture(GL_TEXTURE_2D, GetGLTexture()));

	GLenum dataFormat, dataType;
	GLint glfmt = TexFormatToGLFormatEx(m_Format, dataFormat, dataType);

	const LockInfo* info = lockContainer.GetLockInfo(allocIndex);

	if(IsDXTFormat(m_Format))
	{
		size_t size = GetSurfaceSize(info->width, info->height, m_Format);
		GLCALL(glCompressedTexImage2D(GL_TEXTURE_2D, info->level, glfmt, info->width, info->height, 0, size, info->pData));
	}
	else
	{
		if(info->rect.Right == info->width && info->rect.Bottom == info->height)
		{
			GLCALL(glTexImage2D(
				GL_TEXTURE_2D,
				info->level,
				glfmt,
				info->width,
				info->height,
				0,
				dataFormat,
				dataType,
				info->pData));
		}
		else
		{
			GLCALL(glTexSubImage2D(
				GL_TEXTURE_2D,
				info->level,
				info->rect.Left,
				info->rect.Top,
				info->rect.Right - info->rect.Left,
				info->rect.Bottom - info->rect.Top,
				dataFormat,
				dataType,
				info->pData));
//			if(info->rect.Top == 35)
//				g_bGLReport = true;
		}
	}

	MemoryManager::Free(info->pData);
	lockContainer.FreeLockInfo(allocIndex);
	GetDevice()->DirtyMask(Renderer::GLCM_TEXTURES);
#endif
}

//-----------------------------------------------------------------------------
ERESULT Texture::CopyRGBAData(uint level, uint xpos, uint ypos, const FColor* ptr, uint w, uint h, const RRect& rect)
{
	enf_rlsassert(GetDevice()->CheckThread());
	LockedImageRect lrect;
	SurfaceDesc desc;
	RRect srect;

	srect.Left = xpos;
	srect.Top = ypos;
	srect.Right = xpos + w;
	srect.Bottom = ypos + h;

	uint width = m_iWidth / (1 << level);
	uint height = m_iHeight / (1 << level);

	if(width < (xpos + w))
		return EE_ERROR;

	if(height < (ypos + h))
		return EE_ERROR;

	if(!Lock(level, &srect, &lrect, &desc))
		return EE_ERROR;

	
	bool res = TextureCodec::ConvertRect(lrect.pBits, width, height, (const float*)ptr, w, h, rect, m_Format);

	Unlock(level);

	return res ? EE_OK : EE_ERROR;
}

//-----------------------------------------------------------------------------
ERESULT Texture::CopyData(uint level, const void* data, const RRect& dest)
{
	enf_rlsassert(GetDevice()->CheckThread());
	uint twidth		= enf_max(1, m_iWidth >> level);
	uint theight	= enf_max(1, m_iHeight >> level);

	if(dest.Left >= twidth ||	dest.Top >= theight || dest.Right > twidth ||	dest.Bottom > theight)
		return EE_ERROR;

	LockedImageRect lrect;
	uint tstride = enf::GetFormatStride(GetFormat());

#if defined(ENF_OGL)
	
	if(IsDXTFormat(m_Format))
	{
		int a = 0;
	}
	else
	{
		GLenum dataFormat, dataType;
		GLint glfmt = TexFormatToGLFormatEx(m_Format, dataFormat, dataType);

		GLCALL(glTexImage2D(
			GL_TEXTURE_2D,
			level,
			glfmt,
			m_iWidth,
			m_iHeight,
			0,
			dataFormat,
			dataType,
			data));
	}
#else
	if(Lock(level, &dest, &lrect, NULL))
	{
		uint height = dest.Bottom - dest.Top;
		uint width = dest.Right - dest.Left;

		for(uint h = 0; h < height; h++)
		{
			memcpy((ubyte*)lrect.pBits + (h * lrect.Pitch), data, width * tstride);
			data = ((ubyte*)data + width * tstride);
		}
		Unlock(level);
	}
#endif
	return EE_OK;
}

//-----------------------------------------------------------------------------
ERESULT Texture::FillColor(const FColor& color, uint sx, uint sy, uint w, uint h)
{
	enf_rlsassert(GetDevice()->CheckThread());
	return FillColor((color * 255.0f).PackToInt(), sx, sy, w, h);
}


#if defined(ENF_DX9)
void FillSurface(const D3DLOCKED_RECT& rect, const D3DSURFACE_DESC& d3dsd, uint w, uint h, uint color)
{
	D3DFORMAT fmt = d3dsd.Format;

	//fastmodes as first
	if(fmt == D3DFMT_X8R8G8B8 ||
		fmt == D3DFMT_A8R8G8B8)
	{
		uint xmod;
		xmod = rect.Pitch - (w << 2);
		for(uint y = 0; y < h; y++)
		{
			FillMemory32((char*)rect.pBits + rect.Pitch * y, w, color);
		}
	}
	else if(fmt == D3DFMT_L8 || fmt == D3DFMT_A8)
	{
		for(uint y = 0; y < h; y++)
		{
			memset((char*)rect.pBits + rect.Pitch * y, color, w);
		}
	}
	else
	{
		for(uint y = 0; y < h; y++)
		{
			FillMemory32((char*)rect.pBits + rect.Pitch * y, w, color);
		}
	}
}
#endif

//-----------------------------------------------------------------------------
// Name: FillColor()
// Desc: 
//-----------------------------------------------------------------------------
ERESULT Texture::FillColor(uint color, uint sx, uint sy, uint w, uint h)
{
	enf_rlsassert(GetDevice()->CheckThread());

#if defined(ENF_DX9)
	D3DLOCKED_RECT rect;
	RECT rec;
	D3DSURFACE_DESC d3dsd;

	rec.left = sx;
	rec.top = sy;
	rec.right = sx + w;
	rec.bottom = sy + h;
	if(GetD3DTexture()->GetType() == D3DRTYPE_CUBETEXTURE)
	{
		_D3DCubeTexture* cubeTex = (_D3DCubeTexture*)GetD3DTexture();
		if(cubeTex->GetLevelDesc( 0, &d3dsd) != D3D_OK)
			return EE_ERROR;

		static const D3DCUBEMAP_FACES faces[6] = { 
			D3DCUBEMAP_FACE_POSITIVE_X, 
			D3DCUBEMAP_FACE_NEGATIVE_X, 
			D3DCUBEMAP_FACE_POSITIVE_Y, 
			D3DCUBEMAP_FACE_NEGATIVE_Y, 
			D3DCUBEMAP_FACE_POSITIVE_Z, 
			D3DCUBEMAP_FACE_NEGATIVE_Z 
		}; 
 
		for (uint i = 0; i < 6; i++) 
		{ 
			if(cubeTex->LockRect(faces[i], 0, &rect, &rec, 0) == D3D_OK) 
			{ 
				FillSurface(rect, d3dsd, w, h, color);
				cubeTex->UnlockRect(faces[i], 0); 
			} 
		}
	}
	else
	{
		if(GetD3DTexture()->GetLevelDesc( 0, &d3dsd) != D3D_OK)
			return EE_ERROR;


		if(HasType(RenderTargetTypeID) || (!m_bManaged && m_iFlags & TEXTR_RENDERTARGET))
		{
			IDirect3DSurface9 *pSurface;
			HRESULT hr = GetD3DTexture()->GetSurfaceLevel(0, &pSurface);
			if(hr == D3D_OK)
			{
				hr = GetDevice()->GetDXDevice()->ColorFill(pSurface, &rec, (D3DCOLOR)color);
				pSurface->Release();
			}
			return (hr == D3D_OK) ? EE_OK : EE_ERROR;
		}

		if (m_bManaged)
		{
			if(GetD3DTexture()->LockRect( 0, &rect, &rec, 0) == D3D_OK)
			{
				FillSurface(rect, d3dsd, w, h, color);
				Unlock(0);
			}
		}
		else
		{
			LogF(LL_ERROR, "Enfusion error: Unmanaged textures cannot be updated for now");
		}
	}
#elif defined(ENF_OGL)
	if(!IsCube())
	{
		GLCALL(glBindTexture(GL_TEXTURE_2D, m_iTexture));
		uint size = m_iWidth * m_iHeight;
		uint* ptr = (size <= 65536) ? ALLOCA(uint, m_iWidth * m_iHeight) : (uint*)MemoryManager::Alloc(size * sizeof(uint), __FILE__, __LINE__);
		for(uint s = 0; s < size; s++)
			ptr[s] = color;

		GLint internalFormat = TexFormatToGLFormat(m_Format);
		GLCALL(glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, m_iWidth, m_iHeight, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, ptr));
		GetDevice()->DirtyMask(Renderer::GLCM_TEXTURES|Renderer::GLCM_SAMPLERFILTER|Renderer::GLCM_SAMPLERFILTERV);
		GLCALL(glBindTexture(GL_TEXTURE_2D, 0));
		if(size > 65536) 
			MemoryManager::Free(ptr);
	}

#endif
	return EE_OK;
}

//-----------------------------------------------------------------------
ERESULT Texture::FilterMipMaps(uint level, uint _flags)
{
#ifndef ENF_DUMMYRENDER

	enf_rlsassert(GetDevice()->CheckThread());
#if defined(ENF_DX9)
	DWORD flags = D3DX_FILTER_NONE;

	switch(_flags & 0x7f)
	{
	case TFF_NONE:
		flags = D3DX_FILTER_NONE;
		break;
	case TFF_POINT:
		flags = D3DX_FILTER_POINT;
		break;
	case TFF_LINEAR:
		flags = D3DX_FILTER_LINEAR;
		break;
	case TFF_BOX:
		flags = D3DX_FILTER_BOX;
		break;
	}

	if(_flags & TFF_MIRROR)
		flags |= D3DX_FILTER_MIRROR;

	return D3DXFilterTexture(GetD3DTexture(), NULL, level, flags);
#elif defined(ENF_OGL)
	return EE_OK;
#endif

#endif
	return EE_OK;
}


//-----------------------------------------------------------------------
Texture* IRenderer::CreateTexture(CStr strName, uint dwWidth, uint dwHeight, uint dwFlags)
{
	enf_rlsassert(CheckThread());
	Texture* tc = ENF_NEW Texture(this, strName.cstr(), dwWidth, dwHeight, dwFlags);

	if(tc == NULL)
		return NULL;

	if(tc->Allocate() != EE_OK)
	{
		//FIXME: is it legal?
		delete tc;
		return NULL;
	}

	AddResource(tc);

	return tc;
}

//-----------------------------------------------------------------------
Texture* IRenderer::CreateTexture(CStr strName, Texture* clone, uint dwFlags)
{
	Texture* tc = ENF_NEW Texture(this, strName.cstr(), clone->GetWidth(), clone->GetHeight(), dwFlags|TEXTR_CLONE);

	if(tc == NULL)
		return NULL;
	clone->m_iFlags |= TEXTR_CLONED;

#ifdef ENF_DX9
	clone->GetD3DTexture()->AddRef();
	tc->m_pTexture = clone->GetD3DTexture();

#elif defined(ENF_OGL)
	tc->m_iTexture = clone->GetGLTexture();
#endif

	AddResource(tc);

	return tc;
}

//-----------------------------------------------------------------------
Texture* IRenderer::CreateTextureWithFormat(CStr strName, uint dwWidth, uint dwHeight, TextureFormat format, uint dwFlags, TextureUsage usage)
{
	//it's legal to create textures outside main thread
	enf_rlsassert(CheckThread()); 
	enf_assert(dwWidth <= MAX_TEXTURE_SIZE && dwHeight <= MAX_TEXTURE_SIZE);

	Texture* tc = ENF_NEW Texture(this, strName.cstr(), dwWidth, dwHeight, dwFlags);

	if(tc == NULL)
		return NULL;

	tc->m_Format = format;

	if(tc->Allocate() != EE_OK)
	{
		//FIXME: is it legal?
		delete tc;
		return NULL;
	}

	AddResource(tc);

	return tc;
}

//-----------------------------------------------------------------------
int Texture::Release()
{
	if(Unref() == 0)
	{
		MemStats::m_iDisposedTextures += GetMemorySize();
		//don't keep non-managed or unnamed textures
		//They won't be touched again anyway
		if(m_strName.IsEmpty() || m_pManager == NULL || m_iFlags & TEXTR_NOCACHE)
		{
			Flush();
			return 0;
		}
		//TODO: tell to resource manager that texture is disposable
		if(m_pManager)
		{

//			m_pManager->DisposeTexture(this);
		}
	}

	return RefCount();
}

//-----------------------------------------------------------------------
size_t Texture::GetMemorySize() const
{
#ifdef ENF_DX9
	_D3DTexture* tex = GetD3DTexture();
	if(tex == NULL)
		return 0;

	size_t size = 0;
	bool cube = IsCube();

	for(uint mip = 0; mip < tex->GetLevelCount(); mip++)
	{
		D3DSURFACE_DESC desc;
		if(tex->GetLevelDesc(mip, &desc) == D3D_OK)
		{
			size += GetSurfaceSize(desc.Width, desc.Height, m_Format) * (cube ? 6 : 1);
		}
	}
	return size;

#elif defined(ENF_OGL)
	return 0;
#endif
}
//-----------------------------------------------------------------------
void CopySurface(LockedImageRect& srect, LockedImageRect& trect, int width, int height, TextureFormat fmt, int level, uint tileflags)
{
	switch(fmt)
	{
	case TEXFMT_CTX1:
	case TEXFMT_DXT1:
		{
			int lheight = GetDXTBlocks(height);
			int lwidth = GetDXTBlocks(width);

			for(int v = 0; v < lheight; v++)
			{
				memcpy((ubyte*)trect.pBits + v * trect.Pitch, (ubyte*)srect.pBits + v * srect.Pitch, lwidth * 8);
			}
		}
		break;

	case TEXFMT_DXT2:
	case TEXFMT_DXT4:
	case TEXFMT_DXT3:
	case TEXFMT_DXT5:
		{
			int lheight = GetDXTBlocks(height);
			int lwidth = GetDXTBlocks(width);

			for(int v = 0; v < lheight; v++)
			{
				memcpy((ubyte*)trect.pBits + v * trect.Pitch, (ubyte*)srect.pBits + v * srect.Pitch, lwidth * 16);
			}
		}
		break;

	case TEXFMT_A8:
	case TEXFMT_L8:
		for(int v = 0; v < height; v++)
		{
			memcpy((unsigned char*)trect.pBits + trect.Pitch * v, (unsigned char*)srect.pBits + srect.Pitch * v, width);
		}
		break;

	case TEXFMT_A8R8G8B8:
	case TEXFMT_X8R8G8B8:
		{
			for(int v = 0; v < height; v++)
			{
				memcpy((unsigned char*)trect.pBits + trect.Pitch * v, (unsigned char*)srect.pBits + srect.Pitch * v, width * 4);
			}
		}
		break;

	case TEXFMT_A32B32G32R32F:
		for(int v = 0; v < height; v++)
		{
			memcpy((unsigned char*)trect.pBits + trect.Pitch * v, (unsigned char*)srect.pBits + srect.Pitch * v, width * 16);
		}
		break;

	case TEXFMT_A4R4G4B4:
	case TEXFMT_A8L8:
		for(int v = 0; v < height; v++)
		{
			memcpy((unsigned char*)trect.pBits + trect.Pitch * v, (unsigned char*)srect.pBits + srect.Pitch * v, width * 2);
		}
		break;

	default:
		enf_assert(!"Unsupported format");
		break;
	}
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC void ConvertIntoR32(float *destdata, uchar *srcdata, uint width, uint height, TextureFormat format, uint srcpitch)
{
	const float INV_256 = 1.0f/256.0f;

	uint next  = format == TEXFMT_A8L8 ? 2 : 1;
	bool A8    = format == TEXFMT_A8;

	uchar  alpha = 0, lum = 0;
	uchar& ch = A8 ? alpha : lum;

	for (uint y = 0; y < height; y++)
	{
		uchar* locsrc    = srcdata;
		float* destfloat = destdata;

		for (uint x = 0; x < width; x++)
		{
			if (next == 2)
			{
				#if ENF_INTEGER_BIG_ENDIAN
					alpha	= locsrc[0];
					lum	= locsrc[1];
				#else
					lum	= locsrc[0];
					alpha	= locsrc[1];
				#endif
			}
			else
			{
				ch = *locsrc;
			}

			destfloat[x] = ((float) lum + ((float) alpha)*INV_256);

			locsrc += next;
		}

		destdata += width;
		srcdata  += srcpitch;
	}

}


//-----------------------------------------------------------------------
/*!
swizzling is usefull on PS3, don't know now if it's on PS4 too, so let it
be now alive
*/
//-----------------------------------------------------------------------
template<class T> ENF_INLINEFUNC void SwizzleDataInternal(register uchar*& dst, register const uchar*& src, register uint srcPitch, register uint xpos, register uint ypos, register uint width, register uint height, register uint level)
{
	if (level == 1)
	{
		if(xpos < width && ypos < height)
			*((T*&) dst) = *((T*)(src + ypos*srcPitch + xpos*sizeof(T)));

		dst += sizeof(T);

		return;
	}

	SwizzleDataInternal<T> (dst, src, srcPitch, xpos, ypos, width, height, level - 1);
	SwizzleDataInternal<T> (dst, src, srcPitch, xpos + (1 << (level - 2)), ypos, width, height, level - 1);
	SwizzleDataInternal<T> (dst, src, srcPitch, xpos, ypos + (1 << (level - 2)), width, height, level - 1);
	SwizzleDataInternal<T> (dst, src, srcPitch, xpos + (1 << (level - 2)),ypos + (1U << (level - 2)), width, height, level - 1);
}


//-----------------------------------------------------------------------
ENF_INLINEFUNC void SwizzleData2(uchar* dst, const uchar* src, const uint srcPitch, const uint width, const uint height,	const uint depth)
{
	enf_assert2(depth == 1 || depth == 2 || depth == 4, "unsupported format");
	uint logW = enf_log2(width);

	if (width == height)
	{
		switch (depth)
		{
			case 1 : SwizzleDataInternal<uchar> (dst, src, srcPitch, 0, 0, width, height, logW + 1);  break;
			case 2 : SwizzleDataInternal<ushort> (dst, src, srcPitch, 0, 0, width, height, logW + 1); break;
			case 4 : SwizzleDataInternal<uint> (dst, src, srcPitch, 0, 0, width, height, logW + 1); break;
		}
	}
	else
	{
		uint logH = enf_log2(height);

		if (width > height)
		{
			uint baseLevel	= logW - (logW - logH);
			uint maxL = 1 << (logW - logH);

			for (uint i = 0; i < maxL; i++)
				switch (depth)
				{
					case 1 : SwizzleDataInternal<uchar> (dst, src, srcPitch, (1 << baseLevel)*i, 0, width, height, baseLevel + 1); break;
					case 2 : SwizzleDataInternal<ushort> (dst, src, srcPitch, (1 << baseLevel)*i, 0, width, height, baseLevel + 1); break;
					case 4 : SwizzleDataInternal<uint> (dst, src, srcPitch,(1 << baseLevel)*i, 0, width, height, baseLevel + 1); break;
				}
		}
		else
		{
			uint baseLevel	= logH - (logH - logW);
			uint maxL = 1 << (logH - logW);

			for (uint i = 0; i < maxL; i++)
				switch (depth)
				{
					case 1 : SwizzleDataInternal<uchar> (dst, src, srcPitch, 0, (1 << baseLevel)*i, width, height, baseLevel + 1); break;
					case 2 : SwizzleDataInternal<ushort> (dst, src, srcPitch, 0, (1 << baseLevel)*i, width, height, baseLevel + 1); break;
					case 4 : SwizzleDataInternal<uint> (dst, src, srcPitch, 0, (1 << baseLevel)*i, width, height, baseLevel + 1); break;
				}
		}
	}
}


//-----------------------------------------------------------------------
ERESULT Texture::CopyData(ImageData& imageData, uint flags)
{
	enf_rlsassert(GetDevice()->CheckThread());
	uint twidth = imageData.GetWidth();
	uint theight = imageData.GetHeight();
	TextureFormat fmt = imageData.GetFormat();

	bool convert = false;

	if(GetWidth() != twidth ||
		GetHeight() != theight ||
		(GetFormat() != fmt && !convert))
		return EE_ERROR;

	bool compressed = IsDXTFormat(m_Format);
	uint tileFlags = 0;

	uint numLevels = enf_min(GetNumLevels(), imageData.GetNumLevels());
	uint numFaces = IsCube() ? 6 : 1;

	#if defined(ENF_OGL)
		if(numFaces == 6)
		{
			numFaces = numFaces;
		}
		GLenum dataFormat, dataType;
		GLint glfmt = TexFormatToGLFormatEx(m_Format, dataFormat, dataType);
		GLCALL(glBindTexture(numFaces == 6 ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D, m_iTexture));
		static const GLenum texTargets[] = {
			GL_TEXTURE_2D,
			GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
			GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
			GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z
		};
	#endif

	for(uint face = 0; face < numFaces; face++)
	{
		uint width = twidth;
		uint height = theight;

		for(uint level = 0; level < numLevels; level++)
		{
			LockedImageRect srect;
			if(!imageData.Lock(face, level, srect))
				continue;

			#ifdef ENF_OGL
				if(compressed)
				{
					size_t size = GetSurfaceSize(width, height, m_Format);
					GLCALL(glCompressedTexImage2D(texTargets[face + ((numFaces == 6) ? 1 : 0)], level, glfmt, width, height, 0, size, srect.pBits));
				}
				else
				{
					GLCALL(glTexImage2D(texTargets[face + ((numFaces == 6) ? 1 : 0)], level, glfmt, width, height, 0, dataFormat, dataType, srect.pBits));
				}

			#elif defined(ENF_DX9)
				LockedImageRect trect;
				bool res = false;

				if(IsCube())
					res = LockCubeFace((CubeMapFaces)face, level, NULL, &trect, NULL);
				else
					res = Lock(level, NULL, &trect, NULL);

				if(res)
				{
					CopySurface(srect, trect, width, height, fmt, level, tileFlags);
					if(IsCube())
						UnlockCubeFace((CubeMapFaces)face, level);
					else
						Unlock(level);
				}
			#endif

			imageData.Unlock(face, level);
			width = enf_max<uint>(width >> 1, 1U);
			height = enf_max<uint>(height >> 1, 1U);
		}
	}
	GetDevice()->InvalidateGPUCache(this);

	//we can process only uncompressed textures! (+ R32 format for PS3 and its VS has not to have mipmaps)
	if(!compressed && !convert)
	{
		if(GetNumLevels() > numLevels)
		{
			FilterMipMaps(numLevels - 1, TFF_BOX);
		}
	}

	#ifdef ENF_OGL
		GLCALL(glBindTexture(GL_TEXTURE_2D, 0));
		GetDevice()->DirtyMask(Renderer::GLCM_TEXTURES|Renderer::GLCM_SAMPLERFILTER|Renderer::GLCM_SAMPLERFILTERV);
	#endif

	return EE_OK;
}
#endif

//-----------------------------------------------------------------------
bool Texture::Swap(Texture* other)
{
	enf_rlsassert(GetDevice()->CheckThread());
	enf_assert(other);
	DeviceResource::Swap(other);

#ifdef ENF_DX9
	enf_swap(m_pTexture, other->m_pTexture);
#elif defined(ENF_OGL)
	enf_swap(m_iTexture, other->m_iTexture);
#endif

	enf_swap(m_iWidth, other->m_iWidth);
	enf_swap(m_iHeight, other->m_iHeight);
	enf_swap(m_iMipLevels, other->m_iMipLevels);
	enf_swap(m_iFlags, other->m_iFlags);
	enf_swap(m_Format, other->m_Format);
	return true;
}

//-----------------------------------------------------------------------
size_t GetSourceSurfaceSize(TextureFormat fmt, uint srcDepth, uint width, uint height)
{
	switch(fmt)
	{
	case TEXFMT_CTX1:
	case TEXFMT_DXT1:
		return GetDXTBlocks(width) * GetDXTBlocks(height) * 8;

#ifdef ENF_DX9
	case TEXFMT_DXT2:
	case TEXFMT_DXT4:
#endif
	case TEXFMT_DXT3:
	case TEXFMT_DXT5:
		return GetDXTBlocks(width) * GetDXTBlocks(height) * 16;
		break;

	case TEXFMT_A8:
	case TEXFMT_L8:
		return width * height;

	case TEXFMT_A8R8G8B8:
	case TEXFMT_X8R8G8B8:
		return (width * height * ((srcDepth == 24) ? 3 : 4));

	case TEXFMT_A16B16G16R16F:
		return (width * height * 8);

	case TEXFMT_A32B32G32R32F:
		return (width * height * 16);

	case TEXFMT_R32F:
		return (width * height * 4);

	case TEXFMT_A4R4G4B4:
	case TEXFMT_A8L8:
		return (width * height * 2);
	default:
		enf_assert(!"Unsupported format");
		break;
	}
	return 0;
}

//-----------------------------------------------------------------------
LoadTextureResource::LoadTextureResource(Texture* tex, uint flags, TextureUsage usage, int loadPriority, uint skipMip): LoadResource(tex), m_iFlags(flags), m_Usage(usage), m_iSkipMipMap(skipMip)
{
	m_iPriority = loadPriority;
	tex->AddRef();
}

//-----------------------------------------------------------------------
bool LoadTextureResource::DoLoad()
{
	VFile* vf = GetFileSystem()->Open(GetTexture()->GetFileHandle(), FILEMODE_READ);

	if(vf == NULL)
		return false;

	CFileInputStream instream(vf);
	ImageInfo imginfo;

	ERESULT hr = GetResources()->GetImageLoader()->Load(instream, imginfo, GetResources()->GetTextureDetail() + m_iSkipMipMap, (m_iFlags & TEXTR_NOMIPMAP) ? 1 : 0, m_iFlags, m_ImageData);

	return hr == EE_OK;
}

//-----------------------------------------------------------------------
LoadTextureResource::~LoadTextureResource()
{
	m_pResource->FinishLoading(this);
	GetTexture()->Release();
}

//-----------------------------------------------------------------------
size_t LoadTextureResource::GetDataSize() const
{
	return m_ImageData.GetDataSize();
}

//-----------------------------------------------------------------------
void LoadTextureResource::Apply()
{
	//create texture and copy data
	uint width = m_ImageData.GetWidth();
	uint height = m_ImageData.GetHeight();
	TextureFormat fmt = m_ImageData.GetFormat();

	if(m_ImageData.GetNumLevels() == 1)
		m_iFlags |= TEXTR_NOMIPMAP;

	ComPtr<Texture> loadedtex;

	loadedtex = g_pRender->CreateTextureWithFormat(NULL, width, height, fmt, m_iFlags, m_Usage);
	
	if(!loadedtex.IsNull())
	{
		loadedtex->CopyData(m_ImageData, m_iFlags); 
		Texture* tex = GetTexture();
		tex->Swap(loadedtex);
		tex->SetFlags(TEXTR_LOADED);
		tex->ClearFlags(TEXTR_CLONE);
	}

	m_ImageData.Free();
}

}