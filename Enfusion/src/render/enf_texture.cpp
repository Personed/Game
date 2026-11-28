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
	return m_iMipLevels;
}

//-----------------------------------------------------------------------------
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
	return (m_iFlags & TEXTR_CUBE) != 0;
}

//-----------------------------------------------------------------------------
// Name: Texture()
// Desc: Constructor for a texture object
//-----------------------------------------------------------------------------
Texture::Texture(IRenderer *dev, CStr name, uint dwWidth, uint dwHeight, uint dwFlags):
	DeviceResource(dev),
	ResourceRef(),
#if defined(ENF_OGL)
	m_iTexture(0),
	m_iLocked(-1),
#endif
	m_iWidth(dwWidth),
	m_iHeight(dwHeight),
	m_iMipLevels(0),
	m_Usage(MAP_UNKNOWN),
	m_pStagingData(NULL)
{
	m_iFlags = dwFlags;
	m_Format = ComputeFormat(dwFlags, &m_iMipLevels);
	if(name != NULL)
		m_strName = g_pStatic->AddString(name.cstr());
}

Texture::Texture(IRenderer *dev, CStr name):
	DeviceResource(dev), ResourceRef(),
#if defined(ENF_OGL)
	m_iTexture(0),
#endif
	m_iWidth(0), m_iHeight(0), m_iFlags(0), m_iMipLevels(0),
	m_Usage(MAP_UNKNOWN), m_Format(TEXFMT_UNKNOWN), m_pStagingData(NULL)
{
	if(name.IsEmpty())
		m_strName = g_pStatic->AddString(name.cstr());
}

Texture::Texture(IRenderer *dev):
	DeviceResource(dev), ResourceRef(),
#if defined(ENF_OGL)
	m_iTexture(0),
#endif
	m_iWidth(0), m_iHeight(0), m_iFlags(0), m_iMipLevels(0),
	m_Usage(MAP_UNKNOWN), m_Format(TEXFMT_UNKNOWN), m_pStagingData(NULL)
{
}

Texture::~Texture()
{
	Free();
	if(m_pManager)
	{
		m_pManager->RemoveManagedResource(this);
		m_pManager->RemoveTexture(this);
	}
}

ERESULT Texture::Allocate()
{
	if(!m_iWidth || !m_iHeight || m_Format == TEXFMT_UNKNOWN)
		return EE_INVALIDPARAM;
	if(!m_iMipLevels)
		m_iMipLevels = (m_iFlags & TEXTR_NOMIPMAP) ? 1 : GetMaxMipmaps(m_iWidth, m_iHeight, 1);
	Free();
	m_pStagingData = ENF_NEW MemoryImageData();
	if(!m_pStagingData)
		return EE_ERROR;
	return m_pStagingData->Create(m_iWidth, m_iHeight, m_iMipLevels,
		m_Format, m_iFlags, (TextureUsage)m_Usage);
}

void Texture::Free()
{
	if(m_pStagingData)
	{
		m_pStagingData->Free();
		delete m_pStagingData;
		m_pStagingData = NULL;
	}
}

bool Texture::Lock(uint level, const RRect *rect, LockedImageRect *lrect,
	SurfaceDesc *desc, bool readOnly)
{
	if(!m_pStagingData || !lrect || level >= m_iMipLevels)
		return false;
	if(!m_pStagingData->Lock(0, level, *lrect))
		return false;
	uint width = enf_max(1U, (uint)m_iWidth >> level);
	uint height = enf_max(1U, (uint)m_iHeight >> level);
	if(desc)
	{
		desc->Format = m_Format;
		desc->Usage = m_iFlags;
		desc->Pool = 0;
		desc->Width = width;
		desc->Height = height;
	}
	if(rect)
	{
		size_t stride = GetFormatStride(m_Format);
		if(!stride || rect->Right > width || rect->Bottom > height)
		{
			m_pStagingData->Unlock(0, level);
			return false;
		}
		lrect->pBits = (ubyte*)lrect->pBits + rect->Top * lrect->Pitch + rect->Left * stride;
	}
	return true;
}

void Texture::Unlock(uint level)
{
	if(m_pStagingData && level < m_iMipLevels)
		m_pStagingData->Unlock(0, level);
}

ERESULT Texture::CopyData(ImageData& imageData, uint flags)
{
	if(!m_pStagingData || imageData.GetFormat() != m_Format ||
		imageData.GetWidth() != m_iWidth || imageData.GetHeight() != m_iHeight)
		return EE_INVALIDPARAM;
	uint faces = enf_min(imageData.GetNumFaces(), m_pStagingData->GetNumFaces());
	uint levels = enf_min(imageData.GetNumLevels(), (uint)m_iMipLevels);
	for(uint face = 0; face < faces; ++face)
	{
		for(uint level = 0; level < levels; ++level)
		{
			LockedImageRect src, dst;
			if(!imageData.Lock(face, level, src))
				return EE_ERROR;
			if(!m_pStagingData->Lock(face, level, dst))
			{
				imageData.Unlock(face, level);
				return EE_ERROR;
			}
			uint width = enf_max(1U, (uint)m_iWidth >> level);
			uint height = enf_max(1U, (uint)m_iHeight >> level);
			size_t bytes = GetSurfaceSize(width, height, m_Format);
			memcpy(dst.pBits, src.pBits, bytes);
			m_pStagingData->Unlock(face, level);
			imageData.Unlock(face, level);
		}
	}
	return EE_OK;
}

