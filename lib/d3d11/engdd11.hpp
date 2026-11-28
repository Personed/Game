#ifdef _MSC_VER
#pragma once
#endif

#ifndef __ENGDD11_HPP
#define __ENGDD11_HPP

#if _ENABLE_DX11
#if !_DISABLE_GUI

#include "../engine.hpp"
#include <d3d11.h>

#include <Es/Memory/normalNew.hpp>
#include <d3dx11.h>
#include <D3DX10math.h>
#include <Es/Memory/debugNew.hpp>

#include "../engineDll.hpp"
#include "txtd3d11.hpp"
#include "d3d11Defs.hpp"
#include <El/Multicore/circularQueue.hpp>
#include "../d3dCommon/d3dCommon.hpp"
#include <El/ParamArchive/serializeClass.hpp>
#include <Es/Strings/bString.hpp>
#ifndef _XBOX
/// used for VRAM/AGP checks
#include <ddraw.h>
#endif

#include "El/ActiveObject/activeObject.hpp"
#include "Es/Common/delegate.hpp"

#include "StateLists.hpp"
#include "ShaderConstants.h"

#include "ShaderSources_4_0/common.h"

#if _ARMA3_SIMUL_WEATHER
#include "../SimulWeather/SimulCommonDX11.hpp"
#endif

#include "../object.hpp"  // ProtectedVisualState

class FaceArray;
class FaceData;

#ifdef _XBOX
typedef D3DXMATRIXA16 PASSMATRIXTYPE;
#else
typedef D3DXMATRIX PASSMATRIXTYPE;
#endif

namespace DX11
{

static inline void FlushGPU(ID3D11Device *dev)
  {
#if 0 //def _XBOX
#if 1
    dev->BlockUntilIdle();
#elif 0
    DWORD fence = dev->InsertFence();
    // perform our own waiting, so that we can debug when we detect something gone wrong
    dev->BlockOnFence();
#endif
    DoAssert(GFileServer->CheckIntegrity());
#endif
  }


#define GEngineDD11 static_cast<DX11::EngineDD11 *>(GEngine)
 
struct SSupportedFormats
{
  DXGI_FORMAT _format;
  int _bpp;
};

//! Shadow buffer size
#ifdef _XBOX
const int ShadowBufferSize = 512;
const int ShadowBufferSizeHQ = 512;
#else
const int ShadowBufferSize = 1024;
const int ShadowBufferSizeHQ = 2048;
#endif

/* used for post process effects - store additional info about post effect */
struct PostProcessHndl: public SerializeClass
{
  // effect identifier
  int _hndl;
  PostEffectType _type;
  Ref<RefCount> _pp;

  PostProcessHndl() {}
  PostProcessHndl(int hndl, PostEffectType type, const Ref<RefCount> &pp): _hndl(hndl), _type(type), _pp(pp) {}

  bool operator == (const PostProcessHndl &other) const
  {
    return (_hndl == other._hndl && _type == other._type && _pp == other._pp);
  }

  bool operator != (const PostProcessHndl &other) const
  {
    return !(*this==other);
  }

  LSError Serialize(ParamArchive &ar);
};

// invalid post process handle
static PostProcessHndl InvalidPPHndl(-1, PENone, 0);

} // namespace

// must be outside namespace
template <>
struct FindArrayKeyTraits<DX11::PostProcessHndl>
{
  typedef const int &KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b) { return a == b; }
  /// get a key from an item
  static KeyType GetKey(const DX11::PostProcessHndl &a) { return a._hndl; }
};

namespace DX11 
{

class IPPHndlFunc
{
public:
  virtual bool operator () (PostProcessHndl &hndl) = 0;
};

//@{ default set post process priorities
const static int PPRadBlurPriority = 100;
const static int PPChrAbberPriority = 200;
const static int PPWetDistortPriority = 300;
const static int PPDynBlurPriority = 400;
const static int PPGlowPriority = 1000;
const static int PPColorPriority = 1500;
const static int PPFilmGrainPriority = 2000;
const static int PPSSAOPriority = 90;
const static int PPColorInvPriority = 2500;
const static int ppRain3DPriority = 500;
//}@

/// provide a means for D3DDeviceAutoLocked to check if we are recording a CB
bool NotInRecording();

void Device_UnsetAllDiscardableResources(MyD3DDevice& dev);

#if _ARMA3_VERTEX_BUFFER_EX
/** 
Vertex buffer with additional information about number of primitives we want to actually draw
This is useful when actual number of primitives is decided after the buffer creation in visualize thread
(so we don't know exact number when DrawPrimitive* is called)
*/
class VertexBuffer11Ex 
{
public:
  static const int UndefinedValue = -1; 

  VertexBuffer11Ex(): _numVerticesToDraw(UndefinedValue) {}
  VertexBuffer11Ex(const ComRef<ID3D11Buffer>& val, int numVerticesToDraw = UndefinedValue)
    :_buffer(val), _numVerticesToDraw(numVerticesToDraw)
  {}

  int GetNumVerticesToDraw() const {return _numVerticesToDraw;}
  void SetNumVerticesToDraw(int num) {_numVerticesToDraw = num;}
  bool IsValid() {return _numVerticesToDraw != UndefinedValue && _numVerticesToDraw > 0;}

  const ComRef<ID3D11Buffer>& GetBuffer() const {return _buffer;}
  ComRef<ID3D11Buffer>& GetBuffer() {return _buffer;}

private:
  /// vertex buffer
  ComRef<ID3D11Buffer> _buffer;
  /** actual number of vertices to draw
  if this is UndefinedValue, then number of vertices from Draw... call is used
  */
  int _numVerticesToDraw;  
};

#else //#if _ARMA3_VERTEX_BUFFER_EX
typedef ComRef<IDirect3DVertexBuffer9> VertexBuffer9Ex;
#endif


#define PERFORM_DX11_LOCKING 1

#if PERFORM_DX11_LOCKING

extern CriticalSection DevCriticalSection;

class AutoLockDX11
{
public:
  AutoLockDX11(CriticalSection& cs) : _cs(cs) 
  {
    _cs.Lock();
  }
  ~AutoLockDX11() 
  {
    _cs.Unlock();
  }
private:
  CriticalSection& _cs;
};

#define LOCK_DX11 AutoLockDX11 lock(DevCriticalSection)

#else

#define LOCK_DX11
#endif

class EngineDD11; //forward declaration

/// isolated the part of the interface we use
template <bool allowCB>
class D3DDeviceAutoLocked
{
protected:
  /// native device
  ComRef<ID3D11Device> _dev;
  /// native device context
  ComRef<ID3D11DeviceContext> _devContext;
  /// native swap chain
  ComRef<IDXGISwapChain> _swapChain;

#if _ARMA3_VERTEX_BUFFER_EX
  /// last number of vertices set in SetStreamSource
  int _lastStreamSourceNumVertices;
#endif

public:
  void operator += (ID3D11Device* dev){_dev += dev;}
  void operator << (ID3D11Device* dev){_dev << dev;}
  void operator = (const ComRef<ID3D11Device> &dev){_dev = dev;}

  void operator += (ID3D11DeviceContext *devContext){_devContext += devContext;}
  void operator << (ID3D11DeviceContext *devContext){_devContext << devContext;}
  void operator = (const ComRef<ID3D11DeviceContext> &devContext){_devContext = devContext;}

  void operator += (IDXGISwapChain *swapChain){_swapChain += swapChain;}
  void operator << (IDXGISwapChain *swapChain){_swapChain << swapChain;}
  void operator = (const ComRef<IDXGISwapChain> &swapChain){_swapChain = swapChain;}

  __forceinline void DetachDevice()
  {
    Free();
  }

  __forceinline void AttachDevice(const ComRef<ID3D11Device> &dev, const ComRef<ID3D11DeviceContext> &devContext, const ComRef<IDXGISwapChain> &swapChain)
  {
    operator=(dev);
    operator=(devContext);
    operator=(swapChain);
  }

  ID3D11Device* GetDeviceRef() 
  {
    DoAssert(allowCB || NotInRecording());
    return _dev.GetRef();
  }
  const ID3D11Device* GetDeviceRef() const
  {
    DoAssert(allowCB || NotInRecording());
    return _dev.GetRef();
  }
  ID3D11DeviceContext* GetDeviceContextRef() 
  {
    DoAssert(allowCB || NotInRecording());
    return _devContext.GetRef();
  }
  const ID3D11DeviceContext* GetDeviceContextRef() const
  {
    DoAssert(allowCB || NotInRecording());
    return _devContext.GetRef();
  }
  IDXGISwapChain* GetSwapChainRef() 
  {
    DoAssert(allowCB || NotInRecording());
    return _swapChain.GetRef();
  }
  const IDXGISwapChain* GetSwapChainRef() const
  {
    DoAssert(allowCB || NotInRecording());
    return _swapChain.GetRef();
  }

  bool IsNull() const {return _devContext.IsNull() || _dev.IsNull() || _swapChain.IsNull();}
  void Free() 
  {
    _devContext.Free();
    _swapChain.Free();
    _dev.Free();
  }

  __forceinline void IASetIndexBuffer(const Future< ComRef<IDirect3DIndexBuffer9Old> > &pIndexData,  DXGI_FORMAT format,  UINT offset)
  {
    PROFILE_SCOPE_DETAIL_EX(dvIASIB1,dd11);
    DoAssert(allowCB || NotInRecording());
    // get result before locking device (important - deadlock can occur otherwise)
    ID3D11Buffer* buffer = pIndexData.GetResult();
    Assert(buffer);
    if (buffer)
    {
      LOCK_DX11;
      _devContext->IASetIndexBuffer(buffer, format, offset);
    }
  }

  __forceinline void IASetIndexBuffer(
    ID3D11Buffer *pIndexBuffer,
    DXGI_FORMAT Format,
    UINT Offset
)
  {
    PROFILE_SCOPE_DETAIL_EX(dvIASIB2,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    _devContext->IASetIndexBuffer(pIndexBuffer, Format, Offset);
  }

  __forceinline void IASetVertexBuffers(
    UINT startSlot,
    const Future< VertexBuffer11Ex > &vertexBuffer, 
    const UINT stride,
    const UINT offset)
  {
    PROFILE_SCOPE_DETAIL_EX(dvIASVB1,dd11);
    DoAssert(allowCB || NotInRecording());

#if _ARMA3_VERTEX_BUFFER_EX
    // get result before locking device (important - deadlock can occur otherwise)
    IDirect3DVertexBuffer9Old *buffer = vertexBuffer.GetResult().GetBuffer();
    // set number of primitives
    _lastStreamSourceNumVertices = vertexBuffer.GetResult().GetNumVerticesToDraw();
    DoAssert(_lastStreamSourceNumVertices == VertexBuffer11Ex::UndefinedValue || _lastStreamSourceNumVertices >= 1);
#else
    // get result before locking device (important - deadlock can occur otherwise)
   IDirect3DVertexBuffer9Old *buffer = vertexBuffer.GetResult();
#endif

    Assert(buffer);
    if (buffer)
    {
      LOCK_DX11;
      _devContext->IASetVertexBuffers(startSlot, 1, &buffer, &stride, &offset);
    }
  }

  __forceinline void IASetVertexBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer *const *ppVertexBuffers,
    const UINT *pStrides,
    const UINT *pOffsets
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvIASVB2,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    _devContext->IASetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
  }

  __forceinline HRESULT CreateBuffer(
    const D3D11_BUFFER_DESC *pDesc,
    const D3D11_SUBRESOURCE_DATA *pInitialData,
    ID3D11Buffer **ppBuffer
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrBf,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateBuffer(pDesc, pInitialData, ppBuffer);
  }

  __forceinline HRESULT GetBuffer(
    UINT Buffer,
    REFIID riid,
    void **ppSurface)
  {
    PROFILE_SCOPE_DETAIL_EX(dvGtBf,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _swapChain->GetBuffer(Buffer, riid, ppSurface);
  }

  __forceinline void ClearRenderTargetView(
    ID3D11RenderTargetView *pRenderTargetView,
    const Array<float>& color
  )
  {
    PROFILE_SCOPE_DETAIL_EX(dvClrRTV,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    _devContext->ClearRenderTargetView(pRenderTargetView, color.Data());
  }

  __forceinline void ClearDepthStencilView(
    ID3D11DepthStencilView *pDepthStencilView,
    UINT ClearFlags,
    FLOAT Depth,
    UINT8 Stencil
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvClrDSV,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    _devContext->ClearDepthStencilView(pDepthStencilView, ClearFlags, Depth, Stencil);
  }

  __forceinline HRESULT CreatePixelShader(
    const void *pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage *pClassLinkage,
    ID3D11PixelShader **ppPixelShader
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrPS,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreatePixelShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppPixelShader);
  }

  __forceinline HRESULT CreateVertexShader(
    const void *pShaderBytecode,
    SIZE_T BytecodeLength,
    ID3D11ClassLinkage *pClassLinkage,
    ID3D11VertexShader **ppVertexShader
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrVS,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateVertexShader(pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);
  }

  __forceinline HRESULT CreateInputLayout(
    const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs,
    UINT NumElements,
    const void *pShaderBytecodeWithInputSignature,
    SIZE_T BytecodeLength,
    ID3D11InputLayout **ppInputLayout
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrIL,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateInputLayout(pInputElementDescs, NumElements, 
      pShaderBytecodeWithInputSignature, BytecodeLength, ppInputLayout);
  }

  __forceinline HRESULT Present(
    UINT SyncInterval,
    UINT Flags
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvPrsnt,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _swapChain->Present(SyncInterval, Flags);
  }

  __forceinline HRESULT Map(
    ID3D11Resource *pResource,
    UINT Subresource,
    D3D11_MAP MapType,
    UINT MapFlags,
    D3D11_MAPPED_SUBRESOURCE *pMappedResource
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvMap,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->Map(pResource, Subresource, MapType, MapFlags, pMappedResource);
  }

  __forceinline void Unmap(
    ID3D11Resource *pResource,
    UINT Subresource
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvUnmap,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->Unmap(pResource, Subresource);
  }

  __forceinline HRESULT CreateTexture2D(
    const D3D11_TEXTURE2D_DESC *pDesc,
    const D3D11_SUBRESOURCE_DATA *pInitialData,
    ID3D11Texture2D **ppTexture2D
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrtTx2D,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
  }

#if _ARMA3_SIMUL_WEATHER
  __forceinline HRESULT CreateTexture3D(
    const D3D11_TEXTURE3D_DESC *pDesc,
    const D3D11_SUBRESOURCE_DATA *pInitialData,
    ID3D11Texture3D **ppTexture3D
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrtTx3D,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateTexture3D(pDesc, pInitialData, ppTexture3D);
  }
#endif

  __forceinline HRESULT CreateRenderTargetView(
    ID3D11Resource *pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
    ID3D11RenderTargetView **ppRTView
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrtRTV,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateRenderTargetView(pResource, pDesc, ppRTView);
  }

  __forceinline HRESULT CreateDepthStencilView(
    ID3D11Resource *pResource,
    const D3D11_DEPTH_STENCIL_VIEW_DESC *pDesc,
    ID3D11DepthStencilView **ppDepthStencilView
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrtDSV,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateDepthStencilView(pResource, pDesc, ppDepthStencilView);
  }

  __forceinline HRESULT CreateShaderResourceView(
    ID3D11Resource *pResource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC *pDesc,
    ID3D11ShaderResourceView **ppSRView
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrtSRV,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateShaderResourceView(pResource, pDesc, ppSRView);
  }


  __forceinline void OMSetRenderTargets(
    ID3D11RenderTargetView *pRenderTargetView,
    ID3D11DepthStencilView *pDepthStencilView
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvOMStRT,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    ID3D11RenderTargetView *views[] = {pRenderTargetView};
    return _devContext->OMSetRenderTargets(1, views, pDepthStencilView);
  }

  __forceinline void RSSetViewports(
    const D3D11_VIEWPORT *viewport
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvRSStVw,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->RSSetViewports(1, viewport);
  }

  __forceinline void DrawIndexed(
    UINT IndexCount,
    UINT StartIndexLocation,
    INT BaseVertexLocation
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvDrwIdx,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
  }

  __forceinline void Flush()
  {
    PROFILE_SCOPE_DETAIL_EX(dvFlsh,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->Flush();
  }

  __forceinline void Draw(
    UINT VertexCount,
    UINT StartVertexLocation
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvDrw,dd11);
    DoAssert(allowCB || NotInRecording());
#if _ARMA3_VERTEX_BUFFER_EX
    if (_lastStreamSourceNumVertices != VertexBuffer11Ex::UndefinedValue)
    {
      // override primitive count
      DoAssert(_lastStreamSourceNumVertices >= 1);
      if (_lastStreamSourceNumVertices >= 1)
      {
        VertexCount = _lastStreamSourceNumVertices;
      }
      _lastStreamSourceNumVertices = VertexBuffer11Ex::UndefinedValue;
    }
#endif
    LOCK_DX11;
    return _devContext->Draw(VertexCount, StartVertexLocation);
  }


  __forceinline void IASetPrimitiveTopology(
    D3D11_PRIMITIVE_TOPOLOGY Topology
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvIAStPrTp,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->IASetPrimitiveTopology(Topology);
  }

  __forceinline void CopyResource(
    ID3D11Resource *pDstResource,
    ID3D11Resource *pSrcResource
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCpyRes,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->CopyResource(pDstResource, pSrcResource);
  }

  __forceinline void CopySubresourceRegion(
    ID3D11Resource *pDstResource,
    UINT DstSubresource,
    UINT DstX,
    UINT DstY,
    UINT DstZ,
    ID3D11Resource *pSrcResource,
    UINT SrcSubresource,
    const D3D11_BOX *pSrcBox
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCpySbResR,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->CopySubresourceRegion(pDstResource, DstSubresource,
      DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
  }

  __forceinline void ResolveSubresource(
    ID3D11Resource *pDstResource,
    UINT DstSubresource,
    ID3D11Resource *pSrcResource,
    UINT SrcSubresource,
    DXGI_FORMAT Format
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvRslvSb,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->ResolveSubresource(pDstResource, DstSubresource,
      pSrcResource, SrcSubresource, Format);
  }


  __forceinline void PSSetShaderResources(
    UINT StartSlot,
    UINT NumViews,
    const Array<TextureHandle11 *>& views
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvPSStSR,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    Assert(NumViews == views.Size());
    return _devContext->PSSetShaderResources(StartSlot, NumViews, views.Data());
  }

  __forceinline void PSSetShader(
    ID3D11PixelShader *pPixelShader
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvPSStShdr,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->PSSetShader(pPixelShader, 0, 0);
  }

  __forceinline void PSGetShader(
    ID3D11PixelShader **ppPixelShader,
    ID3D11ClassInstance **ppClassInstances,
    UINT *pNumClassInstances
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvPSGtShdr,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->PSGetShader(ppPixelShader, ppClassInstances, pNumClassInstances);
  }

  __forceinline void VSSetShader(
    ID3D11VertexShader *pVertexShader
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvVSStShdr,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->VSSetShader(pVertexShader, 0, 0);
  }

  __forceinline void VSGetShader(
    ID3D11VertexShader **ppVertexShader,
    ID3D11ClassInstance **ppClassInstances,
    UINT *pNumClassInstances
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvVSGtShdr,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->VSGetShader(ppVertexShader, ppClassInstances, pNumClassInstances);
  }


  __forceinline void IASetInputLayout(
    ID3D11InputLayout *pInputLayout
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvIAStIL,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->IASetInputLayout(pInputLayout);
  }

  __forceinline void IAGetInputLayout(
    ID3D11InputLayout **ppInputLayout
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvIAGtIL,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->IAGetInputLayout(ppInputLayout);
  }

  __forceinline void IAGetVertexBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer **ppVertexBuffers,
    UINT *pStrides,
    UINT *pOffsets
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvIAGtVB,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->IAGetVertexBuffers(StartSlot, NumBuffers, ppVertexBuffers, pStrides, pOffsets);
  }

  __forceinline void RSGetViewports(
    UINT *pNumViewports,
    D3D11_VIEWPORT *pViewports
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvRSGtVw,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->RSGetViewports(pNumViewports, pViewports);
  }

  __forceinline HRESULT CreateDepthStencilState(
    const D3D11_DEPTH_STENCIL_DESC *pDepthStencilDesc,
    ID3D11DepthStencilState **ppDepthStencilState
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrtDSS,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateDepthStencilState(pDepthStencilDesc, ppDepthStencilState);
  }

  __forceinline void OMSetDepthStencilState(
    ID3D11DepthStencilState *pDepthStencilState,
    UINT StencilRef
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvStDSS,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->OMSetDepthStencilState(pDepthStencilState, StencilRef);
  }

  __forceinline HRESULT CreateBlendState(
    const D3D11_BLEND_DESC *pBlendStateDesc,
    ID3D11BlendState **ppBlendState
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrtBlndSt,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateBlendState(pBlendStateDesc, ppBlendState);
  }

  __forceinline void OMSetBlendState(
    ID3D11BlendState *pBlendState,
    const Array<float>& blendFactor,
    UINT SampleMask
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvStBlndSt,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->OMSetBlendState(pBlendState, blendFactor.Data(), SampleMask);
  }

  __forceinline HRESULT CreateRasterizerState(
    const D3D11_RASTERIZER_DESC *pRasterizerDesc,
    ID3D11RasterizerState **ppRasterizerState
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrtRstSt,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateRasterizerState(pRasterizerDesc, ppRasterizerState);
  }

  __forceinline void RSSetState(
    ID3D11RasterizerState *pRasterizerState
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvRSStSt,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->RSSetState(pRasterizerState);
  }

  __forceinline HRESULT CreateSamplerState(
    const D3D11_SAMPLER_DESC *pSamplerDesc,
    ID3D11SamplerState **ppSamplerState
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrtSmplSt,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateSamplerState(pSamplerDesc, ppSamplerState);
  }

  __forceinline void PSSetSamplers(
    UINT StartSlot,
    UINT NumSamplers,
    const Array<ID3D11SamplerState *>& samplers
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvPSStSmpl,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    Assert(NumSamplers == samplers.Size());
    return _devContext->PSSetSamplers(StartSlot,NumSamplers,samplers.Data());
  }

  __forceinline void VSSetConstantBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    const Array<ID3D11Buffer *>& constantBuffers
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvVSStCB,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    Assert(constantBuffers.Size() == NumBuffers);
    return _devContext->VSSetConstantBuffers(StartSlot,NumBuffers,constantBuffers.Data());
  }

 __forceinline void PSSetConstantBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    const Array<ID3D11Buffer *>& constantBuffers
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvPSStCB,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    Assert(constantBuffers.Size() == NumBuffers);
    return _devContext->PSSetConstantBuffers(StartSlot,NumBuffers,constantBuffers.Data());
  }

 __forceinline void VSSetConstantBuffers(
   UINT StartSlot,
   UINT NumBuffers,
   ID3D11Buffer ** constantBuffers
   )
 {
   PROFILE_SCOPE_DETAIL_EX(dvVSStCB,dd11);
   DoAssert(allowCB || NotInRecording());
   LOCK_DX11;
   return _devContext->VSSetConstantBuffers(StartSlot,NumBuffers,constantBuffers);
 }

 __forceinline void PSSetConstantBuffers(
   UINT StartSlot,
   UINT NumBuffers,
   ID3D11Buffer ** constantBuffers
   )
 {
   PROFILE_SCOPE_DETAIL_EX(dvPSStCB,dd11);
   DoAssert(allowCB || NotInRecording());
   LOCK_DX11;
   return _devContext->PSSetConstantBuffers(StartSlot,NumBuffers,constantBuffers);
 }

  __forceinline HRESULT CreateQuery(
    const D3D11_QUERY_DESC *pQueryDesc,
    ID3D11Query **ppQuery
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvCrtQry,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CreateQuery(pQueryDesc, ppQuery);
  }

  __forceinline void Begin(
    ID3D11Asynchronous *pAsync
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvBgn,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->Begin(pAsync);
  }

  __forceinline void End(
    ID3D11Asynchronous *pAsync
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvEnd,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->End(pAsync);
  }

  __forceinline HRESULT GetData(
    ID3D11Asynchronous *pAsync,
    void *pData,
    UINT DataSize,
    UINT GetDataFlags
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvGtDta,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->GetData(pAsync, pData, DataSize, GetDataFlags);
  }

  __forceinline void UpdateSubresource(
    ID3D11Resource *pDstResource,
    UINT DstSubresource,
    const D3D11_BOX *pDstBox,
    UINT SrcRowPitch,
    UINT SrcDepthPitch,
    const Array<unsigned char>& srcData //this was moved to the last position, because of the way it is stored in CB (truncated - so must be last one)
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvUpdSubRs,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _devContext->UpdateSubresource(pDstResource, DstSubresource, pDstBox, 
      srcData.Data(), SrcRowPitch, SrcDepthPitch);
  }

  __forceinline HRESULT CheckFormatSupport(
    DXGI_FORMAT Format,
    UINT *pFormatSupport
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvChckFS,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CheckFormatSupport(Format, pFormatSupport);
  }

  __forceinline HRESULT CheckMultisampleQualityLevels(
    DXGI_FORMAT Format,
    UINT SampleCount,
    UINT *pNumQualityLevels
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvChckMSQL,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _dev->CheckMultisampleQualityLevels(Format, SampleCount, pNumQualityLevels);
  }

  __forceinline HRESULT SetFullscreenState(
    BOOL Fullscreen,
    IDXGIOutput *pTarget
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvStFSS,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _swapChain->SetFullscreenState(Fullscreen, pTarget);
  }

  __forceinline HRESULT ResizeTarget(
    const DXGI_MODE_DESC *pNewTargetParameters
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvRszTrg,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _swapChain->ResizeTarget(pNewTargetParameters);
  }

  HRESULT GetFullscreenState(
    BOOL *pFullscreen,
    IDXGIOutput **ppTarget
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvGtFSS,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _swapChain->GetFullscreenState(pFullscreen, ppTarget);
  }

  HRESULT ResizeBuffers(
    UINT BufferCount,
    UINT Width,
    UINT Height,
    DXGI_FORMAT NewFormat,
    UINT SwapChainFlags
    )
  {
    PROFILE_SCOPE_DETAIL_EX(dvRszBuf,dd11);
    DoAssert(allowCB || NotInRecording());
    LOCK_DX11;
    return _swapChain->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
  }

#if SAFE_RESOURCE_UNSET
  void UnsetAllDiscardableResources()
  {
    PROFILE_SCOPE_DETAIL_EX(dvUnstDisc,dd11);
    Device_UnsetAllDiscardableResources(_devContext);
  }
#endif

  D3DDeviceAutoLocked()
#if _ARMA3_VERTEX_BUFFER_EX
    :_lastStreamSourceNumVertices(VertexBuffer11Ex::UndefinedValue)
#endif
  {}
};

/** represents render target */ 
class RenderTargetFake;
class RenderTarget
{
public:
  static const RenderTarget RenderTargetNULL;
  RenderTarget() {}
  RenderTarget(const RenderTarget& rt)
    :_resource(rt._resource), _rtView(rt._rtView), _srView(rt._srView) {}
  RenderTarget(const RenderTargetFake& rt);
  RenderTarget(ID3D11Texture2D* resource, ID3D11RenderTargetView* rtView, ID3D11ShaderResourceView* srView)
  {
    _resource += resource;
    _rtView += rtView;
    _srView += srView;
  }

  /// resource of render target
  ComRef<ID3D11Texture2D> _resource;
  /// render target view
  ComRef<ID3D11RenderTargetView> _rtView;
  /// shader resource view - can be empty (not all render targets have this view)
  ComRef<ID3D11ShaderResourceView> _srView;

  void Free()
  {
    _rtView.Free();
    _srView.Free();
    _resource.Free();
  }
  bool IsNull() const
  {
    return _resource.IsNull() && _rtView.IsNull() && _srView.IsNull();
  }
  bool NotNull() const
  {
    return !IsNull();
  }
};

class RenderTargetFake
{
public:
  RenderTargetFake& operator=(const RenderTarget& rt)
  {
    _resource = rt._resource;
    _rtView = rt._rtView;
    _srView = rt._srView;
    return *this;
  }
  RenderTargetFake& operator=(const RenderTargetFake& rt)
  {
    _resource = rt._resource;
    _rtView = rt._rtView;
    _srView = rt._srView;
    return *this;
  }

  /// resource of render target
  ComRefFake<ID3D11Texture2D> _resource;
  /// render target view
  ComRefFake<ID3D11RenderTargetView> _rtView;
  /// shader resource view - can be empty (not all render targets have this view)
  ComRefFake<ID3D11ShaderResourceView> _srView;

  void Free()
  {
    _rtView.Free();
    _srView.Free();
    _resource.Free();
  }
  bool IsNull() const
  {
    return _resource.IsNull() && _rtView.IsNull() && _srView.IsNull();
  }
  bool NotNull() const
  {
    return !IsNull();
  }
};

/** represents depth/stencil buffer*/ 
class DepthStencilFake;
class DepthStencil
{
public:
  static const DepthStencil DepthStencilNULL;

