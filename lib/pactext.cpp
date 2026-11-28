#include "wpch.hpp"

#include "global.hpp"
#include "pactext.hpp"
#include "textbank.hpp"
#include <Es/Types/pointers.hpp>
#include <Es/Containers/staticArray.hpp>
//#include <Es/Files/filenameOs.hpp>
#include <El/QStream/qbStream.hpp>
#include <El/Common/perfProf.hpp>
#include <El/Common/randomGen.hpp>
#include <El/PerlinNoise/perlinNoise.hpp>
#include <El/FileServer/fileServerMT.hpp>

#include "jpgImport.hpp"
#include "serializeBinExt.hpp"

#include <El/LZO/lzo/lzoconf.h>
#include <El/LZO/lzo/lzo1x.h>
#include <El/LZO/lzo/lzopro/lzo1x.h>

#pragma warning( disable : 4530 )

#include "oggtext.h"

bool IsWebSource(const char *filename)
{
  const char *prefixes[] = {"http:", "https:", "ftp:", "gopher:"};
  for (int i=0; i<lenof(prefixes); i++)
  {
    const char *prefix = prefixes[i];
    if (strncmp(filename, prefix, strlen(prefix)) == 0) return true;
  }
  return false;
}

typedef word Pixel;

inline Pixel Conv888To555( int rgb )
{
  int r=(rgb>>16)&0xff,g=(rgb>>8)&0xff,b=(rgb>>0)&0xff;
  r>>=3,g>>=3,b>>=3;
  return Pixel( (r<<10)|(g<<5)|(b<<0) );
}

inline Pixel Conv888To565( int rgb )
{
  int r=(rgb>>16)&0xff,g=(rgb>>8)&0xff,b=(rgb>>0)&0xff;
  r>>=3,g>>=2,b>>=3;
  return Pixel( (r<<11)|(g<<5)|(b<<0) );
}

struct DXTBlock64
{
  // see "Compressed Texture Formats" in DX Docs
  word c0,c1; // color data
  word tex0,tex1; // texel data
  
  DWORD GetColor(int x, int y,bool enableTransparency=true) const;
  void Decompress1555(word *base, int w, bool enableTransparency=true) const;
  void Decompress4444(word *base, int w, bool enableTransparency=true) const;
};

struct DXTBlock64AlphaExplicit
{
  word a[4];

  /// decompress one pixel
  DWORD GetAlpha(int x, int y) const;
  /// decompress - write alpha channel only
  void Decompress4444(word *base, int w) const;
};

struct DXTBlock64AlphaImplicit
{
  byte a0,a1; // 8b alpha
  byte tex[6];

  /// decompress one pixel
  DWORD GetAlpha(int x, int y) const;
  /// decompress - write alpha channel only
  void Decompress4444(word *base, int w) const;
};


struct DXTBlock128
{
  union
  {
    /// explicit alpha used for DXT2/3
    DXTBlock64AlphaExplicit alphaExp;
    /// implicit alpha used for DXT4/5
    DXTBlock64AlphaImplicit alphaImp;
  };
  DXTBlock64 color;

  /// use DXT2/3 decompression
  DWORD GetColor2(int x, int y) const;
  void Decompress4444_2(word *base, int w) const;
  /// use DXT4/5 decompression
  DWORD GetColor4(int x, int y) const;
  void Decompress4444_4(word *base, int w) const;
};

#ifdef _MSC_VER
  #define TAGG 'TAGG'
  #define AVGC 'AVGC'
  #define MAXC 'MAXC'
  #define FLAG 'FLAG'
  #define OFFS 'OFFS'
#else
  #define TAGG ((int)'G'+((int)'G'<<8)+((int)'A'<<16)+((int)'T'<<24))
  #define AVGC ((int)'C'+((int)'G'<<8)+((int)'V'<<16)+((int)'A'<<24))
  #define MAXC ((int)'C'+((int)'X'<<8)+((int)'A'<<16)+((int)'M'<<24))
  #define FLAG ((int)'G'+((int)'A'<<8)+((int)'L'<<16)+((int)'F'<<24))
  #define OFFS ((int)'S'+((int)'F'<<8)+((int)'F'<<16)+((int)'O'<<24))
#endif


static inline int Convert5To8( int x )
{
  //return toIntFloor(x*(255.0/31)); // exact but not fast
  return (x*8424)>>10; // much faster
}

static inline int Convert6To8( int x )
{
  //return toIntFloor(x*(255.0/31)); // exact but not fast
  return (x*8290)>>11; // much faster
}

static inline int Convert565To8888( int x )
{
  int r = Convert5To8((x>>11)&0x1f);
  int g = Convert6To8((x>>5 )&0x3f);
  int b = Convert5To8((x    )&0x1f);
  return 0xff000000|(r<<16)|(g<<8)|b;
}

static inline int Convert1555To8888( int x )
{
  int a = (x >> 15) ? 0xff : 0;
  int r = Convert5To8((x>>10)&0x1f);
  int g = Convert5To8((x>>5 )&0x1f);
  int b = Convert5To8((x    )&0x1f);
  return (a<<24)|(r<<16)|(g<<8)|b;
}

static inline int Convert565To1555( int x )
{
  return 0x8000|(x&0x1f)|((x>>1)&0x7fe0);
}

static inline int Convert565To4444( int x )
{
  int r = ((x>>11)&0x1f)>>1;
  int g = ((x>>5 )&0x3f)>>2;
  int b = ((x    )&0x1f)>>1;
  return 0xf000000|(r<<8)|(g<<4)|b;
}

// note: int. division very slow - precalc. table instead

static char Div3[128];

static struct Div3Init
{
  Div3Init()
  {
    for (int i=64; i<128; i++)
    {
      Div3[i]=(i-64)/3;
    }
    for (int i=0; i<64; i++)
    {
      Div3[i]=-(-(i-64)/3);
    }
  }
} dummy;


#if _M_PPC
static inline unsigned short SwapEndian16b(unsigned short d)
{
  return (d<<8)|(d>>8);
}
#endif

DWORD DXTBlock64::GetColor(int x, int y, bool enableTransparency) const
{
  DWORD color[4];
  if (c0>c1 || !enableTransparency)
  {
    // 4 color block
    int rc0 = (c0>>11)&0x1f;
    int gc0 = (c0>> 5)&0x3f;
    int bc0 = (c0    )&0x1f;
    int rc1 = (c1>>11)&0x1f;
    int gc1 = (c1>> 5)&0x3f;
    int bc1 = (c1    )&0x1f;
    // c0-c1 is positive
    int rd3 = Div3[rc1-rc0+64];
    int gd3 = Div3[gc1-gc0+64];
    int bd3 = Div3[bc1-bc0+64];
    //color[2] = c0 + (c1-c0)/3; // (2 * c0 + c1) / 3;
    //color[3] = c1 - (c1-c0)/3; // (c0 + 2 * c1) / 3;
    // convert 565 to 1555
    // verify c2, c3 components in range
    int rc2 = rc0+rd3;
    int gc2 = gc0+gd3;
    int bc2 = bc0+bd3;

    int rc3 = rc1-rd3;
    int gc3 = gc1-gd3;
    int bc3 = bc1-bd3;
    int c2 = (rc2<<11)|(gc2<<5)|bc2;
    int c3 = (rc3<<11)|(gc3<<5)|bc3;

    color[0]= Convert565To8888(c0);
    color[1]= Convert565To8888(c1);
    color[2]= Convert565To8888(c2);
    color[3]= Convert565To8888(c3);
  }
  else if (c0==c1)
  {
    // mono-color transparent block
    DWORD c08888 = Convert565To8888(c0);
    color[0]= c08888;
    color[1]= c08888;
    color[2]= c08888;
    color[3] = c08888&0x00ffffff;
  }
  else
  {
    int rc0 = (c0>>11)&0x1f;
    int gc0 = (c0>> 5)&0x3f;
    int bc0 = (c0    )&0x1f;
    int rc1 = (c1>>11)&0x1f;
    int gc1 = (c1>> 5)&0x3f;
    int bc1 = (c1    )&0x1f;

    int rd2 = (rc1-rc0)>>1;
    int gd2 = (gc1-gc0)>>1;
    int bd2 = (bc1-bc0)>>1;
    
    int c2 = ((rc0+rd2)<<11)|((gc0+gd2)<<5)|(bc0+bd2);

    color[0]= Convert565To8888(c0);
    color[1]= Convert565To8888(c1);
    color[2]= Convert565To8888(c2);
    color[3] = color[2]&0x00ffffff;
    // 3 color + transparency block
  }

  int b = (y<<2)|x;
  switch(b)
  {
    case 0: return color[(tex0    )&3]; // 1:0 Texel[0][0] 
    case 1: return color[(tex0>> 2)&3]; // 3:2 Texel[0][1] 
    case 2: return color[(tex0>> 4)&3]; // 5:4 Texel[0][2] 
    case 3: return color[(tex0>> 6)&3]; // 7:6 Texel[0][3] 

    case 4: return color[(tex0>> 8)&3]; // 9:8 Texel[1][0] 
    case 5: return color[(tex0>>10)&3]; // 11:10 Texel[1][1] 
    case 6: return color[(tex0>>12)&3]; // 13:12 Texel[1][2] 
    case 7: return color[(tex0>>14)&3]; // 15:14 Texel[1][3] 

    case 8: return color[(tex1    )&3]; // 1:0 Texel[2][0] 
    case 9: return color[(tex1>> 2)&3]; // 3:2 Texel[2][1] 
    case 10: return color[(tex1>> 4)&3]; // 5:4 Texel[2][2] 
    case 11: return color[(tex1>> 6)&3]; // 7:6 Texel[2][3] 

    case 12: return color[(tex1>> 8)&3]; // 9:8 Texel[3][0] 
    case 13: return color[(tex1>>10)&3]; // 11:10 Texel[3][1] 
    case 14: return color[(tex1>>12)&3]; // 13:12 Texel[3][2] 
    case 15: return color[(tex1>>14)&3]; // 15:14 Texel[3][3] 
    default:
    return 0;
  }
}

void DXTBlock64::Decompress1555(word *base, int w, bool enableTransparency) const
{
  word color[4];
  // decompress current block
  if (c0>c1)
  {
    // 4 color block
    int rc0 = (c0>>11)&0x1f;
    int gc0 = (c0>> 5)&0x3f;
    int bc0 = (c0    )&0x1f;
    int rc1 = (c1>>11)&0x1f;
    int gc1 = (c1>> 5)&0x3f;
    int bc1 = (c1    )&0x1f;
    // c0-c1 is positive
    int rd3 = Div3[rc1-rc0+64];
    int gd3 = Div3[gc1-gc0+64];
    int bd3 = Div3[bc1-bc0+64];
    //color[2] = c0 + (c1-c0)/3; // (2 * c0 + c1) / 3;
    //color[3] = c1 - (c1-c0)/3; // (c0 + 2 * c1) / 3;
    // convert 565 to 1555
    // verify c2, c3 components in range
    int rc2 = rc0+rd3;
    int gc2 = gc0+gd3;
    int bc2 = bc0+bd3;

    int rc3 = rc1-rd3;
    int gc3 = gc1-gd3;
    int bc3 = bc1-bd3;
    int c2 = (rc2<<11)|(gc2<<5)|bc2;
    int c3 = (rc3<<11)|(gc3<<5)|bc3;
    color[0]= Convert565To1555(c0);
    color[1]= Convert565To1555(c1);
    color[2]= Convert565To1555(c2);
    color[3]= Convert565To1555(c3);
  }
  else if (c0==c1)
  {
    // mono-color transparent block
    word c01555 = Convert565To1555(c0);
    color[0]= c01555;
    color[1]= c01555;
    color[2]= c01555;
    color[3] = c01555&0x7fff;
  }
  else
  {
    int rc0 = (c0>>11)&0x1f;
    int gc0 = (c0>> 5)&0x3f;
    int bc0 = (c0    )&0x1f;
    int rc1 = (c1>>11)&0x1f;
    int gc1 = (c1>> 5)&0x3f;
    int bc1 = (c1    )&0x1f;

    int rd2 = (rc1-rc0)>>1;
    int gd2 = (gc1-gc0)>>1;
    int bd2 = (bc1-bc0)>>1;
    
    int c2 = ((rc0+rd2)<<11)|((gc0+gd2)<<5)|(bc0+bd2);

    color[0]= Convert565To1555(c0);
    color[1]= Convert565To1555(c1);
    color[2]= Convert565To1555(c2);
    color[3] = color[2]&0x7fff;
    // 3 color + transparency block
  }

  // get dibits
  int w0 = tex0;
  int w1 = tex1;

  word *tbase = base;
  *tbase++ = color[(w0    )&3]; // 1:0 Texel[0][0] 
  *tbase++ = color[(w0>> 2)&3]; // 3:2 Texel[0][1] 
  *tbase++ = color[(w0>> 4)&3]; // 5:4 Texel[0][2] 
  *tbase   = color[(w0>> 6)&3]; // 7:6 Texel[0][3] 
  tbase += w-3;

  *tbase++ = color[(w0>> 8)&3]; // 9:8 Texel[1][0] 
  *tbase++ = color[(w0>>10)&3]; // 11:10 Texel[1][1] 
  *tbase++ = color[(w0>>12)&3]; // 13:12 Texel[1][2] 
  *tbase   = color[(w0>>14)&3]; // 15:14 Texel[1][3] 
  tbase += w-3;

  *tbase++ = color[(w1    )&3]; // 1:0 Texel[2][0] 
  *tbase++ = color[(w1>> 2)&3]; // 3:2 Texel[2][1] 
  *tbase++ = color[(w1>> 4)&3]; // 5:4 Texel[2][2] 
  *tbase   = color[(w1>> 6)&3]; // 7:6 Texel[2][3] 
  tbase += w-3;

  *tbase++ = color[(w1>> 8)&3]; // 9:8 Texel[3][0] 
  *tbase++ = color[(w1>>10)&3]; // 11:10 Texel[3][1] 
  *tbase++ = color[(w1>>12)&3]; // 13:12 Texel[3][2] 
  *tbase   = color[(w1>>14)&3]; // 15:14 Texel[3][3] 
}

void DXTBlock64::Decompress4444(word *base, int w, bool enableTransparency) const
{
  word color[4];
  // decompress current block
  if (c0>c1)
  {
    // 4 color block
    int rc0 = (c0>>11)&0x1f;
    int gc0 = (c0>> 5)&0x3f;
    int bc0 = (c0    )&0x1f;
    int rc1 = (c1>>11)&0x1f;
    int gc1 = (c1>> 5)&0x3f;
    int bc1 = (c1    )&0x1f;
    // c0-c1 is positive
    int rd3 = Div3[rc1-rc0+64];
    int gd3 = Div3[gc1-gc0+64];
    int bd3 = Div3[bc1-bc0+64];
    //color[2] = c0 + (c1-c0)/3; // (2 * c0 + c1) / 3;
    //color[3] = c1 - (c1-c0)/3; // (c0 + 2 * c1) / 3;
    // convert 565 to 1555
    // verify c2, c3 components in range
    int rc2 = rc0+rd3;
    int gc2 = gc0+gd3;
    int bc2 = bc0+bd3;

    int rc3 = rc1-rd3;
    int gc3 = gc1-gd3;
    int bc3 = bc1-bd3;
    int c2 = (rc2<<11)|(gc2<<5)|bc2;
    int c3 = (rc3<<11)|(gc3<<5)|bc3;
    color[0]= Convert565To4444(c0);
    color[1]= Convert565To4444(c1);
    color[2]= Convert565To4444(c2);
    color[3]= Convert565To4444(c3);
  }
  else if (c0==c1)
  {
    // mono-color transparent block
    word c01555 = Convert565To4444(c0);
    color[0]= c01555;
    color[1]= c01555;
    color[2]= c01555;
    color[3] = c01555&0x0fff;
  }
  else
  {
    int rc0 = (c0>>11)&0x1f;
    int gc0 = (c0>> 5)&0x3f;
    int bc0 = (c0    )&0x1f;
    int rc1 = (c1>>11)&0x1f;
    int gc1 = (c1>> 5)&0x3f;
    int bc1 = (c1    )&0x1f;

    int rd2 = (rc1-rc0)>>1;
    int gd2 = (gc1-gc0)>>1;
    int bd2 = (bc1-bc0)>>1;
    
    int c2 = ((rc0+rd2)<<11)|((gc0+gd2)<<5)|(bc0+bd2);

    color[0]= Convert565To4444(c0);
    color[1]= Convert565To4444(c1);
    color[2]= Convert565To4444(c2);
    color[3] = color[2]&0x0fff;
    // 3 color + transparency block
  }

  // get dibits
  int w0 = tex0;
  int w1 = tex1;

  word *tbase = base;
  *tbase++ = color[(w0    )&3]; // 1:0 Texel[0][0] 
  *tbase++ = color[(w0>> 2)&3]; // 3:2 Texel[0][1] 
  *tbase++ = color[(w0>> 4)&3]; // 5:4 Texel[0][2] 
  *tbase   = color[(w0>> 6)&3]; // 7:6 Texel[0][3] 
  tbase += w-3;

  *tbase++ = color[(w0>> 8)&3]; // 9:8 Texel[1][0] 
  *tbase++ = color[(w0>>10)&3]; // 11:10 Texel[1][1] 
  *tbase++ = color[(w0>>12)&3]; // 13:12 Texel[1][2] 
  *tbase   = color[(w0>>14)&3]; // 15:14 Texel[1][3] 
  tbase += w-3;

  *tbase++ = color[(w1    )&3]; // 1:0 Texel[2][0] 
  *tbase++ = color[(w1>> 2)&3]; // 3:2 Texel[2][1] 
  *tbase++ = color[(w1>> 4)&3]; // 5:4 Texel[2][2] 
  *tbase   = color[(w1>> 6)&3]; // 7:6 Texel[2][3] 
  tbase += w-3;

  *tbase++ = color[(w1>> 8)&3]; // 9:8 Texel[3][0] 
  *tbase++ = color[(w1>>10)&3]; // 11:10 Texel[3][1] 
  *tbase++ = color[(w1>>12)&3]; // 13:12 Texel[3][2] 
  *tbase   = color[(w1>>14)&3]; // 15:14 Texel[3][3] 
}

__forceinline void SetA(word &t, word s)
{
  t &= 0xffff;
  t |= s<<12;
}

void DXTBlock64AlphaExplicit::Decompress4444(word *base, int w) const
{
  word *tbase = base;
  SetA(*tbase++, (a[0]>>0)); // Texel[0][0] 
  SetA(*tbase++, (a[0]>>4)); // Texel[0][1] 
  SetA(*tbase++, (a[0]>>8)); // Texel[0][2] 
  SetA(*tbase,   (a[0]>>12)); // Texel[0][3] 
  tbase += w-3;

  SetA(*tbase++, (a[1]>>0)); // Texel[1][0] 
  SetA(*tbase++, (a[1]>>4)); // Texel[1][1] 
  SetA(*tbase++, (a[1]>>8)); // Texel[1][2] 
  SetA(*tbase,   (a[1]>>12)); // Texel[1][3] 
  tbase += w-3;

  SetA(*tbase++, (a[2]>>0)); // Texel[2][0] 
  SetA(*tbase++, (a[2]>>4)); // Texel[2][1] 
  SetA(*tbase++, (a[2]>>8)); // Texel[2][2] 
  SetA(*tbase,   (a[2]>>12)); // Texel[2][3] 
  tbase += w-3;

  SetA(*tbase++, (a[3]>>0)); // Texel[3][0] 
  SetA(*tbase++, (a[3]>>4)); // Texel[3][1] 
  SetA(*tbase++, (a[3]>>8)); // Texel[3][2] 
  SetA(*tbase,   (a[3]>>12)); // Texel[3][3] 
}

DWORD DXTBlock64AlphaExplicit::GetAlpha(int x, int y) const
{
  int b = (y<<2)|x;

  int index = b>>2; // 0..3
  int offset = (b&3)*4; // 0..3*4
  
  DWORD bits = a[index];
  
  int a4 = (bits>>offset)&15;
  // convert 0..15 into 0..255 range
  // this is easy - 255/15 is exactly 17
  return a4*17;
}

template <int bits>
static void Create8Alphas8(int *alpha, int a0, int a1)
{
  int shift = 8-bits;
  const float inv7 = 1.0f/7;
  alpha[0] = a0;
  alpha[1] = a1;

  alpha[2] = toIntFloor((6 * a0 + 1 * a1 + 3) *inv7)>>shift;    // bit code 010
  alpha[3] = toIntFloor((5 * a0 + 2 * a1 + 3) *inv7)>>shift;    // bit code 011
  alpha[4] = toIntFloor((4 * a0 + 3 * a1 + 3) *inv7)>>shift;    // bit code 100
  alpha[5] = toIntFloor((3 * a0 + 4 * a1 + 3) *inv7)>>shift;    // bit code 101
  alpha[6] = toIntFloor((2 * a0 + 5 * a1 + 3) *inv7)>>shift;    // bit code 110
  alpha[7] = toIntFloor((1 * a0 + 6 * a1 + 3) *inv7)>>shift;    // bit code 111  
}