ERESULT Texture::FillColor(const FColor& color, uint sx, uint sy, uint w, uint h)
{
	if(m_Format != TEXFMT_A8R8G8B8 && m_Format != TEXFMT_X8R8G8B8)
		return EE_ERROR;
	if(sx + w > m_iWidth || sy + h > m_iHeight)
		return EE_INVALIDPARAM;
	LockedImageRect locked;
	if(!Lock(0, NULL, &locked, NULL))
		return EE_ERROR;
	uint packed = (color * 255.0f).PackToInt();
	for(uint y = sy; y < sy + h; ++y)
	{
		uint* row = (uint*)((ubyte*)locked.pBits + y * locked.Pitch);
		for(uint x = sx; x < sx + w; ++x)
			row[x] = packed;
	}
	Unlock(0);
	return EE_OK;
}

uint Texture::GetMaxMipmaps(uint width, uint height, uint depth)
{
	uint count = 1;
	while(width != 1 || height != 1 || depth != 1)
	{
		width = enf_max(1U, width >> 1);
		height = enf_max(1U, height >> 1);
		depth = enf_max(1U, depth >> 1);
		++count;
	}
	return count;
}

int Texture::Release()
{
	if(Unref() == 0)
	{
		MemStats::m_iDisposedTextures += GetMemorySize();
		if(m_strName.IsEmpty() || m_pManager == NULL || (m_iFlags & TEXTR_NOCACHE))
		{
			Flush();
			return 0;
		}
	}
	return RefCount();
}

size_t Texture::GetMemorySize() const
{
	if(!m_pStagingData)
		return 0;
	size_t size = 0;
	uint width = enf_max(1U, (uint)m_iWidth);
	uint height = enf_max(1U, (uint)m_iHeight);
	for(uint mip = 0; mip < m_iMipLevels; ++mip)
	{
		size += GetSurfaceSize(width, height, m_Format);
		width = enf_max(1U, width >> 1);
		height = enf_max(1U, height >> 1);
	}
	return size * (IsCube() ? 6 : 1);
}

//-----------------------------------------------------------------------
bool Texture::Swap(Texture* other)
{
	enf_rlsassert(GetDevice()->CheckThread());
	enf_assert(other);
	DeviceResource::Swap(other);
	enf_swap(m_pStagingData, other->m_pStagingData);

#if defined(ENF_OGL)
	enf_swap(m_iTexture, other->m_iTexture);
#endif

	enf_swap(m_iWidth, other->m_iWidth);
	enf_swap(m_iHeight, other->m_iHeight);
	enf_swap(m_iMipLevels, other->m_iMipLevels);
	enf_swap(m_iFlags, other->m_iFlags);
	enf_swap(m_Usage, other->m_Usage);
	enf_swap(m_Format, other->m_Format);
	return true;
}

Texture* IRenderer::CreateTexture(CStr name, uint width, uint height, uint flags)
{
	enf_rlsassert(CheckThread());
	Texture* texture = ENF_NEW Texture(this, name, width, height, flags);
	if(!texture)
		return NULL;
	if(texture->Allocate() != EE_OK)
	{
		delete texture;
		return NULL;
	}
	AddResource(texture);
	return texture;
}

Texture* IRenderer::CreateTexture(CStr name, Texture* clone, uint flags)
{
	if(!clone)
		return NULL;
	Texture* texture = CreateTextureWithFormat(name, clone->GetWidth(),
		clone->GetHeight(), clone->GetFormat(), flags | TEXTR_CLONE |
		(clone->GetFlags() & (TEXTR_CUBE | TEXTR_NOMIPMAP)),
		clone->GetUsage());
	if(!texture || !clone->m_pStagingData)
		return texture;
	for(uint face = 0; face < clone->m_pStagingData->GetNumFaces(); ++face)
	{
		for(uint level = 0; level < enf_min(clone->GetNumLevels(), texture->GetNumLevels()); ++level)
		{
			LockedImageRect src, dst;
			if(clone->m_pStagingData->Lock(face, level, src) &&
				texture->m_pStagingData->Lock(face, level, dst))
			{
				uint width = enf_max(1U, clone->GetWidth() >> level);
				uint height = enf_max(1U, clone->GetHeight() >> level);
				memcpy(dst.pBits, src.pBits, GetSurfaceSize(width, height, clone->GetFormat()));
				texture->m_pStagingData->Unlock(face, level);
				clone->m_pStagingData->Unlock(face, level);
			}
		}
	}
	return texture;
}

Texture* IRenderer::CreateTextureWithFormat(CStr name, uint width, uint height,
	TextureFormat format, uint flags, TextureUsage usage)
{
	enf_rlsassert(CheckThread());
	if(!width || !height || width > MAX_TEXTURE_SIZE || height > MAX_TEXTURE_SIZE)
		return NULL;
	Texture* texture = ENF_NEW Texture(this, name, width, height, flags);
	if(!texture)
		return NULL;
	texture->m_Format = format;
	texture->m_Usage = usage;
	if(texture->Allocate() != EE_OK)
	{
		delete texture;
		return NULL;
	}
	AddResource(texture);
	return texture;
}

//-----------------------------------------------------------------------
size_t GetSourceSurfaceSize(TextureFormat fmt, uint srcDepth, uint width, uint height)
{
	switch(fmt)
	{
	case TEXFMT_CTX1:
	case TEXFMT_DXT1:
		return GetDXTBlocks(width) * GetDXTBlocks(height) * 8;

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