  DepthStencil() {}
  DepthStencil(const DepthStencil& ds)
    :_resource(ds._resource), _dsView(ds._dsView), _srView(ds._srView) {}
  DepthStencil(const DepthStencilFake& ds);
  DepthStencil(ID3D11Texture2D* resource, ID3D11DepthStencilView* dsView, ID3D11ShaderResourceView* srView)
  {
    _resource += resource;
    _dsView += dsView;
    _srView += srView;
  }

  /// resource 
  ComRef<ID3D11Texture2D> _resource;
  /// depth/stencil view
  ComRef<ID3D11DepthStencilView> _dsView;
  /// shader resource view - can be empty
  ComRef<ID3D11ShaderResourceView> _srView;

  void Free()
  {
    _dsView.Free();
    _srView.Free();
    _resource.Free();
  }
  bool IsNull() const
  {
    return _resource.IsNull() && _dsView.IsNull() && _srView.IsNull();
  }
  bool NotNull() const
  {
    return !IsNull();
  }
};

class DepthStencilFake
{
public:
  DepthStencilFake& operator=(const DepthStencil& ds)
  {
    _resource = ds._resource;
    _dsView = ds._dsView;
    _srView = ds._srView;
    return *this;
  }
  DepthStencilFake& operator=(const DepthStencilFake& ds)
  {
    _resource = ds._resource;
    _dsView = ds._dsView;
    _srView = ds._srView;
    return *this;
  }

  /// resource 
  ComRefFake<ID3D11Texture2D> _resource;
  /// depth/stencil view
  ComRefFake<ID3D11DepthStencilView> _dsView;
  /// ComRefFake resource view - can be empty
  ComRef<ID3D11ShaderResourceView> _srView;

  void Free() 
  {
    _dsView.Free();
    _srView.Free();
    _resource.Free();
  }
  bool IsNull() const
  {
    return _resource.IsNull() && _dsView.IsNull() && _srView.IsNull();
  }
  bool NotNull() const
  {
    return !IsNull();
  }
};


#if !SAFE_RESOURCE_UNSET
/// introduced to make sure we can debug construction / destruction
template <class Type>
class StoredComRef: public ComRef<Type>
{
public:
  StoredComRef(){}
  ~StoredComRef(){}
};

template <class Type>
class StoredFutureComRef: public Future< ComRef<Type> >
{
public:
  StoredFutureComRef(){}
  ~StoredFutureComRef(){}
};

#if _ARMA3_VERTEX_BUFFER_EX
/// this does not need to be template, but we are using template, so this would compile (little hack..)
template<class Type>
class StoredFutureVertexBuffer9Ex : public Future<VertexBuffer9Ex>
{
};
#endif

/**
Note: we are using Ref for both TextureHandle9 and IDirect3DTexture9. While this is unusual, it is valid.
The purpose of StoredTexRef is to call AddRef/Release as needed, which works for both.
As for additional safety usualy provided by ComRef absence of operator =, we do not need it here.
*/
template <class Type>
class StoredTexRef: public Ref<Type>
{
public:
  StoredTexRef(){}
  StoredTexRef(Type *src):Ref<Type>(src){}
  void operator = (Type *src){Ref<Type>::operator =(src);}
  ~StoredTexRef(){}
};

#if _ARMA3_SIMUL_WEATHER
typedef StoredTexRef<TextureSysGPUDX11> StoredSimulWeatherTexture;
#else
typedef Texture TextureSysGPUDX11;
typedef StoredTexRef<TextureSysGPUDX11> StoredSimulWeatherTexture;
#endif

/// partial specialization - store plain pointer as a ComRef
template <class Type>
struct SerialStoreTraits< StoredComRef<Type> , Type * >
{
  static void Store(StoredComRef<Type> &dst, Type *src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    dst += src;
  }
};

/// partial specialization - store plain pointer as a Future<ComRef>
template <class Type>
struct SerialStoreTraits< StoredFutureComRef<Type>, Future< ComRef<Type> > >
{
  static void Store(StoredFutureComRef<Type> &dst, const Future< ComRef<Type> > &src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    dst += src;
  }
};

/// partial specialization - store texture handle
template <class Type>
struct SerialStoreTraits< StoredTexRef<Type> , Type * >
{
  static void Store(StoredTexRef<Type> &dst, Type *src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    dst = src;
  }
};

#else

/// introduced to make sure we can debug construction / destruction
template <class Type>
class StoredComRef: public InitPtr<Type>
{
public:
  StoredComRef(){}
  void operator += (Type *src){InitPtr<Type>::operator =(src);if (src) src->AddRef();}
  ~StoredComRef(){GEngineDD11->BGAddResourceToRelease(GetRef());}
};

template <class Type>
class StoredFutureComRef: public Future< ComRef<Type> >
{
  typedef Future< ComRef<Type> > base;
public:
  StoredFutureComRef(){}
  void operator += (const Future< ComRef<Type> > &src)
  {
    base::operator =(src);
  }
  ~StoredFutureComRef()
  {
    ComRef<Type> ref = GetResult(); // should be ready now, it is used already
    if (ref.NotNull())
    {
      ref->AddRef();
      GEngineDD11->BGAddResourceToRelease(ref.GetRef());
    }
  }
};

#if _ARMA3_VERTEX_BUFFER_EX
/// this does not need to be template, but we are using template, so this would compile (little hack..)
template<class Type>
class StoredFutureVertexBuffer11Ex : public Future<VertexBuffer11Ex>
{
  typedef Future<VertexBuffer11Ex> base;

public:
  StoredFutureVertexBuffer11Ex() {}
  void operator += (const Future<VertexBuffer11Ex> &src)
  {
    base::operator =(src);
  }
  ~StoredFutureVertexBuffer11Ex()
  {
    ComRef<IDirect3DVertexBuffer9Old> ref = GetResult().GetBuffer(); // should be ready now, it is used already
    if (ref.NotNull())
    {
      ref->AddRef();
      GEngineDD11->BGAddResourceToRelease(ref.GetRef());
    }
  }
};
#else //#if _ARMA3_VERTEX_BUFFER_EX
#define StoredFutureVertexBuffer11Ex StoredFutureComRef
#endif


template <class Type>
class StoredTexRef: public InitPtr<Type>
{
public:
  StoredTexRef(){}
  StoredTexRef(Type *src):InitPtr<Type>(src){if (src) src->AddRef();}
  void operator = (Type *src){InitPtr<Type>::operator =(src);if (src) src->AddRef();}
  ~StoredTexRef() {GEngineDD11->BGAddTextureHandleToRelease(GetRef());}
};

} //namespace DX11

// specializations must be outside namespace
/// partial specialization - store plain pointer as a ComRef
template <class Type>
struct SerialStoreTraits< DX11::StoredComRef<Type> , Type * >
{
  static void Store(DX11::StoredComRef<Type> &dst, Type *src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    // Note: this assumes construction / destruction is performed as needed */
    dst += src;
  }
};

/// partial specialization - store plain pointer as a Future<ComRef>
template <class Type>
struct SerialStoreTraits< DX11::StoredFutureComRef<Type> , Future< ComRef<Type> > >
{
  static void Store(DX11::StoredFutureComRef<Type> &dst, const Future< ComRef<Type> > &src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    dst += src;
  }
};

#if _ARMA3_VERTEX_BUFFER_EX
/// StoredFutureVertexBuffer9Ex
template<class Type>
struct SerialStoreTraits< DX11::StoredFutureVertexBuffer11Ex<Type> , Future< DX11::VertexBuffer11Ex > >
{
  static void Store(DX11::StoredFutureVertexBuffer11Ex<Type> &dst, const Future< DX11::VertexBuffer11Ex > &src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    dst += src;
  }
};
#endif

/// partial specialization - store texture handle
template <class Type>
struct SerialStoreTraits< DX11::StoredTexRef<Type> , Type * >
{
  static void Store(DX11::StoredTexRef<Type> &dst, Type *src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    // Note: this assumes construction / destruction is performed as needed */
    dst = src;
  }
};

namespace DX11 {

#endif

extern StoredRefContainer EngineDDBGStoredRefs11;
typedef StoredRef<Texture,&EngineDDBGStoredRefs11> StoredRefTexture;

#if _ARMA3_SIMUL_WEATHER
typedef StoredRef<TextureSysGPUDX11, &EngineDDBGStoredRefs11> StoredSimulWeatherTexture;
#else
typedef Texture TextureSysGPUDX11;
typedef StoredRef<Texture, &EngineDDBGStoredRefs11> StoredSimulWeatherTexture;
#endif

/// allow storing NULL or pointer content
template <class Type>
struct StoredPtr
{
  /// was the pointer NULL or not?
  bool _notNull; 
  /// when not, store the payload
  Type _data;
  void operator = (const Type *ptr){_notNull = ptr!=NULL; if (ptr) _data = *ptr;}
  operator const Type *() const {return _notNull ? &_data : NULL;}
};

struct StoredRenderTarget
{
  /// resource of render target
  StoredComRef<ID3D11Texture2D> _resource;
  /// render target view
  StoredComRef<ID3D11RenderTargetView> _rtView;
  /// shader resource view - can be empty (not all render targets have this view)
  StoredComRef<ID3D11ShaderResourceView> _srView;

  void operator = (const RenderTarget& rt)
  {
    _resource += rt._resource;    
    _rtView += rt._rtView;
    _srView += rt._srView;
  }
  void operator = (const RenderTargetFake& rt)
  {
    _resource += rt._resource;    
    _rtView += rt._rtView;
    _srView += rt._srView;
  }

  operator const RenderTarget() const
  {
    return RenderTarget(_resource, _rtView, _srView);
  }
};

struct StoredDepthStencil
{
  /// resource 
  StoredComRef<ID3D11Texture2D> _resource;
  /// depth/stencil view
  StoredComRef<ID3D11DepthStencilView> _dsView;
  /// shader resource view - can be empty
  StoredComRef<ID3D11ShaderResourceView> _srView;

  void operator = (const DepthStencil& ds)
  {
    _resource += ds._resource;    
    _dsView += ds._dsView;
    _srView += ds._srView;
  }
  void operator = (const DepthStencilFake& ds)
  {
    _resource += ds._resource;    
    _dsView += ds._dsView;
    _srView += ds._srView;
  }
  
  operator DepthStencil() const
  {
    return DepthStencil(_resource, _dsView, _srView);
  }
};

static const int MaxSamplerStages_DX11 = 16;

typedef StoredArraySimple<float, 4> StoredFloat4;
typedef StoredArraySimple<unsigned char, 192*16> TStoredArrayConsts;
typedef StoredArraySimple<float, 192*4> TStoredArrayConstsFloat;
typedef StoredArraySimple<ID3D11Buffer *,  ShaderConstantBuffer::MaxConstantBufferCount> TStoredArrayConstBuffers;
typedef StoredArraySimple<ID3D11SamplerState *,  MaxSamplerStages_DX11> TStoredArraySamplerStates;

#define D3D_WORK_MESSAGES(CTX,MSG) \
  /* Low-Level recording D3D functions*/ \
  MSG(CTX,Void,IASetIndexBuffer,(const Future< ComRef<ID3D11Buffer > > &arg1, DXGI_FORMAT arg2, UINT arg3),3,StoredFutureComRef<ID3D11Buffer > arg1;DXGI_FORMAT arg2; UINT arg3; ) \
  MSG(CTX,Void,IASetVertexBuffers,(UINT arg1, const Future< VertexBuffer11Ex > &arg2, UINT arg3, UINT arg4),4,UINT arg1; StoredFutureVertexBuffer11Ex<ID3D11Buffer> arg2; UINT arg3; UINT arg4; ) \
  MSG(CTX,Void,ResolveSubresource,(ID3D11Resource *arg1, UINT arg2, ID3D11Resource *arg3, UINT arg4, DXGI_FORMAT arg5), 5, StoredComRef<ID3D11Resource> arg1; UINT arg2; StoredComRef<ID3D11Resource> arg3; UINT arg4; DXGI_FORMAT arg5;) \
  MSG(CTX,Void,CopyResource,(ID3D11Resource *arg1, ID3D11Resource *arg2), 2, StoredComRef<ID3D11Resource> arg1;StoredComRef<ID3D11Resource> arg2;) \
  MSG(CTX,Void,RSSetViewports,(const D3D11_VIEWPORT *arg1), 1, StoredPtr<D3D11_VIEWPORT> arg1;) \
  MSG(CTX,Void,ClearRenderTargetView,(ID3D11RenderTargetView* arg1, const Array<float>& arg2),2,ID3D11RenderTargetView *arg1; StoredFloat4 arg2;) \
  MSG(CTX,Void,ClearDepthStencilView,(ID3D11DepthStencilView* arg1, UINT arg2, float arg3, UINT arg4),4,ID3D11DepthStencilView* arg1; UINT arg2; float arg3; UINT arg4;) \
  MSG(CTX,Void,OMSetRenderTargets,(ID3D11RenderTargetView* arg1 , ID3D11DepthStencilView* arg2), 2, ID3D11RenderTargetView* arg1; ID3D11DepthStencilView* arg2;) \
  /* Low-Level recording engine functions*/ \
  MSG(CTX,Void,SetShaderConstantF,(const ShaderConstant* arg1 , int arg2, int arg3, const Array<float>& arg4), 4, const ShaderConstant* arg1; int arg2; int arg3; TStoredArrayConstsFloat arg4;) \
  MSG(CTX,Void,SetTexture,(int arg1, TextureHandle11* arg2), 2, int arg1; StoredTexRef<TextureHandle11> arg2; ) \
  MSG(CTX,Void,SetTextureDirect,(int arg1, TextureHandle11* arg2), 2, int arg1; StoredTexRef<TextureHandle11> arg2; ) \
  MSG(CTX,Void,SetRenderState,(DWORD arg1, DWORD arg2), 2, DWORD arg1; DWORD arg2; ) \
  MSG(CTX,Void,SetSamplerState,(DWORD arg1, DWORD arg2, DWORD arg3), 3, DWORD arg1; DWORD arg2; DWORD arg3;) \
  MSG(CTX,Void,SetupAllBeforeDraw,(), 0, ) \
  MSG(CTX,Void,DrawIndexedPrimitive,(D3DPRIMITIVETYPE_OLD arg1, INT arg2, UINT arg3, UINT arg4, UINT arg5, UINT arg6), 6, D3DPRIMITIVETYPE_OLD arg1; INT arg2; UINT arg3; UINT arg4; UINT arg5; UINT arg6) \
  MSG(CTX,Void,DrawPrimitive,(D3DPRIMITIVETYPE_OLD arg1, UINT arg2, UINT arg3), 3, D3DPRIMITIVETYPE_OLD arg1; UINT arg2; UINT arg3) \
  MSG(CTX,Void,SetPrimitiveTopology,(D3D11_PRIMITIVE_TOPOLOGY arg1), 1, D3D11_PRIMITIVE_TOPOLOGY arg1;) \
  MSG(CTX,Void,SetVertexShader,(ID3D11VertexShader* arg1, ID3D11InputLayout* arg2), 2, ID3D11VertexShader* arg1; ID3D11InputLayout* arg2;) \
  MSG(CTX,Void,SetPixelShader,(ID3D11PixelShader* arg1), 1, ID3D11PixelShader* arg1) \
  MSG(CTX,Void,R2TCopyTarget,(ID3D11ShaderResourceView *arg1, CONST RECT *arg2, const RenderTarget& arg3, bool arg4),4,StoredComRef<ID3D11ShaderResourceView> arg1; StoredPtr<RECT> arg2; StoredRenderTarget arg3; bool arg4;) \
  MSG(CTX,Void,R2TPostProcess,(int arg1, float arg2, int arg3),3,int arg1; float arg2; int arg3;) \
  MSG(CTX,Void,R2TClearTarget,(ID3D11RenderTargetView *arg1),1,StoredComRef<ID3D11RenderTargetView> arg1;) \
  MSG(CTX,Void,Flush,(), 0, ) \
  \
  MSG(CTX,Void,ProfileBeginGraphScope,(unsigned int arg1, const char *arg2),2,unsigned int arg1; BString<256> arg2;) \
  MSG(CTX,Void,ProfileEndGraphScope,(),0,) \
  \
  /* High-Level recording engine functions */ \
  MSG(CTX,Void,Postprocess,(float arg1, const PostFxSettings &arg2),2,float arg1; PostFxSettings arg2;) \
  MSG(CTX,Void,NoPostprocess,(),0,) \
  MSG(CTX,Void,Draw2D,(const Draw2DParsExt &arg1, const Rect2DAbs &arg2, const Rect2DAbs &arg3),3,Draw2DParsExtStored arg1; Rect2DAbs arg2; Rect2DAbs arg3;) \
  MSG(CTX,Void,SetFogColor,(ColorVal arg1, ColorVal arg2),1,Color arg1; Color arg2) \
  MSG(CTX,Void,FinishRendering,(),0,) \
  MSG(CTX,Void,DrawPolyPrepare,(const MipInfo &arg1, int arg2),2,MipInfoStored arg1; int arg2;) \
  MSG(CTX,Void,DrawLineDo,(const Line2DAbs &arg1, PackedColor arg2, PackedColor arg3, const Rect2DAbs &arg4),4,Line2DAbs arg1; PackedColor arg2; PackedColor arg3; Rect2DAbs arg4;) \
  MSG(CTX,Void,DrawPolyAbsDo,(const Array<Vertex2DAbs> &arg1, const Rect2DAbs &arg2),2,StoredArrayPolyAbs arg1; Rect2DAbs arg2;) \
  MSG(CTX,Void,DrawPolyPixelDo,(const Array<Vertex2DPixel> &arg1, const Rect2DPixel &arg2),2,StoredArrayPolyPixel arg1; Rect2DPixel arg2;) \
  MSG(CTX,Void,FlushQueues,(),0,) \
  MSG(CTX,Void,DrawDecal,(int arg1, Vector3Par arg2, float arg3, float arg4, float arg5, PackedColor arg6, const MipInfo &arg7, const TexMaterial *arg8, int arg9),9,int arg1; Vector3 arg2; float arg3; float arg4; float arg5; PackedColor arg6; MipInfoStored arg7; const TexMaterial *arg8; int arg9;) \
  MSG(CTX,Void,CalculateFlareIntensity,(int arg1, int arg2, int arg3, int arg4),4,int arg1; int arg2; int arg3; int arg4;) \
  MSG(CTX,Void,PresentBackbuffer,(),0,) \
  MSG(CTX,Void,ForgetAvgIntensity,(),0,) \
  /* resource copying */ \
  MSG(CTX,Void,FreeSurface,(TextureHandle11 *arg1),1,StoredTexRef<TextureHandle11> arg1;) \
  MSG(CTX,Void,DrawSSAO,(float arg1, float arg2, float arg3),3, float arg1; float arg2; float arg3;) \
  /* Simul weather */ \
  MSG(CTX,Void,UpdateSimulWeatherTexture,(TextureSysGPUDX11 *arg1, unsigned arg2),2,StoredSimulWeatherTexture arg1; unsigned arg2;) \


#define VISUAL_WORK_MESSAGES(CTX,MSG) \
  MSG(CTX,WaitableVoid,PerformMipmapLoading,(),0,) \
  MSG(CTX,Void,DoLoadHeaders,(Texture *arg1),1,StoredRefTexture arg1;) \
  MSG(CTX,Void,UseMipmap,(Texture *arg1, int arg2, int arg3, int arg4),1,StoredRefTexture arg1;int arg2; int arg3; int arg4;) \
  MSG(CTX,Void,CallDelegate,(VirtDelegate0<void> *arg1),1,SRef< VirtDelegate0<void> > arg1;) \

struct MipInfoStored
{
  MipInfo _pars;

  MipInfoStored()
  {
    StoredRefTexture::Store(_pars._texture);
  }
  MipInfoStored(const MipInfoStored &src)
    :_pars(src._pars)
  {
    StoredRefTexture::Store(_pars._texture);
  }
  void operator = (const MipInfoStored &src)
  {
    StoredRefTexture::Store(_pars._texture);
    StoredRefTexture::Destroy(_pars._texture);
    _pars = src._pars;
  }
  void operator = (const MipInfo &src)
  {
    StoredRefTexture::Store(src._texture);
    StoredRefTexture::Destroy(_pars._texture);
    _pars = src;
  }
  ~MipInfoStored()
  {
    StoredRefTexture::Destroy(_pars._texture);
  }

};
/// store, including texture reference counting
struct Draw2DParsExtStored
{
  Draw2DParsExt _pars;

  Draw2DParsExtStored()
  {
    StoredRefTexture::Store(_pars.mip._texture);
  }
  Draw2DParsExtStored(const Draw2DParsExtStored &src)
    :_pars(src._pars)
  {
    StoredRefTexture::Store(_pars.mip._texture);
  }
  void operator = (const Draw2DParsExtStored &src)
  {
    StoredRefTexture::Store(src._pars.mip._texture);
    StoredRefTexture::Destroy(_pars.mip._texture);
    _pars = src._pars;
  }
  void operator = (const Draw2DParsExt &src)
  {
    StoredRefTexture::Store(src.mip._texture);
    StoredRefTexture::Destroy(_pars.mip._texture);
    _pars = src;
  }
  ~Draw2DParsExtStored()
  {
    StoredRefTexture::Destroy(_pars.mip._texture);
  }
};

D3D_WORK_MESSAGES(D3DDeviceServant,ACTOBJ_DECLARE_ARGS)

VISUAL_WORK_MESSAGES(VisualServant,ACTOBJ_DECLARE_ARGS)

}//namespace DX11

// template specializations from different namespace - must be outside of DX11 namespace

// macro to help specialize templates for StoredArray types -> can be truncated based on real data usage
#define STORED_ARRAY_SIZE_ARG(Name, Arg) \
template <> \
struct StoredSize<const Name##AllArgs> \
{ \
  static size_t SizeOf(const Name##AllArgs &val) \
{ \
  Assert(val.Arg.GetUnusedBytes()<sizeof(val)); \
  return AlignStoreSize(sizeof(val) - val.Arg.GetUnusedBytes()); \
} \
};

STORED_ARRAY_SIZE_ARG(DX11::SetShaderConstantF, arg4);

/*
Size of empty class is by default 1 - this causes empty classes to break alignment for successive elements when stored.
*/
#define STORED_EMPTY_CLASS_SIZE(Name) \
template <>\
struct StoredSize<const Name##AllArgs>\
{\
  static size_t SizeOf(const Name##AllArgs &val) {return 0;}\
};

STORED_EMPTY_CLASS_SIZE(DX11::SetupAllBeforeDraw);
STORED_EMPTY_CLASS_SIZE(DX11::Flush);

namespace DX11
{



/// implementation of servant for the D3D active object
class D3DDeviceServant
{
protected:
  /// native device
  ComRef<ID3D11Device> _dev;
  /// native device context
  ComRef<ID3D11DeviceContext> _devContext;
  /// native swap chain
  ComRef<IDXGISwapChain> _swapChain;
  /// pointer to engine
  InitPtr<EngineDD11> _engine;
  /// last SetIndices succeeded
  bool _ibOK;
  /// last SetStreamSource succeeded
  bool _vbOK;

public:
  D3DDeviceServant():_ibOK(true), _vbOK(true)
  {}

  D3D_WORK_MESSAGES(D3DDeviceServant,ACTOBJ_DECLARE_FUNCTION)

    enum ActObjIds
  {
    D3D_WORK_MESSAGES(D3DDeviceServant,ACTOBJ_ID_FUNCTION)
  };  

  IS_ACTIVE_OBJECT(D3DDeviceServant,D3D_WORK_MESSAGES)
};

/// implementation of servant for the D3D active object
class VisualServant
{
public:

  VISUAL_WORK_MESSAGES(VisualServant,ACTOBJ_DECLARE_FUNCTION)

    enum ActObjIds
  {
    VISUAL_WORK_MESSAGES(VisualServant,ACTOBJ_ID_FUNCTION)
  };  

  IS_ACTIVE_OBJECT(VisualServant,VISUAL_WORK_MESSAGES)
};

/// command buffer recording and playback device
class D3DCommandBufPlay: public CommandBufferPlay<D3DDeviceServant>
{
  typedef CommandBufferPlay<D3DDeviceServant> Base;

public:

  void AttachDevice(const ComRef<ID3D11Device> &dev, const ComRef<ID3D11DeviceContext> &devContext, const ComRef<IDXGISwapChain> &swapChain, EngineDD11* engine)
  {
    Base::_dev = dev;
    Base::_devContext = devContext;
    Base::_swapChain = swapChain;
    Base::_engine = engine;
  }
  void DetachDevice()
  {
    Base::_swapChain.Free();
    Base::_devContext.Free();
    Base::_dev.Free();
  }

  bool IsNull() const {return Base::_dev.IsNull() || Base::_devContext.IsNull() || Base::_swapChain.IsNull();}
};

struct PredicatedPart
{
  /// what predication (-1 =  none, >=0 = pass ID)
  int predicate:Engine::NPredicationModes;
  /// offset of the end of the predicated part
  int end:32-Engine::NPredicationModes;

  ClassIsSimple(PredicatedPart)
};
/// determine which parts of command buffer are predicated
struct PredicationIndex: public AutoArray<PredicatedPart, MemAllocLocal<PredicatedPart,32> >
{
  typedef PredicatedPart Item;
};

/// command buffer recording device
/** One instance for each thread. */
/**
16 KB should be enough to contain the instruction stream including any constants
*/
class D3DCommandBufRec: public CommandBufferRec< D3DDeviceServant,SerialArraySimple<16*1024> >
{
  typedef CommandBufferRec< D3DDeviceServant,SerialArraySimple<16*1024> > base;

public:
  /// predication information related to _rec
  PredicationIndex _predicationIndex;

  /// current predication Id
  InitVal<int,~0> _currPredication;


  D3D_WORK_MESSAGES(D3DDeviceServant,CMDBUF_WRAP_FUNC)

    D3DCommandBufRec()
  {
#if _DEBUG
    _debug.Reset();
#endif
  }
  void Reset()
  {
    base::Reset();
    _predicationIndex.Clear();
#if _DEBUG
    _debug.Reset();
#endif
  }

  void FlushPredication()
  {  
    // check last offset 
    int lastOffset = 0;
    // initialize lastPredication with impossible value
    // when there was no predication so far, we need to make sure we will flush the current one
    int lastPredication = INT_MAX;
    if (_predicationIndex.Size()>0)
    {
      lastOffset = _predicationIndex.Last().end;
      lastPredication = _predicationIndex.Last().predicate;
    }
    int currOffset = GetCurrentOffset();
    // write this section only when non-empty
    if (currOffset>lastOffset)
    {
      // handle one more special case: writing same predication as before
      if (lastPredication==_currPredication)
      {
        Assert(_predicationIndex.Size()>0);
        // only extend the last section
        _predicationIndex.Last().end = currOffset;
      }
      else
      {
        // write into index: current mask, current CB write offset
        PredicationIndex::Item &item = _predicationIndex.Append();
        item.predicate = _currPredication;
        item.end = currOffset;
      }
    }
  }

  const PredicationIndex &GetPredication() const {return _predicationIndex;}

};

/// call either D3DLocked or CmdBuf implementation

class MyD3DDevice: public D3DDeviceAutoLocked<false>
{
public:
  volatile LONG _cbCount;

  /// for debugging purposes - track last fence value each frame
  volatile DWORD _lastFence;
  /// redirect to the upper level
  D3DCommandBufPlay _cb;

public:
  MyD3DDevice(){_cbCount=0;}

  // used so that usage can be similar to IDirect3DDevice9 *
  D3DDeviceAutoLocked *operator -> () {return this;}

  __forceinline void DetachDevice()
  {
    D3DDeviceAutoLocked::Free();
    _cb.DetachDevice();
  }

  __forceinline void AttachDevice(const ComRef<ID3D11Device> &dev, const ComRef<ID3D11DeviceContext> &devContext, const ComRef<IDXGISwapChain> &swapChain, EngineDD11* engine)
  {
    D3DDeviceAutoLocked::AttachDevice(dev, devContext, swapChain);
    _cb.AttachDevice(dev, devContext, swapChain, engine);
  }

  __forceinline bool IsNull() const
  {
    Assert(D3DDeviceAutoLocked::IsNull() == _cb.IsNull());
    return D3DDeviceAutoLocked::IsNull();
  }
};

/// wrapped D3D device, allowing CB recording
class MyD3DDeviceCB: public D3DDeviceAutoLocked<true>
{
public:
  D3DDeviceAutoLocked *operator -> () {return this;}

  void operator += (ID3D11Device* dev){D3DDeviceAutoLocked<true>::operator += (dev);}
  void operator << (ID3D11Device* dev){D3DDeviceAutoLocked<true>::operator << (dev);}
 
  void operator += (ID3D11DeviceContext *devContext){D3DDeviceAutoLocked<true>::operator += (devContext);}
  void operator << (ID3D11DeviceContext *devContext){D3DDeviceAutoLocked<true>::operator << (devContext);}
 
