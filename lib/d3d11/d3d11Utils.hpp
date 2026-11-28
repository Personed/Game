#ifdef _MSC_VER
#pragma once
#endif

#ifndef __D3D11UTILS_HPP
#define __D3D11UTILS_HPP

/**
Various utilities for DX11 engine
*/

#if _ENABLE_DX11

#include <d3d11.h>

namespace DX11
{
  /** returns SRGB format corresponding to given format */
  DXGI_FORMAT GetCorrespondingSRGBFormat(DXGI_FORMAT format);
  /** returns format corresponding to given SRGB format */
  DXGI_FORMAT GetCorrespondingFormatFromSRGB(DXGI_FORMAT format);
  /** returns typeless format corresponding to given format */
  DXGI_FORMAT GetCorrespondingTypelessFormat(DXGI_FORMAT format);

}//namespace DX11


#endif //_ENABLE_DX11
#endif //__D3D11UTILS_HPP