template <int bits>
static void Create8Alphas6(int *alpha, int a0, int a1)
{
  int shift = 8-bits;
  const float inv5 = 1.0f/5;
  alpha[0] = a0;
  alpha[1] = a1;

  alpha[2] = toIntFloor((4 * a0 + 1 * a1 + 2) *inv5)>>shift;    // bit code 010
  alpha[3] = toIntFloor((3 * a0 + 2 * a1 + 2) *inv5)>>shift;    // bit code 011
  alpha[4] = toIntFloor((2 * a0 + 3 * a1 + 2) *inv5)>>shift;    // bit code 100
  alpha[5] = toIntFloor((1 * a0 + 4 * a1 + 2) *inv5)>>shift;    // bit code 101
  alpha[6] = 0;    // bit code 110
  alpha[7] = 0xff>>shift;    // bit code 111  
}

void DXTBlock64AlphaImplicit::Decompress4444(word *base, int w) const
{
  word *tbase = base;

  int alpha[8];
  if (a0>a1)
  {
    Create8Alphas8<4>(alpha,a0,a1);
  }
  else
  {
    Create8Alphas6<4>(alpha,a0,a1);
  }

	const DWORD mask = 7; // bits = 00 00 01 11

	DWORD bits = *( (DWORD*) & ( tex[0] ));

	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase,   alpha[bits & mask]);
	bits >>= 3;
  tbase += w-3;

	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase,   alpha[bits & mask]);
  tbase += w-3;

	// now for last two rows:

	bits = *( (DWORD*) & ( tex[3] ));		// last 3 bytes

	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase,   alpha[bits & mask]);
	bits >>= 3;
  tbase += w-3;

	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase++, alpha[bits & mask]);
	bits >>= 3;
	SetA(*tbase,   alpha[bits & mask]);
}

DWORD DXTBlock64AlphaImplicit::GetAlpha(int x, int y) const
{
  int b = (y<<2)|x;

  int alpha[8];
  if (a0>a1)
  {
    Create8Alphas8<8>(alpha,a0,a1);
  }
  else
  {
    Create8Alphas6<8>(alpha,a0,a1);
  }

  // check which byte and which bits of it do we need
  int bitIndex = b*3;
  int byteIndex = bitIndex>>3;
  int byteShift = byteIndex<<3;
  int posInByte = bitIndex-byteShift;
  int value;
  if (posInByte>8-3)
  {
    int twoBytes = tex[byteIndex]|(tex[byteIndex+1]<<8);
    value = (twoBytes>>posInByte)&7;
  }
  else
  {
    value = (tex[byteIndex]>>posInByte)&7;
  }

  return alpha[value];
}

DWORD DXTBlock128::GetColor4(int x, int y) const
{
  DWORD c = color.GetColor(x,y,false);
  DWORD a = alphaImp.GetAlpha(x,y);
  return (a<<24)|(c&0xffffff);
}

DWORD DXTBlock128::GetColor2(int x, int y) const
{
  DWORD c = color.GetColor(x,y,false);
  DWORD a = alphaExp.GetAlpha(x,y);
  return (a<<24)|(c&0xffffff);
}

void DXTBlock128::Decompress4444_4(word *base, int w) const
{
  // decompress color block
  color.Decompress4444(base,w,false);
  // perform alpha decompression
  alphaImp.Decompress4444(base,w);
}

void DXTBlock128::Decompress4444_2(word *base, int w) const
{
  // decompress color block
  color.Decompress4444(base,w,false);
  // perform alpha decompression
  alphaExp.Decompress4444(base,w);
}

// load texture map from PAC file

int PacPalette::Skip( QIStream &in )
{
  // each texture file starts with palette definition
  // return<0 - error
  // return==0 - texture loaded successfully
  int error=-1;
  
  for(;;)
  {
    // skip any taggs
    DWORD magic=in.getil();
    if( magic!=TAGG )
                {in.seekg(-4,QIOS::cur);break;}
    // some tagg - load it
    DWORD tagg=in.getil();
    (void)tagg;
    int size=in.getil();
    Assert( size>=0 );
    in.seekg(size,QIOS::cur);
    if( in.eof() || in.fail() ) goto Error;
  }

  enum {psize=256};

  // get palette  size
  long C;
  C=in.getiw();
  if( in.eof() || in.fail() ) goto Error;

  if( C<0 || C>psize ) {error=1;goto Error;} // mip-map list terminator

  in.seekg(3*C,QIOS::cur);
  if( in.eof() || in.fail() ) goto Error;
    
  error=0;
    
  Error:
  return error;
}

PacPalette::PacPalette()
:_nColors(0),_palette(NULL), //_palPixel(NULL),
_transparentColor(-1),_averageColor(HBlack),_averageColor32(0xff000000),
_isTransparent(false),_isAlpha(false),_isAlphaNonOpaque(false)
{
}

PacPalette::~PacPalette()
{
  if( _palette ) delete[] _palette;
  //if( _palPixel ) delete[] _palPixel;
}

PacPalette::PacPalette(const PacPalette &src) :_palette(NULL)
{
  Clone(src);
}

PacPalette& PacPalette::operator=( const PacPalette &src)
{
  Clone(src);
  return *this;
}

void PacPalette::Clone(const PacPalette &src)
{
  _nColors = src._nColors;
  delete [] _palette;
  _palette = NULL;
  _averageColor = src._averageColor;
  _averageColor32 = src._averageColor32;
  _maxColor32 = src._maxColor32;
  _clampFlags = src._clampFlags;
  _transparentColor = src._transparentColor;
  _maxColorSet = src._maxColorSet;
  _isAlpha = src._isAlpha;
  _isTransparent = src._isTransparent;
  _isAlphaNonOpaque = src._isAlphaNonOpaque;

#if CHECKSUMS
  _checksumValid = src._checksumValid;
  _checksum = src._checksum;
#endif

  if (_nColors > 0)
  {
    _palette = new DWORD[_nColors];
    for (int i=0; i<_nColors; ++i)    
      _palette[i] = src._palette[i];
  } 
}

enum PicFlags
{
  PicFlagAlpha=1,
  PicFlagTransparent=2,
};

int PacPalette::Load( QIStream &in, int *startOffsets, int maxStartOffsets, PacFormat format )
{
  int error=-1;
  // each texture file starts with palette definition
  // return<0 - error
  // return==0 - texture loaded successfully

  _clampFlags = 0; // by default there is no clamping
  _maxColor32 = PackedWhite;
  _maxColorSet = false;
  switch (format)
  {
    case PacARGB4444:
    case PacARGB8888:
    case PacAI88:
      //_averageColor32=PackedColor(0x80505050);
      _averageColor32=PackedColor(0x80c02020);
      break;
    default:
      //_averageColor32=PackedColor(0xff505050);
      _averageColor32=PackedColor(0xff802020);
      break;
  }
  //_averageColor32=PackedColor(0); // default - transparent
  for(;;)
  {
    // skip/load any taggs
    DWORD magic=in.getil();
    if( magic!=TAGG )
    {in.seekg(-4,QIOS::cur);break;}
    // some tagg - load it
    DWORD tagg=in.getil();
    int size=in.getil();
    if( in.eof() || in.fail() ) goto Error;
    Assert( size>=0 );
    switch( tagg )
    {
      case MAXC:
        Assert( size==sizeof(_maxColor32) );
        _maxColor32=PackedColor(in.getil());
        _maxColorSet = true;
      break;
      case AVGC:
        Assert( size==sizeof(_averageColor32) );
        _averageColor32=PackedColor(in.getil());
      break;
      case FLAG:
      {
        int flags = in.getil();
        if (flags&PicFlagAlpha) _isAlpha = true;
        if (flags&PicFlagTransparent) _isTransparent = true;
      }
      break;
      case OFFS:
      {
        int nOffs = size/sizeof(int);
        for (int i=0; i<nOffs; i++)
        {
          int data = in.getil();
          if (i<maxStartOffsets) startOffsets[i]=data;
        }
      } 
      break;
      default:
        in.seekg(size,QIOS::cur);
      break;
    }
  }
  _averageColor=Color((long)_averageColor32);
  if (_isAlpha)
  {
    // check overall opacity based on a average color
    if (_averageColor32.A8()<0x80)
    {
      _isAlphaNonOpaque = true;
    }
  }
  // load PAC file
  enum {psize=256};
  int i;

  // get palette  size
  long C;
  C=in.getiw();

  if( C<0 || C>psize ) {error=1;goto Error;} // mip-map list terminator

  _nColors=C;
  //_heap=heap;
  
  if( !_palette && _nColors>0) _palette=new DWORD[_nColors]; // packed 24-bit color (palette)
  //if( !_palPixel && _nColors>0 ) _palPixel=new Pixel[_nColors]; // packed 24-bit color (palette)

  _transparentColor=-1;
  if( _palette || _nColors==0 )
  {   
    for( i=0; i<C; i++ )
    {
      _palette[i]=in.geti24();
      //_palPixel[i]=Conv888To555(_palette[i]);
    }

    if( in.eof() || in.fail() ) goto Error;
    
    // check if texture contains transparent color
    for( i=0; i<_nColors; i++ )
    {
      if( _palette[i]==TRANSPARENT1_RGB ) _transparentColor=i;
      if( _palette[i]==TRANSPARENT2_RGB ) _transparentColor=i;
    }
    
    error=0;
    
    if( _transparentColor>=0 )
    {
      // convert transparent color to average texture color
      DWORD avg=_averageColor32;
      avg&=0xffffff; // transparent
      _palette[_transparentColor]=avg;
      _isTransparent = true;
    }
  }


//  if (_averageColor32>0)
//  {
//    // some value loaded - it should be correct
//    if ((_averageColor32&0xff000000)==0xff000000)
//    {
//      // it is not transparent, but is assumed to be
//      _isTransparent = false;
//    }
//    else
//    {
//      // it should be alpha or transparent
//      if (!_isAlpha) _isTransparent = true;
//    }
//  }

  /*
  // check transparency by averageColor?
  if (_isTransparent)
  {
    LogF("  T Average color A %.3f",_averageColor.A());
  }
  else
  {
    LogF("  O Average color A %.3f",_averageColor.A());
  }
  */

  Error:
  if( error<0 )
  {
    //if( _palPixel ) delete[] _palPixel,_palPixel=NULL;
    if( _palette ) delete[] _palette,_palette=NULL;
    _nColors=0;
  }
  return error;
}

void PacPalette::SerializeBin(SerializeBinStream &f)
{
  f.Transfer(_nColors);
  if (f.IsLoading())
  {
    if (_palette)
    {
      delete [] _palette;
      _palette = NULL;
    }
    if (_nColors > 0)
      _palette = new DWORD[_nColors];
  }

  if (_nColors > 0)
  {
    for (int i=0; i<_nColors; ++i)
      f.Transfer(_palette[i]);
  }

  f.Transfer(_averageColor);
  f.Transfer(_averageColor32);
  f.Transfer(_maxColor32);
  f.Transfer(_clampFlags);
  f.Transfer(_transparentColor);
  f.Transfer(_maxColorSet);
  f.Transfer(_isAlpha);
  f.Transfer(_isTransparent);
  f.Transfer(_isAlphaNonOpaque);
}

//! Macro to compare 2 strings in the length of the second one
#define STRSCMP(a,b) strncmp((a), (b), lenof(b)-1)

//! Function to calculate texture type from the extension string
TextureType GetTextureTypeFromExtension(const char *ext, const char *plain)
{
  // Note: Speed optimization - we've got guaranteed the texture name is in lower case

  // Detail ones
  // handle most common case first
  if (!STRSCMP(ext, "_co.")) return TT_Diffuse;
  else if (!STRSCMP(ext, "_ca."))
  {
    // detect a special case - _ti_ca used for thermal images
    if (ext>=plain+3)
    {
      if (!STRSCMP(ext-3,"_ti"))
      {
        return TT_TI;
      }
    }
    return TT_Diffuse;
  }
  else if (!STRSCMP(ext, "_detail.")) return TT_Detail;
  else if (!STRSCMP(ext, "_cdt.")) return TT_Detail;
  else if (!STRSCMP(ext, "_dt.")) return TT_Detail;
  else if (!STRSCMP(ext, "_mco.")) return TT_Detail;

  // Normal ones (speed optimization - whatever starts with "_n" is a normal map)
  //        if (!STRSCMP(ext, "_n")) return TT_Normal;
  // caution: some older (ArmA) textures have no suffix
  // names like blabla_new.paa are used
  else if (!STRSCMP(ext, "_no.")) return TT_Normal;
  else if (!STRSCMP(ext, "_non.")) return TT_Normal;
  else if (!STRSCMP(ext, "_nopx.")) return TT_Normal;
  else if (!STRSCMP(ext, "_noex.")) return TT_Normal;
  else if (!STRSCMP(ext, "_nohq.")) return TT_Normal;
  else if (!STRSCMP(ext, "_novhq.")) return TT_Normal;
  else if (!STRSCMP(ext, "_nofhq.")) return TT_Normal;
  else if (!STRSCMP(ext, "_nof.")) return TT_Normal;
  else if (!STRSCMP(ext, "_nofex.")) return TT_Normal;
  else if (!STRSCMP(ext, "_ns.")) return TT_Normal;
  else if (!STRSCMP(ext, "_nsex.")) return TT_Normal;
  else if (!STRSCMP(ext, "_nshq.")) return TT_Normal;
  else if (!STRSCMP(ext, "_normalmap.")) return TT_Normal;
  // Rest
  else if (!STRSCMP(ext, "_mask.")) return TT_Mask;
  else if (!STRSCMP(ext, "_mc.")) return TT_Macro;
  else if (!STRSCMP(ext, "_as.")) return TT_AmbientShadow;
  else if (!STRSCMP(ext, "_sm.")) return TT_Specular;
  else if (!STRSCMP(ext, "_smdi.")) return TT_Specular;
  else if (!STRSCMP(ext, "_dtsmdi.")) return TT_DTSpecular;
  else if (!STRSCMP(ext, "_sky.")) return TT_Diffuse_Linear;
  else if (!STRSCMP(ext, "_lco."))
  {
    // TODO: we might want SRGB applied on satellite imagery
    // satellite texture looks like:
    // 0123456789
    // S_000_000_lco.png
    if (ext==plain+9 && plain[0]=='s' && plain[1]=='_' && plain)
    {
      return TT_Diffuse;
    }
    else
    {
      return TT_Diffuse_Linear;
    }
  }
  else return TT_Diffuse;
}

TextureType GetTextureTypeFromName(const char *name)
{
  // Get the texture source type upon the "_XYZ." extension
  // Find the last dot
  const char *plain = GetFilenameExt(name);
  const char *ext=GetFileExt(plain);
  if (ext>plain)
  {
    // Find the first '_' before the dot (and watch we're still inside the string)
    while ((*ext != '_') && (ext > plain)) ext--;
    if (*ext == '_')
    {
      return GetTextureTypeFromExtension(ext, plain);
    }
  }
  return TT_Diffuse;
}

#define PAA_4444 0x4444
#define PAA_8080 0x8080
#define PAA_1555 0x1555
#define PAA_DXT1 0xff01
#define PAA_DXT2 0xff02
#define PAA_DXT3 0xff03
#define PAA_DXT4 0xff04
#define PAA_DXT5 0xff05

PacFormat PacFormatFromDesc( int desc, bool &alpha )
{
  PacFormat format = NPacFormat;
  alpha = false;
  if( desc==PAA_8080 ) format=PacAI88, alpha=true;
  else if( desc==PAA_4444 ) format=PacARGB4444, alpha=true;
  else if( desc==PAA_1555 ) format=PacARGB1555;
  else if( desc==PAA_DXT1 ) format=PacDXT1;
  else if( desc==PAA_DXT2 ) format=PacDXT2;
  else if( desc==PAA_DXT3 ) format=PacDXT3;
  else if( desc==PAA_DXT4 ) format=PacDXT4;
  else if( desc==PAA_DXT5 ) format=PacDXT5;
  return format;
}


PacLevelMem::PacLevelMem()
:
//_memData(NULL),
_w(0),_h(0),
_pitch(0),_sFormat(PacP8),_dFormat(PacARGB1555),
_start(-1) // invalid offset
{
}

// general LZW decompression routine

#define N 4096

#define N    4096 // textbuffer length
#define F     18 // max. match len
#define THRESHOLD 2 // min. match len


static int DecodeLZW(
  QIStream &in, char *dst, long lensb, int byteW, int pitch,
  Pixel *resPal=NULL, const PacPalette *pal=NULL
)
{
  if( lensb<=0 ) return 0;

  char text_buf[N + F - 1];
  int i,j,r,c,csum=0;
  int flags;
  int lineCnt=byteW;
  int lineAlign=pitch-byteW;
  int pSize=( resPal ? sizeof(Pixel) : sizeof(char) );
  //Pixel lastVV=0;
  //for( i=0; i<N-F; i++ ) text_buf[i] = ' ';
  memset(text_buf,' ',N-F);
  r=N-F; flags=0;
  while( lensb>0 )
  {
    if( ((flags>>= 1)&256)==0 )
    {
      c=in.get();
      flags=c|0xff00;
    }
    if( in.fail() || in.eof() )
    {
      Fail("LZW: stream read failed");
      return -1;
    }
    if( flags&1 )
    {
      c=in.get();
      if( in.fail() || in.eof() )
      {
        Fail("LZW: stream read failed");
        return -1;
      }
      csum+=(char)c;
      // save pixel
      if( !resPal ) *dst=c;
      else
      {
        //if( c==pal->_transparentColor ) *(word *)dst=lastVV&0x7fff;
        //else lastVV=
        *(word *)dst=resPal[c];
      }
      dst+=pSize;
      lensb--;
      lineCnt-=pSize;
      if( lineCnt==0 ) dst+=lineAlign,lineCnt=byteW;
      // continue decompression
      text_buf[r]=(char)c;
      r++;r&=(N-1);
    }
    else
    {
      i=in.get();
      j=in.get();
      if( in.fail() || in.eof() )
      {
        Fail("LZW: stream read failed");
        return -1;
      }
      i|=(j&0xf0)<<4; j&=0x0f; j+=THRESHOLD;
      for( i=r-i,j+=i; i<=j; i++ )
      {
        c=(byte)text_buf[i&(N-1)];
        csum+=(char)c;
        // save pixel
        if( !resPal ) *dst=c;
        else
        {
          //if( c==pal->_transparentColor ) *(word *)dst=lastVV&0x7fff;
          //else lastVV=
          *(word *)dst=resPal[c];
        }
        dst+=pSize;
        lensb--;
        lineCnt-=pSize;
        if( lineCnt==0 ) dst+=lineAlign,lineCnt=byteW;
        // continue decompression
        text_buf[r]=(char)c;
        r++;r&=(N-1);
      }
    }
  }
  int csr = in.getil();
  if( in.fail() || in.eof() )
  {
    Fail("LZW: end of stream");
    return -1;
  }
  if( csr!=csum )
  {
    Fail("Checksum");
    return -1;
  }
  return 0;
}


/*
bool PacLevelMem::Check( int max ) const
{
  bool error=false;
  char *mem=(char *)_memData;
  int size=_w*_h;
  while( --size>=0 )
  {
    int c=(unsigned char)*mem++;
    if( c>=max ) error=true,mem[-1]=max-1;
  }
  return !error;
}
*/

#define MAGIC_W_LZW 1234
#define MAGIC_H_LZW 8765