  void operator += (IDXGISwapChain *swapChain){D3DDeviceAutoLocked<true>::operator += (swapChain);}
  void operator << (IDXGISwapChain *swapChain){D3DDeviceAutoLocked<true>::operator << (swapChain);}
};


/// vertex declaration in DX11
class VertexDeclaration
{
public:
  VertexDeclaration();
  VertexDeclaration(D3D11_INPUT_ELEMENT_DESC* inputElements, int size, const char *name);
  /// returns if declaration is valid
  bool IsValid() const {return _inputElements != NULL && _size > 0;}

  const D3D11_INPUT_ELEMENT_DESC* GetInputElements() const {return _inputElements;}
  int GetSize() const {return _size;}
  bool GetUsesTangent() const {return _usesTangent;}
  bool GetUses2TexCoords() const {return _uses2TexCoords; }

  /// NULL vertex declaration
  static VertexDeclaration NULLDeclaration;
private:
  /// checks input elements and determines what is used
  void CheckInputElements();

  //! Pointer to input elements (we don't own it)
  D3D11_INPUT_ELEMENT_DESC*_inputElements;
  //! Number of input elements
  int _size;

  /// if tangents are used in input layout
  bool _usesTangent;
  /// if 2 texcoords are used in input layout
  bool _uses2TexCoords;  
  /// for debugging purposes
  RString _name;
};

//! Structure to hold a particular vertex shader and some informations associated with it
struct VertexShaderInfo
{
  //! Vertex shader
  ComRef<ID3D11VertexShader> _shader;
  //! Input layout for shader
  ComRef<ID3D11InputLayout> _inputLayout;
#if _ENABLE_REPORT
  //! for debugging purposes
  VertexDeclaration _declaration;
#endif
  //! Free the resources allocated
  void Free()
  {
    _shader.Free();
    _inputLayout.Free();
  }
  bool NotNull()
  {
    return _shader.NotNull() && _inputLayout.NotNull();
  }
};

/// pixel shader identification
struct PSName
{
  const char *name;
};

/// one pixel shader compilation
struct PSCachedItem
{
  PSName name;
  ComRef<ID3D11PixelShader> ps;
};

/**
we assume function name + set of defines is enough to identify the shader
*/
struct VSName
{
  RString name;
  AutoArray<RString> defines;
};

struct VSCachedItem
{
  VSName name;
  VertexShaderInfo vs;
};

} //namespace

// must be defined outside of namespace
template <>
struct FindArrayKeyTraits<DX11::PSCachedItem>
{
  typedef const DX11::PSName &KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b)
  {
    return !strcmp(a.name ? a.name : "",b.name ? b.name : "");
  }
  /// get a key from an item
  static KeyType GetKey(const DX11::PSCachedItem &a) {return a.name;}
};

template <>
struct MapClassTraits<DX11::VSCachedItem>: public DefMapClassTraits<DX11::VSCachedItem>
{
  /// key type
  typedef const DX11::VSName &KeyType;
  typedef DX11::VSCachedItem Type;
  static unsigned int CalculateHashValue(KeyType key)
  {
    int val = CalculateStringHashValue(key.name);
    for (int i=0; i<key.defines.Size(); i++)
    {
      val = CalculateStringHashValue(key.defines[i],val);
    }
    return val;
  }

  /// compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
  static int CmpKey(KeyType k1, KeyType k2)
  {
    int d = strcmp(k1.name,k2.name);
    if (d) return d;
    d = k1.defines.Size()-k2.defines.Size();
    if (d) return d;
    for (int i=0; i<k1.defines.Size(); i++)
    {
      d = strcmp(k1.defines[i],k2.defines[i]);
      if (d) return d;
    }
    return 0;
  }

  static KeyType GetKey(const Type &item) {return item.name;}
};

namespace DX11
{

  /// simulate Xbox "fence" on PC
/**
Two implementations possible:
advanced and accurate using query
very simple using "safe estimate" -
assume buffer is locked for given number of frames since used
*/
class ResourceBusy11
{
  /// frame in which the fence will still be busy
  int _busyUntil;

public:
  // default number of frames the resource is considered busy
  static const int DefaultNumFrames = 2;

  ResourceBusy11():_busyUntil(0){};

  /// start fence - resource is used
  void Used(int numFrames = DefaultNumFrames);
  /// check if resource is still used
  bool IsBusy() const;
  /// check if resource is still untouched
  bool WasNeverUsed() const {return _busyUntil==0;}
};

struct VectorCompressed
{
  DWORD v;
};

#define DX11_DUMMY_VALUES 1

// DX11 DUMMY VALUES - EXPLANATION
// Why we use dummy values in vertex structures and declarations?
// See news:f4jiqd$vv$1@new_server.localdomain
struct SVertex9
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
#if DX11_DUMMY_VALUES
  VectorCompressed s; // DX11 DUMMY VALUES
  VectorCompressed t; // DX11 DUMMY VALUES
#endif
};

class VertexDynamicData11
{
  friend class EngineDD11;

#ifdef DONT_USE_DYNBUFFERS
  //! Auxiliary array of vertices (used in AddVertices())
  /** TODO:MC: may need one instance for each CB */
  AutoArray<SVertex9> _verticesOnly;
  //! Array of geometry to be drawn - unlike the _vBuffer this data are organized in triangles (without any need of IB)
  AutoArray<SVertex9> _geometry;
#else
  ComRef<IDirect3DVertexBuffer9> _vBuffer[NumDynamicVB3D];
  const ComRef<IDirect3DVertexBuffer9> &GetVBuffer() const {return _vBuffer[_actualVBIndex];}
  int _vBufferSize; // reserved sizes of resources
  int _vBufferUsed; // used sizes
  int _actualVBIndex;
#endif

public:
  //! Constructor
  VertexDynamicData11();
  //! Creation of resources
  void Init(int size, MyD3DDeviceCB &dev);
  //! Destruction of resources
  void Dealloc();
  //! Add vertices (in DONT_USE_DYNBUFFERS case organize them to triangle list - we will not use any IB)
  int AddVertices(int cb, const VertexTableAnimationContext *animContext, const Shape &src);
  //! Allocates space for n vertices and returns pointer to the vertices array to be filled out
  int Lock(SVertex9 *&vertex, int n);
  //! Unlocks the vertex buffer
  void Unlock();
};


/// index buffer encapsulation
class IndexStaticData11: public RefCount
{
  friend class EngineDD11;

public:
  typedef Future< ComRef<IDirect3DIndexBuffer9Old> > BufferFuture;

private:
  //! index buffer
  BufferFuture _iBuffer;
  /// index buffer size (count of items)
  int _count;
  /// busy tracker
  ResourceBusy11 _busy;

public:
  //! Constructor
  /*!
  \param indices Required number of indices in the buffer
  */
  IndexStaticData11(int indices);
  //! Destructor
  ~IndexStaticData11();
  //! Create the buffer synchronously
  void Init(ComRef<IDirect3DIndexBuffer9Old> buffer);
  //! Mark the buffer as a future
  void InitFuture();
  //! Create the buffer and set it to the Future
  void SetFuture(ComRef<IDirect3DIndexBuffer9Old> buffer);
  /// implementation of the buffer creation
  ComRef<IDirect3DIndexBuffer9Old> CreateBuffer();

public:
  //! Deallocating of data
  /*!
  Frees the vertex buffer and informs the engine about that
  */
  void Dealloc();

  //! Calculates how many indices the shape will occupy (as a triangle list)
  static int IndicesNeededTotal(const FaceData &src, int nInstances);
  //! Calculates how many indices the shape will occupy (as a triangle strip)
  static int IndicesNeededStrippedTotal(const FaceArray &src, int nInstances);
  /// scan min/max in the given area of the index buffer
  /** used to catch vertex buffer range violations */
  // void ScanMinMax(int &minI, int &maxI, int start, int size) const;

  //! Add shape data to this buffer
  /*!
  \param src Shape to add
  \param iIndex Information about position and size of the data in the index buffer
  \param dynamic Method is working only if "dynamic == false" (we are working with static data)
  \return True if method succeeded, false elsewhere
  */
  static bool AddShape(ComRef<IDirect3DIndexBuffer9Old> buffer, const FaceData &srcDta, int indicesTotal, bool dynamic, int nInstances, int instanceVertices, const AutoArray<Offset> &sectionsEnds);

  static void *Lock(ComRef<IDirect3DIndexBuffer9Old> buffer, int size);
  //! Add shape data as sprites to this buffer
  /*!
  \param src Shape to add
  \param iIndex Information about position and size of the data in the index buffer
  \param dynamic Method is working only if "dynamic == false" (we are working with static data)
  \return True if method succeeded, false elsewhere
  */
  static bool AddShapeSprite(ComRef<IDirect3DIndexBuffer9Old> buffer, const FaceData &src, int indicesTotal, bool dynamic, int nInstances);
  //! Add shape data as point to this buffer
  static bool AddShapePoint(ComRef<IDirect3DIndexBuffer9Old> buffer, const FaceData &src, int indicesTotal, bool dynamic, int nInstances, int instanceVertices);
  
  int NIndices() const {return _count;}
  
  int GetSize() const {return _count*sizeof(VertexIndex);}

  /// check if buffer is currently used by GPU
  bool IsBusy() const {return _busy.IsBusy();}

  void Used(int numFrames = ResourceBusy11::DefaultNumFrames){_busy.Used(numFrames);}
};

class VertexBufferD3D11;

/// vertex buffer encapsulation
class VertexStaticData11: public RefCount
{
  friend class EngineDD11;

public:
#if _ARMA3_VERTEX_BUFFER_EX
  typedef Future< VertexBuffer11Ex > BufferFuture;
#else
  typedef Future< ComRef<IDirect3DVertexBuffer9Old> > BufferFuture;
#endif

private:
  //! Count of vertices held
  int _count;
  //! Size of one vertex in bytes
  int _vertexSize;
  /// busy tracker
  ResourceBusy11 _busy;
  /// track if dynamic usage was used
  bool _usageDynamic;
  //! Managed vertex buffer
  BufferFuture _vBuffer;

public:
  //!{ Fills out the data with the shape (specialized version for each data type required)
  template <int VertexDecl>
  static void AddShapeData(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf);
  template <int VertexDecl>
  static void AddShapeInstancedData(void *data, const VertexTableData &src, const VertexBufferD3D11 *vbuf, int instancesCount);
  //! Sprite has an own function to fill out the data
  static void AddShapeSpriteInstanced(void *data, const VertexTableData &src, int instancesCount);
  //! Points have an own function to fill out the data
  static void AddShapePoint(void *data, const VertexTableData &src);
  //!}
  //!{ Locking and unlocking vertex data
  static void *Lock(ComRef<IDirect3DVertexBuffer9Old> buffer, int size);
  static void Unlock(ComRef<IDirect3DVertexBuffer9Old> buffer);
  //!}

  //! Constructor
  VertexStaticData11(int vertices, int vertexSize, bool frequent);
  //! Destructor
  ~VertexStaticData11();
  
  //! Create the buffer synchronously
  void Init(ComRef<IDirect3DVertexBuffer9Old> buffer
#if _ARMA3_VERTEX_BUFFER_EX
    , int numVertices = VertexBuffer11Ex::UndefinedValue
#endif
    );
  //! Mark the buffer as a future
  void InitFuture();
  //! Create the buffer and set it to the Future
  void SetFuture(ComRef<IDirect3DVertexBuffer9Old> buffer
#if _ARMA3_VERTEX_BUFFER_EX
    , int numVetices = VertexBuffer11Ex::UndefinedValue
#endif
    );
  /// implementation of the buffer creation
  ComRef<IDirect3DVertexBuffer9Old> CreateBuffer();

  BufferFuture& GetBufferFuture() {return _vBuffer;}

  //! Deallocating of data
  /*!
  Frees the vertex buffer and informs the engine about that
  */
  void Dealloc();

  //!{ Skinned instanced version
#if _ENABLE_SKINNEDINSTANCING
  bool AddShapeSkinnedInstanced(const Shape &src, bool dynamic, int instancesCount);
  bool AddShapeNormalSkinnedInstanced(const Shape &src, bool dynamic, int instancesCount);
  bool AddShapeShadowVolumeSkinnedInstanced(const Shape &src, bool dynamic, int instancesCount);
#endif
  //!}
  //! Removes specified shape from the heap associated with the vertex buffer
  /*!
  \param vIndex Item of the heap to remove
  */
  //! Returns number of allocated vertices
  /*!
  \return Number of allocated vertices
  */
  int Size() {return _count;};
  //! Returns size of the vertex in bytes
  /*!
  \return Size of the vertex in bytes
  */
  int VertexSize() {return _vertexSize;};

  /// check if buffer is currently used by GPU
  bool IsBusy() const {return _busy.IsBusy();}

  void Used(int numFrames = ResourceBusy11::DefaultNumFrames){_busy.Used(numFrames);}
};

enum
{
  // triangle reordering queues have very little effect with HW T&L
  // as they are primarily used to optimize 3D operation across several objects
  // they have little effect on 2D operation, but they also make FlushAllQueues much slower
  MaxTriQueues=1,
  TriQueueSize=4096
};

struct TriQueue11
{
  //int _queueBase,_queueSize; // _triangleQueue position in VB

  StaticArray<WORD> _triangleQueue; // see BeginMesh,EndMesh

  TextureD3D11 *_texture; // which texture
  const TexMaterial *_material; // what material
  int _level; // which level
  int _special; // drawing flags
};

struct Queue11
{
  VertexStaticData11::BufferFuture _meshBuffer[NumDynamicVB2D];
  IndexStaticData11::BufferFuture _indexBuffer[NumDynamicVB2D];
  int _vertexBufferUsed; // how many vertices are in vertex buffer
  int _indexBufferUsed; // how many indices are is index buffer

  int _meshBase,_meshSize; // _mesh position in VB
  int _actualVBIndex;
  int _actualIBIndex;

  TriQueue11 _tri;
  bool _triUsed;

  bool _firstVertex; // first call in scene should always use DISCARD
  bool _firstIndex;

  Queue11();
  int Allocate(TextureD3D11 *tex, int level, const TexMaterial *mat, int spec);
  void Free();

  const VertexStaticData11::BufferFuture &GetVBuffer() const {return _meshBuffer[_actualVBIndex];}
  const IndexStaticData11::BufferFuture &GetIBuffer() const {return _indexBuffer[_actualIBIndex];}
};

/// proxy class used to avoid necessity to declare VertexBufferD3D here
ALIGN_SLIST class VertexBufferD3D11LRUItem: public LinkBidirWithFrameStore, public SListControlled
{
  friend class EngineDD11;
  /// if shape is destroyed, vertex buffer is always destroyed with it
  /**
  owner should call Detach(owner} before destruction
  this is verified in destructor
  */
  const VertexTable *_owner;

public:
  VertexBufferD3D11LRUItem() {_owner = NULL;}
  ~VertexBufferD3D11LRUItem() {DoAssert(_owner==NULL);}
  virtual size_t GetMemoryControlled() const = NULL;

protected:
  void SetOwner(const VertexTable *owner){_owner = owner;}
  const VertexTable *GetOwner() const {return _owner;}
};

//! Specialized version of vertex buffer
class VertexBufferD3D11: public VertexBuffer, public VertexBufferD3D11LRUItem
{
  friend class EngineDD11;
  friend class VertexStaticData11;

private:

  bool _dynamic;

  AutoArray<VBSectionInfo> _sections;

  //! Maximum UV values the vertex buffer contains (suitable for UV compression) for each stage
  UVPair _maxUV[MAX_UV_SETS];
  //! Minimum UV values the vertex buffer contains (suitable for UV compression) for each stage
  UVPair _minUV[MAX_UV_SETS];

  // following are valid only for "shared" (!_separate)
  Ref<VertexStaticData11> _sharedV;
  Ref<IndexStaticData11> _sharedI;

public:
  VertexBufferD3D11();
  ~VertexBufferD3D11();

  bool Init(EngineDD11 *engine, const Shape &src, VBType type, bool frequent);

  template <int set>
  void SetUVRange(const UVPair &minUV, const UVPair &maxUV) {_minUV[set] = minUV, _maxUV[set] = maxUV;}

  template <int set>
  void SetUVRangeDummy()
  {
    if (set==0) _minUV[set].u = _minUV[set].v = 0, _maxUV[set].u = _maxUV[set].v = 1;
    else _minUV[set] = _minUV[0], _maxUV[set] = _maxUV[0];
  }

  template <int vertexDecl>
  void PrepareUV(const VertexTable &src);

  VertexStaticData11 *GetSharedV() const {return _sharedV;}
  IndexStaticData11 *GetSharedI() const {return _sharedI;}

  //@{ implementation of VertexBuffer
  virtual void Update(int cb, const VertexTableAnimationContext *animContext, const Shape &src, const EngineShapeProperties &prop, VSDVersion vsdVersion);
  virtual void Detach(const VertexTable &src);
  virtual void RemoveIndexBuffer();
  virtual bool IsGeometryLoaded() const;
  virtual void OnSizeChanged();
  virtual size_t GetBufferSize() const;
  //@}

  // hotfix - news:h1dkbt$k0h$2@new-server.localdomain
  //! Get number of vertices in the buffer
  int GetVBufferSize() const
  {
    return _sharedV ? _sharedV->Size() : 0;
  }

  //@{ implementation of VertexBufferD3D11LRUItem
  virtual size_t GetMemoryControlled() const;
  //@}
};

/// represents all information needed to remember render target and depth stencil
class RenderTargetDepthStencilInfo
{
public:
  RenderTargetDepthStencilInfo():_minZRange(0.0f), _maxZRange(1.0f), _width(0.0f), _height(0.0f) {}
  RenderTargetDepthStencilInfo(const RenderTargetFake& renderTarget, const DepthStencilFake& depthStencil, float minZRange, float maxZRange)
    :_renderTarget(renderTarget), _depthStencil(depthStencil), _minZRange(minZRange), _maxZRange(maxZRange) {}

  RenderTargetFake _renderTarget;
  DepthStencilFake _depthStencil;
  float _width;
  float _height;
  float _minZRange;
  float _maxZRange;
};

#if DX11_USE_HASHED_STATE_OBJECTS
//! Structure to hold objects in a hash table for quick access with possibility to delete the oldest objects
/*!
TypeDef holds the structure that identifies the object. TypeObj holds the object structure.
*/
template <class TypeDef, class TypeObj, int maxItems>
class HashTableWithHistory
{
private:
  //! Key type
  typedef TypeDef KeyType;

public:
  //! Object in the table
  struct TableItem : public RefCount, public TLinkBidirD
  {
    //! Structure that identifies the object (it is the key)
    KeyType _definition;
    //! Object associated with the key
    TypeObj _object;
    //! Constructor
    TableItem() {}
    //! Constructor
    TableItem(KeyType d, TypeObj o) {_definition = d; _object = o;}
    //! Structure character
    ClassIsMovableZeroed(TableItem)
  };

private:
  //! Traits of the object
  struct TableItemTraits : public DefMapClassTraits<Ref<TableItem> >
  {
    //! Typedef to overwrite the local DefMapClassTraits KeyType typedef
    typedef TypeDef KeyType;

    //! Calculate hash value
    static unsigned int CalculateHashValue(KeyType key)
    {
      unsigned int k = 0;
      int s = sizeof(KeyType) / 4;
      unsigned int *d = (unsigned int *)(&key);
      for (int i = 0; i < s; i++)
      {
        //unsigned int toChangeMask = k^d[i];
        k = k^d[i];
      }
      return k;
    }

    //! Compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
    static int CmpKey(KeyType k1, KeyType k2)
    {
      return memcmp(&k1, &k2, sizeof(KeyType));
    }

    //! Return the key
    static KeyType GetKey(const Ref<TableItem> &item) {return item->_definition;}
  };
  //! Hash table container
  MapStringToClass<Ref<TableItem>, RefArray<TableItem>, TableItemTraits> _hashTable;
  //! Link list container
  TListBidir<TableItem> _list;

public:
  //! Get item from the hash table, mark it as most recent
  const Ref<TableItem> &Get(KeyType &d)
  {
    // Get item from the hash table
    const Ref<TableItem> &htItem = _hashTable.Get(d);

    // If the item was found, then move it to the beginning
    if (_hashTable.NotNull(htItem))
    {
      //! Remove item from the list
      _list.Delete(htItem);

      //! Put it at the beginning
      _list.Insert(htItem);
    }

    // Return the item
    return htItem;
  }
  //! Add item to the hash table, remove some old items if necessary
  void Add(const Ref<TableItem> &item)
  {
    // If the number of items in the hash table exceeded limit, remove the last
    while (_hashTable.NItems() >= maxItems)
    {
      TableItem *lastItem = _list.Last();
      _hashTable.Remove(lastItem->_definition);
      LogF("Info: hash table was reduced");
    }

    // Add item to the hash table
    _hashTable.Add(item);

    // Add item to the link list at the beginning
    _list.Insert(item);
  }
  //! Determine the item to be NULL
  bool IsNull(const Ref<TableItem> &item) const {return _hashTable.IsNull(item);}
  //! Determine the item not to be NULL
  bool NotNull(const Ref<TableItem> &item) const {return _hashTable.NotNull(item);}
};

typedef HashTableWithHistory<D3D11_DEPTH_STENCIL_DESC, ComRef<ID3D11DepthStencilState>, 1024> TDepthStencilStates;
typedef HashTableWithHistory<D3D11_BLEND_DESC, ComRef<ID3D11BlendState>, 1024> TBlendStates;
typedef HashTableWithHistory<D3D11_RASTERIZER_DESC, ComRef<ID3D11RasterizerState>, 1024> TRasterizerStates;
typedef HashTableWithHistory<D3D11_SAMPLER_DESC, ComRef<ID3D11SamplerState>, 1024> TSamplerStates; // Note there must be at least MaxSamplerStages here, otherwise items could be deleted from the used list

#endif //DX11_USE_HASHED_STATE_OBJECTS

struct CursorKey
{
  CursorKey(): _tex(0) {}
  Texture * _tex;
  PackedColor _color;
};

struct CursorInfo
{
  CursorInfo() : _handle(0) {}
  CursorKey _key;
  HCURSOR _handle;
};

}//namespace

// must be defined outside of namespace
template <>
struct FindArrayKeyTraits<DX11::CursorInfo>
{
  typedef const DX11::CursorKey &KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b)
  {
    return a._color==b._color && a._tex == b._tex;
  }
  /// get a key from an item
  static KeyType GetKey(const DX11::CursorInfo &a) {return a._key;}
};

struct LazyLoadFile;

namespace DX11
{

//=======================================================================
//=======================================================================
/**
DirectX 11 engine
*/
//=======================================================================
//=======================================================================
class EngineDD11: public Engine, public MemoryFreeOnDemandHelper
{
  friend class D3DDeviceAutoLocked<true>;
  friend class D3DDeviceAutoLocked<false>;
  friend class D3DDeviceServant;
  friend class TextBankD3D11;
  friend class TextureD3D11;

  //!{ Dynamic post effect - script interface
  friend struct PPPriorities;
  friend struct PostProcessHndl;
  friend class ReleasePPHndlFunc;
  friend class ResetPPHndlFunc;
  friend class InitPPHndlFunc;
  //!}

  friend class PushPopRenderTargetDepthStencilGuard;

#if _ARMA3_SIMUL_WEATHER
  friend class SimulCloudRendererDX11;
  friend class SimulSkyRendererDX11;
#endif

  //!{ Post process classes
  class PostProcess;
  class PPSimple;
  class PPCopy;
  class PPFinal;
  class PPRescaleBicubic;
  class PPGaussianBlur;
  class PostProcessCustomEdge;
  class PPBloom;
  class PPRain3D;
  class PPSSSM;
  class PPSSSMStencil;
#ifdef SSSMBLUR
  class PPSSSMB;
#endif
  class PPDOF;
  class PPDistanceDOF;
  class PPFilmGrain;
  class PPSSAO;
  class PPRain;
  class PPDecimate;
  class PPFinalThermal;
  class QueryManager;
  class QueryManagerSinglePass;
  class PPGlow;
  class PPStencilShadowsPri;
  class PPStencilShadowsSec;
  class PPFlareIntensity;
  class PPRotBlur;
  class PPRadialBlur;
  class PPColors;
  class PPChromAber;
  class PPWetDistort;
  class PPDynamicBlur;
  class PPColorsInversion;
  class PPStretchRect;

  class PPResources;
  class PPColorResources;
  class PPWetDistortResources;
  class PPResourcesExt;
  class PPSimpleExt;
  class PPDynBlurResources;
  class PPFilmGrainResources;
  class PPSSAOResources;
  class PPRain3DResources;

  class PPDiagDepthBuffer;

  //!}

  // @{
  class ISpecialEffect;
  class HitSpecEffect;
  class MovementSpecEffect;
  class TiredSpecEffect;
  class FireSpecEffect;
  // @}

public:
  /// info about one constant buffer
  struct ConstantBufferInfo
  {
  public:
    ConstantBufferInfo(): _size(0) {}
    /// size in bytes
    void CreateBuffer(EngineDD11* engine, int size);
    void Free();

    ComRef<ID3D11Buffer> _buffer;
    /// size of buffer in bytes
    int _size;        
  };

  /** This class serves as one constant buffer, but in fact it can contain more constant buffer of different sizes */
  class MultiSizeConstantBuffer
  {
  public:
    /// gets buffer for given size in bytes
    ComRef<ID3D11Buffer> GetBuffer(int size);
    /** creates buffers
    \param maxSize maximal size in bytes
    \param createMulti if buffers of multiple sizes should be created
    */
    void CreateBuffers(EngineDD11* engine, int maxSize, bool createMulti = false);
    bool IsMulti() const {return _bufferList.Size() > 1;}
    void Free();

  private:
    AutoArray<ConstantBufferInfo> _bufferList;
  };


  //=======================================================================
  // Core
  //=======================================================================

  EngineDD11();
  virtual ~EngineDD11();

  //! Initialization
  bool Init(HINSTANCE hInst, HINSTANCE hPrev, int sw, bool windowed, int bpp, bool generateShaders);
  //! Deinitialization
  void Done();

  /// Creates new engine and returns it (returns 0 if failed)
  static  EngineDD11* CreateEngine(CreateEnginePars &pars);
  /// Returns if DX11 engine should be used
  static bool GetUseDX11() {return UseDX11;}
  static void SetUseDX11(bool use) {UseDX11 = use;}

  enum {MaxStages=8,MaxSamplerStages=MaxSamplerStages_DX11,UsableSampleStages=15};

  //=======================================================================
  // Engine implementation
  //=======================================================================
  /// virtual initialization done after GEngine is assigned
  virtual void Init();

  //! Inform engine the last material could have been changed
  virtual void MaterialHasChanged(int cb) {CBState(cb)._lastMat = TexMaterialLODInfo((TexMaterial *)-1, 0);}

  //! Maximum number of bones the engine is capable to set at once
  virtual int GetMaxBonesCount() const {return _caps._maxBones;};
  /// set bones for skinning shader
  virtual void SetSkinningType(int cb, ESkinningType skinningType, const Matrix4 *bones = NULL, int nBones = 0);
#if _ENABLE_SKINNEDINSTANCING
  virtual void SetSkinningTypeInstance(int cb, ESkinningType skinningType, int instanceIndex, const Matrix4 *bones, int nBones);
#endif

  //! Return maximum intensity a star can have
  virtual float GetMaxStarIntensity() const;

  /// check if animated water surface is supported
  virtual bool CanSeaWaves() const {return true;}
  virtual bool CanSeaUVMapping(int waterSegSize, float &mulUV, float &addUV) const;

  virtual void SetWaterWaves(int cb, const WaterDepthQuad *water, int xRange, int zRange);
  virtual void SetWaterDepth(
    float seaLevel, float shoreTop, float shoreBottom, float peakWaveTop, float peakWaveBottom,
    int xRange, int zRange, float grid
    );


#if !_VBS3_LAND_SHADOW
  //! Setting of shadow information for one segment of terrain
  virtual void SetLandShadow(int cb, const float *shadow, int xRange, int zRange, float grid);
#endif

  virtual void SetInstanceInfo(int cb, ColorVal color, float shadowIntensity, bool optimize = true);
  virtual ColorVal GetInstanceColor(int cb) const {return CBState(cb)._instanceColor;}
  virtual float GetInstanceShadowIntensity(int cb) const {return CBState(cb)._instanceShadowIntensity;}

  //! Sets the modulate color
  virtual void SetModulateColor(const Color &modColor) {_modColor = modColor;}

  //@{ script driven postprocess effects
  /// reset all scripted postprocess effects into default settings
  virtual void ResetGameState();
  virtual LSError SerializeGameState(ParamArchive &ar);
  //@}

  //! Enable SSAO post process effect
  virtual void EnableSSAO(bool enable);
  //! Enable film grain post process effect
  virtual void EnableFilmGrain(bool enable);
  //! Enable radial blur post process effect
  virtual void EnableRadialBlur(bool enable);
  //! Enable chromatic aberration post process effect
  virtual void EnableChromAberration(bool enable);
  //! Enable wet distortion post process effect
  virtual void EnableWetDistortion(bool enable);
  //! Enable color corrections post process effect
  virtual void EnableColorCorrections(bool enable);
  //! Enable dynamic blur post process effect
  virtual void EnableDynamicBlur(bool enable);
  //! Enable color inversion post effect
  virtual void EnableClrInvers(bool enable);

