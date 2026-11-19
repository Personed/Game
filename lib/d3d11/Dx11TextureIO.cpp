#include "../wpch.hpp"

#if _ENABLE_DX11

#include <Es/Memory/normalNew.hpp>
#include "Dx11TextureIO.hpp"
#include <d3d11.h>
#include <wincodec.h>
#include <cstring>
#include <limits>
#include <new>

#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

// TGA has no signature, so only treat standard TGA image types as candidates.
static HRESULT DecodeTGA32(
  const BYTE *data,
  SIZE_T dataSize,
  BYTE **pixels,
  UINT *outWidth,
  UINT *outHeight)
{
  if (dataSize < 18)
    return S_FALSE;

  const BYTE imageType = data[2];
  if (imageType != 1 && imageType != 2 && imageType != 3 &&
      imageType != 9 && imageType != 10 && imageType != 11)
    return S_FALSE;

  // shortcut: supports only the shipped uncompressed 32-bit top-left TGA; add
  // other TGA encodings here only when an asset requires them.
  if (data[1] != 0 || imageType != 2 || data[16] != 32 || data[17] != 0x28)
    return E_NOTIMPL;

  const UINT width = static_cast<UINT>(data[12]) | (static_cast<UINT>(data[13]) << 8);
  const UINT height = static_cast<UINT>(data[14]) | (static_cast<UINT>(data[15]) << 8);
  if (width == 0 || height == 0)
    return E_INVALIDARG;

  const SIZE_T maxSize = (std::numeric_limits<SIZE_T>::max)();
  if (width > maxSize / height)
    return E_INVALIDARG;
  const SIZE_T pixelCount = static_cast<SIZE_T>(width) * height;
  if (pixelCount > maxSize / 4)
    return E_INVALIDARG;

  const SIZE_T pixelOffset = 18 + data[0];
  const SIZE_T imageSize = pixelCount * 4;
  if (pixelOffset > dataSize || imageSize > dataSize - pixelOffset)
    return E_INVALIDARG;

  BYTE *rgba = new (std::nothrow) BYTE[imageSize];
  if (!rgba)
    return E_OUTOFMEMORY;

  const BYTE *bgra = data + pixelOffset;
  for (SIZE_T i = 0; i < pixelCount; ++i)
  {
    rgba[i * 4] = bgra[i * 4 + 2];
    rgba[i * 4 + 1] = bgra[i * 4 + 1];
    rgba[i * 4 + 2] = bgra[i * 4];
    rgba[i * 4 + 3] = bgra[i * 4 + 3];
  }

  *pixels = rgba;
  *outWidth = width;
  *outHeight = height;
  return S_OK;
}

