#ifdef _MSC_VER
#pragma once
#endif

#ifndef __ENGDD9_HPP
#define __ENGDD9_HPP

#if !_DISABLE_GUI

// implementation of DirectDraw engine
// without actual triangle drawing
// so that different implementations (including Direct3D) are possible

#include "d3d9defs.hpp"
#include "../textbank.hpp"
#include "txtd3d9.hpp"
#include "DXGI.h"

#include <Es/Containers/array.hpp>
#include <Es/Strings/bString.hpp>
#include <Es/Containers/staticArray.hpp>
#include <Es/Containers/smallArray.hpp>
#include <Es/ReportStack/reportStack.hpp>
#include <Es/Common/delegate.hpp>
#include <El/FreeOnDemand/memFreeReq.hpp>
#include <El/ActiveObject/activeObject.hpp>
#include <El/Multicore/circularQueue.hpp>

#include "../global.hpp"
#include "../types.hpp"
#include "../engine.hpp"
#include "../tlVertex.hpp"
#include "../Shape/plane.hpp"
#include "ShaderSources_3_0/common.h"
#include "../d3d9/d3d9Buffers.hpp"

#if _ARMA3_SIMUL_WEATHER
#include "../SimulWeather/SimulCommonDX9.hpp"
#endif

#include "../object.hpp"  // ProtectedVisualState

//! Vertex shader constant definition
#define VCS_CDEFS(type,name,regtype,regnum) const int name = regnum;
#define VCS_CDEFA(type,name,regtype,regnum,dim) VCS_CDEFS(type,name,regtype,regnum)
VSC_LIST(VCS_CDEFS,VCS_CDEFA)

//! Pixel shader constant definition
#define PSC_CDEFS(type,name,regtype,regnum) const int name = regnum;
#define PSC_CDEFA(type,name,regtype,regnum,dim) const int name = regnum;
PSC_LIST(PSC_CDEFS,PSC_CDEFA)

/// queues used for 2D rendering
enum
{
  MeshBufferLength=16*1024,
  IndexBufferLength=32*1024
};

#define CUSTOM_VB_DISCARD 1

//! Stream with ready data - used for shader loading
class QOStrStreamData : public QOStrStream
{
public:
  void CopyFile(const char *fileName)
  {
    QIFStreamB in;
    in.AutoOpen(fileName);
    in.copy(*this);
    in.close();
    GFileServerFunctions->FlushReadHandle(fileName);
  }
  QOStrStreamData(){}
  QOStrStreamData(const char *fileName)
  {
    CopyFile(fileName);
  }
};

#if CUSTOM_VB_DISCARD
const int NumDynamicVB3D = 4;
const int NumDynamicVB2D = 4;
#else
const int NumDynamicVB3D = 1;
const int NumDynamicVB2D = 1;
#endif

// dynamic buffer do not work well with predication
# define DONT_USE_DYNBUFFERS 1

#if defined(_X360SHADERGEN)
# define XBOX_D3D 1
#else
# define XBOX_D3D 0
#endif

struct FloatConstant
{
  float ar[4];

  float operator[] (int ix) const { return ar[ix]; }
  float &operator[] (int ix) { return ar[ix]; }
};

TypeIsSimple(FloatConstant);

struct IntConstant
{
  int ar[4];

  int operator[] (int ix) const { return ar[ix]; }
  int &operator[] (int ix) { return ar[ix]; }
};

TypeIsSimple(IntConstant);

#if XBOX_D3D
//typedef void *HWND;

typedef const D3DSURFACE_PARAMETERS *CreateRTLastParType;

#else

typedef HANDLE *CreateRTLastParType;
#endif


/// automatically take a lock whenever the device is used
class D3DRefAutoLock
{
  ComRef<IDirect3DDevice9> _dev;

public:
  D3DRefAutoLock(){}
  D3DRefAutoLock(IDirect3DDevice9 *dev){_dev += dev;}
  operator ComRef<IDirect3DDevice9> () const {return _dev;}
  operator IDirect3DDevice9 *() const {return _dev;}
  IDirect3DDevice9 *operator ->() const {return _dev;}

  void Free(){_dev.Free();}
  bool IsNull() const {return _dev.IsNull();}
  /// use with caution - direct access with no locking
  IDirect3DDevice9 *NoLock() const {return _dev;}
};

void Device_UnsetAllDiscardableResources(IDirect3DDevice9 *dev);

/// provide a means for D3DDeviceAutoLocked to check if we are recording a CB
bool NotInRecording();

void IntentionalCrash();

#include <El/FileServer/fileServer.hpp>

static inline void FlushGPU(IDirect3DDevice9 *dev)
{
}

bool CheckValidIntegerConstant(const IntConstant *data, int registerCount);
#if _ARMA3_VERTEX_BUFFER_EX
/** 
Vertex buffer with additional information about number of primitives we want to actually draw
This is useful when actual number of primitives is decided after the buffer creation in visualize thread
(so we don't know exact number when DrawPrimitive* is called)
*/
class VertexBuffer9Ex 
{
public:
  static const int UndefinedValue = -1; 

  VertexBuffer9Ex(): _numPrimitivesToDraw(UndefinedValue) {}
  VertexBuffer9Ex(const ComRef<IDirect3DVertexBuffer9>& val, int numPrimitivesToDraw = UndefinedValue)
    :_buffer(val), _numPrimitivesToDraw(numPrimitivesToDraw)
  {}

  int GetNumPrimitivesToDraw() const {return _numPrimitivesToDraw;}
  void SetNumPrimitivesToDraw(int num) {_numPrimitivesToDraw = num;}
  bool IsValid() {return _numPrimitivesToDraw != UndefinedValue && _numPrimitivesToDraw > 0;}

  const ComRef<IDirect3DVertexBuffer9>& GetBuffer() const {return _buffer;}
  ComRef<IDirect3DVertexBuffer9>& GetBuffer() {return _buffer;}

private:
  /// vertex buffer
  ComRef<IDirect3DVertexBuffer9> _buffer;
  /** actual number of primitives to draw
  if this is UndefinedValue, then number of primitives from DrawPrimitive... call is used
  */
  int _numPrimitivesToDraw;  
};

#else //#if _ARMA3_VERTEX_BUFFER_EX
typedef ComRef<IDirect3DVertexBuffer9> VertexBuffer9Ex;
#endif

/// isolated the part of the interface we use
template <bool allowCB>
class D3DDeviceAutoLocked
{
protected:
  /// native implementation
  ComRef<IDirect3DDevice9> _dev;
  
#if _ARMA3_VERTEX_BUFFER_EX
  /// last number of primitives set in SetStreamSource
  int _lastStreamSourceNumPrimitives;
#endif
  
  #ifdef _DEBUG
  /// tracking why sometimes declaration resets to NULL
  InitVal<IDirect3DVertexDeclaration9 *,(IDirect3DVertexDeclaration9 *)-1> _vdSet;
  #endif

public:
  void operator += (IDirect3DDevice9 *device){_dev += device;}
  void operator << (IDirect3DDevice9 *device){_dev << device;}
  void operator = (const ComRef<IDirect3DDevice9> &dev){_dev = dev;}

  ComRef<IDirect3DDevice9> &GetRef()
  {
    // this should never be called while recording
    DoAssert(NotInRecording());
    return _dev;
  }
  // same as GetRef, but allowed withing CB scope - use with caution
  ComRef<IDirect3DDevice9> &GetRefForCB() {return _dev;}

  bool IsNull() const {return _dev.IsNull();}
  void Free() {_dev.Free();}

  __forceinline void DrawSSAO(float fogEnd, float fogStart, float fogCoef)
  {
    // should be never called
    DoAssert(false);
  }

	__forceinline void DrawEnfDebugShapes()
	{
		// should be never called
		DoAssert(false);
	}

	__forceinline void QueryIssue(IDirect3DQuery9* query, int issueCommand)
	{
		DoAssert(false);
	}

  __forceinline void R2TCopyTarget(IDirect3DSurface9 *arg1, CONST RECT *arg2, IDirect3DSurface9 *arg3, bool arg4)
  {
    DoAssert(false);
  }

  __forceinline void R2TPostProcess(int, float, int)
  {
    DoAssert(false);
  }

  __forceinline void R2TClearTarget(IDirect3DTexture9 *arg1)
  {
    DoAssert(allowCB || NotInRecording());

    if (arg1)
    {
      ComRef<IDirect3DSurface9> origRT;
      _dev->GetRenderTarget(0, origRT.Init());

      ComRef<IDirect3DSurface9> surfToClear;
      arg1->GetSurfaceLevel(0, surfToClear.Init());

      HRESULT hr = _dev->SetRenderTarget(0, surfToClear);
      DoAssert(hr == S_OK);
      _dev->Clear(0, 0, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.0f, 0);

      _dev->SetRenderTarget(0, origRT);
    }
  }
  
  // basic functions, needed for common rendering
  __forceinline void SetIndices(const Future< ComRef<IDirect3DIndexBuffer9> > &pIndexData)
  {
    DoAssert(allowCB || NotInRecording());
    //LogF("Direct: SetIndices %p",pIndexData);
    _dev->SetIndices(pIndexData.GetResult());
  }
  __forceinline HRESULT DrawIndexedPrimitive(
    D3DPRIMITIVETYPE PrimitiveType,
    INT BaseVertexIndex, UINT MinIndex, UINT NumIndices, UINT StartIndex, UINT PrimitiveCount
    )
  {
    DoAssert(allowCB || NotInRecording());
    FlushGPU(_dev);
    HRESULT ret = _dev->DrawIndexedPrimitive(PrimitiveType,BaseVertexIndex,MinIndex,NumIndices,StartIndex,PrimitiveCount);
    FlushGPU(_dev);
    return ret;
  }
  __forceinline HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
  {
    DoAssert(allowCB || NotInRecording());
    FlushGPU(_dev);
#if _ARMA3_VERTEX_BUFFER_EX
    if (_lastStreamSourceNumPrimitives != VertexBuffer9Ex::UndefinedValue)
    {
      // override primitive count
      DoAssert(_lastStreamSourceNumPrimitives >= 1);
      if (_lastStreamSourceNumPrimitives >= 1)
        PrimitiveCount = _lastStreamSourceNumPrimitives;
    }
#endif
    HRESULT ret = _dev->DrawPrimitive(PrimitiveType,StartVertex,PrimitiveCount);
    FlushGPU(_dev);
    return ret;
  }

  __forceinline void DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const Array<char> &vertexData, UINT VertexStreamZeroStride)
  {
    Assert(NotInRecording());
    FlushGPU(_dev);
    _dev->DrawPrimitiveUP(PrimitiveType, PrimitiveCount, vertexData.Data(), VertexStreamZeroStride);
    FlushGPU(_dev);
  }
  __forceinline void SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetSamplerState(Sampler,Type,Value);
  }
  __forceinline void SetRenderState(D3DRENDERSTATETYPE State, DWORD Value)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetRenderState(State,Value);
  }
  __forceinline void SetTexture(DWORD Sampler, IDirect3DBaseTexture9 *pTexture)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetTexture(Sampler,pTexture);
  }
  __forceinline void SetTextureHandle(DWORD Sampler, TextureHandle9 *tex)
  {
    DoAssert(allowCB || NotInRecording());
    #if ENCAPSULATE_TEXTURE_REF
    _dev->SetTexture(Sampler,tex ? tex->_surface : NULL);
    #else
    _dev->SetTexture(Sampler,tex);
    #endif
  }

  __forceinline void SetStreamSource(UINT StreamNumber, const Future< VertexBuffer9Ex> &pStreamData, UINT OffsetInBytes, UINT Stride)
  {
    DoAssert(allowCB || NotInRecording());

#if _ARMA3_VERTEX_BUFFER_EX
    // set number of primitives
    _lastStreamSourceNumPrimitives = pStreamData.GetResult().GetNumPrimitivesToDraw();
    DoAssert(_lastStreamSourceNumPrimitives == VertexBuffer9Ex::UndefinedValue || _lastStreamSourceNumPrimitives >= 1);
#endif

    _dev->SetStreamSource(StreamNumber,
#if _ARMA3_VERTEX_BUFFER_EX
      pStreamData.GetResult().GetBuffer(),
#else
      pStreamData.GetResult(),
#endif
      OffsetInBytes,Stride);
  }

  __forceinline void SetVertexDeclaration(IDirect3DVertexDeclaration9 *pDecl)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetVertexDeclaration(pDecl);
    #if _DEBUG
    _vdSet = pDecl;
    #endif
  }

  __forceinline void SetVertexShader(IDirect3DVertexShader9 *pShader)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetVertexShader(pShader);
  }
  __forceinline void SetPixelShader(IDirect3DPixelShader9* pShader)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetPixelShader(pShader);
  }

  __forceinline void SetVertexShaderConstantF(UINT StartRegister, const Array<FloatConstant> &cs)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetVertexShaderConstantF(StartRegister,(float *)cs.Data(),cs.Size());
  }
  __forceinline void SetVertexShaderConstantI(UINT StartRegister, CONST int * pConstantData, UINT RegisterCount)
  {
    DoAssert(allowCB || NotInRecording());
    Assert(CheckValidIntegerConstant(reinterpret_cast<const IntConstant *>(pConstantData),RegisterCount));
  // we need to verify values are sensible as well
    _dev->SetVertexShaderConstantI(StartRegister,pConstantData,RegisterCount);
  }
  __forceinline void SetVertexShaderConstantB(UINT StartRegister, CONST BOOL * pConstantData, UINT RegisterCount)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetVertexShaderConstantB(StartRegister,pConstantData,RegisterCount);
  }
  __forceinline void SetPixelShaderConstantF(UINT StartRegister, CONST FLOAT* pConstantData, UINT RegisterCount)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetPixelShaderConstantF(StartRegister,pConstantData,RegisterCount);
  }
  __forceinline void SetPixelShaderConstantI(UINT StartRegister, CONST int * pConstantData, UINT RegisterCount)
  {
    DoAssert(allowCB || NotInRecording());
    Assert(CheckValidIntegerConstant(reinterpret_cast<const IntConstant *>(pConstantData),RegisterCount));
    _dev->SetPixelShaderConstantI(StartRegister,pConstantData,RegisterCount);
  }
  __forceinline void SetPixelShaderConstantB(UINT StartRegister, CONST BOOL * pConstantData, UINT RegisterCount)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetPixelShaderConstantB(StartRegister,pConstantData,RegisterCount);
  }


  __forceinline void SetPixelShaderConstantsF(UINT StartRegister, const Array<FloatConstant> &cs)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetPixelShaderConstantF(StartRegister,(const float *)cs.Data(),cs.Size());
  }
  __forceinline void SetVertexShaderConstantsF(UINT StartRegister, const Array<FloatConstant> &cs)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetVertexShaderConstantF(StartRegister,(const float *)cs.Data(),cs.Size());
  }
  __forceinline void SetPixelShaderConstantsB(UINT StartRegister, const Array<BOOL> &cs)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetPixelShaderConstantB(StartRegister,cs.Data(),cs.Size());
  }
  __forceinline void SetVertexShaderConstantsB(UINT StartRegister, const Array<BOOL> &cs)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetVertexShaderConstantB(StartRegister,cs.Data(),cs.Size());
  }
  __forceinline void SetPixelShaderConstantsI(UINT StartRegister, const Array<IntConstant> &cs)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetPixelShaderConstantI(StartRegister,reinterpret_cast<const int *>(cs.Data()),cs.Size());
  }
  __forceinline void SetVertexShaderConstantsI(UINT StartRegister, const Array<IntConstant> &cs)
  {
    DoAssert(allowCB || NotInRecording());
    _dev->SetVertexShaderConstantI(StartRegister,reinterpret_cast<const int *>(cs.Data()),cs.Size());
  }

  // advanced functions, needed only infrequently (1-10 per frame)
  __forceinline void Clear(DWORD Count, CONST D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
  {
    DoAssert(NotInRecording());
    _dev->Clear(Count,pRects,Flags,Color,Z,Stencil);
  }
  __forceinline void SetViewport(CONST D3DVIEWPORT9 &pViewport)
  {
    DoAssert(NotInRecording());
    _dev->SetViewport(&pViewport);
  }
  __forceinline void BeginScene()
  {
    DoAssert(NotInRecording());
    _dev->BeginScene();
  }
  __forceinline void EndScene()
  {
    DoAssert(NotInRecording());
    _dev->EndScene();
  }

  __forceinline void GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 ** ppRenderTarget)
  {
    Assert(NotInRecording());
    _dev->GetRenderTarget(RenderTargetIndex,ppRenderTarget);
  }
  __forceinline void GetDepthStencilSurface(IDirect3DSurface9 ** ppZStencilSurface)
  {
    Assert(NotInRecording());
    _dev->GetDepthStencilSurface(ppZStencilSurface);
  }
  __forceinline void SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 * ppRenderTarget)
  {
    // with X360 HW CB we need to allow recording SetRenderTarget/SetDepthStencilSurface
    DoAssert(NotInRecording());
    _dev->SetRenderTarget(RenderTargetIndex,ppRenderTarget);
  }
  __forceinline void SetDepthStencilSurface(IDirect3DSurface9 * pNewZStencil)
  {
    DoAssert(NotInRecording());
    _dev->SetDepthStencilSurface(pNewZStencil);
  }
  __forceinline HRESULT CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9** ppQuery)
  {
    Assert(NotInRecording());
    return _dev->CreateQuery(Type,ppQuery);
  }

#if !XBOX_D3D
  __forceinline void StretchRect(
    IDirect3DSurface9 * pSourceSurface, CONST RECT * pSourceRect,
    IDirect3DSurface9 * pDestSurface, CONST RECT * pDestRect,
    D3DTEXTUREFILTERTYPE Filter
    )
  {
    DoAssert(NotInRecording());
    _dev->StretchRect(pSourceSurface,pSourceRect,pDestSurface,pDestRect,Filter);
  }
#endif


#if !XBOX_D3D
  __forceinline HRESULT GetRenderTargetData(IDirect3DSurface9* pRenderTarget,IDirect3DSurface9* pDestSurface)
  {
    Assert(NotInRecording());
    return _dev->GetRenderTargetData(pRenderTarget,pDestSurface);
  }


  __forceinline void ShowCursor(BOOL bShow)
  {
    Assert(NotInRecording());
    _dev->ShowCursor(bShow);
  }
  __forceinline void SetCursorProperties(
    UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9 * pCursorBitmap
    )
  {
    Assert(NotInRecording());
    _dev->SetCursorProperties(XHotSpot,YHotSpot,pCursorBitmap);
  }
  __forceinline void SetCursorPosition(INT X, INT Y, DWORD Flags)
  {
    DoAssert(NotInRecording());
    _dev->SetCursorPosition(X,Y,Flags);
  }
  __forceinline HRESULT CreateOffscreenPlainSurface(
    UINT Width, UINT Height, D3DFORMAT Format,
    D3DPOOL Pool, IDirect3DSurface9** ppSurface, HANDLE* pSharedHandle
  )
  {
    Assert(NotInRecording());
    return _dev->CreateOffscreenPlainSurface(Width,Height,Format,Pool,ppSurface,pSharedHandle);
  }
#endif

  // resource management, needed rarely (not in each frame)

  __forceinline HRESULT CreateVertexBuffer(
    UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9** ppVertexBuffer, HANDLE* pSharedHandle
    )
  {
    // TODO:MC: avoid this call inside of CB recording
    //Assert(NotInRecording());
    return _dev->CreateVertexBuffer(Length,Usage,FVF,Pool,ppVertexBuffer,pSharedHandle);
  }
  __forceinline HRESULT CreateIndexBuffer(
    UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool, IDirect3DIndexBuffer9** ppIndexBuffer, HANDLE* pSharedHandle
    )
  {
    // TODO:MC: avoid this call inside of CB recording
    //Assert(NotInRecording());
    return _dev->CreateIndexBuffer(Length,Usage,Format,Pool,ppIndexBuffer,pSharedHandle);
  }
  __forceinline HRESULT CreateTexture(
    UINT Width, UINT Height, UINT Levels,
    DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DTexture9** ppTexture, HANDLE* pSharedHandle
    )
  {
    //Assert(NotInRecording());
    return _dev->CreateTexture(Width,Height,Levels,Usage,Format,Pool,ppTexture,pSharedHandle);
  }

#if _ARMA3_SIMUL_WEATHER
  __forceinline HRESULT CreateVolumeTexture(
    UINT Width, UINT Height, UINT Depth, UINT Levels,
    DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
    IDirect3DVolumeTexture9** ppVolumeTexture, HANDLE* pSharedHandle
    )
  {
    //Assert(NotInRecording());
    return _dev->CreateVolumeTexture(Width,Height, Depth, Levels,Usage,Format,Pool,ppVolumeTexture,pSharedHandle);
  }
#endif

#if !XBOX_D3D
  __forceinline HRESULT UpdateTexture(IDirect3DBaseTexture9 * pSourceTexture, IDirect3DBaseTexture9 * pDestinationTexture)
  {
    //Assert(NotInRecording());
    return _dev->UpdateTexture(pSourceTexture,pDestinationTexture);
  }
  __forceinline UINT GetAvailableTextureMem()
  {
    // TODO: consider memory in the resources which are waiting for creation
    //Assert(NotInRecording());
    return _dev->GetAvailableTextureMem();
  }
#endif


  // once per frame - Present at al.  
  __forceinline HRESULT Present(
    CONST RECT * pSourceRect, CONST RECT * pDestRect, HWND hDestWindowOverride, CONST RGNDATA * pDirtyRegion
    )
  {
    Assert(NotInRecording());
    // cast done to allow for conversion to X360 API, which expects void *
    return _dev->Present(pSourceRect,pDestRect,hDestWindowOverride,unconst_cast(pDirtyRegion));
  }
#if !XBOX_D3D
  __forceinline HRESULT TestCooperativeLevel()
  {
    Assert(NotInRecording());
    return _dev->TestCooperativeLevel();
  }
#endif

  // initialization (never needed while game is running)
  __forceinline HRESULT CreateVertexShader(CONST DWORD *pFunction, IDirect3DVertexShader9 **ppShader)
  {
    Assert(NotInRecording());
    return _dev->CreateVertexShader(pFunction,ppShader);
  }
  __forceinline HRESULT CreatePixelShader(CONST DWORD *pFunction, IDirect3DPixelShader9 **ppShader)
  {
    Assert(NotInRecording());
    return _dev->CreatePixelShader(pFunction,ppShader);
  }
  __forceinline HRESULT CreateVertexDeclaration(CONST D3DVERTEXELEMENT9* pVertexElements, IDirect3DVertexDeclaration9** ppDecl)
  {
    Assert(NotInRecording());
    return _dev->CreateVertexDeclaration(pVertexElements,ppDecl);
  }
  __forceinline HRESULT CreateRenderTarget(
    UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
    BOOL Lockable, IDirect3DSurface9** ppSurface, CreateRTLastParType pSharedHandle
    )
  {
    Assert(NotInRecording());
    return _dev->CreateRenderTarget(Width,Height,Format,MultiSample,MultisampleQuality,Lockable,ppSurface,pSharedHandle);
  }
  __forceinline HRESULT CreateDepthStencilSurface(
    UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
    BOOL Discard, IDirect3DSurface9** ppSurface,   CreateRTLastParType pSharedHandle
    )
  {
    Assert(NotInRecording());
    return _dev->CreateDepthStencilSurface(Width,Height,Format,MultiSample,MultisampleQuality,Discard,ppSurface,pSharedHandle);
  }


  __forceinline HRESULT Reset(D3DPRESENT_PARAMETERS* pPresentationParameters)
  {
    Assert(NotInRecording());
    return _dev->Reset(pPresentationParameters);
  }
  __forceinline void SetGammaRamp(UINT  iSwapChain, DWORD Flags, CONST D3DGAMMARAMP * pRamp)
  {
    Assert(NotInRecording());
    return _dev->SetGammaRamp(iSwapChain,Flags,pRamp);
  }
  __forceinline void GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters)
  {
    Assert(NotInRecording());
    _dev->GetCreationParameters(pParameters);
  }
  __forceinline void GetDeviceCaps(D3DCAPS9 * pCaps)
  {
    Assert(NotInRecording());
    _dev->GetDeviceCaps(pCaps);
  }
  
  #if SAFE_RESOURCE_UNSET
  void UnsetAllDiscardableResources()
  {
    Device_UnsetAllDiscardableResources(_dev);
  }
  #endif
  
  // X360 specific - advanced, once/few times per frame