  //@{ start commit for post process
  virtual void CommitSSAO(float time);
  virtual void CommitRadBlur(float time);
  virtual void CommitFilmGrain(float time);
  virtual void CommitChromAberration(float time);
  virtual void CommitWetDistortion(float time);
  virtual void CommitColorCorrections(float time);
  virtual void CommitDynamicBlur(float time);
  virtual void CommitClrInvers(float time);
  //@}

  //@{ check state of post process
  virtual bool CommittedSSAO() const;
  virtual bool CommittedRadBlur() const;
  virtual bool CommittedFilmGrain() const;
  virtual bool CommittedChromAberration() const;
  virtual bool CommittedWetDistortion() const;
  virtual bool CommittedColorCorrections() const;
  virtual bool CommittedDynamicBlur() const;
  virtual bool CommittedClrInvers() const;
  //@}

  //! Set radial blur post process parameters
  virtual void SetRadialBlurrParams(float pwrX, float pwrY, float offsX, float offsY);
  //! Set wet distortion params
  virtual void SetWetDistortionParams(float blur, float pwrX, float pwrY, float sh1, float sh2, float sv1, float sv2,
    float ah1, float ah2, float av1, float av2,float rndX, float rndY, float posX, float posY);
  //! Set chromatic aberration parameters
  virtual void SetChromaticAberrationParams(float aberationPowerX, float aberationPowerY, bool value);
  //! Set SSAO parameters
  virtual void SetSSAOParams(const float *params, int nums);
  //! Set film grain parameters
  virtual void SetFilmGrainParams(const float *params, int nums);
  //! Set color correction parameters
  virtual void SetColorCorrectionsParameters(float brightness, float contrast, float offset, Color blendClr, Color colClr, Color weights);
#if _ARMA3_RADIAL_COLOR_PP
  virtual void SetColorCorrectionsParametersRadial(float radialMajorAxisRadius, float radialMinorAxisRadius, float radialRotationDeg, 
    float radialCenterX, float radialCenterY, float radialInnerRadiusCoef, float radialInterpCoef);
#endif
  //! Set dynamic blur parameters
  virtual void SetDynamicBlurParameters(float bluriness);
  //! Set inversion parameters
  virtual void SetClrInversParams(Color invColor);
  /// set texture used for rain postprocess effect
  virtual void SetRainTexture(Texture *tex);
  //! Set density of rain postprocess effect
  virtual void SetRainDensity(float density);

  /// some diagnostic rendering is not compatible with background recording
  virtual void FlushBackgroundScope();
  /// perform any postprocessing effects as necessary
  virtual void Postprocess(float deltaT, const PostFxSettings &pfx);
  /// indicate no postprocess effects will be ever performed
  virtual void NoPostprocess();

  virtual void CaptureRefractionsBackground();
#if _ARMA3_REFRACTION
  /// command buffer version
  virtual void CaptureRefractionsBackground(int cb);
#endif

  //! Create post effect on the basis of PostEffectType
  virtual int CreatePostprocess(PostEffectType pFxType, int priority);
  virtual int CreatePostprocess(RString peTypeName, int priority);
  //! Destroy post effect specified by handle
  virtual void DestroyPostprocess(int hndl);
  //! Enable / disable post effect
  virtual void EnablePostprocess(int hndl, bool enable);
  //! Set parameters
  virtual int SetPostprocessParams(int hndl, AutoArray<float> &pars);
  virtual int SetPostprocessParams(int hndl, const float* pars, int nPars);
  //! Commit post effect pars
  virtual void CommitPostprocess(int hndl, float time);
  //! check state of post process
  virtual bool CommittedPostprocess(int hndl);
  /// returns if postprocess can be enabled (= if postprocess can be enabled with current postprocess quality level settings)
  virtual bool CanPostprocessBeEnabled(int hndl);


  //! Function to calculate flare intensity at specified coordinates (in <-1,1> space) into texture,
  //! and to set the texture at stage 1 (function takes advantage of the post process effect)
  virtual void CalculateFlareIntensity(int x, int y, int sizeX, int sizeY);

  /// check if we can draw (handle lost device)
  virtual AbilityToDraw IsAbleToDraw(bool wantReset);
  /// check if we can draw (handle lost device), only return known status, never call any D3D API
  virtual bool IsAbleToDrawCheckOnly();

  virtual void InitDraw( bool clear=false, Color color = HBlack ); // Begin scene
  virtual void InitNoDraw();
  virtual void FinishNoDraw(int wantedDurationVSync);
  virtual void FinishDraw(int wantedDurationVSync, bool allBackBuffers = false, RString fileName = RString());
  virtual void FinishPresent();
  virtual void FinishFinishPresent();
  virtual bool PresentIsAsync() {return _hlRecording;}

  bool IsMipLoadingAsync() const;

  virtual void NextFrame(bool doPresent); // swap frames - get ready for next frame
  virtual bool InitDrawDone();

  // make sure a copy of the current backbuffer copy is present in the whole swap chain
  virtual void BackBufferToAllBuffers();

  //@{ get timing  suitable for animation
  virtual float GetSmoothFrameDuration() const;
  virtual float GetSmoothFrameDurationMax() const;
  //#}

  virtual void Pause();
  virtual void Restore();
  virtual void StopAll();

  virtual bool SwitchRes(int x, int y, int w, int h, int bpp);
  virtual bool SwitchRefreshRate(int refresh);
  virtual bool SwitchWindowed(bool windowed, int w=0, int h=0, int bpp=0);
  virtual void KeepAspect(int &newW, int &newH);
  virtual void SetAspect(int newW, int newH);

  virtual void BeginOptionsVideo();
  virtual void EndOptionsVideo();
  virtual bool IsInOptionsVideo() const {return _inVideoOptions;}

  virtual void ListResolutions(FindArray<ResolutionInfo> &ret); // result is zero terminated w,h pairs of valid resolutions
  virtual void ListRefreshRates(FindArray<int> &ret); // result is zero terminated w,h pairs of valid resolutions

  virtual HDRPrec HDRPrecision() const {return _hdrPrecWanted;}
  virtual void SetHDRPrecision(HDRPrec hdrPrec);
  virtual int SupportedHDRPrecision() const {return _hdrPrecSupported;}

  virtual void Clear(bool clearZ=true, bool clear=true, PackedColor color=PackedColor(0), bool clearStencil=true);

  virtual Ref<VertexBuffer> CreateVertexBuffer(const Shape &src, VBType type, bool frequent);
  virtual void RefreshBuffer(const VertexTable &sMesh);
  virtual void DeferVBufferRelease( VertexBuffer *buffer );

  virtual void ReleaseAllVertexBuffers();

  virtual int AFrameTime() const {return FrameTime();}

  virtual void SetGamma( float g );
  virtual float GetGamma() const {return _gamma;} 

  virtual RECT GetMonitorResolution();

  virtual void SetTextureQuality(int value);
  virtual int GetTextureQuality() const {return _textureQuality;}
  virtual int GetMaxTextureQuality() const;

  virtual void SetTextureMemory(int value);
  virtual int GetTextureMemory() const {return _textureMemory;}
  virtual int GetMaxTextureMemory() const;
  virtual int GetTextureMemoryDefault() const;

  virtual void SetFSAA(int quality);
  virtual int GetFSAA() const;
  virtual int GetMaxFSAA() const;
  /// gets FSAA info.If this info cannot be get, returns false
  virtual bool GetFSAAInfo(int fsaaMode, FSAAInfo &info);

  /// for some changes pushbuffers need to be flushed
  virtual void FlushPushBuffers();

  virtual void SetAnisotropyQuality(int quality);
  virtual int GetAnisotropyQuality() const;
  virtual int GetMaxAnisotropyQuality() const;

  virtual void SetPostprocessEffects(int quality);
  virtual int GetPostprocessEffects() const;

  virtual float GetPreHDRBrightness() const;

  virtual float GetDefaultGamma() const {return 1.0f;}
  virtual float GetDefaultBrightness() const {return 1.0f;}

  virtual void SetFillrateControl( ResolutionInfo info );
  virtual ResolutionInfo GetFillrateControl() const;
  virtual float GetDefaultFillrateControl(float &fMin, float &fMax) const;
  virtual void SetDefaultFillrateControl();
  virtual void ListFillrateResolutions(FindArray<ResolutionInfo> &ret);

  /// we want to use less than 1 bit
  virtual float GetHDRFactor() const;

  virtual int GetDeviceLevel() const;
  virtual int GetCurrentDeviceLevel() const;
  virtual void SetDefaultSettingsLevel(int level);

  virtual void LoadConfig(ParamEntryPar cfg,ParamEntryPar fcfg, const char *section) override;
  virtual void SaveConfig(ParamFile &cfg, const char *section) override;

  virtual void FlushQueues();

  virtual void SetAlphaFog(int cb, float start, float end)
  {
    CBState(cb)._aFogStart=start,CBState(cb)._aFogEnd=end;
    CBState(cb)._applyAplhaFog = start<end;
  }

  virtual void GetZCoefs(int cb, float &zAdd, float &zMult);
  int GetBias(int cb){return CBState(cb)._bias;}
  virtual void SetBias( int cb, int value );

  //! This function name doesn't correspond to what it is actually doing. Check usage of this function (it is used to disable stencil shadows drawing)
  virtual bool ZBiasExclusion() const
  {
    return !_caps._hasStencilBuffer;
  }

  virtual AbstractTextBank *TextBank() {return _textBank;}

  virtual void Activate();
  virtual bool Deactivate();
  virtual void Screenshot(RString filename);

  //@{ video interface
  virtual VideoPlayback *VideoOpen(const char *path, float volume);
  virtual void VideoClose(VideoPlayback *video);
  virtual void VideoFrame(VideoPlayback *video);
  //@}


  void DrawPolyPrepare(const MipInfo &mip, int specFlags=DefSpecFlags2D);
  virtual void DrawPolyDo
    (
    const Vertex2DAbs *vertices, int nVertices,
    const Rect2DAbs &clip=Rect2DClipAbs
    );
  virtual void DrawPolyDo
    (
    const Vertex2DPixel *vertices, int nVertices,
    const Rect2DPixel &clip=Rect2DClipPixel
    );

  virtual void DrawPoly3D(const PositionRender &space, const MipInfo &mip, const Vertex2DFloat *vertices, int nVertices, const Rect2DFloat &clip, int specFlags, const TLMaterial &matType = TLMaterial::_default, const Pars3D *pars3D=NULL);

  virtual void PrepareTriangle( int cb, const MipInfo &absMip, const TexMaterial *mat, int specFlags );
  virtual void PrepareTriangleTL(
    int cb, const PolyProperties *section,
    const MipInfo &mip, const TexMaterialLODInfo &mat, int specFlags,
    const TLMaterial &tlMat, int spec, const EngineShapeProperties &prop
    );

  virtual void DrawPolygon(const VertexIndex *ii, int n);
  virtual void DrawSection(const FaceArray &face, Offset beg, Offset end);

  virtual bool GetTL() const {return true;}
  virtual bool HasWBuffer() const {return false;}

  virtual float GetLastKnownAvgIntensity() const;
  virtual void ForgetAvgIntensity();

  // engine w-buffer interface
  virtual bool IsWBuffer() const;
  virtual bool CanWBuffer() const;
  virtual void SetWBuffer(bool val);

  virtual bool CanTwoSidedStencil() const {return _caps._canTwoSidedStencil;}

  virtual void ProjectionChanged(int cb);
  virtual void EnableSunLight(int cb, EMainLight mainLight);
  virtual void SetFogMode(int cb, EFogMode fogMode);

  virtual int ShapePropertiesNeeded(const TexMaterial *mat) const;
  virtual bool BeginMeshTLInstanced(int cb, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias, float minDist2, const DrawParameters &dp);

  virtual void SetupInstances(int cb, const ObjectInstanceInfo *instances, int nInstances);
  virtual void SetupInstancesSprite(int cb, const ObjectInstanceInfo *instances, int nInstances);
  virtual void SetupInstancesVolCloud(int cb, const ObjectInstanceInfo *instances, int nInstances);

  virtual void DrawMeshTLInstanced(
    int cb, const Shape &sMesh, const SectionMaterialLODs &matLOD, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias,
    const ObjectInstanceInfo *instances, int nInstances, float minDist2, const DrawParameters &dp
    );
  virtual void DrawShadowVolumeInstanced(
    int cb, LODShape *shape, int level, const ObjectInstanceInfo *instances, int instanceCount, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
    , int drawListId
#endif
    );

  virtual void BeginShadowVolume(int cb, float shadowVolumeSize);
  virtual void BeginShadowVolumeRendering();
  virtual void EndShadowVolumeRendering();

  virtual void BeginInstanceTL(int cb, const PositionRender &modelToWorld, float minDist2, int spec, const LightList &lights, const DrawParameters &dp);
  virtual bool BeginMeshTL(int cb, const VertexTableAnimationContext *animContext, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias); // convert all mesh vertices
  virtual void EndMeshTL(int cb, const Shape &sMesh ); // forget mesh

  virtual IVisibilityQuery *BegPixelCounting(IVisibilityQuery *query);
  virtual void EndPixelCounting(IVisibilityQuery *query);

  virtual bool IsCursorSupported() const;
  virtual bool IsCursorShown() const;
  virtual void SetCursor(Texture *cursor, int x, int y, float hsX, float hsY, PackedColor color, int shadow);
  virtual void UpdateCursorPos(int x, int y);
  virtual void CursorOwned(bool owned);

  virtual bool IsCBRecording() const {return _d3dDeviceNonRec._cbCount>0 || _backgroundD3D==RecLowLevel;}
  virtual bool IsInCBScope() const;

  virtual int StartCBRecording(int resultIndex, int thrId, int debugId);
  virtual void StopCBRecording(int cb, int resultIndex, const char *name);

  virtual void StartCBScope(int numThreads, int nResults);
  virtual void EndCBScope();

  virtual bool CopyCBPredicatedScope(PredicatedCBScope &scope, PredicationMode pred, bool canDestroy, bool canAdvance);
  virtual bool StartCBPredicatedScope(PredicatedCBScope &scope, PredicationMode pred);
  virtual void EndCBPredicatedScope(PredicatedCBScope &scope);

  virtual bool Allow3DUI() const;

  virtual bool IsSBPossible() const {return true;}
  virtual void EnableSBShadows(int quality);
  virtual bool IsSBEnabled() const;
  virtual void CommitSettings();

  /// because of predication, we need some shadow parameters to be prepared before priming begins
  virtual void PrepareShadowMapRendering(float sunClosestDistanceFrom0, int layerIndex, float shadowsZ, bool firstLayer);

  virtual void BeginShadowMapRendering(float sunClosestDistanceFrom0, int layerIndex, float shadowsZ, bool firstLayer, bool rainMap) override;
  virtual void EndShadowMapRendering(float minShadowsZ, float maxShadowsZ, bool firstLayer, bool lastLayer, bool rainMap) override;
  virtual void RenderStencilShadows();

  //!{ Methods to control the depth of field effect
  /// some engines may be able to implement depth of field
  virtual void EnableDepthOfField(float focusDist, float blur, bool farOnly);
  virtual void EnableDepthOfField(int quality);
  virtual bool IsDepthOfFieldEnabled() const;
  virtual void BeginDepthMapRendering(bool zOnly
#if _HELISIM_RENDER_TO_TEXTURE
    , bool useR2T
#endif  
    );
  virtual void EndDepthMapRendering(bool zOnly);
  virtual void SetDepthClip(float zClip);
  virtual void SetDepthRange(float minZ = DepthBorderCommonMin, float maxZ = DepthBorderCommonMax);
  //!}

#if _ARMA3_CAUSTICS
  virtual void BeginCausticsRendering();
  virtual void EndCausticsRendering();
#endif

  //! Pass the temperature table into the GPU
  virtual void SetTemperatureTable(const float *table, int gSize, int dotSize, int kSize);
  //! Relative setting of TI brightness (black-hot modes are considered)
  virtual void SetTIBrightnessRelative(float brightness);
  //! Chek if texture update needed
  virtual bool TemperatureTableUpdateNeeded(const float *table, int length);
  //!{ TI Brightness and Contrast wrappers
  virtual void SetTIBlur(float blur) {saturate(blur, 0.0f, 1.0f); _tiBlurCoef = blur;}
  virtual float GetTIBlur() const {return _tiBlurCoef;}
  virtual void SetTIBrightness(float brightness) {saturate(brightness, TempMin, TempMax); _tiBrightness = brightness;}
  virtual float GetTIBrightness() const {return _tiBrightness;}
  virtual void SetTIContrast(float contrast) {saturate(contrast, 1.0f, ContrastMax); _tiContrast = contrast;}
  virtual float GetTIContrast() const {return _tiContrast;}
  virtual void SetTIMode(int mode) {saturate(mode, 0, TIConversionDimensionH - 1); _tiMode = mode;}
  virtual int GetTIMode() const {return _tiMode;}
  virtual void SetTIAutoBC(bool autoBC) {_tiAutoBC = autoBC;}
  virtual bool GetTIAutoBC() const {return _tiAutoBC;}
  virtual void SetTIAutoBCContrastCoef(float contrastCoef) {saturate(contrastCoef, 0.1f, 5.0f); _tiAutoBCContrastCoef = contrastCoef;}
  virtual float GetTIAutoBCContrastCoef() const {return _tiAutoBCContrastCoef;}
  virtual void LoadTIConversionTexture(RString name);
  //!}

  //! Method to fill out the PS constants with craters properties
  virtual void UseCraters(int cb, const AutoArray<CraterProperties> &craters, int maxCraters, const Color &craterColor);
  //! Method to return the maximum number of available craters
  virtual int GetMaxCraters() const {return MAX_CRATERS;}

  virtual void DrawSectionTL(int cb, const Shape &sMesh, int beg, int end, int bias, int instancesOffset = 0, int instancesCount = 0, const DrawParameters &dp = DrawParameters::_default);
  virtual void DrawStencilShadows(bool interior, int quality);
  virtual void DrawDecal(int cb, Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color, const MipInfo &mip, const TexMaterial *mat, int specFlags);

  virtual void DrawSSAO();

  virtual void Draw2D(const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip);
  virtual void Draw2D(const Draw2DPars &pars, const Rect2DAbs &rect, const Rect2DAbs &clip=Rect2DClipAbs);
  virtual void Draw2D(const Draw2DParsNoTex &pars, const Rect2DAbs &rect, const Rect2DAbs &clip=Rect2DClipAbs);

  virtual void DrawLinePrepare();
  virtual void DrawLineDo(const Line2DAbs &line, PackedColor c0, PackedColor c1, const Rect2DAbs &clip=Rect2DClipAbs);

  virtual void DrawText3DCenteredAsync(
    Vector3Par center, Vector3Par up, Vector3Par dir, ClipFlags clip,
    Font *font, PackedColor color, int spec, const char *text,
    float x1c = 0, float y1c = 0, float x2c = 1e6, float y2c = 1
    );

  virtual void PerformTaskOnMainThread(MainThreadRenderTask * task);

  virtual void FogColorChanged(ColorVal fogColor) override;
  virtual void EnableNightEye(float night);
  virtual void SetZPrimingDone(int cb, bool zPrimingDone);

  virtual void PrepareMesh( int cb, int spec ); // prepare internal variables
  virtual bool IsDummy() override { return false; }
  virtual bool BeginMesh( int cb, TLVertexTable &mesh, int spec ); // convert all mesh vertices
  virtual void EndMesh( int cb, TLVertexTable &mesh ); // forget mesh

  virtual void SetDebugMarker(int id);
  virtual int ProfileBeginGraphScope(unsigned int color,const char *name) const;
  virtual int ProfileEndGraphScope() const;

  virtual float ZShadowEpsilon() const {return 0.01;}
  virtual float ZRoadEpsilon() const {return 0.005;}
  virtual float ObjMipmapCoef() const {return 1.5;}
  virtual float LandMipmapCoef() const {return 1.0;}

  virtual void FlushMemory(bool deep, bool allowAutoDeep);
  virtual void RequestFlushMemory();

#if _ENABLE_CHEATS
  virtual RString GetStat(int statId, RString &statVal, RString &statVal2);
#endif
#ifdef TRAP_FOR_0x8000
  virtual void TrapFor0x8000(const char *str);
#endif

  virtual int Width() const {return _wRT;}
  virtual int Height() const {return _hRT;}
  virtual int PixelSize() const {return _pixelSize;} // 16 or 32 bit mode?
  virtual int RefreshRate() const {return _swapChainDesc.BufferDesc.RefreshRate.Numerator;}
  virtual bool CanBeWindowed() const {return true;}
  virtual bool IsWindowed() const {return _windowed;}

  virtual int WidthBB() const {return _w;}
  virtual int HeightBB() const {return _h;}

  virtual int MinGuardX() const {return _minGuardX;}
  virtual int MaxGuardX() const {return _maxGuardX;}
  virtual int MinGuardY() const {return _minGuardY;}
  virtual int MaxGuardY() const {return _maxGuardY;}

  virtual int MinSatX() const {return _minGuardX;}
  virtual int MaxSatX() const {return _maxGuardX;}
  virtual int MinSatY() const {return _minGuardY;}
  virtual int MaxSatY() const {return _maxGuardY;}

  virtual void DrawLine(const TLVertexTable &mesh, int beg, int end, int orFlags=0);

  virtual void SetSpecialEffectPars(int index, const float *pars, int nPars);

  virtual RString GetDebugName() const;

  /// Used to determine if colors should be reversed during construction of color textures (TextureSourceColor)
  virtual bool ReversedColors() const {return true;}

  /// called when app is activated
  virtual void OnAppActivated();

  /** sets underwater params
  \param occlusionDistance - distance of the underwater occlusion object
  \param underwaterDepth - depth under water (negative values = distance above water)
  \param waterDepthFogStart - depth where fog should start
  \param fogColor - color of the water fog
  */
  virtual void SetUnderwaterPars(float occlusionDistance, float underWaterDepth, float waterDepthFogStart, const Color& fogColor);

#if _ARMA3_REFRACTION
  /** sets parameters for refractions
  \param intensity intensity of effect
  */
  virtual void SetRefractionPars(float intensity, const Vector3& scales, const Vector3& timeCoefs);
#endif
#if _ARMA3_REFRACTION_SPRITES
  /** sets parameters for refractions of sprites
  \param intensity intensity of effect
  \param scales scales of refraction (r, g, b)
  \param timeCoefs time coefficients for animation
  */
  virtual void SetSpriteRefractionPars(float intensity, const Vector3& scales, const Vector3& timeCoefs);
#endif


#if _ARMA3_SIMUL_WEATHER
  virtual SimulWeatherBase* CreateSimulWeather();
  void UpdateSimulWeatherTexture(TextureSysGPUDX11 *tex, unsigned textureIndex);

  virtual int GetSWGeometryLayersCount() const { return _swLayersCount; }
  virtual int GetSWGeometryDefault() const { return DefaultSWLayers; }
  virtual void SetSWGeometryLayersCount(int layers);

#endif

#if _ARMA3_DX11_PROFILING
  virtual void PerfReport() { ProfileReport();}
#endif
  //=======================================================================
  // MemoryFreeOnDemandHelper implementation
  //=======================================================================

   virtual float Priority() const;
   virtual size_t MemoryControlled() const;
   virtual size_t MemoryControlledRecent() const;
   virtual size_t FreeOneItem();
   virtual void MemoryControlledFrame();

  //=======================================================================
  // other public functions
  //=======================================================================

  TextBankD3D11 *TextBankDD() const {return _textBank;}
  void InitVRAMPixelFormat( DXGI_FORMAT &pf, PacFormat format, bool isSRGB );

  void DDError11( const char *text, int err );
  void ReportGRAM(const char *name);

  /// gets D3D device, that can be used only when not recording
  MyD3DDevice &GetD3DDeviceNonRec()  {return _d3dDeviceNonRec;}
  const MyD3DDevice &GetD3DDeviceNonRec() const {return _d3dDeviceNonRec;}
  /// gets D3D device, that can be used even when recording
  MyD3DDeviceCB &GetD3DDeviceRec() {return _d3dDeviceRec;}
  const MyD3DDeviceCB &GetD3DDeviceRec() const {return _d3dDeviceRec;}

  int AlignVRAMTex(int size) const {return (size+_caps._texAlignment-1)&~(_caps._texAlignment-1);}
  int AlignVRAMVB(int size) const {return (size+_caps._vbAlignment-1)&~(_caps._vbAlignment-1);}
  int AlignVRAMVBSigned(int size) const {return size>=0 ? AlignVRAMVB(size) : -AlignVRAMVB(-size);}

  void RequestFlushMemoryCritical();

  void AddLocalVRAMAllocated(int size);
  void AddIBuffersAllocated(int n);
  void AddVBuffersAllocated(int n, bool dynamic);

  void D3DTextureDestroyed( SurfaceInfoD3D11& surface );

  bool Can565() const {return _caps._can565;}
  bool Can88() const {return _caps._can88;}
  bool Can8888() const {return _caps._can8888;}
  int MaxTextureSize() const {return _caps._maxTextureSize;}

  int DXTSupport() const {return _caps._dxtFormats;}
  bool CanDXT( int i ) const {return (_caps._dxtFormats&(1<<i))!=0;}

  bool GetHWTL() const {return true;}
 
  void TextureDestroyed( Texture *tex );

  struct CheckMemTypeScope
  {
#if _ENABLE_CHEATS
#define TRACK_VRAM_ALLOC 0
    //#define TRACK_VRAM_ALLOC 1
#endif
#if TRACK_VRAM_ALLOC
    EngineDD11 *_engine;
    int _local,_nonlocal,_size;
    const char *_text;
#endif

    CheckMemTypeScope(EngineDD11 *engine, const char *text, int size)
    {
#if TRACK_VRAM_ALLOC
      _engine = engine;
      _text = text;
      _size = size;
      _engine->CheckFreeTextureMemory(_local,_nonlocal);
#endif
    }

    ~CheckMemTypeScope()
    {
#if TRACK_VRAM_ALLOC
      int local,nonlocal;
      _engine->CheckFreeTextureMemory(local,nonlocal);

      LogF("%s - %d: loc %d,nonloc %d",_text,_size,_local-local,_nonlocal-nonlocal);
#endif
    }
  };

  /// total VRAM (local+nonlocal) allocation limit used for static VRAM management
  int GetVRAMAllocLimit() const {return _caps._vramAllocLimit;}
  int GetLocalVramAmount() const {return _caps._localVramAmount;}
  bool GetDynamicVBNonLocal() const {return _caps._dynamicVBNonLocal;}

  /// get memory status (used in case of out of memory errors)
  const char *GetMemErrorStatus() const;

  int GetVBAlignment() const {return _caps._vbAlignment;}
  int GetTexAlignment() const {return _caps._texAlignment;}


  int GetFreeLocalVramAmount() const {return _freeLocalVramAmount;}
  int GetFreeNonlocalVramAmount() const {return _freeNonlocalVramAmount;}

  int GetNonLocalVramAmount() const {return _caps._nonlocalVramAmount;}
  void SetVBuffersStatic(bool val){_caps._vbuffersStatic = val;}
  bool GetVBuffersStatic() const {return _caps._vbuffersStatic;}

  bool UseLocalPrediction() const {return _caps._localPrediction;}


    //@{ VRAM management - vertex buffers only
  size_t ReleaseVideoMemory(size_t size, int minAge);
  size_t ThrottleVRAMAllocation(size_t limit, int minAge);
  size_t UsedVRAM() const;
  /// VRAM discard metrics for current discard candidate
  float VRAMDiscardMetrics() const;
  int VRAMDiscardCountAge(int minAge, int *totalSize=NULL) const;
  /// amount of memory older than 1 frame
  int VRAMDiscardMemoryOld() const 
  {
    return _vBufferLRU.MemoryControlled()-_vBufferLRU.MemoryControlledRecent();
  }
  int VRAMMemoryTotal() const
  {
    return _vBufferLRU.MemoryControlled();
  }
  int VRAMMemoryRecent() const 
  {
    return _vBufferLRU.MemoryControlledRecent();
  }
  //@}


  //! Setting of texgen scale and offset (usually called when setting the VB)
  void SetTexGenScaleAndOffset(int cb, const UVPair *scale, const UVPair *offset);
  /// helper: used from VertexBufferD3D11::Update
  void SetVSourceStatic(int cb, const Shape &src, const EngineShapeProperties &prop, const VertexBufferD3D11 &buf, VSDVersion vsdVersion);
  void SetVSourceDynamic(
    int cb, const VertexTableAnimationContext *animContext, const Shape &src, const EngineShapeProperties &prop, const VertexBufferD3D11 &buf
    );

  // access to _statVBuffer
  template <int vertexDecl>
  Ref<VertexStaticData11> AddShapeVertices(const Shape &src, VertexBufferD3D11 &vbuf, bool dynamic, bool separate, bool frequent);
  template <int vertexDecl>
  Ref<VertexStaticData11> AddShapeVerticesInstanced(const Shape &src, VertexBufferD3D11 &vbuf, bool dynamic, bool separate, bool frequent, int instancesCount);