// ---------------------------------------------------------------------------
// LoadTextureFromMemoryWIC
// Uses WIC to decode an image (PNG, BMP, JPEG, etc.) and upload it into a
// D3D11 texture. This replaces the legacy D3DX11CreateTextureFromMemory call
// so we no longer need d3dx11_41.dll at runtime.
// ---------------------------------------------------------------------------
HRESULT LoadTextureFromMemoryWIC(
  ID3D11Device *device,
  const void *data,
  SIZE_T dataSize,
  DXGI_FORMAT format,
  D3D11_USAGE usage,
  UINT cpuAccessFlags,
  UINT mipLevels,
  UINT bindFlags,
  ID3D11Resource **ppTexture,
  UINT *outWidth,
  UINT *outHeight)
{
  if (!device || !data || dataSize == 0 || !ppTexture)
    return E_INVALIDARG;

  *ppTexture = nullptr;
  if (dataSize > MAXDWORD)
    return E_INVALIDARG;

  // Ensure COM is initialized for this thread (WIC requires it).
  // COINIT_MULTITHREADED is compatible with the engine's threading model.
  // S_FALSE means COM was already initialized — we still match the balance
  // with CoUninitialize at the end.
  HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  bool comInitialized = SUCCEEDED(hrCom);

  // Create WIC factory
  IWICImagingFactory *pFactory = nullptr;
  HRESULT hr = CoCreateInstance(
    CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
    IID_PPV_ARGS(&pFactory));

  // Create stream from memory
  IWICStream *pStream = nullptr;
  if (SUCCEEDED(hr))
    hr = pFactory->CreateStream(&pStream);
  if (SUCCEEDED(hr))
    hr = pStream->InitializeFromMemory(
      const_cast<BYTE *>(static_cast<const BYTE *>(data)),
      static_cast<DWORD>(dataSize));

  // Create decoder
  IWICBitmapDecoder *pDecoder = nullptr;
  if (SUCCEEDED(hr))
    hr = pFactory->CreateDecoderFromStream(
      pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder);

  // Get first frame
  IWICBitmapFrameDecode *pFrame = nullptr;
  if (SUCCEEDED(hr))
    hr = pDecoder->GetFrame(0, &pFrame);

  // Convert to target pixel format (RGBA8)
  IWICFormatConverter *pConverter = nullptr;
  if (SUCCEEDED(hr))
    hr = pFactory->CreateFormatConverter(&pConverter);
  if (SUCCEEDED(hr))
    hr = pConverter->Initialize(
      pFrame, GUID_WICPixelFormat32bppRGBA,
      WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

  UINT width = 0, height = 0;
  if (SUCCEEDED(hr))
    hr = pConverter->GetSize(&width, &height);

  // Copy pixels to buffer
  UINT rowPitch = 0;
  UINT imageSize = 0;
  BYTE *pixels = nullptr;
  if (SUCCEEDED(hr))
  {
    if (width == 0 || height == 0 || width > static_cast<UINT>(-1) / 4 ||
        height > static_cast<UINT>(-1) / (width * 4))
      hr = E_INVALIDARG;
    else
    {
      rowPitch = width * 4;
      imageSize = rowPitch * height;
      pixels = new (std::nothrow) BYTE[imageSize];
      if (!pixels)
        hr = E_OUTOFMEMORY;
    }
  }
  if (SUCCEEDED(hr))
    hr = pConverter->CopyPixels(nullptr, rowPitch, imageSize, pixels);

  // WIC does not decode TGA on all supported Windows versions.
  if (FAILED(hr))
  {
    const HRESULT wicError = hr;
    delete[] pixels;
    pixels = nullptr;
    const HRESULT tgaResult = DecodeTGA32(
      static_cast<const BYTE *>(data), dataSize, &pixels, &width, &height);
    if (tgaResult != S_FALSE)
    {
      hr = tgaResult;
      if (SUCCEEDED(hr))
        rowPitch = width * 4;
    }
    else
      hr = wicError;
  }

  // Create the D3D11 texture
  if (SUCCEEDED(hr))
  {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mipLevels;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = usage;
    desc.BindFlags = bindFlags;
    desc.CPUAccessFlags = cpuAccessFlags;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = pixels;
    initData.SysMemPitch = rowPitch;

    ID3D11Texture2D *pTex2D = nullptr;
    hr = device->CreateTexture2D(&desc, &initData, &pTex2D);
    if (SUCCEEDED(hr))
      *ppTexture = pTex2D;
  }

  if (outWidth) *outWidth = width;
  if (outHeight) *outHeight = height;

  delete[] pixels;
  if (pConverter) pConverter->Release();
  if (pFrame) pFrame->Release();
  if (pDecoder) pDecoder->Release();
  if (pStream) pStream->Release();
  if (pFactory) pFactory->Release();

  if (comInitialized) CoUninitialize();

  return hr;
}

// ---------------------------------------------------------------------------
// SaveTextureToDDS
// Debug-only utility that saves a D3D11 texture to a raw DDS file.
// Replaces D3DX11SaveTextureToFile which requires d3dx11_41.dll.
// This is used only in _ENABLE_REPORT builds.
// ---------------------------------------------------------------------------
HRESULT SaveTextureToDDS(
  ID3D11DeviceContext *context,
  ID3D11Resource *resource,
  const char *filename)
{
  if (!context || !resource || !filename)
    return E_INVALIDARG;

  // Get the texture description
  D3D11_RESOURCE_DIMENSION dim;
  resource->GetType(&dim);
  if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
    return E_NOTIMPL;

  ID3D11Texture2D *srcTex = nullptr;
  HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void **)&srcTex);
  if (FAILED(hr)) return hr;

  D3D11_TEXTURE2D_DESC desc;
  srcTex->GetDesc(&desc);

  // Create a staging copy
  ID3D11Device *device = nullptr;
  context->GetDevice(&device);

  D3D11_TEXTURE2D_DESC stagingDesc = desc;
  stagingDesc.Usage = D3D11_USAGE_STAGING;
  stagingDesc.BindFlags = 0;
  stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  stagingDesc.MiscFlags = 0;

  ID3D11Texture2D *stagingTex = nullptr;
  hr = device->CreateTexture2D(&stagingDesc, nullptr, &stagingTex);
  if (SUCCEEDED(hr))
    context->CopyResource(stagingTex, srcTex);

  // Map and write DDS
  if (SUCCEEDED(hr))
  {
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = context->Map(stagingTex, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr))
    {
      FILE *f = nullptr;
      fopen_s(&f, filename, "wb");
      if (f)
      {
        // DDS magic
        DWORD magic = 0x20534444; // "DDS "
        fwrite(&magic, 4, 1, f);

        // Minimal DDS header (124 bytes)
        BYTE header[124] = {};
        DWORD *h = (DWORD *)header;
        h[0] = 124;              // dwSize
        h[1] = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000; // flags: CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT
        h[2] = desc.Height;      // dwHeight
        h[3] = desc.Width;       // dwWidth
        h[4] = mapped.RowPitch;  // dwPitchOrLinearSize

        // Pixel format at offset 76 (19 DWORDs in)
        DWORD *pf = h + 19;
        pf[0] = 32;              // dwSize
        pf[1] = 0x41;            // dwFlags: RGB|ALPHAPIXELS
        pf[3] = 32;              // dwRGBBitCount
        pf[4] = 0x000000FF;      // R mask
        pf[5] = 0x0000FF00;      // G mask
        pf[6] = 0x00FF0000;      // B mask
        pf[7] = 0xFF000000;      // A mask

        // Caps at offset 108 (27 DWORDs in)
        h[27] = 0x1000;          // TEXTURE

        fwrite(header, 124, 1, f);

        // Write pixel rows
        const BYTE *src = static_cast<const BYTE *>(mapped.pData);
        UINT rowSize = desc.Width * 4;
        for (UINT row = 0; row < desc.Height; ++row)
        {
          fwrite(src + row * mapped.RowPitch, rowSize, 1, f);
        }
        fclose(f);
      }
      context->Unmap(stagingTex, 0);
    }
  }

  if (stagingTex) stagingTex->Release();
  if (device) device->Release();
  if (srcTex) srcTex->Release();

  return hr;
}

#include <Es/Memory/debugNew.hpp>

#endif // _ENABLE_DX11
