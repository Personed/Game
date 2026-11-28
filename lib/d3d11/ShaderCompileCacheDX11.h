#ifdef _MSC_VER
#pragma once
#endif

#ifndef _ShaderCompileCache_HPP
#define _ShaderCompileCache_HPP

#if _ENABLE_COMPILED_SHADER_CACHE && !defined _SERVER && _ENABLE_DX11

#include <d3d11.h>
#include <D3DX11.h>
#include "../d3dCommon/d3dCommon.hpp"
#include "Es/Strings/rString.hpp"

namespace DX11
{

  struct D3DXCompileShader_Result
  {
    ComRef<ID3D10Blob> shader;
    ComRef<ID3D10Blob> errorMsgs;
  };


HRESULT WINAPI D3DX11CompileShader_Cached(
  const LazyLoadFile &src,
  LPCSTR pFileName,
  CONST D3D10_SHADER_MACRO *pDefines,
  LPD3D10INCLUDE pInclude,
  LPCSTR pFunctionName,
  LPCSTR pProfile,
  UINT Flags1,
  UINT Flags2,
  ID3DX11ThreadPump *pPump,
  HRESULT *pHResult,
  D3DXCompileShader_Result &res
  );

class ShaderCompileCache
{
  static void ShaderCacheInit(const Array<RString> &sources, const char *sectionName, RStringB customPath, bool forceCreate);
  static void ShaderCacheDone();
public:
#if _ENABLE_CHEATS
  /// set this flag to true before compiling specific shader - this shader will be always compiled, even if it is in cache
  static bool ForceCompileShader;
#endif

  ShaderCompileCache(const Array<RString> &sources, const char *sectionName, RStringB customPath = RStringB(), bool forceCreate = false)
  {
    ShaderCacheInit(sources, sectionName, customPath, forceCreate);
  }

  ShaderCompileCache(RString fpshader, const char *sectionName, RStringB customPath = RStringB(), bool forceCreate = false)
  {
    ShaderCacheInit(Array<RString>(fpshader), sectionName, customPath, forceCreate);
  }

  ~ShaderCompileCache()
  {
    ShaderCacheDone();
  }
};

} //namespace DX11
#endif //_ENABLE_COMPILED_SHADER_CACHE && !defined _SERVER && _ENABLE_DX11
#endif //_ShaderCompileCache_HPP