  Ref<VertexStaticData11> AddShapeVerticesSpriteInstanced(const Shape &src, VertexBufferD3D11 &vbuf, bool dynamic, bool separate, bool frequent, int instancesCount);
  Ref<VertexStaticData11> AddShapeVerticesPoint(const Shape &src, VertexBufferD3D11 &vbuf, bool dynamic, bool separate, bool frequent);
#if _ENABLE_SKINNEDINSTANCING
  VertexStaticData11 *AddShapeVerticesSkinnedInstanced(const Shape &src, bool dynamic, bool separate, bool frequent, int instancesCount);
  VertexStaticData11 *AddShapeVerticesNormalSkinnedInstanced(const Shape &src, bool dynamic, bool separate, bool frequent, int instancesCount);
  VertexStaticData11 *AddShapeVerticesShadowVolumeSkinnedInstanced(const Shape &src, bool dynamic, bool separate, bool frequent, int instancesCount);
#endif

  Ref<IndexStaticData11> AddShapeIndices(const FaceArray &src, bool dynamic, int nInstances, int instanceVertices);
  Ref<IndexStaticData11> AddShapeIndicesSprite(const FaceArray &src, bool dynamic, int nInstances);
  Ref<IndexStaticData11> AddShapeIndicesPoint(const FaceArray &src, bool dynamic, int nInstances, int instanceVertices);

  void DeleteVB(VertexStaticData11 *vb);
  void DeleteIB(IndexStaticData11 *ib);

  bool CanCreateBuffersAsync() const;

  void BufferReleased(VertexBufferD3D11 *buf);
  void OnSizeChanged(VertexBufferD3D11 *buf);

  int MaxInstances() const {return _caps._maxInstances;}
  int MaxInstancesSprite() const {return _caps._maxInstancesSprite;}
  int MaxInstancesShadowVolume() const {return _caps._maxInstancesShadowVolume;}

  /// recover from memory allocation failure
  size_t FreeOnDemandSystemMemoryLowLevel(size_t toRelease) const;

  HRESULT Map(ID3D11Resource *resource, UINT subResource, D3D11_MAP mapType, UINT mapFlags, void **ppData);
  void Unmap(ID3D11Resource *resource, UINT subResource);

  bool IsLLRecording() const {return _backgroundD3D==RecLowLevel;}
  bool IsRecording() const {return _backgroundD3D!=RecNone;}

  //@{ only set values, do not apply it yet
  bool SetFSAAWanted(int quality);
  bool SetHDRPrecisionWanted(HDRPrec prec);
  bool SetResolutionWanted(int w, int h, int bpp);
  bool SetPostprocessEffectsWanted(int quality);
  bool SetFillrateLevelWanted(int level);
  bool SetFillrateControlWanted(ResolutionInfo info);
  bool SetVerticalSynchronization(bool vsync);
  //@}
  bool SetTextureQualityWanted(int value);
  bool SetTextureMemoryWanted(int value);

  void SetVSync(bool vsync);
  bool GetVSync() { return _vsync; };

  HDRPrec ValidateHDRPrecision(HDRPrec prec) const;

  bool ResetNeeded() const {return FAILED(_resetStatus);}

  static void ReportShaderErrors(ID3D10Blob* errorMsgs);

  bool GetRenderTargetDimensions(const RenderTarget& rt, int &outWidth, int &outHeight) const;
  bool GetDepthStencilDimensions(const DepthStencil& ds, int &outWidth, int &outHeight) const;
  bool GetShaderResourceDimensions(TextureHandle11* sr, int &outWidth, int &outHeight) const;

#if SAFE_RESOURCE_UNSET
  void BGAddResourceToRelease(ID3D11DeviceChild *item){if (item) _bgResToRelease.Add(item);}
  void BGAddTextureHandleToRelease(TextureHandle11* item){if (item) _bgTexHandleToRelease.Add(item);}
#endif
  void BGAddRefToRelease(RefCount *item);
  void BGReleaseResourcesAndTextures();
  void BGReleaseRefs();

  /// should we use a predication for rendering?
  bool UsePredication() const;
  /// callback wrapper around a member function
  static DWORD WINAPI BGThreadCallback(void *param) {((EngineDD11 *)param)->BGThread();return 0;}
  static DWORD WINAPI VisThreadCallback(void *param) {((EngineDD11 *)param)->VisThread();return 0;}

  /** creates surface
  \param createSRGBView if srgb resource view should be created
  \param createRTView if render target view should be created
  */
  HRESULT CreateTexture(UINT width, UINT height,
    UINT mipLevels,
    DXGI_FORMAT format,
    D3D11_USAGE usage, 
    SurfaceDataD3D11& result,
    bool createSRGBView,
    bool createRTView                                 
    );

  /** simple creation of 2d texture */
  HRESULT CreateTextureSimple2D(UINT width, UINT height,
    UINT mipLevels,
    DXGI_FORMAT format,
    D3D11_USAGE usage,
    UINT cpuAccessFlags,
    SurfaceDataD3D11& result);

  HRESULT EngineDD11::CreateTextureSimple2D(const D3D11_TEXTURE2D_DESC& desc,
    SurfaceDataD3D11& result);

  /** simple creation of 3d texture */
  HRESULT CreateTextureSimple3D(UINT width, UINT height, UINT depth,
    UINT mipLevels,
    DXGI_FORMAT format,
    D3D11_USAGE usage,
    UINT cpuAccessFlags,
    SurfaceDataD3D11& result);

  HRESULT EngineDD11::CreateTextureSimple3D(const D3D11_TEXTURE3D_DESC& desc,
    SurfaceDataD3D11& result);

  /// gets format support enum for given format
  UINT GetFormatSupport(DXGI_FORMAT format) ;
private:
  /// current 2D/3D preference
  enum TLMode
  {
    /// 2D rendering
    TLDisabled,
    /// 3D rendering
    TLEnabled,
    /// 3D view space rendering - post-process effects
    TLViewspace
  };

private:
  enum
  {
    SHP_VertexShaderID      = NVertexShaderPoolID,
    SHP_UVInput             = 2,
    SHP_MainLight           = 4,
    SHP_RenderingMode       = NRenderingMode,
    SHP_SkinningInstancing  = 3,
  };

  enum
  {
    // our shaders are ready only up to 50
#if _ENABLE_SKINNEDINSTANCING
    LimitBones = 256,
#else
    LimitBones = 60,
#endif
    LimitInstances = 33,
    LimitInstancesSprite = 60,
    LimitInstancesShadowVolume = 60,
  };

  typedef Engine base;
  typedef AutoArray<Matrix4, MemAllocLocal<Matrix4, LimitBones, AllocAlign16> > BonesType;

  enum CleanUpRequest
  {
    CleanUpNone,
    CleanUpNeeded,
    CleanUpForced,
  };

  //========================================================
  // Member variables
  //========================================================
  struct SPP
  {
    Ref<HitSpecEffect> _hit;
    Ref<MovementSpecEffect> _movement;
    Ref<TiredSpecEffect> _tired;
    Ref<FireSpecEffect> _fire;
  } _spp;

  /// global flag - if DX11 should be used
  static bool UseDX11;

  bool _initialized;

  //@{ back buffer dimensions
  int _w,_h;
  //@}
  //@{ render target dimensions
  /** often this is the same as back buffer, but sometimes we prefer to use rescaling */
  int _wRT,_hRT;
  //@}
  //@{ viewport dimensions
  int _viewW,_viewH;
  //@}

  //@{ max. RT size as set by the user
  int _wRTPreferred,_hRTPreferred;
  int _wRTRounded,_hRTRounded; // as selected by FindFillrateControl
  //@}

  //@{ remember windowed and fullscreen settings - needed in case we want to switch between them
  int _winX,_winY,_winW,_winH;
  int _WindowAspectW,_WindowAspectH;
  int _fullW,_fullH;
  //@}

  int _pixelSize; // 16 or 32 bit mode?
  int _depthBpp; // 16 or 32 bit mode?
  int _refreshRate;

  // max. render ahead value as allowed by the user
  int _framesAhead;
  // max. value render ahead value as detected by the game
  int _framesAheadDetected;

  /// performance level of the current device
  int _deviceLevel;

  /// used to detect a device change
  RString _lastDeviceId;

  /// swap chain description
  DXGI_SWAP_CHAIN_DESC _swapChainDesc;

  /// feature level
  D3D_FEATURE_LEVEL _featureLevel;

  //! Flag to determine whether NVPerfHUD is active or not
  bool _nvPerfHUDActive;

  //! Engine's driver type
  D3D_DRIVER_TYPE _devType;

  /// format of the primary render target
  DXGI_FORMAT _rtFormat;

  /// format of the depth/stencil buffer
  DXGI_FORMAT _depthFormat;

  /// format used for shadow maps (or variance shadow maps)
  DXGI_FORMAT _smFormat;

  /// format of the down-sampling render target
  /** we want this to be the same as _rtFormat, but we need filtering available */
  DXGI_FORMAT _downsampleFormat;

  /// format of aperture surfaces
  DXGI_FORMAT _apertureFormat;

  //@{ multi-sampling settings
  int _rtMultiSampleCount;
  int _rtMultiSampleQuality;

  /// user FSAA settings
  int _fsAAWanted;
  /// max FSAA supported by the current mode
  int _maxFSAA;

  /// array containing information for FSAA modes
  AutoArray<FSAAInfo> _fsaaModes;

  enum AToCMask
  {
    AToC_Disable = 0,
    AToC_Grass = 1,
    AToC_TreeNew = 2,
    AToC_TreeOld = 4,
    AToC_SuperHair = 8,
    AToC_EnableAllOld = 7, //old value, not used anymore
    AToC_EnableAll = 255
  };

  /// should we use AToC?
#ifdef _XBOX
  static const bool _useAToC = true;
#else
  bool _useAToC;
  int _AToCMask;
#endif
  //@}

  // enable / disable vertical synchronization, default set to enable
  bool _vsync;
  //@{ anisotropy filtering settings
  int _afWanted;
  /// max. level we allow to the user (0..4)
  int _maxAF;
  /// max. level allowed by the HW (typicaly 1..16)
  int _maxAFLevel;
  //@}

  /// postprocess effects quality
  int _postFxQuality;

  /// HDR precision requested by the user (8,16 or 32 may make a sense)
  HDRPrec _hdrPrecWanted;
  /// HDR precision supported by the game - combination of HDR8,HDR16
  int _hdrPrecSupported;

  //!{ Eye accommodation based on image space measurements
  float _lastKnownAvgIntensity;
  //!}
  //@{ adaptation factor used to render this frame
  /** we cannot use _accomFactor/_accomFactorFixed directly, because of MT safety of HL recording */
  float _accomFactorThis;
  bool _accomFactorFixedThis;
  float _nightEyeThis;
  bool _nightVisionThis;
  bool _postFxEnabledThis;
  unsigned char _postFXLevelMask;
  unsigned char _postFXLevelMaskThis;
  bool _thermalVisionThis;
  float _focusDistThis;
  float _blurCoefThis;
  bool _farOnlyThis;
  //@}
  /// adaptation factor used to render last frame
  float _accomFactorPrev;

  //!{ Clear parameters used by Clear in InitDraw method
  Color _clearColor;
  //!}

  /// refcount for handling nested Begin/EndVisiblityTest
  int _visTestCount;

  HWND _hwndApp;
  HINSTANCE _hInst;

  DWORD _shaderHandle;

  int _minGuardX; // guard band clipping properties
  int _maxGuardX;
  int _minGuardY;
  int _maxGuardY;

  /// while in the video options screen, some functionality may be disabled
  bool _inVideoOptions;

  bool _windowed;
  /// device was lost - reset is needed
  /// reset might be needed - reasons may be different
  /** we use value of D3DERR_DEVICENOTRESET to force the reset */
  HRESULT _resetStatus;
  /// sometimes reset is requested (as a memory flush/cleanup technique)
  bool _resetRequested;
  /// reset was done (lost device was recovered)
  bool _resetDone;

  //! texture manager
  Ref<TextBankD3D11> _textBank;

  //! device, which functions can be called only when we are not recording
  MyD3DDevice _d3dDeviceNonRec;
  //! device, which functions can be called even when we are recording
  MyD3DDeviceCB _d3dDeviceRec;

  //! DXGI factory - used for getting information about adapters and devices
  ComRef<IDXGIFactory> _dxgiFactory;
  //! selected adapter
  ComRef<IDXGIAdapter> _dxgiAdapter;
  //! selected output
  ComRef<IDXGIOutput> _dxgiOutput;

  //! D3D errors and warnings - for debugging purposes
  ComRef<ID3D11InfoQueue> _d3dInfoQueue;
  //! if device was created in debug mode
  bool _d3dDebugMode;

  // class used to check if some operation added new messages to queue
  class D3DInfoQueueGuard
  {
  public:
    D3DInfoQueueGuard(ComRef<ID3D11InfoQueue>& queue)
      :_queue(queue)
    {
      if (_queue)
        _queue->ClearStoredMessages();
    }

    ~D3DInfoQueueGuard()
    {
      if (_queue)
      {
        if (_queue->GetNumStoredMessages() > 0)
        {
          DoAssert("DX11 Info Queue produced new error/warning");
        }
        _queue->ClearStoredMessages();
      }
    }
    
  private:
    ComRef<ID3D11InfoQueue> _queue;
  };

  /// background operation active - when active, do not use _d3DDevice, use Record/Play instead
  /**
  Even when not _backgroundD3D, we may be recording high-level functions (Postprocess, Draw2D) background,
  as denoted by _backgroundHighLevel.

  Depending on _backgroundD3D, following is true:

  RecNone: shadow state owned by main thread, CALL_CB executes, PPLHigh level executes
  RecLowLevel: shadow state owned by main thread, CALL_CB records, PPLHigh level executes
  RecHighLevel: shadow state owned by worker thread, CALL_CB executes, PPLHigh level records

  When transferring shadow state ownership, proper publish/subscribe barriers must be used.
  */
  enum RecordingMode
  {
    /// no recording, direct playback
    RecNone,
    /// low-level recording (DrawIndexedPrimitive, SetVertexShader ...)
    RecLowLevel,
    /// high-level recording (Postprocess, Draw2D, DrawLine, Present ...)
    RecHighLevel
  };
  RecordingMode _backgroundD3D;

  bool _hlRecording;

#if _DEBUG
# define KICK_OFF_REC(engine) if (Glob.config._renderThread==Config::ThreadingEmulated) engine->KickOffCBDebug(); else {}
#else
# define KICK_OFF_REC(engine)
#endif
  /// record into a command buffer or execute depending on input
#define CALL_CB(cb,Function,...) \
  if (cb<0) \
  { \
  if (IsLLRecording()) /* even if multicore is not allowed, we may still be able to use single core CB */ \
  { \
  CB(cb).Function(__VA_ARGS__); /*LogF("D3D " #Function);*/ \
  /*Assert(!_bgRecPredication);*/ /* top-level recorded instructions must not be predicated*/ \
  } \
    else \
  { \
  Assert(!IsCBRecording()); /* verify parameter corresponds to what is set by the device */\
  _d3dDeviceNonRec->Function(__VA_ARGS__); \
  } \
  } \
  else \
  { \
  CB(cb).Function(__VA_ARGS__); /*LogF("CB " #Function);*/ \
  }

#define CALL_CB_ENGINE(cb,Function,...) \
  if (cb<0) \
  { \
  if (IsLLRecording()) /* even if multicore is not allowed, we may still be able to use single core CB */ \
  { \
  CB(cb).Function(__VA_ARGS__); /*LogF("D3D " #Function);*/ \
  /*Assert(!_bgRecPredication);*/ /* top-level recorded instructions must not be predicated*/ \
  } \
    else \
  { \
  Assert(!IsCBRecording()); /* verify parameter corresponds to what is set by the device */\
  Do##Function(__VA_ARGS__); \
  } \
  } \
  else \
  { \
  CB(cb).Function(__VA_ARGS__); /*LogF("CB " #Function);*/ \
  }

  /// record into a background buffer or execute depending on input
#define CALL_D3D_MEMBER(engine,Function,...) \
  engine->AssertLowLevelThreadAccess(); /* Assert(XXX) would be nicer, but leads to a strange compiler error */ \
  if (!engine->IsLLRecording()) engine->_d3dDeviceRec->Function(__VA_ARGS__); \
    else {engine->_cbState[0]._rec.Function(__VA_ARGS__);KICK_OFF_REC(engine);}

#define CALL_D3D_MEMBER_ENGINE(engine,Function,...) \
  engine->AssertLowLevelThreadAccess(); /* Assert(XXX) would be nicer, but leads to a strange compiler error */ \
  if (!engine->IsLLRecording()) engine->Do##Function(__VA_ARGS__); \
    else {engine->_cbState[0]._rec.Function(__VA_ARGS__);KICK_OFF_REC(engine);}

  /// common variant of CALL_D3D_MEMBER used by the legacy render-state adapter
  /** TODO: PRIM: verify we are not called from the main thread when doing HighLevel recording */
#define CALL_D3D(Function,...) CALL_D3D_MEMBER(this,Function,__VA_ARGS__)
#define CALL_D3D_ENGINE(Function,...) CALL_D3D_MEMBER_ENGINE(this,Function,__VA_ARGS__)

  /// record into a background buffer or call (Do)Function engine function
#define CALL_CB_DIRECT(engine,Function,...) \
  engine->AssertLowLevelThreadAccess(); /* Assert(XXX) would be nicer, but leads to a strange compiler error */ \
  if (!engine->IsLLRecording()) engine->Do##Function(__VA_ARGS__); \
    else {engine->_cbState[0]._rec.Function(__VA_ARGS__);KICK_OFF_REC(engine);}

  //@{ active viewport z-range
  float _minZRange,_maxZRange;
  //@}

  // dynamic data for TL
  VertexDynamicData11 _dynVBuffer; // EXCL

  // misc. device properties and capabilities
  struct Caps
  {

    /// assumed alignment for VB allocations
    int _vbAlignment;

    /// assumed alignment for texture allocations
    int _texAlignment;

    /// can we assume static vertex buffers go into non-local memory?
    bool _staticVBNonLocal;

    /// can we assume dynamic vertex buffers go into non-local memory?
    bool _dynamicVBNonLocal;

    /// can we assume index buffers go into non-local memory?
    bool _ibNonLocal;

    // should we use the local VRAM prediction and DX7 number?
    bool _localPrediction;

    /// prevent overwriting the heuristics values once they have been set
    bool _localPredictionSet;

    /// should all vertex buffers be static?
    /** this is set depending on amount of local VRAM and texture detail required */
    bool _vbuffersStatic;

    /// total memory allocated in all render-targets/back-buffers...
    int _rtAllocated;

    /// VRAM amount (checked by DX7)
    int _localVramAmount;
    /// AGP amount (checked by DX7)
    int _nonlocalVramAmount;

    //@{ when local prediction in off, what is the texture allocation limit relative to local/non-local memory
    int _vramAllocLimit;
    //@}

    bool _can565,_can88,_can8888;
    int _dxtFormats; // bit-field of DXT formats (1..5)
    int _maxTextureSize;  

    bool _canHWMouse;
    bool _canReadSRGB;
    bool _canWriteSRGB;

    bool _hasStencilBuffer;
    bool _canTwoSidedStencil;
    int _maxLights;

    int _vertexShaders; // pixel shader version
    int _pixelShaders; // pixel shader version

    /// vertex shader version descriptor used for compilation (for example "vs_2_0")
    RString _vsVersion;
    /// pixel shader version descriptor used for compilation (for example "ps_2_0")
    RString _psVersion;
    /// vertex shader version descriptor used for linking (for example "vs_2_0")
    RString _vsVersionLink;

    // how many bones at one time are supported by given implementation
    int _maxBones;
    int _maxInstances;
    int _maxInstancesSprite;
    int _maxInstancesShadowVolume;

    //!{ Maximum instances in the night (smaller number than in the day)
    int _maxNightInstances;
    int _maxNightInstancesSprite;
    //!}

    /// is render target linear, or is SRGB on write performed?
    bool _rtIsSRGB;
  } _caps;

  /// short-term cache - avoid the same shader being compiled twice
  FindArrayKey<PSCachedItem> _psCache;
  MapStringToClass<VSCachedItem, AutoArray<VSCachedItem> > _vsCache;

  /// map shader IDs to D3D shader objects
  ComRef<ID3D11PixelShader> _pixelShader[NPixelShaderID][NPixelShaderType];
  /// LRU list of vertex buffers - used to release buffer that were not used for a long time
  /** list of all shapes which have geometry loaded, but it can be released */
  FramedMemoryControlledList<VertexBufferD3D11LRUItem> _vBufferLRU;

  /// lock free container gathering updates for LRU requests
  LockFreeGrowingList<VertexBufferD3D11> _vbLRURequest;

  //! how much memory is allocated in index buffers (in bytes)
  int _iBuffersAllocated;
  //! how much memory is allocated in vertex buffers (in bytes)
  int _vBuffersAllocated;

  //@{ The estimation based on DX7 test
  /// VRAM amount free (estimated)
  int _freeLocalVramAmount;
  /// AGP amount free (estimated)
  int _freeNonlocalVramAmount;
  /// how much did happen since the last DX7 test (how much is a new test needed)
  int _freeEstDirty;
  /// how much did happen since the last Reset (how much is a new Reset needed)
  int _resetDirty;
  /// how many frames (Presents) has passed since the last Reset?
  int _resetFrame;

  /// time when last check was performed
  DWORD _freeEstCheckTime;
  //@}

  /// HLSL include support for custom files
  class HLSLInclude : public ID3D10Include
  {
  public:
    /**
    @param data [out] file data
    @param bytes [out] file size
    @return error code
    */
    STDMETHOD(Open)(D3D10_INCLUDE_TYPE includeType, LPCSTR fileName, LPCVOID parentData, LPCVOID *data, UINT *bytes);
    STDMETHOD(Close)(LPCVOID pData);
  };

  /// HLSL include support for custom files
  HLSLInclude _hlslInclude;

  //! Shader pool n-dimension array
  VertexShaderInfo _vertexShaderPool[SHP_VertexShaderID][SHP_UVInput][SHP_MainLight][SHP_RenderingMode][SHP_SkinningInstancing];
  //! Shadow volume shaders for normal, skinning and instancing objects
  VertexShaderInfo _vertexShaderShadowVolume[SHP_SkinningInstancing];
  //! List of special vertex shaders (those shaders that exist in one version only)
  VertexShaderInfo _vertexShaderSpecial[NVertexShaderSpecialID];
  //! Terrain shader in 3 variations
  VertexShaderInfo _vertexShaderTerrain[SHP_RenderingMode];
  //! Terrain grass shader in 3 variations
  VertexShaderInfo _vertexShaderTerrainGrass[SHP_RenderingMode];
  //! Road shader in 3 variations
  VertexShaderInfo _vertexShaderRoad[SHP_RenderingMode];
  //!}

  //! Non-TL vertex shader (shader for 2D drawing)
  VertexShaderInfo _vsNonTL;

  //! Array of all possible vertex declarations
  ComRef<ID3D11InputLayout> _vertexDeclD[VDCount][VSDVCount];
  //! Non-TL vertex declaration
  ComRef<ID3D11InputLayout> _vdNonTL;

  /// modulation color - used for HDR buffer transformation
  Color _modColor;

  /// is pixel shader work done in linear space, or SRGB?
  bool _linearSpace;

  float _night;

  float _nightEye;

#if _ENABLE_SKINNEDINSTANCING
  //! Bones with instancing
  AutoArray<BonesType> _bonesInstanced;
#endif

  //! Actual back buffer
  RenderTarget _backBuffer;
  
  //{! Render targets
  /** primary render target (MSAA) */
  RenderTarget _renderTargetS;
  //! Depth buffer associated with primary render target (MSAA) */
  DepthStencil _depthStencilS;
  /** non MSAA render target used for postprocesses (if MSAA is disabled, then _renderTargetS is used for postprocesses) */
  RenderTarget _renderTargetPP;
  /** render target used to get copies of primary render target (used mainly as shader resource for postprocesses*/
  RenderTarget _renderTargetD;

  /** we remember current render target here (fake reference) */
  RenderTargetFake _currRenderTarget;
  /** we remember current depth/stencil buffer here (fake reference) */
  DepthStencilFake _currDepthStencil;
  //@}

  //!{ Render target containing depth informations, filled out in z-prime step (no MSAA)
  RenderTarget _renderTargetDepthInfo;
  //! Depth/stencil for _renderTargetDepthInfo (no MSAA)
  DepthStencil _depthStencilDepthInfo;

  RenderTarget _sssmRenderTarget;
#ifdef SSSMBLUR
  RenderTarget _sssmBRenderTarget;
#endif
  RenderTarget _ssaoRenderTarget;

  //!{ Depth/stencil buffer used to render shadow map - layer with index 0 is the roughest (largest) part of the cascade
  /**
  note: in spite of using multiple layers, the layers are combined in a loop
  only one shadow buffer surface is needed for all layers together
  */
  DepthStencil _depthStencilSB;
  //!}


  //!{ Setting and restoring of temporary render target
  //! queue used to push/pop render target and depth/stencil info
  AutoArray<RenderTargetDepthStencilInfo> _savedRenderTargetInfoQueue;
  //!}

  //////////////////////////////////////////////////////////////////////////

#if _ARMA3_SIMUL_WEATHER

  enum SW_Layers {
    MinSWLayers = 32,
    DefaultSWLayers = 64,
    MaxSWLayers = 128
  };

  int _swLayersCount;

#endif

#if _HELISIM_RENDER_TO_TEXTURE
  bool _enabledR2T;


  struct R2THDRPars: public RefCount
  {
    //! additional light gain, e.g. by night vision goggles
    Color _lightGain;
    //! accommodation factor
    float _accomFactor;
    /// adaptation factor used to render last frame
    float _accomFactorPrev;
    //! sometimes adaptation factor is fixed and we want to avoid GPU updating it
    bool _accomFactorFixed;
    //!
    Color _accomodateEye;
    //! Eye accommodation based on image space measurements
    float _lastKnownAvgIntensity;
    //! adaptation factor used to render this frame
    float _nightEye;

    //@{ 1x1 RT used for aperture control - double buffered
    RenderTarget _rtApertureR2T[2];
    int _rtApertureCurrentR2T;

    //! Query manager
    QueryManagerSinglePass *_querySinglePassR2T;

    // elapsed time
    float _deltaTR2T;
    // nv active
    bool _nightVisionR2T;
    // eye adaptation
    float _eyeAdaptMinR2T;
    float _eyeAdaptMaxR2T;

    R2THDRPars(): _querySinglePassR2T(NULL) {}
    ~R2THDRPars() { UnInitialize(); }

    bool Initialize(EngineDD11 *engine);
    void UnInitialize();
  };

  struct R2TTiPars: public RefCount
  {
    float _deltaT;
    int _tiMode;
    float _maxTemperatureR2T;
    int _temperaturesR2T[16];
    QueryManager *_queryR2T;

    R2TTiPars(): _queryR2T(NULL) {}
    ~R2TTiPars() { UnInitialize(); }

    bool Initialize(EngineDD11 *engine);
    void UnInitialize();
  };

  struct R2TColorsPars: public RefCount
  {
    bool _enabled;
    //!
    float _brightness;
    //! image contrast ( default value = 1 )
    float _contrast;
    //! contrast offset ( default value = 0 )
    float _offset;
    //! overlay color blending (rgb ..lerp color, a ..lerp factor)
    Color _lerpColor;
    //! color for colorization;  (rgb ..color, a ..saturation; white is default)
    Color _colorizeColor;
    //! rgb weights for desaturation ( 0.299, 0.587, 0.114 )
    Color _rgbWeights;
    //!
    Vector3 _coeffs;

    R2TColorsPars();
    bool Initialize(EngineDD11 *engine);
    void Update(const float *pars, int count);
  };

  struct R2TRT
  {
    // Non FSAA render target
    RenderTarget _renderTargetS;
    //! Texture containing the render target data (no multisampling used)
    RenderTarget _renderTargetDepthInfo;
    bool _renderTargetDepthInfoSurfaceSet;

    //! Render target copy
    RenderTarget _rtCopy;

    //!{ Setting and restoring of temporary render target
    RenderTargetFake _renderTargetSaved;
    DepthStencilFake _renderTargetDepthBufferSurfaceSaved;

    float _minZRangeSaved;
    float _maxZRangeSaved;
    float _minZRange;
    float _maxZRange;

    float _wSaved;
    float _hSaved;

    int _viewWSaved;
    int _viewHSaved;
    //! }

    // save GEngine pars 
    R2TTiPars _globTIPars;
    // save GEngine pars
    R2THDRPars _globHDRPars;

    Color _lightGain;
    float _accomFactor;
    float _accomFactorPrev;
    bool _accomFactorFixed;
    Color _accomodateEye;
    float _lastKnownAvgIntensity;
    float _nightEye;

    Color _clearColor;
    bool _atocUsed;
  } _r2tRT;

  struct R2TTargetDescLite
  {
    // source texture
    RenderTarget _texture;
    // assigned render target name
    RString _name;
    //! RT width
    int _width;
    //! RT height
    int _height;

    ClassIsMovableZeroed(R2TTargetDescLite);
  };

