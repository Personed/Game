
#ifdef _MSC_VER
#pragma once
#endif

#ifndef _D3DDEFST_HPP
#define _D3DDEFST_HPP

#include <Es/common/win.h>

#include <d3d9.h>
#include <d3d10.h>
#include <d3d10Misc.h>

#include <Es/Memory/normalNew.hpp>
#include <d3dx10.h>
#include <Es/Memory/debugNew.hpp>

typedef ID3D10ShaderResourceView *TextureD3DHandle;

//! Texture and associated resource view
struct SurfaceData
{
  //! Texture data
  ComRef<ID3D10Texture2D> _texture;
  //! Texture view
  ComRef<ID3D10ShaderResourceView> _resview;
};

//! Structure to hold a particular vertex shader and some informations associated with it
struct VertexShaderInfo
{
  //! Vertex shader
  ComRef<ID3D10VertexShader> _shader;
  //! Vertex declaration object
  ComRef<ID3D10InputLayout> _inputLayout;
  //! Bytecode the shader was created from (will be used later for layout creation)
  //ComRef<ID3D10Blob> _shaderBytecode;
  //! Free the resources allocated
  void Free()
  {
    _shader.Free();
    _inputLayout.Free();
    //_shaderBytecode.Free();
  }
};

//!{ Definitions used for converting from DX9 to DX10
typedef int           D3DRENDERSTATETYPE_OLD;
typedef int           D3DSAMPLERSTATETYPE_OLD;
typedef int           D3DTEXTURESTAGESTATETYPE_OLD;
typedef ID3D10Buffer  IDirect3DIndexBuffer9Old;
typedef ID3D10Buffer  IDirect3DVertexBuffer9Old;
typedef int           D3DMULTISAMPLE_TYPE_OLD;
typedef int           D3DPRIMITIVETYPE_OLD;
typedef DWORD         D3DCOLOR;
#define D3D_OK        S_OK