int PacLevelMem::LoadPacARGB1555( QIStream &in, void *mem, const PacPalette *pal ) const
{
  RptF("Obsolete palette format texture");
  // picture is saved in Palette 8 format
  // load as raw ARGB 1555 data
  // return<0 - error
  // return>0 - no more mip-map levels available
  // return==0 - texture loaded successfully

  // load PAC file
  
  // resulting surface is 16b-rgb
  // prepare a palette for conversion
  //Temp<word> resPal(pal->_nColors);
  word resPal[256];
  Assert( pal->_nColors<=256 );
  int i;
  for( i=0; i<pal->_nColors; i++ )
  {
    word rgb=Conv888To555(pal->_palette[i]);
    if( i!=pal->_transparentColor ) rgb|=0x8000;
    //rgb&=0x1f;
    resPal[i]=rgb;
  }
  
  // get image size
  {
    long w=in.getiw();
    long h=in.getiw();
    if( w==0 && h==0 ) {return 1;} // mip-map list terminator

    if( w==MAGIC_W_LZW && h==MAGIC_H_LZW )
    {
      w=in.getiw();
      h=in.getiw();

      long dSize=in.geti24();
      (void)dSize;

      if( w>4096 || w<1 )
      {
        Fail("Size out of range");
        return -1;
      }
      
      Assert( _w==w );
      Assert( _h==h );
      Assert(_pitch > 0);

      // skip actual image data
      //in.seekg(dSize,QIOS::cur);
      //memset(mem,0xff,_pitch*_h);
      if( DecodeLZW(in,(char *)mem,_w*_h,_w*2,_pitch,resPal,pal)<0 )
      {
        Fail("LZW Decode error");
        return -1;
      }
    }
    else
    {
      // get compressed data size
      //Fail("Old PAC Format.");
      long dSize=in.geti24();
      (void)dSize;
      
      long W=w*h;
      
      //if( w!=h ) goto Error; // square textures only
      if( w>4096 || w<1 )
      {
        Fail("Size out of range");
        return -1;
      }
      
      //_w=w,_h=h;
      Assert( _w==w );
      Assert( _h==h );
      Assert(_pitch > 0);

      int lineCnt=w;
      int lineAlign=_pitch-w*2;
      
      word *B=(word *)mem;
      //int lastVV=0; // start with black
      while( W>0 )
      {
        int c=in.get();
        if( c&0x80 )
        {
          int v=in.get();
          c&=0x7f;
          c++;
          W-=c;
          word vv;
          //if( v==pal->_transparentColor ) vv=lastVV&0x7fff;
          //else lastVV=
          vv=resPal[v];
          if( lineAlign==0 )
          {
            while( --c>=0 ) *B++=vv;
          }
          else
          {
            while( --c>=0 )
            {
              *B++=vv;
              if( --lineCnt==0 ) B=(word *)((byte *)B+lineAlign),lineCnt=w;
            }
          }
        }
        else
        {
          c++;
          W-=c;
          while( --c>=0 )
          {
            int v=in.get();
            word vv;
            //if( v==pal->_transparentColor ) vv=lastVV&0x7fff;
            //else lastVV=
            vv=resPal[v];
            *B++=vv;
            if( --lineCnt==0 ) B=(word *)((byte *)B+lineAlign),lineCnt=w;
          }
        }
      }
    }
    
    if( in.fail() || in.eof() )
    {
      Fail("Texture load failed.");
      return -1;
    }
    
    return 0;
  }
  
  //Error:
  //return error;
}


int PacLevelMem::LoadPacRGB565( QIStream &in, void *mem, const PacPalette *pal ) const
{
  RptF("Obsolete palette format texture");
  // picture is saved in Palette 8 format
  // load as raw RGB 565 data
  // return<0 - error
  // return>0 - no more mip-map levels available
  // return==0 - texture loaded successfully

  // load PAC file
  
  // resulting surface is 16b-rgb
  // prepare a palette for conversion
  //Temp<word> resPal(pal->_nColors);
  word resPal[256];
  Assert( pal->_nColors<=256 );
  Assert( pal->_transparentColor<0 );
  int i;
  for( i=0; i<pal->_nColors; i++ )
  {
    word rgb=Conv888To565(pal->_palette[i]);
    //rgb&=0x1f;
    resPal[i]=rgb;
  }
  
  // get image size
  {
    long w=in.getiw();
    long h=in.getiw();
    if( w==0 && h==0 ) {return 1;} // mip-map list terminator

    if( w==MAGIC_W_LZW && h==MAGIC_H_LZW )
    {
      w=in.getiw();
      h=in.getiw();

      long dSize=in.geti24();
      (void)dSize;

      if( w>4096 || w<1 )
      {
        Fail("Size out of range");
        return -1;
      }
      
      Assert( _w==w );
      Assert( _h==h );
      Assert(_pitch > 0);

      // skip actual image data
      //in.seekg(dSize,QIOS::cur);
      //memset(mem,0xff,_pitch*_h);
      if( DecodeLZW(in,(char *)mem,_w*_h,_w*2,_pitch,resPal,pal)<0 )
      {
        Fail("LZW Decode error");
        return -1;
      }
    }
    else
    {
      // get compressed data size
      //Fail("Old PAC Format.");
      long dSize=in.geti24();
      (void)dSize;
      
      long W=w*h;
      
      //if( w!=h ) goto Error; // square textures only
      if( w>4096 || w<1 )
      {
        Fail("Size out of range");
        return -1;
      }
      
      //_w=w,_h=h;
      Assert( _w==w );
      Assert( _h==h );
      Assert(_pitch > 0);

      int lineCnt=w;
      int lineAlign=_pitch-w*2;
      
      word *B=(word *)mem;
      //int lastVV=0; // start with black
      while( W>0 )
      {
        int c=in.get();
        if( c&0x80 )
        {
          int v=in.get();
          c&=0x7f;
          c++;
          W-=c;
          word vv;
          //if( v==pal->_transparentColor ) vv=lastVV&0x7fff;
          //else lastVV=
          vv=resPal[v];
          if( lineAlign==0 )
          {
            while( --c>=0 ) *B++=vv;
          }
          else
          {
            while( --c>=0 )
            {
              *B++=vv;
              if( --lineCnt==0 ) B=(word *)((byte *)B+lineAlign),lineCnt=w;
            }
          }
        }
        else
        {
          c++;
          W-=c;
          while( --c>=0 )
          {
            int v=in.get();
            word vv;
            //if( v==pal->_transparentColor ) vv=lastVV&0x7fff;
            //else lastVV=
            vv=resPal[v];
            *B++=vv;
            if( --lineCnt==0 ) B=(word *)((byte *)B+lineAlign),lineCnt=w;
          }
        }
      }
    }
    
    if( in.fail() || in.eof() )
    {
      Fail("Texture load failed.");
      return -1;
    }
    
    return 0;
  }
  
  //Error:
  //return error;
}

bool TextureHeaderManager::AddHeader(const RString& name, const TextureSourcePac& src, int numMipmaps, PacLevelMem* mipmaps, int textureFileSize)
{
  static const int MaxTmpStringSize = 1024;
  char tmpString[MaxTmpStringSize];
  int copyLength = std::min(MaxTmpStringSize, name.GetLength()+1);

  strncpy(tmpString, name.Data(), copyLength);
  NormalizePath(tmpString);

  int result = _headersMap.Add(TextureHeader(tmpString, src, numMipmaps, mipmaps, textureFileSize));
  if (result == -1)
  {
    RptF("Error while inserting texture \"%s\" to TextureHeaderManager - failed to add header to map.", tmpString);
  }
  return result != -1;
}


int PacLevelMem::LoadPaaBin16( QIStream &in, void *mem, const PacPalette *pal ) const
{
  PROFILE_SCOPE(pacLB);
  // return<0 - error
  // return>0 - no more mip-map levels available
  // return==0 - texture loaded successfully

  Assert( pal->_nColors==0 );

  // load PAA file
  // get image size
  long w=in.getiw();
  long h=in.getiw();
  if( w==0 && h==0 ) {return 1;} // mip-map list terminator

  // get compressed data size
  long dSize=in.geti24();
  (void)dSize;
  
  //long W=w*h;
  
  if( w>4096 || w<1 )
  {
    Fail("Size out of range");
    return -1;
  }
  
  Assert( _w==w );
  Assert( _h==h );
  
  if (_dFormat!=PacARGB8888)
  {
    // picture is saved in 4444 LZW format
    Assert(_pitch > 0);
    if( DecodeLZW(in,(char *)mem,_w*_h*2,_w*2,_pitch)<0 )
    {
      Fail("LZW Decode error");
      return -1;
    }

    if (_sFormat==PacAI88 && _dFormat==PacARGB4444)
    {
      int n=_w*_h;
      word *data=reinterpret_cast<word *>(mem);
      Assert( data );
      while( --n>=0 )
      {
        word s=*data;
        word a=(s&0xf000);
        word i=(s&0x00f0);
        word d=a|(i<<4)|i|(i>>4);
        *data++=d;
      }   
    }
    else
    {
      DoAssert( _sFormat==_dFormat );
    }
    //memset(mem,~0,_h*_pitch);
  }
  else
  {
    // conversion from 88 to 8888
    // allocate temporary 88 surface
    AUTO_STATIC_ARRAY(char,temp,128*128*2);
    temp.Resize(_w*_h*2);
    if( DecodeLZW(in,temp.Data(),_w*_h*2,_w*2,_w*2)<0 )
    {
      Fail("LZW Decode error");
      return -1;
    }
    // convert from temp to mem
    int n=_w*_h;
    DWORD *data=reinterpret_cast<DWORD *>(mem);
    const word *sdata=reinterpret_cast<const word *>(temp.Data());
    Assert( data );

    // Copy data from any 16b format to 8888 format
    if (_sFormat == PacAI88)
    {
      while( --n>=0 )
      {
        DWORD s = *sdata++;
        DWORD a = (s&0xff);
        DWORD c = ((s>>8)&0xff);
        DWORD d = (a<<24)|(c<<16)|(c<<8)|c;
        *data++=d;
      } 
    }
    else if (_sFormat == PacARGB1555)
    {
      while( --n>=0 )
      {
        *data++=Convert1555To8888(*sdata++);
      } 
    }
    else
    {
      Fail("Error: Unsupported source format");
    }

  }
  
  return 0;
}

int PacLevelMem::MipmapSize( PacFormat format, int w, int h )
{
  switch (format)
  {
    case PacDXT1:
    return w*h/2;
    case PacDXT2: case PacDXT3: case PacDXT4: case PacDXT5:
    return w*h;
    case PacARGB8888:
    return w*h*4;
    default:
    return w*h*2; // assume 16-b textures
  }
  /*NOTREACHED*/
}



void PacLevelMem::DecompressDXT1( void *dst, const void *src, int w, int h )
{
  const DXTBlock64 *s = (const DXTBlock64 *)src;
  word *line = (word *)dst;
  for (int y=0; y<h; y+=4)
  {
    word *base = line;
    for (int x=0; x<w; x+=4, base+=4)
    {
      s->Decompress1555(base,w);
      s++; // move to next block
    }
    line += 4*w; // skip all 4 decompressed lines
  }
}

void PacLevelMem::DecompressDXT2( void *dst, const void *src, int w, int h )
{
  const DXTBlock128 *s = (const DXTBlock128 *)src;
  word *line = (word *)dst;
  for (int y=0; y<h; y+=4)
  {
    word *base = line;
    for (int x=0; x<w; x+=4, base+=4)
    {
      s->Decompress4444_2(base,w);
      s++; // move to next block
    }
    line += 4*w; // skip all 4 decompressed lines
  }
}

void PacLevelMem::DecompressDXT4( void *dst, const void *src, int w, int h )
{
  const DXTBlock128 *s = (const DXTBlock128 *)src;
  word *line = (word *)dst;
  for (int y=0; y<h; y+=4)
  {
    word *base = line;
    for (int x=0; x<w; x+=4, base+=4)
    {
      s->Decompress4444_4(base,w);
      s++; // move to next block
    }
    line += 4*w; // skip all 4 decompressed lines
  }
}

  typedef MemAllocLocal<char, 32*1024> TexAllocator;
//DataStack GTexStack(128*1024);
//typedef MemAllocDataStack<char, 128*1024, int, &GTexStack> TexAllocator;

static void LoadDXT1(QIStream &in, void *mem, int w, int h, int pitch)
{
  // load to temporary buffer and decompress
  AutoArray<char, TexAllocator> temp;
  temp.Resize(h*pitch);
  // TODO: implement support for pitch not matching to w
  Assert(pitch==w*2); // loading into 16b pixels
  in.read(temp.Data(),temp.Size());
  // decompress from temp to mem
  PROFILE_SCOPE_EX(pacD1,tex);
  PacLevelMem::DecompressDXT1(mem,temp.Data(),w,h);
}

static void LoadDXT2(QIStream &in, void *mem, int w, int h, int pitch)
{
  // load to temporary buffer and decompress
  AutoArray<char,TexAllocator> temp;
  temp.Resize(h*pitch);
  // TODO: implement support for pitch not matching to w
  Assert(pitch==w*2); // loading into 16b pixels
  in.read(temp.Data(),temp.Size());
  // decompress from temp to mem
  PROFILE_SCOPE_EX(pacD2,tex);
  PacLevelMem::DecompressDXT2(mem,temp.Data(),w,h);
}

static void LoadDXT4(QIStream &in, void *mem, int w, int h, int pitch)
{
  // load to temporary buffer and decompress
  AutoArray<char,TexAllocator> temp;
  temp.Resize(pitch*h);
  // TODO: implement support for pitch not matching to w
  Assert(pitch==w*2); // loading into 16b pixels
  in.read(temp.Data(),temp.Size());
  // decompress from temp to mem
  PROFILE_SCOPE_EX(pacD4,tex);
  PacLevelMem::DecompressDXT4(mem,temp.Data(),w,h);
}

static void LZOLoadDXT(QIStream &in, void *mem, int w, int h, int pitch, PacFormat format, int compressedSize)
{
  Assert(pitch==w*2); // loading into 16b pixels

  lzo_uint uncompressedSize = h * pitch;

  // load to temporary buffer
  AutoArray<char,TexAllocator> compressed;
  compressed.Resize(compressedSize);
  in.read(compressed.Data(), compressedSize);

  // lzo decompress
  AutoArray<char,TexAllocator> temp;
  {
    PROFILE_SCOPE_EX(pacLZ, tex);
    temp.Resize(uncompressedSize);
    int ok = lzo_init();
    if (ok != LZO_E_OK)
    {
      Fail("LZO initialization failed");
      return;
    }
    ok = lzopro_lzo1x_decompress((lzo_bytep)compressed.Data(), compressedSize,
      (lzo_bytep)temp.Data(), &uncompressedSize, NULL);
    if (ok != LZO_E_OK)
    {
      Fail("LZO decompression failed");
      return;
    }
  }

  // TODO: implement support for pitch not matching to w
  // decompress from temp to mem
  switch (format)
  {
  case PacDXT1:
    {
      PROFILE_SCOPE_EX(pacD1,tex);
      PacLevelMem::DecompressDXT1(mem,temp.Data(),w,h);
    }
    break;
  case PacDXT2:
  case PacDXT3:
    {
      PROFILE_SCOPE_EX(pacD2,tex);
      PacLevelMem::DecompressDXT2(mem,temp.Data(),w,h);
    }
    break;
  case PacDXT4:
  case PacDXT5:
    {
      PROFILE_SCOPE_EX(pacD4,tex);
      PacLevelMem::DecompressDXT4(mem,temp.Data(),w,h);
    }
    break;
  }
}

static inline int RGB565(float r, float g, float b)
{
  int ri = toInt(r*31);
  int gi = toInt(g*63);
  int bi = toInt(b*31);
  saturate(ri,0,31);
  saturate(gi,0,63);
  saturate(bi,0,31);
  return (ri<<11)|(gi<<5)|bi;
}


static void FillDiagnosticColor(PacFormat format, void *dta, int size, int fill565)
{
  switch (format)
  {
    case PacDXT1: // DXT1 is the only case where we care about transparent encoding
      {
        // process 4x4 blocks one by one
        DXTBlock64 *s = (DXTBlock64 *)dta;
        while (size>0)
        {
          // c0>c1 -> no transparency
          if (s->c0>s->c1)
          {
            // keep no transparency - make sure c0 is still bigger:
            s->c0 = fill565|32;
            s->c1 = fill565&~32;
            // we do not care about slight color variations in G
            // if we would, we could fill tex0,tex1 accordingly
          }
          else
          {
            // c0==c1, c0<c1 -> some pixels transparent
            s->c0 = s->c1 = fill565;
          }
          s++;
          size -= sizeof(*s);
        }
      }
      break;
    default:
      // fill colors in the color block, leave alpha block untouched
      {
        // process 4x4 blocks one by one
        DXTBlock128 *s = (DXTBlock128 *)dta;
        while (size>0)
        {
          s->color.c0 = s->color.c1 = fill565;
          s++;
          size -= sizeof(*s);
        }
      }
      break;
  }
}

#if _ENABLE_CHEATS
int MipmapLoadingDiags = 0;
#else
const int MipmapLoadingDiags = 0;
#endif

/**
@param texSize total texture size (as loaded) - used for mipmap diagnostics
*/

// For testing decompression speed
//struct TickInfo
//{
//  DWORD _old;
//  DWORD _new;
//  DWORD _oldQ;
//  DWORD _newQ;
//
//};
//#include <vector>
//std::vector<RStringB> sLzoInfo;

int PacLevelMem::LoadPaaDXT( QIStream &in, void *mem, const PacPalette *pal, int texSize ) const
{
  PROFILE_SCOPE_EX(txLdX,tex);
  // return<0 - error
  // return>0 - no more mip-map levels available
  // return==0 - texture loaded successfully

  Assert( pal->_nColors==0 );

  // load PAA file
  // get image size
  long w=in.getiw();
  long h=in.getiw();
  if( w==0 && h==0 ) {return 1;} // mip-map list terminator

  // get compressed data size
  long dSize=in.geti24();

  // load the whole data at once, avoiding seek times
  in.PreReadSequential(dSize);

  // LZO compression is used for the mipmap data (highest bit of w used, w still can be up to 32768)
  bool lzo = (w & 0x8000) != 0;
  w &= 0x7fff;
  
  if( w>4096 || w<1 )
  {
    Fail("Size out of range");
    return -1;
  }
  
  Assert( _w==w );
  Assert( _h==h );

  // diagnostics color, based on a mipmap size
  // 12 distinct colors? Or we could rather use some patterns?
  static const int mipColors[]=
  {
    RGB565(0,0,1),
    RGB565(0,0,1),
    RGB565(0,0,1),
    RGB565(0,0,1),
    RGB565(0,0,1), // 4
    RGB565(0,1,1),
    RGB565(0,1,0),
    RGB565(0.1,0.1,0.1),
    RGB565(1,0,0), // 8
    RGB565(1,1,0),
    RGB565(1,0,1),
    RGB565(0.8,0.8,0.8), // 11 (2048)
  };
  
  int color = 0;
  if (MipmapLoadingDiags>0 && texSize>0)
  {
    int sizeLog = MipmapLoadingDiags==1 ? log2Ceil(intMax(_w,_h)) : log2Ceil(texSize);
    saturate(sizeLog,0,lenof(mipColors)-1);
    color = mipColors[sizeLog];
    /*
    // 16x16: blue (4)
    const int blueColor = 4;
    // 256x256: green (8)
    const int greenColor = 8;
    // 2048x2048: red (12)
    const int redColor = 12;
    // color encoded into 565 space
    // total texture size or the size of the current mipmap
    if (sizeLog>redColor) color = RGB565(1,0,0);
    else if (sizeLog>greenColor)
    {
      float red = InterpolativC(sizeLog,greenColor,redColor,0,1);
      color = RGB565(red,1-red,0);
    }
    else if (sizeLog>blueColor)
    {
      float green = InterpolativC(sizeLog,blueColor,greenColor,0,1);
      color = RGB565(0,green,1-green);
    }
    else
    {
      color = RGB565(0,0,1);
    }
    DoAssert(color!=0);
    */
  }

  // check if texture compressed using LZO will be stored in the continuous block of memory
  // (needed for the lzo decompression)
  if (lzo)
  {
    if (_sFormat == PacDXT1)
    {
      if (_w != 2 * _pitch)
      {
        Fail("Unexpected pitch for compressed texture");
        return -1;
      }
    }
    else
    {
      if (_w != _pitch)
      {
        Fail("Unexpected pitch for compressed texture");
        return -1;
      }
    }
  }

  // picture is saved in raw compressed data format
  if (_dFormat == _sFormat)
  {
    PROFILE_SCOPE(pacRd);
    if (lzo)
    {
      lzo_uint uncompressedSize = w * h;
      if (_dFormat == PacDXT1) uncompressedSize /= 2;

      #if 1
      // load to temporary buffer
      AutoArray<char,TexAllocator> compressed;
      {
        PROFILE_SCOPE_EX(pacLL, tex);
        compressed.Resize(dSize);
        in.read(compressed.Data(), dSize);
      }

      // decompress
      {
        PROFILE_SCOPE_EX(pacLZ, tex);
        int ok = lzo_init();
        if (ok != LZO_E_OK)
        {
          Fail("LZO initialization failed");
          return -1;
        }

        //TickInfo ti;  

        //static int loopCount = 10000;

        //DWORD cTick = GetTickCount();

        //for (int i = 0; i < loopCount; ++i)
        //{
        //  ok = lzo1x_decompress((lzo_bytep)compressed.Data(), dSize,
        //    (lzo_bytep)mem, &uncompressedSize, NULL);
        //}        

        //ti._old = GetTickCount() - cTick;
        //cTick = GetTickCount();

        //for (int i = 0; i < loopCount; ++i)
        //{
        //  ok = lzopro_lzo1x_decompress((lzo_bytep)compressed.Data(), dSize,
        //    (lzo_bytep)mem, &uncompressedSize, NULL);
        //}

        //ti._new = GetTickCount() - cTick;
        //cTick = GetTickCount();

        //for (int i = 0; i < loopCount; ++i)
        //{
        //  in.seekg(-dSize,QIOS::cur);
        //  ok = lzopro_lzo1x_decompress_from_stream(in,
        //    (lzo_bytep)mem, &uncompressedSize, NULL);
        //}

        //ti._newQ = GetTickCount() - cTick;
        //cTick = GetTickCount();

        //for (int i = 0; i < loopCount; ++i)
        //{
        //  in.seekg(-dSize,QIOS::cur);
        //  ok = lzo1x_decompress_from_stream_old(in,
        //    (lzo_bytep)mem, &uncompressedSize, NULL);
        //}        

        //ti._oldQ = GetTickCount() - cTick;
        //RStringB lzoInfo = Format("Raw: %.3f QiStream: %.3f",ti._new/float(ti._old),ti._newQ/float(ti._oldQ));
        //sLzoInfo.push_back(lzoInfo);

        ok = lzopro_lzo1x_decompress((lzo_bytep)compressed.Data(), dSize,
          (lzo_bytep)mem, &uncompressedSize, NULL);

        if (ok != LZO_E_OK)
        {
          Fail("LZO decompression failed");
          return -1;
        }
      }
      #else
        PROFILE_SCOPE_EX(pacLZ, tex);

        int ok = lzo_init();
        if (ok != LZO_E_OK)
        {
          Fail("LZO initialization failed");
          return -1;
    }
        ok = lzopro_lzo1x_decompress_from_stream(in,
          (lzo_bytep)mem, &uncompressedSize, NULL);
        if (ok != LZO_E_OK)
        {
          Fail("LZO decompression failed");
          return -1;
        }
      #endif
    }
    else
    {
      if (_dFormat == PacDXT1)
      {
        if(w*h!=dSize*2)
        {
          Fail("Bad DXT1 mipmap size");
          return -1;
        }
      }
      else
      {
        if (w*h!=dSize)
        {
          Fail("Bad DXT2-5 mipmap size");
          return -1;
        }
      }
      // we cannot read in one piece unless the pitch is matching width
      /* if size of pitch*h is matching total size, pitch matches width */
      if (_pitch*h==dSize)
      {
        PROFILE_SCOPE_EX(pacRR, tex);
        in.read(mem,dSize);
        if (MipmapLoadingDiags && color>0) FillDiagnosticColor(_dFormat,mem,dSize,color);
      }
      else
      {
        PROFILE_SCOPE_EX(pacRP, tex);
        int blockHeight = h/4;
        int blockPitch = _pitch*4;
        int blockSrcPitch = dSize/blockHeight;
        for (int i=0; i<blockHeight; i++)
        {
          // read one line of blocks
          in.read(mem,blockSrcPitch);
          if (MipmapLoadingDiags && color>0) FillDiagnosticColor(_dFormat,mem,blockSrcPitch,color);
          // advance pointer to next line
          mem = (char *)mem + blockPitch;
        }
      }
    }
    if (in.fail())
    {
      Fail("Compressed Read error");
      return -1;
    }
  }
  else if (_dFormat==PacARGB1555 && _sFormat==PacDXT1)
  {
    if (lzo)
      LZOLoadDXT(in, mem, _w, _h, _pitch, _sFormat, dSize);
    else
      LoadDXT1(in,mem,_w,_h,_pitch);
  }
  else if (_dFormat==PacARGB4444 && (_sFormat==PacDXT2 || _sFormat==PacDXT3))
  {
    if (lzo)
      LZOLoadDXT(in, mem, _w, _h, _pitch, _sFormat, dSize);
    else
      LoadDXT2(in,mem,_w,_h,_pitch);
  }
  else if (_dFormat==PacARGB4444 && (_sFormat==PacDXT4 || _sFormat==PacDXT5))
  {
    if (lzo)
      LZOLoadDXT(in, mem, _w, _h, _pitch, _sFormat, dSize);
    else
      LoadDXT4(in,mem,_w,_h,_pitch);
  }
  else
  {
    ErrF("Conversion %s to %s not supported",cc_cast(FormatName(_sFormat)),cc_cast(FormatName(_dFormat)));
  }
  return 0;
}