  struct R2TTargetDesc
  {
    R2TTargetDescLite _desc;
    // assigned target
    RenderTargetID _id;
    //! last rendering frame
    int _frameId;
    //! thermal pars
    Ref<R2TTiPars> _tiPars;
    //! HDR params, unique for each target
    Ref<R2THDRPars> _hdrPars;
    //! Color post effect pars
    Ref<R2TColorsPars> _clrPars;
    //! TRUE when target used 
    volatile bool _active;
    //! 
    bool _releaseWanted;

    ~R2TTargetDesc()
    {
      _desc._texture.Free();
      _hdrPars.Free();
      _tiPars.Free();
    }

    ClassIsMovableZeroed(R2TTargetDesc);
  };

  void ConvertFullToLite(R2TTargetDesc &, R2TTargetDescLite&);
  bool ConvertLiteToFull(R2TTargetDesc &, R2TTargetDescLite&);

  //! registered RT source textures
  AutoArray<R2TTargetDescLite> _r2tSrcTargets;

  //! render targets descriptors
  AutoArray<R2TTargetDesc> _r2tTargets;

  //! true when r2t rendering is in progress
  volatile bool _r2tBegin;

  bool _r2tCamsSyncWanted;

  //! Set RTs used for r2t
  virtual void SetR2TRenderTargets();
  //! Restore main scene RTs
  virtual void RestoreR2TRenderTargets(RenderTargetID rtid, bool saveEnable);

  //! Create CB - post effect are done as single CB command
  virtual void R2TPostProcess(int effect, float dT, RenderTargetID rtid);
  //! Do R2T post process
  void DoR2TPostProcess(int vMode, float deltaT, RenderTargetID rtid);
  //! 
  virtual void R2TSetPEColorsPars(RenderTargetID rtid, const float* pars, int count);

  virtual float GetLastKnownR2TAvgIntensity(RenderTargetID rtid) const;

  //! Debug function - save dest texture & R2T to *.dds files
  void SaveR2TToFile(const RenderTarget& surfToSave);

  //! Create R2T render targets & depth stencil surfaces
  void CreateR2TResources();
  //! Destroy e R2T render targets & depth stencil surfaces
  void DestroyR2TResources();
  //! Invalidate all R2T RTs stages
  void InitializeR2TTargets();
  //! Destroy all R2T RTs stages
  void DestroyR2TTargets();
  //! Invalidate R2T stage
  void InvalidateR2TTarget(R2TTargetDesc &r2tSourceDesc, bool registerTexture = false);    
  //! Remove all textures from render target list
  void UnRegisterAllR2TSourceTextures();

  void ClearR2TTarget(R2TTargetDescLite &desc);

  //! Check R2T dest texture usage
  virtual void TrackR2TTexUsage(const TextureD3D11* texture);
  //! Create render target copy
  ComRef<TextureHandle11> CreateR2TRenderTargetCopyAsTexture(const RECT *rect); 
  //! Create depth buffer copy - used as texture for sprites etc.
  void CreateR2TDepthAsTexture();

  //! Inform about r2t targets invalidation - sync with r2t cams
  virtual bool R2TCamsSyncNeeded();

  void DoR2TCopyTarget(ID3D11ShaderResourceView *source, CONST RECT *srcRect, const RenderTarget& dest, bool save);
  void DoR2TClearTarget(ID3D11RenderTargetView *rtView);
public:
  virtual void InitR2TDraw();
  //! Add texture to render target textures list
  void RegisterR2TSourceTexture(const SurfaceDataD3D11 &texture, int w, int h, RString name);
  //! Remove texture from render target textures list
  void UnRegisterR2TSourceTexture(const SurfaceDataD3D11 &texture);
  //! Bind camera (specified by name) to render target
  virtual RenderTargetID BindR2T(const RString& name);
  //! UnBind camera from render target
  virtual bool UnBindR2T(RenderTargetID rtid);
  //! Check whether target is valid
  virtual bool IsBindR2T(RenderTargetID rtid) const;
  //! Check whether target was used in main scene
  virtual bool WasR2TUsed(RenderTargetID rtid) const;
  //! Begin rendering to texture
  virtual void BeginR2T(RenderTargetID rtid);
  //! End rendering to texture
  virtual void EndR2T(RenderTargetID rtid);
  //!
  virtual void EnableR2T(bool enable) { _enabledR2T = enable; }
  //!
  virtual bool IsR2TEnabled() const { return _enabledR2T; }
protected:

#endif

  //////////////////////////////////////////////////////////////////////////


  enum PPLevel : char
  {
    PPLForce = 1, PPLVeryLow = 2, PPLLow = 4, PPLNormal = 8, PPLHigh = 16, PPLVeryHigh = 32
  };

  /// Post processing stuff
  struct PPs
  {
    Ref<PPGaussianBlur> _gaussianBlur;
    Ref<PPBloom> _bloom;
    Ref<PPRain3D> _rain3D;
    Ref<PPSSSM> _SSSM;
    Ref<PPSSSMStencil> _SSSMStencil;
#ifdef SSSMBLUR
    Ref<PPSSSMB> _SSSMB;
#endif
    Ref<PPDOF> _DOF;
    Ref<PPDistanceDOF> _distantDOF;
    Ref<PPFilmGrain> _filmGrain;
    Ref<PPSSAO> _ssao;
    Ref<PostProcess> _finalThermal;
    Ref<PPGlow> _glow;
    Ref<PPFinal> _final;
    Ref<PPRescaleBicubic> _bicubic;
    Ref<PPFlareIntensity> _flareIntensity;
    Ref<PPRotBlur> _rotBlur;
    Ref<PPRadialBlur> _radialBlur;
    Ref<PPColors> _colors;
    Ref<PPChromAber> _chromAber;
    Ref<PPWetDistort> _wetDistort;
    Ref<PPDynamicBlur> _dynamicBlur;
    Ref<PPColorsInversion> _clrInvers;
    Ref<PPStretchRect> _stretchRect;
    Ref<PPDiagDepthBuffer> _diagDepthBuffer;

#if ENABLE_NVG_NOISE
    Ref<PPFilmGrain> _nvgNoise;
#endif

    FindArrayKey< PostProcessHndl, FindArrayKeyTraits<PostProcessHndl> > _effects;
    // copy for background thread, MT safe
    FindArrayKey< PostProcessHndl, FindArrayKeyTraits<PostProcessHndl> > _effectsThis;
  } _pp;

  typedef DWORD SamplerStateInfo;
  typedef DWORD RenderStateInfo;

  enum RenderMode {RMLines,RMTris,RM2DLines,RM2DTris};

  /**
  Separate rendering state, so that rendering can be done on multiple threads simultaneously.
  For this each thread needs to keep its own copy of a D3D device.On PC this is actually only
  a recording wrapper around a device, on X360 it can be a special recording variant of the device.

  Any state accessed from any rendering methods used from Object::Draw needs to be located in this state.

  Any state marked as EXCL must be protected so that it is never access from Object::Draw
  */
  struct RendState
  {
    RendState()
    {
    }

    // are we rendering 2D or 3D?
    TLMode _tlActive;
    //@{ shadowed shader constants
    // we are not shadowing whole range, only a part of it, where equality is likely.

    /** char so that we can represent impossible value */ 
    ShaderConstantBufferList _shaderConsts;
    //@}
    //@{ shadowed D3D rendering state
#if _ARMA3_PREDICATED_SAMPLER_STATES
    AutoArray<SamplerStateInfo> _samplerState[NPredicationModes][MaxSamplerStages];
#else
    AutoArray<SamplerStateInfo> _samplerState[MaxSamplerStages];
#endif
    AutoArray<RenderStateInfo> _renderState[NPredicationModes];
    //@}

    /// Rendering mode the render states are set with 
    RenderingMode _lastRenderingMode;
    /// z-priming done (set into a device)
    bool _lastZPrimingDone;
    /// z-priming done (wanted)
    bool _zPrimingDone;
    //! Flag to determine the primitive will use the SSSM approach
    bool _doSSSM;
    /// tree crown dimensions for the tree shader
    Vector3 _treeCrown[2];
    //!{ Single instance parameters
    float _instanceShadowIntensity;
    Color _instanceColor;
    //!}
    /// z-buffer bias
    int _bias; 
    //@{ high-level tracking of what is set into a device or internal engine state
    int _lastSpec;
    bool _lastAToC;
    TexMaterialLODInfo _lastMat;
    int _prepSpec;
    //@}
    /**
    comparing IDirect3DTexture9 is enough - ABA problem cannot happen, because whenever destroying a texture,
    we make sure it is not present in any _lastHandle
    */
    TextureHandle11* _lastHandle[MaxSamplerStages];
    EShadowState _currentShadowStateSettings;

    /// some rendering used no back face culling
    bool _backCullDisabled;

    //! Maximal color of texture on stage 0
    Color _texture0MaxColor;
    //! Average color of texture on stage 0
    Color _texture0AvgColor;

    //!{ UV compression properties
    UVPair _texGenScale[MAX_UV_SETS];
    UVPair _texGenOffset[MAX_UV_SETS];
    bool _texGenScaleOrOffsetHasChanged;
    //!}
    //!{ UV sources for particular stages
    UVSource _uvSource[MaxStages];
    int _uvSourceCount;
    bool _uvSourceOnlyTex0;
    //!}
    /// Currently active lights
    AutoArray<Light *, MemAllocLocal<Light *,16> > _lights;
    //!{ P&S lights count and their current values in vertex shader
    int _pointLightsCount;
    int _spotLightsCount;
    //!}

    /// last SetStreamSource
    VertexStaticData11::BufferFuture _vBufferLast; // TODO: do we need a different RefCount handling? (FutureFake)
    int _strideLast;

    //@{ last SetIndices
    IndexStaticData11::BufferFuture _iBufferLast; // TODO: do we need a different RefCount handling? (FutureFake)
    int _vOffsetLast;
    int _vBufferSize;
    //@}
    EMainLight _mainLight;

    bool _applyAplhaFog;

    EFogMode _fogMode;

    //@{ fog parameters
    float _fogStart,_fogEnd;
    //@}
    //@{ alpha fog parameters
    float _aFogStart,_aFogEnd;
    //@}
    /// exponential fog density
    /** used in shader as haze = exp(dist*_expFogCoef) */
    float _expFogCoef;
    PixelShaderID _pixelShaderSel;
    PixelShaderType _pixelShaderTypeSel;
    ComRefFake<ID3D11PixelShader> _pixelShaderLast[NPredicationModes];
    //! New vertex shader ID
    VertexShaderID _vertexShaderID; 

    //! Vertex shader that was used in the previous draw call
    /** may be different for each predicated branch */
    ComRefFake<ID3D11VertexShader> _vertexShaderLast[NPredicationModes];

    //! last primitive topology used
    D3D11_PRIMITIVE_TOPOLOGY _lastPrimitiveTopology;

    //@{ misc. rendering state
    Matrix4 _world;
    Matrix4 _invWorld;
    Matrix4 _view;
    /// to avoid numeric precision problem we avoid inverting camera space in model space
    /** we prefer subtracting position and inverting orientation only */
    Matrix4 _invView;
    /// some post-effects may be interested in knowing scene projection
    Matrix4 _proj;

    Color _lightAmbient;
    Color _lightDiffuse;
    Color _lightSpecular;

    //@{ TODO: it seems light and shadow direction do not need to be per-CB, as it is constant in the scene?
    Vector3 _direction;
    Vector3 _shadowDirection;
    //@}

    Color _matAmbient;
    Color _matDiffuse;
    Color _matForcedDiffuse;
    Color _matSpecular;
    Color _matEmmisive;
    float _matPower;
    //@}
    //! Type of skinning
    ESkinningType _skinningType;

    AutoArray<FloatVSConstant> _waves; // TODO:MC: set directly, no shadowing here
    //! Array of bones
    BonesType _bones;
    //! Flag to determine bones were set during SetSkinningType method call
    bool _bonesWereSetDirectly;

    /// compare two render states, report any differences into the debug output
    /** debugging only - used to detect CB rendering artifacts */
    bool ReportDifferences(const RendState &with) const;
  };


  /// state tied with the CB id, but not a part of the device state
  struct RendStateExt: public RendState
  {
    /// recording "fake" D3D device
    D3DCommandBufRec _rec;

    /** useful for debugging as well: common reason for failing assert is missing EndPredication */
    void StartPredication(int val) {Assert(_rec._currPredication==~0);_rec._currPredication = val;}
    void StopPredication() {Assert(_rec._currPredication!=~0);_rec._currPredication = ~0;}

    /// no need to enter undefined state when we are rendering tasks which follow each other
    int _lastResult;

#if PIX_NAMED_CB_SCOPES
    /// debugging ID of the currently rendered object
    int _debugId;
#endif

    ClassIsGeneric(RendStateExt)
  };

  //@{ Shadow buffer parameters
  /// did user enable the SB rendering?
  int _sbQualityWanted;
  /// what is the setting we have currently set in the engine?
  int _sbQuality;
  /// size of shadow buffer surface
  int _sbSize;
  //@}

  //! Flag to determine whether bloom post-process will be used
  bool _doBloom;

  //!{ Shadow plane parameters
  //! Matrix for conversion from transformed position to shadow map
  PASSMATRIXTYPE _shadowMapMatrixDX;
  //! Length in meters the shadow buffer value 1 represents (1 is the maximum in shadow buffer) - used for determining the shadow length
  float _maxDepth;
  //! SP rounded camera position SP orientation
  Matrix4 _oldLRSP[ShadowBufferCascadeLayers];
  //!}


  //@{ Depth of field parameters
  //! Flag to determine engine is capable of depth buffer
  bool _dbAvailable;
  //! Did user enable the DB rendering?
  int _dbQuality;
  //! Focal plane distance
  float _focusDist;
  //! Amount of blurring from interval <0, 1>
  float _blurCoef;
  //! Flag to signalize special DOF effect (blur far only objects) should be used
  bool _farOnly;
  //@}

  //!{ Thermal imaging parameters
  //! Tc table texture
  SurfaceDataD3D11 _tableTc;
  //! Tc table texture
  SurfaceDataD3D11 _tableTcSystem;
  // Table of current temperatures 
  AutoArray<float> _temperatureTable;
  //! Temperature of the air
  float _airTemperature;
  //! Name of the color conversion texture (so that we could load it after reset)
  RString _tiConversionName;
  //! Table used for color conversion
  SurfaceDataD3D11 _tiConversion;
  ComRef<ID3D11Resource> _tiConversionSystem;
  //! Array to specify for each row if black-hot mode is used there
  bool _tiBlackHot[TIConversionDimensionH];
  //! Coefficient from interval <0,1> describing current level of blur
  float _tiBlurCoef;
  //! Brightness of the TI device
  float _tiBrightness;
  //! Contrast of the TI device
  float _tiContrast;
  //! Max visible object temperature on screen
  float _maxTemperature;
  //! Mode of the thermal imaging (hot-white, hot-black etc...)
  int _tiMode;
  //! Automatic calculation of brightness and contrast enabled/disabled
  bool _tiAutoBC;
  //! Coefficient to be used to multiply the variation
  float _tiAutoBCContrastCoef;
  //! Flag to determine the thermal vision was already visualized in the frame
  bool _tiVisualizationDone;
  //!}

  //! Flag to determine we render the 3D part of the scene
  bool _3DRenderingInProgress;

  /// was End3DRendering requested from the main thread?
  bool _end3DRequested;

  /// mouse cursor currently in use
  /** NULL = cursor disabled */
  LLink<Texture> _cursorTex;
  /// color of current mouse cursor
  PackedColor _cursorColor;
  /// mouse center x coordinate 
  int _cursorX;
  /// mouse center y coordinate 
  int _cursorY;
  /// was cursor set in this frame?
  bool _cursorSet;
  /// is the cursor currently visible?
  bool _cursorShown;
  /// last value passed by CursorOwned()
  bool _cursorOwned;

  FindArrayKey<CursorInfo> _cursors;
  // current cursor
  HCURSOR _curCursor;

  /// individual command buffer produced by a micro-job for later play-back
  struct CBToPlayBase
  {
    char *_data;
    int _size;

    /// how much memory can be reclaimed to the allocator after this CB is processed
    /**
    CBs are processed in blocks. Inside of the block, no CB can free any memory,
    this can be done only after the whole block is processed

    Therefore we mark the last CB in the block with this information
    */
    int _memToClear;

    CBToPlayBase()
    {
      _data=NULL;_size=0;_memToClear=0;
    }

    //@{ mimic buffer interface
    template <class Alloc>
    void Resize(int size, Alloc &alloc)
    {
      if (size>0)
      {
        _data = (char *)alloc.Allocate(size);
      }
      else
      {
        _data = NULL;
      }
      _size = size;
    }

    template <class Alloc>
    void Clear(Alloc &alloc)
    {
      if (_data)
      {
        alloc.Free(_data,_size);
        _data = NULL;
        _size = 0;
      }
    }
    char *Data() {return _data;}
    const char *Data() const {return _data;}
    int Size() const {return _size;}
    //@}


    // if the buffer is large, initialization via construction is likely to be faster
    ClassIsGeneric(CBToPlayBase)
  };

  /// individual command buffer produced by a micro-job for later play-back
  struct RenderCBToPlay: public CBToPlayBase
  {
    typedef D3DCommandBufRec BGDataType;

    /// which pass should be rendered (as defined by PredicationMode, <0 means render everything)
    PredicationMode _predicate;

    /// mark the CB as non-destroyed, needed in case the CB will be played back again
    bool _noDestroy;

    PredicationIndex _predication;

#if _DEBUG
    /// textual representation of the command stream
    RString _debug;
#endif
#if PIX_NAMED_CB_SCOPES
    static int _currDebugId;
    int _debugId;
    /// name used for PIX scope
    BString<50> _scopeName;
#endif

    RenderCBToPlay()
    {
      _noDestroy=false;
#if PIX_NAMED_CB_SCOPES
      _debugId=0;
#endif
    }
    ~RenderCBToPlay()
    {
      // verify the data were destroyed properly
      Assert(!_data || _noDestroy);
    }

    void MarkNoDestroy(bool noDestroy=true){_noDestroy=noDestroy;}

    bool IsReadyToDestroy() const {return !_noDestroy;}

    void LogResultsDealloc()
    {    
#if PIX_NAMED_CB_SCOPES
      if (_size>0) {LogF("%s (%d): Dealloc %d",cc_cast(_scopeName),_debugId,_size);}
#endif
    }

    void LogResultsAlloc(const char *pixScope, int dtaSize)
    {
#if PIX_NAMED_CB_SCOPES
      if (dtaSize>0) {LogF("%s (%d): Allocate %d",pixScope,_debugId,dtaSize);}
#endif
    }

    void LogResultsDestroy()
    {
#if PIX_NAMED_CB_SCOPES
      if (_size>0) {LogF("%s (%d): Dealloc %d",cc_cast(_scopeName),_debugId,_size);}
      if (_memToClear>0) {LogF("%s (%d): Clear %d",cc_cast(_scopeName),_debugId,_memToClear);}
#endif
    }


    static void OnFlush()
    {    
#if PIX_NAMED_CB_SCOPES
      // reset the numbering so that numbers in successive frames are repeatable
      RenderCBToPlay::_currDebugId = 0;
#endif
    }
    template <class SourceCBType>
    void FinishSubmit(EngineDD11 *engine, SourceCBType &bgData, const char *pixScope)
    {
      _predication = bgData.GetPredication();
      // predicate depending on current rendering mode
      _predicate = engine->CurrentPredicationMode();
      MarkNoDestroy(engine->_bgRecPredication);
#if _DEBUG
      // we can access _debug RString from one thread only
      if (Glob.config._mtSerial) _debug = bgData.GetDebugText();
#endif
#if PIX_NAMED_CB_SCOPES
      _debugId = ++RenderCBToPlay::_currDebugId;
      _scopeName = pixScope;
#endif

    }

    // if the buffer is large, initialization via construction is likely to be faster
    ClassIsGeneric(RenderCBToPlay)

  };


  template <class CB_Type, int cbCount, int consumerCount>
  struct WorkerThread 
  {
    typedef CB_Type CBType;
    typedef typename CBType::BGDataType BGDataType;

    /// command buffers recorded in individual tasks
    /**
    - indexed with the same index as source object lists (sparse, may contain NULLs)

    When background thread is running, separate consumers are responsible for processing and destruction
    In single core mode the processing consumer is doing the destruction as well
    */
    CircularArray<CBType, cbCount, consumerCount> _results;

#if _ENABLE_CHEATS
    InitVal<int,0> _resultsMaxUsed;

    MovingHistory<int,250,25> _resultsMaxUsedHistory;
#endif

    /// contiguous common memory for all _results allocations
    SimpleAllocatorMT<> _resultsMem;

    /// used to track amount of memory allocated so that we can release (Clear) it
    int _resultsMemUsedStart;

    /// signalize termination request to the thread
    Event _terminate;
    /// background thread Id
    /** used for asserts */
    DWORD _threadId;
    /// thread handle
    SignaledObject _thread;

    /// advisory event - set whenever some data may be pending
    /** set only by the main thread */
    Event _dataPending;

    /// indicate event should be set, but was not - lazy optimization to prevent the Event API overhead
    /** accessed only by the main thread */
    bool _dataPendingNeeded;

    WorkerThread()
      :_thread(NoInitHandle)
    {
      _dataPendingNeeded = false;
    }

    void Init(DWORD stackSize, LPTHREAD_START_ROUTINE startAddress, LPVOID parameter, int cpu, int priority, const char *name)
    {
      _thread.Init( CreateThreadOnCPU(stackSize, startAddress, parameter, cpu, priority, name, &_threadId) );
    }

    void InitMem(int memSize)
    {
      _resultsMem.SetMemory(new char[memSize],memSize);
      _resultsMemUsedStart = 0;
    }
    void ClearMem()
    {
      delete[] (char *)_resultsMem.GetMemory();
      _resultsMem.SetMemory(NULL,0);
    }

    void Done()
    {
      _terminate.Set();
      // wait until termination is done
      _thread.Wait();
      _thread.Done();
    }
    bool OneIteration()
    {
      // wait until some job is ready or termination
      SignaledObject *handles[] = {&_terminate, &_dataPending};
      int result = SignaledObject::WaitForMultiple(handles, lenof(handles));
      return result != 0; // 0 == done

    }
    void SetDataPending(bool setEvent)
    {
      if (setEvent) _dataPending.Set(),_dataPendingNeeded = false;
      else _dataPendingNeeded = true;
    }

    void ProcessLazyDataPending()
    {
      if (_dataPendingNeeded)
      {
        // when the event needs to be set, set it, so that we prevent a deadlock
        _dataPending.Set();
        _dataPendingNeeded = false;
      }
    }

    bool ProcessBackground();

    virtual void DoWorkWhileWaiting() {}
  };



  enum
  {
    /// playback (background thread)
    ProcessResults,
    /// destruction (main thread)
    DestroyResults,
    /// destruction allowed (main thread has locked something for copying)
    CanDestroyResults,
    /// consumer count
    NBackgroundConsumers
  };

#ifdef _XBOX
  static const int MaxBGItems = 4*1024;
#else
  static const int MaxBGItems = 8*1024;
#endif

  typedef WorkerThread<RenderCBToPlay,MaxBGItems,NBackgroundConsumers> BGWorkerThread;

  class VisWorkerThreadImpl: public ActiveObject<VisualServant>, public SignaledObject
  {
    Event _terminate;


  public:

    DWORD _threadId;

    VisWorkerThreadImpl()
      :SignaledObject(NoInitHandle)
    {}

    bool OneIteration()
    {
      // wait until some job is ready or termination
      SignaledObject *handles[] = {&_terminate, &_kicked};
      int result = SignaledObject::WaitForMultiple(handles, lenof(handles));
      return result != 0; // 0 == done
    }

    VISUAL_WORK_MESSAGES(VisWorkerThreadImpl,ACTOBJ_WRAP_FUNC)
  };

  BGWorkerThread _bg;

  VisWorkerThreadImpl _vis;

  enum {IAmMainThread,IAmVisualizeThread};

  /// one queue for each of visualize/render threads */
  //StoredRefContainer _bgRefToRelease;

#if SAFE_RESOURCE_UNSET
  /// D3D resources to release
  AutoArray<ID3D11DeviceChild*, MemAllocLocal<ID3D11DeviceChild *,64> > _bgResToRelease;
  /// textures to release
  AutoArray<TextureHandle11*, MemAllocLocal<TextureHandle11*,64> > _bgTexHandleToRelease;
#endif

  RefArray<VertexBuffer> _vBufferToRelease;

    /// how many results were submitted in this particular CBScope?
  /** remember value from StartCBScope to EndCBScope */
  int _bgResultsInThisScope;

  /// where was background write pointer located when this particular CBScope started
  int _bgWriteInThisScope;

  /// are we recording with predication?
  /** when we are, the CBs recorded cannot be destroyed until*/ 
  bool _bgRecPredication;

  /// we currently do not intend to support more then 8 CB threads in parallel
  /** array of CB states, slot 0 is used for the real (immediate) state */
  AutoArray<RendStateExt> _cbState;

  /// means to wait for _performMipmapLoadingWait
  Future<WaitableVoid> _performMipmapLoadingWait;

#if 0 // _DEBUG || _PROFILE
#define VIS_JOURNAL 1
  Journal _visJournal;
#endif


  /// stored undefined state (TODO: could be shared accross frames?)
  RendState _undefined;


  /// possible number of CB threads - set by StartCBScope
  /**
  -1 outside of the CB scope,
  0 in a CB scope, but not recording any CBs
  >0 number of concurrent CBs recorded
  */
  int _cbCount;

  RenderMode _renderMode; // EXCL
  Queue11 _queueNo; // EXCL

  float _gamma;

  //@{ corresponds to TextBank value,
  /** needed here because it can be set before TextBank is created */
  int _textureQuality;
  int _textureMemory;
  //@}

  bool _d3dFrameOpen; 

  /// is FinishFrame necessary?
  /** was InitDraw called? - differs from _d3dFrameOpen when performing HighLevel recording */
  bool _d3dFrameOpenRequested;

  //! Values passed by SetWaterDepth
  struct
  {
    float _shoreTop,_shoreBottom;
    float _peakWaveTop,_peakWaveBottom;
    float _seaLevel;
    float _grid;
    float _waterSegSize;
#if _VBS3_LARGE_WATERSEG
    float _waterArraySize;
#endif
  } _waterPar; // EXCL - is global for the scene

  //! Number of DrawIndexedPrimitive calls from the beginning of the frame
  int _dipCount;

  /// Flag to determine what we're rendering (Usual stuff, shadow buffer, depth buffer...)
  RenderingMode _renderingMode; // EXCL

  //! Temporary setting of a render state
  /*!
  The state will be returned to it's previous value as soon as this object is destroyed
  */
  class SetRenderStateTemp
  {
  private:
    //! Engine
    static EngineDD11 *_engine;
    //! State we set and then set back
    D3DRENDERSTATETYPE_OLD _state;
    //! Old value to set back
    DWORD _oldValue;
    //! if we are in direct execution mode
    bool _direct;
  public:
    //! Constructor - remember the old state and value, set the new one
    SetRenderStateTemp(D3DRENDERSTATETYPE_OLD state, DWORD value, bool direct = false): _direct(direct), _state(state)
    {
      if (_direct)
      {
        int index = state;
        if (index >=0 && index < _engine->_currentRenderState._renderState.Size())
        {
          _oldValue =  _engine->_currentRenderState._renderState[index];
        }
        else
        {
          _oldValue = -1;
        }
        _engine->DoSetRenderState(state, value);
      }
      else
      {
        const int cb = -1;
#ifdef _XBOX
        // TODOX360: eliminate states which do not exists
        if (state<0 || state>=D3DRS_FORCE_DWORD) return;
        // on Xbox 360:
        Assert(state>=D3DRS_ZENABLE); // D3DRS_ZENABLE is the lowest value of the enum (40)
        Assert(state%4==0); // all render states are a multiply of 4
        int index = (state-D3DRS_ZENABLE)>>2;
#else
        int index = state;
#endif
        // we should not be inside of a predicated region
        EngineDD11::RendStateExt &cbState = _engine->CBState(cb);
        Assert(cbState._rec._currPredication==~0);
        // if states are not equal, assume invalid (unknown) state
        // we use -1, though chance is such value may actually be valid for some states!
        DWORD oldValue = -1;
        if (cbState._renderState[0].Size()>index)
        {
          oldValue = cbState._renderState[0][index];
          for (int i=1; i<NPredicationModes; i++)
          {
            if (cbState._renderState[i].Size()<=index || cbState._renderState[i][index]!=oldValue)
            {
              oldValue = -1;
              break;
            }
          }
        }
        _oldValue = oldValue;
        _engine->D3DSetRenderState(cb, state, value);
      }
    }
    //! Destructor - restore the old state
    ~SetRenderStateTemp()
    {
      // If the state was not previously set, keep the current value
#ifndef _XBOX
#ifndef _X360SHADERGEN
      if (_oldValue == -1)
        return;
#endif
#endif

      if (_direct)
      {
        _engine->DoSetRenderState(_state, _oldValue);
      }
      else
      {
        _engine->D3DSetRenderState(-1, _state, _oldValue);
      }
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  };

