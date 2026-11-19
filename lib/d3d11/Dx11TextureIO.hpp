#pragma once
// Dx11TextureIO.hpp - Windows SDK replacements for D3DX11 texture I/O
//
// Provides LoadTextureFromMemoryWIC() as a drop-in replacement for
// D3DX11CreateTextureFromMemory, using WIC (Windows Imaging Component).
// This eliminates the runtime dependency on d3dx11_41.dll for texture loading.

#include <d3d11.h>

// Load an image from memory (PNG, BMP, JPEG, etc.) into a D3D11 staging texture.
// Returns the created texture in ppTexture. The caller is responsible for releasing it.
// If outWidth/outHeight are non-null, they receive the image dimensions.
HRESULT LoadTextureFromMemoryWIC(
  ID3D11Device *device,
  const void *data,
  SIZE_T dataSize,
  DXGI_FORMAT format,       // e.g. DXGI_FORMAT_R8G8B8A8_UNORM
  D3D11_USAGE usage,        // e.g. D3D11_USAGE_STAGING
  UINT cpuAccessFlags,      // e.g. D3D11_CPU_ACCESS_READ
  UINT mipLevels,           // 1 for no mip chain
  UINT bindFlags,           // 0 for staging
  ID3D11Resource **ppTexture,
  UINT *outWidth = nullptr,
  UINT *outHeight = nullptr);

// Save a D3D11 texture to a DDS file.
// A debug/report-only utility that replaces D3DX11SaveTextureToFile.
HRESULT SaveTextureToDDS(
  ID3D11DeviceContext *context,
  ID3D11Resource *resource,
  const char *filename);