inline WORD Conv8888To1555( DWORD argb )
{
  unsigned a=(argb>>24)&0xff;
  unsigned r=(argb>>16)&0xff;
  unsigned g=(argb>>8)&0xff;
  unsigned b=(argb>>0)&0xff;
  r>>=3,g>>=3,b>>=3;
  a>>=7;
  return WORD( (a<<15)|(r<<10)|(g<<5)|(b<<0) );
}
inline WORD Conv8888To4444( DWORD argb )
{
  unsigned a=(argb>>24)&0xff;
  unsigned r=(argb>>16)&0xff;
  unsigned g=(argb>>8)&0xff;
  unsigned b=(argb>>0)&0xff;
  r>>=4,g>>=4,b>>=4;
  a>>=4;
  return WORD( (a<<12)|(r<<8)|(g<<4)|(b<<0) );
}
inline WORD Conv8888To88( DWORD argb )
{
  unsigned a=(argb>>24)&0xff;
  unsigned r=(argb>>16)&0xff;
  unsigned g=(argb>>8)&0xff;
  unsigned b=(argb>>0)&0xff;
  // usually r,g,b are the same value
  // if not, we attempt to do a simple weighting
  // this also helps us to avoid slow division
  unsigned i = (r+2*g+b)/4;
  return WORD( (a<<8)|(i<<0) );
}

inline WORD Conv8888To565( DWORD argb )
{
  unsigned r=(argb>>16)&0xff;
  unsigned g=(argb>>8)&0xff;
  unsigned b=(argb>>0)&0xff;
  r>>=3,g>>=2,b>>=3;
  return WORD( (r<<11)|(g<<5)|(b<<0) );
}

static WORD ColorToPixel(DWORD argb, PacFormat format)
{
  switch (format)
  {
    case PacARGB1555: return Conv8888To1555(argb);
    case PacRGB565: return Conv8888To565(argb);
    case PacARGB4444: return Conv8888To4444(argb);
    case PacAI88: return Conv8888To88(argb);
    default:
      Fail("Unknown format");
      return 0;
  }
}

bool PacLevelMem::FillMipmap(void *dta, PackedColor color) const
{
  //color = PackedColor(0x80d0c020);
  switch (_dFormat.GetEnumValue())
  {
    case PacARGB8888:
    {
      DWORD *data = (DWORD *)dta;
      for (int y=0; y<_h; y++)
      {
        DWORD *line = data;
        for (int x=0; x<_w; x++)
        {
          *line++ = color;
        }
        Assert(_pitch > 0);
        data = (DWORD *)((char *)data+_pitch);
      }
      return true;
    }
      
    case PacARGB4444:
    case PacARGB1555:
    case PacRGB565:
    case PacAI88:
    {
      WORD pixel = ColorToPixel(color,_dFormat);
      WORD *data = (WORD *)dta;
      for (int y=0; y<_h; y++)
      {
        WORD *line = data;
        for (int x=0; x<_w; x++)
        {
          *line++ = pixel;
        }
        Assert(_pitch > 0);
        data = (WORD *)((char *)data+_pitch);
      }
      return true;
    }
    //  return false;
    case PacDXT1:
    {
      DXTBlock64 *s = (DXTBlock64 *)dta;
      // if alpha is low, mark all pixels as transparent
      //int tex0 = color.A8()>=128 ? 0 : ~0;
      //int tex1 = color.A8()>=128 ? 0 : ~0;
      
      // dithered transparency based on alpha value
      // 16 various 4x4 dither patterns
      static const DWORD ditherPattern[16]=
      {
        /*  0 */ 0xffffffff,
        /*  1 */ 0x3fffffff,
        /*  2 */ 0x3ffff3ff,
        /*  3 */ 0x33fff3ff,
        /*  4 */ 0x33ff33ff,
        /*  5 */ 0x33ff33ff,
        /*  6 */ 0x33cc33cc,
        /*  7 */ 0x33cc33cc,
        /*  8 */ 0x33cc33cc,
        /*  9 */ 0x33cc33cc,
        /* 10 */ 0x33003300,
        /* 11 */ 0x33003300,
        /* 12 */ 0x33003300,
        /* 13 */ 0x33003300,
        /* 14 */ 0,
        /* 15 */ 0,
      };
      int alpha = color.A8()>>4;
      WORD tex0 = (ditherPattern[alpha]>>16)&0xffff;
      WORD tex1 = ditherPattern[alpha]&0xffff;
      WORD color565 = Conv8888To565(color);
      for (int y=0; y<_h; y+=4)
      {
        for (int x=0; x<_w; x+=4)
        {
          // singular case
          s->c0 = color565;
          s->c1 = color565;
          s->tex0 = tex0;
          s->tex1 = tex1;
          s++;
        }
      }
      return true;
    }
    case PacDXT5:
    {
      DXTBlock128 *s = (DXTBlock128 *)dta;
      // if alpha is low, mark all pixels as transparent
      int alpha = color.A8()>>4;
      WORD color565 = Conv8888To565(color);
      for (int y=0; y<_h; y+=4)
      {
        for (int x=0; x<_w; x+=4)
        {
          s->alphaImp.a0 = alpha;
          s->alphaImp.a1 = alpha;
          // singular case
          s->color.c0 = color565;
          s->color.c1 = color565;
          s->color.tex0 = 0;
          s->color.tex1 = 0;
          s++;
        }
      }
      return true;
    }
    default:
      Fail("Unsupported fill format");
      return false;
  }
}



int PacLevelMem::LoadPac( QIStream &in, void *mem, const PacPalette *pal, int texSize ) const
{
  int ret=-1;
  switch(_sFormat.GetEnumValue())
  {
    case PacDXT1: case PacDXT2: case PacDXT3: case PacDXT4: case PacDXT5:
      ret=LoadPaaDXT(in,mem,pal,texSize);
    break;
    case PacP8:
      Fail("Obsolette P8 format");
      switch (_dFormat.GetEnumValue())
      {
        case PacARGB1555:
          ret=LoadPacARGB1555(in,mem,pal);
          break;
        case PacRGB565:
          ret=LoadPacRGB565(in,mem,pal);
          break;
        default:
          ErrF("Bad destination format for P8 source %d",(int)_sFormat.GetEnumValue());
          ret=-1;
        break;
      }
      break;
    default:
      ErrF("Bad source texture format %d",(int)_sFormat.GetEnumValue());
      ret=-1;
    break;
  }
  return ret;
}
int PacLevelMem::LoadPaa( QIStream &in, void *mem, const PacPalette *pal, int texSize ) const
{
  int ret=-1;
  switch( _sFormat.GetEnumValue() )
  {
    case PacARGB1555:
      //Fail("Breakpoint");
    case PacARGB4444:
    case PacAI88:
      ret=LoadPaaBin16(in,mem,pal);
    break;
    case PacDXT1: case PacDXT2: case PacDXT3: case PacDXT4: case PacDXT5:
      ret=LoadPaaDXT(in,mem,pal,texSize);
    break;
    default:
      Fail("Bad texture format.");
      ret=-1;
    break;
  }
  return ret;
}

void PacLevelMem::Interpolate
(
  void *data, void *withData,
  const PacLevelMem &with, float factor
)
{
  if (_dFormat!=with._dFormat)
  {
    Fail("Interpolated surface format does not match.");
    return;
  }
  //LogF("Interpolate %d with %d",_dFormat,with._dFormat);
  if( _dFormat==PacARGB1555 )
  {
    Assert( _w==with._w );
    Assert( _pitch==with._pitch );
    Assert( _h==with._h );
    //Assert( with._memData );
    //Assert( _memData );
    const word *sData=reinterpret_cast<const word *>(withData);
    word *dData=reinterpret_cast<word *>(data);
    int x,y;
    int coef=toIntFloor(factor*256);
    if( coef>255 ) coef=255;if( coef<0 ) coef=0;
    int dCoef=255-coef;
    for( y=0; y<_h; y++ ) for( x=0; x<_w; x++ )
    {
      Assert(_pitch > 0);
      int offset=y*(_pitch/2)+x;
      word s=sData[offset];
      word &d=dData[offset];
      word dd=d;
      int r=(((s>>10)&0x1f)*coef+((dd>>10)&0x1f)*dCoef+128)>>8;
      int g=(((s>>5)&0x1f)*coef+((dd>>5)&0x1f)*dCoef+128)>>8;
      int b=(((s>>0)&0x1f)*coef+((dd>>0)&0x1f)*dCoef+128)>>8;
      // assume alpha opaque
      d=0x8000|(r<<10)|(g<<5)|b;
    }
  }
  else if( _dFormat==PacRGB565 )
  {
    Assert( _w==with._w );
    Assert( _pitch==with._pitch );
    Assert( _h==with._h );
    //Assert( with._memData );
    //Assert( _memData );
    const word *sData=reinterpret_cast<const word *>(withData);
    word *dData=reinterpret_cast<word *>(data);
    int x,y;
    int coef=toIntFloor(factor*256);
    if( coef>255 ) coef=255;if( coef<0 ) coef=0;
    int dCoef=255-coef;
    for( y=0; y<_h; y++ ) for( x=0; x<_w; x++ )
    {
      Assert(_pitch > 0);
      int offset=y*(_pitch/2)+x;
      word s=sData[offset];
      word &d=dData[offset];
      word dd=d;
      int r=(((s>>11)&0x1f)*coef+((dd>>11)&0x1f)*dCoef+128)>>8;
      int g=(((s>>5)&0x3f)*coef+((dd>>5)&0x3f)*dCoef+128)>>8;
      int b=(((s>>0)&0x1f)*coef+((dd>>0)&0x1f)*dCoef+128)>>8;
      // assume alpha opaque
      d=(r<<11)|(g<<5)|b;
    }
  }
  else
  {
    Fail("Unsupported interpolation format.");
  }
}


void PacLevelMem::SetDestFormat(PacFormat dFormat, int align)
{
  _dFormat = dFormat;
}

/*!
\patch_internal 1.08 Date 7/26/2001 by Ondra
- Improved: better handling of data errors.
*/


int PacLevelMem::Init( QIStream &in, PacFormat sFormat)
{
  // return<0 - error
  // return>0 - no more mip-map levels available
  // return==0 - texture loaded successfully
  
  // if data is already loaded, skip it

  int error=-1;

  _sFormat = sFormat;
  
  // get size and palette  size

  if (_w>0 && _h>0)
  {
    // mipmap already initialized
    // no need to read and seek - offset based file
    return 0;
  }

  { 
    int startOffset = in.tellg(); // remember start offset  

    int w=in.getiw(); // get texture dimensions
    int h=in.getiw();
    if( w==0 && h==0 )
    {
      // Fail("Terminator reached");
      error=1;
      goto Error;
    } // mip-map list terminator
    
    if (_start>=0)
    {
      DoAssert( _start==startOffset );
    }
    else
    {
      //Log("Data error - Late texture initialization, format %s",(const char *)FormatName(_sFormat));
    }
    _start = startOffset;
    // some textures are saved using lzw
    if( w==MAGIC_W_LZW && h==MAGIC_H_LZW )
    {
      w=in.getiw();
      h=in.getiw();
    }

    // mask the lzo compression flag
    w &= 0x7fff;

    //int W=w*h;
    
    // get compressed data size
    int dSize=in.geti24();

    //if( w!=h ) goto Error; // square textures only
    // FIX
    if( w>4096 || w<2 || h>4096 || h<2)
    {
      ErrF("Extreme texture size (%dx%d)",w,h);
      goto Error; // check extreme size
    }
    
    _w=w,_h=h;
    
    // skip actual image data
    in.seekg(dSize,QIOS::cur);
    //_skip = in.tellg()-startOffset; // calculate space used
    
    error=0;
    // no ramp - ramp is attached by bank 
  }
  Error:
  if( error<0 )
  {
    Fail("Texture init failed.");
    //_memData=NULL;
    _w=0;
    _h=0;
  }
  return error;
}

int PacLevelMem::Init(PacFormat sFormat)
{
  return 0;
}

PackedColor PacLevelMem::GetPixel(const void *data, float u, float v) const
{
  int iU=toIntFloor(u*_w);
  int iV=toIntFloor(v*_h);
  if( iU<0 ) iU=0;
  if( iV<0 ) iV=0;
  if( iU>_w-1 ) iU=_w-1;
  if( iV>_h-1 ) iV=_h-1;
  return GetPixelInt(data,iU,iV);
}

void PacLevelMem::SerializeBin(SerializeBinStream &f)
{
  f.Transfer(_w);
  f.Transfer(_h);
  f.Transfer(_pitch);
  f.TransferLittleEndian(_sFormat);
  f.TransferLittleEndian(_dFormat);
  f.Transfer(_start);
}

const char *FormatName( PacFormat format )
{
  switch( format )
  {
    case PacAI88: return "88";
    case PacARGB8888: return "8888";
    case PacARGB1555: return "1555";
    case PacRGB565: return "o565";
    case PacARGB4444: return "4444";
    case PacDXT1: return "DXT1";
    case PacDXT2: return "DXT2";
    case PacDXT3: return "DXT3";
    case PacDXT4: return "DXT4";
    case PacDXT5: return "DXT5";
    case PacP8: return "P8";
    default: return "xxxx";
  }
}

inline PackedColor PackedColor565( int rgb )
{
  int r=(rgb>>11)&0x1f;
  int g=(rgb>>5)&0x3f;
  int b=(rgb>>0)&0x1f;
  const int scale5=255/31;
  const int scale6=255/63;
  return PackedColor(r*scale5,g*scale6,b*scale5,255);
}

#ifndef _WIN32
static unsigned short _byteswap_ushort(unsigned short i)
{
    unsigned short j;
    j =  (i << 8) ;
    j += (i >> 8) ;
    return j;
}
#endif

static void SwapEndian16b(void *dst, const void *src, int size)
{
  unsigned short *d = (unsigned short *)dst;
  const unsigned short *s = (const unsigned short *)src;
  size >>= sizeof(unsigned short);
  while (--size>=0)
  {
    *d++ = _byteswap_ushort(*s++);
  }
}


PackedColor PacLevelMem::GetPixelInt(const void *data, int u, int v) const
{
  switch( _dFormat.GetEnumValue() )
  {
    case PacDXT1:
    {
      Assert(_w==_pitch*2);
      // calc. 64-b block address
      int x = u>>2, y = v>>2;
      int bPitch = _w>>2;
      DXTBlock64 *s = ((DXTBlock64 *)data)+y*bPitch+x;
#if _M_PPC
      DXTBlock64 t;
      SwapEndian16b(&t,s,sizeof(t));
      s = &t;
#endif
      // pitch of one 64b blocks line (2 actual lines) size
      // get color 0 and 1
      return PackedColor(s->GetColor(u&3,v&3));
    }
    case PacDXT4:
    case PacDXT5:
    {
      Assert(_w==_pitch);
      // calc. 128-b block address
      int x = u>>2, y = v>>2;
      int bPitch = _w>>2;
      DXTBlock128 *s = ((DXTBlock128 *)data)+y*bPitch+x;
#if _M_PPC
      DXTBlock128 t;
      SwapEndian16b(&t.color,&s->color,sizeof(t.color));
      // we use bytes in the structure, and therefore we need to byte swapping for alphaImp
      t.alphaImp = s->alphaImp;
      s = &t;
#endif
      // pitch of one 64b blocks line (2 actual lines) size
      // get color 0 and 1
      DWORD rgb = s->GetColor4(u&3,v&3);
      return PackedColor(rgb);
    }
    case PacDXT2:
    case PacDXT3:
    {
      Assert(_w==_pitch);
      // calc. 128-b block address
      int x = u>>2, y = v>>2;
      int bPitch = _w>>2;
      DXTBlock128 *s = ((DXTBlock128 *)data)+y*bPitch+x;
#if _M_PPC
      DXTBlock128 t;
      // only words used in the alphaExp block, full swap needed
      SwapEndian16b(&t,s,sizeof(t));
      t.alphaImp = s->alphaImp;
      s = &t;
#endif
      // pitch of one 64b blocks line (2 actual lines) size
      // get color 0 and 1
      DWORD rgb = s->GetColor2(u&3,v&3);
      // perform DXT5 decompression
      return PackedColor(rgb);
    }
    case PacP8:
    {
      Fail("Unsupported format palette 8");
      return PackedColor(0);
    }
    case PacRGB565:
    {
      Assert(_w*2==_pitch);
      int rgb=((Pixel *)data)[v*_w+u];
      return PackedColor565(rgb);
    }
    //break;
    case PacARGB1555:
    {
      Assert(_w*2==_pitch);
      int rgb=((Pixel *)data)[v*_w+u];
      int r=(rgb>>10)&0x1f;
      int g=(rgb>>5)&0x1f;
      int b=(rgb>>0)&0x1f;
      int a=((rgb>>15)&1)*255;
      const int scale5=255/31;
      return PackedColor(r*scale5,g*scale5,b*scale5,a);
    }
    case PacARGB4444:
    {
      Assert(_w*2==_pitch);
      int rgb=((Pixel *)data)[v*_w+u];
      int a=(rgb>>12)&0xf;
      int r=(rgb>>8)&0xf;
      int g=(rgb>>4)&0xf;
      int b=(rgb>>0)&0xf;
      const int scale4=255/15;
      return PackedColor(r*scale4,g*scale4,b*scale4,a*scale4);
    }
    case PacAI88:
    {
      Assert(_w*2==_pitch);
      int rgb=((Pixel *)data)[v*_w+u];
      int i=(rgb>>0)&0xff;
      int a=(rgb>>8)&0xff;
      return PackedColor(i,i,i,a);
    }
    case PacARGB8888:
      Assert(_w*4==_pitch);
      return PackedColor(((unsigned *)data)[v*_w+u]);
    default:
      Fail("Bad texture format in GetPixel");
      return PackedColor(0,0,0,0);
  }
}