  //! Temporary setting of a sampler state
  /*!
  The state will be returned to it's previous value as soon as this object is destroyed
  */
  class SetSamplerStateTemp
  {
  private:
    //! Engine
    static EngineDD11 *_engine;
    //! Stage we set and then set back
    int _stage;
    //! State we set and then set back
    D3DSAMPLERSTATETYPE_OLD _state;
    //! Old value to set back
    DWORD _oldValue;
    //! If we are in direct execution m ode
    bool _direct;
  public:
    //! Constructor - remember the old state and value, set the new one
    SetSamplerStateTemp(int stage, D3DSAMPLERSTATETYPE_OLD state, DWORD value, bool direct = false): _direct(direct), _stage(stage), _state(state)
    {
      if (_direct)
      {
        if (stage >=0 && stage < EngineDD11::MaxSamplerStages && state >= 0 && state < _engine->_currentRenderState._samplerState[stage].Size())
        {
          _oldValue = _engine->_currentRenderState._samplerState[stage][state];
        }
        else
        {
          _oldValue = -1;
        }
        _engine->DoSetSamplerState(stage, state, value);
      }
      else
      {
        const int cb = -1;
#ifdef _XBOX
        // TODOX360: eliminate states which do not exists
        if (state<0 || state>=D3DSAMP_FORCE_DWORD) return;
        // on Xbox 360:
        Assert(state%4==0); // all render states are a multiply of 4
        int index = (state>>2);
#else
        int index = state;
#endif

#if _ARMA3_PREDICATED_SAMPLER_STATES
        // we should not be inside of a predicated region
        EngineDD11::RendStateExt &cbState = _engine->CBState(cb);
        Assert(cbState._rec._currPredication==~0);
        // if states are not equal, assume invalid (unknown) state
        // we use -1, though chance is such value may actually be valid for some states!
        DWORD oldValue = -1;
        Assert(stage >= 0 && stage < MaxSamplerStages);
        if (stage >= 0 && stage < MaxSamplerStages)
        {
          if (cbState._samplerState[0][stage].Size()>index)
          {
            oldValue = cbState._samplerState[0][stage][index];
            for (int i=1; i<NPredicationModes; i++)
            {
              if (cbState._samplerState[i][stage].Size()<=index || cbState._samplerState[i][stage][index]!=oldValue)
              {
                oldValue = -1;
                break;
              }
            }
          }
        }
        _oldValue = oldValue;
#else
        _engine->CBState(cb)._samplerState[stage].Access(index);
        RenderStateInfo &info = _engine->CBState(cb)._samplerState[stage][index];
        _oldValue = info;
#endif
        _engine->D3DSetSamplerState(cb, stage, state, value);
      }
    }
    //! Destructor - restore the old state
    ~SetSamplerStateTemp()
    {
      // If the state was not previously set, keep the current value
      if (_oldValue == -1)
        return;

      if (_direct)
      {
        _engine->DoSetSamplerState(_stage, _state, _oldValue);
      }
      else
      {
        _engine->D3DSetSamplerState(-1, _stage, _state, _oldValue);
      }
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  };


  //! Temporary setting of a shader constant
  /*!
  The value will be returned to it's previous value as soon as this object is destroyed
  */
  class PushShaderConstant: private ::NoCopy
  {
  protected:
    static const int MAX_FLOAT_COUNT = 256;
    //! Engine
    static EngineDD11 *_engine;
    //! State we set and then set back
    const ShaderConstant& _constant;
    //! Old value to set back
    float _oldValue[MAX_FLOAT_COUNT];
    //! how many floats we stored
    int _numStoredFloats;
    //! if we are in direct execution mode
    bool _direct;
  public:
    //! Constructor - remember the old state and value, set the new one
    PushShaderConstant(const ShaderConstant& constant, int numFloats, bool direct = false)
      :_constant(constant), _numStoredFloats(numFloats), _direct(direct)
    {
      Assert(_numStoredFloats < MAX_FLOAT_COUNT);
      saturateMin(_numStoredFloats, MAX_FLOAT_COUNT);

      if (_direct)
      {
        _engine->_currentRenderState._shaderConstants.GetConstantValue(_constant, _oldValue, _numStoredFloats);
      }
      else
      {
        const int cb = -1;
        _engine->CBState(cb)._shaderConsts.GetConstantValue(_constant, _oldValue, _numStoredFloats);
      }
    }
    //! Destructor - restore the old state
    ~PushShaderConstant()
    {
      if (_direct)
      {
        _engine->DoSetShaderConstantF(&_constant, _numStoredFloats, 0, Array<float>(_oldValue, _numStoredFloats));
      }
      else
      {
        const int cb = -1;
        _engine->SetShaderConstantF(cb, _constant, _oldValue, _numStoredFloats);
      }
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  };

  //! Temporary setting of a  shader constant
  /*!
  The value will be returned to it's previous value as soon as this object is destroyed
  */
  class SetShaderConstantTemp: public PushShaderConstant
  {
  public:
    //! Constructor - remember the old state and value, set the new one
    SetShaderConstantTemp(const ShaderConstant& constant, const float *value, int numFloats = 4, bool direct = false)
      :PushShaderConstant(constant, numFloats, direct)
    {
      if (direct)
      {
        _engine->DoSetShaderConstantF(&constant, numFloats, 0, Array<float>((float*)value, numFloats));
      }
      else
      {
        _engine->SetShaderConstantF(-1,constant, value, numFloats);
      }
    }
  };

  //! Temporary setting of a texture
  /*!
  The value will be returned to it's previous value as soon as this object is destroyed
  */
  class SetTextureTemp
  {
  private:
    //! Engine
    static EngineDD11 *_engine;
    //! Stage we set and then set back
    int _stage;
    //! Old texture to set back
    TextureHandle11* _oldTex;
    //! If texture should be set directly
    bool _direct;
  public:
    //! Constructor - remember the old state and value, set the new one
    SetTextureTemp(int stage, TextureHandle11* tex, bool direct = false)
    {
      const int cb = -1;
      _oldTex = _engine->CBState(cb)._lastHandle[stage];
      _stage = stage;
      _direct = direct;
      if (_direct)
      {
        _engine->DoSetTexture(stage, tex);
      }
      else
      {
        _engine->SetTexture(cb, stage, tex);
      }
    }
    //! Destructor - restore the old state
    ~SetTextureTemp()
    {
      if (_direct)
      {
        _engine->DoSetTexture(_stage, _oldTex);
      }
      else
      {
        _engine->SetTexture(-1, _stage, _oldTex);
      }
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  };

  //! class used to remember and restore current stream source
  class PushStreamSource: private ::NoCopy
  {
  private:
    //! Engine
    static EngineDD11 *_engine;
    //! old buffer
    VertexStaticData11::BufferFuture _oldBuf;
    //! old stride
    UINT _oldStride;
    //! old buffer size
    int _oldBufferSize;
    //! if we are in "direct" mode 
    bool _direct;
  public:
    //! Constructor - remember the old stream data
    PushStreamSource(bool direct = false) : _direct(direct)
    {
      if (_direct)
      {
        UINT offset;
        ComRef<ID3D11Buffer> buf;

        _engine->GetD3DDeviceRec()->IAGetVertexBuffers(0, 1, buf.Init(), &_oldStride, &offset);
        _oldBuf = VertexStaticData11::BufferFuture(buf);
        _oldBufferSize = 0; //we don't need this
      }
      else
      {
        int cb = -1;
        const RendStateExt& cbState = _engine->CBState(cb);
        _oldBuf = cbState._vBufferLast;
        _oldStride = cbState._strideLast;
        _oldBufferSize = cbState._vBufferSize;
      }
    }
    //! Destructor - restore the old state
    ~PushStreamSource()
    {
      if (_direct)
      {
        _engine->DoSetStreamSource(_oldBuf, _oldStride, _oldBufferSize);
      }
      else
      {
        int cb = -1;
        _engine->SetStreamSource(cb, _oldBuf, _oldStride, _oldBufferSize);
      }
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  };

  //! class used to remember and restore current vertex shader
  class PushVertexShader: private ::NoCopy
  {
  private:
    //! Engine
    static EngineDD11 *_engine;
    //! old shader info
    VertexShaderInfo _oldInfo;
    //! if we are in direct execution mode
    bool _direct;
  public:
    //! Constructor - remember the old vertex shader
    PushVertexShader(bool direct = false) : _direct(direct)
    {           
      _engine->GetD3DDeviceRec()->IAGetInputLayout(_oldInfo._inputLayout.Init());
      _engine->GetD3DDeviceRec()->VSGetShader(_oldInfo._shader.Init(), 0, 0);
    }
    //! Destructor - restore the old state
    ~PushVertexShader()
    {
      if (_direct)
      {
        _engine->DoSetVertexShader(_oldInfo._shader, _oldInfo._inputLayout);
      }
      else
      {
        _engine->SetVertexShader(-1, _oldInfo);
      }
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  };

  //! class used to remember and restore current pixel shader
  class PushPixelShader: private ::NoCopy
  {
  private:
    //! Engine
    static EngineDD11* _engine;
    //! old shader 
    ComRef<ID3D11PixelShader> _oldShader;
    //! if we are in direct execution mode
    bool _direct;
  public:
    //! Constructor - remember the old vertex shader
    PushPixelShader(bool direct = false) : _direct(direct)
    {           
      _engine->GetD3DDeviceRec()->PSGetShader(_oldShader.Init(), 0, 0);
    }
    //! Destructor - restore the old state
    ~PushPixelShader()
    {
      if (_direct)
      {
        _engine->DoSetPixelShader(_oldShader);
      }
      else
      {
        _engine->SetPixelShader(-1, _oldShader);
      }
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  };

  //! class used to remember and restore current view shader
  class PushViewport: private ::NoCopy
  {
  protected:
    //! Engine
    static EngineDD11* _engine;
    //! old viewport 
    D3D11_VIEWPORT _oldViewport;
    //! if we are in direct execution mode
    bool _direct;

  public:
    PushViewport(bool direct): _direct(direct)
    {
      UINT numViewports = 1;
      _engine->GetD3DDeviceRec()->RSGetViewports(&numViewports, &_oldViewport);
    }

    ~PushViewport()
    {
      if (_direct)
      {
        _engine->SetViewportDirect(_oldViewport.TopLeftX, _oldViewport.TopLeftY, _oldViewport.Width, 
          _oldViewport.Height, _oldViewport.MinDepth, _oldViewport.MaxDepth);
      }
      else
      {
        _engine->SetViewport(_oldViewport.Width, _oldViewport.Height, _oldViewport.MinDepth, _oldViewport.MaxDepth);
      }
    }

    //! Setting of the engine pointer
    static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  };

  //! class used to remember and restore current view, render target and depth/stencil
  class PushRenderTargetAndView: public PushViewport
  {
  private:
    //! old info
    RenderTargetDepthStencilInfo _oldInfo;
  public:
    PushRenderTargetAndView(bool direct): PushViewport(direct)
    {
      _oldInfo = RenderTargetDepthStencilInfo(_engine->_currRenderTarget, _engine->_currDepthStencil, _engine->_minZRange, _engine->_maxZRange);
    }

    ~PushRenderTargetAndView()
    {
      if (_direct)
      {
        _engine->SetRenderTargetAndDepthStencilDirect(_oldInfo._renderTarget, _oldInfo._depthStencil, _oldInfo._minZRange, _oldInfo._maxZRange);
      }
      else
      {
        _engine->SetRenderTargetAndDepthStencil(_oldInfo._renderTarget, _oldInfo._depthStencil, _oldInfo._minZRange, _oldInfo._maxZRange);
      }
    }   
  };

  CircularArray<MainThreadRenderTask *, 256> _mainThreadTasks;
  struct DrawText3DCenteredAsyncPars;

  AutoArray<MultiSizeConstantBuffer> _shaderConstantBuffer[ShaderConstant::SCTCount];

  // default values, that are used if the value is not set
  struct CurrentRenderState
  {
    CurrentRenderState()
    {
      Reset();
    }

    void Reset()
    {
      // we cannot reset constants - we set them to zero and flags all as dirty (so that all will be sent to the GPU for the first time)
      _shaderConstants.SetZeroAndDirty();

      for (int i=0; i<MaxSamplerStages; ++i)
      {
        _lastHandle[i] = 0;
        _setHandle[i] = 0;
        _samplerStateLast[i].Free();

        for (int j=0; j<_samplerState[i].Size(); ++j)
          _samplerState[i][j] = -1;

        _samplerStateChanged[i] = true;
      }

      _depthStencilStateLast.Free();
      _blendStateLast.Free();
      _rasterizerStateLast.Free();

      _lastTopology = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;

      _lastVertexShader.Free();
      _lastInputLayout.Free();
      _lastPixelShader.Free();

      for (int shaderType = 0; shaderType < ShaderConstant::SCTCount; ++shaderType)
      {
        for (int i=0; i< ShaderConstantBuffer::MaxConstantBufferCount; ++i)
        {
          _constantBufferLast[shaderType][i] = 0;
        }
      }

    }

    AutoArray<RenderStateInfo> _renderState;
    AutoArray<SamplerStateInfo> _samplerState[MaxSamplerStages];
    TextureHandle11* _lastHandle[MaxSamplerStages];
    TextureHandle11* _setHandle[MaxSamplerStages];
    ComRefFake<ID3D11DepthStencilState> _depthStencilStateLast;
    ComRefFake<ID3D11BlendState> _blendStateLast;
    ComRefFake<ID3D11RasterizerState> _rasterizerStateLast;
    ComRefFake<ID3D11SamplerState> _samplerStateLast[MaxSamplerStages];
    bool _samplerStateChanged[MaxSamplerStages];
    ShaderConstantBufferList _shaderConstants;
    D3D11_PRIMITIVE_TOPOLOGY _lastTopology;
    ComRefFake<ID3D11VertexShader> _lastVertexShader;
    ComRefFake<ID3D11InputLayout> _lastInputLayout;
    ComRefFake<ID3D11PixelShader> _lastPixelShader;
    ComRefFake<ID3D11Buffer> _constantBufferLast[ShaderConstant::SCTCount][ShaderConstantBuffer::MaxConstantBufferCount]; 
  };

  CurrentRenderState _currentRenderState;  

#if !DX11_USE_HASHED_STATE_OBJECTS
  DepthStencilStateList _depthStencilStateList;
  BlendStateList _blendStateList;
  RasterizerStateList _rasterizerStateList;
  SamplerStateList _samplerStateList;
#endif //!DX11_USE_HASHED_STATE_OBJECTS

  /// if Present() was required during HL recording
  bool _hlPresentRequired;

  struct UnderwaterPars
  {
    UnderwaterPars() : _occlusionDistance(0), _underwaterDepth(0), _waterDepthFogStart(0) {}

    /// distance of underwater occlusion model
    float _occlusionDistance;
    /// depth under water (negative values = distance above water)
    float _underwaterDepth;
    /// waterDepthFogStart - depth where fog should start
    float _waterDepthFogStart;
    /// color of the water fog
    Color _fogColor;
  };

  UnderwaterPars _underwaterPars;

#if _ARMA3_REFRACTION
  struct RefractionPars
  {
    RefractionPars() : _intensity(1.0f), _scales(0.04f, 0.03f, 0.05f), _timeCoefs(0.3f, 0.3f, 0.3f) {}

    /// intensity of refract effect
    float _intensity; 
    /// scales of refract effect (r, g, b)
    Vector3 _scales;
    /// time coefs for animation (horizontal, vertical, not used)
    Vector3 _timeCoefs;
  };

  RefractionPars _refractionPars;
  /// number of BG captures this frame
  int _numBgCaptures;
  /// maximal possible BG captures in one frame
  static const int MaxBgCaptures = 10;
#endif

#if _ARMA3_REFRACTION_SPRITES
  struct SpriteRefractionPars
  {
    SpriteRefractionPars() : _intensity(1.0f), _scales(0.02f, 0.02f, 0.02f), _timeCoefs(0.3f, 0.3f, 0.3f) {}

    /// intensity of refract effect
    float _intensity; 
    /// scales of refract effect (r, g, b)
    Vector3 _scales;
    /// time coefs for animation (horizontal, vertical, not used)
    Vector3 _timeCoefs;
  };

  RefractionPars _spriteRefractionPars;
#endif

#if _ARMA3_DX11_PROFILING
  //@{ Profiling counters

  struct ProfileInfo
  {
    ProfileInfo() { Clear(); }
    /// number of setups
    int _profileNumSetups;    

    void Clear()
    {
      _profileNumSetups = 0;
    }
  };

  /// current profile info
  mutable ProfileInfo _profileInfoCur;
  /// profile info from previous frame (we need to use this, because info from current frame may not be filled correctly in the moment we ask for it (due to multithreaded rendering)
  mutable ProfileInfo _profileInfoPrev;
  /// needs to be called when next frame starts
  void ProfileNextFrame();
  /// reports profiling info to output
  void ProfileReport();

  //@}
#endif


  //========================================================
  // Private member functions
  //========================================================
  typedef AutoArray<D3D10_SHADER_MACRO, MemAllocLocal<D3D10_SHADER_MACRO,16> > D3DXMacroList;

  ID3D11PixelShader *PSFromCache(const char *psName) const
  {
    PSName name;
    name.name = psName;
    int index = _psCache.FindKey(name);
    if (index>=0) return _psCache[index].ps;
    ErrF("Shader %s not found in cache",psName);
    return NULL;
  }

  void ProcessLRURequests();

  int CBStateCount() const {return _cbState.Size();}
  /// count of real CBs, ignoring immediate state
  int CBCount() const {return _cbState.Size()-1;}

  RendStateExt &CBState(int cb)
  {
    Assert(VerifyLowLevelThreadAccess());
    return _cbState[cb+1];
  }
  const RendStateExt &CBState(int cb) const
  {
    Assert(VerifyLowLevelThreadAccess());
    return _cbState[cb+1];
  }
  /** used for asserts only - can introduce race conditions, thread access rights not verified */
  const RendStateExt &CBStateUnsafe(int cb) const {return _cbState[cb+1];}

  /// verify thread accessing low-level functionality can do so  
  bool VerifyLowLevelThreadAccess() const;
  void AssertLowLevelThreadAccess() const {Assert(VerifyLowLevelThreadAccess());}

  // special level thread access -> in DX11 we are recording some engine functions as low level
  bool VerifySpecialLevelThreadAccess() const;
  void AssertSpecialLevelThreadAccess() const {Assert(VerifySpecialLevelThreadAccess());}
  

  /// get current render target for sampling
  TextureHandle11* GetCurrRenderTargetAsTexture();

  /// get depth target (used for soft sprites and depth of field effects)
  TextureHandle11* GetDepthAsTexture() const;

  /// we need to render to the backbuffer
  void SwitchToBackBufferRenderTarget();

  /// make sure we have a copy of the current render target
  TextureHandle11* CreateRenderTargetCopyAsTexture
    (
#if _ARMA3_REFRACTION
    int cb = -1
#endif
    );

  /// pushes current render target and depth/stencil to the saved queue
  void PushRenderTargetDepthStencil();
  /// pops render target and depth/stencil and sets it as current render target
  void PopRenderTargetDepthStencil();

  /// create basic preprocessor defines for shader compilation
  void CreateBasicDefines(D3DXMacroList &list, bool close=true);

  /// initialize given shader model
  void InitShaderModel(int select=-1);

  bool InitVertexShaders(VertexShaderID vs = NVertexShaderID);
  void DeInitVertexShaders();
  void ReloadVertexShader(VertexShaderID ps);
  void ReloadPixelShader(PixelShaderID vs);

  bool InitPixelShaders(PixelShaderID from=PixelShaderID(0), PixelShaderID to=PixelShaderID(NPixelShaderID-1));
  void DeInitPixelShaders();

  //! Method to compile and create a vertex shader
  void CreateShader(RString filename, CONST D3D10_SHADER_MACRO *pDefines,
    LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags, const VertexDeclaration &ie,
    VertexShaderInfo& vertexShader);
  void CreateShader(LazyLoadFile &hlslSource, CONST D3D10_SHADER_MACRO *pDefines,
    LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags, const VertexDeclaration &ie,
    VertexShaderInfo& vertexShader);

  bool CreateTextBank(ParamEntryPar cfg, ParamEntryPar fcfg);
  void DestroyTextBank();

  bool CreateShaderDeclarations();
  void DestroyShaderDeclarations();

  bool CreatePostProcessStuff();

  void DestroyPostProcessStuff();

  // initialize all post process effects, typically called over device reset
  bool InitPostProcessStuff();

  bool NewPostProcessStuff(bool firstInit);
  // release resources allocated by post process effects, called before device reset 
  void ReleasePostProcessStuff();

  void WorkToBack();
  void BackToFront();

  /// body implementing Postprocess functionality
  /** used from Active Object servant as well */
  void DoPostprocess(float deltaT, const PostFxSettings &pfx);
  void DoNoPostprocess();
  void DoPresentBackbuffer();

#if _ARMA3_SIMUL_WEATHER
  void DoUpdateSimulWeatherTexture(TextureSysGPUDX11 *tex, unsigned textureIndex);
#endif

  void SearchMonitor(int &x, int &y, int &w, int &h);

  bool InitDXGIAdapter();
  bool InitDXGIOutput();
  void InitDirectDraw7();

  void DeInitCursors();

  /// get approximate adapter performance
  int GetAdapterLevel(IDXGIAdapter* adapter) const;

  /// prepare presentation parameters based on _w, _h, _pixelSize
  void FindMode(); 

  void ShadowsStatesSettings(int cb, EShadowState ss, bool noBackCull)
  {
    if (ss == CBState(cb)._currentShadowStateSettings && CBState(cb)._backCullDisabled==noBackCull) return;
    DoShadowsStatesSettings(cb,ss,noBackCull);
  }
  void DoShadowsStatesSettings(int cb, EShadowState ss, bool noBackCull);

  void PrepareDetailTex(int cb, const TexMaterialLODInfo &mat, const EngineShapeProperties &prop);
  void PrepareSingleTexDiffuseA(int cb, const TexMaterialLODInfo &mat);

  void EnableCustomTexGen(int cb, const PASSMATRIXTYPE **matrix, int nMatrices, bool optimize);
  void EnableCustomTexGenZero(int cb, bool optimize);

  void D3DEnableAlphaToCoverage(int cb, bool value, bool optimize=true);

  PredicationMode CurrentPredicationMode() const
  {
    if (_renderingMode==RMDPrime)
    {
#ifndef _XBOX
      if (IsFSAAEnabled()) return DepthPass;
#endif
      return DepthPass;
    }
    if (_renderingMode==RMZPrime) return PrimingPass;

#if _ARMA3_CAUSTICS
    if (_renderingMode==RMCaustics) return CausticsPass;
#endif    

    Assert(_renderingMode==RMCommon || _renderingMode==RMShadowBuffer);
    return RenderingPass;
  }

  bool BeginPredication(int cb, PredicationMode mode,PredicationMode pass)
  {
    return BeginPredicationMask(cb,(1<<mode),pass);
  }
  bool BeginPredication(int cb, PredicationMode mode1,PredicationMode mode2,PredicationMode pass)
  {
    return BeginPredicationMask(cb,(1<<mode1)|(1<<mode2),pass);
  }
  bool BeginPredicationMask(int cb, int mask, PredicationMode pass);

  /// mark part of D3D calls which need to be executed only in some particular pass
  template <PredicationMode mode>
  bool BeginPredication(int cb, PredicationMode pass)
  {
    return BeginPredication(cb,mode,pass);
  }
  template <PredicationMode mode1, PredicationMode mode2>
  bool BeginPredication(int cb, PredicationMode pass)
  {
    return BeginPredicationMask(cb,(1<<mode1)|(1<<mode2),pass);
  }
  /// end predicated part marked by BeginPredication
  void EndPredication(int cb);

  /// mark part of D3D calls which may to be executed only in some particular pass
  /**
  advisory only: the engine is free to execute them if skipping them would be difficult to implement
  The reason for this is so that code may be skipped when predication is done record time,
  but executed when predicating play time, because cost of having different paths is higher than cost of the calls.
  */
  template <PredicationMode mode>
  bool BeginPredicationAdvisory(int cb, PredicationMode pass) const
  {
    //return BeginPredication<mode>(cb,pass);
    return true;
  }
  template <PredicationMode mode1, PredicationMode mode2>
  bool BeginPredicationAdvisory(int cb, PredicationMode pass) const
  {
    //return BeginPredication<mode>(cb,pass);
    return true;
  }
  void EndPredicationAdvisory(int cb){/*EndPredication(cb);*/}

  //! Setting of HW to undefined state
  /*!
  After calling of this function every first HW setting will be performed
  */
  void SetHWToUndefinedState(int cb);
  /// sets all command buffers to undefined state
  void SetCommandBuffersToUndefinedState();
  //! Finishing of PB drawing - invalidating of high-level lazy variables
  void PBDrawingCleanup(int cb);
  /// make sure our shadow state is consistent to what UnsetAll did
  void SimulateUnsetAll(int cb);

  /// initialize a device into a reasonable default state
  void InitDeviceState(int cb);

  /// initialize a device used for 3D scene rendering into a reasonable default state
  void InitDeviceState3D(int cb);

  void SetTexture(int cb, int stage, TextureHandle11* tex, bool optimize = true);
  // sets given texture directly to the pixel shader stage (calls D3D function)
  // this is needed for some special cases (ie we need to unset shadow map texture PS stage)
  void SetTextureDirect(int cb, int stage, TextureHandle11* tex, bool optimize = true);

  void SetStreamSource(int cb, const VertexStaticData11::BufferFuture &buf, int stride, int bufSize, bool optimize = true);

  //\param additionalFloat4Offset - additional offset that should be added to constant offset (!! number of float4 values !! not bytes or floats)
  bool SetVertexShaderConstantF(int cb, const ShaderConstant& constant, const float *data, int vector4fCount, bool optimize = true, int additionalFloat4Offset = 0);
  bool SetVertexShaderConstantI(int cb, const ShaderConstant& constant, const int *data, int vector4fCount, bool optimize = true);
  bool SetVertexShaderConstantB(int cb, const ShaderConstant& constant, const bool *data, int boolCount, bool optimize = true);

  //\param additionalFloat4Offset - additional offset that should be added to constant offset (!! number of float4 values !! not bytes or floats)
  bool SetPixelShaderConstantF(int cb, const ShaderConstant& constant, const float *data, int vector4fCount, bool optimize = true, int additionalFloat4Offset = 0);
  bool SetPixelShaderConstantI(int cb, const ShaderConstant& constant, const int *data, int vector4fCount, bool optimize = true);
  bool SetPixelShaderConstantB(int cb, const ShaderConstant& constant, const bool *data, int count, bool optimize = true);

  bool SetShaderConstantF(int cb, const ShaderConstant& constant, const float *data, int floatCount, bool optimize = true, int additionalFloat4Offset = 0);
  bool SetShaderConstantI(int cb, const ShaderConstant& constant, const int *data, int floatCount, bool optimize = true);
  bool SetShaderConstantB(int cb, const ShaderConstant& constant, const bool *data, int boolCount, bool optimize = true);

  void ResetShaderConstantValues(int cb);

  void DoSwitchTL(int cb, TLMode active );

  void SetVertexDeclaration(int cb, ID3D11InputLayout *decl, bool optimize = true);

  void ShowDeviceCursor(bool showIt);

  void ResetLogDevice();

  struct D3DCommandBufRecWithSubmit 
  {
    D3DCommandBufRec &_ptr;

    D3DCommandBufRecWithSubmit(D3DCommandBufRec &ptr):_ptr(ptr){}
    D3DCommandBufRec *operator ->() const {return &_ptr;}
    ~D3DCommandBufRecWithSubmit()
    {
      GEngineDD11->KickOffHighLevel();
    }
  };
  D3DCommandBufRecWithSubmit RecordHighlevel();
  void KickOffHighLevel();

  void DoSetGamma();
 
  /// verify current estimations
  void CheckLocalNonlocalTextureMemory();
  /// create resources needed for shadow buffers
  void CreateSBResources();
  /// destroy resources needed for shadow buffers
  void DestroySBResources();

  /// Create common resources
  bool CreateResources();
  /// Destroy common resources
  void DestroyResources();

  //! Create resources needed for thermal imaging
  bool CreateTIResources();
  //! Destroy resources needed for thermal imaging
  void DestroyTIResources();

  // Method to forget buffers used by the device (function is used prior removing or locking buffers)
  void ForgetDeviceBuffers(int cb);

  void CreateVB();
  void DestroyVB();

  void CreateVBTL();
  void DestroyVBTL();

  void RestoreVB();

  /** Create new render target
  \param createShaderResource - if true, shader resource will be created for render target
  \param viewFormat - you can specify format for render target view here (if DXGI_FORMAT_UNKNOWN, then param format is used)
  \param srFormat - you can specify format for shader resource view here (if DXGI_FORMAT_UNKNOWN, then param format is used)
  */
  HRESULT CreateRenderTarget(
    UINT width, UINT height,
    DXGI_FORMAT format,
    UINT multisampleCount, UINT multisampleQuality,
    D3D11_USAGE usage, 
    bool createShaderResource,
    RenderTarget& result,
    DXGI_FORMAT rtFormat = DXGI_FORMAT_UNKNOWN,
    DXGI_FORMAT srFormat = DXGI_FORMAT_UNKNOWN
    );

