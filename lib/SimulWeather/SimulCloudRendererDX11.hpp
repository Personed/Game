#ifdef _MSC_VER
#pragma once
#endif

#ifndef _ARMA3_SIMULCLOUDRENDERERDX11_HPP
#define _ARMA3_SIMULCLOUDRENDERERDX11_HPP

#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11

#include "SimulCloudRendererBase.hpp"
#include <Es/Common/delegate.hpp>
#include "SimulCommonDX11.hpp"
#include "../d3d11/engdd11.hpp"

namespace DX11
{

class SimulCloudRendererDX11;

/** represents vertex buffer used to store cloud geometry */
class SimulCloudVertexBuffer11: public RefCount
{
public:
  /**
  \param maxCount maximal number of vertices that can be stored in the buffer
  */
  SimulCloudVertexBuffer11(int maxCount, int vertexSize);
  ~SimulCloudVertexBuffer11();

  /** creates buffer */
  void CreateBuffer();

  /// returns if buffer is busy
  /// we mark the buffer busy in current and prev frame
  bool IsBusy() const {return _usedFrameId >= Glob.frameID - 1;}
  /// marks the buffer as used
  void SetUsed() {_usedFrameId = Glob.frameID;}

  /// returns how many free vertices are we able to write
  int GetNumFree() const {return _maxCount - _curWritePos;}

  /// fills vertex buffer with given data, returns starting vertex index
  int FillVertices(void *data, int vertexSize, int numVertices);

  int GetVertexSize() const {return _vertexSize;}
  int GetMaxCount() const {return _maxCount;}
  int GetCurWritePos() const {return _curWritePos;}

  ComRef<ID3D11Buffer>& GetBuffer() {return _buffer;}

private:
  /// buffer 
  ComRef<ID3D11Buffer> _buffer;
  /// maximal possible number of vertices in the buffer
  int _maxCount;
  /// size of one vertex in bytes
  int _vertexSize;
  /// when this buffer was last used
  int _usedFrameId;
  /// current free write position in the buffer (in vertices, not bytes!)
  int _curWritePos;
};


/** DX11 implementation of SimulCloudRenderer */
class SimulCloudRendererDX11: public SimulCloudRendererBase
{
public:
  SimulCloudRendererDX11(DX11::EngineDD11* engine);
  ~SimulCloudRendererDX11();

  //@{ Implementation of CloudRenderCallback
  virtual void FillCloudTextureSequentially(int texture_index,int texel_index,int num_texels,const unsigned *uint32_array);
  virtual void CycleTexturesForward();
  virtual void FillIlluminationSequentially(int source_index,int texel_index,int num_texels,const unsigned char *uchar8_array);
  //@}

  //@{ Implementation of BaseCloudRenderer
  //! Get the list of three textures used for cloud rendering.
  void **GetCloudTextures();
  //@}

  //@{ Implementation of SimulCloudRendererBase
  /// renders clouds
  virtual void Render(int cb, bool cubemap = false);
  //@}

  /// setting of textures used for distance fading
  void SetLossTextures(Ref<TextureSysGPUDX11>& t1,Ref<TextureSysGPUDX11>& t2);
  void SetInscatterTextures(Ref<TextureSysGPUDX11>& t1, Ref<TextureSysGPUDX11>& t2);

private:
  static const int MaxVertexBuffers = 6;

  //@{ Implementation of SimulCloudRendererBase
  /// initializes renderer - creates all resources,..., returns if succeeded
  virtual bool InitRenderer();
  /// deinitializes renderer - destroys all resources,...
  virtual void DeInitRenderer();
  /** creates noise texture */
  virtual void CreateNoiseTexture(RString fileName);
  /** creates cloud texture */
  virtual void CreateCloudTexture();
  /** creates illumination texture */
  virtual void CreateIlluminationTexture();
  //@}

  /// setups shader constants for cloud rendering
  void SetupShaderConstants(int cb, bool cubemap);
  /// setups textures for cloud rendering
  void SetupTextures(int cb);
  /// setups render states for cloud rendering
  void SetupRenderStates(int cb);
  /// setups render states AFTER cloud rendering
  void RestoreRenderStates(int cb);
  /// setups shaders for clouod rendering
  void SetupShaders(int cb);
  /// updates helper and renders geometry
  void RenderGeometry(int cb, bool cubemap);

  /// render geometry using vertex buffers
  void RenderGeometryVertexBuffer(int cb, bool cubemap, bool async);

  /// returns free vertex buffer that we can use for drawing
  Ref<SimulCloudVertexBuffer11 > GetFreeVertexBuffer(int numVertices);

  /// creates noise texture from arma file (paa), returns if succeeded
  bool CreateNoiseTextureFromFile(RString fileName);

  /// format of textures
  DXGI_FORMAT _textureFormat;

  /// pointer to rendering engine
  DX11::EngineDD11* _engine;

  /// Three cloud textures - 2 to interpolate and one to fill
  Ref<TextureSysGPUDX11> _cloudTextures[TextureCount];
  /// Cloud textures used only for function GetCloudTextures
  ID3D11Texture3D* _cloudTexturesPtrs[TextureCount];

  /// Illumination texture  
  Ref<TextureSysGPUDX11> _illuminationTexture;

  /// Noise texture loaded from paa file
  Ref<Texture> _noiseTexture;
  /// Created noise texture
  SurfaceDataD3D11 _noiseTextureCreated;

  /// textures for distance fading taken from sky renderer
  Ref<TextureSysGPUDX11> _skyLossTexture1;
  Ref<TextureSysGPUDX11> _skyLossTexture2;
  Ref<TextureSysGPUDX11> _skyInscatterTexture1;
  Ref<TextureSysGPUDX11> _skyInscatterTexture2;

  typedef AutoArray<Ref<SimulCloudVertexBuffer11 > > TVertexBufferList;
  /// array of vertex buffers
  TVertexBufferList _vertexBuffers;
  /// array for vertex buffer used for CPU fade
  TVertexBufferList _vertexBuffersCPU;

  /// vertices for drawing - used to fill vertex buffers
  AutoArray<Vertex_t> _vertices;
  AutoArray<CPUFadeVertex_t> _cpuFadeVertices;

  RString _noiseTextureFileName;

  /// critical section to protect vertex buffer creation
  CriticalSection _renderVBCritSection;
};

} //namespace DX11
#endif //#if _ARMA3_SIMUL_WEATHER && _ENABLE_DX11
#endif //_ARMA3_SIMULCLOUDRENDERERDX11_HPP