/// load pac/paa format texture from any source (file, memory)

ITextureSource *CreatePacMemTextureSource(QIStream *stream, RString name)
{
  TextureSourcePacMem *source = new TextureSourcePacMem(stream);
  source->SetName(name);
  return source;
}

//! all routines required to create pac/paa texture
class TextureSourcePacFactory: public ITextureSourceFactory
{
  public:
  bool Check(const char *name);
  bool PreInit(
    const char *name,
    RequestableObject *obj=NULL, RequestableObject::RequestContext *ctx=NULL
  );
  ITextureSource *Create(const char *name, PacLevelMem *mips, int maxMips);
  TextureType GetTextureType(const char *name) {return GetTextureTypeFromName(name);}
  TexFilter GetMaxFilter(const char *name) {return TFAnizotropic16;}
};

//! all routines required to create ogg/ogv texture
class TextureSourceVideoFactory: public ITextureSourceFactory
{
public:
  bool Check(const char *name);
  bool PreInit(
    const char *name,
    RequestableObject *obj=NULL, RequestableObject::RequestContext *ctx=NULL
    );
  ITextureSource *Create(const char *name, PacLevelMem *mips, int maxMips);
  TextureType GetTextureType(const char *name) {return GetTextureTypeFromName(name);}
  TexFilter GetMaxFilter(const char *name) {return TFAnizotropic16;}
};

bool TextureSourceVideoFactory::Check( const char *name )
{
  //! pre-init data source
  return QFBankQueryFunctions::FileExists(name);
}

bool TextureSourceVideoFactory::PreInit( const char *name, RequestableObject *obj/*=NULL*/, RequestableObject::RequestContext *ctx/*=NULL */ )
{
  return true;
}

ITextureSource * TextureSourceVideoFactory::Create( const char *name, PacLevelMem *mips, int maxMips )
{
  TextureSourceVideo *source = new TextureSourceVideo;
  source->SetName(name);
  //ITextureSource *source = CreateColorTextureSource(HWhite);
  if (source && !source->Init(mips,maxMips))
  {
    delete source;
    return NULL;
  }

  return  source;
}



#include <El/FileServer/fileServer.hpp>

TextureSourcePacBase::TextureSourcePacBase()
{
  _mipmaps = 0;
  _isPaa = false;
  _tt = TT_Diffuse;
}

TextureSourcePacBase::~TextureSourcePacBase()
{
}

static PacFormat BasicFormat( const char *name )
{
  const char *ext=strrchr(name,'.');
  if( ext && !strcmp(ext,".paa") ) return PacARGB4444;
  else return PacP8;
}

#define MIN_MIP_SIZE 4 // guarantee QWORD alignment (4 16-bit pixels are enough)

void TextureSourcePacBase::SetName(const char *name)
{
  _name = name;
  _tt = GetTextureTypeFromName(name);
}

void TextureSourcePacBase::SerializeBin(SerializeBinStream &f)
{
  _pal.SerializeBin(f);
  f.Transfer(_mipmaps);
  f.TransferLittleEndian(_format);
  f.Transfer(_littleEndian);
  f.Transfer(_isPaa);
  f.Transfer(_name);
  f.TransferLittleEndian(_tt);
}


#if _ENABLE_TEXTURE_HEADERS
bool TextureSourcePacBase::InitFromHeaderMng(PacLevelMem *mips, int maxMips)
{
  if (!GUseTextureHeaders)
    return false;

  const TextureHeader* h = GTextureHeaderManager.GetHeader(_name);
  if (!h)
    return false;

  // if the header is from PBO and patching is enabled, we have to check, if file is really taken from PBO
#if _ENABLE_PATCHING
  if (h->_isFromPBO && !QFBankQueryFunctions::FileIsTakenFromBank(_name))
    return false;
#endif

  // check if the header is usable
  if (!h->_isUsable)
    return false;


#if 0
  // size test
  if (h->_textureFileSize != QFBankQueryFunctions::GetFileSize(_name))
  {
    Assert(h->_textureFileSize == QFBankQueryFunctions::GetFileSize(_name) && "Different file size of texture and original texture, that the header was taken from! Invalid header!");
    LogF("Different file size of texture and original texture, that the header was taken from! Invalid header: \"%s\"\n!", cc_cast(_name));
    return false;
  }
#endif

  int n = intMin(maxMips, h->_src.GetMipmapCount());
  for (int i=0; i<n; ++i)
  {
    mips[i] = h->_mipmaps[i];
  }
  
  *this = h->_src;
  return true;
}
#endif //ENABLE_TEXTURE_HEADERS

bool TextureSourcePacBase::Init(PacLevelMem *mips, int maxMips)
{
#if _ENABLE_TEXTURE_HEADERS
  if (GUseTextureHeaders)
  {
    if (InitFromHeaderMng(mips, maxMips))
      return true;
  }
#endif

  SRef<QIStream> stream = OpenStream();
  if (!stream) return false;
  QIStream &in = *stream;

  // .paa should start with format marker
  unsigned short desc = in.getiw();
  bool alpha = false;
  PacFormat format=BasicFormat(_name);

  if( format==PacARGB4444 ) _isPaa=true;
  else _isPaa=false;

  PacFormat nFormat = PacFormatFromDesc(desc,alpha);
  if( nFormat!=NPacFormat ) format=nFormat;
  else in.seekg(-2,QIOS::cur);


  // currently all paa/pac files are little endian
  // TODOX360: save textures in big endian
  _littleEndian = true;
  
  _format = format;

  const int maxOffsets = 16;
  int offsets[maxOffsets];
  for (int i=0; i<maxOffsets; i++ ) offsets[i] = -1;

  if (_pal.Load(in,offsets,maxOffsets,format))
  {
    CloseStream(stream);
    return false;
  }
  #if _ENABLE_REPORT
    if (offsets[0]<0)
    {
      LogF(
        "No offsets for texture %s - conversion needed",
        (const char *)_name
      );
    }
  #endif
  // get number of mipmaps
  // initialize them
  
  #if _ENABLE_REPORT
    if (!_pal._maxColorSet)
    {
      //LogF("No dynamic range for %s",(const char *)name);
    }
    else
    {
      //Log("Dynamic range for %s",(const char *)name);
    }
  #endif

  int mipI;
  for (mipI=0; mipI<maxMips; )
  {
    Assert(mipI < 12);
    PacLevelMem &mip=mips[mipI];

    mip.SetStart(offsets[mipI]);

    if (mipI>0 && offsets[mipI]>0 )
    {
      // _w and _h size is known
      mip._w = mips[0]._w >> mipI;
      mip._h = mips[0]._h >> mipI;
    }

    int ret = mips[mipI].Init(in,format);

    if( ret<0 )
    {
      // texture is corrupt - release the info we have already loaded
      for (int m=0; m<=mipI; m++)
      {
        mips[m]._w = 0;
        mips[m]._h = 0;
        mips[m].SetStart(offsets[m]);
      }
      CloseStream(stream);
      return false;
    }
    if( ret>0 ) break; // last mip-map read

    mipI++;
    if (offsets[0]>0 && offsets[mipI]<=0) break; // last known mipmap read

    // If there is a DXT compression
    if (PacFormatIsDXT(format))
    {
      // If the resolution is smaller than minimum
      if ((mip._w <= MIN_MIP_SIZE) || (mip._h <= MIN_MIP_SIZE))
      {
        LogF("Warning: Texture %s is DXT compressed, but its mipmap resolution is smaller than %d.", (const char *)_name, MIN_MIP_SIZE);
        break;
      }
    }
  }
  _mipmaps = mipI;

  CloseStream(stream);
  return true;
}

int TextureSourcePacBase::GetMipmapCount() const
{
  return _mipmaps;
}

float TextureSourcePacBase::GetMaxTexDetail(float maxTexDetail) const
{
  switch (_tt)
  {
    case TT_Diffuse: case TT_Diffuse_Linear:
      return maxTexDetail;
    default:
      return FLT_MAX;
  }
}

PacFormat TextureSourcePacBase::GetFormat() const
{
  return _format;
}

bool TextureSourcePacBase::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  int ret;
  SRef<QIStream> stream = OpenStream();
  if (!stream) return false;
  
  QIStream &in = *stream;


  DoAssert (mip._start>=0)
  in.seekg(mip._start,QIOS::beg);
  if( _isPaa )
  {
    ret=mip.LoadPaa(in,mem,&_pal,texSize);
  }
  else
  {
    ret=mip.LoadPac(in,mem,&_pal,texSize);
  }

  CloseStream(stream);
  return ret==0;
}

bool TextureSourcePacBase::RequestMipmapData(
  const PacLevelMem *mips, int levelMin, int levelMax, FileRequestPriority prior
) const
{
  // note: we ignore levelMax, as we read the whole rest of the file anyway
  Assert(levelMin < 10);
  QFileSize startOffset = mips[levelMin]._start;
  if (startOffset<0)
  {
    LogF("Cannot background load %s, missing offset",(const char *)_name);
    return true;
  }

  SRef<QIStream> stream = OpenStream();
  if (!stream) return false;

  QIStream &in = *stream;

  bool ret = in.PreRead(prior,startOffset,QFileSizeMax);
  CloseStream(stream);
  return ret;
}

SRef<QIStream> TextureSourcePac::OpenStream() const
{
  QIFStreamB *stream = new QIFStreamB;
  #if TEXSRC_RELOAD
  _timestamp = QFBankQueryFunctions::TimeStamp(_name);
  #endif
  stream->AutoOpen(_name);
  if (stream->fail() || stream->rest() == 0)
  {
    delete stream;   
    stream = NULL;
  }
  return stream;
}

void TextureSourcePac::FlushHandles()
{
}

void TextureSourcePac::CloseStream(SRef<QIStream> stream) const
{
}

bool TextureSourcePacFactory::Check(const char *name)
{
  //! pre-init data source
  return QFBankQueryFunctions::FileExists(name);
}
bool TextureSourcePacFactory::PreInit(
  const char *name,
  RequestableObject *obj, RequestableObject::RequestContext *ctx
)
{

  QIFStreamB in;
  in.AutoOpen(name);
  // headers are always very small
  // the worst case is for palette textures, but such textures should not be used any longer
  // we are not sure we will need the file,
  // but in case disk is idle, it will be handy to preload it
  in.PreReadObject(obj,ctx,FileRequestPriority(5000),0,512);
  // never let the handler to finish the loading
  return false;
  // TODO: sometimes we are on the same thread and could finish it now
}

ITextureSource *TextureSourcePacFactory::Create(const char *name, PacLevelMem *mips, int maxMips)
{
  TextureSourcePac *source = new TextureSourcePac;
  source->SetName(name);
  if (source && !source->Init(mips,maxMips))
  {
    delete source;
    return NULL;
  }
  return source;
}


static inline void Fill16B(void *mem, int w, int h, int pitch, unsigned short color)
{
  for (int y=0; y<h; y++)
  {
    unsigned short *dst = (unsigned short *)mem;
    // advance one line (note: not valid for DXT)
    mem = (char *)mem + pitch;
    for (int x=0; x<w; x++)
    {
      *dst++ = color;
    }
  }
}
static inline void Fill32B(void *mem, int w, int h, int pitch, unsigned color)
{
  for (int y=0; y<h; y++)
  {
    unsigned int *dst = (unsigned int *)mem;
    // advance one line (note: not valid for DXT)
    mem = (char *)mem + pitch;
    for (int x=0; x<w; x++)
    {
      *dst++ = color;
    }
  }
}

template <class Functor>
inline void Fill16BProc(void *mem, int w, int h, int pitch, Functor f)
{
  const float invW = 1.0f/(w-1);
  const float invH = 1.0f/(h-1);
  for (int y=0; y<h; y++)
  {
    unsigned short *dst = (unsigned short *)mem;
    // advance one line (note: not valid for DXT)
    mem = (char *)mem + pitch;
    for (int x=0; x<w; x++)
    {
      *dst++ = f(x*invW,y*invH);
    }
  }
}
template <class Functor>
inline void Fill32BProc(void *mem, int w, int h, int pitch, Functor f)
{
  const float invW = 1.0f/(w-1);
  const float invH = 1.0f/(h-1);
  for (int y=0; y<h; y++)
  {
    unsigned int *dst = (unsigned int *)mem;
    // advance one line (note: not valid for DXT)
    mem = (char *)mem + pitch;
    for (int x=0; x<w; x++)
    {
      *dst++ = f(x*invW,y*invH);
    }
  }
}
/// helper to provide functor interface for Fill16BProcInt for table defined data 
class Func2DArray16B
{
  const unsigned char *_horiz;
  const unsigned char *_vert;
  
  public:
  Func2DArray16B(const unsigned char *horiz, const unsigned char *vert)
  :_horiz(horiz),_vert(vert)
  {
  }
  unsigned short operator () (int x, int y) const
  {
    return (_vert[y]<<8)|_horiz[x];
  }
};

/// helper to provide functor interface for Fill16BProcInt for table defined data 
/** reversed alpha/intensity */
class Func2DArray16BVH
{
  const unsigned char *_horiz;
  const unsigned char *_vert;
  
  public:
  Func2DArray16BVH(const unsigned char *horiz, const unsigned char *vert)
  :_horiz(horiz),_vert(vert)
  {
  }
  unsigned short operator () (int x, int y) const
  {
    return (_vert[y])|(_horiz[x]<<8);
  }
};

/// helper to provide functor interface for Fill16BProcInt for table defined data 
/** reversed alpha/intensity */
class Func2DArray16BNH
{
  const unsigned char *_horiz;
public:
  Func2DArray16BNH(const unsigned char *horiz) :_horiz(horiz)
  {
  }
  unsigned short operator () (int x, int y) const
  {
    return (_horiz[x]<<8);
  }
};

/// helper to provide functor interface for Fill32BProcInt for table defined data
class Func2DArray32B
{
  const unsigned char *_horiz;
  const unsigned char *_vert;
  
  public:
  Func2DArray32B(const unsigned char *horiz, const unsigned char *vert)
  :_horiz(horiz),_vert(vert)
  {
  }
  unsigned int operator () (int x, int y) const
  {
    return (unsigned(_vert[y])<<24)|(unsigned(_horiz[x])*0x10101);
  }
};
/// helper to provide functor interface for Fill32BProcInt for table defined data
class Func2DArray32BVH
{
  const unsigned char *_horiz;
  const unsigned char *_vert;
  
  public:
  Func2DArray32BVH(const unsigned char *horiz, const unsigned char *vert)
  :_horiz(horiz),_vert(vert)
  {
  }
  unsigned int operator () (int x, int y) const
  {
    return (unsigned(_horiz[x])<<24)|(unsigned(_vert[y])*0x10101);
  }
};

/// helper to provide functor interface for Fill32BProcInt for table defined data
class Func2DArray32BNH
{
  const unsigned char *_horiz;

public:
  Func2DArray32BNH(const unsigned char *horiz) :_horiz(horiz)
  {
  }
  unsigned int operator () (int x, int y) const
  {
    return (unsigned(_horiz[x])<<24);
  }
};

/// "irradiance" zero handling - when horiz is zero, zero is used for vert as well
class Func2DArray16BIrrZero
{
  const unsigned char *_horiz;
  const unsigned char *_vert;
  
  public:
  Func2DArray16BIrrZero(const unsigned char *horiz, const unsigned char *vert)
  :_horiz(horiz),_vert(vert)
  {
  }
  unsigned short operator () (int x, int y) const
  {
    if (_horiz[x]==0) return 0;
    return (_vert[y]<<8)|_horiz[x];
  }
};

/// "irradiance" zero handling - when horiz is zero, zero is used for vert as well
class Func2DArray32BIrrZero
{
  const unsigned char *_horiz;
  const unsigned char *_vert;
  
  public:
  Func2DArray32BIrrZero(const unsigned char *horiz, const unsigned char *vert)
  :_horiz(horiz),_vert(vert)
  {
  }
  unsigned long operator () (int x, int y) const
  {
    if (_horiz[x]==0) return 0;
    return (unsigned(_vert[y])<<24)|(unsigned(_horiz[x])*0x10101);
  }
};


/// fill 16b destination using integer coordinates
template <class Functor>
static inline void Fill16BProcInt(void *mem, int w, int h, int pitch, Functor f)
{
  for (int y=0; y<h; y++)
  {
    unsigned short *dst = (unsigned short *)mem;
    // advance one line (note: not valid for DXT)
    mem = (char *)mem + pitch;
    for (int x=0; x<w; x++)
    {
      *dst++ = f(x,y);
    }
  }
}

/// fill 32b destination using integer coordinates
template <class Functor>
static inline void Fill32BProcInt(void *mem, int w, int h, int pitch, Functor f)
{
  for (int y=0; y<h; y++)
  {
    unsigned int *dst = (unsigned int *)mem;
    // advance one line (note: not valid for DXT)
    mem = (char *)mem + pitch;
    for (int x=0; x<w; x++)
    {
      *dst++ = f(x,y);
    }
  }
}

TextureSourceSimple::TextureSourceSimple(
  PacFormat format, int w, int h, int nMipmaps
)
:_w(w),_h(h),_nMipmaps(nMipmaps),
_format(format)
{
}
TextureSourceSimple::~TextureSourceSimple()
{
}

bool TextureSourceSimple::Init(PacLevelMem *mips, int maxMips)
{
  int i;
  if (maxMips>_nMipmaps) maxMips = _nMipmaps;
  for (i=0; i<maxMips; )
  {
    //Assert(i < 10);
    PacLevelMem &mip=mips[i];

    mip._w = _w>>i;
    mip._h = _h>>i;
    mip._pitch = 0;
    mip._sFormat = _format;
    mip._dFormat = _format;
    mip._start = -1;

    i++;

    if (mip._w<=1 || mip._h<=1) break;
    
    // If there is a DXT compression
    if (PacFormatIsDXT(_format))
    {
      // If the resolution is smaller than minimum
      if ((mip._w <= MIN_MIP_SIZE) || (mip._h <= MIN_MIP_SIZE))
      {
        LogF("Warning: Texture is DXT compressed, but its mipmap resolution is smaller than %d.", MIN_MIP_SIZE);
        break;
      }
    }
  }
  _nMipmaps = i;

  return true;
}

#if _HELISIM_RENDER_TO_TEXTURE

class TextureSourceR2T: public TextureSourceSimple
{
  RString _name;
  float _aspect;

public:
  TextureSourceR2T(PacFormat format, int w, int h, int nMipmaps, RString name, float aspect);
  ~TextureSourceR2T() {};

  virtual TextureType GetTextureType() const {return TT_R2T;}
  virtual TexFilter GetMaxFilter() const {return TFPoint;}

  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  PackedColor GetAverageColor() const {return PackedWhite;}
  bool IsAlpha() const {return false;}
  bool IsAlphaNonOpaque() const {return false;}

  int GetClamp() const {return 0;}
  void SetClamp(int clampFlags) {}

  virtual RString GetR2TName() const { return _name; }
  virtual float GetR2TAspect() const { return _aspect; }

};

TextureSourceR2T::TextureSourceR2T(PacFormat format, int w, int h, int nMipmaps, RString name, float aspect) 
: TextureSourceSimple(format, w, h, nMipmaps), _name(name), _aspect(aspect)
{

}

bool TextureSourceR2T::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  memset(mem, 0, mip._pitch * mip._h);
  return true;
}

ITextureSource *CreateTextureSourceR2T(PacFormat format, int w, int h, int nMipmaps, RString name, float aspect)
{
  return new TextureSourceR2T(format, w, h, nMipmaps, name, aspect);
}

#endif

/// texture containing a single color
class TextureSourceColor: public TextureSourceSimple
{
  /// color of the texture
  Color _color;
  //! Type of the texture the texture source represents
  TextureType _tt;
public:
  TextureSourceColor(PacFormat format, ColorVal color, int w, int h, int nMipmaps, TextureType tt = NTextureType);
  ~TextureSourceColor();

  virtual TextureType GetTextureType() const {return _tt;};
  virtual TexFilter GetMaxFilter() const {return TFPoint;}

  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  PackedColor GetAverageColor() const {return PackedColor(_color);}
  bool IsAlpha() const {return _color.A()<1.0f;}
  bool IsAlphaNonOpaque() const {return _color.A()<0.05f;}
  /** no clamping needed for constant color texture */
  int GetClamp() const {return 0;}
  void SetClamp(int clampFlags) {}
};