#if XBOX_D3D
  __forceinline void SetShaderGPRAllocation(DWORD Flags, DWORD VertexShaderCount, DWORD PixelShaderCount)
  {
    DoAssert(NotInRecording());
    _dev->SetShaderGPRAllocation(Flags,VertexShaderCount,PixelShaderCount);
  }
  __forceinline void ClearF(DWORD Flags, CONST D3DRECT *pRect, CONST D3DVECTOR4 *pColor, float Z, DWORD Stencil)
  {
    DoAssert(NotInRecording());
    FlushGPU(_dev);
    _dev->ClearF(Flags, pRect, pColor, Z, Stencil);
    FlushGPU(_dev);
  }

  __forceinline void BeginTiling(DWORD Flags, DWORD Count, CONST D3DRECT* pTileRects, CONST D3DVECTOR4* pClearColor, float ClearZ, DWORD ClearStencil) 
  {
    DoAssert(NotInRecording());
    _dev->BeginTiling(Flags, Count, pTileRects, pClearColor, ClearZ, ClearStencil);
  }
  __forceinline HRESULT EndTiling(DWORD ResolveFlags, CONST D3DRECT* pResolveRects, D3DBaseTexture* pDestTexture, CONST D3DVECTOR4* pClearColor, float ClearZ, DWORD ClearStencil, CONST D3DRESOLVE_PARAMETERS* pParameters) 
  {
    DoAssert(NotInRecording());
    return _dev->EndTiling(ResolveFlags, pResolveRects, pDestTexture, pClearColor, ClearZ, ClearStencil, pParameters);
  }
  __forceinline void Resolve(DWORD Flags, CONST D3DRECT *pSourceRect, D3DBaseTexture *pDestTexture, CONST D3DPOINT *pDestPoint, UINT DestLevel, UINT DestSliceOrFace, CONST D3DVECTOR4* pClearColor, float ClearZ, DWORD ClearStencil, CONST D3DRESOLVE_PARAMETERS* pParameters) 
  {
    DoAssert(NotInRecording());
    FlushGPU(_dev);
    _dev->Resolve(Flags, pSourceRect, pDestTexture, pDestPoint, DestLevel, DestSliceOrFace, pClearColor, ClearZ, ClearStencil, pParameters);
    FlushGPU(_dev);
  }
  __forceinline void GpuOwnPixelShaderConstantF(DWORD StartRegister, DWORD Vector4fCount) 
  {
    DoAssert(NotInRecording());
    _dev->GpuOwnPixelShaderConstantF(StartRegister, Vector4fCount);
  }
  __forceinline void SetPredication(DWORD PredicationMask) 
  {
    DoAssert(NotInRecording());
    _dev->SetPredication(PredicationMask);
  }
  __forceinline void GpuPixelShaderConstantF4_1(UINT StartRegister, D3DVECTOR4 data) 
  {
    DoAssert(NotInRecording());
    // Request an allocation of pixel shader constants from the command buffer.
    D3DVECTOR4* pConstantData = NULL;
    HRESULT hr = _dev->GpuBeginPixelShaderConstantF4(StartRegister, &pConstantData, 4);
    if( SUCCEEDED(hr) )
    {
      ZeroMemory(pConstantData, 4 * sizeof(D3DVECTOR4));

      // Fill in the first constant with the tiling offset for this tile.
      pConstantData[0] = data;

      _dev->GpuEndPixelShaderConstantF4();
    }
  }
  __forceinline void GpuEndPixelShaderConstantF4() 
  {
    DoAssert(NotInRecording());
    _dev->GpuEndPixelShaderConstantF4();
  }

  __forceinline void GpuOwnPixelShaderConstantF4(DWORD reg, DWORD count)
  {
    DoAssert(NotInRecording());
    _dev->GpuOwnPixelShaderConstantF4(reg,count);
  }
  
  __forceinline void GpuDisownAll()
  {
    DoAssert(NotInRecording());
    _dev->GpuDisownAll();
  }
  __forceinline void QueryBufferSpace(DWORD* pUsed, DWORD* pRemaining)
  {
    DoAssert(NotInRecording());
    _dev->QueryBufferSpace( pUsed, pRemaining);
  }
  __forceinline HRESULT GetFrontBuffer(D3DTexture **ppFrontBuffer)
  {
    DoAssert(NotInRecording());
    return _dev->GetFrontBuffer(ppFrontBuffer);
  }
  __forceinline void SetPWLGamma(DWORD Flags, CONST D3DPWLGAMMA *pRamp)
  {
    Assert(NotInRecording());
    _dev->SetPWLGamma(Flags, pRamp);
  }
  __forceinline void SynchronizeToPresentationInterval()
  {
    Assert(NotInRecording());
    _dev->SynchronizeToPresentationInterval();
  }
  __forceinline void SetBlockCallback(DWORD Flags, D3DBLOCKCALLBACK pCallback)
  {
    Assert(NotInRecording());
    _dev->SetBlockCallback(Flags, pCallback);
  }
  __forceinline void Swap(D3DBaseTexture *pFrontBuffer, CONST D3DVIDEO_SCALER_PARAMETERS *pParameters)
  {
    Assert(NotInRecording());
    _dev->Swap(pFrontBuffer, pParameters);
  }

#endif 


  D3DDeviceAutoLocked()
#if _ARMA3_VERTEX_BUFFER_EX
    :_lastStreamSourceNumPrimitives(VertexBuffer9Ex::UndefinedValue)
#endif  
  {}

};

#define GEngineDD static_cast<EngineDD9 *>(::GEngine)


/// set of float constants set in one call
template <int maxN=192>
struct FloatConstantsSet
{
  static const int maxNum = maxN;
  int num;
  /* values need to go last - sometimes truncated */
  FloatConstant val[maxNum];

  __forceinline FloatConstantsSet(){}
  __forceinline void operator = (const Array<FloatConstant> &src)
  {
    Assert(src.Size()<=maxNum);
    memcpy(val,src.Data(),src.Size()*sizeof(FloatConstant));
    num = src.Size();
  }

  __forceinline size_t GetUnusedBytes() const {return (maxNum-num)*sizeof(FloatConstant);}
};

/// set of int constants set in one call
struct IntConstantsSet
{
  static const int maxNum = 16;
  int num;
  /* values need to go last - sometimes truncated */
  IntConstant val[maxNum];

  IntConstantsSet(){}
  IntConstantsSet(const Array<IntConstant> &src)
  {
    Assert(src.Size()<=maxNum);
    memcpy(val,src.Data(),src.Size()*sizeof(IntConstant));
    num = src.Size();
  }
  __forceinline size_t GetUnusedBytes() const {return (maxNum-num)*sizeof(IntConstant);}
};


/// set of bool constants set in one call
template <class Type, int store>
struct StoredArray
{
  static const int maxNum = store;
  int num;
  /* values need to go last - sometimes truncated */
  Type val[maxNum];

  StoredArray(){}
  StoredArray(const Array<Type> &src)
  {
    Assert(src.Size()<=maxNum);
    // optimized for a common case of binary/simple types
    ConstructTraits<Type>::DestructArray(val,src.Size());
    ConstructTraits<Type>::CopyConstruct(val,src.Data(),src.Size());
    // for generic types plain loop might be more efficient, but we do not expect to be working with them
    //for (int i=0; i<src.Size(); i++) val[i] = src[i];
    num = src.Size();
  }
  __forceinline size_t GetUnusedBytes() const {return (maxNum-num)*sizeof(Type);}
  
  const Type *Data() const {return val;}
  int Size() const {return num;}
};

typedef StoredArray<BOOL,16> BoolConstantsSet;

typedef StoredArray<Vertex2DAbs,6> StoredArrayPolyAbs;
typedef StoredArray<Vertex2DPixel,6> StoredArrayPolyPixel;

/*
/// set of bool constants set in one call
struct BoolConstantsSet
{
  static const int maxNum = 16;
  int num;
  / * values need to go last - sometimes truncated * /
  BOOL val[maxNum];

  BoolConstantsSet(){}
  BoolConstantsSet(const Array<BOOL> &src)
  {
    Assert(src.Size()<=maxNum);
    memcpy(val,src.Data(),src.Size()*sizeof(BOOL));
    num = src.Size();
  }
  __forceinline size_t GetUnusedBytes() const {return (maxNum-num)*sizeof(BOOL);}
};
*/

/// store comref with no reference counting - plain pointer with a ComRef like interface
/** we use this to store references we know that are permanent */
template <class Type>
class ComRefFake: public InitPtr<Type>
{
public:
  /// releasing fake ref does not change ref. count, only sets to NULL

  ComRefFake(){}
  ComRefFake(Type *src):InitPtr<Type>(src){}
  void operator = (Type *src){InitPtr<Type>::operator =(src);}
  void Free(){InitPtr<Type>::operator =(NULL);}
};

# define SAFE_RESOURCE_UNSET 1
/*
on PC safe unset not needed - set resources are AddRef-ed by the device.
Still, using Release on the main thread only prevents possible problems with D3D CS contention.
*/

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
As for additional safety usually provided by ComRef absence of operator =, we do not need it here.
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
typedef StoredTexRef<TextureSysGPUDX9> StoredSimulWeatherTexture;
#else
typedef Texture TextureSysGPUDX9;
typedef StoredTexRef<TextureSysGPUDX9> StoredSimulWeatherTexture;
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
  ~StoredComRef(){GEngineDD->BGAddResourceToRelease(GetRef());}
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
		if (!IsNull())
    {
			ComRef<Type> ref = GetResult(); // should be ready now, it is used already
			if (ref.NotNull())
			{
				ref->AddRef();
				GEngineDD->BGAddResourceToRelease(ref.GetRef());
			}
		}
  }
};

#if _ARMA3_VERTEX_BUFFER_EX
/// this does not need to be template, but we are using template, so this would compile (little hack..)
template<class Type>
class StoredFutureVertexBuffer9Ex : public Future<VertexBuffer9Ex>
{
  typedef Future<VertexBuffer9Ex> base;

public:
  StoredFutureVertexBuffer9Ex() {}
  void operator += (const Future<VertexBuffer9Ex> &src)
  {
    base::operator =(src);
  }
  ~StoredFutureVertexBuffer9Ex()
  {
		if (!IsNull())
		{
			ComRef<IDirect3DVertexBuffer9> ref = GetResult().GetBuffer(); // should be ready now, it is used already
			if (ref.NotNull())
			{
				ref->AddRef();
				GEngineDD->BGAddResourceToRelease(ref.GetRef());
			}
		}
  }
};
#else //#if _ARMA3_VERTEX_BUFFER_EX
#define StoredFutureVertexBuffer9Ex StoredFutureComRef
#endif

template <class Type>
class StoredTexRef: public InitPtr<Type>
{
public:
  StoredTexRef(){}
  StoredTexRef(Type *src):InitPtr<Type>(src){if (src) src->AddRef();}
  void operator = (Type *src){InitPtr<Type>::operator =(src);if (src) src->AddRef();}
  ~StoredTexRef() 
	{
		if (IsNull())
		{
			return;
		}
		GEngineDD->BGAddTextureHandleToRelease(GetRef());
	}
};


/// partial specialization - store plain pointer as a ComRef
template <class Type>
struct SerialStoreTraits< StoredComRef<Type> , Type * >
{
  static void Store(StoredComRef<Type> &dst, Type *src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    // Note: this assumes construction / destruction is performed as needed */
    dst += src;
  }
};

/// partial specialization - store plain pointer as a Future<ComRef>
template <class Type>
struct SerialStoreTraits< StoredFutureComRef<Type> , Future< ComRef<Type> > >
{
  static void Store(StoredFutureComRef<Type> &dst, const Future< ComRef<Type> > &src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    dst += src;
  }
};

#if _ARMA3_VERTEX_BUFFER_EX
/// StoredFutureVertexBuffer9Ex
template<class Type>
struct SerialStoreTraits< StoredFutureVertexBuffer9Ex<Type> , Future< VertexBuffer9Ex > >
{
  static void Store(StoredFutureVertexBuffer9Ex<Type> &dst, const Future< VertexBuffer9Ex > &src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    dst += src;
  }
};
#endif

/// partial specialization - store texture handle
template <class Type>
struct SerialStoreTraits< StoredTexRef<Type> , Type * >
{
  static void Store(StoredTexRef<Type> &dst, Type *src)
  {
    // we want the destination to increase refcount, to make sure the object exists as long as is it "stored"
    // Note: this assumes construction / destruction is performed as needed */
    dst = src;
  }
};

#endif

extern StoredRefContainer EngineDDBGStoredRefs;

class TextureD3D9;

class StoredRefTexture: public StoredRef<Texture,&EngineDDBGStoredRefs>
{
  typedef StoredRef<Texture,&EngineDDBGStoredRefs> base;

  public:
  StoredRefTexture(){}
  StoredRefTexture(TextureD3D9 *src):base(src){}
  TextureD3D9 *operator ->() const {return static_cast<TextureD3D9 *>(base::GetRef());}
};

#if _ARMA3_SIMUL_WEATHER
typedef StoredRef<TextureSysGPUDX9,&EngineDDBGStoredRefs> StoredSimulWeatherTexture;
#else
typedef Texture TextureSysGPUDX9;
typedef StoredRef<Texture,&EngineDDBGStoredRefs> StoredSimulWeatherTexture;
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
/// wrapper around a D3D device
/**
Created to allow background thread processing of the D3D runtime / driver calls
*/

const int MaxVSC = 180;
const int MaxPSC = 24;

#if !XBOX_D3D
/// pretend some unused Xbox type exist even on PC
class D3DVECTOR4 {};
class D3DBaseTexture {};
class D3DPOINT {};
class D3DRESOLVE_PARAMETERS {};
#endif

#define D3D_WORK_MESSAGES(CTX,MSG) \
  /* basic, simple functions with straightforward implementation */ \
  MSG(CTX,Void,DrawIndexedPrimitive,(D3DPRIMITIVETYPE arg1, INT arg2, UINT arg3, UINT arg4, UINT arg5, UINT arg6),6,D3DPRIMITIVETYPE arg1; INT arg2; UINT arg3; UINT arg4; UINT arg5; UINT arg6;) \
  MSG(CTX,Void,DrawPrimitive,(D3DPRIMITIVETYPE arg1, UINT arg2, UINT arg3),3,D3DPRIMITIVETYPE arg1; UINT arg2; UINT arg3;) \
  /* TODO:MC: optimized storage for Array<char> */ \
  MSG(CTX,Void,DrawPrimitiveUP,(D3DPRIMITIVETYPE arg1, UINT arg2, const Array<char> &arg3, UINT arg4),4,D3DPRIMITIVETYPE arg1; UINT arg2; AutoArray<char> arg3;UINT arg4;) \
  MSG(CTX,Void,SetSamplerState,(DWORD arg1, D3DSAMPLERSTATETYPE arg2, DWORD arg3),3,DWORD arg1; D3DSAMPLERSTATETYPE arg2; DWORD arg3;) \
  MSG(CTX,Void,SetRenderState,(D3DRENDERSTATETYPE arg1, DWORD arg2),2,D3DRENDERSTATETYPE arg1; DWORD arg2;) \
  MSG(CTX,Void,SetTextureHandle,(DWORD arg1, TextureHandle9 *arg2),2,DWORD arg1; StoredTexRef<TextureHandle9> arg2;) \
  MSG(CTX,Void,SetTexture,(DWORD arg1, IDirect3DBaseTexture9 *arg2),2,DWORD arg1; IDirect3DBaseTexture9 *arg2;) \
  MSG(CTX,Void,SetStreamSource,(UINT arg1, const Future< VertexBuffer9Ex > &arg2, UINT arg3, UINT arg4),4,UINT arg1; StoredFutureVertexBuffer9Ex<IDirect3DVertexBuffer9> arg2; UINT arg3; UINT arg4;) \
  MSG(CTX,Void,SetIndices,(const Future< ComRef<IDirect3DIndexBuffer9> > &arg1),1,StoredFutureComRef<IDirect3DIndexBuffer9> arg1;) \
  MSG(CTX,Void,SetVertexDeclaration,(IDirect3DVertexDeclaration9 *arg1),1,IDirect3DVertexDeclaration9 *arg1;) \
  MSG(CTX,Void,SetVertexShader,(IDirect3DVertexShader9 *arg1),1,IDirect3DVertexShader9 *arg1;) \
  MSG(CTX,Void,SetPixelShader,(IDirect3DPixelShader9* arg1),1,IDirect3DPixelShader9* arg1;) \
  MSG(CTX,Void,SetPixelShaderConstantsF,(UINT arg1, const Array<FloatConstant> &arg2),2,UINT arg1; FloatConstantsSet<MaxPSC> arg2;) \
  MSG(CTX,Void,SetPixelShaderConstantsB,(UINT arg1, const Array<BOOL> &arg2),2,UINT arg1; BoolConstantsSet arg2;) \
  MSG(CTX,Void,SetPixelShaderConstantsI,(UINT arg1, const Array<IntConstant> &arg2),2,UINT arg1; IntConstantsSet arg2;) \
  MSG(CTX,Void,SetVertexShaderConstantsB,(UINT arg1, const Array<BOOL> &arg2),2,UINT arg1; BoolConstantsSet arg2;) \
  MSG(CTX,Void,SetVertexShaderConstantsI,(UINT arg1, const Array<IntConstant> &arg2),2,UINT arg1; IntConstantsSet arg2;) \
  MSG(CTX,Void,SetVertexShaderConstantsF,(UINT arg1, const Array<FloatConstant> &arg2),2,UINT arg1; FloatConstantsSet<MaxVSC> arg2;) \
  MSG(CTX,Void,SetVertexShaderConstantF,(UINT arg1, const Array<FloatConstant> &arg2),2,UINT arg1; AutoArray<FloatConstant> arg2;) \
  /* a few special messages */ \
  MSG(CTX,Void,SetRenderTarget,(DWORD arg1, IDirect3DSurface9 * arg2),2,DWORD arg1; IDirect3DSurface9 *arg2;) \
  MSG(CTX,Void,SetDepthStencilSurface,(IDirect3DSurface9 *arg1),1, IDirect3DSurface9 *arg1;) \
  MSG(CTX,Void,Clear,(DWORD arg1, CONST D3DRECT *arg2, DWORD arg3, D3DCOLOR arg4, float arg5, DWORD arg6),6,DWORD arg1; StoredPtr<D3DRECT> arg2; DWORD arg3; D3DCOLOR arg4; float arg5; DWORD arg6;) \
  MSG(CTX,Void,SetViewport,(CONST D3DVIEWPORT9 &arg1),1,D3DVIEWPORT9 arg1) \
  MSG(CTX,Void,BeginScene,(),0,) \
  MSG(CTX,Void,EndScene,(),0,) \
  MSG(CTX,Void,ClearF,(DWORD arg1, CONST D3DRECT *arg2, CONST D3DVECTOR4 *arg3, float arg4, DWORD arg5),5,DWORD arg1; StoredPtr<D3DRECT> arg2; StoredPtr<D3DVECTOR4> arg3; float arg4; DWORD arg5;) \
  MSG(CTX,Void,Resolve,(DWORD arg1, CONST D3DRECT *arg2, D3DBaseTexture *arg3, CONST D3DPOINT *arg4, UINT arg5, UINT arg6, CONST D3DVECTOR4* arg7, float arg8, DWORD arg9, CONST D3DRESOLVE_PARAMETERS* arg10),10,DWORD arg1; StoredPtr<D3DRECT> arg2; D3DBaseTexture *arg3; StoredPtr<D3DPOINT> arg4; UINT arg5; UINT arg6; StoredPtr<D3DVECTOR4> arg7; float arg8; DWORD arg9; StoredPtr<D3DRESOLVE_PARAMETERS> arg10;) \
  MSG(CTX,Void,StretchRect,(IDirect3DSurface9 *arg1, CONST RECT *arg2, IDirect3DSurface9 *arg3, CONST RECT *arg4, D3DTEXTUREFILTERTYPE arg5),5,IDirect3DSurface9 *arg1; StoredPtr<RECT> arg2; IDirect3DSurface9 *arg3; StoredPtr<RECT> arg4; D3DTEXTUREFILTERTYPE arg5) \
  MSG(CTX,Void,SetPredication,(DWORD arg1),1,DWORD arg1) \
  MSG(CTX,Void,SetShaderGPRAllocation,(DWORD arg1,DWORD arg2,DWORD arg3),3,DWORD arg1;DWORD arg2;DWORD arg3) \
  MSG(CTX,Void,GpuOwnPixelShaderConstantF,(UINT arg1, UINT arg2),2,UINT arg1; UINT arg2;) \
  MSG(CTX,Void,GpuPixelShaderConstantF4_1,(UINT arg1, D3DVECTOR4 arg2),2,UINT arg1; D3DVECTOR4 arg2;) \
  MSG(CTX,Void,GpuDisownAll,(),0,) \
  MSG(CTX,Void,BeginTiling,(DWORD arg1, DWORD arg2, CONST D3DRECT* arg3, CONST D3DVECTOR4* arg4, float arg5, DWORD arg6),6,DWORD arg1; DWORD arg2; StoredPtr<D3DRECT> arg3; StoredPtr<D3DVECTOR4> arg4; float arg5; DWORD arg6) \
  MSG(CTX,Void,EndTiling,(DWORD arg1, CONST D3DRECT* arg2, D3DBaseTexture* arg3, CONST D3DVECTOR4* arg4, float arg5, DWORD arg6, CONST D3DRESOLVE_PARAMETERS* arg7),7,DWORD arg1; StoredPtr<D3DRECT> arg2; D3DBaseTexture* arg3; StoredPtr<D3DVECTOR4> arg4; float arg5; DWORD arg6; StoredPtr<D3DRESOLVE_PARAMETERS> arg7) \
  MSG(CTX,Void,BGResourceCleanUp,(),0,) \
  MSG(CTX,Void,ShowCursor,(bool arg1),1,bool arg1;) \
  MSG(CTX,Void,SetCursorPosition,(SHORT arg1,SHORT arg2,DWORD arg3),3,SHORT arg1;SHORT arg2;DWORD arg3;) \
  \
  MSG(CTX,Void,ProfileBeginGraphScope,(unsigned int arg1, const char *arg2),2,unsigned int arg1; BString<256> arg2;) \
  MSG(CTX,Void,ProfileEndGraphScope,(),0,) \
  \
  /* higher level */ \
  MSG(CTX,Void,Postprocess,(float arg1, const PostFxSettings &arg2),2,float arg1; PostFxSettings arg2;) \
  MSG(CTX,Void,NoPostprocess,(),0,) \
  MSG(CTX,Void,Draw2D,(const Draw2DParsExt &arg1, const Rect2DAbs &arg2, const Rect2DAbs &arg3),3,Draw2DParsExtStored arg1; Rect2DAbs arg2; Rect2DAbs arg3;) \
	MSG(CTX,Void,QueryIssue,(IDirect3DQuery9* arg1, int arg2),2, IDirect3DQuery9* arg1; int arg2;) \
  MSG(CTX,Void,SetFogColor,(ColorVal arg1),1,Color arg1;) \
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
  MSG(CTX,Void,CopyToVRAM,(IDirect3DTexture9 *arg1, IDirect3DTexture9 *arg2),2,StoredComRef<IDirect3DTexture9> arg1; StoredComRef<IDirect3DTexture9> arg2;) \
  MSG(CTX,Void,FreeSurface,(TextureHandle9 *arg1),1,StoredTexRef<TextureHandle9> arg1;) \
  MSG(CTX,Void,DrawSSAO,(float arg1, float arg2, float arg3),3, float arg1; float arg2; float arg3;) \
	MSG(CTX,Void,DrawEnfDebugShapes,(),0, ) \
  /* render 2 texture */ \
  MSG(CTX,Void,R2TCopyTarget,(IDirect3DSurface9 *arg1, CONST RECT *arg2, IDirect3DSurface9 *arg3, bool arg4),4,IDirect3DSurface9 *arg1; StoredPtr<RECT> arg2; IDirect3DSurface9 *arg3; bool arg4;) \
  MSG(CTX,Void,R2TPostProcess,(int arg1, float arg2, int arg3),3,int arg1; float arg2; int arg3;) \
  MSG(CTX,Void,R2TClearTarget,(IDirect3DTexture9 *arg1),1,StoredComRef<IDirect3DTexture9> arg1;) \
  /* Simul weather */ \
  MSG(CTX,Void,UpdateSimulWeatherTexture,(TextureSysGPUDX9 *arg1, unsigned arg2),2,StoredSimulWeatherTexture arg1; unsigned arg2;) \

