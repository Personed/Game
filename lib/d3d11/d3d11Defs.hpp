#ifdef _MSC_VER
#pragma once
#endif

#ifndef _D3D11_DEFS_HPP
#define _D3D11_DEFS_HPP

#include <d3d11.h>

#include <Es/Memory/normalNew.hpp>
#include "Dx11MathCompat.hpp"
#include <Es/Memory/debugNew.hpp>

#if _ENABLE_PERFLOG && _PROFILE
#define PERF_PROF 1
#else
#define PERF_PROF 0
#endif

#include <El/Common/perfProf.hpp>

namespace DX11
{

typedef ID3D11ShaderResourceView TextureHandle11;

//#if !defined _XBOX && (_DEBUG || _PROFILE)
//  #if _DEBUG
//    #define SHADER_DBG_LEVEL 1
//  #else
//    #define SHADER_DBG_LEVEL 1
//  #endif
//#else
//  #define SHADER_DBG_LEVEL 0
//#endif

// LUKE HACK - just to speed things up
// DX11 TODO
#define SHADER_DBG_LEVEL 0


// Keep the validated DX11 state-list path active. The experimental hashed
// state-object path stalled during Chernarus startup on the x64 probe.
#define DX11_USE_HASHED_STATE_OBJECTS 0

#if PERF_PROF

#define PROFILE_DX_SCOPE(name) PROFILE_SCOPE_EX(name,d3d)
#define PROFILE_DX_SCOPE_DETAIL(name) PROFILE_SCOPE_DETAIL_EX(name,d3d)

#else
#define PROFILE_DX_SCOPE(name)
#define PROFILE_DX_SCOPE_DETAIL(name)
#endif

//!{ Definitions used for converting from DX9 to DX11
typedef int           D3DRENDERSTATETYPE_OLD;
typedef int           D3DSAMPLERSTATETYPE_OLD;
typedef int           D3DTEXTURESTAGESTATETYPE_OLD;
typedef ID3D11Buffer  IDirect3DIndexBuffer9Old;
typedef ID3D11Buffer  IDirect3DVertexBuffer9Old;
typedef int           D3DMULTISAMPLE_TYPE_OLD;
typedef int           D3DPRIMITIVETYPE_OLD;
typedef DWORD         D3DCOLOR;
#define D3D_OK        S_OK

enum RenderStatesFromDX9
{
  D3DRS_ZENABLE_OLD                   = 7,    /* D3DZBUFFERTYPE (or TRUE/FALSE for legacy) */
  D3DRS_FILLMODE_OLD                  = 8,    /* D3DFILLMODE */
  D3DRS_SHADEMODE_OLD                 = 9,    /* D3DSHADEMODE */
  D3DRS_ZWRITEENABLE_OLD              = 14,   /* TRUE to enable z writes */
  D3DRS_ALPHATESTENABLE_OLD           = 15,   /* TRUE to enable alpha tests */
  D3DRS_SRCBLEND_OLD                  = 19,   /* D3DBLEND */
  D3DRS_DESTBLEND_OLD                 = 20,   /* D3DBLEND */
  D3DRS_CULLMODE_OLD                  = 22,   /* D3DCULL */
  D3DRS_ZFUNC_OLD                     = 23,   /* D3DCMPFUNC */
  D3DRS_ALPHAREF_OLD                  = 24,   /* D3DFIXED */
  D3DRS_ALPHAFUNC_OLD                 = 25,   /* D3DCMPFUNC */
  D3DRS_DITHERENABLE_OLD              = 26,   /* TRUE to enable dithering */
  D3DRS_ALPHABLENDENABLE_OLD          = 27,   /* TRUE to enable alpha blending */
  D3DRS_FOGCOLOR_OLD                  = 34,   /* D3DCOLOR */
  D3DRS_STENCILENABLE_OLD             = 52,   /* BOOL enable/disable stenciling */
  D3DRS_STENCILFAIL_OLD               = 53,   /* D3DSTENCILOP when stencil test fails */
  D3DRS_STENCILZFAIL_OLD              = 54,   /* D3DSTENCILOP when stencil passes and depth fails */
  D3DRS_STENCILPASS_OLD               = 55,   /* D3DSTENCILOP to do if both stencil and Z tests pass */
  D3DRS_STENCILFUNC_OLD               = 56,   /* D3DCMPFUNC fn.  Stencil Test passes if ((ref & mask) stencilfn (stencil & mask)) is true */
  D3DRS_STENCILREF_OLD                = 57,   /* Reference value used in stencil test */
  D3DRS_STENCILMASK_OLD               = 58,   /* Mask value used in stencil test */
  D3DRS_STENCILWRITEMASK_OLD          = 59,   /* Write mask applied to values written to stencil buffer */
  D3DRS_CLIPPING_OLD                  = 136,
  D3DRS_LIGHTING_OLD                  = 137,
  D3DRS_POINTSIZE_OLD                 = 154,   /* in DX11 used to signal alpha to coverage usage*/
  D3DRS_COLORWRITEENABLE_OLD          = 168,  // per-channel write enable
  D3DRS_BLENDOP_OLD                   = 171,   // D3DBLENDOP setting
  D3DRS_BLENDFACTOR_OLD               = 193,   // D3DCOLOR used by blend-factor blend modes
  D3DRS_SLOPESCALEDEPTHBIAS_OLD       = 175,
  D3DRS_TWOSIDEDSTENCILMODE_OLD       = 185,   /* BOOL enable/disable 2 sided stenciling */
  D3DRS_CCW_STENCILPASS_OLD           = 188,   /* D3DSTENCILOP to do if both ccw stencil and Z tests pass */
  D3DRS_CCW_STENCILFUNC_OLD           = 189,   /* D3DCMPFUNC fn.  ccw Stencil Test passes if ((ref & mask) stencilfn (stencil & mask)) is true */
  D3DRS_SRGBWRITEENABLE_OLD           = 194,   /* Enable rendertarget writes to be DE-linearized to SRGB (for formats that expose D3DUSAGE_QUERY_SRGBWRITE) */
  D3DRS_DEPTHBIAS_OLD                 = 195,
  D3DRS_SEPARATEALPHABLENDENABLE_OLD  = 206,  /* TRUE to enable a separate blending function for the alpha channel */
  D3DRS_SRCBLENDALPHA_OLD             = 207,  /* SRC blend factor for the alpha channel when D3DRS_SEPARATEDESTALPHAENABLE is TRUE */
  D3DRS_DESTBLENDALPHA_OLD            = 208,  /* DST blend factor for the alpha channel when D3DRS_SEPARATEDESTALPHAENABLE is TRUE */
  D3DRS_BLENDOPALPHA_OLD              = 209   /* Blending operation for the alpha channel when D3DRS_SEPARATEDESTALPHAENABLE is TRUE */
};
enum SamplerStatesFromDX9
{
  D3DSAMP_ADDRESSU_OLD       = 1,  /* D3DTEXTUREADDRESS_OLD for U coordinate */
  D3DSAMP_ADDRESSV_OLD       = 2,  /* D3DTEXTUREADDRESS_OLD for V coordinate */
  D3DSAMP_ADDRESSW_OLD       = 3,  /* D3DTEXTUREADDRESS_OLD for W coordinate */
  D3DSAMP_BORDERCOLOR_OLD    = 4,  /* D3DCOLOR */
  D3DSAMP_MAGFILTER_OLD      = 5,  /* D3DTEXTUREFILTER filter to use for magnification */
  D3DSAMP_MINFILTER_OLD      = 6,  /* D3DTEXTUREFILTER filter to use for minification */
  D3DSAMP_MIPFILTER_OLD      = 7,  /* D3DTEXTUREFILTER filter to use between mipmaps during minification */
  D3DSAMP_MIPMAPLODBIAS_OLD  = 8,  /* float mip level bias */
  D3DSAMP_MAXANISOTROPY_OLD  = 10, /* DWORD maximum anisotropy */
  D3DSAMP_SRGBTEXTURE_OLD    = 11, /* Default = 0 (which means Gamma 1.0*/
};
enum TextureAdressFromDX9
{
  D3DTADDRESS_WRAP_OLD            = 1,
  D3DTADDRESS_MIRROR_OLD          = 2,
  D3DTADDRESS_CLAMP_OLD           = 3,
  D3DTADDRESS_BORDER_OLD          = 4,
  D3DTADDRESS_MIRRORONCE_OLD      = 5,
};
#define D3DTEXTUREADDRESS_OLD TextureAdressFromDX9
enum TextureStageStateFromDX9
{
  //   D3DTSS_COLOROP_OLD        =  1, /* D3DTEXTUREOP - per-stage blending controls for color channels */
  //   D3DTSS_ALPHAOP_OLD        =  4, /* D3DTEXTUREOP - per-stage blending controls for alpha channel */
  //   D3DTSS_TEXCOORDINDEX_OLD  = 11, /* identifies which set of texture coordinates index this texture */
};

#define D3DTEXTUREFILTERTYPE_OLD FilterTypeFromDX9
enum FilterTypeFromDX9
{
  D3DTEXF_NONE_OLD            = 0,    // filtering disabled (valid for mip filter only)
  D3DTEXF_POINT_OLD           = 1,    // nearest
  D3DTEXF_LINEAR_OLD          = 2,    // linear interpolation
  D3DTEXF_ANISOTROPIC_OLD     = 3,    // anisotropic
  D3DTEXF_PYRAMIDALQUAD_OLD   = 6,    // approximate with linear filtering on D3D11
  D3DTEXF_GAUSSIANQUAD_OLD    = 7,    // approximate with linear filtering on D3D11
};
enum StencilOpFromDX9
{
  D3DSTENCILOP_KEEP_OLD           = 1,
  D3DSTENCILOP_ZERO_OLD           = 2,
  D3DSTENCILOP_REPLACE_OLD        = 3,
  D3DSTENCILOP_INCRSAT_OLD        = 4,
  D3DSTENCILOP_DECRSAT_OLD        = 5,
  D3DSTENCILOP_INVERT_OLD         = 6,
  D3DSTENCILOP_INCR_OLD           = 7,
  D3DSTENCILOP_DECR_OLD           = 8,
};
enum CmpFuncFromDX9
{
  D3DCMP_NEVER_OLD                = 1,
  D3DCMP_LESS_OLD                 = 2,
  D3DCMP_EQUAL_OLD                = 3,
  D3DCMP_LESSEQUAL_OLD            = 4,
  D3DCMP_GREATER_OLD              = 5,
  D3DCMP_NOTEQUAL_OLD             = 6,
  D3DCMP_GREATEREQUAL_OLD         = 7,
  D3DCMP_ALWAYS_OLD               = 8,
};
enum CullFromDX9
{
  D3DCULL_NONE_OLD                = 1,
  D3DCULL_CW_OLD                  = 2,
  D3DCULL_CCW_OLD                 = 3,
};
enum BlendFromDX9
{
  D3DBLEND_ZERO_OLD               = 1,
  D3DBLEND_ONE_OLD                = 2,
  D3DBLEND_SRCCOLOR_OLD           = 3,
  D3DBLEND_INVSRCCOLOR_OLD        = 4,
  D3DBLEND_SRCALPHA_OLD           = 5,
  D3DBLEND_INVSRCALPHA_OLD        = 6,
  D3DBLEND_DESTALPHA_OLD          = 7,
  D3DBLEND_INVDESTALPHA_OLD       = 8,
  D3DBLEND_DESTCOLOR_OLD          = 9,
  D3DBLEND_INVDESTCOLOR_OLD       = 10,
  D3DBLEND_SRCALPHASAT_OLD        = 11,
  D3DBLEND_BOTHSRCALPHA_OLD       = 12,
  D3DBLEND_BOTHINVSRCALPHA_OLD    = 13,
  D3DBLEND_BLENDFACTOR_OLD        = 14,
  D3DBLEND_INVBLENDFACTOR_OLD     = 15,
  D3DBLEND_SRCCOLOR2_OLD          = 16,
  D3DBLEND_INVSRCCOLOR2_OLD       = 17,
};
enum ZBTypeFromDX9
{
  D3DZB_FALSE_OLD                 = 0,
  D3DZB_TRUE_OLD                  = 1, // Z buffering
  D3DZB_USEW_OLD                  = 2, // W buffering
};
enum TextureOpFromDX9
{
  D3DTOP_DISABLE_OLD              = 1,      // disables stage
  D3DTOP_SELECTARG1_OLD           = 2,      // the default
  D3DTOP_MODULATE2X_OLD           = 5,      // multiply and  1 bit
};
enum PrimitiveTypeFromDX9
{
  D3DPT_TRIANGLELIST_OLD          = 4,
  D3DPT_TRIANGLESTRIP_OLD         = 5,
  D3DPT_TRIANGLEFAN_OLD           = 6,
};
enum FillModeFromDX9
{
  D3DFILL_POINT_OLD              = 1,
  D3DFILL_WIREFRAME_OLD           = 2,
  D3DFILL_SOLID_OLD               = 3,
};

enum ShadeModeFromDX9
{
  D3DSHADE_GOURAUD_OLD            = 2,
};

enum BlendOpFromDX9
{
  D3DBLENDOP_ADD_OLD              = 1,
  D3DBLENDOP_SUBTRACT_OLD         = 2,
  D3DBLENDOP_REVSUBTRACT_OLD      = 3,
  D3DBLENDOP_MIN_OLD              = 4,
  D3DBLENDOP_MAX_OLD              = 5,
};

#define D3DCOLORWRITEENABLE_RED_OLD     (1L<<0)
#define D3DCOLORWRITEENABLE_GREEN_OLD   (1L<<1)
#define D3DCOLORWRITEENABLE_BLUE_OLD    (1L<<2)
#define D3DCOLORWRITEENABLE_ALPHA_OLD   (1L<<3)


static const int ShadowMapDepthBias = 10000;
static const float ShadowMapSlopeScaleDepthBias = 1.0f;

}//namespace DX11
#endif //_D3D11_DEFS_HPP