/*!
  \patch_internal 5092 Date 11/28/2006 by Flyman
  - Fixed: When using "shading detail" "very low", some models with procedural textures could be displayed with a wrong color
*/
static const struct
{
  Color c;
  TextureType t;
} defaultsTSC[]=
{
  {Color(0.5f,0.5f,0.5f,1.0f),TT_Detail},
  {Color(0.5f,0.5f,1.0f,1.0f),TT_Normal},
  // for white texture there is no difference between SRGB and linear
  // this is however used to setup postprocess effects, which replace texture
  // with a different one later on
  {Color(1.0f,1.0f,1.0f,1.0f),TT_Diffuse_Linear},
  {Color(0.0f,0.0f,0.0f,0.0f),TT_Macro},
  {Color(1.0f,1.0f,1.0f,0.0f),TT_Macro},
  {Color(1.0f,0.0f,0.0f,1.0f),TT_Specular},
  {Color(1.0f,0.0f,1.0f,1.0f),TT_Specular},
};
TextureSourceColor::TextureSourceColor(PacFormat format, ColorVal color, int w, int h, int nMipmaps, TextureType tt)
:TextureSourceSimple(format,w,h,nMipmaps),
_color(color)
{
  // Either estimate the texture type or use the one from the input
  if (tt >= NTextureType)
  {
    TextureType bestType = TT_Diffuse;
    float bestDist2 = FLT_MAX;
    for (int i=0; i<sizeof(defaultsTSC)/sizeof(defaultsTSC[0]); i++)
    {
      float dist2 = (color - defaultsTSC[i].c).SquareSize();
      if (bestDist2>dist2)
      {
        bestDist2 = dist2;
        bestType = defaultsTSC[i].t;
      }
    }

    // If difference is reasonably small, then use the calculated result, else simply use the Diffuse type
    // Diffuse can be better than wrong texture type (like normal map on a place of a shadow map)
     if (bestDist2 < 0.5f)
    {
      _tt = bestType;
    }
    else
    {
      _tt = TT_Diffuse;
    }
  }
  else
  {
    _tt = tt;
  }
}

TextureSourceColor::~TextureSourceColor()
{
}

bool TextureSourceColor::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  Assert(mip._w == (_w>>level));
  Assert(mip._h == (_h>>level));
  switch (mip._dFormat.GetEnumValue())
  {
    case PacARGB4444:
      {
        int r = toInt(_color.R()*15);
        int g = toInt(_color.G()*15);
        int b = toInt(_color.B()*15);
        int a = toInt(_color.A()*15);
        saturate(r,0,15);
        saturate(g,0,15);
        saturate(b,0,15);
        saturate(a,0,15);
        unsigned short color = (a<<12)|(r<<8)|(g<<4)|b;
        Fill16B(mem,mip._w,mip._h,mip._pitch,color);
      }
      return true;
    case PacAI88:
      {
        int i = toInt(_color.Brightness()*255);
        int a = toInt(_color.A()*255);
        saturate(i,0,255);
        saturate(a,0,255);
        unsigned short color = (a<<8)|i;
        Fill16B(mem,mip._w,mip._h,mip._pitch,color);
      }
      return true;
    case PacARGB1555:
      {
        int r = toInt(_color.R()*31);
        int g = toInt(_color.G()*31);
        int b = toInt(_color.B()*31);
        int a = toInt(_color.A());
        saturate(r,0,31);
        saturate(g,0,31);
        saturate(b,0,31);
        saturate(a,0,1);
        unsigned short color = (a<<15)|(r<<10)|(g<<5)|b;
        Fill16B(mem,mip._w,mip._h,mip._pitch,color);
      }
      return true;
    case PacARGB8888:
      {
        PackedColor argb(_color);
        Fill32B(mem,mip._w,mip._h,mip._pitch,argb);
      }
      return true;
    default:
      Fail("Unsupported format");
      return false;
  }
}


/// functor for packing a,i information into AI88 format
class PackResultAI88
{
  public:
  int operator () (float a, float i)
  {
    int a8 = toInt(a*255);
    int i8 = toInt(i*255);
    saturate(a8,0,255);
    saturate(i8,0,255);
    return (a8<<8)|i8;
  }
};

/// functor for packing a,i information into ARGB8888 format
class PackResultARGB8888
{
  public:
  int operator () (float a, float i)
  {
    int a8 = toInt(a*255);
    int i8 = toInt(i*255);
    saturate(a8,0,255);
    saturate(i8,0,255);
    return (a8<<24)|(i8<<16)|(i8<<8)|i8;
  }
};


/// texture containing a irradiance map
class TextureSourceIrradiance: public TextureSourceSimple
{
  /// specularity exponent
  float _specPower;
  
  public:
  TextureSourceIrradiance(
    PacFormat format, float specPower, int w, int h, int nMipmaps
  );
  ~TextureSourceIrradiance();

  //! Virtual method
  virtual TextureType GetTextureType() const {return TT_Irradiance;};
  virtual TexFilter GetMaxFilter() const {return TFLinear;}

  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  /** average color is not important for irradiance maps */
  PackedColor GetAverageColor() const {return PackedWhite;}
  /** irradiance maps have always alpha channel */
  bool IsAlpha() const {return true;}
  bool IsAlphaNonOpaque() const {return false;}
  /** irradiance is always clamped in both directions */
  int GetClamp() const {return TexClampU|TexClampV;}
  void SetClamp(int clampFlags) {}
};


TextureSourceIrradiance::TextureSourceIrradiance(
  PacFormat format, float specPower, int w, int h, int nMipmaps
)
:TextureSourceSimple(format,w,h,nMipmaps),
_specPower(specPower)
{
}

TextureSourceIrradiance::~TextureSourceIrradiance()
{
}

/**
Irradiance map generated using two 1D functions.
*/

bool TextureSourceIrradiance::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  Assert(mip._w == (_w>>level));
  Assert(mip._h == (_h>>level));
  AUTO_STATIC_ARRAY(unsigned char,specular,512);
  AUTO_STATIC_ARRAY(unsigned char,diffuse,512);
  specular.Resize(mip._h);
  diffuse.Resize(mip._w);
  const float invW = 1.0f/(mip._w-1);
  const float invH = 1.0f/(mip._h-1);
  // calculate specular factor
  for (int i=0; i<mip._h; i++)
  {
    float x = i*invH;
    float res = powf(x,_specPower);
    int resI = toInt(res*255);
    saturate(resI,0,255);
    specular[i] = resI;
  }
  // calculate diffuse factor
  for (int i=0; i<mip._w; i++)
  {
    float x = i*invW;
    int resI = toInt(x*255);
    saturate(resI,0,255);
    diffuse[i] = resI;
  }
  switch (mip._dFormat.GetEnumValue())
  {
    case PacAI88:
      Fill16BProcInt(mem,mip._w,mip._h,mip._pitch,Func2DArray16BIrrZero(diffuse.Data(),specular.Data()));
      return true;
    case PacARGB8888:
      Fill32BProcInt(mem,mip._w,mip._h,mip._pitch,Func2DArray32BIrrZero(diffuse.Data(),specular.Data()));
      return true;
    default:
      Fail("Unsupported format");
      return false;
  }
}

/// texture containing a irradiance map
class TextureSourceWaterIrradiance: public TextureSourceSimple
{
  /// specularity exponent
  float _specPower;
  
  public:
  TextureSourceWaterIrradiance(
    PacFormat format, float specPower, int w, int h, int nMipmaps
  );
  ~TextureSourceWaterIrradiance();

  virtual TextureType GetTextureType() const {return TT_Irradiance;};
  virtual TexFilter GetMaxFilter() const {return TFLinear;}

  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  /** average color is not important for irradiance maps */
  PackedColor GetAverageColor() const {return PackedWhite;}
  /** irradiance maps have always alpha channel */
  bool IsAlpha() const {return true;}
  bool IsAlphaNonOpaque() const {return false;}
  /** irradiance is always clamped in both directions */
  int GetClamp() const {return TexClampU|TexClampV;}
  void SetClamp(int clampFlags) {}
};


TextureSourceWaterIrradiance::TextureSourceWaterIrradiance(
  PacFormat format, float specPower, int w, int h, int nMipmaps
)
:TextureSourceSimple(format,w,h,nMipmaps),
_specPower(specPower)
{
}

TextureSourceWaterIrradiance::~TextureSourceWaterIrradiance()
{
}

/**
Irradiance map generated using two 1D functions.
*/

bool TextureSourceWaterIrradiance::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  Assert(mip._w == (_w>>level));
  Assert(mip._h == (_h>>level));
  AUTO_STATIC_ARRAY(unsigned char,specular,512);
  AUTO_STATIC_ARRAY(unsigned char,fresnel,512);
  specular.Resize(mip._h);
  fresnel.Resize(mip._w);
  const float invW = 1.0f/(mip._w-1);
  const float invH = 1.0f/(mip._h-1);
  // calculate specular factor
  for (int i=0; i<mip._h; i++)
  {
    float x = i*invH;
    float res = powf(x,_specPower);
    int resI = toInt(res*255);
    saturate(resI,0,255);
    specular[i] = resI;
  }
  // calculate Fresnel factor
  // dot product -> Reflection amount
  const float n1 = 1.0002926f; // air index of refraction
  const float n2 = 1.337f;    // water index of refraction
  for (int i=0; i<mip._w; i++)
  {
    float x = i*invW;

    // x is cos(V.N)
    float cosTheta1 = x;
    //float theta1 = acos(x);
    // theta1 - angle of incidence / reflection
    // theta2 - angle of refraction
    // calculate angle of refraction based on Snell's law
    // n1 * sin(theta1) = n2 * sin(theta2)
    //float sinTheta2 = n1/n2 * sqrt(1-Square(cosTheta1));
    //float cosTheta2 = sqrt(1-Square(n1/n2 * sqrt(1-Square(cosTheta1))));
    float cosTheta2 = sqrt(1-Square(n1/n2) * (1-Square(cosTheta1)));
    // calculation for non-polarized light (http://en.wikipedia.org/wiki/Fresnel_equations)
    float r = (
      +Square((n1*cosTheta1-n2*cosTheta2)/(n1*cosTheta1+n2*cosTheta2))
      +Square((n2*cosTheta1-n1*cosTheta2)/(n2*cosTheta1+n1*cosTheta2))
    )*0.5;    
    // possible approximation: r = 1/pow(x+1,8);
    int resI = toInt(r*255);
    saturate(resI,0,255);
    fresnel[i] = resI;
  }
  switch (mip._dFormat.GetEnumValue())
  {
    case PacAI88:
      Fill16BProcInt(mem,mip._w,mip._h,mip._pitch,Func2DArray16BVH(fresnel.Data(),specular.Data()));
      return true;
    case PacARGB8888:
      Fill32BProcInt(mem,mip._w,mip._h,mip._pitch,Func2DArray32BVH(fresnel.Data(),specular.Data()));
      return true;
    default:
      Fail("Unsupported format");
      return false;
  }
}

/// texture containing a irradiance map
class TextureSourceFresnelGlass: public TextureSourceSimple
{
  //! Glass refraction index
  float _refractionIndex;
public:
  TextureSourceFresnelGlass(PacFormat format, int w, int h, int nMipmaps, float refractionIndex);
  ~TextureSourceFresnelGlass();

  virtual TextureType GetTextureType() const {return TT_Irradiance;};
  virtual TexFilter GetMaxFilter() const {return TFLinear;}

  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  /** average color is not important for irradiance maps */
  PackedColor GetAverageColor() const {return PackedWhite;}
  /** irradiance maps have always alpha channel */
  bool IsAlpha() const {return true;}
  bool IsAlphaNonOpaque() const {return false;}
  /** irradiance is always clamped in both directions */
  int GetClamp() const {return TexClampU|TexClampV;}
  void SetClamp(int clampFlags) {}
};


TextureSourceFresnelGlass::TextureSourceFresnelGlass(PacFormat format, int w, int h, int nMipmaps, float refractionIndex)
  :TextureSourceSimple(format,w,h,nMipmaps)
{
  if (refractionIndex<=0)
  {
    RptF("Glass refraction index must be >0, given %g",_refractionIndex);
    refractionIndex = 0.001;
  }
  _refractionIndex = refractionIndex;
}

TextureSourceFresnelGlass::~TextureSourceFresnelGlass()
{
}

/**
Irradiance map generated using two 1D functions.
*/

bool TextureSourceFresnelGlass::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  // Prepare array for Fresnel data
  Assert(mip._w == (_w>>level));
  Assert(mip._h == (_h>>level));
  AUTO_STATIC_ARRAY(unsigned char,fresnel,512);
  fresnel.Resize(mip._w);
  const float invW = 1.0f/(mip._w-1);
  
  // Calculate the glass Fresnel factor upon cos of the input angle
  const float n1 = 1.0002926f;        // air index of refraction
  const float n2 = _refractionIndex;  // glass index of refraction
  for (int i=0; i<mip._w; i++)
  {
    float x = i*invW;
    float s1 = acos(x) + 1e-6; // +1 is here to avoid singular case
    float s2 = asin(sin(s1) * n1 / n2);
    float rs = (sin(s1 - s2) / sin(s1 + s2)); rs *= rs;
    float rp = (tan(s1 - s2) / tan(s1 + s2)); rp *= rp;
    float rr = (rs + rp) * 0.5f;
    float r = 2.0f * rr / (1 + rr);
    int resI = toInt(r*255);
    saturate(resI,0,255);
    fresnel[i] = resI;
  }
  
  // Fill out the texture with Fresnel data
  switch (mip._dFormat.GetEnumValue())
  {
  case PacAI88:
    Fill16BProcInt(mem,mip._w,mip._h,mip._pitch,Func2DArray16BNH(fresnel.Data()));
    return true;
  case PacARGB8888:
    Fill32BProcInt(mem,mip._w,mip._h,mip._pitch,Func2DArray32BNH(fresnel.Data()));
    return true;
  default:
    Fail("Unsupported format");
    return false;
  }
}

//! Texture containing a fresnel reflection coefficient, constructed upon N and K parameters
class TextureSourceFresnel: public TextureSourceSimple
{
  //! Parameters of the material (complex parameters in fresnel equation)
  float _n, _k;
public:
  //! Constructor
  TextureSourceFresnel(PacFormat format, int w, int h, int nMipmaps, float n, float k);
  //! Destructor
  ~TextureSourceFresnel();
  //! Virtual method
  virtual TextureType GetTextureType() const {return TT_Irradiance;};
  //! Virtual method
  virtual TexFilter GetMaxFilter() const {return TFLinear;}
  //! Virtual method
  virtual bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  /** average color is not important for irradiance maps */
  PackedColor GetAverageColor() const {return PackedWhite;}
  /** irradiance maps have always alpha channel */
  bool IsAlpha() const {return true;}
  bool IsAlphaNonOpaque() const {return false;}
  /** irradiance is always clamped in both directions */
  int GetClamp() const {return TexClampU|TexClampV;}
  void SetClamp(int clampFlags) {}
};

TextureSourceFresnel::TextureSourceFresnel(PacFormat format, int w, int h, int nMipmaps, float n, float k) : TextureSourceSimple(format,w,h,nMipmaps)
{
  if (n<=0)
  {
    RptF("Fresnel n must be >0, given n=%g,k=%g",n,k);
    n = 0.001;
  }
  if (k<=0)
  {
    RptF("Fresnel k must be >0, given n=%g,k=%g",n,k);
    k = 0.001;
  }
  _n = n;
  _k = k;
}

TextureSourceFresnel::~TextureSourceFresnel()
{
}

// division which alwas produces a finite result, never a NaN or infinity
static inline float SafeDiv(float nom, float denom)
{
  if (fabs(denom)<FLT_MIN)
  {
    // both zero, return 1
    if (fabs(nom)<FLT_MIN) return 1;
    // return almost infinity
    return FLT_MAX*(denom*nom>=0 ? +1 : -1);
  }
  return nom/denom;
}

/**
Irradiance map generated using two 1D functions.
*/

bool TextureSourceFresnel::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  // Prepare array for Fresnel data
  Assert(mip._w == (_w>>level));
  Assert(mip._h == (_h>>level));
  AUTO_STATIC_ARRAY(unsigned char,fresnel,512);
  fresnel.Resize(mip._w);
  const float invW = 1.0f/(mip._w-1);

  // Calculate the glass Fresnel factor upon cos of the input angle
  for (int i=0; i<mip._w; i++)
  {
    float x = i * invW;
    float s = acos(x);
    float sins = sin(s);
    float coss = cos(s);
    float tans = tan(s);
    float sin2 = sins * sins;
    float nksin = _n * _n - _k * _k - sin2;
    float cos2 = coss * coss;
    float tan2 = tans * tans;
    float a = sqrt((sqrt(nksin * nksin + 4 * _n * _n * _k * _k) + nksin)/2);
    float b = sqrt((sqrt(nksin * nksin + 4 * _n * _n * _k * _k) - nksin)/2);
    float fs = SafeDiv((a * a + b * b - 2 * a * coss + cos2) , (a * a + b * b + 2 * a * coss + cos2));
    // handle singular case - limit case of x = pi/2 - division leads to fs*tan2/tan2 -> fs
    float fp = i==0 ? fs : SafeDiv(fs * (a * a + b * b - 2 * a * sins * tans + sin2 * tan2) , (a * a + b * b + 2 * a * sins * tans + sin2 * tan2));
    float r = (fs + fp) / 2.0f;
    int resI = toInt(r * 255);
    saturate(resI, 0, 255);
    fresnel[i] = resI;
  }

  // Fill out the texture with Fresnel data
  switch (mip._dFormat.GetEnumValue())
  {
  case PacAI88:
    Fill16BProcInt(mem,mip._w,mip._h,mip._pitch,Func2DArray16BNH(fresnel.Data()));
    return true;
  case PacARGB8888:
    Fill32BProcInt(mem,mip._w,mip._h,mip._pitch,Func2DArray32BNH(fresnel.Data()));
    return true;
  default:
    Fail("Unsupported format");
    return false;
  }
}

//! Texture containing a random map with values from interval <0.5, 1> (for alpha testing purposes)
class TextureSourceRandomTest: public TextureSourceSimple
{
public:
  TextureSourceRandomTest(PacFormat format, int w, int h, int nMipmaps);
  //! implements TextureSourceSimple
  virtual TextureType GetTextureType() const {return TT_RandomTest;};
  virtual TexFilter GetMaxFilter() const {return TFTrilinear;}
  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  PackedColor GetAverageColor() const {return PackedBlack;}
  bool IsAlpha() const {return true;}
  bool IsAlphaNonOpaque() const {return false;}
  int GetClamp() const {return 0;}
  void SetClamp(int clampFlags) {}
};

TextureSourceRandomTest::TextureSourceRandomTest(PacFormat format, int w, int h, int nMipmaps)
  : TextureSourceSimple(format, w, h, nMipmaps)
{
}

bool TextureSourceRandomTest::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  // Fill out the texture with random data
  RandomGenerator rg;
  rg.SetSeed(0); // To always generate the same texture
  switch (mip._dFormat.GetEnumValue())
  {
  case PacAI88:
    for (int y=0; y<mip._h; y++)
    {
      unsigned short *dst = (unsigned short *)mem;
      // advance one line (note: not valid for DXT)
      mem = (char *)mem + mip._pitch;
      for (int x=0; x<mip._w; x++)
      {
        *dst++ = toInt(rg.RandomValue() * 255.0f) << 8;
      }
    }
    return true;
  case PacARGB8888:
    for (int y=0; y<mip._h; y++)
    {
      unsigned int *dst = (unsigned int *)mem;
      // advance one line (note: not valid for DXT)
      mem = (char *)mem + mip._pitch;
      for (int x=0; x<mip._w; x++)
      {
        DWORD v = toInt(rg.RandomValue() * 255.0f);
        *dst++ = (v << 24);
      }
    }
    return true;
  default:
    Fail("Unsupported format");
    return false;
  }
}

/// texture containing a tree crown map
class TextureSourceTreeCrown: public TextureSourceSimple
{
  /// specularity exponent
  float _density;
  
  public:
  TextureSourceTreeCrown(
    PacFormat format, float specPower, int w, int h, int nMipmaps
  );
  ~TextureSourceTreeCrown();

  virtual TextureType GetTextureType() const {return TT_TreeCrown;};
  virtual TexFilter GetMaxFilter() const {return TFLinear;}

  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  /** average color is not important for tree crown maps */
  PackedColor GetAverageColor() const {return PackedWhite;}
  /** tree crown maps have always alpha channel */
  bool IsAlpha() const {return true;}
  bool IsAlphaNonOpaque() const {return false;}
  /** tree crown is always clamped in both directions */
  int GetClamp() const {return TexClampU|TexClampV;}
  void SetClamp(int clampFlags) {}
};


TextureSourceTreeCrown::TextureSourceTreeCrown(
  PacFormat format, float density, int w, int h, int nMipmaps
)
:TextureSourceSimple(format,w,h,nMipmaps),
_density(density)
{
}

TextureSourceTreeCrown::~TextureSourceTreeCrown()
{
}

/**
TreeCrown map generated using two 1D functions.
*/