#define VISUAL_WORK_MESSAGES(CTX,MSG) \
  MSG(CTX,WaitableVoid,PerformMipmapLoading,(),0,) \
  MSG(CTX,Void,DoLoadHeaders,(TextureD3D9 *arg1),1,StoredRefTexture arg1;) \
  /*MSG(CTX,Void,UseMipmap,(Texture *arg1, int arg2, int arg3, int arg4),1,StoredRefTexture arg1;int arg2; int arg3; int arg4;)*/ \
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


//@{ provide size estimate specializations for selected message types


/// all constant passing APIs use the same structure - arg2 is the array containing the constants

#define CONSTANTS_PASSED(Name) \
  template <> \
  struct StoredSize<const Name##AllArgs> \
  { \
    static size_t SizeOf(const Name##AllArgs &val) \
    { \
      Assert(val.arg2.GetUnusedBytes()<sizeof(val)); \
      return AlignStoreSize(sizeof(val) - val.arg2.GetUnusedBytes()); \
    } \
  };

CONSTANTS_PASSED(SetPixelShaderConstantsF)
CONSTANTS_PASSED(SetPixelShaderConstantsI)
CONSTANTS_PASSED(SetPixelShaderConstantsB)

CONSTANTS_PASSED(SetVertexShaderConstantsF)
CONSTANTS_PASSED(SetVertexShaderConstantsI)
CONSTANTS_PASSED(SetVertexShaderConstantsB)

/**
Size of empty class is by default 1 - this causes empty classes to break alignment for successive elements when stored.
*/
template <>
struct StoredSize<const EndSceneAllArgs>
{
  static size_t SizeOf(const EndSceneAllArgs &val) {return 0;}
};
template <>
struct StoredSize<const BeginSceneAllArgs>
{
  static size_t SizeOf(const BeginSceneAllArgs &val) {return 0;}
};
template <>
struct StoredSize<const GpuDisownAllAllArgs>
{
  static size_t SizeOf(const GpuDisownAllAllArgs &val) {return 0;}
};

//@}

/// implementation of servant for the D3D active object
class D3DDeviceServant
{
protected:
  /// pointer to a native device
  ComRef<IDirect3DDevice9> _dev;
  /// last SetIndices succeeded
  bool _ibOK;
  /// last SetStreamSource succeeded
  bool _vbOK;
  /// last number of primitives in SetStreamSource
  int _lastStreamSourceNumPrimitives;

public:
  D3DDeviceServant():_ibOK(true), _vbOK(true)
#if _ARMA3_VERTEX_BUFFER_EX
    , _lastStreamSourceNumPrimitives(VertexBuffer9Ex::UndefinedValue) 
#endif
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

  void AttachDevice(const ComRef<IDirect3DDevice9> &dev)
  {
    Base::_dev = dev;
  }
  void DetachDevice()
  {
    Base::_dev.Free();
  }

  bool IsNull() const {return Base::_dev.IsNull();}
};

struct PredicatedPart
{
  /// what predication (-1 =  none, >=0 = pass ID)
  int predicate:3;
  /// offset of the end of the predicated part
  int end:29;
  
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
  __forceinline void AttachDevice(const ComRef<IDirect3DDevice9> &dev)
  {
    D3DDeviceAutoLocked::operator =(dev);
    _cb.AttachDevice(dev);
  }
  __forceinline bool IsNull()
  {
    Assert(D3DDeviceAutoLocked::IsNull() == _cb.IsNull());
    return D3DDeviceAutoLocked::IsNull();
  }
};

/// wrapped D3D device, allowing CB recording
class MyD3DDeviceCB: public D3DDeviceAutoLocked<true>
{
  public:
  void operator += (IDirect3DDevice9 *dev){D3DDeviceAutoLocked<true>::operator += (dev);}
  void operator << (IDirect3DDevice9 *dev){D3DDeviceAutoLocked<true>::operator << (dev);}
};


//! Variation of the same shader for different purposes
enum PixelShaderType
{
  PSTBasic,                 // Ordinary PS - no shadows
  PSTBasicSSSM,             // PS with SSSM shadows
  PSTShadowReceiver1,       // PS with shadows - kernel size 1
  PSTShadowCaster,          // PS simplified as a caster
  PSTInvDepthAlphaTest,     // PS to write inverse of depth value including alpha kill for alpha-test
  PSTThermal,               // Thermal vision
  PSTAlphaOnly,             // PS to get accurate alpha - used for z-priming pass
  NPixelShaderType,
  PSTUninitialized = -1
};

//! Rendering modes
enum RenderingMode
{
  RMCommon,
  RMShadowBuffer,
  RMZPrime,
  RMDPrime,
  RMCrater,
  NRenderingMode
};

struct SSupportedFormats
{
  D3DFORMAT _format;
  int _bpp;
};

//! Shadow buffer size
const int BasicShadowBufferSize = 1024;


// several queues for different textures
// other states must be same

class TexMaterial;
class TextureD3D9;

struct TriQueue9
{
  //int _queueBase,_queueSize; // _triangleQueue position in VB

  StaticArray<WORD> _triangleQueue; // see BeginMesh,EndMesh

  TextureD3D9 *_texture; // which texture
  const TexMaterial *_material; // what material
  int _level; // which level
  int _special; // drawing flags
};

/// Vertex shader debugging switch
#if _ENABLE_CHEATS
extern bool DebugVS;
#else
const bool DebugVS=false;
#endif

/// enable debugging textures and render targets
//#define DEBUG_SURF

#if _ENABLE_PERFLOG
#define DEBUG_PERFHUD
#endif

/// simulate Xbox "fence" on PC
/**
Two implementations possible:
advanced and accurate using query
very simple using "safe estimate" -
assume buffer is locked for given number of frames since used
*/
class ResourceBusy9
{
  /// frame in which the fence will still be busy
  int _busyUntil;

public:
  // default number of frames the resource is considered busy
  static const int DefaultNumFrames = 2;

  ResourceBusy9():_busyUntil(0){};

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

struct SVertex9
{
  Vector3P pos;
  VectorCompressed norm;
  UVPairCompressed t0;
};
TypeIsSimple(SVertex9)

struct SVertex9_ST : public SVertex9
{
	STPair st;
};
TypeIsSimple(SVertex9_ST)


template<class Vertex> class VertexDynamicData9 
{
  friend class EngineDD9;

#ifdef DONT_USE_DYNBUFFERS
  //! Auxiliary array of vertices (used in AddVertices())
  /** TODO:MC: may need one instance for each CB */
  AutoArray<Vertex> _verticesOnly;
  //! Array of geometry to be drawn - unlike the _vBuffer this data are organized in triangles (without any need of IB)
  AutoArray<Vertex> _geometry;

	void FillBuffers(const VertexTableAnimationContext *animContext, const Shape &src);
#else
  ComRef<IDirect3DVertexBuffer9> _vBuffer[NumDynamicVB3D];
  const ComRef<IDirect3DVertexBuffer9> &GetVBuffer() const {return _vBuffer[_actualVBIndex];}
  int _vBufferSize; // reserved sizes of resources
  int _vBufferUsed; // used sizes
  int _actualVBIndex;
#endif

public:
  //! Constructor
  VertexDynamicData9(); 
  //! Creation of resources
  void Init(int size, MyD3DDevice &dev);
  //! Destruction of resources
  void Dealloc();
  //! Add vertices (in DONT_USE_DYNBUFFERS case organize them to triangle list - we will not use any IB)
  int AddVertices(int cb, const VertexTableAnimationContext *animContext, const Shape &src);
  //! Allocates space for n vertices and returns pointer to the vertices array to be filled out
  int Lock(Vertex *&vertex, int n);
  //! Unlocks the vertex buffer
  void Unlock();
	//! size of data in bytes
	const void* GetData() const 
	{
		return static_cast<const void*>(_geometry.Data());
	}
	//! size of data in bytes
	int GetDataSize() const 
	{
		return _geometry.Size()*sizeof(Vertex);
	}
	//! stride of vertex
	int GetStride() const
	{
		return sizeof(Vertex);
	}
	int GetVerticesCount() const 
	{
		return _geometry.Size();
	}
};

class FaceArray;
class FaceData;


/// index buffer encapsulation
class IndexStaticData9: public RefCount
{
  friend class EngineDD9;

public:
  typedef Future< ComRef<IDirect3DIndexBuffer9> > BufferFuture;

private:
  //! index buffer
  BufferFuture _iBuffer;
  /// index buffer size (count of items)
  int _count;
  /// busy tracker
  ResourceBusy9 _busy;

public:
  //! Constructor
  /*!
  \param indices Required number of indices in the buffer
  */
  IndexStaticData9(int indices);
  //! Destructor
  ~IndexStaticData9();
  //! Create the buffer synchronously
  void Init(ComRef<IDirect3DIndexBuffer9> buffer);
  //! Mark the buffer as a future
  void InitFuture();
  //! Create the buffer and set it to the Future
  void SetFuture(ComRef<IDirect3DIndexBuffer9> buffer);
  /// implementation of the buffer creation
  ComRef<IDirect3DIndexBuffer9> CreateBuffer();
	
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
  static bool AddShape(ComRef<IDirect3DIndexBuffer9> buffer, const FaceData &srcDta, int indicesTotal, bool dynamic, int nInstances, int instanceVertices, const AutoArray<Offset> &sectionsEnds);

  static void *Lock(ComRef<IDirect3DIndexBuffer9> buffer, int size);
  //! Add shape data as sprites to this buffer
  /*!
  \param src Shape to add
  \param iIndex Information about position and size of the data in the index buffer
  \param dynamic Method is working only if "dynamic == false" (we are working with static data)
  \return True if method succeeded, false elsewhere
  */
  static bool AddShapeSprite(ComRef<IDirect3DIndexBuffer9> buffer, const FaceData &src, int indicesTotal, bool dynamic, int nInstances);
  //! Add shape data as point to this buffer
  static bool AddShapePoint(ComRef<IDirect3DIndexBuffer9> buffer, const FaceData &src, int indicesTotal, bool dynamic, int nInstances, int instanceVertices);
  
  int NIndices() const {return _count;}
  
  int GetSize() const {return _count*sizeof(VertexIndex);}

  /// check if buffer is currently used by GPU
  bool IsBusy() const {return _busy.IsBusy();}

  void Used(int numFrames = ResourceBusy9::DefaultNumFrames){_busy.Used(numFrames);}
};

class VertexBufferD3D9;

/// vertex buffer encapsulation
class VertexStaticData9: public RefCount
{
  friend class EngineDD9;

public:
#if _ARMA3_VERTEX_BUFFER_EX
  typedef Future< VertexBuffer9Ex > BufferFuture;
#else
  typedef Future< ComRef<IDirect3DVertexBuffer9> > BufferFuture;
#endif

private:
  //! Count of vertices held
  int _count;
  //! Size of one vertex in bytes
  int _vertexSize;
  /// busy tracker
  ResourceBusy9 _busy;
  /// track if dynamic usage was used
  bool _usageDynamic;
  //! Managed vertex buffer
  BufferFuture _vBuffer;

public:
  //!{ Fills out the data with the shape (specialized version for each data type required)
  template <int VertexDecl>
  static void AddShapeData(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf);
  template <int VertexDecl>
  static void AddShapeInstancedData(void *data, const VertexTableData &src, const VertexBufferD3D9 *vbuf, int instancesCount);
  //! Sprite has an own function to fill out the data
  static void AddShapeSpriteInstanced(void *data, const VertexTableData &src, int instancesCount);
  //! Points have an own function to fill out the data
  static void AddShapePoint(void *data, const VertexTableData &src);
  //!}
  //!{ Locking and unlocking vertex data
  static void *Lock(ComRef<IDirect3DVertexBuffer9> buffer, int size);
  //!}

  //! Constructor
  VertexStaticData9(int vertices, int vertexSize, bool frequent);
  //! Destructor
  ~VertexStaticData9();
  
  //! Create the buffer synchronously
  void Init(ComRef<IDirect3DVertexBuffer9> buffer
#if _ARMA3_VERTEX_BUFFER_EX
    , int numPrimitives = VertexBuffer9Ex::UndefinedValue
#endif
    );
  //! Mark the buffer as a future
  void InitFuture();
  //! Create the buffer and set it to the Future
  void SetFuture(ComRef<IDirect3DVertexBuffer9> buffer
#if _ARMA3_VERTEX_BUFFER_EX
    , int numPrimitives = VertexBuffer9Ex::UndefinedValue
#endif
    );

  /// implementation of the buffer creation
  ComRef<IDirect3DVertexBuffer9> CreateBuffer();

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

  void Used(int numFrames = 2){_busy.Used(numFrames);}
};

enum
{
  // triangle reordering queues have very little effect with HW T&L
  // as they are primarily used to optimize 3D operation across several objects
  // they have little effect on 2D operation, but they also make FlushAllQueues much slower
  MaxTriQueues=1,
  TriQueueSize=4096
};

struct Queue9
{
  VertexStaticData9::BufferFuture _meshBuffer[NumDynamicVB2D];
  IndexStaticData9::BufferFuture _indexBuffer[NumDynamicVB2D];
  int _vertexBufferUsed; // how many vertices are in vertex buffer
  int _indexBufferUsed; // how many indices are is index buffer

  int _meshBase,_meshSize; // _mesh position in VB
  int _actualVBIndex;
  int _actualIBIndex;

  TriQueue9 _tri;
  bool _triUsed;

  bool _firstVertex; // first call in scene should always use DISCARD
  bool _firstIndex;

  Queue9();
  int Allocate(TextureD3D9 *tex, int level, const TexMaterial *mat, int spec);
  void Free();

  const VertexStaticData9::BufferFuture &GetVBuffer() const {return _meshBuffer[_actualVBIndex];}
  const IndexStaticData9::BufferFuture &GetIBuffer() const {return _indexBuffer[_actualIBIndex];}
};

// ------------------------
// Shader constants factory
/*
// The factory
#define CONSTANTS_LIST(XX) \
XX(proj_matrix) \
XX(view_matrix) \
XX(view_matrix_1)  XX(view_matrix_2)  XX(view_matrix_3)  XX(view_matrix_4)  XX(view_matrix_5)  XX(view_matrix_6)  XX(view_matrix_7)  XX(view_matrix_8)  XX(view_matrix_9) \
XX(view_matrix_10) XX(view_matrix_11) XX(view_matrix_12) XX(view_matrix_13) XX(view_matrix_14) XX(view_matrix_15) XX(view_matrix_16) XX(view_matrix_17) XX(view_matrix_18) XX(view_matrix_19) \
XX(view_matrix_20) XX(view_matrix_21) XX(view_matrix_22) XX(view_matrix_23) XX(view_matrix_24) XX(view_matrix_25) XX(view_matrix_26) XX(view_matrix_27) XX(view_matrix_28) XX(view_matrix_29) \
XX(view_matrix_30) XX(view_matrix_31) XX(view_matrix_32) \
XX(matrix_offset) \
XX(A_Dforced_E) \
XX(LDirection_D) \
XX(LDirection_S) \
XX(LDirection_TransformedDir) \
XX(LPoint_TransformedPos1)  XX(LPoint_TransformedPos2)  XX(LPoint_TransformedPos3)  XX(LPoint_TransformedPos4)  XX(LPoint_TransformedPos5)  XX(LPoint_TransformedPos6)  XX(LPoint_TransformedPos7)  XX(LPoint_TransformedPos8) \
XX(LPoint_Atten1)           XX(LPoint_Atten2)           XX(LPoint_Atten3)           XX(LPoint_Atten4)           XX(LPoint_Atten5)           XX(LPoint_Atten6)           XX(LPoint_Atten7)           XX(LPoint_Atten8) \
XX(LPoint_D1)               XX(LPoint_D2)               XX(LPoint_D3)               XX(LPoint_D4)               XX(LPoint_D5)               XX(LPoint_D6)               XX(LPoint_D7)               XX(LPoint_D8) \
XX(LPoint_A1)               XX(LPoint_A2)               XX(LPoint_A3)               XX(LPoint_A4)               XX(LPoint_A5)               XX(LPoint_A6)               XX(LPoint_A7)               XX(LPoint_A8) \
XX(LSpot_TransformedPos1) XX(LSpot_TransformedPos2) XX(LSpot_TransformedPos3) XX(LSpot_TransformedPos4) XX(LSpot_TransformedPos5) XX(LSpot_TransformedPos6) XX(LSpot_TransformedPos7) XX(LSpot_TransformedPos8) \
XX(LSpot_TransformedDir1) XX(LSpot_TransformedDir2) XX(LSpot_TransformedDir3) XX(LSpot_TransformedDir4) XX(LSpot_TransformedDir5) XX(LSpot_TransformedDir6) XX(LSpot_TransformedDir7) XX(LSpot_TransformedDir8) \
XX(LSpot_CosPhiHalf1)     XX(LSpot_CosPhiHalf2)     XX(LSpot_CosPhiHalf3)     XX(LSpot_CosPhiHalf4)     XX(LSpot_CosPhiHalf5)     XX(LSpot_CosPhiHalf6)     XX(LSpot_CosPhiHalf7)     XX(LSpot_CosPhiHalf8) \
XX(LSpot_CosThetaHalf1)   XX(LSpot_CosThetaHalf2)   XX(LSpot_CosThetaHalf3)   XX(LSpot_CosThetaHalf4)   XX(LSpot_CosThetaHalf5)   XX(LSpot_CosThetaHalf6)   XX(LSpot_CosThetaHalf7)   XX(LSpot_CosThetaHalf8) \
XX(LSpot_InvCTHMCPH1)     XX(LSpot_InvCTHMCPH2)     XX(LSpot_InvCTHMCPH3)     XX(LSpot_InvCTHMCPH4)     XX(LSpot_InvCTHMCPH5)     XX(LSpot_InvCTHMCPH6)     XX(LSpot_InvCTHMCPH7)     XX(LSpot_InvCTHMCPH8) \
XX(LSpot_Atten1)          XX(LSpot_Atten2)          XX(LSpot_Atten3)          XX(LSpot_Atten4)          XX(LSpot_Atten5)          XX(LSpot_Atten6)          XX(LSpot_Atten7)          XX(LSpot_Atten8) \
XX(LSpot_D1)              XX(LSpot_D2)              XX(LSpot_D3)              XX(LSpot_D4)              XX(LSpot_D5)              XX(LSpot_D6)              XX(LSpot_D7)              XX(LSpot_D8) \
XX(LSpot_A1)              XX(LSpot_A2)              XX(LSpot_A3)              XX(LSpot_A4)              XX(LSpot_A5)              XX(LSpot_A6)              XX(LSpot_A7)              XX(LSpot_A8) \
XX(specularPower_alpha_fogEnd_rcpFogEndMinusFogStart) \
XX(texTransform0) XX(texTransform1)

// The enum
#define CONSTANTS_LIST_ENUM_ITEM(item) item,
enum EConstantsList
{
CONSTANTS_LIST(CONSTANTS_LIST_ENUM_ITEM)
NEConstantsList
};
*/

// Type of Texture coordinate generation
enum ETTG {
  TTG_UNASSIGNED = -1,
  TTG_MATRIX = 0,
  TTG_COPY = 1
};

/// proxy class used to avoid necessity to declare VertexBufferD3D here
ALIGN_SLIST class VertexBufferD3D9LRUItem: public LinkBidirWithFrameStore, public SListControlled
{
  friend class EngineDD9;
  /// if shape is destroyed, vertex buffer is always destroyed with it
  /**
  owner should call Detach(owner} before destruction
  this is verified in destructor
  */
  const VertexTable *_owner;

public:
  VertexBufferD3D9LRUItem() {_owner = NULL;}
  ~VertexBufferD3D9LRUItem() {DoAssert(_owner==NULL);}
  virtual size_t GetMemoryControlled() const = NULL;

protected:
  void SetOwner(const VertexTable *owner){_owner = owner;}
  const VertexTable *GetOwner() const {return _owner;}
};

/// query manager
class QueryId
{
  /// Xbox HW limit is D3DVISIBILITY_TEST_MAX - 16K, but we do not need that much
  enum {MaxQueries=1024};
  /// recycle handles
  ComRef<IDirect3DQuery9> _query[MaxQueries];
  /// check which queries are currently in use
  bool _used[MaxQueries];
  /// last one we have used - helps to find the next free one fast
  int _curUsed;

public:
  QueryId();

  int Open();
  void Close(int id);

	IDirect3DQuery9* GetQuery(int id)
	{
		return _query[id];
	}
};

//! Stencil shadow drawing mode
enum EShadowState
{
  SS_None,
  /// transparent - does not change z buffer, does not change stencil buffer
  SS_Transparent,
  /// ordinary model, can receive stencil shadows
  SS_Receiver,
  /// ordinary model, can not receive stencil shadows
  SS_Disabled,
  /// front facing part of the shadow volume
  SS_Volume_Front,
  /// back facing part of the shadow volume
  SS_Volume_Back,
  //! Shadow buffer rendering
  SS_ShadowBuffer,
};

//! Enum to determine what SB technique we can use on present HW
/*!
\patch 5151 Date 3/29/2007 by Flyman
- Fixed: Crash when trying to use HW without possibility to use shadow buffer technique (observed on Intel on board graphics card)
*/
enum SBTechnique
{
  SBT_Default,  // D3DFMT_R32F format of texture, PS calculation
  SBT_nVidia,   // nVidia solution - rendering to depth buffer D3DFMT_D24S8
  SBT_nVidiaINTZ, // nVidia solution - rendering to depth buffer D3DFMT_D24S8, using INTZ
  SBT_ATIDF16,  // ATI DF16 solution
  SBT_ATIDF24,  // ATI DF24 solution (can be Fetch4, not yet implemented)
  SBT_None,     // No SB shadows can be used
  SBTCount
};


//#define NEW_SB_INIT
#ifdef NEW_SB_INIT
	/*!
	wanted formats for shadow buffers
	*/
	struct SBFormat
	{
		D3DFORMAT		_depthFormat;
		D3DFORMAT		_rtFormat;
		SBTechnique	_sbTechnique;
	};			
#endif



struct FloatVSConstant
{
  float ar[4];

  FloatVSConstant() {};
  float operator[] (int ix) const { return ar[ix]; }
  float &operator[] (int ix) { return ar[ix]; }
};
TypeIsSimple(FloatVSConstant);

// needed for D3DXMacroList
TypeIsSimple(D3DXMACRO)

#if defined _M_PPC && defined _MATH3DK_HPP
typedef XMVECTOR MatrixFloat34[3];
#else
typedef float MatrixFloat34[3][4];
#endif

//! Informations related to one section
struct VBSectionInfo
{
  int beg,end;
  int begVertex,endVertex; // used vertex range
};
TypeIsSimple(VBSectionInfo);

//! Maximum number of UV sets
#define MAX_UV_SETS 2

//! Specialized version of vertex buffer
class VertexBufferD3D9: public VertexBuffer, public VertexBufferD3D9LRUItem
{
  friend class EngineDD9;
  friend class VertexStaticData9;

private:

  bool _dynamic;

  AutoArray<VBSectionInfo> _sections;

  //! Maximum UV values the vertex buffer contains (suitable for UV compression) for each stage
  UVPair _maxUV[MAX_UV_SETS];
  //! Minimum UV values the vertex buffer contains (suitable for UV compression) for each stage
  UVPair _minUV[MAX_UV_SETS];

  // following are valid only for "shared" (!_separate)
  Ref<VertexStaticData9> _sharedV;
  Ref<IndexStaticData9> _sharedI;

  /// optimization: avoid refreshing twice in the same frame, refreshing is slow (includes a critical section)
  int _frameRefreshed;

public:
  VertexBufferD3D9();
  ~VertexBufferD3D9();

  bool Init(EngineDD9 *engine, const Shape &src, VBType type, bool frequent);

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

  VertexStaticData9 *GetSharedV() const {return _sharedV;}
  IndexStaticData9 *GetSharedI() const {return _sharedI;}

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

  //@{ implementation of VertexBufferD3D9LRUItem
  virtual size_t GetMemoryControlled() const;
  //@}
};

struct FallbackNew
{
  static void *Alloc(size_t size) {return new char[size];}
  static void Free(void *mem) {return delete[] (char *)mem;}
};

/// simple "growing" allocator
/**
Allocating from a pre-allocated heap, growing only, Free is not returning any memory into the pre-allocated heap
MT safe, lock free and wait free
*/
template <class Fallback=FallbackNew>
class SimpleAllocatorMT
{
  private:
  //@{ memory backup
  char *_memory;
  int _size;
  //@}
  /// how much is already allocated
  AtomicInt _used;
  /// buffer can be used as circular, freeing from the beginning
  /** note: freeing is not MT safe */
  int _freeTo;
  
  #if _DEBUG || _PROFILE
    #define DEBUG_COUNT_ALLOC 1
  #else
    #define DEBUG_COUNT_ALLOC 0
  #endif
  #if DEBUG_COUNT_ALLOC
    /// a simple way do detect existence of leak (missing Free for Allocate)
    AtomicInt _allocationsCount;
    /** it is possible we access this only from main thread, but having it MT safe does little harm */
    AtomicInt _allocationsAmount;
  #endif

  /// accumulate all memory allocated and released
  InitVal<int,0> _memoryTraffic;
  
  public:
  SimpleAllocatorMT()
  {
    _used = AtomicInt(0);
    _freeTo = 0; // modulo arithmetics - same as "whole free"
    #if DEBUG_COUNT_ALLOC
      _allocationsCount = AtomicInt(0);
      _allocationsAmount = AtomicInt(0);
    #endif
  }

  void SetMemory(void *memory, int size)  
  {
    _size = size;
    _freeTo = size;
    _memory = (char *)memory;
    // we are using x&size to implement cyclical buffer - check if size meets requirements
    DoAssert(roundTo2PowerNCeil(_size)==_size || _size==0);
  }
  void *GetMemory() const {return _memory;}


  void *Allocate(int size)
  {
    #if DEBUG_COUNT_ALLOC
      _allocationsCount++;
      _allocationsAmount += size;
    #endif
    
    Assert(size>0 && size<_size);
    Retry:
    // note: += returns new value, but underlying InterlockedExchangeAdd returns original value
    int newEnd = (_used += size);
    // TODO: check the compiler optimizes +size-size away
    int newStart = newEnd-size;
    
    // allow circular manipulation
    int newEndCircular = newEnd&(_size-1);
    int newStartCircular = newStart&(_size-1);
    // check if we looped around
    if (newEndCircular<newStartCircular)
    {
      // we can never go back. Once we have crossed the boundary, we need to allocate once more
      // check if we have any chance to fit into the free region
      if (newEnd+size<_freeTo)
      {
        goto Retry;
      }
    }
    else
    {
      // newStart..newEnd
      // free region is newStart.._free, _free is always behind the start
      // verify we fit into the free region
      int free = _freeTo-newEnd; // size of the free region
      // if there is still some memory free after the allocation, we may use it
      if (free>0)
      {
        return _memory+newStartCircular;
      }
    }
    LogF("SimpleAllocatorMT not large enough (%d, alloc %d, needed %d)",_size,size,_size-(_freeTo-newEnd));
    // 
    // once we fail, we always fail (and use dynamic allocator) - this keeps implementation simple
    // we could also track back on failure, some allocation might fail meanwhile, but successive ones might succeed again
    // another alternative could be a lock-free (but not wait-free) loop
    // however, as we do not expect the failures to happen at all, it is not worth implementing
    return Fallback::Alloc(size);
  }
  
  /** free is still needed because of fallback */
  void Free(void *mem, int size)
  {
    #if DEBUG_COUNT_ALLOC
      _allocationsCount--;
      _allocationsAmount-=size;
      // when count is zero, amount needs to be zero as well
      Assert(_allocationsCount>0 || _allocationsAmount==0);
      DoAssert(_allocationsCount>=0);
    #endif
    // when freeing the memory allocated by new, we must delete it
    if (mem<_memory || mem>=_memory+_size)
    {
      Fallback::Free(mem);
    }
  }
  
  void Clear(int amount)
  {
    // move the free boundary
    _freeTo += amount;
    _memoryTraffic += amount;
  }
  
  /* not MT safe - may be called only when other threads are not accessing the allocator */
  int Compact()
  {
    // the caller is currently calling this only when expecting all allocations are flushed
    #if DEBUG_COUNT_ALLOC
      DoAssert(_allocationsCount==0);
      DoAssert(_allocationsAmount==0);
    #endif
    // if whole buffer is free, we may reset the buffer
    // ... and we know the buffer is free, otherwise nobody should call the Compact
    //if (_freeTo==_used+_size)
    if (true)
    {
      // verify there is nothing left
      int adjust = -_used;
      _used = AtomicInt(0);
      _freeTo = _size;
      return adjust;
    }
    else
    {
      // both numbers need to move the same amount, so that _freeTo does not chase in front of _used
      // as _freeTo goes behind _used (and is therefore smaller), we always modify based on it
      DoAssert(_freeTo>_used);
      int newUsed = _used&(_size-1);
      int adjust = newUsed-_used;
      _freeTo += adjust;
      _used = AtomicInt(newUsed);
      DoAssert(_freeTo>_used);
      return adjust;;
    }
  }
  
  /// return memory allocation measurements results and reset them
  /** the stats are maintained by Compact, which is responsible for reclaiming the memory */
  int AllocationTurnover()
  {
    int ret = _memoryTraffic;
    _memoryTraffic = 0;
    return ret;
  }
  
  int MaxSize() {return _size;}
  
  int Allocated() const {return _size-(_freeTo-_used);}
  int UsedPtr() const {return _used;}
};

/// Coefficient to scale by the content of an environmental map (to represent bigger range of values)
static const float EnvMapScale = 1.0f;

#if _PROFILE || _DEBUG
  #define PIX_NAMED_CB_SCOPES 1
#endif

class TextBankD3D9;

typedef D3DXMATRIX PASSMATRIXTYPE;

/// pixel shader identification
struct PSName
{
  const char *name;
};

/// one pixel shader compilation
struct PSCache
{
  PSName name;
  ComRef<IDirect3DPixelShader9> ps;
};
TypeIsMovable(PSCache)

template <>
struct FindArrayKeyTraits<PSCache>
{
  typedef const PSName &KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b)
  {
    return !strcmp(a.name ? a.name : "",b.name ? b.name : "");
  }
  /// get a key from an item
  static KeyType GetKey(const PSCache &a) {return a.name;}
};

/// one bound shader info
struct D3DBoundShaderName
{
  ComRefFake<IDirect3DVertexShader9> _vs;
  ComRefFake<IDirect3DVertexDeclaration9> _vd;
  int _stride;
  ComRefFake<IDirect3DPixelShader9> _ps;
  
  D3DBoundShaderName(){}
  D3DBoundShaderName(IDirect3DVertexShader9 *vs, IDirect3DVertexDeclaration9 *vd, int stride, IDirect3DPixelShader9 *ps)
  :_vs(vs),_vd(vd),_stride(stride),_ps(ps)
  {}
};

struct D3DBoundShader
{
  D3DBoundShaderName _name;
};


TypeIsMovableZeroed(D3DBoundShader)

template <>
struct MapClassTraits<D3DBoundShader>: public DefMapClassTraits<D3DBoundShader>
{
  typedef const D3DBoundShaderName &KeyType;
  
  /// calculate hash value
  static unsigned int CalculateHashValue(KeyType key)
  {
    //TODO: use IDs instead
    return (int)key._vs.GetRef()+(int)key._ps.GetRef()*33+(int)key._vd.GetRef()*33+(int)key._stride*33;
  }

  /// compare keys, return negative when k1<k2, positive when k1>k2, zero when equal
  static int CmpKey(KeyType k1, KeyType k2)
  {
    if (k1._vs!=k2._vs) return 1;
    if (k1._ps!=k2._ps) return 1;
    if (k1._vd!=k2._vd) return 1;
    if (k1._stride!=k2._stride) return 1;
    return 0;
  }
  
  /// get a key for given a item
  static KeyType GetKey(const D3DBoundShader &item) {return item._name;}
};

/// manager of bound shaders
class D3DBoundShadersManager
{
  
  // hash-map based on ID - low level solution?
  // TODO: use SetIdentifier / GetIdentifier to map VS/PS/VD to ID, which can then be used as a hash key
  MapStringToClass< D3DBoundShader, AutoArray<D3DBoundShader> > _bound;

  public:  
  IDirect3DVertexShader9 *GetBoundShader(const D3DBoundShaderName &name) const
  {
    const D3DBoundShader &bound = _bound.Get(name);
    if (_bound.IsNull(bound)) return NULL;
    return name._vs;
  }
  
  void Bind(EngineDD9 *engine, const D3DBoundShaderName &name);
};


// 8b stencil layout
#define STENCIL_FULL 0xff
#define STENCIL_COUNTER 0xff // 5 bits counter
#define STENCIL_SHADOW_LEVEL_MASK 0x00
#define STENCIL_SHADOW_LEVEL_STEP 0x00

  

#include <El/ParamArchive/serializeClass.hpp>
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

TypeIsMovable(PostProcessHndl);

// invalid post process handle
static PostProcessHndl InvalidPPHndl(-1, PENone, 0);

template <>
struct FindArrayKeyTraits<PostProcessHndl>
{
  typedef const int &KeyType;
  /// check if two keys are equal
  static bool IsEqual(KeyType a, KeyType b) { return a == b; }
  /// get a key from an item
  static KeyType GetKey(const PostProcessHndl &a) { return a._hndl; }
};

class IPPHndlFunc
{
public:
  virtual bool operator () (PostProcessHndl &hndl) = 0;
};

//@{ default set post process priorities
const static int PPSSAOPriority = 90;
const static int PPGodRaysPriority = 95;
const static int PPRadBlurPriority = 100;
const static int PPChrAbberPriority = 200;
const static int PPWetDistortPriority = 300;
const static int PPDynBlurPriority = 400;
const static int PPRainPriority = 600;
const static int PPGlowPriority = 1000;
const static int PPColorPriority = 1500;
const static int PPFilmGrainPriority = 2000;
const static int PPColorInvPriority = 2500;
//}@

#undef max
#undef min

template <class type>
inline type max(type a, type b){return a>b ? a : b;}

template <class type>
inline type min(type a, type b){return a<b ? a : b;}

#include <limits>


template <class Numeric, int age, int cluster>
class MovingHistory
{
  static const int NClusters = (age+cluster-1)/cluster;
  Numeric _clustersMax[NClusters];
  Numeric _clustersMin[NClusters];
  int _clusterAge;
  
  public:
  MovingHistory()
  {
    _clusterAge = 0;
    for (int i=0; i<NClusters; i++) _clustersMax[i] = 0, _clustersMin[i] = 0;
  }
  void AddValue(Numeric value)
  {
    if (++_clusterAge>=cluster)
    {
      for (int i=0; i<NClusters-1; i++)
      {
        _clustersMax[i] = _clustersMax[i+1];
        _clustersMin[i] = _clustersMin[i+1];
      }
      _clustersMax[NClusters-1] = +std::numeric_limits<Numeric>::min();
      _clustersMin[NClusters-1] = +std::numeric_limits<Numeric>::max();
      _clusterAge = 0;
    }
    if (value>_clustersMax[NClusters-1]) _clustersMax[NClusters-1] = value;
    if (value<_clustersMin[NClusters-1]) _clustersMin[NClusters-1] = value;
  }
  
  Numeric Min() const
  {
    Numeric ret = +std::numeric_limits<Numeric>::max();
    for (int i=0; i<NClusters; i++) ret = min(ret,_clustersMin[i]);
    return ret;
  }
  Numeric Max() const
  {
    Numeric ret = -std::numeric_limits<Numeric>::max();
    for (int i=0; i<NClusters; i++) ret = max(ret,_clustersMax[i]);
    return ret;
  }
};

#if _PROFILE || _DEBUG
# define LOG_RESULTS_ALLOC 0
#endif

#if LOG_RESULTS_ALLOC
  #define LOG_MEM(format,...) GEngineDD->MemJournalAdd(Format(format,__VA_ARGS__))
  #else
  #define LOG_MEM(format,...) do {} while (false)
  #endif

struct LazyLoadFile;

//! D3D engine implementation
/*!
both HW and SW T&L version are implemented
*/

#define ENABLE_NVG_NOISE 1

namespace enf
{
	class IRenderer;
  class ShapeCollection;
}

class EngineDD9: public Engine, public MemoryFreeOnDemandHelper
{
	friend class enf::IRenderer;
  friend class D3DVisibilityQuery;
  friend class D3DDeviceServant;
  friend class TextBankD3D9;
  friend class TextureD3D9;
  friend class enf::ShapeCollection;
  
  //!{ Dynamic post effect - script interface
  friend struct PPPriorities;
  friend struct PostProcessHndl;
  friend class ReleasePPHndlFunc;
  friend class ResetPPHndlFunc;
  friend class InitPPHndlFunc;
  //!}

  // making GEngine global shadowed / inaccesable inside EngineDD9
  nullptr_t GEngine;
    
#if _ARMA3_SIMUL_WEATHER
  friend class SimulCloudRendererDX9;
  friend class SimulSkyRendererDX9;
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
  class PPSSSM;
  class PPSSSMStencil;
#ifdef SSSMBLUR
  class PPSSSMB;
#endif
  class PPDOF;
  class PPDistanceDOF;
  class PPFilmGrain;
	class PPGodRays;
	class PPRain;
	class PPEnfShapeCollectionRes;
  class PPSSAO;
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
  class PPFXAA;
  class PPSMAA;
  
  class PPResources;
  class PPColorResources;
  class PPWetDistortResources;
  class PPResourcesExt;
  class PPSimpleExt;
  class PPDynBlurResources;
  class PPFilmGrainResources;
	class PPGodRaysResources;
	class PPRainResources;
  class PPSSAOResources;
  //!}

public:
    void SetSpecialEffectPars(int index, const float *pars, int nPars);

 struct Settings
  {
    Settings() {Reset();}
    void Reset()
    {
      _w = 0;
      _h = 0;
      _bpp = 0;
      _refreshRate = 0;
      _wRTPreferred = 0;
      _hRTPreferred = 0;
      _wRTComputed = 0;
      _hRTComputed = 0;
      _hdrPrec = HDR8;
      _vsync = true;
      _multiSampleQuality = 0;
      _ppaaQuality = 0;
      _AToCMask = VideoOptionsConfig::ATOCValue::AToC_EnableAll;
      _afWanted = 0;
      _r2tQuality = 0;
      _simulWeatherQuality = 0;
      _shadowQuality = 0;
      _shadowTextureSize = 0;
      _shadowShaderQuality = 0;

      _textureQuality = 0;

      _postFxQuality = 0;
			_ssaoEnabled = 0;
    }

    /// back buffer width
    int _w;
    /// back buffer height
    int _h;
    /// backbuffer bits per pixel
    int _bpp; 
    /// preferred refresh rate
    int _refreshRate;
    /// preferred main render target width
    int _wRTPreferred;
      /// preferred main render target height
    int _hRTPreferred;    
    /// computed main render target width (based on preferred one)
    int _wRTComputed;
    /// computed main render target height (based on preferred one)
    int _hRTComputed;    
    /// HDR precision requested by the user (8,16 or 32 may make a sense)
    HDRPrec _hdrPrec;
    /// wanted quality of MSAA (corresponds to quality in DXGI_SAMPLE_DESC)
    int _multiSampleQuality;
    // enable / disable vertical synchronization, default set to enable
    bool _vsync;
    /// quality of ppaa
    int _ppaaQuality;
    // antialiasing quality
    int _fsaaQuality;
    /// mask for AToC
    VideoOptionsConfig::ATOCValue::AToCMask _AToCMask;
    ///{ anisotropy filtering settings
    int _afWanted;

    /// quality of render to texture (PIP)
    int _r2tQuality;

    /// quality of simul weather simulation
    int _simulWeatherQuality;

    /// quality of shadows
    int _shadowQuality;
    /// size of shadow texture
    int _shadowTextureSize;
    /// quality of shadow shader
    int _shadowShaderQuality;

    //@{ corresponds to TextBank value,
    /** needed here because it can be set before TextBank is created */
    int _textureQuality;
    //@}

    /// postprocess effects quality
    int _postFxQuality;
		int _ssaoEnabled; 
  };

private:
  // @{
  class ISpecialEffect;
  class HitSpecEffect;
  class MovementSpecEffect;
  class TiredSpecEffect;
  // @}

  struct SPP
  {
    Ref<HitSpecEffect> _hit;
    Ref<MovementSpecEffect> _movement;
    Ref<TiredSpecEffect> _tired;
  } _spp;

  /// HLSL include support for custom files
  class HLSLInclude:public ID3DXInclude
  {
  public:
    /**
    @param data [out] file data
    @param bytes [out] file size
    @return error code
    */
#if !defined(_X360SHADERGEN)
    STDMETHOD(Open)(
      D3DXINCLUDE_TYPE includeType, LPCSTR fileName,
      LPCVOID parentData,
      LPCVOID * data, UINT * bytes
      );
#else
    STDMETHOD(Open)(D3DXINCLUDE_TYPE includeType, LPCSTR fileName, LPCVOID parentData,
      LPCVOID *data, UINT *bytes,
      /* OUT */ LPSTR fullPath, DWORD fullPathSize);
#endif
    STDMETHOD(Close)(LPCVOID pData);

  };


  typedef Engine base;

protected:
  // data members
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

  #if !XBOX_D3D
    //@{ remember windowed and fullscreen settings - needed in case we want to switch between them
    int _winX,_winY,_winW,_winH;
    int _WindowAspectW,_WindowAspectH;
    int _fullW,_fullH;
    //@}
  #endif

#if XBOX_D3D
  //!{ Tiling rectangles
  D3DRECT _tilingRects[8];
  INT _tilingRectsCount;
  //!}
#endif


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

  /// we may use _ddraw interface to peform memory status queries
  ComRef<IDirectDraw7> _ddraw;

  D3DPRESENT_PARAMETERS _presentPars;

  //! Flag to determine whether NVPerfHUD is active or not
  bool _nvPerfHUDActive;

  //! Engine's device type
  D3DDEVTYPE _devType;

  /// format of the primary render target
  D3DFORMAT _rtFormat;

  /// format used for shadow maps (or variance shadow maps)
  D3DFORMAT _smFormat;

  /// format of the down-sampling render target
  /** we want this to be the same as _rtFormat, but we need filtering available */
  D3DFORMAT _downsampleFormat;

  /// format of aperture surfaces
  D3DFORMAT _apertureFormat;

  //@{ multi-sampling settings
  D3DMULTISAMPLE_TYPE _rtMultiSampleType;
  DWORD               _rtMultiSampleQuality;

  /// user FSAA settings
  int _fsAAWanted;
  /// max FSAA supported by the current mode
  int _maxFSAA;

  /// user FXAA preset, 0 = disabled, 1 = FXAA_PRESET_10 ...
  int _ppaaQuality;

  /// user FXAA sharp filter edge value, 0 = disabled,
  int _fxaaSharpQuality;

  enum AToCMode
  {
    AToC_None,
    AToC_nVidia,
    AToC_ATI,
  };
  
  enum AToCMask
  {
    AToC_Disable = 0,
    AToC_Grass = 1,
    AToC_TreeNew = 2,
    AToC_TreeOld = 4,
    AToC_EnableAll = 7
  };
  /// which AToC mode is supported by the HW
  AToCMode _aToCSupported;
  
  /// should we use AToC?
  bool _useAToC;
  int _AToCMask;
  //@}

  // enable / disable vertical synchronization, default set to enable
  bool _vsync;
  //@{ anisotropy filtering settings
  int _afWanted;
  /// max. level allowed by the HW (typicaly 1..16)
  int _maxAFLevel;
  //@}

  int _postFxQuality;					/// postprocess effects quality
	bool _ssaoEnabled;					/// ssao in options enabled, if it is really used depends on _postFxQuality setting

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

  QueryId _queryIDs;

protected:
  Ref<TextBankD3D9> _textBank;
  
  ComRef<IDirect3D9> _direct3D;
  #if !XBOX_D3D
  ComRef<IDirect3D9Ex> _direct3DEx;
  #endif

  /// direct 3D device - encapsulation to protect against access while CB recording, provide playback means
  mutable MyD3DDevice _d3DDevice;

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
  
  /// record into a background buffer or execute depending on input
  #define CALL_D3D_MEMBER(engine,Function,...) \
    engine->AssertLowLevelThreadAccess(); /* Assert(XXX) would be nicer, but leads to a strange compiler error */ \
    if (!engine->IsLLRecording()) engine->_d3DDevice->Function(__VA_ARGS__); \
    else {engine->_cbState[0]._rec.Function(__VA_ARGS__);KICK_OFF_REC(engine);}
  
  /// common variant of CALL_D3D_MEMBER - used in EngineDD9 member functions
  /** TODO: PRIM: verify we are not called from the main thread when doing HighLevel recording */
  #define CALL_D3D(Function,...) CALL_D3D_MEMBER(this,Function,__VA_ARGS__)

  /// record into a background buffer or call (Do)Function engine function
  #define CALL_CB_DIRECT(engine,Function,...) \
  engine->AssertLowLevelThreadAccess(); /* Assert(XXX) would be nicer, but leads to a strange compiler error */ \
  if (!engine->IsLLRecording()) engine->Do##Function(__VA_ARGS__); \
    else {engine->_cbState[0]._rec.Function(__VA_ARGS__);KICK_OFF_REC(engine);}

public:    
	enum {MaxStages=8,MaxSamplerStages=16,UsableSampleStages=15};
  
	HWND GetHWND() const	
	{
		return _hwndApp;
	}
  
protected:

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

  //@{ active viewport z-range
  float _minZRange,_maxZRange;
  //@}


  // dynamic data for TL
  VertexDynamicData9<SVertex9>		_dynVBuffer_SVertex9; // EXCL
	VertexDynamicData9<SVertex9_ST> _dynVBuffer_SVertex9_ST; // EXCL

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
    /// WDDM driver (Vista or newer, no local/nonlocal has sense)
    bool _wddm;

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

    //! Flag to determine SM2 is being used (either the HW doesn't support SM3, or the engine was forced to use SM2)
    bool _sm2Used;
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
  FindArrayKey<PSCache> _psCache;
  
  /// shader binding manager - used to reduce vertex shader patching
  D3DBoundShadersManager _boundShaders;
  
  IDirect3DPixelShader9 *PSFromCache(const char *psName) const
  {
    PSName name;
    name.name = psName;
    int index = _psCache.FindKey(name);
    if (index>=0) return _psCache[index].ps;
    ErrF("Shader %s not found in cache",psName);
    return NULL;
  }
  /// map shader IDs to D3D shader objects
  ComRef<IDirect3DPixelShader9> _pixelShader[NPixelShaderID][NPixelShaderType];
  /// map _pixelShader back into _psCache
  int _pixelShaderCacheId[NPixelShaderID][NPixelShaderType];
  /// LRU list of vertex buffers - used to release buffer that were not used for a long time
  /** list of all shapes which have geometry loaded, but it can be released */
  FramedMemoryControlledList<VertexBufferD3D9LRUItem> _vBufferLRU;

  /// lock free container gathering updates for LRU requests
  LockFreeGrowingList<VertexBufferD3D9> _vbLRURequest;

  void ProcessLRURequests();

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

  // BLoom
  const float DefaultBloomThreshold;
  float _bloomThreshold;
  // RotBlur
  const float DefaultRotBlurPower;
  float _rotBlurPower;


protected:


  /// HLSL include support for custom files
  HLSLInclude _hlslInclude;
  //! Array of all possible vertex declarations
  ComRef<IDirect3DVertexDeclaration9> _vertexDeclD[VDCount][VSDVCount];
  //! Non-TL vertex shader (shader for 2D drawing)
  ComRef<IDirect3DVertexShader9> _vsNonTL;
  //! Non-TL vertex declaration
  ComRef<IDirect3DVertexDeclaration9> _vdNonTL;
  
	ComRef<IDirect3DVertexShader9> _vsHeatHaze;						//! heat haze VS
	ComRef<IDirect3DVertexDeclaration9> _vdHeatHaze;			//! heat haze VD

	
	//!{ Shader pool (no-linking)
  //! Shader pool dimensions
  enum
  {
    SHP_VertexShaderID      = NVertexShaderPoolID,
    SHP_UVInput             = 2,
    SHP_MainLight           = 4,
    SHP_RenderingMode       = NRenderingMode,
    SHP_SkinningInstancing  = 3,
  };
  //! Shader pool n-dimension array
  ComRef<IDirect3DVertexShader9> _vertexShaderPool[SHP_VertexShaderID][SHP_UVInput][SHP_MainLight][SHP_RenderingMode][SHP_SkinningInstancing];
  //! Shadow volume shaders for normal, skinning and instancing objects
  ComRef<IDirect3DVertexShader9> _vertexShaderShadowVolume[SHP_SkinningInstancing];
  //! List of special vertex shaders (those shaders that exist in one version only)
  ComRef<IDirect3DVertexShader9> _vertexShaderSpecial[NVertexShaderSpecialID];
  //! Terrain shader in 3 variations
  ComRef<IDirect3DVertexShader9> _vertexShaderTerrain[SHP_RenderingMode];
  //! Terrain grass shader in 3 variations
  ComRef<IDirect3DVertexShader9> _vertexShaderTerrainGrass[SHP_RenderingMode];
  //!}

  /// modulation color - used for HDR buffer transformation
  Color _modColor;

  /// is pixel shader work done in linear space, or SRGB?
  bool _linearSpace;

  float _night;

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
protected:

  typedef AutoArray<Matrix4, MemAllocLocal<Matrix4, LimitBones, AllocAlign16> > BonesType;

#if _ENABLE_SKINNEDINSTANCING
  //! Bones with instancing
  AutoArray<BonesType> _bonesInstanced;
#endif

protected:

  bool CreateShaderDeclarations();
  void DestroyShaderDeclarations();

  bool CreatePostProcessStuff();

  void DestroyPostProcessStuff();

  // initialize all post process effects, typically called over device reset
  bool InitPostProcessStuff(bool firstInit);

  bool NewPostProcessStuff(bool firstInit);
  // release resources allocated by post process effects, called before device reset 
  void ReleasePostProcessStuff();

  float _nightEye;

  // members used for both DirectDraw/Direct3D

  //! Actual back buffer
  /*!
  We remember this back buffer at the start of each frame and set it again in the end.
  */
  ComRef<IDirect3DSurface9> _backBuffer;
  //! Depth buffer associated with _backBuffer
  ComRef<IDirect3DSurface9> _depthBuffer;
  //! Depth buffer associated with 3D render target - used for FSAA
  ComRef<IDirect3DSurface9> _depthBufferRT;

  //{! Render targets
  /*!
  We set these textures as render target at the start of each frame, use them for
  rendering and in the end we copy the data to the back buffer. We use destination (D)
  as a target and source (S) as a texture to render from. Each PP step switches the
  source and the destination.
  */
  ComRef<IDirect3DTexture9> _renderTargetD;
  ComRef<IDirect3DSurface9> _renderTargetSurfaceD;
  ComRef<IDirect3DTexture9> _renderTargetS;
  ComRef<IDirect3DSurface9> _renderTargetSurfaceS;

  ComRefFake<IDirect3DTexture9> _currRenderTarget;
  ComRefFake<IDirect3DSurface9> _currRenderTargetSurface;
  ComRefFake<IDirect3DSurface9> _currRenderTargetDepthBufferSurface;
  //@}

  //!{ Render target containing depth informations, filled out in z-prime step
  //! Texture containing the render target data (no multisampling used)
  ComRef<IDirect3DTexture9> _renderTargetDepthInfo;
  //! Surface will share the space with the texture
  ComRef<IDirect3DSurface9> _renderTargetDepthInfoSurface;
  //! Texture containing the render target data Z (no multisampling used)
  ComRef<IDirect3DSurface9> _renderTargetDepthInfoZ;
  //!}

  //////////////////////////////////////////////////////////////////////////

#if _ARMA3_SIMUL_WEATHER

  enum SW_Layers {
    MinSWLayers = 0,
    DefaultSWLayers = 64,
    MaxSWLayers = 128
  };

  int _swLayersCount;

#endif

#if _HELISIM_RENDER_TO_TEXTURE

  int _r2tQuality;

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
    ComRef<IDirect3DTexture9> _rtApertureR2T[2];
    ComRef<IDirect3DSurface9> _rtsApertureR2T[2];
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

    bool Initialize(EngineDD9 *engine);
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

    bool Initialize(EngineDD9 *engine);
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
    bool Initialize(EngineDD9 *engine);
    void Update(const float *pars, int count);
  };

  struct R2TRT
  {
    // Non FSAA render target
    ComRef<IDirect3DTexture9> _renderTargetS;
    // FSAA render target
    ComRef<IDirect3DSurface9> _renderTargetSurfaceS;
    //! Texture containing the render target data (no multisampling used)
    ComRef<IDirect3DTexture9> _renderTargetDepthInfo;
    //! Surface will share the space with the texture
    ComRef<IDirect3DSurface9> _renderTargetDepthInfoSurface;

    //! Render target copy
    ComRef<IDirect3DTexture9> _rtCopy;
    ComRef<IDirect3DSurface9> _rtCopySurface;

    //!{ Setting and restoring of temporary render target
    ComRefFake<IDirect3DTexture9> _renderTargetSaved;
    ComRefFake<IDirect3DSurface9> _renderTargetSurfaceSaved;
    ComRefFake<IDirect3DSurface9> _renderTargetDepthBufferSurfaceSaved;

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
    ComRef<IDirect3DTexture9> _texture;
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
    // texture surface
    ComRef<IDirect3DSurface9> _surface;
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
      _surface.Free();
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
  void SaveR2TToFile(IDirect3DSurface9 *surfToSave) const;

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
  virtual void TrackR2TTexUsage(IDirect3DTexture9 *texture);
  //! Create render target copy
  ComRef<IDirect3DTexture9> CreateR2TRenderTargetCopyAsTexture(IDirect3DDevice9 *device, const RECT *rect, D3DTEXTUREFILTERTYPE filter); 
  //! Create depth buffer copy - used as texture for sprites etc.
  void CreateR2TDepthAsTexture();
  //! Inform about r2t targets invalidation - sync with r2t cams
  virtual bool R2TCamsSyncNeeded();

public:
  virtual void InitR2TDraw();
  //! Add texture to render target textures list
  void RegisterR2TSourceTexture(IDirect3DTexture9 *texture, int w, int h, RString name);
  //! Remove texture from render target textures list
  void UnRegisterR2TSourceTexture(IDirect3DTexture9 *texture);
  //! Bind camera (specified by name) to render target
  virtual RenderTargetID BindR2T(const RString &name);
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
  virtual void SetPipQuality(int quality) { _r2tQuality = quality; }
  //!
  virtual int GetPipQuality() const { return _r2tQuality; }
  //!
  virtual bool IsR2TEnabled() const { return _r2tQuality != 0; }
protected:

#endif

  //////////////////////////////////////////////////////////////////////////
  //! Image-based, select post-processing antialiasing technique
  virtual void SetPPAAQuality(int quality);
  //! Get selected post-processing antialiasing technique
  virtual int GetPPAAQuality() const;

  /// get current render target for sampling
  IDirect3DTexture9 *GetCurrRenderTargetAsTexture();

  /// get depth target (used for soft sprites and depth of field effects)
  IDirect3DTexture9 *GetDepthAsTexture() const;

  /// we need to render to the backbuffer
  void SwitchToBackBufferRenderTarget();

  /// make sure we have a copy of the current render target
  IDirect3DTexture9 *CreateRenderTargetCopyAsTexture();

  //!{ Setting and restoring of temporary render target
  ComRefFake<IDirect3DTexture9> _renderTargetSaved;
  ComRefFake<IDirect3DSurface9> _renderTargetSurfaceSaved;
  ComRefFake<IDirect3DSurface9> _renderTargetDepthBufferSurfaceSaved;
  float _minZRangeSaved;
  float _maxZRangeSaved;
  void SetTemporaryRenderTarget(IDirect3DTexture9 *rt, IDirect3DSurface9 * rts, IDirect3DSurface9 *dts);
  void RestoreOldRenderTarget();
  //!}

  enum PPLevel : char
  {
    PPLForce = 1, PPLVeryLow = 2, PPLLow = 4, PPLNormal = 8, PPLHigh = 16, PPLVeryHigh = 32
  };
  /// Post processing stuff
  struct PPs
  {
    Ref<PPGaussianBlur> _gaussianBlur;
    Ref<PPBloom> _bloom;
    Ref<PPSSSM> _SSSM;
    Ref<PPSSSMStencil> _SSSMStencil;
#ifdef SSSMBLUR
    Ref<PPSSSMB> _SSSMB;
#endif
    Ref<PPDOF> _DOF;
    Ref<PPDistanceDOF> _distantDOF;
    Ref<PPFilmGrain> _filmGrain;
		Ref<PPGodRays> _godRays;
		Ref<PPRain> _rain;
		Ref<PPEnfShapeCollectionRes> _enfShapeCollectionRes;
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
    Ref<PPFXAA> _fxaa;
    Ref<PPSMAA> _smaa;

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
    // are we rendering 2D or 3D?
    TLMode _tlActive;
    //@{ shadowed shader constants
    // we are not shadowing whole range, only a part of it, where equality is likely.
    
    /** char so that we can represent impossible value */ 
    char _shaderConstValuesB[8];  // 16 possible with SM 3
    float _shaderConstValues[FREESPACE_START_REGISTER][4]; // 256 possible with SM 3
    int _shaderConstValuesI[4][4]; // 16 possible with SM 3
    float _pixelShaderConstValues[64][4]; // 64 constants are enough for us (224 possible with SM 3)
    int _pixelShaderConstValuesI[4][4];
    //@}
    //@{ shadowed D3D rendering state
    AutoArray<SamplerStateInfo> _samplerState[MaxSamplerStages];
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
    IDirect3DBaseTexture9 *_lastHandle[MaxSamplerStages];
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
    // ComRefFake<IDirect3DVertexBuffer9> _vBufferLast; 
    VertexStaticData9::BufferFuture _vBufferLast; // TODO: do we need a different RefCount handling? (FutureFake)
    int _strideLast;
    
    //@{ last SetIndices
    // ComRefFake<IDirect3DIndexBuffer9> _iBufferLast;
    IndexStaticData9::BufferFuture _iBufferLast; // TODO: do we need a different RefCount handling? (FutureFake)
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
    ComRefFake<IDirect3DPixelShader9> _pixelShaderLast[NPredicationModes];
    //! New vertex declaration
    ComRefFake<IDirect3DVertexDeclaration9> _vertexDecl; 
    //! Current vertex declaration
    ComRefFake<IDirect3DVertexDeclaration9> _vertexDeclLast; 
    //! New vertex shader ID
    VertexShaderID _vertexShaderID; 

    //! Vertex shader that was used in the previous draw call
    /** may be different for each predicated branch */
    ComRefFake<IDirect3DVertexShader9> _vertexShaderLast[NPredicationModes];

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

  #if LOG_RESULTS_ALLOC
  Journal _memJournal;
  void MemJournalAdd(const char *str)
  {
    _memJournal.Add(str);
  }
  #endif

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
      /// check the buffer was copied
      bool _copied;
    #endif
    
    RenderCBToPlay()
    {
      _noDestroy=false;
      #if PIX_NAMED_CB_SCOPES
      _debugId=0;
      _copied = false;
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
      if (_size>0) {LOG_MEM("%s (%d): Dealloc %d",cc_cast(_scopeName),_debugId,_size);}
      #endif
    }
    
    void LogResultsAlloc(const char *pixScope, int dtaSize)
    {
      #if PIX_NAMED_CB_SCOPES
      if (dtaSize>0) {LOG_MEM("%s (%d): Allocate %d",pixScope,_debugId,dtaSize);}
      #endif
    }

    void LogResultsDestroy()
    {
      #if PIX_NAMED_CB_SCOPES
      if (_size>0) {LOG_MEM("%s (%d): Dealloc %d",cc_cast(_scopeName),_debugId,_size);}
      if (_memToClear>0) {LOG_MEM("%s (%d): Clear %d",cc_cast(_scopeName),_debugId,_memToClear);}
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
    void FinishSubmit(EngineDD9 *engine, SourceCBType &bgData, const char *pixScope)
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

  #if SAFE_RESOURCE_UNSET
  /// D3D resources to release
  AutoArray<IDirect3DResource9 *, MemAllocLocal<IDirect3DResource9 *,64> > _bgResToRelease;
  /// textures to release
  AutoArray<TextureHandle9 *, MemAllocLocal<TextureHandle9 *,64> > _bgTexHandleToRelease;
  #endif

  RefArray<VertexBuffer> _vBufferToRelease;

  /// one queue for each of visualize/render threads */
  //StoredRefContainer &_bgRefToRelease;

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

	/*
	original value was 8*1024 but some tests map with a lot of loot had problem with space
	*/
  static const int MaxBGItems = 12*1024;
  
  typedef WorkerThread<RenderCBToPlay,MaxBGItems,NBackgroundConsumers> BGWorkerThread;
  
  class VisWorkerThreadImpl: public ActiveObject<VisualServant>, public SignaledObject
  {
    Event _terminate;
    
    
    public:
    
    DWORD _threadId;
    
    VisWorkerThreadImpl()
    :SignaledObject(NoInitHandle)
    {
		_threadId = 0;
	}
    
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

  public:  
  enum {IAmMainThread,IAmVisualizeThread};
  
  protected:
  
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
  
  int CBStateCount() const {return _cbState.Size();}
  /// count of real CBs, ignoring immediate state
  int CBCount() const {return _cbState.Size()-1;}
  
  /// stored undefined state (TODO: could be shared accross frames?)
  RendState _undefined;

  
  /// possible number of CB threads - set by StartCBScope
  /**
  -1 outside of the CB scope,
  0 in a CB scope, but not recording any CBs
  >0 number of concurrent CBs recorded
  */
  int _cbCount;
  
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
  
  RenderMode _renderMode; // EXCL
  Queue9 _queueNo; // EXCL

  float _gamma;

  //@{ corresponds to TextBank value,
  /** needed here because it can be set before TextBank is created */
  int _textureQuality;
  int _textureMemory;
  //@}

  /// FinishFrame is necessary
  /** was InitDraw really done? - to be called from the thread owning the low-level state */
  
  bool _d3dFrameOpen; 

  /// is FinishFrame necessary?
  /** was InitDraw called? - differs from _d3dFrameOpen when performing HighLevel recording */
  bool _d3dFrameOpenRequested;

private:
  void ResetShaderConstantValues(int cb);


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

  PredicationMode CurrentPredicationMode() const
  {
    if (_renderingMode==RMDPrime)
    {
      if (GetFSAA()>0) return DepthPass;
      return DepthPass;
    }
    if (_renderingMode==RMZPrime) return PrimingPass;
    Assert(_renderingMode==RMCommon || _renderingMode==RMShadowBuffer);
    return RenderingPass;
  }
  //!{ Shadow buffer parameters
  /// did user enable the SB rendering?
  int _sbQualityWanted;
  /// what is the setting we have currently set in the engine?
  int _sbQuality;
  //! Flag to determine what SB technique can be used
  SBTechnique _sbTechnique;
  /// size of shadow buffer surface
  int _sbSize;

  //! Flag to determine whether bloom post-process will be used
  bool _doBloom;

  //!{ Render target - layer with index 0 is the roughest (largest) part of the cascade
  /**
  note: in spite of using multiple layers, the layers are combined in a loop
  only one shadow buffer surface is needed for all layers together
  */
  ComRef<IDirect3DTexture9> _renderTargetSB;
  ComRef<IDirect3DSurface9> _renderTargetSurfaceSB;
  ComRef<IDirect3DTexture9> _renderTargetDepthBufferSB;
  ComRef<IDirect3DSurface9> _renderTargetDepthBufferSurfaceSB;
  //!}
  //!{ Shadow plane parameters
  //! Matrix for conversion from transformed position to shadow map
  PASSMATRIXTYPE _shadowMapMatrixDX;
  //! Length in meters the shadow buffer value 1 represents (1 is the maximum in shadow buffer) - used for determining the shadow length
  float _maxDepth;
  //! SP rounded camera position SP orientation
	#ifdef TEST_RAIN_MAP
		Matrix4 _oldLRSP[ShadowBufferCascadeLayers + 1];
	#else
		Matrix4 _oldLRSP[ShadowBufferCascadeLayers];
	#endif
  //!}

	#ifdef TEST_RAIN_MAP
		ComRef<IDirect3DTexture9> _rtRainMap;
		ComRef<IDirect3DSurface9> _rtRainMapSurface;
		ComRef<IDirect3DTexture9> _rtRainMapDepth;
		ComRef<IDirect3DSurface9> _rtRainMapDepthSurface;

		struct RainMapParams
		{
			Vector3 _shadowPos;
			Vector3 _projTrans;
		} _rainMapParams;

		void DestroyRainMapResources();
		void CreateRainMapResources();

		bool IsRainMapEnabled() const;

		#ifdef NEW_SB_INIT
			struct SBBuffers
			{
				ComRef<IDirect3DTexture9> _rtColorBuffer;
				ComRef<IDirect3DSurface9> _rtColorBufferSurface;
				ComRef<IDirect3DTexture9> _rtDepthBuffer;
				ComRef<IDirect3DSurface9> _rtDepthBufferSurface;

				void DestroyResources();
			};

			SBBuffers _sbBuffer;
			SBBuffers _rainMapBuffer;
			SBFormat	_sbFormatDesc;

			bool SBTR_CheckFormat2(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat, D3DFORMAT depthFormat, D3DFORMAT rtFormat) const;
			SBFormat SBTR_FindFormat2(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat);
			void SBTR_CreateShadowMap2(const SBFormat& format, int sbSize, SBBuffers& buffers);
		#endif

	#endif

  // 
  ComRef<IDirect3DTexture9> _ssao;
  ComRef<IDirect3DSurface9> _ssaoSurface;

  //!{ Screen space shadow map
  ComRef<IDirect3DTexture9> _sssm;
  ComRef<IDirect3DSurface9> _sssmSurface;
#ifdef SSSMBLUR
  ComRef<IDirect3DTexture9> _sssmB; // Blurred version
  ComRef<IDirect3DSurface9> _sssmBSurface; // Blurred version
#endif
  //!}
  //!}

  //!{ Depth of field parameters
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
  //!}

  //!{ Thermal imaging parameters
  //! Tc table texture
  ComRef<IDirect3DTexture9> _tableTc;
  //! Tc table texture
  ComRef<IDirect3DTexture9> _tableTcSystem;
  // Table of current temperatures 
  AutoArray<float> _temperatureTable;
  //! Temperature of the air
  float _airTemperature;
  //! Name of the color conversion texture (so that we could load it after reset)
  RString _tiConversionName;
  //! Table used for color conversion
  ComRef<IDirect3DTexture9> _tiConversion;
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

#if _VBS3
  //! Tunnel vision coefficient
  float _tunnelVisionCoef;

  //!{ Flashbang effect
  bool _fbGetScreen;          //! Flag to determine the first call of PPFlashBang should take the screen capture (the effect have been just invoked)
  float _fbCurrentIntensity;  //! Coefficient to determine the current state of the effect (1 - effect was just invoked, 0 - the effect passed away)
  float _fbInvDuration;       //! Inverse of the time in seconds of how long the effect is going to last
  //!}

#endif

  //! Flag to determine we render the 3D part of the scene
  bool _3DRenderingInProgress;
  
  /// was End3DRendering requested from the main thread?
  bool _end3DRequested;

  /// mouse cursor currently in use
  /** NULL = cursor disabled */
  LLink<TextureD3D9> _cursorTex;
  /// color of current mouse cursor
  PackedColor _cursorColor;
  /// shadow under cursor
  int _cursorShadow;
  /// we may need to perform format conversion
  ComRef<IDirect3DSurface9> _cursorSurface;
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

  //! Uses _world and _invWorld matrices to transform specified position to object-space
  Vector3 TransformPositionToObjectSpace(int cb, Vector3Par position);
  //! Uses _world and _invWorld matrices to transform and normalize specified position to object-space
  Vector3 TransformAndNormalizePositionToObjectSpace(int cb, Vector3Par position);
  //! Transforms a vector to object space
  Vector3 TransformAndNormalizeVectorToObjectSpace(int cb, Vector3Par vector);
  /// interface for tree shader
  const Vector3 *GetTreeCrown(int cb) const {return CBState(cb)._treeCrown;}

  void SetFiltering(int cb, int stage, TexFilter filter, int maxAFLevel);
  void SetMultiTexturing(int cb, TextureD3D9 **tex, int count, int keepUntouchedLimit = TEXID_FIRST);
  void SetTextureAndMaterial( int cb, const TextureD3D9 *tex, const TexMaterial *mat, int specFlags, int filterFlags, DefTIMode defTI );

  void SetTexture0Color(int cb, ColorVal maxColor, ColorVal avgColor, bool optimize);
  void SetTexture0(int cb, const TextureD3D9 *tex);

  //!{ Wrappers of DX9 functions, some of them lazy
public:
  HRESULT Lock(const ComRef<IDirect3DVertexBuffer9> &buf, UINT offsetToLock, UINT sizeToLock, void **ppbData, DWORD flags);
  HRESULT Lock(const ComRef<IDirect3DIndexBuffer9> &buf, UINT offsetToLock, UINT sizeToLock, void **ppbData, DWORD flags);

  RString GetTextureDebugName(IDirect3DBaseTexture9 *tex) const;
  RString GetSurfaceDebugName(IDirect3DSurface9 *surf) const;
  
  RString GetPSConstantFDebugName(int index) const;
  RString GetPSConstantIDebugName(int index) const;
  RString GetPSConstantBDebugName(int index) const;

  RString GetVSConstantFDebugName(int index) const;
  RString GetVSConstantIDebugName(int index) const;
  RString GetVSConstantBDebugName(int index) const;

  
private:
  void SetTextureHandle(int cb, int stage, TextureHandle9 *tex, bool optimize = true);
  void SetTexture(int cb, int stage, IDirect3DBaseTexture9 *tex, bool optimize = true);
  void SetStreamSource(int cb, const VertexStaticData9::BufferFuture &buf, int stride, int bufSize, bool optimize = true);
  void SetVertexDeclaration(int cb, IDirect3DVertexDeclaration9 *decl, bool optimize = true);
  void SetVertexShader(int cb, IDirect3DVertexShader9 *vs, bool optimize = true);

  bool CheckVSNeedsToBeSet(IDirect3DVertexShader9 *shader, RendStateExt &cbState, int mask)
  {
    bool diff = false;
    for (int i=0; i<NPredicationModes; i++)
    {
      if ((mask&(1<<i)) && shader!=cbState._vertexShaderLast[i])
      {
        cbState._vertexShaderLast[i] = shader;
        diff = true;
      }
    }
    return diff;
  }
  bool CheckPSNeedsToBeSet(IDirect3DPixelShader9 *shader, RendStateExt &cbState, int mask)
  {
    bool diff = false;
    for (int i=0; i<NPredicationModes; i++)
    {
      if ((mask&(1<<i)) && shader!=cbState._pixelShaderLast[i])
      {
        cbState._pixelShaderLast[i] = shader;
        diff = true;
      }
    }
    return diff;
  }
  bool CheckRSNeedsToBeSet(RendStateExt &cbState, int index, DWORD value, int mask)
  {
    bool diff = false;
    for (int i=0; i<NPredicationModes; i++)
    {
      if ((mask&(1<<i)) && cbState._renderState[i][index]!=value)
      {
        cbState._renderState[i][index]=value;
        diff = true;
      }
    }
    return diff;
  }

  void SetPixelShader(int cb, IDirect3DPixelShader9 *ps, bool optimize = true);
  
  bool SetVertexShaderConstantF(int cb, int startRegister, const float *data, int vector4fCount, bool optimize = true);
  bool SetVertexShaderConstantI(int cb, int startRegister, const int *data, int vector4fCount, bool optimize = true);
  bool SetVertexShaderConstantB(int cb, int startRegister, const bool *data, int boolCount, bool optimize = true);
  
  bool SetPixelShaderConstantF(int cb, int startRegister, const float *data, int vector4fCount, bool optimize = true);
  bool SetPixelShaderConstantI(int cb, int startRegister, const int *data, int vector4fCount, bool optimize = true);
  bool SetPixelShaderConstantB(int cb, int startRegister, const BOOL *data, int count, bool optimize = true);

  void SetRenderTarget(IDirect3DSurface9 *rts, IDirect3DTexture9 *rt, IDirect3DSurface9 *dt, bool optimize = true);
  // Resolve is nop on PC - textures are render targets when needed
  void ResolveRenderTarget(){}
  void SetRenderTargetAndDepthStencil(
    IDirect3DSurface9 *rts, IDirect3DTexture9 *rt, IDirect3DSurface9 *dts,  
    bool optimize = true);
  void DrawPrimitive(int cb, D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount);
  //!}

  //!{ Wrappers that ensure filling the just created render targets and depth buffers
private:
  void FillOutRenderTarget(const ComRef<IDirect3DSurface9> &rt, const ComRef<IDirect3DSurface9> &db);
public:
  HRESULT CreateRenderTarget(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality, BOOL lockable, ComRef<IDirect3DSurface9> &surface, bool initialize, void* pSharedHandle);
  /// create a texture usable as a render target
  HRESULT CreateTextureRT(UINT width,UINT height, UINT levels, D3DFORMAT format, D3DPOOL pool, ComRef<IDirect3DTexture9> &texture, bool initialize);
  /// create a texture usable as a render target, obtain surface interface as well
  HRESULT CreateTextureRT(UINT width,UINT height, UINT levels, D3DFORMAT format, D3DPOOL pool, ComRef<IDirect3DTexture9> &texture, ComRef<IDirect3DSurface9> &rts, bool initialize);
  /// create a texture usable as a depth/stencil
  HRESULT CreateTextureDS(UINT width,UINT height, UINT levels, D3DFORMAT format, D3DPOOL pool, ComRef<IDirect3DTexture9> &texture, const ComRef<IDirect3DSurface9> &rt);
  /// create a depth/stencil render target
  HRESULT CreateDepthStencilSurface(UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE multiSample, DWORD multisampleQuality, BOOL discard, ComRef<IDirect3DSurface9> &surface, const ComRef<IDirect3DSurface9> &rt, void* pSharedHandle);
  //!}

public:
  // properties
  RString GetDebugName() const;
  int Width() const {return _wRT;}
  int Height() const {return _hRT;}
  int PixelSize() const {return _pixelSize;} // 16 or 32 bit mode?
  int RefreshRate() const {return _presentPars.FullScreen_RefreshRateInHz;}
  bool CanBeWindowed() const {return true;}
  bool IsWindowed() const {return _windowed;}

  int WidthBB() const {return _w;}
  int HeightBB() const {return _h;}

  int MinGuardX() const {return _minGuardX;}
  int MaxGuardX() const {return _maxGuardX;}
  int MinGuardY() const {return _minGuardY;}
  int MaxGuardY() const {return _maxGuardY;}

  int MinSatX() const {return _minGuardX;}
  int MaxSatX() const {return _maxGuardX;}
  int MinSatY() const {return _minGuardY;}
  int MaxSatY() const {return _maxGuardY;}

  void InitVRAMPixelFormat( D3DFORMAT &pf, PacFormat format, bool isSRGB );

  int MaxInstances() const {return _caps._maxInstances;}
  int MaxInstancesSprite() const {return _caps._maxInstancesSprite;}
  int MaxInstancesShadowVolume() const {return _caps._maxInstancesShadowVolume;}

	int GetFrameCount() const {return _resetFrame;}

protected:
  void WorkToBack();
  void BackToFront();

  bool CreateD3D();
  void DestroyD3D();
  void SearchMonitor(int &x, int &y, int &w, int &h);

public:
  // constructor - init all pointers to NULL

  void CreateSurfaces(bool windowed, ParamEntryPar cfg, ParamEntryPar fcfg);
  void DestroySurfaces(bool destroyTexBank = true);

public:
  EngineDD9();
  /*
  EngineDD9
  (
  HINSTANCE hInst, HINSTANCE hPrev, int sw,
  int x, int y, int width, int height, bool windowed, int bpp
  );
  */
  ~EngineDD9();
	
	bool IsDummy() { return false; }

  //! Virtual method
  virtual PushBuffer *CreateNewPushBuffer(int key);

  //! Initialization
  bool Init(HINSTANCE hInst, HINSTANCE hPrev, int sw, bool windowed, int bpp, bool generateShaders);
  //! Deinitialization
  void Done();
  /// virtual initialization done after GEngine is assigned
  virtual void Init();
  //! Destroying of all possible surfaces and buffers
  /*!
  \param hard Release system textures as well
  */
  void PreReset(bool hard);
  void PostReset(bool hard);
  /// can be used before time consuming engine switch (resolution switch...)
  void DisplayWaitScreen();
  bool Reset(); // use Reset to change setting / reset device
  bool ResetHard(); // destroy and create the device again

  void InitTexMemEstimations();

  //! Virtual method
  virtual void MaterialHasChanged(int cb) {CBState(cb)._lastMat = TexMaterialLODInfo((TexMaterial *)-1);}

  //! Method to set bones constants on the GPU
  void SetBones(int cb, int minBoneIndex, const Matrix4 *bones, int nBones);

  //! Virtual method
  virtual int GetMaxBonesCount() const {return _caps._maxBones;};
  //! Virtual method
  virtual void SetSkinningType(int cb, ESkinningType skinningType, const Matrix4 *bones = NULL, int nBones = 0);
#if _ENABLE_SKINNEDINSTANCING
  virtual void SetSkinningTypeInstance(int cb, ESkinningType skinningType, int instanceIndex, const Matrix4 *bones, int nBones);
#endif

  //! Return maximum intensity a star can have
  virtual float GetMaxStarIntensity() const;

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

  //! Enable SSAO post process effect
  void EnableSSAO(bool enable);
  bool IsEnabledSSAO() const;
  
  //! quality of ssao
  void SetSSAOQuality(int quality);

  //! Enable FilmGrain post process effect
  void EnableFilmGrain(bool enable);
	void EnableGodRays(bool enable);
	void EnableRain(bool enable);
  //! Enable radial blur post process effect
  void EnableRadialBlur(bool enable);
  //! Enable chromatic aberration post process effect
  void EnableChromAberration(bool enable);
  //! Enable wet distortion post process effect
  void EnableWetDistortion(bool enable);
  //! Enable color corrections post process effect
  void EnableColorCorrections(bool enable);
  //! Enable dynamic blur post process effect
  void EnableDynamicBlur(bool enable);
  //! Enable color inversion
  void EnableClrInvers(bool enable);

  //@{ start commit for post process
  void CommitRadBlur(float time);
  void CommitSSAO(float time);
  void CommitFilmGrain(float time);
	void CommitGodRays(float time);
	void CommitRain(float time);
  void CommitChromAberration(float time);
  void CommitWetDistortion(float time);
  void CommitColorCorrections(float time);
  void CommitDynamicBlur(float time);
  void CommitClrInvers(float time);
  //@}

  //@{ check state of post process
  bool CommittedRadBlur() const;
  bool CommittedSSAO() const;
  bool CommittedFilmGrain() const;
	bool CommittedGodRays() const;
	bool CommittedRain() const;
  bool CommittedChromAberration() const;
  bool CommittedWetDistortion() const;
  bool CommittedColorCorrections() const;
  bool CommittedDynamicBlur() const;
  bool CommittedClrInvers() const;
  //@}

	void ReloadGodRays() const; 
	void ReloadRain() const; 
	void ReloadSSAO() const; 
	void ReloadDOF() const; 
	void ReloadEnfShapeCollectionRes() const;
	void ReloadRotBlur() const;
	
	const PPEnfShapeCollectionRes* GetEnfShapeCollectionResources() const
	{
		return _pp._enfShapeCollectionRes;
	}

  //! Set radial blur post process parameters
  virtual void SetRadialBlurrParams(float pwrX, float pwrY, float offsX, float offsY);
  //! Set wet distortion params
  virtual void SetWetDistortionParams(float blur, float pwrX, float pwrY, float sh1, float sh2, float sv1, float sv2,
    float ah1, float ah2, float av1, float av2,float rndX, float rndY, float posX, float posY);
  //! Set chromatic aberration parameters
  virtual void SetChromaticAberrationParams(float aberationPowerX, float aberationPowerY, bool value);
  //! Set film grain parameters
  virtual void SetFilmGrainParams(const float *params, int nums);
	//! Set godrays parameters
	virtual void SetGodRaysParams(const float *params, int nums);
	virtual void SetGodraysKernel(Texture *tex);

	virtual void SetRainParams(const float *params, int nums);
	virtual void SetNewRainTexture(Texture *tex);

  //! Set SSAO parameters
  virtual void SetSSAOParams(const float *params, int nums);  
  //! Set color correction parameters
  virtual void SetColorCorrectionsParameters(float brightness, float contrast, float offset, Color blendClr, Color colClr, Color weights);
  //! Set dynamic blur parameters
  virtual void SetDynamicBlurParameters(float bluriness);
  //! Set color inversion parameters 
  virtual void SetClrInversParams(Color invColor);

  virtual void SetRainDensity(float density);
	virtual float GetRainRadius() const;
  //@}

  virtual void FlushBackgroundScope();
  virtual void PreparePostprocess();
  virtual void Postprocess(float deltaT, const PostFxSettings &pfx);
  virtual void CaptureRefractionsBackground();

  void SwitchRecordingToHighLevel();
  virtual void StopAll();

  void EndHighLevelRecording();
  bool EndHighLevelRecordingPart1();
  void EndHighLevelRecordingPart2(bool part1Result);
  
#if _ARMA3_SIMUL_WEATHER
  virtual SimulWeatherBase* CreateSimulWeather();
  void UpdateSimulWeatherTexture(TextureSysGPUDX9 *tex, unsigned textureIndex);

  virtual int GetSWGeometryLayersCount() const { return _swLayersCount; }
  virtual int GetSWGeometryDefault() const { return DefaultSWLayers; }
  virtual void SetSWGeometryLayersCount(int layers);
#endif

  struct D3DCommandBufRecWithSubmit 
  {
    D3DCommandBufRec &_ptr;
    
    D3DCommandBufRecWithSubmit(D3DCommandBufRec &ptr):_ptr(ptr){}
    D3DCommandBufRec *operator ->() const {return &_ptr;}
    ~D3DCommandBufRecWithSubmit()
    {
      GEngineDD->KickOffHighLevel();
    }
  };
  D3DCommandBufRecWithSubmit RecordHighlevel();
  
  void KickOffHighLevel();
  void KickOffCBDebug();
  
  /// body implementing Postprocess functionality
  /** used from Active Object servant as well */
  void DoPostprocess(float deltaT, const PostFxSettings &pfx);
  void DoNoPostprocess();
  void DoPresentBackbuffer();

  virtual void NoPostprocess();

  //! Define unique handle - used for post effect instance identification
  int GetPostEffectHandle() const;
  //! Find post process handle, if not found then return InvalidPPHndl handle
  PostProcessHndl& AccessPostprocess(int hndl);
  //! Check if effect priority can be used
  bool ValidatePPPriority(int priority) const;
  //! Create post effect on the basis of PostEffectType
  int CreatePostprocess(PostEffectType pFxType, int priority);
  int CreatePostprocess(RString peTypeName, int priority);

  Ref<EngineDD9::PostProcess> CreatePostprocess(PostEffectType pEType);
  //! Destroy post effect specified by handle
  void DestroyPostprocess(int hndl);
  //! Enable / disable post effect
  void EnablePostprocess(int hndl, bool enable);
  //! Set parameters
  int SetPostprocessParams(int hndl, AutoArray<float> &pars);
  int SetPostprocessParams(int hndl, const float* pars, int nPars);
  //! Commit post effect pars
  void CommitPostprocess(int hndl, float time);
  //! check state of post process
  bool CommittedPostprocess(int hndl);

  void ForEachPostEffectHndl(IPPHndlFunc &func);

  //! Virtual method
  virtual void CalculateFlareIntensity(int x, int y, int sizeX, int sizeY);
  
  void DoCalculateFlareIntensity(int x, int y, int sizeX, int sizeY);

  virtual AbilityToDraw IsAbleToDraw(bool wantReset);
  virtual bool IsAbleToDrawCheckOnly();
  
  bool ResetNeeded() const {return FAILED(_resetStatus);}
  //! Virtual method
  virtual void InitDraw( bool clear=false, Color color = HBlack ); // Begin scene

  void InitSceneInvariants(const int cb);
  
  void ResetLogDevice();

  virtual void InitNoDraw();
  virtual void FinishNoDraw(int wantedDurationVSync);
  virtual void FinishDraw(int wantedDurationVSync, bool allBackBuffers = false, RString fileName = RString());
  virtual void FinishPresent();
  virtual void FinishFinishPresent();
  virtual bool PresentIsAsync() {return _hlRecording;}

  bool IsMipLoadingAsync() const;

  void DoFinishRendering();
  bool InitDrawDone();
  void BackBufferToAllBuffers();
  void NextFrame(bool doPresent); // swap frames - get ready for next frame

  virtual float GetSmoothFrameDuration() const;
  virtual float GetSmoothFrameDurationMax() const;

  void Pause();
  void Restore();
  void SetLimitedViewport(float viewSize);

  void DoSwitchRes(bool reset3DSize=true);
  virtual bool SwitchRes(int x, int y, int w, int h, int bpp);

  virtual bool SwitchRefreshRate(int refresh);
  virtual bool SwitchWindowed(bool windowed, int w=0, int h=0, int bpp=0);
  virtual void KeepAspect(int &newW, int &newH);
  virtual void SetAspect(int newW, int newH);

  virtual void BeginOptionsVideo();
  virtual void EndOptionsVideo();
  virtual bool IsInOptionsVideo() const {return _inVideoOptions;}
  
  void AdjustWindowAttributes();
  
  void ListResolutions(FindArray<ResolutionInfo> &ret); // result is zero terminated w,h pairs of valid resolutions
  void ListRefreshRates(FindArray<int> &ret); // result is zero terminated w,h pairs of valid resolutions

  HDRPrec ValidateHDRPrecision(HDRPrec prec) const;

  virtual HDRPrec HDRPrecision() const {return _hdrPrecWanted;}
  virtual void SetHDRPrecision(HDRPrec hdrPrec);
  virtual int SupportedHDRPrecision() const {return _hdrPrecSupported;}

  // functions specific to Direct3D

  // functions for both DirectDraw/Direct3D
  void InitSurfaces(ParamEntryPar cfg, ParamEntryPar fcfg);
  void DoneSurfaces(bool destroyTexBank = true);

  void AddLocalVRAMAllocated(int size);
  void AddIBuffersAllocated(int n);
  void AddVBuffersAllocated(int n, bool dynamic);

  IDirectDraw7 *GetDirectDraw() const {return _ddraw;}
  IDirect3D9 *GetDirect3D() const {return _direct3D;}
  /// global device (cannot be used while recording CB - see StartCBRecording)
  MyD3DDevice &GetDirect3DDevice() const {return _d3DDevice;}
  /// device corresponding to a given command buffer
  D3DCommandBufRec &CB(int cb)
  {
    Assert( GetCurrentThreadId() != _bg._threadId );
    return _cbState[cb+1]._rec;
  }

  bool Can565() const {return _caps._can565;}
  bool Can88() const {return _caps._can88;}
  bool Can8888() const {return _caps._can8888;}
  int MaxTextureSize() const {return _caps._maxTextureSize;}

  int DXTSupport() const {return _caps._dxtFormats;}
  bool CanDXT( int i ) const {return (_caps._dxtFormats&(1<<i))!=0;}

  D3DDEVTYPE GetDevType() const {return _devType;}
  /// checks if given format is supported
  HRESULT CheckDeviceFormat(DWORD usage, D3DRESOURCETYPE type, D3DFORMAT format);
public:

  bool GetHWTL() const {return true;}

  void Clear( bool clearZ=true, bool clear=true, PackedColor color=PackedColor(0), bool clearStencil=true );

  virtual Ref<VertexBuffer> CreateVertexBuffer(const Shape &src, VBType type, bool frequent);
  virtual void RefreshBuffer(const VertexTable &sMesh);
  virtual void DeferVBufferRelease( VertexBuffer *buffer );

  void BufferReleased(VertexBufferD3D9 *buf);
  void OnSizeChanged(VertexBufferD3D9 *buf);
  virtual void ReleaseAllVertexBuffers();

  // access to _statVBuffer
  template <int vertexDecl>
  Ref<VertexStaticData9> AddShapeVertices(const Shape &src, VertexBufferD3D9 &vbuf, bool dynamic, bool separate, bool frequent);
  template <int vertexDecl>
  Ref<VertexStaticData9> AddShapeVerticesInstanced(const Shape &src, VertexBufferD3D9 &vbuf, bool dynamic, bool separate, bool frequent, int instancesCount);

  Ref<VertexStaticData9> AddShapeVerticesSpriteInstanced(const Shape &src, VertexBufferD3D9 &vbuf, bool dynamic, bool separate, bool frequent, int instancesCount);
  Ref<VertexStaticData9> AddShapeVerticesPoint(const Shape &src, VertexBufferD3D9 &vbuf, bool dynamic, bool separate, bool frequent);
#if _ENABLE_SKINNEDINSTANCING
  VertexStaticData9 *AddShapeVerticesSkinnedInstanced(const Shape &src, bool dynamic, bool separate, bool frequent, int instancesCount);
  VertexStaticData9 *AddShapeVerticesNormalSkinnedInstanced(const Shape &src, bool dynamic, bool separate, bool frequent, int instancesCount);
  VertexStaticData9 *AddShapeVerticesShadowVolumeSkinnedInstanced(const Shape &src, bool dynamic, bool separate, bool frequent, int instancesCount);
#endif

  Ref<IndexStaticData9> AddShapeIndices(const FaceArray &src, bool dynamic, int nInstances, int instanceVertices);
  Ref<IndexStaticData9> AddShapeIndicesSprite(const FaceArray &src, bool dynamic, int nInstances);
  Ref<IndexStaticData9> AddShapeIndicesPoint(const FaceArray &src, bool dynamic, int nInstances, int instanceVertices);
  void DeleteVB(VertexStaticData9 *vb);
  void DeleteIB(IndexStaticData9 *ib);

  bool CanCreateBuffersAsync() const;

  bool IsPermanent(IDirect3DVertexBuffer9 *b);
  bool IsPermanent(IDirect3DIndexBuffer9 *b);
  
  int FrameTime() const;
  int AFrameTime() const {return FrameTime();}

  void DoSetGamma();
  void SetGamma( float gamma );
  float GetGamma() const {return _gamma;}

  virtual RECT GetMonitorResolution();

  bool SetTextureQualityWanted(int value);
  virtual void SetTextureQuality(int value);
  virtual int GetTextureQuality() const {return _textureQuality;}
  virtual int GetMaxTextureQuality() const;

  bool SetTextureMemoryWanted(int value);
  virtual void SetTextureMemory(int value);
  virtual int GetTextureMemory() const {return _textureMemory;}
  virtual int GetMaxTextureMemory() const;
  virtual int GetTextureMemoryDefault() const;
  
  /// total VRAM (local+nonlocal) allocation limit used for static VRAM management
  int GetVRAMAllocLimit() const {return _caps._vramAllocLimit;}

  int GetLocalVramAmount() const {return _caps._localVramAmount;}

  bool GetDynamicVBNonLocal() const {return _caps._dynamicVBNonLocal;}

  /// get memory status (used in case of out of memory errors)
  const char *GetMemErrorStatus() const;

  int GetVBAlignment() const {return _caps._vbAlignment;}
  int GetTexAlignment() const {return _caps._texAlignment;}

  int AlignVRAMTex(int size) const {return (size+_caps._texAlignment-1)&~(_caps._texAlignment-1);}
  int AlignVRAMVB(int size) const {return (size+_caps._vbAlignment-1)&~(_caps._vbAlignment-1);}
  int AlignVRAMVBSigned(int size) const {return size>=0 ? AlignVRAMVB(size) : -AlignVRAMVB(-size);}

  int GetFreeLocalVramAmount() const {return _freeLocalVramAmount;}
  int GetFreeNonlocalVramAmount() const {return _freeNonlocalVramAmount;}

  int GetNonLocalVramAmount() const {return _caps._nonlocalVramAmount;}
  void SetVBuffersStatic(bool val){_caps._vbuffersStatic = val;}
  bool GetVBuffersStatic() const {return _caps._vbuffersStatic;}

  bool UseLocalPrediction() const {return _caps._localPrediction;}

  virtual void SetFSAA(int quality);
  virtual int GetFSAA() const;
  virtual int GetMaxFSAA() const;
  
  bool ZPrimed() const
  {
      // on PC we currently do not prime z buffer when using MSAA
      return true;
  }

  //@{ only set values, do not apply it yet
  bool SetFSAAWanted(int quality);
  bool SetHDRPrecisionWanted(HDRPrec prec);
  bool SetResolutionWanted(int w, int h, int bpp);
  bool SetPostprocessEffectsWanted(int quality);
  bool SetFillrateLevelWanted(int level);
  bool SetFillrateControlWanted(ResolutionInfo info);
  bool SetVerticalSynchronization(bool vsync);
  //@}

  void SetVSync(bool vsync);
  bool GetVSync() { return _vsync; };

  //@{ user settings - AToC, Bloom
  virtual void SetAToC(int level);
  virtual int GetAToC() const;

  virtual void SetBloom(float bloom) override;
  virtual float GetBloom() const override;
  virtual void SetRotBlurPower(float blurPow) override;
  virtual float GetRotBlurPower() const override;
  //@}

  
  /// apply values set by Wanted functions
  void ApplyWanted();

  /// for some changes pushbuffers need to be flushed
  virtual void FlushPushBuffers();

  virtual void SetAnisotropyQuality(int quality);
  virtual int GetAnisotropyQuality() const;

  virtual void SetPostprocessEffects(int quality);
  virtual int GetPostprocessEffects() const;

  int AnisotropyLevel(PixelShaderID ps, bool road, TexFilter filter) const;

  virtual float GetPreHDRBrightness() const;
  float GetPostHDRBrightness() const;

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
  void SetDefaultSettings();
  virtual void SetDefaultSettingsLevel(int level);

  void LoadConfig(ParamEntryPar cfg,ParamEntryPar fcfg, const char *section) override;
  void SaveConfig(ParamFile &cfg, const char *section) override;

  //HWND WindowHandle() const {return _hwndApp;}
  //HWND GetWindowHandle() const {return _hwndApp;}

  void FlushQueues();

  virtual void SetAlphaFog(int cb, float start, float end)
  {
    CBState(cb)._aFogStart=start,CBState(cb)._aFogEnd=end;
    CBState(cb)._applyAplhaFog = start<end;
  }

  virtual void GetZCoefs(int cb, float &zAdd, float &zMult);
  void SetBias( int cb, int bias );
  int GetBias(int cb){return CBState(cb)._bias;}

  //! This function name doesn't correspond to what it is actually doing. Check usage of this function (it is used to disable stencil shadows drawing)
  bool ZBiasExclusion() const
  {
#if _VBS3_SM2
    return !_caps._hasStencilBuffer || _caps._sm2Used;
#else
    return !_caps._hasStencilBuffer;
#endif
  }

  AbstractTextBank *TextBank();
  TextBankD3D9 *TextBankDD() const {return _textBank;}

  void CreateTextBank(ParamEntryPar cfg, ParamEntryPar fcfg);
  void DestroyTextBank();
  void ReportGRAM9(const char *name);

  virtual void Activate();
  virtual bool Deactivate();
  virtual void Screenshot(RString filename);
  void DoSaveSurfaceIntoFile(IDirect3DSurface9 *surface);

#if _VBS3_SCREENSHOT
  virtual void SaveScreenshot(RString fileName, RString fileType);
#endif
  //@{ video interface
  virtual VideoPlayback *VideoOpen(const char *path, float volume);
  virtual void VideoClose(VideoPlayback *video);
  virtual void VideoFrame(VideoPlayback *video);
  //@}

  #if _ENABLE_CHEATS
  //@{ debugging
  RString GetVSName(IDirect3DVertexShader9 *vs);
  RString GetVDName(IDirect3DVertexDeclaration9 *vd);
  RString GetPSName(IDirect3DPixelShader9 *ps);
  //@}
  #endif
  
protected: // D3D helpers


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

  void D3DSetSamplerState(int cb, DWORD stage, D3DSAMPLERSTATETYPE state, DWORD value, bool optimize=true);
  void D3DSetRenderState(int cb, D3DRENDERSTATETYPE state, DWORD value, bool optimize=true);

  void D3DEnableAlphaToCoverage(int cb, bool value, bool optimize=true);

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
  //! Finishing of PB drawing - invalidating of high-level lazy variables
  void PBDrawingCleanup(int cb);
  /// make sure our shadow state is consistent to what UnsetAll did
  void SimulateUnsetAll(int cb);
  
  /// initialize a device into a reasonable default state
  void InitDeviceState(int cb);

  /// initialize a device used for 3D scene rendering into a reasonable default state
  void InitDeviceState3D(int cb);
  
  //! Method to compile and create a vertex shader
  void CreateShader(LazyLoadFile &hlslSource, CONST D3DXMACRO *pDefines,
    LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags,
    ComRef<IDirect3DVertexShader9> &vertexShader);

  void CreateShader(RString filename, CONST D3DXMACRO *pDefines,
    LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags,
    ComRef<IDirect3DVertexShader9> &vertexShader);
    
  void CreateShader(LazyLoadFile &hlslSource, const Array<D3DXMACRO> &pDefines,
    LPCSTR pFunctionName, LPCSTR pProfile, DWORD flags,
    Future< ComRef<ID3DXBuffer> > &vertexShader);
  
  //@}
  
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
  void SetPixelShaderConstantDBSZero(int cb, float matPower);
  void SetPixelShaderConstantLayers(int cb, int layerMask, const TexMaterial *mat);
	void SetPixelShaderConstantHeatHaze(int cb, Texture* tex);
	void SetPixelShaderConstantWater(int cb);
  void SetPixelShaderConstantWaterSimple(int cb);
  void SetPixelShaderConstantSuper(int cb, ColorVal matSpecular);
  void SetPixelShaderConstantSkin( int cb, ColorVal matAmbient, ColorVal matDiffuse, ColorVal matForcedDiffuse, ColorVal matEmissive, ColorVal matSpecular );
  void SetPixelShaderConstantTreeAdv( int cb, ColorVal matAmbient, ColorVal matDiffuse, ColorVal matForcedDiffuse, ColorVal matEmissive, ColorVal matSpecular );
  void SetPixelShaderConstantTreePRT(int cb, ColorVal matEmmisive, ColorVal matAmbient, ColorVal matDiffuse);
  void DrawIndexedPrimitive(int cb, int beg, int end, const Shape &sMesh, int instancesOffset, int instancesCount);
  void SetTextureR(int cb, DWORD stage, const TextureD3D9 *texture);
  void SetPixelShaderConstantUI(int cb, Vector3Val val, float intensity);
  void SetPixelShaderNightEmissiveConst(int cb, ColorVal matEmissive, bool useNightCoef);
  //@}
  
  // Method to forget buffers used by the device (function is used prior removing or locking buffers)
  void ForgetDeviceBuffers(int cb);

#if _ARMA3_SIMUL_WEATHER
  void DoUpdateSimulWeatherTexture(TextureSysGPUDX9 *tex, unsigned textureIndex);
#endif

  //! Temporary setting of a render state
  /*!
  The state will be returned to it's previous value as soon as this object is destroyed
  */
  class SetRenderStateTemp
  {
  private:
    //! Engine
    static EngineDD9 *_engine;
    //! State we set and then set back
    D3DRENDERSTATETYPE _state;
    //! Old value to set back
    DWORD _oldValue;
  public:
    //! Constructor - remember the old state and value, set the new one
    SetRenderStateTemp(D3DRENDERSTATETYPE state, DWORD value)
    {
      const int cb = -1;
      _state = state;
      int index = state;
      // we should not be inside of a predicated region
      EngineDD9::RendStateExt &cbState = _engine->CBState(cb);
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
    //! Destructor - restore the old state
    ~SetRenderStateTemp()
    {
      // If the state was not previously set, keep the current value
#ifndef _X360SHADERGEN
      if (_oldValue == -1)
        return;
#endif

      _engine->D3DSetRenderState(-1, _state, _oldValue);
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD9 *engine) {_engine = engine;}
  };

  //! Temporary setting of a sampler state
  /*!
  The state will be returned to it's previous value as soon as this object is destroyed
  */
  class SetSamplerStateTemp
  {
  private:
    //! Engine
    static EngineDD9 *_engine;
    //! Stage we set and then set back
    int _stage;
    //! State we set and then set back
    D3DSAMPLERSTATETYPE _state;
    //! Old value to set back
    DWORD _oldValue;
  public:
    //! Constructor - remember the old state and value, set the new one
    SetSamplerStateTemp(int stage, D3DSAMPLERSTATETYPE state, DWORD value)
    {
      const int cb = -1;
      _state = state;
      int index = state;

		_engine->CBState(cb)._samplerState[stage].Access(index);
      RenderStateInfo &info = _engine->CBState(cb)._samplerState[stage][index];
      _oldValue = info;
      _stage = stage;
      _engine->D3DSetSamplerState(cb, stage, state, value);
    }
    //! Destructor - restore the old state
    ~SetSamplerStateTemp()
    {
      // If the state was not previously set, keep the current value
      if (_oldValue == -1)
        return;

      _engine->D3DSetSamplerState(-1, _stage, _state, _oldValue);
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD9 *engine) {_engine = engine;}
  };


  //! Scope "protection" of a vertex shader constant
  /*!
  The value will be returned to it's previous value as soon as this object is destroyed
  */

	//small helper to next template children
	class PushInternall
	{
	public:
		static EngineDD9 *_engine;
		static void SetEngine(EngineDD9 *engine) {_engine = engine;}
	};

	class SetPixelShaderTemp: private ::NoCopy, private PushInternall
	{
	protected:
		ComRefFake<IDirect3DPixelShader9> _pixelShaderLast;
	public:
		SetPixelShaderTemp(IDirect3DPixelShader9* ps)	
		{
			_pixelShaderLast = _engine->CBState(-1)._pixelShaderLast[_engine->CurrentPredicationMode()];
			_engine->SetPixelShader(-1, ps);
		}
		~SetPixelShaderTemp()
		{
			_engine->SetPixelShader(-1, _pixelShaderLast);
		}
	};

	class SetVertexShaderTemp: private ::NoCopy, private PushInternall
	{
	protected:
		ComRefFake<IDirect3DVertexShader9> _vertexShaderLast;
	public:
		SetVertexShaderTemp(IDirect3DVertexShader9* vs)	
		{
			_vertexShaderLast = _engine->CBState(-1)._vertexShaderLast[_engine->CurrentPredicationMode()];
			_engine->SetVertexShader(-1, vs);
		}
		~SetVertexShaderTemp()
		{
			_engine->SetVertexShader(-1, _vertexShaderLast);
		}
	};


	template<int REGS> class PushVertexShaderConstantAll: private ::NoCopy, private PushInternall
	{
	protected:
		//! State we set and then set back
		int _registerIndex;
		//! Old value to set back
		float _oldValue[REGS][4];
	public:
		//! Constructor - remember the old state and value, set the new one
		PushVertexShaderConstantAll(int registerIndex)	
		{
			Assert(registerIndex + REGS < FREESPACE_START_REGISTER)

			memcpy (_oldValue, &(_engine->CBState(-1)._shaderConstValues[registerIndex][0]), sizeof(float)*REGS*4);
			_registerIndex = registerIndex;
		}
		//! Destructor - restore the old state
		~PushVertexShaderConstantAll()
		{
			for (int i = 0; i < REGS; i++)
			{
				if(_oldValue[i][0]==FLT_MAX && _oldValue[i][1]==FLT_MAX && _oldValue[i][2]==FLT_MAX && _oldValue[i][3]==FLT_MAX) 
					continue;
				_engine->SetVertexShaderConstantF(-1, _registerIndex + i, _oldValue[i], 1);
			}
		}
	};


  class PushVertexShaderConstant: private ::NoCopy
  {
  protected:
    //! Engine
    static EngineDD9 *_engine;
    //! State we set and then set back
    int _registerIndex;
    //! Old value to set back
    float _oldValue[4];
  public:
    //! Constructor - remember the old state and value, set the new one
    PushVertexShaderConstant(int registerIndex)
    {
      const int cb = -1;
      _oldValue[0] = _engine->CBState(cb)._shaderConstValues[registerIndex][0];
      _oldValue[1] = _engine->CBState(cb)._shaderConstValues[registerIndex][1];
      _oldValue[2] = _engine->CBState(cb)._shaderConstValues[registerIndex][2];
      _oldValue[3] = _engine->CBState(cb)._shaderConstValues[registerIndex][3];
      _registerIndex = registerIndex;
    }
    //! Destructor - restore the old state
    ~PushVertexShaderConstant()
    {
      if(_oldValue[0]==FLT_MAX && _oldValue[1]==FLT_MAX && _oldValue[2]==FLT_MAX && _oldValue[3]==FLT_MAX) return;
      _engine->SetVertexShaderConstantF(-1,_registerIndex, _oldValue, 1);
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD9 *engine) {_engine = engine;}
  };

  //! Temporary setting of a vertex shader constant
  /*!
  The value will be returned to it's previous value as soon as this object is destroyed
  */
  class SetVertexShaderConstantTemp: public PushVertexShaderConstant
  {
  public:
    //! Constructor - remember the old state and value, set the new one
    SetVertexShaderConstantTemp(int registerIndex, const float *value)
      :PushVertexShaderConstant(registerIndex)
    {
      _engine->SetVertexShaderConstantF(-1,registerIndex, value, 1);
    }
  };

  //! Temporary setting of a pixel shader constant
  /*!
  The value will be returned to it's previous value as soon as this object is destroyed
  */
	template<int REGS> class PushPixelShaderConstantAll: private ::NoCopy, public PushInternall
	{
	protected:
		//! State we set and then set back
		int _registerIndex;
		//! Old value to set back
		float _oldValue[REGS][4];
	public:
		//! Constructor - remember the old state and value, set the new one
		PushPixelShaderConstantAll(int registerIndex)
		{
			Assert(registerIndex + REGS < 64);
			
			memcpy (_oldValue, &(_engine->CBState(-1)._pixelShaderConstValues[registerIndex][0]), sizeof(float)*REGS*4);
			_registerIndex = registerIndex;
		}
		//! Destructor - restore the old state
		~PushPixelShaderConstantAll()
		{
			const int cb = -1;
			for (int i = 0; i < REGS; i++)
			{
				if(_oldValue[i][0]==FLT_MAX && _oldValue[i][1]==FLT_MAX && _oldValue[i][2]==FLT_MAX && _oldValue[i][3]==FLT_MAX) 
					continue;
				_engine->SetPixelShaderConstantF(cb,_registerIndex + i, _oldValue[i], 1);
			}
		}
	};

  class PushPixelShaderConstant: private ::NoCopy
  {
  protected:
    //! Engine
    static EngineDD9 *_engine;
    //! State we set and then set back
    int _registerIndex;
    //! Old value to set back
    float _oldValue[4];
  public:
    //! Constructor - remember the old state and value, set the new one
    PushPixelShaderConstant(int registerIndex)
    {
      const int cb = -1;
      _oldValue[0] = _engine->CBState(cb)._pixelShaderConstValues[registerIndex][0];
      _oldValue[1] = _engine->CBState(cb)._pixelShaderConstValues[registerIndex][1];
      _oldValue[2] = _engine->CBState(cb)._pixelShaderConstValues[registerIndex][2];
      _oldValue[3] = _engine->CBState(cb)._pixelShaderConstValues[registerIndex][3];
      _registerIndex = registerIndex;
    }
    //! Destructor - restore the old state
    ~PushPixelShaderConstant()
    {
      const int cb = -1;
      if(_oldValue[0]==FLT_MAX && _oldValue[1]==FLT_MAX && _oldValue[2]==FLT_MAX && _oldValue[3]==FLT_MAX) return;
      _engine->SetPixelShaderConstantF(cb,_registerIndex, _oldValue, 1);
    }
    //! Setting of the engine pointer
    static void SetEngine(EngineDD9 *engine) {_engine = engine;}
  };

  //! Temporary setting of a pixel shader constant
  /*!
  The value will be returned to it's previous value as soon as this object is destroyed
  */
  class SetPixelShaderConstantTemp: public PushPixelShaderConstant
  {
  public:
    //! Constructor - remember the old state and value, set the new one
    SetPixelShaderConstantTemp(int registerIndex, const float *value)
      :PushPixelShaderConstant(registerIndex)
    {
      _engine->SetPixelShaderConstantF(-1,registerIndex, value, 1);
    }
  };

	  //! push setting of a texture
  /*!
  The value will be returned to it's previous value as soon as this object is destroyed
  */
	class PushTextureState
	{
	protected:
		//! Engine
		static EngineDD9 *_engine;
		//! Stage we set and then set back
		int _stage;
		//! Old texture to set back
		IDirect3DBaseTexture9 *_oldTex;

	public:
		//! Constructor - remember the old state and value, set the new one
		PushTextureState(int stage)
		{
			_oldTex = _engine->CBState(-1)._lastHandle[stage];
			_stage = stage;
		}
		//! Destructor - restore the old state
		~PushTextureState()
		{
			_engine->SetTexture(-1, _stage, _oldTex);
		}
		//! Setting of the engine pointer
		static void SetEngine(EngineDD9 *engine) {_engine = engine;}

	};

  //! Temporary setting of a texture
  /*!
  The value will be returned to it's previous value as soon as this object is destroyed
  */
  class SetTextureTemp: public PushTextureState
  {
  public:
    //! Constructor - remember the old state and value, set the new one
    SetTextureTemp(int stage, IDirect3DTexture9 *tex): PushTextureState(stage)
    {
      _engine->SetTexture(-1, stage, tex);
    }
  };

  WORD *QueueAdd( int n );
  void QueueFan( const VertexIndex *ii, int n );

  void Queue2DPoly( const TLVertex *v, int n );

  void FlushQueue();

  void FlushAndFreeQueue();

  void AllocateQueue(TextureD3D9 *tex, int level, const TexMaterial *mat, int spec);
  void FreeQueue();
  void FreeAllQueues();
  void FlushAndFreeAllQueues();
  void FlushAllQueues();

  void CloseAllQueues();

  //void FlushQueue(){FlushQueue(_queue,index;
  //void Flush2DQueue();

  void SwitchRenderMode( RenderMode mode )
  {
    if (_renderMode==mode) return;
    DoSwitchRenderMode(mode);
  }
  void DoSwitchRenderMode( RenderMode mode );

  void DrawLine( const TLVertexTable &mesh, int beg, int end, int orFlags=0 );

  void DoDrawPolyPrepare(const MipInfo &mip, int specFlags);
  void DoDrawPolyDo(const Vertex2DPixel *vertices, int nVertices, const Rect2DPixel &clip);
  void DoDrawPolyDo(const Vertex2DAbs *vertices, int nVertices, const Rect2DAbs &clip);

  void DrawPolyPrepare(const MipInfo &mip, int specFlags=DefSpecFlags2D);
  void DrawPolyDo(const Vertex2DPixel *vertices, int nVertices, const Rect2DPixel &clip=Rect2DClipPixel);
  void DrawPolyDo(const Vertex2DAbs *vertices, int nVertices, const Rect2DAbs &clip=Rect2DClipAbs);

  virtual void DrawPoly3D(const PositionRender &space, const MipInfo &mip, const Vertex2DFloat *vertices, int nVertices, const Rect2DFloat &clip, int specFlags, const TLMaterial &matType = TLMaterial::_default, const Pars3D *pars3D=NULL);

  void QueuePrepareTriangle( const MipInfo &absMip, const TexMaterial *mat, int specFlags );
  void DoPrepareTriangle(int cb, TextureD3D9 *tex, const TexMaterialLODInfo &mat, int level, int spec, const EngineShapeProperties &prop);

  void SetupMaterialTextures(const TexMaterialLODInfo &mat, int cb, const EngineShapeProperties & prop, int specFlags);
  
  void PrepareTriangle( int cb, const MipInfo &absMip, const TexMaterial *mat, int specFlags );
  virtual void PrepareTriangleTL(
    int cb, const PolyProperties *section,
    const MipInfo &mip, const TexMaterialLODInfo &mat, int specFlags,
    const TLMaterial &tlMat, int spec, const EngineShapeProperties &prop
    );

  void DrawPolygon(const VertexIndex *ii, int n);
  void DrawSection(const FaceArray &face, Offset beg, Offset end);


  // integrated transform&lighting
  bool GetTL() const {return true;}
  bool HasWBuffer() const {return false;}
  //bool HasWBuffer() const {return false;}

  virtual float GetLastKnownAvgIntensity() const;
  virtual void ForgetAvgIntensity();

  void DoForgetAvgIntensity();

#if _VBS3
  //! Virtual method
  virtual void SetTunnelVisionCoef(float tunnelVisionCoef) {_tunnelVisionCoef = tunnelVisionCoef;}
  //! Virtual method
  virtual void SetFlashBang(float duration);
#endif

  // engine w-buffer interface
  virtual bool IsWBuffer() const;
  virtual bool CanWBuffer() const;
  virtual void SetWBuffer(bool val);

  virtual bool CanTwoSidedStencil() const {return _caps._canTwoSidedStencil;}

  //! Setup all the lights for one model (main light and P&S lights)
  void SetupLights(int cb, const LightList &lights, int spec, const DrawParameters &dp);
  //! Combine material and light colors
  void SetupMaterialLights(int cb, const TLMaterial &mat);
  void DoSwitchTL(int cb, TLMode active );
  
  void SetWorldView(int cb);
  /// setups world/view/projection matrix and camera position
  void SetupWorldViewProjCam(int cb, const PositionRender &modelToWorld);

  bool AddVertices( const TLVertex *v, int n );

  HRESULT LockDynBuffer(const ComRef<IDirect3DVertexBuffer9> &buf, void * &data, int lockFlags, int lockOffset, int size);
  HRESULT LockDynBuffer(const ComRef<IDirect3DIndexBuffer9> &ibuf, void * &data, DWORD flags, int indexOffset, int size);

public:
  //! Setting of texgen scale and offset (usually called when setting the VB)
  void SetTexGenScaleAndOffset(int cb, const UVPair *scale, const UVPair *offset);
  /// helper: used from VertexBufferD3D9::Update
  void SetVSourceStatic(int cb, const Shape &src, const EngineShapeProperties &prop, const VertexBufferD3D9 &buf, VSDVersion vsdVersion);
  void SetVSourceDynamic(
    int cb, const VertexTableAnimationContext *animContext, const Shape &src, const EngineShapeProperties &prop, const VertexBufferD3D9 &buf
  );


public:
  void SwitchTL(int cb,TLMode active)
  {
    if (active==CBState(cb)._tlActive) return;
    DoSwitchTL(cb,active);
  }
  //! Set the current material, combine material color with lights
  void DoSetMaterial(int cb, const TLMaterial &mat);
  //void SetMaterial(const TLMaterial &mat, const LightList &lights, int spec);

  void ProjectionChanged(int cb);
  void EnableSunLight(int cb, EMainLight mainLight);
  void SetFogMode(int cb, EFogMode fogMode);

  int ShapePropertiesNeeded(const TexMaterial *mat) const;
  bool BeginMeshTLInstanced(int cb, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias, float minDist2, const DrawParameters &dp);

  /// prepare offset needed to fight shadow acne
  void PrepareShadowOffset(int cb, const EngineShapeProperties &prop);
  
  virtual void SetupInstances(int cb, const ObjectInstanceInfo *instances, int nInstances);
  virtual void SetupInstancesSprite(int cb, const ObjectInstanceInfo *instances, int nInstances);
  void SetupInstancesShadowVolume(int cb, const ObjectInstanceInfo *instances, int nInstances);
  //! Function to draw all sections in instanced version of drawing
  void DrawMeshTLSectionsInstanced(int cb, const Shape &sMesh, const EngineShapeProperties &prop, int spec, int bias, int instancesOffset, int instancesCount);
  //! Instanced version of drawing
  virtual void DrawMeshTLInstanced(
    int cb, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias,
    const ObjectInstanceInfo *instances, int nInstances, float minDist2, const DrawParameters &dp
    );
  virtual void DrawShadowVolumeInstanced(
    int cb, LODShape *shape, int level, const ObjectInstanceInfo *instances, int instanceCount, SortObject *oi
#if _HELISIM_RENDER_TO_TEXTURE
    , int drawListId
#endif
  );

  void ClearLights(int cb);

  //void PrepareMeshTL( const LightList &lights, const Matrix4 &modelToWorld, int spec ); // prepare internal variables
  virtual void BeginShadowVolume(int cb, float shadowVolumeSize);
  virtual void BeginShadowVolumeRendering();
  virtual void EndShadowVolumeRendering();
  //! Function to calculate closest Z^2 upon specified mindist2 and Engine's aspect settings
  float GetClosestZ2(float minDist2);
  virtual void BeginInstanceTL(int cb, const PositionRender &modelToWorld, float minDist2, int spec, const LightList &lights, const DrawParameters &dp);
  virtual bool BeginMeshTL(int cb, const VertexTableAnimationContext *animContext, const Shape &sMesh, const EngineShapeProperties &prop, int spec, const LightList &lights, int bias); // convert all mesh vertices
  virtual void EndMeshTL(int cb, const Shape &sMesh ); // forget mesh

  virtual IVisibilityQuery *BegPixelCounting(int cb, IVisibilityQuery *query);
  virtual void EndPixelCounting(int cb, IVisibilityQuery *query);

  virtual bool IsCursorSupported() const;
  virtual bool IsCursorShown() const;
  virtual void SetCursor(Texture *cursor, int x, int y, float hsX, float hsY, PackedColor color, int shadow);
  virtual void UpdateCursorPos(int x, int y);
  virtual void CursorOwned(bool owned);

	//currently for stencil test purposes
	virtual void SetStencilBufferTest(int cb, bool test);

private:

  void ShowDeviceCursor(bool showIt);

  //!{ Selecting shadow map type
  void UseShadowMapSSSM(int cb);
  void UseShadowMapSB(int cb);
  //!}

#if _ARMA3_NEW_LIGHTING_CFG
  /** computes back reflection color
  \param diffuse combined light and material diffuse component
  */
  Color EngineDD9::ComputeBackReflection(const Color& diffuse);
  /// computes ground reflection color
  Color EngineDD9::ComputeGroundReflection(const Color& lightAmbient, const Color& matAmbient);
#endif

public:

  bool IsLLRecording() const {return _backgroundD3D==RecLowLevel;}
  bool IsRecording() const {return _backgroundD3D!=RecNone;}
  virtual bool IsCBRecording() const {return _d3DDevice._cbCount>0 || _backgroundD3D==RecLowLevel;}
  virtual bool IsInCBScope() const;

  virtual int StartCBRecording(int resultIndex, int thrId, int debugId);
  virtual void StopCBRecording(int cb, int resultIndex, const char *name);

  virtual void StartCBScope(int numThreads, int nResults);
  virtual void EndCBScope();

  virtual bool CopyCBPredicatedScope(PredicatedCBScope &scope, PredicationMode pred);
  virtual bool StartCBPredicatedScope(PredicatedCBScope &scope, PredicationMode pred);
  virtual void EndCBPredicatedScope(PredicatedCBScope &scope);

  virtual bool Allow3DUI() const;

  template <class WorkerThreadClass>
  void WaitUntilSpaceAvailable(WorkerThreadClass &bg, int nResults);
  
  void StartBackgroundScope();
  void EndBackgroundScope(bool flushMem, const char *pixScope);

  void UnsetAllDiscardableResourcesFromCBState();
  void UnsetAllDiscardableResources();
  /// verify all discardable resources are unset (corresponding pointers in the state are NULL)
  bool CheckAllDiscardableResourcesUnset() const;
  
  #if SAFE_RESOURCE_UNSET
  void BGAddResourceToRelease(IDirect3DResource9 *item){if (item) _bgResToRelease.Add(item);}
  void BGAddTextureHandleToRelease(TextureHandle9 *item){if (item) _bgTexHandleToRelease.Add(item);}
  #endif
  
  void BGAddRefToRelease(Texture *item);
  
  void BGReleaseResourcesAndTextures();
  void BGReleaseRefs();

  /// should we use a predication for rendering?
  bool UsePredication() const;
  /// callback wrapper around a member function
  static DWORD WINAPI BGThreadCallback(void *param) {((EngineDD9 *)param)->BGThread();return 0;}
  static DWORD WINAPI VisThreadCallback(void *param) {((EngineDD9 *)param)->VisThread();return 0;}
  
  /// background rendering thread - processing data
  void BGThreadProcess();
  /// background rendering thread
  void BGThread();

  /// background rendering thread - processing data
  void VisThreadProcess();
  /// background rendering thread
  void VisThread();

  //@{ compile time conditional dispatch
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
  
  enum CleanUpRequest
  {
    CleanUpNone,
    CleanUpNeeded,
    CleanUpForced,
  };
  /// submit any instructions pending in corresponding GetBackgroundData
  template <class WorkerThreadClass>
  bool SubmitBackground(WorkerThreadClass &bg, bool flushMem, bool setEvent, CleanUpRequest needCleanUp, const char *pixScope);
  
  template <class WorkerThreadClass>
  void FlushBackground(WorkerThreadClass &bg, bool flushMem, const char *pixScope, bool waitUntilDone=true);

  template <class WorkerThreadClass>
  void FlushWaitUntilDone(WorkerThreadClass &bg, bool flushMem=true);

  template <class WorkerThreadClass>
  typename WorkerThreadClass::BGDataType &GetBackgroundData();

  /// debugging/testing: check if background thread state is correct and consistent
  template <class WorkerThreadClass>
  bool CheckThreadMemory(WorkerThreadClass &bg) const;
  
  //bool ProcessBackground();
  //!{ Methods to control the ShadowMapRendering
  virtual bool IsSBPossible() const {return _sbTechnique != SBT_None;}
  virtual void EnableSBShadows(int quality);
  virtual bool IsSBEnabled() const;
  virtual void CommitSettings();
  
  float ComputeShadowConstants(float shadowsZ, float sunClosestDistanceFrom0, Matrix4Val shadowPlaneOrigin, bool rainMap);
  Matrix4 ComputeShadowPlaneOrigin(float shadowsZ, float sunClosestDistanceFrom0, int layerIndex);
  /// Calculate the position of the frustum bounding sphere (in world coordinates)
  float ComputeShadowFrustumBoundingSphere(float shadowsZ, Vector3 &position, bool rainMap);
  /// Calculate the shadow plane offset from center of the frustum bounding sphere
  float ComputeShadowPlaneOffset(Vector3Par position, float sunClosestDistanceFrom0, float radius, bool rainMap);
  

  /// because of predication, we need some shadow parameters to be prepared before priming begins
  virtual void PrepareShadowMapRendering(float sunClosestDistanceFrom0, int layerIndex, float shadowsZ, bool firstLayer);

  virtual void BeginShadowMapRendering(float sunClosestDistanceFrom0, int layerIndex, float shadowsZ, bool firstLayer, bool rainMap);
  virtual void EndShadowMapRendering(float minShadowsZ, float maxShadowsZ, bool firstLayer, bool lastLayer, bool rainMap);
  virtual void RenderStencilShadows();
  
  void EnableSBShadowsCommit();
  //!}

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

  //!{ Methods to encapsulate the 3D rendering of the scene
  /*!
  This is used for instance for predicated tiling on X360
  */
  void Begin3DRendering();
  void End3DRendering();
  //!}


  #define LOG_WAIT_GPU(name,obj)
  //! Method to fill out the PS constants with craters properties
  virtual void UseCraters(int cb, const AutoArray<CraterProperties> &craters, int maxCraters, const Color &craterColor);
  //! Method to return the maximum number of available craters
  virtual int GetMaxCraters() const {return MAX_CRATERS;}

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
  void SetupVSConstantsPoint(int cb);
  void SetupVSConstantsWater(int cb);
  void SetupVSConstantsShore(int cb);
  void SetupVSConstantsTerrain(int cb);
  void SetupVSConstantsTerrainGrass(int cb);
  void SetupVSConstants(int cb, int minBoneIndex, int bonesCount);
  //!}
  //! Sets the PS constants
  void SetupPSConstants(int cb);

  void DoDrawSSAO(float fogEnd, float fogStart, float fogCoef);
  void DrawSSAO();

	void DoDrawEnfDebugShapes();
	void DrawEnfDebugShapes();

  virtual void DrawSectionTL(int cb, const Shape &sMesh, int beg, int end, int bias, int instancesOffset = 0, int instancesCount = 0, const DrawParameters &dp = DrawParameters::_default);
  void DrawStencilShadows(bool interior, int quality);

  virtual void DrawDecal(int cb, Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color, const MipInfo &mip, const TexMaterial *mat, int specFlags);
  
  void DoDrawDecal(int cb, Vector3Par screen, float rhw, float sizeX, float sizeY, PackedColor color, const MipInfo &mip, const TexMaterial *mat, int specFlags);
  
  bool ClipDraw2D(
    float &xBeg, float &yBeg, float &xEnd, float &yEnd,
    float &uBeg, float &vBeg, float &uEnd, float &vEnd,
    const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip
  );
  void Draw2D(const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip);
  virtual void Draw2D(const Draw2DPars &pars, const Rect2DAbs &rect, const Rect2DAbs &clip=Rect2DClipAbs);
  virtual void Draw2D(const Draw2DParsNoTex &pars, const Rect2DAbs &rect, const Rect2DAbs &clip=Rect2DClipAbs);
  
  void DrawLinePrepare();
  void DrawLineDo(const Line2DAbs &line, PackedColor c0, PackedColor c1, const Rect2DAbs &clip=Rect2DClipAbs);
  void DoDrawLineDo(const Line2DAbs &line, PackedColor c0, PackedColor c1, const Rect2DAbs &clip);

  /// implementation of Draw2D, used for high-level recording
  void DoDraw2D(const Draw2DParsExt &pars, const Rect2DAbs &rect, const Rect2DAbs &clip);

  virtual void DrawText3DCenteredAsync(
    Vector3Par center, Vector3Par up, Vector3Par dir, ClipFlags clip,
    Font *font, PackedColor color, int spec, const char *text,
    float x1c = 0, float y1c = 0, float x2c = 1e6, float y2c = 1
  );

  virtual void PerformTaskOnMainThread(MainThreadRenderTask * task);

  /// 3D texts waiting to be rendered
  /** added by MJs, thread safety needed */
  
  CircularArray<MainThreadRenderTask *, 256> _mainThreadTasks;
  
  struct DrawText3DCenteredAsyncPars;
  
  void DoDrawText3DCenteredAsync(const DrawText3DCenteredAsyncPars &pars);

  void D3DDrawTexts();

  void D3DConstruct();
  void D3DDestruct();
  void TextureDestroyed( Texture *tex );

  void D3DTextureDestroyed( TextureHandle9 *tex );
	void DebugTestTexture(IDirect3DTexture9* tex);
	void DebugTestIndexBuffers(IDirect3DResource9* ib);

  void D3DCloseDraw();
  void D3DBeginScene();
  void D3DEndScene();
public:
  void CreateVB();
  void DestroyVB();

  void CreateVBTL();
  void DestroyVBTL();

  void RestoreVB();

  void DDError9( const char *text, int err );

  bool CanRestore();

	void DoQueryIssue(IDirect3DQuery9* query, int issueCommand);
	void QueryIssue(int cb, int queryID, int issueCommand);

  void SetFogColor();
  void DoSetFogColor(ColorVal fog);

  virtual void FogColorChanged(ColorVal fogColor );
  virtual void EnableNightEye(float night);
  virtual void SetZPrimingDone(int cb, bool zPrimingDone);

  void PrepareMesh( int cb, int spec ); // prepare internal variables
  bool BeginMesh( int cb, TLVertexTable &mesh, int spec ); // convert all mesh vertices
  void EndMesh( int cb, TLVertexTable &mesh ); // forget mesh

  void SetDebugMarker(int id, const char *text) override;
  int ProfileBeginGraphScope(unsigned int color,const char *name) const;
  int ProfileEndGraphScope() const;


  typedef AutoArray<D3DXMACRO, MemAllocLocal<D3DXMACRO,16> > D3DXMacroList;

  /// create basic preprocessor defines for shader compilation
  void CreateBasicDefines(D3DXMacroList &list, bool close=true);

  /// initialize given shader model
  void InitShaderModel(int select=-1);

  void InitVertexShaders(VertexShaderID vs = NVertexShaderID);
  void DeinitVertexShaders();
  void ReloadVertexShader(VertexShaderID ps);
  void ReloadPixelShader(PixelShaderID vs);

  void InitPixelShaders(SBTechnique sbTechnique, PixelShaderID from=PixelShaderID(0), PixelShaderID to=PixelShaderID(NPixelShaderID-1));
  void DeinitPixelShaders();

  void SelectPixelShader(int cb, PixelShaderID ps)
  {
    CBState(cb)._pixelShaderSel = ps;
    #if _ENABLE_REPORT
    // Log the irradiance shader if present
    switch (ps)
    {
    case PSNormalMap:
    case PSNormalMapMacroAS:
      RPTSTACK();
      RptF("Info: Irradiance map used - shaders with irradiance maps are obsolete");
    }
    #endif
  }
  void SelectPixelShaderType(int cb, PixelShaderType pixelShaderType)
  {
    if (_thermalVision /*&& !_tiVisualizationDone*/)
    {
      // Convert pixel shared type to corresponding thermal type
      switch (pixelShaderType)
      {
      case PSTBasic:
      case PSTBasicSSSM:
      case PSTShadowReceiver1:
        CBState(cb)._pixelShaderTypeSel = PSTThermal;
        break;
      default:
        CBState(cb)._pixelShaderTypeSel = pixelShaderType;
      }
    }
    else
    {
      CBState(cb)._pixelShaderTypeSel = pixelShaderType;
    }
  }

  void DoSelectPixelShader(int cb, const TexMaterial *mat=NULL, int cratersCount=0);

  //!{ Checking and creation of SB particular resources
  bool SBTR_DefaultCheck(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat) const;
  void SBTR_DefaultCreate(int sbSize);

  bool SBTR_nVidiaCheck(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat, D3DFORMAT sbFormat) const;
  void SBTR_nVidiaCreate(D3DFORMAT sbFormat, int sbSize);
  
  bool SBTR_ATICheck(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat, D3DFORMAT sbFormat) const;
  void SBTR_ATICreate(D3DFORMAT sbFormat, int sbSize);
  
  bool SBTR_nVidiaCheck(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat) const;
  void SBTR_nVidiaCreate(int sbSize);
  bool SBTR_nVidiaINTZCheck(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat) const;
  void SBTR_nVidiaINTZCreate(int sbSize);
  
  bool SBTR_ATID16Check(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat) const;
  void SBTR_ATID16Create(int sbSize);
  bool SBTR_ATID24Check(UINT adapter, D3DDEVTYPE devType, D3DFORMAT backBufferFormat) const;
  void SBTR_ATID24Create(int sbSize);
  //!}

  void CheckFreeTextureMemory(int &local, int &nonlocal) const;

  struct CheckMemTypeScope
  {
#if _ENABLE_CHEATS
#define TRACK_VRAM_ALLOC 0
    //#define TRACK_VRAM_ALLOC 1
#endif
#if TRACK_VRAM_ALLOC
    EngineDD9 *_engine;
    int _local,_nonlocal,_size;
    const char *_text;
#endif

    CheckMemTypeScope(EngineDD9 *engine, const char *text, int size)
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
  /// verify current estimations
  void CheckLocalNonlocalTextureMemory();
  /// create resources needed for shadow buffers
  void CreateSBResources();
  /// destroy resources needed for shadow buffers
  void DestroySBResources();

  /// Create common resources
  void CreateResources();
  /// Destroy common resources
  void DestroyResources();

  //! Create resources needed for thermal imaging
  void CreateTIResources();
  //! Destroy resources needed for thermal imaging
  void DestroyTIResources();

  /// get approximate adapter performance
  int GetAdapterLevel(int adapter) const;

  void Init3DState();
  void Done3DState();
  /// prepare presentation parameters based on _w, _h, _pixelSize
  void FindMode(int adapter); 

  D3DFORMAT SelectRTFormat( int adapter, D3DFORMAT displayModeFormat, int minBpp );

  // prepare _wRT,_hRT based on _w,_h
  bool FindFillrateControl(int wRTPreferred, int hRTPreferred);
  
  void Init3D();

  void InitDXGIInfo(const DXGI_ADAPTER_DESC &adapterDesc);
  void InitDirectDraw7(int adapter);
  void Done3D();
  // make sure adapter is selected and _deviceLevel set
  void InitAdapter();
  //void SetD3DBias( int bias );

	UserVertexBufferArray _heatHazeVB;
	void DrawLightHeatHaze(HeatHazeRenderArray& instances, Texture *texture);

#if _ARMA3_SIMUL_WEATHER
  // initializes renderer
  void InitSimulWeather();
  // deinitializes renderer
  void DeInitSimulWeather();
#endif

  float ZShadowEpsilon() const {return 0.01;}
  float ZRoadEpsilon() const {return 0.005;}

  float ObjMipmapCoef() const {return 1.5;}
  float LandMipmapCoef() const {return 1.0;}

  bool ShadowsFirst() const {return false;}
  bool SortByShape() const {return true;}

private:
  // helper for various freeing implementations
  size_t FreeOneItem(int minAge);
  size_t Free(size_t size, int minAge);

public:
  //@{ implementation of MemoryFreeOnDemandHelper
  virtual float Priority() const;
  virtual size_t MemoryControlled() const;
  virtual size_t MemoryControlledRecent() const;
  virtual size_t FreeOneItem();
  virtual void MemoryControlledFrame();
  //@}

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

  // diagnostics
  void CheckLRU() const;
  void WaitForVisThread() const;
  void FlushVisThread() const;

  /// recover from memory allocation failure
  size_t FreeOnDemandSystemMemoryLowLevel(size_t toRelease) const;
  virtual void FlushMemory(bool deep, bool allowAutoDeep);
  virtual void RequestFlushMemory();
  
  void RequestFlushMemoryCritical();

  float GetResetFactor() const;
  
#if _ENABLE_CHEATS
  RString GetStat(int statId, RString &statVal, RString &statVal2);
#endif
#ifdef TRAP_FOR_0x8000
  virtual void TrapFor0x8000(const char *str);
#endif
};

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
      _d3DDevice->Function(__VA_ARGS__); \
    } \
  } \
  else \
  { \
    CB(cb).Function(__VA_ARGS__); /*LogF("CB " #Function);*/ \
  }

#define CALL_CB_ENGINE(engine, cb,Function,...) \
  if (cb<0) \
  { \
    if (engine->IsLLRecording()) /* even if multicore is not allowed, we may still be able to use single core CB */ \
    { \
      engine->CB(cb).Function(__VA_ARGS__); /*LogF("D3D " #Function);*/ \
      /*Assert(!_bgRecPredication);*/ /* top-level recorded instructions must not be predicated*/ \
    } \
    else \
    { \
      Assert(!engine->IsCBRecording()); /* verify parameter corresponds to what is set by the device */\
      engine->_d3DDevice->Function(__VA_ARGS__); \
    } \
  } \
  else \
  { \
    engine->CB(cb).Function(__VA_ARGS__); /*LogF("CB " #Function);*/ \
  }


// XBOX / Win32 specific texture format definition

#define tex_A1R5G5B5 D3DFMT_A1R5G5B5
#define tex_A4R4G4B4 D3DFMT_A4R4G4B4
#define tex_A8R8G8B8 D3DFMT_A8R8G8B8
#define tex_R5G6B5 D3DFMT_R5G6B5
#define tex_A8L8 D3DFMT_A8L8

#include <Dxerr9.h>
#define DX_CHECK(hr) \
  if (hr!=D3D_OK) \
{ \
  DXTRACE_ERR("Check",hr); \
  Assert(false); \
}

#define DX_CHECKN(n,hr) \
  if (hr!=D3D_OK) \
{ \
  DXTRACE_ERR(n,hr); \
  Assert(false); \
}

#if _ENABLE_CHEATS

/// macro to make creating specializations of ArgumentToPrint easier
#define ARG_TO_PRINT(retType,type,format) \
  template <> \
  struct ArgumentToPrint<type> \
  { \
    typedef retType RetType; \
    typedef type ArgType; \
    retType FormatArg(const ArgType &value) {return format;} \
  };

ARG_TO_PRINT(RString,int,Format("%d",value))
ARG_TO_PRINT(RString,unsigned int,Format("%d",value))
ARG_TO_PRINT(RString,unsigned long,Format("%d",value))
ARG_TO_PRINT(RString,float,Format("%g",value))
ARG_TO_PRINT(RString,bool,value ? "TRUE" : "FALSE")
ARG_TO_PRINT(RString,Vector3,Format("%g,%g,%g",value.X(),value.Y(),value.Z()))
ARG_TO_PRINT(RString,Color,Format("%.3f,%.3f,%.3f,%.3f",value.R(),value.G(),value.B(),value.A()))
ARG_TO_PRINT(RString,PostFxSettings,Format("rotBlur %s",value.rotBlur ? "TRUE" : "FALSE"))
ARG_TO_PRINT(RString,Draw2DParsExt,value.mip._texture ? value.mip._texture->GetName() : RString())
ARG_TO_PRINT(RString,MipInfo,value._texture ? value._texture->GetName() : RString())
ARG_TO_PRINT(RString,Rect2DAbs,Format("%g,%g,%g,%g",value.x,value.y,value.w,value.h))
ARG_TO_PRINT(RString,Line2DAbs,Format("%g,%g..%g,%g",value.beg.x,value.beg.y,value.end.x,value.end.y))
ARG_TO_PRINT(RString,Rect2DPixel,Format("%g,%g,%g,%g",value.x,value.y,value.w,value.h))
ARG_TO_PRINT(RString,PackedColor,Format("%x",value))


/// partial specialization for all StoredComRef-s
template <class Type>
struct ArgumentToPrint< StoredComRef<Type> >
{
  typedef RString RetType;
  RString FormatArg(const StoredComRef<Type> &value) {return Format("%p",safe_cast<Type *>(value));}
};

/// partial specialization for all StoredComRefFuture-s
template <class Type>
struct ArgumentToPrint< StoredFutureComRef<Type> >
{
  typedef RString RetType;
  RString FormatArg(const StoredFutureComRef<Type> &value) {return value.FormatValue();}
};

/// partial specialization for all pointers
template <class Type>
struct ArgumentToPrint<Type *>
{
  typedef RString RetType;
  RString FormatArg(Type *value) {return Format("%p",value);}
};

/// partial specialization for StoredPtr-s
template <class Type>
struct ArgumentToPrint< StoredPtr<Type> >
{
  typedef RString RetType;
  RString FormatArg(const StoredPtr<Type> &value) {return RString();}
};

/// partial specialization for Arrays-s
template <class Type>
struct ArgumentToPrint< Array<Type> >
{
  typedef RString RetType;
  RString FormatArg(const Array<Type> &value) {return RString();}
};

ARG_TO_PRINT(RString,D3DVIEWPORT9,Format("%g..%g",value.MinZ,value.MaxZ))

ARG_TO_PRINT(RString,D3DPRIMITIVETYPE,Format("%d",value))

ARG_TO_PRINT(RString,IDirect3DSurface9 *,GEngineDD->GetSurfaceDebugName(value))

ARG_TO_PRINT(RString,IDirect3DTexture9 *,GEngineDD->GetTextureDebugName(value))

template <>
struct ArgumentToPrint< StoredTexRef<IDirect3DTexture9> >
{
  typedef RString RetType;
  RString FormatArg(const StoredTexRef<IDirect3DTexture9> &value) {return GEngineDD->GetTextureDebugName(value);}
};


// template <>
// struct ArgumentToPrint<IDirect3DTexture9 *>
// {
//   typedef RString RetType;
//   RString FormatArg(const StoredComRef<IDirect3DTexture9> &value) {return GEngineDD->GetTextureDebugName(value);}
// };

/*
template <class Type>
struct ArgumentToPrint< StoredComRef<Type> >
{
  typedef RString RetType;
  RString FormatArg(const StoredComRef<Type> &value) {return Format("%p",safe_cast<Type *>(value));}
};
*/

#define ENUM_NAME_DEF_CS(x) {x,#x},

struct EnumNameCStr {int value;const char *name;};

template <class Type>
const EnumNameCStr *GetEnumNamesCStr(Type value);

template <class Type>
const char *FindEnumNameCStr(Type value)
{
  const EnumNameCStr *names = GetEnumNamesCStr(value);
  for (int i=0; names[i].name; i++)
  {
    if (names[i].value == value) return names[i].name;
  }
  return "";
}


#if !defined _X360SHADERGEN
ARG_TO_PRINT(RString,D3DRENDERSTATETYPE,FindEnumNameCStr(value))
ARG_TO_PRINT(RString,D3DSAMPLERSTATETYPE,FindEnumNameCStr(value))
#else
ARG_TO_PRINT(RString,D3DRENDERSTATETYPE,Format("%d",value))
ARG_TO_PRINT(RString,D3DSAMPLERSTATETYPE,Format("%d",value))
#endif

ARG_TO_PRINT(RString,D3DTEXTUREFILTERTYPE,Format("%d",value))

ARG_TO_PRINT(RString,IDirect3DVertexShader9 *,GEngineDD->GetVSName(value))
ARG_TO_PRINT(RString,IDirect3DPixelShader9 *,GEngineDD->GetPSName(value))
ARG_TO_PRINT(RString,IDirect3DVertexDeclaration9 *,GEngineDD->GetVDName(value))


// char const *ArgumentToPrint<struct IDirect3DSurface9 *>(struct IDirect3DSurface9 * const &);
// char const *ArgumentToPrint<struct _D3DRECT const *>(struct _D3DRECT const * const &);

// char const *ArgumentToPrint<struct StoredPtr<struct tagRECT> >(struct StoredPtr<struct tagRECT> const &);

#endif


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
void ComputeCalmWaterPars( EngineDD9 *engine, SCalmWaterIn *cwmp, ColorVal matAmbient, ColorVal matDiffuse,
    ColorVal matForcedDiffuse, ColorVal matEmmisive, ColorVal matSpecular, const float matPower );


////////////////////////////////////////////////////////////////////////////////////////////////////
// SEA WATER STUFF
////////////////////////////////////////////////////////////////////////////////////////////////////
/*
//! struct for input engine params for computing sea water params
struct SSeaWaterIn
{
  float cameraPosition[3]; //  world space cam pos (x, y, z)
  float mainLightDirO[3];
  float skyLightColor[3];
  float mainLightColor[3];
  float waveHeight;
};

//! structure for sea water vertex shader params
struct SSeaWaterParamsVS
{
  // [0]
  float cameraPosition[3]; // world space cam pos (x,y,z)
  float wrapScale0;
  // [1]
  float wrapRotA[2]; // wrap A rotation angle sin & cos
  float wrapRotB[2]; // wrap B rotation angle sin & cos
  // [2]
  float waveScaleA;
  float waveScaleB;
  float waveShiftA;
  float waveShiftB;
  // [3]
  float waveHeightA;
  float waveHeightB;
  float derivCoefA; // mul coef for TS calc from derivative
  float derivCoefB;
  // [4]
  float wrapA[4]; // xy ..wrap scale (from world pos), zw ..offset (from speed)
  // [5]
  float wrapB[4];
  // [6]
  float waveScaleCDEF[4]; // 1 / lengths for waves C, D, E, F
  // [7]
  float waveShiftCDEF[4];
  // [8]
  float waveAmpsCDEF[4]; // amplitudes for waves C, D, E, F
  // [9]
  float waveComplementCD; // wave C, D amplitudes complement
  float waveComplementEF; // wave E, F amplitudes complement

  //<<<
  float NA[SEAWATER_VSPARS_SIZE * 4];
};

//! structure for sea water pixel shader params
struct SSeaWaterParamsPS
{
  // [0]
  float invRtSizes[2]; // inverse render target sizes
  float NA1[2];
  //float waveHeightA; 
  //float waveHeightB;
  // [1]
  float blendAnimA[4];
  // [2]
  float blendAnimB[4];
  // [3]
  float blendAmpKoefs[4];
  // [4] - same as SSeaWaterParamsVS  [3]
  float waveHeightA; // for computing surface height in PS ()
  float waveHeightB;
  float derivCoefA; // mul coef for TS calc from derivative
  float derivCoefB;

  // [5]
  float mainLightDirO[3];
  float mainLightSpecPow;

  // [6]
  float maxWaveHeight; // 
  float NA6[3];

  // [7]
  float fogPars[4];
  // [8]
  float lightPars[4];
  
  // [9]
  float lightSum[3];


  //<<<
  float NA[SEAWATER_PSPARS_SIZE_1 * 4];
};

/// reference to input params structure
SSeaWaterIn& GetSeaWaterInPars();

/// freference to const reg param array for shader
SSeaWaterParamsVS& GetSeaWaterParsVSRef();

/// float pointer to const reg param array for shader
const float* GetSeaWaterParsVS();

/// reference to const reg param array for pixel shader
SSeaWaterParamsPS& GetSeaWaterParsPS1Ref();

/// float pointer to const reg param array for pixel shader
const float* GetSeaWaterParsPS1();

void ComputeSeaWaterPars( EngineDD9 *engine, SSeaWaterIn &swip, ColorVal matAmbient, ColorVal matDiffuse,
                         ColorVal matForcedDiffuse, ColorVal matEmmisive, ColorVal matSpecular, const float matPower );

*/


#endif // !_DISABLE_GUI

#endif