enum RenderStatesFromDX9
{
  D3DRS_ZENABLE_OLD                   = 7,    /* D3DZBUFFERTYPE (or TRUE/FALSE for legacy) */
  D3DRS_ZWRITEENABLE_OLD              = 14,   /* TRUE to enable z writes */
  D3DRS_ALPHATESTENABLE_OLD           = 15,   /* TRUE to enable alpha tests */
  D3DRS_SRCBLEND_OLD                  = 19,   /* D3DBLEND */
  D3DRS_DESTBLEND_OLD                 = 20,   /* D3DBLEND */
  D3DRS_CULLMODE_OLD                  = 22,   /* D3DCULL */
  D3DRS_ZFUNC_OLD                     = 23,   /* D3DCMPFUNC */
  D3DRS_ALPHAREF_OLD                  = 24,   /* D3DFIXED */
  D3DRS_ALPHAFUNC_OLD                 = 25,   /* D3DCMPFUNC */
  D3DRS_ALPHABLENDENABLE_OLD          = 27,   /* TRUE to enable alpha blending */
  D3DRS_FOGCOLOR_OLD                  = 34,   /* D3DCOLOR */
  D3DRS_STENCILENABLE_OLD             = 52,   /* BOOL enable/disable stenciling */
  D3DRS_STENCILPASS_OLD               = 55,   /* D3DSTENCILOP to do if both stencil and Z tests pass */
  D3DRS_STENCILFUNC_OLD               = 56,   /* D3DCMPFUNC fn.  Stencil Test passes if ((ref & mask) stencilfn (stencil & mask)) is true */
  D3DRS_STENCILREF_OLD                = 57,   /* Reference value used in stencil test */
  D3DRS_STENCILMASK_OLD               = 58,   /* Mask value used in stencil test */
  D3DRS_STENCILWRITEMASK_OLD          = 59,   /* Write mask applied to values written to stencil buffer */
  D3DRS_CLIPPING_OLD                  = 136,
  D3DRS_COLORWRITEENABLE_OLD          = 168,  // per-channel write enable
  D3DRS_SLOPESCALEDEPTHBIAS_OLD       = 175,
  D3DRS_TWOSIDEDSTENCILMODE_OLD       = 185,   /* BOOL enable/disable 2 sided stenciling */
  D3DRS_CCW_STENCILPASS_OLD           = 188,   /* D3DSTENCILOP to do if both ccw stencil and Z tests pass */
  D3DRS_CCW_STENCILFUNC_OLD           = 189,   /* D3DCMPFUNC fn.  ccw Stencil Test passes if ((ref & mask) stencilfn (stencil & mask)) is true */
  D3DRS_SRGBWRITEENABLE_OLD           = 194,   /* Enable rendertarget writes to be DE-linearized to SRGB (for formats that expose D3DUSAGE_QUERY_SRGBWRITE) */
  D3DRS_DEPTHBIAS_OLD                 = 195,
};
enum SamplerStatesFromDX9
{
  D3DSAMP_ADDRESSU_OLD       = 1,  /* D3DTEXTUREADDRESS_OLD for U coordinate */
  D3DSAMP_ADDRESSV_OLD       = 2,  /* D3DTEXTUREADDRESS_OLD for V coordinate */
  D3DSAMP_MAGFILTER_OLD      = 5,  /* D3DTEXTUREFILTER filter to use for magnification */
  D3DSAMP_MINFILTER_OLD      = 6,  /* D3DTEXTUREFILTER filter to use for minification */
  D3DSAMP_MIPFILTER_OLD      = 7,  /* D3DTEXTUREFILTER filter to use between mipmaps during minification */
  D3DSAMP_MAXANISOTROPY_OLD  = 10, /* DWORD maximum anisotropy */
  D3DSAMP_SRGBTEXTURE_OLD    = 11, /* Default = 0 (which means Gamma 1.0*/
};
enum TextureAdressFromDX9
{
  D3DTADDRESS_WRAP_OLD            = 1,
  D3DTADDRESS_CLAMP_OLD           = 3,
};
#define D3DTEXTUREADDRESS_OLD TextureAdressFromDX9
enum TextureStageStateFromDX9
{
//   D3DTSS_COLOROP_OLD        =  1, /* D3DTEXTUREOP - per-stage blending controls for color channels */
//   D3DTSS_ALPHAOP_OLD        =  4, /* D3DTEXTUREOP - per-stage blending controls for alpha channel */
//   D3DTSS_TEXCOORDINDEX_OLD  = 11, /* identifies which set of texture coordinates index this texture */
};
enum FilterTypeFromDX9
{
  D3DTEXF_NONE_OLD            = 0,    // filtering disabled (valid for mip filter only)
  D3DTEXF_POINT_OLD           = 1,    // nearest
  D3DTEXF_LINEAR_OLD          = 2,    // linear interpolation
  D3DTEXF_ANISOTROPIC_OLD     = 3,    // anisotropic
};
enum StencilOpFromDX9
{
  D3DSTENCILOP_KEEP_OLD           = 1,
  D3DSTENCILOP_REPLACE_OLD        = 3,
  D3DSTENCILOP_INCR_OLD           = 7,
  D3DSTENCILOP_DECR_OLD           = 8,
};
enum CmpFuncFromDX9
{
  D3DCMP_LESSEQUAL_OLD            = 4,
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
  D3DBLEND_SRCALPHA_OLD           = 5,
  D3DBLEND_INVSRCALPHA_OLD        = 6,
};
enum ZBTypeFromDX9
{
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
  D3DPT_TRIANGLEFAN_OLD,
  D3DPT_TRIANGLELIST_OLD,
};


// TODO: find D3DPERF_counterparts for DX10
#define D3DPERF_SetMarker_OLD(num,text)
#define D3DPERF_BeginEvent_OLD(color,name) (0)
#define D3DPERF_EndEvent_OLD() (0)
//#define DXGetErrorString8(err) ""

#define D3DCOLORWRITEENABLE_RED     (1L<<0)
#define D3DCOLORWRITEENABLE_GREEN   (1L<<1)
#define D3DCOLORWRITEENABLE_BLUE    (1L<<2)
#define D3DCOLORWRITEENABLE_ALPHA   (1L<<3)

//!}

#undef SearchPath
#undef DrawText
#undef GetObject
#undef GetMessage
#undef min
#undef max
#define min(a,b) (((a)<(b))?(a):(b))
#define max(a,b) (((a)>(b))?(a):(b))

#if _ENABLE_PERFLOG && _PROFILE
  #define PERF_PROF 1
#else
  #define PERF_PROF 0
#endif

#include <El/Common/perfProf.hpp>

#if PERF_PROF

  //#define PROFILE_DX_SCOPE(name) PROFILE_SCOPE(d3d)
  //#define PROFILE_DX_SCOPE_DETAIL(name) PROFILE_SCOPE_DETAIL(d3d)
  #define PROFILE_DX_SCOPE(name) PROFILE_SCOPE_EX(name,d3d)
  #define PROFILE_DX_SCOPE_DETAIL(name) PROFILE_SCOPE_DETAIL_EX(name,d3d)

#else
  #define PROFILE_DX_SCOPE(name)
  #define PROFILE_DX_SCOPE_DETAIL(name)
#endif


/// used for VRAM/AGP checks
#include <ddraw.h>

#endif