bool TextureSourceTreeCrown::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  // TODO: 1D texture is all we need here
  Assert(mip._w == (_w>>level));
  Assert(mip._h == (_h>>level));
  AUTO_STATIC_ARRAY(unsigned char,diffuseH,512);
  AUTO_STATIC_ARRAY(unsigned char,diffuseW,512);
  diffuseH.Resize(mip._h);
  diffuseW.Resize(mip._w);
  const float invW = 1.0f/(mip._w-1);
  const float invH = 1.0f/(mip._h-1);
  // calculate diffuse lighting amount based on light travel distance
  if (_density>0)
  {
    float logDensity = log(_density);
    for (int i=0; i<mip._h; i++)
    {
      float x = i*invH;
      float res = exp(x*logDensity);
      int resI = toInt(res*255);
      saturate(resI,0,255);
      diffuseH[i] = resI;
    }
    // calculate diffuse lighting amount based on light travel distance
    for (int i=0; i<mip._w; i++)
    {
      float x = i*invW;
      float res = exp(x*logDensity);
      int resI = toInt(res*255);
      saturate(resI,0,255);
      diffuseW[i] = resI;
    }
  }
  else
  {
    // handle singular case - log would fail here
    for (int i=0; i<mip._h; i++)
    {
      diffuseH[i] = 0;
    }
    for (int i=0; i<mip._w; i++)
    {
      diffuseW[i] = 0;
    }
  }
  switch (mip._dFormat.GetEnumValue())
  {
    case PacAI88:
      Fill16BProcInt(mem,mip._w,mip._h,mip._pitch,Func2DArray16B(diffuseW.Data(),diffuseH.Data()));
      return true;
    case PacARGB8888:
      Fill32BProcInt(mem,mip._w,mip._h,mip._pitch,Func2DArray32B(diffuseW.Data(),diffuseH.Data()));
      return true;
    default:
      Fail("Unsupported format");
      return false;
  }
}

///////////////////////////////////
/// texture containing a tree crown ambient map
class TextureSourceTreeCrownAmb: public TextureSourceSimple
{
  /// specularity exponent
  float _density;
  
  public:
  TextureSourceTreeCrownAmb(
    PacFormat format, float specPower, int w, int h, int nMipmaps
  );
  ~TextureSourceTreeCrownAmb();

  virtual TextureType GetTextureType() const {return TT_TreeCrown;};
  virtual TexFilter GetMaxFilter() const {return TFLinear;}

  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  /** average color is not important for tree crown ambient maps */
  PackedColor GetAverageColor() const {return PackedWhite;}
  /** tree crown ambient maps have always alpha channel */
  bool IsAlpha() const {return true;}
  bool IsAlphaNonOpaque() const {return false;}
  /** tree crown ambient is always clamped in both directions */
  int GetClamp() const {return TexClampU|TexClampV;}
  void SetClamp(int clampFlags) {}
};


TextureSourceTreeCrownAmb::TextureSourceTreeCrownAmb(
  PacFormat format, float density, int w, int h, int nMipmaps
)
:TextureSourceSimple(format,w,h,nMipmaps),
_density(density)
{
}

TextureSourceTreeCrownAmb::~TextureSourceTreeCrownAmb()
{
}

/// tree crown ambient lighting functor
template <class PackResult>
class TreeCrownAmb
{
  float _densityLog;
  /**
  this is here only to handle density = 0,
  which is errorneous, but we still want to work with it
  */
  float _density;
  
  public:
  TreeCrownAmb(float density)
  :_density(density),_densityLog(density>0 ? log(density) : 0)
  {}
  
  /// u,v in range 0..1
  int operator () (float u, float v) const
  {
    if (_density<=0) return 0;
    float x = u; // horizontal is always positive
    float y = v*2-1;
    float dist = x*x+y*y;
    float i = 1;
    if (dist<1)
    {
      i = exp(_densityLog*(1-dist));
    }
    return PackResult()(i,i);
  }
};

/**
TreeCrownAmb map generated using 2D function
*/

bool TextureSourceTreeCrownAmb::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  Assert(mip._w == (_w>>level));
  Assert(mip._h == (_h>>level));

  switch (mip._dFormat.GetEnumValue())
  {
    case PacAI88:
      {
        TreeCrownAmb<PackResultAI88> irr(_density);
        Fill16BProc(mem,mip._w,mip._h,mip._pitch,irr);
      }
      return true;
    case PacARGB8888:
      {
        TreeCrownAmb<PackResultARGB8888> irr(_density);
        Fill32BProc(mem,mip._w,mip._h,mip._pitch,irr);
      }
      return true;
    default:
      Fail("Unsupported format");
      return false;
  }
}
///////////////////////////////////

/// texture containing a dither pattern
class TextureSourceDither: public ITextureSource
{
  ///
  int _max,_min;
  int _nMipmaps; //!< source file mipmap count
  int _dim; //!< texture dimensions
  PacFormat _format; //!< source file pixel format

  public:
  TextureSourceDither(
    PacFormat format, int dim, int min, int max
  );
  ~TextureSourceDither();

  virtual TextureType GetTextureType() const {return TT_Dither;};
  virtual TexFilter GetMaxFilter() const {return TFLinear;}

  bool Init(PacLevelMem *mips, int maxMips);
  
  int GetMipmapCount() const {return _nMipmaps;}
  PacFormat GetFormat() const {return _format;}
  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const; 
  bool RequestMipmapData(
    const PacLevelMem *mips, int levelMin, int levelMax, FileRequestPriority prior
  ) const
  {
    // data are always ready
    return true;
  }
  bool IsLittleEndian() const {return IsPlatformLittleEndian();}

  PackedColor GetAverageColor() const
  {
    int avg = (_min+_max)/2;
    return PackedColor(avg,avg,avg,avg);
  }
  /// no dynamic range compression
  PackedColor GetMaxColor() const {return PackedColor(0xffffffff);}

  bool IsAlpha() const {return true;}
  bool IsTransparent() const {return false;}
  bool IsAlphaNonOpaque() const {return false;}
  /// hack used to fix textures that have alpha flag missing
  void ForceAlpha(){}
  
  float GetMaxTexDetail(float maxTexDetail) const {return FLT_MAX;}
  // no timestamps
  virtual QFileTime GetTimestamp() const {return 0;}
  virtual void FlushHandles() {}
  
  /** dither texture is looping */
  int GetClamp() const {return 0;}
  void SetClamp(int clampFlags) {}
};


TextureSourceDither::TextureSourceDither(
  PacFormat format, int dim, int min, int max
)
{
  _dim = dim;
  _format = format;
  _min = min;
  _max = max;
  int dimLog = log2Floor(dim);
  _nMipmaps = dimLog;
}
TextureSourceDither::~TextureSourceDither()
{
}

bool TextureSourceDither::Init(PacLevelMem *mips, int maxMips)
{
  int i;
  for (i=0; i<maxMips; )
  {
    //Assert(i < 10);
    PacLevelMem &mip=mips[i];

    mip._w = _dim>>i;
    mip._h = _dim>>i;
    mip._pitch = 0;
    mip._sFormat = _format;
    mip._dFormat = _format;
    mip._start = -1;

    i++;

    if (mip._w<=1 || mip._h<=1) break;
    
    // If there is a DXT compression
    if (PacFormatIsDXT(_format))
    {
      // If the resolution is smaller than minimum
      if ((mip._w <= MIN_MIP_SIZE) || (mip._h <= MIN_MIP_SIZE))
      {
        LogF("Warning: Texture is DXT compressed, but its mipmap resolution is smaller than %d.", MIN_MIP_SIZE);
        break;
      }
    }
  }
  _nMipmaps = i;

  return true;
}

static void CreateDitherMatrix(unsigned short *res, int dim, int min, int max)
{
  #define RES(x,z) res[z*dim+x]
  for (int x=0; x<dim; x++) for (int z=0; z<dim; z++)
  {
    RES(x,z) = 0;
  }
  //int step=2;
  for (int step=2; step<=dim; step*=2)
  {
    for (int x=0; x<dim; x+=step) for (int z=0; z<dim; z+=step)
    {
      for (int xx=x; xx<x+step/2; xx++)
      for (int zz=z; zz<z+step/2; zz++)
      {
        RES(xx,zz) += min;
      }
      for (int xx=x+step/2; xx<x+step; xx++)
      for (int zz=z; zz<z+step/2; zz++)
      {
        RES(xx,zz) += max-(max-min)*2/4;
      }
      for (int xx=x; xx<x+step/2; xx++)
      for (int zz=z+step/2; zz<z+step; zz++)
      {
        RES(xx,zz) += max-(max-min)*1/4;
      }
      for (int xx=x+step/2; xx<x+step; xx++)
      for (int zz=z+step/2; zz<z+step; zz++)
      {
        RES(xx,zz) += max-(max-min)*3/4;
      }
    }
    int nMin = 0;
    int nMax = (max-min)/4;
    min = nMin;
    max = nMax;
  }
  unsigned half = (max-min)/2;
  for (int x=0; x<dim; x++) for (int z=0; z<dim; z++)
  {
    unsigned int c = RES(x,z)+half;
    if (c>255) c = 255;
    RES(x,z) = (c<<8)|c;
  }
  #undef RES

}

bool TextureSourceDither::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  Assert(mip._w == (_dim>>level));
  Assert(mip._h == (_dim>>level));
  switch (mip._dFormat.GetEnumValue())
  {
    case PacAI88:
      CreateDitherMatrix((unsigned short *)mem,mip._w,_min,_max);
      return true;
    default:
      Fail("Unsupported format");
      return false;
  }
}

/// texture containing a single color
class TextureSourcePerlin: public TextureSourceSimple
{
  typedef TextureSourceSimple base;

  float _min,_max;
  float _xScale,_yScale;

public:
  TextureSourcePerlin(PacFormat format, int w, int h, int nMipmaps,
  float xScale, float yScale, float minV, float maxV);
  ~TextureSourcePerlin();

  /// average 0.5 describes us quite well, assuming min and max are balanced
  virtual TextureType GetTextureType() const {return TT_Detail;};
  virtual TexFilter GetMaxFilter() const {return TFTrilinear;}

  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  PackedColor GetAverageColor() const {return PackedColor(0x80808080);}
  bool IsAlpha() const {return true;}
  bool IsAlphaNonOpaque() const {return false;}
  /** no clamping needed for constant color texture */
  int GetClamp() const {return 0;}
  void SetClamp(int clampFlags) {}
};

TextureSourcePerlin::TextureSourcePerlin(
  PacFormat format, int w, int h, int nMipmaps,
  float xScale, float yScale,  float minV, float maxV
)
:base(format,w,h,nMipmaps),_min(minV),_max(maxV),_xScale(xScale),_yScale(yScale)
{

}

TextureSourcePerlin::~TextureSourcePerlin()
{
}

bool TextureSourcePerlin::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  float invW = 1.0f/mip._w;
  float invH = 1.0f/mip._h;
  // fill data needed, for each point check if we hit clutter in a near cell
  // TODO: for high resolution cell driver rasterization would be faster
  for (int y=0; y<mip._h; y++)
  {
    void *line = ((char *)mem)+mip._pitch*y;
    for (int x=0; x<mip._w; x++)
    {
      float u = (x+0.5f)*invW;
      float v = (y+0.5f)*invH;

      float perlin = (GPerlinNoise(u*_xScale,v*_yScale)*0.5f+0.5f)*(_max-_min)+_min;

      int a8 = toInt(perlin*255);
      saturate(a8,0,255);
      switch (mip._dFormat.GetEnumValue())
      {
        case PacAI88:
          // replicate into 2 bytes
          ((unsigned short *)line)[x] = (a8<<8)|a8;
          break;
        case PacARGB8888:
          // replicate into 4 bytes
          ((unsigned int *)line)[x] = (a8<<24)|(a8<<16)|(a8<<8)|a8;
          break;
      }
    }
  }
  return true;
}

class TextureSourcePoint : public TextureSourceSimple
{
  typedef TextureSourceSimple base;
public:
  TextureSourcePoint(PacFormat format, int w, int h, int nMipmaps);
  ~TextureSourcePoint();
  /// average 0.5 describes us quite well, assuming min and max are balanced
  virtual TextureType GetTextureType() const {return TT_Detail;};
  virtual TexFilter GetMaxFilter() const {return TFTrilinear;}
  bool GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const;
  PackedColor GetAverageColor() const {return PackedColor(0x80808080);}
  bool IsAlpha() const {return true;}
  bool IsAlphaNonOpaque() const {return false;}
  /** point texture clamped in both directions */
  int GetClamp() const {return TexClampU|TexClampV;}
  void SetClamp(int clampFlags) {}
};

TextureSourcePoint::TextureSourcePoint(PacFormat format, int w, int h, int nMipmaps) : base(format,w,h,nMipmaps)
{
}

TextureSourcePoint::~TextureSourcePoint()
{
}

bool TextureSourcePoint::GetMipmapData(void *mem, const PacLevelMem &mip, int level, int texSize) const
{
  float invW = 1.0f/(mip._w - 1);
  float invH = 1.0f/(mip._h - 1);
  // fill data needed, for each point check if we hit clutter in a near cell
  // TODO: for high resolution cell driver rasterization would be faster
  for (int y=0; y<mip._h; y++)
  {
    void *line = ((char *)mem)+mip._pitch*y;
    for (int x=0; x<mip._w; x++)
    {
      // Get u and v to correspond to values from texView
      float u = (x * invW - 0.5f) * 2.0f;
      float v = (y * invH - 0.5f) * 2.0f;

      // Calculate the alpha value
      float alpha = 1.0f - sqrt(u * u + v * v);

      // Write the value to output
      // all interesting data contained in alpha
      int a8 = toInt(alpha*255);
      saturate(a8,0,255);
      // alpha multiplied by the texture color in the shader - make sure the texture color is 1
      int i8 = 255;
      switch (mip._dFormat.GetEnumValue())
      {
      case PacAI88:
        ((unsigned short *)line)[x] = (a8<<8)|i8;
        break;
      case PacARGB8888:
        ((unsigned int *)line)[x] = (a8<<24)|(i8<<16)|(i8<<8)|i8;
        break;
      }
    }
  }
  return true;
}


DEFINE_ENUM(PacFormat, Pac, PAC_FORMAT_ENUM)


/// global instance of the factory
static TextureSourcePacFactory STextureSourcePacFactory;

static TextureSourceVideoFactory STextureSourceVideoFactory;

#define PROC_TEXTURE_ID_ENUM(type,prefix,XX) \
  XX(type, prefix, Irradiance) \
  XX(type, prefix, Color) \
  XX(type, prefix, Dither) \
  XX(type, prefix, PerlinNoise) \
  XX(type, prefix, WaterIrradiance) \
  XX(type, prefix, FresnelGlass) \
  XX(type, prefix, TreeCrown) \
  XX(type, prefix, TreeCrownAmb) \
  XX(type, prefix, Point) \
  XX(type, prefix, Fresnel) \
  XX(type, prefix, R2T) \

/// id of the procedure
#ifndef DECL_ENUM_PROC_TEXTURE_ID
#define DECL_ENUM_PROC_TEXTURE_ID
DECL_ENUM(ProcTextureId)
#endif
DECLARE_DEFINE_ENUM(ProcTextureId, PT, PROC_TEXTURE_ID_ENUM)

#define PROC_TEXTURE_FORMAT_ENUM(type,prefix,XX) \
  XX(type, prefix, AI) \
  XX(type, prefix, ARGB) \
  XX(type, prefix, RGB) \
  XX(type, prefix, A) \
  XX(type, prefix, I) \

/// id of the procedure
#ifndef DECL_ENUM_PROC_TEXTURE_FORMAT
#define DECL_ENUM_PROC_TEXTURE_FORMAT
DECL_ENUM(ProcTextureFormat)
#endif
DECLARE_DEFINE_ENUM(ProcTextureFormat, PF, PROC_TEXTURE_FORMAT_ENUM)


struct TextureProcDecl
{
  /// texture dimensions
  int w,h;
  /// texture mip-map count
  int nMipmaps;
  /// texture format
  PacFormat format;
  /// additional arguments
  RString args;
};

//! all routines required to create a procedural texture
class TextureSourceProcFactory: public ITextureSourceFactory
{
  ProcTextureId SelectProc(const char *name, TextureProcDecl &decl);
  
  public:
  bool Check(const char *name);
  bool PreInit(
    const char *name,
    RequestableObject *obj=NULL, RequestableObject::RequestContext *ctx=NULL
  );
  ITextureSource *Create(const char *name, PacLevelMem *mips, int maxMips);
  virtual TextureType GetTextureType(const char *name)
  {
    // following cannot fail any more, because of active object used (async creation)
    //Fail("Procedural textures should always be already created");
    // create a temporary source and ask it
    // TODO: perhaps optimize to reduce allocations?
    SRef<ITextureSource> src = Create(name,NULL,0);
    return src->GetTextureType();
  }
  virtual TexFilter GetMaxFilter(const char *name)
  {
    //Fail("Procedural textures should always be already created");
    SRef<ITextureSource> src = Create(name,NULL,0);
    return src->GetMaxFilter();
  }
};

static const char *GetWord(char *buf, int bufSize, const char *src, char delim)
{
  const char *end = strchr(src,delim);
  if (!end) return NULL;
  if (end-src>=bufSize) return NULL;
  strncpy(buf,src,end-src);
  buf[end-src] = 0;
  if (delim==0) return end;
  return ++end;
}
static const char *GetInt(int &buf, const char *src, char delim)
{
  if (!isdigit(src[0])) return NULL;
  buf = strtol(src,(char **)&src,10);
  if (*src!=delim) return NULL;
  if (delim==0) return src;
  return ++src;
}

/**
@return NProcTextureId on any input error
*/


ProcTextureId TextureSourceProcFactory::SelectProc(const char *name, TextureProcDecl &decl)
{
  const char *name0 = name;
  {
    (void)name0;
    Assert(name[0]=='#');
    name++;
    // parse texture format and dimensions
    if (*name!='(') goto Error;
    name++;
    // scan input, parse: format,w,h,nmipmaps
    char buf[64];
    name = GetWord(buf,sizeof(buf),name,',');
    if (!name) goto Error;

    ProcTextureFormat content = GetEnumValue<ProcTextureFormat>(buf);
    if ((int)content<0) goto Error;
    PacFormat format = PacARGB8888;
    switch (content)
    {
      // contents with no RGB can be stored as AI88
      case PFI: case PFA: case PFAI:
        format = PacAI88;
        break;
      // TODO: implement DXT1..5
      // without DXT support all other need ARGB8888
    }
    decl.format = format;
  
    name = GetInt(decl.w,name,',');
    if (!name) goto Error;
    name = GetInt(decl.h,name,',');
    if (!name) goto Error;
    name = GetInt(decl.nMipmaps,name,')');
    if (!name) goto Error;
    // check if w,h, mipmaps are reasonable values
    if (decl.nMipmaps<=0) goto ErrorDim;
    if (decl.w<=0) goto ErrorDim;
    if (decl.h<=0) goto ErrorDim;
    if (decl.w!=roundTo2PowerNCeil(decl.w)) goto ErrorDim;
    if (decl.h!=roundTo2PowerNCeil(decl.h)) goto ErrorDim;
    if ((1<<(decl.nMipmaps-1))>intMax(decl.w,decl.h)) goto ErrorDim;
    /// scan procedure name
    name0 = name;
    name = GetWord(buf,sizeof(buf),name,'(');
    if (!name) goto Error;
    ProcTextureId procType = GetEnumValue<ProcTextureId>(buf);
    if ((int)procType<0) goto Error;
    // additional arguments is everything from ( to )
    name = GetWord(buf,sizeof(buf),name,')');
    decl.args = buf;
    return procType;
  }
  
  Error:
  LogF("Procedural texture declaration syntax error in '%s',",name0);
  return NProcTextureId;

  ErrorDim:
  LogF("Invalid texture dimensions in '%s',",name0);
  return NProcTextureId;
}

bool TextureSourceProcFactory::Check(const char *name)
{
  TextureProcDecl decl;
  return SelectProc(name,decl)!=NProcTextureId;
}
bool TextureSourceProcFactory::PreInit(
  const char *name,
  RequestableObject *obj, RequestableObject::RequestContext *ctx
)
{
  // no data needed for procedural textures
  // make sure handler is executed immediately 
  obj->RequestDone(ctx,RequestableObject::RRSuccess);
  return true;
}

static const char *GetFloat(float &buf, const char *src, char delim, char delimAlternate = 0)
{
  if (!isdigit(src[0])) return NULL;
  buf = strtod(src,(char **)&src);
  if ((*src!=delim) && (*src!=delimAlternate)) return NULL;
  if (*src==0) return src;
  return ++src;
}