  /** Create new render target
  \param createShaderResource - if true, shader resource will be created 
  \param viewFormat - you can specify format for render depth stencil view here (if DXGI_FORMAT_UNKNOWN, then param format is used)
  \param srFormat - you can specify format for shader resource view here (if DXGI_FORMAT_UNKNOWN, then param format is used)
  */
  HRESULT CreateDepthStencil(
    UINT width, UINT height,
    DXGI_FORMAT format,
    UINT multisampleCount, UINT multisampleQuality,
    D3D11_USAGE usage, 
    bool createShaderResource,
    DepthStencil& result,
    DXGI_FORMAT dsFormat = DXGI_FORMAT_UNKNOWN,
    DXGI_FORMAT srFormat = DXGI_FORMAT_UNKNOWN
    );

  void SwitchTL(int cb,TLMode active)
  {
    if (active==CBState(cb)._tlActive) return;
    DoSwitchTL(cb,active);
  }

  template <class WorkerThreadClass>
  void WaitUntilSpaceAvailable(WorkerThreadClass &bg, int nResults);

  void StartBackgroundScope();
  void EndBackgroundScope(bool flushMem, const char *pixScope);

  /// apply values set by Wanted functions
  void ApplyWanted();

  // prepare _wRT,_hRT based on _w,_h
  bool FindFillrateControl(int wRTPreferred, int hRTPreferred);

  //! Uses _world and _invWorld matrices to transform specified position to object-space
  Vector3 TransformPositionToObjectSpace(int cb, Vector3Par position);
  //! Uses _world and _invWorld matrices to transform and normalize specified position to object-space
  Vector3 TransformAndNormalizePositionToObjectSpace(int cb, Vector3Par position);
  //! Transforms a vector to object space
  Vector3 TransformAndNormalizeVectorToObjectSpace(int cb, Vector3Par vector);
  /// interface for tree shader
  const Vector3 *GetTreeCrown(int cb) const {return CBState(cb)._treeCrown;}

  void SetFiltering(int cb, int stage, TexFilter filter, int maxAFLevel);
  void SetMultiTexturing(int cb, TextureD3D11 **tex, int count, int keepUntouchedLimit = TEXID_FIRST);
  void SetTextureAndMaterial( int cb, const TextureD3D11 *tex, const TexMaterial *mat, int specFlags, int filterFlags );

  void SetTexture0Color(int cb, ColorVal maxColor, ColorVal avgColor, bool optimize);
  void SetTexture0(int cb, const TextureD3D11 *tex);

  //! Destroying of all possible surfaces and buffers
  /*!
  \param hard Release system textures as well
  */
  void PreReset(bool hard, bool resizeTargets, bool setFullscreenState);
  void PostReset(bool hard);
  /// can be used before time consuming engine switch (resolution switch...)
  void DisplayWaitScreen();
  /**  use Reset to change setting / reset device
  \param hard - if reset should be hard (device will be destroyed)
  \param resizeTargets - if swapchain targets should be resized
  \param setFullscreenState - if fullscreen state changed */
  bool Reset(bool hard, bool resizeTargets, bool setFullscreenState);

  void InitTexMemEstimations();

  //! Method to set bones constants on the GPU
  void SetBones(int cb, int minBoneIndex, const Matrix4 *bones, int nBones);

  //! quality of ssao
  void SetSSAOQuality(int quality);

  void SwitchRecordingToHighLevel();
  void EndHighLevelRecording();
  bool EndHighLevelRecordingPart1();
  void EndHighLevelRecordingPart2(bool part1Result);

   Ref<EngineDD11::PostProcess> CreatePostprocess(PostEffectType pEType);

  void DoCalculateFlareIntensity(int x, int y, int sizeX, int sizeY);
  int FrameTime() const;

  // helper for various freeing implementations
  size_t FreeOneItem(int minAge);
  size_t Free(size_t size, int minAge);

  void CheckFreeTextureMemory(int &local, int &nonlocal) const;

  float GetResetFactor() const;

  void DoForgetAvgIntensity();

  /// device corresponding to a given command buffer
  D3DCommandBufRec &CB(int cb)
  {
    Assert( GetCurrentThreadId() != _bg._threadId );
    return _cbState[cb+1]._rec;
  }

  //!{ Methods to encapsulate the 3D rendering of the scene
  /*!
  This is used for instance for predicated tiling on X360
  */
  void Begin3DRendering();
  void End3DRendering();
  //!}

  void SetFogColor();

  void DoSwitchRes(bool reset3DSize,bool setFullscreenState);

  void DoSetFogColor(ColorVal fog, ColorVal waterFogColor);

  void AdjustWindowAttributes();

  float GetPostHDRBrightness() const;

  void DoFinishRendering();

  /// background rendering thread - processing data
  void BGThreadProcess();
  /// background rendering thread
  void BGThread();

  /// background rendering thread - processing data
  void VisThreadProcess();
  /// background rendering thread
  void VisThread();

  void SwitchRenderMode( RenderMode mode )
  {
    if (_renderMode==mode) return;
    DoSwitchRenderMode(mode);
  }
  void DoSwitchRenderMode( RenderMode mode );

  WORD *QueueAdd( int n );
  void QueueFan( const VertexIndex *ii, int n );

  void Queue2DPoly( const TLVertex *v, int n );

  void AllocateQueue(TextureD3D11 *tex, int level, const TexMaterial *mat, int spec);
  void FlushAndFreeQueue();
  void FlushAndFreeAllQueues();
  void FlushQueue();
  void FreeQueue();
  void FreeAllQueues();
  void FlushAllQueues();

  void CloseAllQueues();

  void DoPrepareTriangle(int cb, TextureD3D11 *tex, const TexMaterialLODInfo &mat, int level, int spec, const EngineShapeProperties &prop);
  //! Set the current material, combine material color with lights
  void DoSetMaterial(int cb, const TLMaterial &mat);
  void DoDrawDecal(int cb, Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color, const MipInfo &mip, const TexMaterial *mat, int specFlags);

  //@{ common code sequences
  void SetPixelShaderConstant_A_DFORCED_E_NoSun(int cb, ColorVal matEmmisive);
  void SetPixelShaderConstant_A_DFORCED_E_Sun(int cb, ColorVal matEmmisive, ColorVal matAmbient, ColorVal matDiffuse, ColorVal matForcedDiffuse);
  void SetPixelShaderConstant_A_DFORCED_E_Black(int cb);
  void SetVertexShaderConstantSAFR(int cb, float matPower, Color matAmbient);
  void SetVertexShaderConstantFFAR(int cb);
  void SetVertexShaderConstantLWSM(int cb);
  //! set calm water specific params to vertex shader const regs
  void SetVertexShaderConstantCalmWater( int cb, const RendState &cbState );
  //! set calm water specific params to pixel shader const regs
  void SetPixelShaderParamsCalmWater(int cb );
  void SetTextureSBR(int cb, bool alphaPrimitives);
  void SetPixelShaderConstantDBS(int cb, ColorVal matDiffuse, ColorVal matForcedDiffuse, ColorVal matSpecular, float matPower, bool useGroundBidir, bool useNewSpecular);
  void SetPixelShaderConstantDBSZero(int cb);
  void SetPixelShaderConstantLayers(int cb, int layerMask, const TexMaterial *mat);
  void SetPixelShaderConstantWater(int cb);
  void SetPixelShaderConstantWaterSimple(int cb);
  void SetPixelShaderConstantSuper(int cb, ColorVal matSpecular);
  void SetPixelShaderConstantSkin( int cb, ColorVal matAmbient, ColorVal matDiffuse, ColorVal matForcedDiffuse, ColorVal matEmissive, ColorVal matSpecular );
  void SetPixelShaderConstantTreeAdv( int cb, ColorVal matAmbient, ColorVal matDiffuse, ColorVal matForcedDiffuse, ColorVal matEmissive, ColorVal matSpecular );
  void SetPixelShaderConstantTreePRT(int cb, ColorVal matEmmisive, ColorVal matAmbient, ColorVal matDiffuse);
  void DrawIndexedPrimitive(int cb, int beg, int end, const Shape &sMesh, int instancesOffset, int instancesCount);
  void SetTextureR(int cb, DWORD stage, const TextureD3D11 *texture);
  void SetPixelShaderConstantUI(int cb, Vector3Val val, float intensity);
#if _ARMA3_CAUSTICS
  void SetPixelShaderConstantCaustic(int cb);
#endif
#if _ARMA3_REFRACTION
  void SetPixelShaderConstantsRefract(int cb, ColorVal matSpecular);
#endif
#if _ARMA3_REFRACTION_SPRITES
  void SetPixelShaderConstantsRefractSprite(int cb);
#endif

  void SetPixelShaderNightEmissiveConst(int cb);

  //@}

  //! Setting of the shader
  void SetupSectionTL(
    int cb, EMainLight mainLight, EFogMode fogMode,
    VertexShaderID vertexShaderID, const UVSource *uvSource, int uvSourceCount, bool uvSourceOnlyTex0,
    ESkinningType skinningType,
    bool instancing, int instancesCount, int pointLightsCount, int spotLightsCount,
    bool shadowReceiver, RenderingMode renderingMode, bool backCullDisabled, int creatersCount,
    const TexMaterial *mat
    );

  //!{ Sets the VS constants
  void SetupVSConstantsPSLights(int cb, bool setupPSInstead = false);
  void SetupVSConstantsLODPars(int cb);
  void SetupVSConstantsPool(int cb, int minBoneIndex, int bonesCount);
  void SetupVSConstantsShadowVolume(int cb, int minBoneIndex, int bonesCount);
  void SetupVSConstantsSprite(int cb);
  void SetupVSConstantsVolCloud(int cb);
  void SetupVSConstantsPoint(int cb);
  void SetupVSConstantsWater(int cb);
  void SetupVSConstantsShore(int cb);
  void SetupVSConstantsTerrain(int cb);
  void SetupVSConstantsTerrainGrass(int cb);
  void SetupVSConstants(int cb, int minBoneIndex, int bonesCount);
  void SetupVSConstantsUnderwaterOcclusion(int cb);
#if _VBS3_CPU_LAND_LIGHT
  void SetupVSConstantsLandLights(int cb);
#endif

  //!}

  //! Sets the PS constants
  void SetupPSConstants(int cb);

  void SelectPixelShader(int cb, PixelShaderID ps);
  void SelectPixelShaderType(int cb, PixelShaderType pixelShaderType);
  void DoSelectPixelShader(int cb, const TexMaterial *mat=NULL, int cratersCount=0);

  void UnsetAllDiscardableResources();
  void UnsetAllDiscardableResourcesFromCBState();
  /// verify all discardable resources are unset (corresponding pointers in the state are NULL)
  bool CheckAllDiscardableResourcesUnset() const;

  HRESULT LockDynBuffer(const ComRef<IDirect3DVertexBuffer9Old> &buf, void * &data, D3D11_MAP lockFlags, int lockOffset, int size);

  void DrawIndexedPrimitive(int cb, D3DPRIMITIVETYPE_OLD primitiveType, INT baseVertexIndex, UINT minIndex, UINT numVertices, UINT startIndex, UINT primitiveCount);
  void DrawPrimitive(int cb, 
    D3DPRIMITIVETYPE_OLD primitiveType,
    UINT startVertex,
    UINT primitiveCount);

  void SetupInstancesShadowVolume(int cb, const ObjectInstanceInfo *instances, int nInstances);
  //! Function to draw all sections in instanced version of drawing
  void DrawMeshTLSectionsInstanced(int cb, const Shape &sMesh, const SectionMaterialLODs &matLOD, const EngineShapeProperties &prop, int spec, int bias, int instancesOffset, int instancesCount);

  //! Function to calculate closest Z^2 upon specified mindist2 and Engine's aspect settings
  float GetClosestZ2(float minDist2);

  //! Setup all the lights for one model (main light and P&S lights)
  void SetupLights(int cb, const LightList &lights, int spec, const DrawParameters &dp);
  //! Combine material and light colors
  void SetupMaterialLights(int cb, const TLMaterial &mat);
  void ClearLights(int cb);

  void SetWorldView(int cb);
  /// setups world/view/projection matrix and camera position
  void SetupWorldViewProjCam(int cb, const PositionRender &modelToWorld);

  /// prepare offset needed to fight shadow acne
  void PrepareShadowOffset(int cb, const EngineShapeProperties &prop);

  float ComputeShadowConstants(float shadowsZ, float sunClosestDistanceFrom0, Matrix4Val shadowPlaneOrigin);
  Matrix4 ComputeShadowPlaneOrigin(float shadowsZ, float sunClosestDistanceFrom0, int layerIndex);
  /// Calculate the position of the frustum bounding sphere (in world coordinates)
  float ComputeShadowFrustumBoundingSphere(float shadowsZ, Vector3 &position);
  /// Calculate the shadow plane offset from center of the frustum bounding sphere
  float ComputeShadowPlaneOffset(Vector3Par position, float sunClosestDistanceFrom0, float radius);

  void EnableSBShadowsCommit();

  void InitSceneInvariants(const int cb);

  void SetRenderTargetAndDepthStencil(
    const RenderTarget& rt, const DepthStencil& dt, float minZ = 0.0f, float maxZ = 1.0f, 
    bool optimize = true);

  /// implementation of Draw2D, used for high-level recording
  void DoDraw2D(const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip);

  bool ClipDraw2D(
    float &xBeg, float &yBeg, float &xEnd, float &yEnd,
    float &uBeg, float &vBeg, float &uEnd, float &vEnd,
    const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip
    );

  bool AddVertices( const TLVertex *v, int n );

  void QueuePrepareTriangle( const MipInfo &absMip, const TexMaterial *mat, int specFlags );

  void DoDrawPolyPrepare(const MipInfo &mip, int specFlags);
  void DoDrawPolyDo(const Vertex2DPixel *vertices, int nVertices, const Rect2DPixel &clip);
  void DoDrawPolyDo(const Vertex2DAbs *vertices, int nVertices, const Rect2DAbs &clip);

  void DoDrawLineDo(const Line2DAbs &line, PackedColor c0, PackedColor c1, const Rect2DAbs &clip);
  void DoDrawText3DCenteredAsync(const DrawText3DCenteredAsyncPars &pars);

  void SetPixelShader(int cb, ID3D11PixelShader *ps, bool optimize = true);
  void SetVertexShader(int cb, const VertexShaderInfo* sInfo, bool optimize = true);
  void SetVertexShader(int cb, const VertexShaderInfo& sInfo, bool optimize = true);

  bool CheckVSNeedsToBeSet(ID3D11VertexShader *shader, RendStateExt &cbState, int mask);
  bool CheckPSNeedsToBeSet(ID3D11PixelShader *shader, RendStateExt &cbState, int mask);
  bool CheckRSNeedsToBeSet(RendStateExt &cbState, int index, DWORD value, int mask);
#if _ARMA3_PREDICATED_SAMPLER_STATES
  bool CheckSSNeedsToBeSet(RendStateExt &cbState, int stage, int index, DWORD value, int mask);
#endif

  bool CheckDepthStencilStateNeedsToBeSet(RendStateExt &cbState, ID3D11DepthStencilState* state, int mask);
  bool CheckBlendStateNeedsToBeSet(RendStateExt &cbState, ID3D11BlendState* state, int mask);
  bool CheckRasterizerStateNeedsToBeSet(RendStateExt &cbState, ID3D11RasterizerState* state, int mask);

  DWORD GetRSValue(int index);

  // compile time conditional dispatch
  template <class WorkerThreadClass> void ThreadProcess();
  template <class WorkerThreadClass> bool CheckAsync();
  template <class WorkerThreadClass> bool CanDestroyImmediately();
  template <class WorkerThreadClass> void ReportDone();

  template <> void ThreadProcess<BGWorkerThread>() {BGThreadProcess();}

  template <> bool CheckAsync<BGWorkerThread>() {return _backgroundD3D!=RecNone;}

  template <> void ReportDone<BGWorkerThread>() {_backgroundD3D = RecNone;}

  template <> bool CanDestroyImmediately<BGWorkerThread>() {return !UsePredication() || _backgroundD3D==RecHighLevel;}

  /// play a command buffer (including its predication)
  void PlayCB(const RenderCBToPlay &cb);

  template <class WorkerThreadClass>
  typename WorkerThreadClass::BGDataType &GetBackgroundData();

  /// submit any instructions pending in corresponding GetBackgroundData
  template <class WorkerThreadClass>
  bool SubmitBackground(WorkerThreadClass &bg, bool flushMem, bool setEvent, CleanUpRequest needCleanUp, const char *pixScope);

  template <class WorkerThreadClass>
  void FlushBackground(WorkerThreadClass &bg, bool flushMem, const char *pixScope, bool waitUntilDone=true);

  template <class WorkerThreadClass>
  void FlushWaitUntilDone(WorkerThreadClass &bg, bool flushMem=true);

  /// debugging/testing: check if background thread state is correct and consistent
  template <class WorkerThreadClass>
  bool CheckThreadMemory(WorkerThreadClass &bg) const;

  void D3DCloseDraw();
  void D3DBeginScene();
  void D3DEndScene();

  void D3DSetSamplerState(int cb, DWORD stage, D3DSAMPLERSTATETYPE_OLD state, DWORD value, bool optimize=true);
  void D3DSetRenderState(int cb, D3DRENDERSTATETYPE_OLD state, DWORD value, bool optimize=true);

  void SetupMaterialTextures(const TexMaterialLODInfo &mat, int cb, const EngineShapeProperties & prop, int specFlags);

  bool ZPrimed() const
  {
#ifndef _XBOX
    // on PC we currently do not prime z buffer when using MSAA
    return true;
#else
    // on Xbox z-buffer content is always valid
    return true;
#endif
  }

  //!{ Checking and creation of SB particular resources
  void SBTR_DefaultCreate();
  //!}

  int AnisotropyLevel(PixelShaderID ps, bool road, TexFilter filter) const;

  //!{ Selecting shadow map type
  void UseShadowMapSSSM(int cb);
  void UseShadowMapSB(int cb);
  //!}

  void KickOffCBDebug();

  void ForEachPostEffectHndl(IPPHndlFunc &func);
  //! Check if effect priority can be used
  bool ValidatePPPriority(int priority) const;
  //! Define unique handle - used for post effect instance identification
  int GetPostEffectHandle() const;
  //! Find post process handle, if not found then return InvalidPPHndl handle
  PostProcessHndl& AccessPostprocess(int hndl);

#if DX11_USE_HASHED_STATE_OBJECTS
  TDepthStencilStates _depthStencilStates;
  TBlendStates _blendStates;
  TRasterizerStates _rasterizerStates;
  TSamplerStates _samplerStates;
#endif //DX11_USE_HASHED_STATE_OBJECTS

#ifdef _XBOX
  void ResolveRenderTarget();
#else
  // Resolve is nop on PC - textures are render targets when needed
  void ResolveRenderTarget(){}
#endif

  void SetViewport(int width, int height, float minZ, float maxZ);
  void SetViewportDirect(int x, int y, int width, int height, float minZ, float maxZ);

  void CreateShaderConstantBuffers();
  void DestroyShaderConstantBuffers();

  /** gets input layout for given shader */
  ComRef<ID3D11InputLayout> GetInputLayout(const VertexDeclaration &vertDecl, ComRef<ID3D10Blob>& shader, LPCSTR pFunctionName);

  void SetPrimitiveTopology(int cb, D3D11_PRIMITIVE_TOPOLOGY topology, bool optimize = true);

  void DoDrawSSAO(float fogEnd, float fogStart, float fogCoef);

  // initialize current state
  void InitCurrentRenderState();

  void DoSetupAllBeforeDraw();
  void SetupAlphaTest();
  void SetupDepthStencilStates();
  void SetupBlendStates();
  void SetupRasterizerStates();
  void SetupSamplerStates();
  void SetupConstantBuffers();
  void SetupTextures();

  void DoSetShaderConstantF(const ShaderConstant* constant, int floatCount, int additionalFloat4Offset, const Array<float>& data);
  void DoSetTexture(int stage, TextureHandle11* tex);
  void DoSetTextureDirect(int stage, TextureHandle11* tex);
  void DoSetRenderState(DWORD state, DWORD value);
  void DoSetSamplerState(DWORD stage, DWORD state, DWORD value);
  void DoSetStreamSource(const VertexStaticData11::BufferFuture &buf, int stride, int bufSize);
  void DoSetPixelShader(ID3D11PixelShader *shader);
  void DoSetVertexShader(ID3D11VertexShader* shader, ID3D11InputLayout* layout);
  void DoDrawIndexedPrimitive(D3DPRIMITIVETYPE_OLD primitiveType, INT baseVertexIndex, UINT minIndex, UINT numVertices, UINT startIndex, UINT primitiveCount);
  void DoDrawPrimitive(D3DPRIMITIVETYPE_OLD primitiveType, UINT startVertex, UINT primitiveCount);
  void DoSetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology);
 
  // this function is used specially for SSAO effect - we need to call all D3D commands directly, not record them,
  // because SSAO drawing is all in background thread already
  // DO NOT use this function normally, because it does not remember targets as current!
  void SetRenderTargetAndDepthStencilDirect(
    const RenderTarget& rt, const DepthStencil& dt, float minZ, float maxZ);  

  HMONITOR GetAdapterMonitor(IDXGIAdapter* adapter);
  ComRef<IDXGIOutput> GetAdapterOutput(IDXGIAdapter* adapter);

  /// find render target and depth formats, fill various caps
  void FindRTFormats();
  /// returns if given format is suitable as render target
  bool IsSuitableRT( DXGI_FORMAT format, bool mustBeBlendable = true ) ;
  /// gets max multisample levels for given format
  int GetMaxMultisampleLevels( DXGI_FORMAT format) ;
  /// initializes fsaa modes based on given formats
  void InitFSAAModes(DXGI_FORMAT rtFormat, DXGI_FORMAT depthFormat);
  /// creates name for given fsaa mode, returns if succeeded 
  bool CreateFSAAModeName(int numSamples, int quality, int maxQuality, RString& name);
  /// selects best RT format based on given minimal bpp
  DXGI_FORMAT SelectRTFormat( int minBpp ) ;

  /// initialize command buffers
  bool InitCB();
  /// initialize DXGi stuff - factory, adapter, output
  bool InitDXGI();
  void DeInitDXGI();
  /// initialize app window
  bool InitAppWindow(HINSTANCE hInst, HINSTANCE hPrev, int sw, bool windowed);
  /** initialize shaders
  \param generateShaders if we are in shader generation mode */
  bool InitShaders(bool generateShaders);
  void DeInitShaders();
  /// initialize d3d device
  bool InitD3D();
  void DeInitD3D();
  /// initialize all resources
  bool InitResources(ParamEntryPar cfg, ParamEntryPar fcfg);
  void DeInitResources(bool destroyTexBank);
  bool InitVB();
  void DeInitVB();
  /// initializes render targets 
  bool InitRenderTargets();
  void DeInitRenderTargets();
  /// setups initial state
  bool InitD3DState();
  /// initializes state lists
  void InitStateLists();
  void DeInitStateLists();

#if _ARMA3_SIMUL_WEATHER
  // initializes SW renderer
  void InitSimulWeather();
  // deinitializes SW renderer
  void DeInitSimulWeather();
#endif


  /// unsets all resources from CB's and device
  void UnsetAllResources();

  /** Gets cursor based on given values. If cursor does not exist, it is created.
  \param cursorTex cursor texture
  \param hsX hotspot x
  \param hsY hotspot y
  \param color color
  */
  HCURSOR GetCursor(Texture *cursorTex, float hsX, float hsY, PackedColor color);

  /** returns if fsaa is enabled */
  bool IsFSAAEnabled() const {return _rtMultiSampleCount > 1;}

#if _ARMA3_NEW_LIGHTING_CFG
  /** computes back reflection color
  \param diffuse combined light and material diffuse component
  */
  Color ComputeBackReflection(const Color& diffuse);
  /// computes ground reflection color
  Color ComputeGroundReflection(const Color& lightAmbient, const Color& matAmbient);
#endif

};

/// pushes render target and depth stencil when constructed and pops it when destructed
class PushPopRenderTargetDepthStencilGuard
{
public:
  PushPopRenderTargetDepthStencilGuard(EngineDD11* engine)
    :_engine(engine) 
  {
    if (_engine)
      _engine->PushRenderTargetDepthStencil();
  }

  ~PushPopRenderTargetDepthStencilGuard()
  {
    if (_engine)
      _engine->PopRenderTargetDepthStencil();
  }

private:
  EngineDD11* _engine;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// CALM WATER STUFF
////////////////////////////////////////////////////////////////////////////////////////////////////

//! struct for input engine params for computing calm water params
struct SCalmWaterIn
{
  // [2:10]
  float skyLightColor[3];
  float specularPower;
  // [2:11]
  float mainLightDir[3];
  float waterColorR;
  // [2:12]
  float mainLightColor[3];
  float waterColorG;
  // [2:13]
  float mainLightSpecular[3];
  float waterColorB;
  // [2:14]
  // world space cam pos
  float cameraPositionX;
  float cameraPositionZ;
  float NA1[2];
  //
  // [2:15]
  float weather[4];
};

//! structure for calm water shader (both vertex and pixel for simplicity) const regs params
struct StructCalmWaterParams
{
  // 1. part
  // [0]
  float vertAnimPhaseXMulA; // vertex anim scale in x-axis
  float vertAnimPhaseYMulA; // vertex anim scale in y-axis
  float vertAnimZMulA; // vertex anim scale in z-axis (amplitude)
  float vertAnimPhaseTA; // vertex anim speed
  // [1]
  float vertAnimPhaseXMulB;
  float vertAnimPhaseYMulB;
  float vertAnimZMulB;
  float vertAnimPhaseTB;
  // [2]
  float blendAnimPhaseA[4]; // animation coefs for blending of 4 normal maps - wave A
  // [3]
  float blendAnimPhaseB[4]; // animation coefs for blending of 4 normal maps - wave B
  // [4]
  float baseWrapScaleX;
  float baseWrapScaleY;
  float invBBResX; // 1.0 / back buffer x-resolution
  float invBBResY; // 1.0 / back buffer y-resolution
  // [5]
  float dWrapA[2];
  float dWrapB[2];
  // [6]
  float dDetailWrapA[2]; // delta wrap - animation
  float dDetailWrapB[2];
  // [1:7]
  float detailScale[2];
  float sinBlendAmpDetail;
  float finalBlendOffset;
  // [1:8] // [8] // old [13]
  float waterFogPars[3];
  float lightSumCoef;
  // [1:9] // [9] // old [14]
  float waterScatterPars[4];
  // [1:10] // [10]
  float parallaxParsXZ[2]; // old [2:15 zw]
  //float cameraPositionXZ[2];
  float NA1[6];
  //

  // 2. part
  // [2:0]
  float blendAnimDatails[4]; // animation coefs for blending of detail maps
  // [2:1]
  float fresnelParams[4];
  // [2:2]
  float edgeFoamTcPars[4];
  // [2:3]
  float edgeFoamColor[3]; // foam material color
  float edgeFoamDistVanish;
  // [2:4]
  float edgeFoamDepthPars[4];

  // [2:5] // [15]
  float causticsPars0[4];
  // [2:6] // [16]
  float causticsPars1[4];
  // [2:7] // [17]
  float causticsPars2[4];
  // [2:8] // [18]
  float causticsPars3[4];

  // [2:9]
  float NA2[4];

  // [2:10 - 2:15] 
  SCalmWaterIn inPars;
};

/// reference to input params structure
SCalmWaterIn& GetCalmWaterInPars();

/// pointer to const reg param array for shader
const float* GetCalmWaterPars();
/// pointer to const reg param array for shader (second part)
const float* GetCalmWaterPars2();
/// prepares array of params for shaders' const regs
void ComputeCalmWaterPars( EngineDD11 *engine, SCalmWaterIn *cwmp, ColorVal matAmbient, ColorVal matDiffuse,
                          ColorVal matForcedDiffuse, ColorVal matEmmisive, ColorVal matSpecular, const float matPower );

} //namespace DX11

TypeIsMovable(DX11::PostProcessHndl);
TypeIsMovable(DX11::PSCachedItem)
TypeIsMovableZeroed(DX11::VSCachedItem)
// needed for D3DXMacroList
TypeIsSimple(D3D10_SHADER_MACRO)
TypeIsSimple(DXGI_MODE_DESC)

TypeIsSimple(DX11::SVertex9)
TypeIsGeneric(DX11::RenderTargetDepthStencilInfo)
TypeIsSimple(DX11::CursorInfo)
TypeIsGeneric(DX11::EngineDD11::ConstantBufferInfo)
TypeIsGeneric(DX11::EngineDD11::MultiSizeConstantBuffer)

#endif //!_DISABLE_GUI
#endif //_ENABLE_DX11

#endif //__ENGDD11_HPP