/*!
  \patch 5122 Date 1/24/2007 by Flyman
  - Fixed: Texture extension can be supplied with the procedural color texture now (texture type is calculated according to it)
*/
ITextureSource *CreateProcSource(ProcTextureId id, const TextureProcDecl &decl)
{
  switch (id)
  {
    case PTIrradiance:
      // scan arguments array
      {
        const char *name = decl.args;
        const char *name0 = name;
        float spec;
        name = GetFloat(spec,name,0);if (!name) goto ErrorIrrad;
        return new TextureSourceIrradiance(
          decl.format,spec,
          decl.w,decl.h,decl.nMipmaps
        );
        ErrorIrrad:
        LogF("Syntax error in irradiance arguments '%s'",name0);
        return NULL;
      }
    case PTWaterIrradiance:
      // scan arguments array
      {
        const char *name = decl.args;
        const char *name0 = name;
        float spec;
        name = GetFloat(spec,name,0);if (!name) goto ErrorWaterIrrad;
        return new TextureSourceWaterIrradiance(
          decl.format,spec,
          decl.w,decl.h,decl.nMipmaps
        );
        ErrorWaterIrrad:
        LogF("Syntax error in water irradiance arguments '%s'",name0);
        return NULL;
      }
    case PTFresnelGlass:
      {
        const char *name = decl.args;
        const char *name0 = name;
        float refractionIndex = 1.7f; // Refraction index - 1.7 is default (glass)
        if (strlen(name) > 0)
        {
          name = GetFloat(refractionIndex,name,0);
          if (!name) goto ErrorFresnelGlass;
        }
        return new TextureSourceFresnelGlass(decl.format, decl.w,1,1,refractionIndex);
ErrorFresnelGlass:
        LogF("Syntax error in fresnel glass arguments '%s'",name0);
        return NULL;
      }
    case PTFresnel:
      {
        const char *name = decl.args;
        const char *name0 = name;
        float n = 0.969770;
        float k = 0.0118000;
        if (strlen(name) > 0)
        {
          name = GetFloat(n,name,',');
          if (!name) goto ErrorFresnel;
          name = GetFloat(k,name,0);
          if (!name) goto ErrorFresnel;
        }
        return new TextureSourceFresnel(decl.format, decl.w,1,1,n,k);
ErrorFresnel:
        LogF("Syntax error in fresnel arguments '%s'",name0);
        return NULL;
      }
    case PTTreeCrown:
      // scan arguments array
      {
        const char *name = decl.args;
        const char *name0 = name;
        float density;
        name = GetFloat(density,name,0);if (!name) goto ErrorTreeCrown;
        return new TextureSourceTreeCrown(
          decl.format,density,
          decl.w,decl.h,decl.nMipmaps
        );
        ErrorTreeCrown:
        LogF("Syntax error in tree crown arguments '%s'",name0);
        return NULL;
      }
    case PTTreeCrownAmb:
      // scan arguments array
      {
        const char *name = decl.args;
        const char *name0 = name;
        float density;
        name = GetFloat(density,name,0);if (!name) goto ErrorTreeCrownAmb;
        return new TextureSourceTreeCrownAmb(
          decl.format,density,
          decl.w,decl.h,decl.nMipmaps
        );
        ErrorTreeCrownAmb:
        LogF("Syntax error in tree crown ambient arguments '%s'",name0);
        return NULL;
      }
    case PTColor:
      // scan arguments array
      {
        const char *name = decl.args;
        const char *name0 = name;
        float r,g,b,a;
        name = GetFloat(r,name,',');if (!name) goto ErrorColor;
        name = GetFloat(g,name,',');if (!name) goto ErrorColor;
        name = GetFloat(b,name,',');if (!name) goto ErrorColor;
        name = GetFloat(a,name,0,',');if (!name) goto ErrorColor;
        TextureType tt;
        if (*name==0)
        {
          tt = NTextureType;
        }
        else
        {
          // Add dot to the end of the string, make the string lowercase and get the texture type from it
          BString<32> nameWithDot;
          strcpy(nameWithDot, "_");
          strcat(nameWithDot, name);
          strcat(nameWithDot, ".");
          nameWithDot.StrLwr();
          tt = GetTextureTypeFromExtension(nameWithDot, "");
        }
        // anything other then 1x1 as no sense for procedural color texture
        // using it could waste quite some memory on X360 because of 4K mipmap alignment
        return new TextureSourceColor(
          decl.format,Color(r,g,b,a),
          1,1,1,tt
          );
        ErrorColor:
        LogF("Syntax error in color arguments '%s'",name0);
        return NULL;
      }
    case PTDither:
      // scan arguments array
      {
        const char *name = decl.args;
        const char *name0 = name;
        float min,max;
        name = GetFloat(min,name,',');if (!name) goto ErrorDither;
        name = GetFloat(max,name,0);if (!name) goto ErrorDither;
        { //scope needed, jump to label ErrorDither crosses initialization of int dim
          int dim = decl.w;
          if (dim<decl.h) dim = decl.h;
          return new TextureSourceDither(decl.format,dim,toInt(min),toInt(max));
        }
        ErrorDither:
        LogF("Syntax error in dither arguments '%s'",name0);
        return NULL;
      }
    case PTPerlinNoise:
      // scan arguments array
      {
        const char *name = decl.args;
        const char *name0 = name;
        {
          float min,max,xScale,yScale;
          name = GetFloat(xScale,name,',');if (!name) goto ErrorPerlin;
          name = GetFloat(yScale,name,',');if (!name) goto ErrorPerlin;
          name = GetFloat(min,name,',');if (!name) goto ErrorPerlin;
          name = GetFloat(max,name,0);if (!name) goto ErrorPerlin;
          return new TextureSourcePerlin(decl.format,decl.w,decl.h,decl.nMipmaps,xScale,yScale,min,max);
        }
        ErrorPerlin:
        LogF("Syntax error in perlin arguments '%s'",name0);
        return NULL;
      }
    case PTPoint:
      // no arguments
      return new TextureSourcePoint(decl.format,decl.w,decl.h,decl.nMipmaps);

#if _HELISIM_RENDER_TO_TEXTURE
    case PTR2T:
      {
        const char *arg = decl.args;
        char buf[64];
        float aspect;
        arg = GetWord(buf, sizeof(buf), arg, ','); if (!arg) goto ErrorR2T;
        arg = GetFloat(aspect,arg,0); if (!arg) goto ErrorR2T;

        return new TextureSourceR2T(decl.format, decl.w, decl.h, 1, RString(buf), 1.0f);

ErrorR2T:
        LogF("Syntax error in R2T arguments '%s'", decl.args);
        return NULL;
      }
#endif
  }
  Fail("Unsupported procedural texture type");
  return NULL;
}

#if _ENABLE_CHEATS
void TestPAA()
{
  _control87(_PC_53,_MCW_PC);
  {
	  PacLevelMem mip;
 	  SRef<ITextureSource> temp = new TextureSourceFresnel(PacARGB8888,64,64,1,1,0.7);
 	  mip._w = 64;
 	  mip._h = 64;
	  mip._dFormat = PacARGB8888;
	  mip._sFormat = PacARGB8888;
	  mip._pitch = mip._w*sizeof(int);
	  AutoArray<int> data;
	  data.Resize(mip._w*mip._h);
	  temp->GetMipmapData(data.Data(),mip,0,0);
	}
  {
	  PacLevelMem mip;
 	  SRef<ITextureSource> temp = new TextureSourceFresnel(PacARGB8888,32,128,1,1.95,0);
 	  mip._w = 32;
 	  mip._h = 128;
	  mip._dFormat = PacARGB8888;
	  mip._sFormat = PacARGB8888;
	  mip._pitch = mip._w*sizeof(int);
	  AutoArray<int> data;
	  data.Resize(mip._w*mip._h);
	  temp->GetMipmapData(data.Data(),mip,0,0);
	}
}
#endif


ITextureSource *TextureSourceProcFactory::Create(const char *name, PacLevelMem *mips, int maxMips)
{
  TextureProcDecl decl;
  ProcTextureId procId = SelectProc(name,decl);
  if (procId==NProcTextureId)
  {
    // texture does not exist
    return NULL;
  }
  ITextureSource *source = CreateProcSource(procId,decl);
  if (source && !source->Init(mips,maxMips))
  {
    delete source;
    return NULL;
  }
  return source;
}

/// global instance of the factory

static TextureSourceProcFactory STextureSourceProcFactory;

ITextureSourceFactory *SelectTextureSourceFactory(const char *name)
{
  if (!name) return NULL;
  if (name[0]=='#')
  {
    return &STextureSourceProcFactory;
  }
  const char *ext = GetFileExt(GetFilenameExt(name));
  if (!stricmp(ext,".pac") || !stricmp(ext,".paa"))
  {
    return &STextureSourcePacFactory;
  }
  
  if (!strcmp(ext,".ogg") || !strcmp(ext,".ogv"))
  {
    return &STextureSourceVideoFactory;
  }

  #if defined(_WIN32) 
  if (!stricmp(ext,".jpg") || !stricmp(ext,".jpeg"))
  {
    return GTextureSourceJPEGFactory;
  }
  #endif
  RptF("Unrecognized texture type '%s': '%s'",ext,name);
  return NULL;
}


ITextureSource *CreateColorTextureSource(ColorVal color)
{
  // autoselect best possible format
  // there is probably no reason to use anything else but 8888
  return new TextureSourceColor(PacARGB8888,color,1,1,1);
}

ITextureSource *CreateRandomTestTextureSource()
{
  return new TextureSourceRandomTest(PacAI88, 64, 64, 7);
}

ITextureSource *CreateDitherTextureSource(int dim, int min, int max)
{
  // autoselect best possible format
  // there is probably no reason to use anything else but 88
  return new TextureSourceDither(PacAI88,dim,min,max);
}

ITextureSource *CreatePerlinNoise(int x, int y, int nMipmaps, float xScale, float yScale, float min, float max)
{
  // there is probably no reason to use anything else but 88
  return new TextureSourcePerlin(PacAI88,x,y,nMipmaps,xScale,yScale,min,max);
}

TextureHeader::TextureHeader():_isFromPBO(false), _textureFileSize(0), _isUsable(true)
{
}

TextureHeader::TextureHeader(const RString& name, const TextureSourcePac& src, int numMipmaps, PacLevelMem* mipmaps, int textureFileSize)
:_src(src), _mipmaps(mipmaps, numMipmaps), _isFromPBO(false), _textureFileSize(textureFileSize), _isUsable(true)
{
  _src.SetName(name);
}


void TextureHeader::SerializeBin(SerializeBinStream &f)
{
  // load/save version
  const int currentVersion = 1;
  int version = currentVersion;
  f.Transfer(version);

  DoAssert(version <= currentVersion);

  _src.SerializeBin(f);
  f.TransferArray(_mipmaps);
  f.Transfer(_textureFileSize);
}

//----------------------------------------------------------------------------------------------
// TextureHeaderManager implementation
//----------------------------------------------------------------------------------------------

const char * const TextureHeaderManager::DefaultFileName = "texHeaders.bin";

  

bool TextureHeaderManager::AddHeaderFromFile(const RString& fileName, const RString& textureName)
{
  // create texture source factory
  TextureSourcePacFactory* factory = dynamic_cast<TextureSourcePacFactory *>(SelectTextureSourceFactory(fileName));
  if (!factory)
  {
    RptF("Error while inserting texture \"%s\" to TextureHeaderManager - invalid file type.", cc_cast(textureName));
    return false;
  }

  // this is not very good, but MAX_MIPMAPS may not be available here...
  static const int MaxMipmaps = 12;

  PacLevelMem mipmaps[MaxMipmaps];
  // load header
  const SRef<ITextureSource> source(factory->Create(fileName, mipmaps, MaxMipmaps));
  if (source.IsNull())
  {
    RptF("Error while inserting texture \"%s\" to TextureHeaderManager - failed to read the texture.", cc_cast(textureName));
    return false;
  }

  TextureSourcePac* srcPac = dynamic_cast<TextureSourcePac*>(source.GetRef());
  if (!srcPac)
  {
    RptF("Error while inserting texture \"%s\" to TextureHeaderManager - invalid file type.", cc_cast(textureName));
    return false;
  }  

  return AddHeader(textureName, *srcPac, srcPac->GetMipmapCount(), mipmaps, QIFileFunctions::GetFileSize(fileName));
}


bool TextureHeaderManager::SaveToFile(const RString& file)
{
  QOFStream out;
  out.open(file.Data());
  if (out.fail())
  {
    RptF("Failed to save TextureHeaderManager to file \"%s\" - failed to open the file.", cc_cast(file));
    return false;
  }

  SerializeBinStream bs(&out);
  SerializeBin(bs, false, RString());
  bool ok = (!out.fail() && bs.GetError() == SerializeBinStream::EOK);
  if (!ok)
  {
    RptF("Failed to save TextureHeaderManager to file \"%s\" - failed to save binstream.", cc_cast(file));
  }
  out.close();

  return ok;
}

bool TextureHeaderManager::LoadFromFile(const RString& file, bool append, bool isFromPBO, const RString& prefix)
{
  QIFStreamB in;
  in.AutoOpen(file.Data());
  if (in.fail())
  {
    RptF("Failed to load TextureHeaderManager from file \"%s\" - invalid file structure.", cc_cast(file));
    return false;
  }

  in.PreReadSequential();

  if (!append)
    _headersMap.Clear();

  SerializeBinStream bs(&in);
  SerializeBin(bs, isFromPBO, prefix);
  bool ok = (!in.fail() && bs.GetError() == SerializeBinStream::EOK);
  if (!ok)
  {
    RptF("Failed to load TextureHeaderManager from file \"%s\" - failed to open the file.", cc_cast(file));
  }
  in.close(); 
  return ok;
}

void TextureHeaderManager::SerializeBin(SerializeBinStream &f, bool isFromPBO, const RString& prefix)
{
#ifdef _MSC_VER
  if (!f.Version('THD0'))
#else
  if (!f.Version(StrToInt("0DHT")))
#endif
  {
    f.SetError(f.EBadFileType);
    return;
  }

  // load/save version
  const int currentVersion = 1;
  int version = currentVersion;
  f.Transfer(version);

  DoAssert(version <= currentVersion);

  if (f.IsLoading())
  {
    //loading
    int n = 0;
    f.Transfer(n);
    _headersMap.Reserve(_headersMap.NItems() + n);
    for (int i=0; i != n; ++i)
    {
      TextureHeader header;
      header.SerializeBin(f);
      if (f.GetError() != SerializeBinStream::EOK)
      {
        DoAssert(!"Invalid texture header file structure!");
        return;
      }

      header._isFromPBO = isFromPBO;
      // add prefix if needed
      if (!prefix.IsEmpty())
      {
        RString name = prefix;
        name.Lower();
        // do not add prefix if current name already starts with prefix
        if (header._src.GetName().IsEmpty() || CmpStartStr(header._src.GetName(), name) != 0)
        {
          name = name + header._src.GetName();
          header._src.SetName(name);
        }
      }

      // try to find the texture in the map
      const TextureHeader& h = _headersMap.Get(header.GetKey());
      if (_headersMap.NotNull(h))
      {
        // header already in the manager
        Assert(!"Texture header already in the manager!");
        // test if the headers are the same
        bool areTheSame = true;
        
        if (h._mipmaps.Size() != header._mipmaps.Size() || h._textureFileSize != header._textureFileSize)
        {
          areTheSame = false;
        }
        else
        {
          for (int i=0; i < h._mipmaps.Size(); ++i)
          {
            if (h._mipmaps[i]._w != header._mipmaps[i]._w)
            {
              areTheSame = false;
              break;
            }
            if (h._mipmaps[i]._h != header._mipmaps[i]._h)
            {
              areTheSame = false;
              break;
            }
            if (h._mipmaps[i]._pitch != header._mipmaps[i]._pitch)
            {
              areTheSame = false;
              break;
            }
            if (h._mipmaps[i]._start != header._mipmaps[i]._start)
            {
              areTheSame = false;
              break;
            }
            if (h._mipmaps[i]._sFormat != header._mipmaps[i]._sFormat)
            {
              areTheSame = false;
              break;
            }
            if (h._mipmaps[i]._dFormat != header._mipmaps[i]._dFormat)
            {
              areTheSame = false;
              break;
            }
          }
        }

        if (!areTheSame)
        {
          // this is serious error - new and old headers are not the same..we better remove this header from the manager, so it is read from the file
          Assert(!"New and old header are different! Removing header from the manager.");
          RptF("Texture header \"%s\" already in the manager and new header is different! Removing header from the manager.", header.GetKey());
          _headersMap.Remove(header.GetKey());
        }
      }
      else
      {
        // header is not in the manager - just add it
        _headersMap.Add(header);
      }

    }
  }
  else
  {
    //saving
    int n = _headersMap.NItems();
    f.Transfer(n);
    if (n > 0)
    {
      for (int i=0; i != _headersMap.NTables(); ++i)
      {
        AutoArray<TextureHeader> &table = _headersMap.GetTable(i);
        for (int j=0; j != table.Size(); ++j)
        {
          TextureHeader& header = table[j];
          header.SerializeBin(f);
        }
      }
    }
   
  }
}

bool TextureHeaderManager::HasHeader(const RString& fileName) const
{
  return GetHeader(fileName) != NULL;
}

const TextureHeader* TextureHeaderManager::GetHeader(const RString& fileName) const
{
  if (_headersMap.NItems() == 0)
    return NULL;
  RString name = fileName;
  name.Lower();
  const TextureHeader& header = _headersMap.Get(name);
  if (_headersMap.NotNull(header))
    return &header;
  else
    return NULL;
}

void TextureHeaderManager::CheckHeaderIsCorrect(TextureHeader &header, THeadersMap *headersMap, void *context)
{
  Assert(context);
  if (!context)
    return;

  TextureHeadersTestContext* thContext = static_cast<TextureHeadersTestContext*>(context);

#if _ENABLE_PATCHING
  if (header._isFromPBO && !QFBankQueryFunctions::FileIsTakenFromBank(header.GetKey()))
  {
    // header is patched - do not test for size, just flag it as not usable 
    Log("Texture \"%s\" is patched - header will not be used.", cc_cast(header.GetKey()));
    header._isUsable = false;

    thContext->_numPatched++;
    return;
  }
#endif

  QFileSize realSize = QFBankQueryFunctions::GetFileSize(header.GetKey());
  if (header._textureFileSize != realSize)
  {
    Assert(false && "Different file size of texture and original texture, that the header was taken from! Invalid header!");
    Log("Different file size of texture and original texture, that the header was taken from! Invalid header: \"%s\", realSize = %d, size in header = %d!", 
      cc_cast(header.GetKey()), realSize, header._textureFileSize);
    // flag the header as not usable
    header._isUsable = false;
    thContext->_numFailed++;
  }
  else
  {
    thContext->_numOK++;
  }
}

bool TextureHeaderManager::CheckHeadersAreCorrect() 
{
  TextureHeadersTestContext ctx;
  _headersMap.ForEach(CheckHeaderIsCorrect, &ctx);
  Log("Checking texture headers done - ok = %d, failed = %d, patched = %d.", ctx._numOK, ctx._numFailed, ctx._numPatched);
  return ctx._numFailed == 0;
}

#if _ENABLE_TEXTURE_HEADERS
TextureHeaderManager GTextureHeaderManager;
bool GUseTextureHeaders = true;
#endif

void ITextureSource::FlushHandles()
{
#ifdef _WIN32
  throw std::exception("The method or operation is not implemented.");
#endif
}

//---

TextureSourceVideo::TextureSourceVideo(
                                       PacFormat format, int w, int h, int nMipmaps
                                       )
                                       :_w(w),_h(h),_nMipmaps(nMipmaps),
                                       _format(format)
{
  _textureCreated = false;
  _tt = TT_Diffuse;
  _decoder = new OggDecoder();
}

TextureSourceVideo::TextureSourceVideo()
{
  _textureCreated = false;
  _w  = 0;
  _h  = 0;
  _nMipmaps = 1;
  _format = PacARGB8888;
  _tt = TT_Diffuse;
  _decoder = new OggDecoder();
}

TextureSourceVideo::~TextureSourceVideo()
{
  _decoder.Free();
}


bool TextureSourceVideo::Init( PacLevelMem *mips, int maxMips )
{

  SetTimestamp(_name);
  _decoder->init(_name,_w, _h, false);

  int i = -1;

  if (maxMips>_nMipmaps) maxMips = _nMipmaps;
  for (i=0; i<maxMips; )
  {
    PacLevelMem &mip=mips[i];

    mip._w = _w>>i;
    mip._h = _h>>i;
    mip._pitch = 0;
    mip._sFormat = _format;
    mip._dFormat = _format;
    mip._start = -1;

    i++;

    if (mip._w<=1 || mip._h<=1) break;

    // If there is a DXT compression
    if (PacFormatIsDXT(_format))
    {
      // If the resolution is smaller than minimum
      if ((mip._w <= MIN_MIP_SIZE) || (mip._h <= MIN_MIP_SIZE))
      {
        LogF("Warning: Texture is DXT compressed, but its mipmap resolution is smaller than %d.", MIN_MIP_SIZE);
        break;
      }
    }
  }
  _nMipmaps = i;

  return  true;

}


bool TextureSourceVideo::GetMipmapData( void *mem, const PacLevelMem &mip, int level, int texSize ) const
{
  _textureCreated = true; 
  _decoder->getData(&mem);
    return  true;  
  }

PackedColor TextureSourceVideo::GetAverageColor() const
{
  return PackedColor(0xffffffff);
}

bool TextureSourceVideo::IsAlpha() const
{
  return  true;
}

bool TextureSourceVideo::IsAlphaNonOpaque() const
{
  return  true;
}

void TextureSourceVideo::SetName( const char *name )
{
  _name = name;
  //_tt = GetTextureTypeFromName(name);
}


bool TextureSourceVideo::IgnoreLoadedLevels() const
{
  return  _textureCreated;
}

QFileTime TextureSourceVideo::GetTimestamp() const
{
  return _timestamp;
}

void TextureSourceVideo::SetTimestamp(RStringB name)
{
  QIFStreamB *stream = new QIFStreamB;
  _timestamp = QFBankQueryFunctions::TimeStamp(name);
  delete stream;

}


