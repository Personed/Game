#ifndef __POSTPROCESSDX11_HPP
#define __POSTPROCESSDX11_HPP

#if _ENABLE_DX11

#include "engdd11.hpp"
#include <El/ParamArchive/serializeClass.hpp>
#include "../paramArchiveExt.hpp"
#include "../global.hpp"
#include <Es/Common/mathUtils.hpp>

namespace DX11
{

//! Abstract ancestor of post processes
class EngineDD11::PostProcess : public RefCount, public SerializeClass
{
protected:
  //! Engine
  static EngineDD11 *_engine;
  //! Preparing of the post process (called each time in Do)
  virtual void Prepare();
  //! Finishing of the post process (called each time at the end of Do)
  virtual void Finish();
  
  //! effect priority - priority define order of applying effect to scene
  int _priority;
  //! enable/disable effect based on video options value(level)
  unsigned char _level;
  /// helper function for pixel shader compilation
  bool CompilePixelShader(
    const LazyLoadFile &hlsl, const char *psName, ComRef<ID3D11PixelShader> &ps,
    bool fullPrec=false
  ) const;
  bool CompilePixelShader(
    const LazyLoadFile &hlsl, const char *psName, ComRef<ID3D11PixelShader> &ps,
    bool fullPrec, const D3DXMacroList *extraDefines
  ) const;
  /// helper function for vertex shader compilation
  /// creates vertex shader and its layout
  bool CompileVertexShader(
    const LazyLoadFile &hlsl, const char *vsName, VertexShaderInfo &vs, const D3D11_INPUT_ELEMENT_DESC *iDesc, int numDescElements
  ) const;
  
public:
  //! Setting of the engine pointer
  static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  //! Initialization of the post process
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs) {SetLevel(0xFF); return true;}
  //! Applying of post process effect
  /*!
    @param isLast when true, no more postprocess effects will be called - if possible, postprocess to backbuffer
  */
  virtual void Do(bool isLast) = 0;
#if _HELISIM_RENDER_TO_TEXTURE
  virtual void DoDirect(const RECT *rect, int idx) {}    
#endif
  /// check if the postprocess pass will be executed
  /** some effects sometimes do not execute, depending on parameters */
  virtual bool WillBeDone() const {return true;}
  /* Set effect priority - priority define order of applying pp to scene */
  void SetPriority(int priority) { _priority = priority; }
  /* Get effect priority */
  int GetPriority() const { return _priority; }
  /* Check if effect is enabled */
  virtual bool IsEnabled() { return true; }
  /* Enable / disable effect */
  virtual void Enable (bool enable) {};
  /* Set effect params, when time is 0 then params are set immediately, else pars are linearly interpolated from last used to last set */
  virtual void Commit(float time) {};
  /* Check whether given post process effect is commited */
  virtual bool Committed() const {return false;}
  /* Set effect parameters */
  virtual int SetParams(const AutoArray<float> &pars) {return 0;}
  virtual int SetParams(const float* pars, int nPars) {return 0;}  
  virtual void SetParamsDirect(const float *pars, int nPars) { }
  virtual bool UnInit() {return true;}
  virtual LSError Serialize(ParamArchive &ar) {return LSOK;}
  /* Set effect level mask - enable/disable effect based on value set in video options */
  virtual void SetLevel(unsigned char level) {_level = level;}
  virtual unsigned char GetLevel() const {return _level;}

};

//! Abstract ancestor of simple post processes
class EngineDD11::PPSimple : public PostProcess
{
private:
  typedef PostProcess base;
protected:
  struct Vertex
  {
    Vector3P pos;
    UVPair t0;
  };
  VertexStaticData11::BufferFuture _vb;

  VertexShaderInfo _vs;
  ComRef<ID3D11PixelShader> _ps;
  
  virtual void Prepare();
public:
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, const char *vsName);
};

//! Custom edge vertex structure
struct VertexCustomEdge
{
  Vector3P pos;
  float index;
};

class EngineDD11::PostProcessCustomEdge : public PostProcess
{
  private:
  typedef PostProcess base;
  
  protected:
  //!{ Custom edge resources
  VertexStaticData11::BufferFuture _vbCustomEdge;

  VertexShaderInfo _vsCustomEdge;
  //!}

  public:
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  virtual void Prepare();
};
//! Gaussian blur effect
/*!
  This effect is not applied directly to the output - it only calculates and holds
  blurred image for later use.
*/
class EngineDD11::PPGaussianBlur : public PostProcessCustomEdge
{
private:
  typedef PostProcessCustomEdge base;
protected:

  //!{ Horizontal blur resources
  ComRef<ID3D11PixelShader> _psHorizontal;
  RenderTarget _rtHorizontal;
  //!}

  //!{ Final blur resources
  ComRef<ID3D11PixelShader> _psBlur;
  RenderTarget _rtBlur;
  //!}

  // source texture to blur
  TextureHandle11* _toBlur;
  
public:
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  void Do(bool isLast, TextureHandle11* toBlur);
  virtual void Do(bool isLast);

  //! Function to retrieve the blurred texture
  const RenderTarget& GetBlurredRt() const {return _rtBlur;}
  //! function to retrieve first pass RT
  const RenderTarget& GetFirstRt() const {return _rtHorizontal;}
  //! function to retrieve second pass RT
  const RenderTarget& GetSecondRt() const {return _rtBlur;}
};

//! Final - copying of source to back buffer
class EngineDD11::PPFinal : public PostProcessCustomEdge
{
private:
  typedef PostProcessCustomEdge base;
  ComRef<ID3D11PixelShader> _ps;
  
public:
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  virtual void Do(bool isLast);
  void Do(bool isLast, bool forceNoSRGBOnRead);
};

//! Diag depth buffer PP - only copies depth buffer to current render target
class EngineDD11::PPDiagDepthBuffer: public PostProcessCustomEdge
{
private:
  typedef PostProcessCustomEdge base;
  ComRef<ID3D11PixelShader> _ps;

public:
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  virtual void Do(bool isLast);
};

//! Final - rescaling of source to back buffer (bicubic filter)
class EngineDD11::PPRescaleBicubic : public PPSimple
{
private:
  typedef PPSimple base;
  
  SurfaceDataD3D11 _lookUpTexture;
  
public:
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  virtual void Do(bool isLast);
};

//! Bloom effect
class EngineDD11::PPBloom: public PostProcess
{
private:
	typedef PostProcess base;
protected:
 
	//!{ Custom edge resources
	VertexStaticData11::BufferFuture _vbCustomEdge;
 	VertexShaderInfo _vsCustomEdge;
	//!}

	ComRef<ID3D11PixelShader> _psBlur;
	ComRef<ID3D11PixelShader> _psDownsample2;
	ComRef<ID3D11PixelShader> _psDownsample4;
	ComRef<ID3D11PixelShader> _psCombine;
	//ComRef<ID3D11PixelShader> _ps;

	//!{ Bright resources
  RenderTarget _rtBright;
	//!}

	//!{ Horizontal blur resources
	RenderTarget _rtHorizontal;
	//!}

	//!{ Final blur resources
	RenderTarget _rtBlur;
	//!}

	Ref<Texture> _gammaTexture;

	//! Custom edge resources creation
	bool InitCustomEdge(const LazyLoadFile &vs);

  /// contribution of image color in the composition pass
  float		_imageScale;
  /// contribution of the blurred bloom image in the composition pass
  float		_blurScale;
  /// threshold value
  float		_threshold;
  /// coef used to multiply the value after thresholding
  float   _overbrightCoef;
  /// kernel size of gaussian blur
  int   _blurKernelSize;

public:
	float GetBlurScale() const
	{
		return _blurScale;
	}

	float GetImageScale() const
	{
		return _imageScale;
	}

	//! Virtual method
	virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
	//! Virtual method
	virtual void Do(bool isLast);
	
  void Do(bool isLast, TextureHandle11* rt);
	//! Function to retrieve the blurred texture
  const RenderTarget& GetBlurredTexture() const {return _rtBlur;}
  /// set effect specific parameters
  int SetParams(const float *pars, int nPars);
};

//! SSSM creation
class EngineDD11::PPSSSM : public PPSimple
{
private:
  typedef PPSimple base;
protected:
  //! Virtual method
  virtual void Prepare();
public:
  //! Non-virtual version of init
  bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  virtual void Do(bool isLast) {}
  void Do(float minShadowsZ, float maxShadowsZ, bool firstLayer, bool lastLayer);
};

//! SSSM Stencil shadows inclusion
class EngineDD11::PPSSSMStencil : public PPSimple
{
private:
  typedef PPSimple base;
public:
  //! Non-virtual version of init
  bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  //! Virtual method
  virtual void Do(bool isLast);
};

#ifdef SSSMBLUR
//! SSSMB creation
class EngineDD11::PPSSSMB : public PPGaussianBlur
{
protected:
  //! Shader to perform the final screenspace shadow blur
  ComRef<ID3D11PixelShader> _psFinalBlur;
public:
  //! Virtual method
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  //! Virtual method
  virtual void Do(bool isLast);
};
#endif



//! Depth of field effect
class EngineDD11::PPDOF : public PPSimple
{
private:
  typedef PPSimple base;

protected:
  //! Reference to post process with a texture we require here in this post process
  Ref<PPGaussianBlur> _ppGaussianBlur;

  //true if DistanceDOF distance is used
  bool _useFarDistance;
public:
  //! Non-virtual version of init
  bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, const Ref<PPGaussianBlur> &ppGaussianBlur);
  virtual void Do(bool isLast);
};

//! Depth of field for undefined focal plane case (we blur only from focal plane distance further)
class EngineDD11::PPDistanceDOF : public PPDOF
{
private:
  typedef PPDOF base;
  typedef PPSimple grandBase;
public:
  //! Non-virtual version of init
  bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, PPGaussianBlur *ppGaussianBlur);
};

//! Rain visualization
/**
note: this is similar to a postprocess effect, but in fact it is not a PP effect -
it has no read access to the render target
*/
class EngineDD11::PPRain : public PPSimple
{
private:
  typedef PPSimple base;
public:
  bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  virtual void Do(bool isLast);
};

/// Number of queries (width of the histogram)
static const int NBrightQueries = 16;

/// Manager of the queries
class EngineDD11::QueryManager
{
protected:
  static EngineDD11 *_engine;
  static const int NItems = 4;
  ComRef<ID3D11Query> _query[NItems][NBrightQueries];
  /// record value for each query so that we know the absolute (HDR) meaning of the result
  float _value[NItems][NBrightQueries];
  /// for debugging purposes we may want to store one master value with each query set
  float _masterValue[NItems];
  /// new query to be issued
  int _newQuery;
  /// query which should be ready first
  int _oldestQuery;
  /// how many queries were issued
  int _queriesIssued;
  /// gather results as we get them
  UINT64 _results[NBrightQueries];
  /// index of the next result to come
  int _currentResultIndex;
  //@{ once results are finalized, copy them here
  UINT64 _mostRecentResult[NBrightQueries];
  float _mostRecentResultValue[NBrightQueries];
  float _mostRecentMasterValue;
  //@}

  int Next(int i) {return (i < NItems - 1) ? ++i : 0;}
public:
  static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  QueryManager();
  //! Method to reset all queries
  void ResetQueries();
  bool Issue(const float *value, const float*alpha, float master);
  bool IssueDirect(const float *value, const float *alpha, float masterValue);
  float GetNumberOfPixelsDrawn(int *npd, float *value, bool usePixelCount = false);
};

/// Manager of the queries
class EngineDD11::QueryManagerSinglePass
{
protected:
  static EngineDD11 *_engine;
  static const int NItems = 8;
  ComRef<ID3D11Query> _query[NItems];
  /// record value for each query so that we know the absolute (HDR) meaning of the result
  float _value[NItems][NBrightQueries];
  /// for debugging purposes we may want to store one master value with each query set
  float _masterValue[NItems];
  /// new query to be issued
  int _newQuery;
  /// query which should be ready first
  int _oldestQuery;
  /// how many queries were issued
  int _queriesIssued;
  //@{ once results are finalized, copy them here
  UINT64 _mostRecentResult[NBrightQueries];
  float _mostRecentResultValue[NBrightQueries];
  float _mostRecentMasterValue;
  //@}

  int Next(int i) {return (i < NItems - 1) ? ++i : 0;}
public:
  static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  QueryManagerSinglePass();
  //! Method to reset all queries
  void ResetQueries();
  bool Issue(const float *value, float master, TextureHandle11* alphaTex);
  bool IssueDirect(const float *value, float masterValue, TextureHandle11*alphaTex);
  float GetNumberOfPixelsDrawn(int *npd, float *value);
};

class EngineDD11::PPDecimate : public PostProcess
{
private:
  typedef PostProcess base;
protected:
  //@{ cascade of low-resolution render targets
  /// 16 render targets is enough for more then 64Kx64K pixels
  /** each pass performs 4x or 2x decimation */
  enum {MaxDecimations=16};
  struct DecimationPass
  {
    RenderTarget _rt;
    int _w,_h;
  };
  DecimationPass _dec[MaxDecimations];
  // end of 4x passes (avg only)
  int _decimations;
  //@}

  //////////////////////////////////////////////////////////////////////////
#if _HELISIM_RENDER_TO_TEXTURE

  enum { MaxR2TDecimations = 2 };
  DecimationPass _decR2T[MaxR2TDecimations];

  //@{ RT used for measurements
  RenderTarget _rtMeasureR2T;
  DepthStencil _rtsMeasureDepthR2T;
  //@}

#endif
  //////////////////////////////////////////////////////////////////////////

  //!{ Custom edge resources
  VertexStaticData11::BufferFuture _vbCustomEdge;
  VertexShaderInfo _vsCustomEdge;
  //!}

  /// initial luminance calculations
  ComRef<ID3D11PixelShader> _psLuminance;
  /// average luminance down-sampling
  ComRef<ID3D11PixelShader> _psAvgLuminance;
  /// brightness query pixel shader
  ComRef<ID3D11PixelShader> _psQuery;

  //@{ RT used for measurements
  RenderTarget _rtMeasure;
  DepthStencil _rtMeasureDepth;
  //@}

  //! Query manager
  QueryManager _query;

public:
  //! Histogram of the image
  float _histogram[NBrightQueries];
  int _temperatures[NBrightQueries];
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
#if _HELISIM_RENDER_TO_TEXTURE
  virtual void DoDirect(const RECT *rect, int idx);
#endif
  virtual void Do(bool isLast);
  //   //! Function to retrieve the blurred texture
  //   ComRef<IDirect3DTexture9> GetBlurredTexture() const {return _rtBlur;}
};

//! Thermal visualization
class EngineDD11::PPFinalThermal : public PPSimple
{
private:
  typedef PPSimple base;
protected:
  //! Decimation post process for histogram retrieving
  PPDecimate _ppDecimate;
  //! Post process with a texture we require here in this post process
  PPGaussianBlur _ppGaussianBlur;

  //! Thermal visualisation shader
  ComRef<ID3D11PixelShader> _psThermal;

  RenderTarget _rt; // temporal render target

  // last capture frame time
  Time _time;

  //@{ parameters
  float _deltaT;
  //@}

  //////////////////////////////////////////////////////////////////////////
#if _HELISIM_RENDER_TO_TEXTURE
  RenderTarget _blurRT;
  RenderTarget _rtTextureR2T; // temporal render target

#endif
  //////////////////////////////////////////////////////////////////////////

public:
  //! Virtual method
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  int SetParams(const float *pars, int nPars);
  //! Virtual method
  virtual void Do(bool isLast);
#if _HELISIM_RENDER_TO_TEXTURE
  virtual void DoDirect(const RECT *rect, int idx);
#endif
};

//! Glow
class EngineDD11::PPGlow : public PostProcess
{
private:
  typedef PostProcess base;
protected:
  //! Reference to post process with a texture we require here in this post process
  Ref<PPGaussianBlur> _ppGaussianBlur;
  //@{ cascade of low-resolution render targets
  /// 16 render targets is enough for more then 64Kx64K pixels
  /** each pass performs 4x or 2x decimation */
  enum {MaxDecimations=16};
  struct DecimationPass
  {
    RenderTarget _rt;
    int _w,_h;
  };
  DecimationPass _dec[MaxDecimations];
#if _HELISIM_RENDER_TO_TEXTURE
  DecimationPass _decR2T[MaxDecimations];
#endif
  // end of 4x passes (avg only)
  int _decimation4x;
  // end of last 2x passes (avg/min/max only) - index+1
  int _decimation2x;
  //@}
  
  //@{ 1x1 RT used for aperture control - double buffered
  RenderTarget _rtAperture[2];
  int _rtApertureCurrent;

  /// histogram generating texute (individual brigtnes thresholds)
  SurfaceDataD3D11 _histogramTholds;
  //@}

  #if _DEBUG
  // DX11 TODO
  ///// debugging - lockable sys.mem. surface
  //ComRef<IDirect3DSurface9> _rtsDebugAp;
  //ComRef<IDirect3DSurface9> _rtsDebugDec;
  //
  //Color GetContent(IDirect3DSurface9 *surf, IDirect3DSurface9 *temp, D3DFORMAT format);
  //Color GetContentAp(IDirect3DSurface9 *surf);
  //Color GetContentDec(IDirect3DSurface9 *surf);
  #endif

  //@{ 1x1 RT used for measurements
  RenderTarget _rtMeasure;
  DepthStencil _rtMeasureDepth;
  //@}
  
  //@{ custom edge UV vertex shader + buffer + declaration
	VertexStaticData11::BufferFuture _vbCustomEdge;
  VertexShaderInfo _vsCustomEdge;
  //@}
  
  /// initial luminance calculations
  ComRef<ID3D11PixelShader> _psLuminance;
  /// average luminance down-sampling
  ComRef<ID3D11PixelShader> _psAvgLuminance;
  /// avg-min-max down-sampling
  ComRef<ID3D11PixelShader> _psMaxAvgMinLuminance;
  /// final luminace operator
  ComRef<ID3D11PixelShader> _psAssumedLuminance;
  /// brightness query pixel shader
  ComRef<ID3D11PixelShader> _psQuery;
  //! Pixel shader for glow
  ComRef<ID3D11PixelShader> _psGlow;
  //! Pixel shader for glow + night eye vision
  ComRef<ID3D11PixelShader> _psGlowNight;
  //! Pixel shader for glow + night vision (color transformation)
  ComRef<ID3D11PixelShader> _psGlowNVG;

  //! Queries
  QueryManager _query;

  //! Query manager
  QueryManagerSinglePass _querySinglePass;

  //@{ parameters
  float _deltaT;
  float _eyeAdaptMin;
  float _eyeAdaptMax;
  //@}
  
public:
  static void SetEngine(EngineDD11 *engine)
  {
    QueryManager::SetEngine(engine);
    QueryManagerSinglePass::SetEngine(engine);
  }
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, PPGaussianBlur *blur);
  virtual void Do(bool isLast);
#if _HELISIM_RENDER_TO_TEXTURE
  virtual void DoDirect(const RECT *rect, int idx);
#endif
  int SetParams(const float *pars, int nPars);
 
  //! Method to return aperture texture
  const RenderTarget& GetApertureRT() {return _rtAperture[_rtApertureCurrent];};
  //! Method to return decimation texture
  const RenderTarget& GetDecimationRT() {return _dec[_decimation2x-1]._rt;};
  //! Returns cones
  float Cones() const;
  //! Returns EyeSens coefficient
  Color EyeSens(float cones) const;
  //! Method to cancel all queries
  void CancelQueries() {_query.ResetQueries();}
};

#if defined(_XBOX) || defined(_X360SHADERGEN)
#define PPFLARE_INTENSITY_BASE PPSimple
#else
#define PPFLARE_INTENSITY_BASE PostProcess
#endif

//! Calculating of flare intensity into special texture
class EngineDD11::PPFlareIntensity : public PPFLARE_INTENSITY_BASE
{
private:
  typedef PPFLARE_INTENSITY_BASE base;
protected:
  //!{ Flare intensity resources
  const static int RenderTargetNum = 4;
  RenderTarget _rtLightValues[RenderTargetNum];
  //!}
public:
  virtual void Prepare();
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  virtual void Do(bool isLast) {};
  void Do(int x, int y, int sizeX, int sizeY);
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// vertex data
struct VertexP3T2
{
	Vector3P pos;
	UVPair tc;
};

//! create vertex buffer data for post-process screen plane
bool CreateScreenPlane( VertexP3T2 *v, int segsX, int segsY, float ou, float ov );
//! create vertex buffer data for wet distortion PP screen plane
bool CreateScreenPlane2( VertexP3T2 *v, int segsX, int segsY );
////////////////////////////////////////////////////////////////////////////////////////////////////
//! RotBlur - blurring during camera rotation
class EngineDD11::PPRotBlur : public PostProcess
{
private:
	typedef PostProcess base;
protected:
	VertexStaticData11::BufferFuture _vb;
 	VertexShaderInfo _vs;
	ComRef<ID3D11PixelShader> _ps;
	//! num verts in screen plane
	int vertexCount;
	//! num tris in triangle strip
	int stripSize;

	//! post-process enabled
	bool enabled;
	//! cos of threshold angle ( default value = cos( 1.0 * PI / 180 ) )
	float minCos;
	//! blur power ( default value = 0.03 )
	float blurPower;
	//! depth threshold ...areas with greater value (closer to screen) are not blurred  ( default (compromise) value = 1.2 )
	float depthThreshold;
  //! last frame view matrix
  Matrix4 _oldView;
  //! maximal rotational angle
  float _upperBound;
  //! rendering time of last frame
  Time _lastTime;
  
	//! called before Do()
	virtual void Prepare();

public:
	//! is effect enabled
	bool IsEnabled() { return enabled; }
	//! enable/disable effect
	void Enable( bool enable ) { enabled = enable; }
	//! sets minimal rotation angle between frames for applying of blur effect (in degrees; default = 1.0)
	void SetThresholdAngle( float angle = 1.0 );
	//! set blur power ( default value = 0.03 )
	void SetBlurPower( float value ) { blurPower = value; }
	//! get blur power
	float GetBlurPower() const { return blurPower; }

	//! set depth threshold
	void SetDepthThreshold( float value ) { depthThreshold = value; }
	//! get depth threshold
	float GetDepthThreshold() const { return depthThreshold; }

	//! default values of params (brightness, lerpColor, colorizeColor)
	void SetDefaultValues()
	{
		SetThresholdAngle( 1.0 );
		SetBlurPower( 0.015f );

    #ifdef _M_PPC
      SetDepthThreshold( 0.1f ); 
    #else
       SetDepthThreshold( 0.87f );
    #endif
	}

	//! Virtual method
	virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
	//bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, const Ref<PPGaussianBlur> &ppGaussianBlur, const Camera *camera );
	//! Virtual method
	virtual void Do(bool isLast);
};

//////////////////////////////////////////////////////////////////////////
/* 
RadBlurPar structure store parameters for PPRadialBlur class, these params are used for effect control.

For setting PPRadialBlur params is used EngineDD11::SetRadialBlurrParams() function. 

For applying last set params is necessary used Commit(time) function.

When Commit(time) is called, parameters are linearly interpolated between last used and last set params on the 
basis of elapsed time. When time is set to zero, params are applied immediately. Linear interpolation is
done by class PPEParsInterpolator.                                                                     
*/

template<class Type>
struct SimulateForOneTraits
{
  static void Simulate(Type &tmp, Type src, float diff)
  {
    tmp = tmp + (src - tmp) * diff;
  }
};

// specialization for bool type, interpolation for this type is nonsensical
template<>
struct SimulateForOneTraits<bool>
{
  static void Simulate(bool &tmp, bool src, float diff)
  {
  }
};

/// linear interpolation between src and tmp value on the basis of diff parameter
struct SimulateForOne
{
  float _diff;

  SimulateForOne(float diff):_diff(diff){}

  template<class Type>
  void operator() (Type &tmp, Type src, const char *id)
  {
    SimulateForOneTraits<Type>::Simulate(tmp, src, _diff);
  }
};

/// implement serialize via as global Serialize call, will redirect to ParamArchive member if needed
/** not using ::Serialize directly allows for specialization */
template<class Type>
static LSError SerializeOneFunc(ParamArchive &ar, Type &tmp, const char *id)
{
  return ::Serialize(ar,id,tmp,0,Type());
}

/// serialize one item
struct SerializeOne
{
  ParamArchive &_ar;
  LSError _error;
  SerializeOne(ParamArchive &ar):_ar(ar),_error(LSOK){}

  template<class Type>
  void operator() (Type &tmp, const char *id)
  {
    if (_error==LSOK)
    {
      _error = SerializeOneFunc<Type>(_ar, tmp, id);
    }
  }
};

/// adapter: call unary functor for a given member
template <class Pars, class MemberType, class Functor>
struct CallForMember
{
  Functor _func;
  MemberType Pars::*_member;
  
  CallForMember(const Functor &func, MemberType Pars::*member):_func(func),_member(member){}
  void operator () (const char *id)
  {
    _func(_func.GetObj().*_member,id);
  }


};
/// adapter: call binary functor for a given member
template <class Pars, class MemberType, class Functor>
struct CallForMembers
{
  Functor _func;
  MemberType Pars::*_member;
  
  CallForMembers(const Functor &func, MemberType Pars::*member):_func(func),_member(member){}
  void operator () (const char *id)
  {
    _func(_func.GetObj().*_member,_func.GetWith().*_member,id);
  }

};

/// creative function
template <template <class T, class MT, class F> class Func, class MemberType, class Functor, class Pars>
void CallFunctorForMember(Functor func, MemberType Pars::*member, const char *id)
{
  // create a functor and call it
  Func<Pars, MemberType, Functor>(func,member)(id);
}

#define PAR_MEMBER_LIST(x) CallFunctorForMember<Func>(func,&Par::_##x,#x);
/*
  Store parameters for radial post process effect
*/
struct RadBlurPar
{
  typedef RadBlurPar Par;
  
  //! default value = 0.01
  float _powerX;
  float _powerY;
  //! blur offset - size of unblured center area ( default value = 0.06 )
  float _offsetX;
  float _offsetY;

  
  /// list all members
  template <template <class T, class MT, class F> class Func, class Functor>
  void ForEachMember(Functor func)
  {
    PAR_MEMBER_LIST(powerX);
    PAR_MEMBER_LIST(powerY);
    PAR_MEMBER_LIST(offsetX);
    PAR_MEMBER_LIST(offsetY);
  }

};

/// extend parameters core with expected functionality
template <class Pars>
struct ParsExt: public Pars, public SerializeClass
{
  /// bind one arguments into a functor
  template <class Functor>
  struct BindUnary: public Functor
  {
    Pars &_obj;
    
    BindUnary(Pars &obj, const Functor &func):_obj(obj),Functor(func){}
    Pars &GetObj() const {return _obj;}
  };
  /// bind two arguments into a functor
  template <class Functor>
  struct BindBinary: public Functor
  {
    Pars &_obj;
    const Pars &_with;
    
    BindBinary(Pars &obj, const Pars &with, const Functor &func):_obj(obj),_with(with),Functor(func){}
    Pars &GetObj() const {return _obj;}
    const Pars &GetWith() const {return _with;}
  };
  /// call binary functor for each member
  template<class Func>
  void ForEachWith(Func func, const Pars &with)
  {
    Pars::ForEachMember<CallForMembers>(BindBinary<Func>(*this,with,func));
  }
  /// call unary functor for each member
  template<class Func>
  void ForEach(Func func)
  {
    Pars::ForEachMember<CallForMember>(BindUnary<Func>(*this,func));
  }

  Pars Simulate(const Pars &with, float diff)
  {
    ParsExt<Pars> tmp=*this;
    tmp.ForEachWith(SimulateForOne(diff), with);
    return tmp;
  }

  LSError Serialize(ParamArchive &ar)
  {
    SerializeOne serialize(ar);
    ForEach(serialize);
    return serialize._error;
  }
};

/// Class provide linear interpolation based on elapsed time, between down and upper bound.
template<class ParFX>
class PPEParsInterpolator : public SerializeClass
{
  Time _startTime;
  Time _endTime;  
  bool _commitDone;
  float _timeRange;

public:
  // upper bound
  ParsExt<ParFX> _pars;
  // down bound
  ParsExt<ParFX> _lastPars;

public:
  PPEParsInterpolator(void):_commitDone(true), _timeRange(0.0f) {}
  // set parameters to default values
  void Initialize(void) { _lastPars = _pars; }

  void Commit(float time)
  {
    _startTime = Glob.time;
    _endTime = _startTime + time;
    _commitDone = false;

    _timeRange = (time > 0.0f) ? (1.0f / time) : 0.0;

    // immediate simulation
    if (time <= 0)
    {
      Simulate();
    }
  }

  bool Committed() const {return _commitDone;}

  
  // set params for effect
  ParFX Simulate(void)
  {
    if (_commitDone)
    {
      return _lastPars;
    }
    else
    {
      // perform interpolation on the basis of the elapsed time
      if (Glob.time < _endTime)
      {
        float diff = (Glob.time - _startTime) * _timeRange;

        // linear interpolated params, interpolation is done between _lastPars and _pars
        ParFX parL = _lastPars.Simulate(_pars, diff);
        
        // return interpolated params 
        return parL;
      }
      // commit done
      else
      {
        _lastPars = _pars;
        _commitDone = true;

        // return final params
        return _pars;
      }
    }
  }

	virtual LSError Serialize(ParamArchive &ar)
  {
    CHECK(ar.Serialize("lastPars",_lastPars, 0));
    CHECK(ar.Serialize("pars",_pars, 0));
    CHECK(ar.Serialize("commitDone", _commitDone, 0));
    CHECK(ar.Serialize("timeRange", _timeRange, 0));

    if (ar.IsLoading())
    {
      Time saveTime;
      CHECK(::Serialize(ar, "saveTime",saveTime,0,Time(0)));
      CHECK(::Serialize(ar, "startTime",_startTime,0,Time(0)));
      CHECK(::Serialize(ar, "endTime",_endTime,0,Time(0)));

      float dt = Glob.time - saveTime;
      // bounds time shift
      _startTime += dt;
      _endTime += dt;
    }

    if (ar.IsSaving())
    {
      // saving time, used for correct time boundaries shift
      Time currTime = Glob.time;
      CHECK(::Serialize(ar, "saveTime",currTime ,0,Time(0)));
      CHECK(::Serialize(ar, "startTime",_startTime,0,Time(0)));
      CHECK(::Serialize(ar, "endTime",_endTime,0,Time(0)));
    }

    return LSOK;
  }
};

class EngineDD11::PPResources: public RefCount
{
public:
  __forceinline const PPResources *GetType() const { return this; }
  virtual void UnInit() {}
};

// Resources holder
class EngineDD11::PPResourcesExt: public PPResources
{
  // @{ classes using resourcesExt
  friend class EngineDD11::PPSimpleExt;
  friend class EngineDD11::PPChromAber;
  friend class EngineDD11::PPRadialBlur;
  friend class EngineDD11::PPColorsInversion;
  // @}

protected:
  VertexStaticData11::BufferFuture _vb;
  VertexShaderInfo _vs;
  ComRef<ID3D11PixelShader> _ps;

public:
  PPResourcesExt() : _vb(VertexStaticData11::BufferFuture::Null) {}
  void UnInit()
  {
    _vb = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
    _vs.Free();
    _ps.Free();
  }
};

// 
class EngineDD11::PPSimpleExt : public PostProcess
{
  friend class EngineDD11::PostProcess;

private:
  typedef PostProcess base;

protected:
  struct Vertex
  {
    Vector3P pos;
    UVPair t0;
  };
  // IUnknown stuff
  Ref<PPResources> _resrc;
  // Set vs, ps stuff
  virtual void Prepare();

public:
  // no IUnknown initialization
  virtual bool Init(PPResources *resources, bool firstInit);
  // first - initialization of IUnknown stuff
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, const char *vsName);
  const PPResourcesExt* Resources() { return static_cast<const PPResourcesExt *> (_resrc->GetType()); }
};


class EngineDD11::PPRain3DResources: public PPResources
{
  friend class EngineDD11::PPRain3D;

private:
	VertexStaticData11::BufferFuture _vbRainGeometry;
 	VertexShaderInfo _vsRain;
	ComRef<ID3D11PixelShader> _psRain;

	//!{ rain resources
	Ref<TextureD3D11> _rainTexture;
	//!}


public:
  PPRain3DResources() : _vbRainGeometry(VertexStaticData11::BufferFuture::Null) {}

  void UnInit()
  {
    _vbRainGeometry = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
    _vsRain.Free();
    _psRain.Free();
  }
};

//! Rain3D effect
class EngineDD11::PPRain3D: public PPSimpleExt
{
private:
	typedef PostProcess base;
protected:
	static const int numLayers = 3;

	struct Vertex
	{
		float	pos[3];
		float	u, v, layer;
    inline void Copy(Vector3Par src)
    {
	    pos[0] = src.X();
	    pos[1] = src.Z();
	    pos[2] = src.Y();
    }
	};
  bool _textureReady;
  float _rainStrength;
  float _currentOccluder;
  float _targetOccluder;

  //! resources stuff
  Ref<PPRain3DResources> _resrc;

public:

	//! Virtual method
	virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  virtual bool Init(PPResources *resources, bool firstInit);
  
  void SetTexture(TextureD3D11 *tex);
  void Prepare(float deltaT);
  void Cleanup();
  //! Virtual method
	virtual void Do(bool isLast);
	
  /// set effect specific parameters
  int SetParams(const float *pars, int nPars);
  const PPRain3DResources* Resources() { return static_cast<const PPRain3DResources *> (_resrc->GetType()); }

};

////////////////////////////////////////////////////////////////////////////////////////////////////
//! RadialBlur - centered radial zoom blur with optional unblurred area around center
class EngineDD11::PPRadialBlur : public PPSimpleExt//, public SerializeClass
{
private:
	typedef PPSimpleExt base;
protected:
	//! post-process enabled
	bool enabled;
  // down and upper bound for interpolation
  PPEParsInterpolator<RadBlurPar> _pInterp;

public:
	//! is effect enabled
	bool IsEnabled() { return enabled; }
	//! enable/disable effect
	void Enable( bool enable ) { enabled = enable; }

	//! set blur power ( default value = 0.01 )
  void SetBlurPower( float powerX, float powerY ) { _pInterp._pars._powerX = powerX; _pInterp._pars._powerY = powerY; }
	//! set blur offset ( default value = 0.06 )
	void SetBlurOffset( float offsetX, float offsetY ) { _pInterp._pars._offsetX = offsetX; _pInterp._pars._offsetY = offsetY; }
	//
	void SetDefaultValues()
	{
		SetBlurPower( 0.01f, 0.01f );
		SetBlurOffset( 0.06f, 0.06f );
	}

	//! Init - virtual method
	virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit);
  bool Init(PPResources *res, bool firstInit);
  //! Release resources used by effect
  virtual bool UnInit();
	//! Do - virtual method
	virtual void Do(bool isLast);
	
	//@{ implement SerializeClass interface
	//virtual bool IsDefaultValue(ParamArchive &ar) const {return false;}
	virtual LSError Serialize(ParamArchive &ar);
	virtual void LoadDefaultValues(ParamArchive &ar) {SetDefaultValues();}
  //@}

  void Commit(float time) { _pInterp.Commit(time); }
  bool Committed() const {return _pInterp.Committed();}
  int SetParams(const AutoArray<float> &pars);
  int SetParams(const float* pars, int nPars);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/*
  Store parameters for color correction post process effect
*/
struct ColorPar
{
  typedef ColorPar Par;
  
  //! image brightness ( <0;2>, 0..black, 2..white, 1 ..default
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

#if _ARMA3_RADIAL_COLOR_PP
  static const int NumRadialPars = 7;

  //! major axis radius of ellipse in relative coords (1=whole screen) (<0 means that radial effect is not active)
  float _radialMajorAxisRadius;
  //! minor axis radius of ellipse in relative coords (1=whole screen) (<0 means that radial effect is not active)
  float _radialMinorAxisRadius;
  //! rotation of ellipse axis (in degrees)
  float _radialRotationDeg;
  //! center of ellipse on the screen (in relative coords, [-0.5] -> [0.5], where 0 is in the screen center
  float _radialCenterX;
  //! center of ellipse on the screen (in relative coords, [-0.5] -> [0.5], where 0 is in the screen center
  float _radialCenterY;
  //! coefficient for inner radius (where effect is not applied)
  float _radialInnerRadiusCoef;
  //! coefficient for color interpolation between innter and outer radius
  float _radialInterpCoef;
#endif


  /// list all members
  template <template <class T, class MT, class F> class Func, class Functor>
  void ForEachMember(Functor func)
  {
    PAR_MEMBER_LIST(brightness);
    PAR_MEMBER_LIST(contrast);
    PAR_MEMBER_LIST(offset);
    PAR_MEMBER_LIST(lerpColor);
    PAR_MEMBER_LIST(colorizeColor);
    PAR_MEMBER_LIST(rgbWeights);
#if _ARMA3_RADIAL_COLOR_PP
    PAR_MEMBER_LIST(radialMajorAxisRadius);
    PAR_MEMBER_LIST(radialMinorAxisRadius);
    PAR_MEMBER_LIST(radialRotationDeg);
    PAR_MEMBER_LIST(radialCenterX);
    PAR_MEMBER_LIST(radialCenterY);
    PAR_MEMBER_LIST(radialInnerRadiusCoef);
    PAR_MEMBER_LIST(radialInterpCoef);
#endif
  }
};

// PPColors IUnknown* stuff, shared between instances
class EngineDD11::PPColorResources: public PPResources
{
  friend class EngineDD11::PPColors;

private:
  VertexStaticData11::BufferFuture _vb;
  VertexShaderInfo _vs;
  ComRef<ID3D11PixelShader> _ps;
#if _ARMA3_RADIAL_COLOR_PP
  /// radial version of pixel shader
  ComRef<ID3D11PixelShader> _psRadial;
#endif

  //! num verts in screen plane
  int _vertexCount;
  //! num tris in triangle strip
  int _stripSize;

public:
  PPColorResources() : _vb(VertexStaticData11::BufferFuture::Null) {}
  void UnInit()
  {
    _vb = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
    _vs.Free();
    _ps.Free();
#if _ARMA3_RADIAL_COLOR_PP
    _psRadial.Free();
#endif
  }
};

//////////////////////////////////////////////////////////////////////////

//! Colors - group of color adjustment effects ( contrast, brightness, saturation, colorization, color overlay )
class EngineDD11::PPColors : public PostProcess//, public SerializeClass
{
private:
	typedef PostProcess base;

  friend class EngineDD11::PPColorResources;
protected:
	//! post-process enabled
	bool enabled;
  //! class members
  PPEParsInterpolator<ColorPar> _pInterp;
  // IUnknown stuff
  Ref<PPResources> _resrc;

	//! set stuff
	virtual void Prepare();

public:
	//! is effect enabled
	bool IsEnabled() { return enabled; }
	//! enable/disable effect
	void Enable( bool enable ) { enabled = enable; }

	//! set image brightness (0 ..black, 1 ..unchanged, 2 ..white )
	void SetBrightness( float value ) { _pInterp._pars._brightness = value; }
	//! get image brightness
	float GetBrightness() const { return _pInterp._pars._brightness; }

	//! set image contrast (1 ..normal contrast)
	void SetContrast( float value ) { _pInterp._pars._contrast = value; }
	//! get image contrast
	float GetContrast() const { return _pInterp._pars._contrast; }

	//! set image contrast offset (0 ..unchanged )
	void SetOffset( float value ) { _pInterp._pars._offset = value; }
	//! get image contrast offset
	float GetOffset() const { return _pInterp._pars._offset; }

	//! set color for blending ( rgb ..color, a ..blend factor ( 0..original color, 1..blend color ) )
	void SetBlendColor( Color color ) { _pInterp._pars._lerpColor = color; }
	//! get color for blending
	Color GetBlendColor() const { return _pInterp._pars._lerpColor; }

	//! set color for colorization ( rgb ..color, a ..saturation ( 0..original color, 1..BW multiplied by colorize color ) )
	void SetColorizeColor( Color color ) { _pInterp._pars._colorizeColor = color; }
	//! get color for colorization
	Color GetColorizeColor() const { return _pInterp._pars._colorizeColor; }

	//! set rgb weights for desaturation ( 0.299, 0.587, 0.114, NA )
	void SetRGBWeights( Color weights ) { _pInterp._pars._rgbWeights = weights; }
	//! get color for colorization
	Color GetRGBWeights() const { return _pInterp._pars._rgbWeights; }

#if _ARMA3_RADIAL_COLOR_PP
  //! if we should use radial shader
  bool UseRadialShader(const ColorPar* pars = 0);
  //! setups pixel shader constant for radial color correction
  void SetupRadialShaderConsts(D3DXVECTOR4* vectors, const ColorPar& pars);

  //! major axis radius of ellipse
  float GetRadialMajorAxisRadius() const {return _pInterp._pars._radialMajorAxisRadius;}
  void SetRadialMajorAxisRadius(float radialMajorAxisRadius) {_pInterp._pars._radialMajorAxisRadius = radialMajorAxisRadius;}

  //! minor axis radius of ellipse
  float GetRadialMinorAxisRadius() const {return _pInterp._pars._radialMinorAxisRadius;}
  void SetRadialMinorAxisRadius(float radialMinorAxisRadius) {_pInterp._pars._radialMinorAxisRadius = radialMinorAxisRadius;}

  //! rotation of ellipse axis (in degrees)
  float GetRadialRotationDeg() const {return _pInterp._pars._radialRotationDeg;}
  void SetRadialRotationDeg(float radialRotationDeg) {_pInterp._pars._radialRotationDeg = radialRotationDeg;}

  //! center of ellipse on the screen (in relative coords, [-0.5] -> [0.5], where 0 is in the screen center
  float GetRadialCenterX() const {return _pInterp._pars._radialCenterX;}
  void SetRadialCenterX(float radialCenterX) {_pInterp._pars._radialCenterX = radialCenterX;}

  //! center of ellipse on the screen (in relative coords, [-0.5] -> [0.5], where 0 is in the screen center
  float GetRadialCenterY() const {return _pInterp._pars._radialCenterY;}
  void SetRadialCenterY(float radialCenterY) {_pInterp._pars._radialCenterY = radialCenterY;}

  //! coefficient for inner radius (where effect is not applied)
  float GetRadialInnerRadiusCoef() const {return _pInterp._pars._radialInnerRadiusCoef;}
  void SetRadialInnerRadiusCoef(float radialInnerRadiusCoef) {_pInterp._pars._radialInnerRadiusCoef = radialInnerRadiusCoef;}

  //! coefficient for color interpolation between innter and outer radius
  float GetRadialInterpCoef() const {return _pInterp._pars._radialInterpCoef;}
  void SetRadialInterpCoef(float radialInterpCoef) {_pInterp._pars._radialInterpCoef = radialInterpCoef;}
#endif


	//! default values of params (brightness, lerpColor, colorizeColor)
	void SetDefaultValues()
	{
		SetBrightness( 1.0 );
		SetContrast( 1.0 );
		SetOffset( 0.0 );
		SetBlendColor( Color( 0.0f, 0.0f, 0.0f, 0.0f ) );
		SetColorizeColor( HWhite );
    SetRGBWeights( Color( R_EYE, G_EYE, B_EYE ) );

#if _ARMA3_RADIAL_COLOR_PP
    SetRadialMajorAxisRadius(-1.0f);
    SetRadialMinorAxisRadius(-1.0f);
    SetRadialRotationDeg(0);
    SetRadialCenterX(0);
    SetRadialCenterY(0);
    SetRadialInnerRadiusCoef(0);
    SetRadialInterpCoef(0);
#endif

	}
	//!
	bool IsActive()
	{
		return IsEnabled();
	}
	//! Virtual method
	virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit);
	virtual bool Init(PPResources *resources, bool firstInit);

  const PPColorResources* Resources() { return static_cast<const PPColorResources *> (_resrc->GetType()); }

  //! Release resources used by effect
  virtual bool UnInit();
	//! Virtual method
	virtual void Do( bool isLast );

  virtual void DoDirect(const RECT *rect, int idx);

	//virtual void Do1( bool isLast );

  //@{ implement SerializeClass interface
  //virtual bool IsDefaultValue(ParamArchive &ar) const {return false;}
  virtual LSError Serialize(ParamArchive &ar);
  virtual void LoadDefaultValues(ParamArchive &ar) {SetDefaultValues();}
  //@}

  void Commit(float time) { _pInterp.Commit(time); }
  bool Committed() const {return _pInterp.Committed();}
  int SetParams(const AutoArray<float> &pars);
  int SetParams(const float* pars, int count);
};

////////////////////////////////////////////////////////////////////////////////////////////////////

struct FourFloats
{
  float _x[4];
  
  float &operator [] (int i) {return _x[i];}
  float operator [] (int i) const {return _x[i];}
  
  operator const float *() const {return _x;}
  
  FourFloats operator + (const FourFloats &w) const
  {
    FourFloats r;
    r._x[0] = _x[0]+w._x[0];
    r._x[1] = _x[1]+w._x[1];
    r._x[2] = _x[2]+w._x[2];
    r._x[3] = _x[3]+w._x[3];
    return r;
  }
  FourFloats operator - (const FourFloats &w) const
  {
    FourFloats r;
    r._x[0] = _x[0]-w._x[0];
    r._x[1] = _x[1]-w._x[1];
    r._x[2] = _x[2]-w._x[2];
    r._x[3] = _x[3]-w._x[3];
    return r;
  }
  FourFloats operator * (float f) const
  {
    FourFloats r;
    r._x[0] = _x[0]*f;
    r._x[1] = _x[1]*f;
    r._x[2] = _x[2]*f;
    r._x[3] = _x[3]*f;
    return r;
  }
};

} //namespace

/// advertize template specialization
template <>
LSError Serialize(ParamArchive &ar, const RStringB &name, DX11::FourFloats &value, int minVersion);

namespace DX11
{

/*
Store parameters for color correction post process effect
*/
struct WetDistortPar
{
  typedef WetDistortPar Par;
  
  //! blurriness of distorted image
  float _blurriness;
  //! effect power (top/bottom of screen separately)
  float _powerTop, _powerBottom;
  //! waves speeds .. frequency/PI ( index 0,1 ..horizontal waves; 2,3 ..vertical )
  FourFloats _waveSpeeds;
  //! waves amplitudes (delta texture coordinates) (default: 0.0054, 0.0041, 0.0090, 0.0070 )
  FourFloats _waveAmps;
  /*! coefficients for phase computing;
  [0]..randX, [1]..randY - weight of random vertex data on horizontal/vertical wave phases
  [2]..posX, [3]..posY - weight of vertex X/Y-position on horizontal/vertical wave phases */
  FourFloats _phaseCoefs;

  /// list all members
  template <template <class T, class MT, class F> class Func, class Functor>
  void ForEachMember(Functor func)
  {
    PAR_MEMBER_LIST(blurriness);
    PAR_MEMBER_LIST(powerTop);
    PAR_MEMBER_LIST(powerBottom);
    PAR_MEMBER_LIST(waveSpeeds);
    PAR_MEMBER_LIST(waveAmps);
    PAR_MEMBER_LIST(phaseCoefs);
  }
  
};

class EngineDD11::PPWetDistortResources: public PPResources
{
  friend class EngineDD11::PPWetDistort;

private:
  VertexStaticData11::BufferFuture _vb;
  VertexShaderInfo _vs;
  ComRef<ID3D11PixelShader> _ps;
  //! num verts in screen plane
  int _vertexCount;
  //! num tris in triangle strip
  int _stripSize;

public:
  PPWetDistortResources() : _vb(VertexStaticData11::BufferFuture::Null) {}
  void UnInit()
  {
    _vb = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
    _vs.Free();
    _ps.Free();
  }
};

//! WetDistort - image distortion & blur
class EngineDD11::PPWetDistort : public PostProcess//, public SerializeClass
{
  friend class PPWetDistortResources;

private:
	typedef PostProcess base;
protected:
	//! reference to GaussianBlur post process
	Ref<PPGaussianBlur> _ppGaussianBlur;
	//! post-process enabled
	bool enabled;
  //! class parameters
  PPEParsInterpolator<WetDistortPar> _pInterp;
  //! resources stuff
  Ref<PPResources> _resrc;

  Matrix4 _oldView;
  
	virtual void Prepare();

public:
	//! is effect enabled
	bool IsEnabled() { return enabled; }
	//! enable/disable effect
	void Enable( bool enable ) { enabled = enable; }

	//! set effect blurriness <0,1> (values above 1 causes unusual )
	void SetBlurriness( float value ) { _pInterp._pars._blurriness = value; }
	//! get effect blurriness
	float GetBlurriness() { return _pInterp._pars._blurriness; }
	//! set effect power (top/bottom of screen separately) <0,1>
	void SetEffectPower( float top, float bottom ) { _pInterp._pars._powerTop = top; _pInterp._pars._powerBottom = bottom; }
	//! get effect power on top of screen
	float GetPowerOnTop() { return _pInterp._pars._powerTop; }
	//! get effect power on bottom of screen
	float GetPowerOnBottom() { return _pInterp._pars._powerBottom; }
	//! set waves speeds (frequency/PI)
	void SetWaveSpeeds( float horizontal1, float horizontal2, float vertical1, float vertical2 ) 
  {
		_pInterp._pars._waveSpeeds[0] = horizontal1; _pInterp._pars._waveSpeeds[1] = horizontal2; 
    _pInterp._pars._waveSpeeds[2] = vertical1; _pInterp._pars._waveSpeeds[3] = vertical2; 
  }
	//! get waves speeds
	const float* GetWaveSpeeds() { return _pInterp._pars._waveSpeeds; }
	//! set waves amplitudes
	void SetWaveAmplitudes( float horizontal1, float horizontal2, float vertical1, float vertical2 ) 
  {
		_pInterp._pars._waveAmps[0] = horizontal1; _pInterp._pars._waveAmps[1] = horizontal2; 
    _pInterp._pars._waveAmps[2] = vertical1; _pInterp._pars._waveAmps[3] = vertical2; 
  }
	//! get waves amplitudes
	const float* GetWaveAmplitudes() { return _pInterp._pars._waveAmps; }

	//! set waves amplitudes
	void SetPhaseCoefs( float randX, float randY, float posX, float posY )
  {
		_pInterp._pars._phaseCoefs[0] = randX; _pInterp._pars._phaseCoefs[1] = randY; 
    _pInterp._pars._phaseCoefs[2] = posX; _pInterp._pars._phaseCoefs[3] = posY; 
  }
	//! get waves amplitudes
	const float* GetPhaseCoefs() { return _pInterp._pars._phaseCoefs; }

	//! set default values of params
	void SetDefaultValues()
	{
		SetBlurriness( 1 );
		SetEffectPower( 1, 1 );
		SetWaveSpeeds( 4.10, 3.70, 2.50, 1.85 );
		SetWaveAmplitudes( 0.0054, 0.0041, 0.0090, 0.0070 );
		SetPhaseCoefs( 0.5, 0.3, 10.0, 6.0 );		
	}

	//! 
	bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, PPGaussianBlur *ppGaussianBlur, bool firstInit);
  bool Init(PPResources *resources, PPGaussianBlur *ppGaussianBlur, bool firstInit);
  //! Release resources used by effect
  virtual bool UnInit();
	virtual void Do(bool isLast);

  //@{ implement SerializeClass interface
  //virtual bool IsDefaultValue(ParamArchive &ar) const {return false;}
  virtual LSError Serialize(ParamArchive &ar);
  virtual void LoadDefaultValues(ParamArchive &ar) {SetDefaultValues();}
  //@}

  void Commit(float time) { _pInterp.Commit(time); }
  bool Committed() const {return _pInterp.Committed();}
  int SetParams(const AutoArray<float> &pars);
  int SetParams(const float* pars, int nPars);

  const PPWetDistortResources* Resources() { return static_cast<const PPWetDistortResources*> (_resrc->GetType()); }
};

////////////////////////////////////////////////////////////////////////////////////////////////////

/*
Store parameters for color correction post process effect
*/
struct ChromAberPar
{
  typedef ChromAberPar Par;
  
  //! blur power ( default value = 0.01 )
  float _powerX, _powerY;
  //!  (default = false)
  bool _aspectCorrection;

  /// list all members
  template <template <class T, class MT, class F> class Func, class Functor>
  void ForEachMember(Functor func)
  {
    PAR_MEMBER_LIST(powerX);
    PAR_MEMBER_LIST(powerY);
    PAR_MEMBER_LIST(aspectCorrection);
  }

  LSError Serialize(ParamArchive &ar)
  {
    CHECK(ar.Serialize("powerX",_powerX,0,0.005f));
    CHECK(ar.Serialize("powerY",_powerY,0,0.005f));
    CHECK(ar.Serialize("aspectCorrection",_aspectCorrection,0,false));

    return LSOK;
  }
};

//! ChromAber - lens chromatic aberration
class EngineDD11::PPChromAber: public PPSimpleExt
{
private:
	typedef PPSimpleExt base;
protected:
	//! post-process enabled
	bool enabled;
  //! class parameters
  PPEParsInterpolator<ChromAberPar> _pInterp;

public:
	//! is effect enabled
	bool IsEnabled() { return enabled; }
	//! enable/disable effect
	void Enable( bool enable ) { enabled = enable; }

	//! set aberration power ( default value = 0.01 )
	void SetAberration( float aberationPowerX, float aberationPowerY ) { _pInterp._pars._powerX = aberationPowerX; _pInterp._pars._powerY = aberationPowerY; }
	//! enable / disable aspect correction
	void SetAspectCorrection( bool value ) { _pInterp._pars._aspectCorrection = value; }
	//! 
	void SetDefaultValues()
	{
		SetAberration( 0.005f, 0.005f );
		SetAspectCorrection( false );
	}

	//! Init - virtual method
	virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit);
  bool Init(PPResources *resources, bool firstInit);
  //! Release resources used by effect
  virtual bool UnInit();
	//! Do - virtual method
	virtual void Do(bool isLast);

  //@{ implement SerializeClass interface
  //virtual bool IsDefaultValue(ParamArchive &ar) const {return false;}
  virtual LSError Serialize(ParamArchive &ar);
  virtual void LoadDefaultValues(ParamArchive &ar) {SetDefaultValues();}
  //@}

  void Commit(float time) { _pInterp.Commit(time); }
  bool Committed() const {return _pInterp.Committed();}
  int SetParams(const AutoArray<float> &pars);
  int SetParams(const float* pars, int nPars);
};

//////////////////////////////////////////////////////////////////////////

/*
Parameters for color inversion post process effect
*/
struct ColorInversionPar
{
  typedef ColorInversionPar Par;

  //! image inversion (0..no blur, 1..very blurred)
  Color _invColor;

  /// list all members
  template <template <class T, class MT, class F> class Func, class Functor>
  void ForEachMember(Functor func)
  {
    PAR_MEMBER_LIST(invColor);
  }
};

class EngineDD11::PPColorsInversion: public PPSimpleExt
{
private:
  typedef PPSimpleExt base;

protected:
  bool _initialized;
  //! post-process enabled (default is false)
  bool _enabled;
  // parameters
  PPEParsInterpolator<ColorInversionPar> _pInterp;

public:
  //! is effect enabled
  bool IsEnabled() { return _enabled; }
  //! enable/disable effect
  void Enable( bool enable ) { _enabled = enable; }
  //! set blur values
  void SetInversion(Color invColor) { _pInterp._pars._invColor = invColor; }
  //! set default values of params
  void SetDefaultValues() { SetInversion(Color(0,0,0,0)); }
  //! 
  bool Init( const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit );
  bool Init(PPResources *resources, bool firstInit);
  //! release resources used by effect
  virtual bool UnInit();
  //! 
  virtual void Do(bool isLast);

  virtual LSError Serialize(ParamArchive &ar);
  virtual void LoadDefaultValues(ParamArchive &ar) {SetDefaultValues();}

  void Commit(float time) { _pInterp.Commit(time); }
  bool Committed() const {return _pInterp.Committed();}
  //! set effect parameters, if pars count is correct return 0, else return number of required pars
  int SetParams(const AutoArray<float> &pars);
  int SetParams(const float* pars, int nPars);
};


//////////////////////////////////////////////////////////////////////////

#define SSAO_INTENSITY        2.8f
#define SSAO_TRESHOLD0        0.15f
#define SSAO_TRESHOLD1        1.0f
#define SSAO_NEAR_RADIUS      50.0f
#define SSAO_FAR_RADIUS       15.0f
#define SSAO_NEAR_DIST        0.13f
#define SSAO_FAR_DIST         25.0f
#define SSAO_DEPTH_BLUR_DIST  6.0f
#define SSAO_BLUR_PASSES      2
#define SSAO_HALF_RES         false
#define SSAO_BLUR_HALF_RES    false


/*
parameters for film grain
*/
struct SSAOPar
{
  typedef SSAOPar Par;

  float _intensity;
  float _treshold0;
  float _treshold1;
  float _nearradius;
  float _farradius;
  float _neardist;
  float _fardist;
  float _depthblurdist;
  float _ssaoblurpasses;
  bool  _halfres;
  bool  _blurhalfres;

  /// list all members
  template <template <class T, class MT, class F> class Func, class Functor>
  void ForEachMember(Functor func)
  {
    PAR_MEMBER_LIST(intensity);
    PAR_MEMBER_LIST(treshold0);
    PAR_MEMBER_LIST(treshold1);
    PAR_MEMBER_LIST(nearradius);
    PAR_MEMBER_LIST(farradius);
    PAR_MEMBER_LIST(neardist);
    PAR_MEMBER_LIST(fardist);
    PAR_MEMBER_LIST(depthblurdist);
    PAR_MEMBER_LIST(ssaoblurpasses);

    PAR_MEMBER_LIST(halfres);
    PAR_MEMBER_LIST(blurhalfres);
  }

  LSError Serialize(ParamArchive &ar)
  {
    CHECK(ar.Serialize("intensity", _intensity, 0, SSAO_INTENSITY));
    CHECK(ar.Serialize("treshold0", _treshold0, 0, SSAO_TRESHOLD0));
    CHECK(ar.Serialize("treshold1", _treshold1, 0, SSAO_TRESHOLD1));
    CHECK(ar.Serialize("nearradius", _nearradius, 0, SSAO_NEAR_RADIUS));
    CHECK(ar.Serialize("farradius", _farradius, 0, SSAO_FAR_RADIUS));
    CHECK(ar.Serialize("neardist", _neardist, 0, SSAO_NEAR_DIST));
    CHECK(ar.Serialize("fardist", _fardist, 0, SSAO_FAR_DIST));
    CHECK(ar.Serialize("depthblurdist", _depthblurdist, 0, SSAO_DEPTH_BLUR_DIST));
    CHECK(ar.Serialize("ssaoblurpasses", _ssaoblurpasses, 0, SSAO_BLUR_PASSES));

    CHECK(ar.Serialize("halfres", _halfres, 0, SSAO_HALF_RES));
    CHECK(ar.Serialize("blurhalfres", _blurhalfres, 0, SSAO_BLUR_HALF_RES));

    return LSOK;
  }
  
  void SetHighestSetting()
  {
    _intensity = 2.0f;
    _treshold0 = 0.125f;//SSAO_TRESHOLD0;
    _treshold1 = 1.0f;//SSAO_TRESHOLD1;
    _nearradius = 40.0f;//SSAO_NEAR_RADIUS;
    _farradius = 7.0f;//SSAO_FAR_RADIUS;
    _neardist = 0.1f;//SSAO_NEAR_DIST;
    _fardist = 75.0f;
    _depthblurdist = -1.0f;
    _ssaoblurpasses = 6.0f;
    _halfres = false;
    _blurhalfres = false;
  }

  void SetHighSetting()
  {
    _intensity = 1.75f;
    _treshold0 = 0.15f;
    _treshold1 = 1.0f;
    _nearradius = 32.0f;
    _farradius = 9.0f;
    _neardist = 0.125f;
    _fardist = 50.0f;
    _depthblurdist = -1.0f;
    _ssaoblurpasses = 3.0f;
    _halfres = false;
    _blurhalfres = true;
  }

  void SetMediumSetting()
  {
    _intensity = 1.5f;
    _treshold0 = 0.15f;
    _treshold1 = 1.0f;
    _nearradius = 25.0f;
    _farradius = 10.0f;
    _neardist = 0.15f;
    _fardist = 40.0f;
    _depthblurdist = -1.0f;
    _ssaoblurpasses = 2.0f;
    _halfres = true;
    _blurhalfres = true;
  }
 
  void SetLowSetting()
  {
    _intensity = 0.0f;
  }
  

  SSAOPar()
  {
    SetHighestSetting();
  } 
};


/*!
resources
*/
class EngineDD11::PPSSAOResources: public PPResources
{
  friend class EngineDD11::PPSSAO;

  private:
    int	  _texWidth;
    int   _texHeight;

    int   _SSAOWidth;
    int   _SSAOHeight;

    int   _blurWidth;
    int   _blurHeight;

    VertexStaticData11::BufferFuture  _vb;
    SurfaceDataD3D11 _noiseTexture;

    VertexShaderInfo _vsSSAO;
    VertexShaderInfo _vsSSAODown;

    ComRef<ID3D11PixelShader>	_psSSAO;
    ComRef<ID3D11PixelShader> _psBlurHorz;
    ComRef<ID3D11PixelShader>	_psBlurVert;
    ComRef<ID3D11PixelShader> _psBlurHorzDepth;
    ComRef<ID3D11PixelShader>	_psBlurVertDepth;
    ComRef<ID3D11PixelShader>	_psSSAOFinal;
    ComRef<ID3D11PixelShader> _psSSAODebug;
    ComRef<ID3D11PixelShader> _psSSAODown;

    RenderTarget _rtSSAO;
    RenderTarget _rtBlur;
    RenderTarget _rtSmall;

  public:
    void UnInit()
    {
      _vb = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
      _noiseTexture.Free();

      _vsSSAO.Free();
      _vsSSAODown.Free();

      _psSSAO.Free();
      _psBlurHorz.Free();
      _psBlurVert.Free();
      _psBlurHorzDepth.Free();
      _psBlurVertDepth.Free();
      _psSSAOFinal.Free();
      _psSSAODebug.Free();
      _psSSAODown.Free();

      _rtSSAO.Free();
      _rtBlur.Free();
      _rtSmall.Free();
    }
};


/*!
SSAO effect
*/
class EngineDD11::PPSSAO: public PostProcess
{
  private:
    typedef PostProcess base;
    friend class PPSSAOResources;

    struct Vertex
    {
       Vector3P pos;
       UVPair t0;
    };

    bool _bHalfResOld;
    bool _bBlurHalfResOld;
    bool _bDebug;

    PPEParsInterpolator<SSAOPar> _pInterp;
    SSAOPar _params;
    Ref<PPResources> _resrc;
    bool  _enabled;

    bool SetRT(MyD3DDeviceCB &device, PPSSAOResources *res, bool halfres, bool blurhalfres);

  public:
    bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit);
    bool Init(PPResources *resources, bool firstInit);
    void Do(bool isLast) {}
    void Do(float fogEnd, float fogStart, float fogCoef);
    bool UnInit();

    int SetParams(const float *pars, int nPars);
    void SetDefaultValues();
    virtual void LoadDefaultValues(ParamArchive &ar) {SetDefaultValues();}
    LSError Serialize( ParamArchive &ar );

    bool IsEnabled()        { return _enabled; }
    bool Enabled()					{ return _enabled; }
    void Enable(bool enable)	{ _enabled = enable; }
    bool IsActive()				{ return Enabled() && _pInterp._pars._intensity > 0.001f; }

    void Commit(float time) { _pInterp.Commit(time); }
    bool Committed() const {return _pInterp.Committed();}
    int SetParams(const AutoArray<float> &pars); 

    void SetQuality(int quality);

    void setIntensity(float i)        { saturate(i, 0.0f, 10.0f); _pInterp._pars._intensity = i; }
    void setTreshold0(float s)        { saturate(s, 0.0f, 1.0f); _pInterp._pars._treshold0 = s; }
    void setTreshold1(float g)        { saturate(g, 0.0f, 1.0f); _pInterp._pars._treshold1 = g; }
    void setNearRadius(float i)       { saturate(i, 0.0f, 200.0f); _pInterp._pars._nearradius = i;  }
    void setFarRadius(float i)        { saturate(i, 0.0f, 200.0f); _pInterp._pars._farradius = i;  }
    void setNearDist(float i)         { saturate(i, 0.0f, 200.0f); _pInterp._pars._neardist = i;  }
    void setFarDist(float i)          { saturate(i, 0.0f, 200.0f); _pInterp._pars._fardist = i;  }
    void setDepthBlurDist(float i)    { saturate(i, -200.0f, 200.0f); _pInterp._pars._depthblurdist = i;  }
    void setBlurPasses(int i)         { saturate(i, 0, 4); _pInterp._pars._ssaoblurpasses = i;  }
    void SetHalfRes(bool s)           { _pInterp._pars._halfres = s; }
    void SetBlurHalfRes(bool s)       { _pInterp._pars._blurhalfres = s; }

    float getIntensity()               { return _pInterp._pars._intensity; }
    float getTreshold0()               { return _pInterp._pars._treshold0; }
    float getTreshold1()               { return _pInterp._pars._treshold1; }
    float getNearRadius()              { return _pInterp._pars._nearradius;  }
    float getFarRadius()               { return _pInterp._pars._farradius;  }
    float getNearDist()                { return _pInterp._pars._neardist;  }
    float getFarDist()                 { return _pInterp._pars._fardist;  }
    float getDepthBlurDist()           { return _pInterp._pars._depthblurdist;  }
    float getBlurPasses()              { return _pInterp._pars._ssaoblurpasses;  }    
    bool getHalfRes()                  { return _pInterp._pars._halfres; }
    bool getBlurHalfRes()              { return _pInterp._pars._blurhalfres; }
 
    const PPSSAOResources* Resources() { return static_cast<const PPSSAOResources *> (_resrc->GetType()); }

    void Prepare();
    void CollectParams() { _params = _pInterp.Simulate(); }
};





//////////////////////////////////////////////////////////////////////////

/*
parameters for film grain
*/
struct FilmGrainPar
{
  typedef FilmGrainPar Par;

  float _intensity;
  float _sharpness;
  float _grainSize;
  float _intensityX0;
  float _intensityX1;
  bool  _monochromatic;

  /// list all members
  template <template <class T, class MT, class F> class Func, class Functor>
  void ForEachMember(Functor func)
  {
    PAR_MEMBER_LIST(intensity);
    PAR_MEMBER_LIST(sharpness);
    PAR_MEMBER_LIST(grainSize);
    PAR_MEMBER_LIST(intensityX0);
    PAR_MEMBER_LIST(intensityX1);

    PAR_MEMBER_LIST(monochromatic);
  }

  LSError Serialize(ParamArchive &ar)
  {
    CHECK(ar.Serialize("intensity", _intensity, 0, 0.005f));
    CHECK(ar.Serialize("sharpness", _sharpness, 0, 1.25f));
    CHECK(ar.Serialize("grainSize", _grainSize, 0, 2.01f));
    CHECK(ar.Serialize("intensityX0", _intensityX0, 0, 0.75f));
    CHECK(ar.Serialize("intensityX1", _intensityX1, 0, 1.00f));

    CHECK(ar.Serialize("monochromatic", _monochromatic, 0, true));

    return LSOK;
  }
  
  FilmGrainPar()
  {
    _intensity = 0;
		_sharpness	= 0;
		_grainSize	= 0;
		_intensityX0	= 0;
		_intensityX1	= 0;
		_monochromatic = true;
  }
};

// PPColors IUnknown* stuff, shared between instances
class EngineDD11::PPFilmGrainResources: public PPResources
{
  friend class EngineDD11::PPFilmGrain;

private:
  float	_invtexWidth;
  float	_invtexHeight;

  int	_texWidth;
  int   _texHeight;

  VertexStaticData11::BufferFuture  _vb;
  VertexShaderInfo _vs;
  SurfaceDataD3D11	_noiseTexture;
  ComRef<ID3D11PixelShader>		_psColor;
  ComRef<ID3D11PixelShader>		_psMono;

public:
  PPFilmGrainResources() : _vb(VertexStaticData11::BufferFuture::Null) {}
  void UnInit()
  {
    _vb = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
    _vs.Free();
    _noiseTexture.Free();
    _psColor.Free();
    _psMono.Free();
  }
};

//! Film grain effect
class EngineDD11::PPFilmGrain: public PostProcess
{
private:
  typedef PostProcess base;
  friend class PPFilmGrainResources;

  struct Vertex
  {
    Vector3P pos;
    UVPair t0;
  };

  PPEParsInterpolator<FilmGrainPar> _pInterp;
  // IUnknown stuff
  Ref<PPResources> _resrc;
  bool  _enabled;

public:
  bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, bool firstInit);
  bool Init(PPResources *resources, bool firstInit);
  void Do(bool isLast);
  bool UnInit();

  int SetParams(const float *pars, int nPars);
  void SetDefaultValues();
  virtual void LoadDefaultValues(ParamArchive &ar) {SetDefaultValues();}
  LSError Serialize( ParamArchive &ar );

  bool IsEnabled()					{ return _enabled; }
  void Enable(bool enable)	{ _enabled = enable; }
  bool IsActive()				{ return IsEnabled() && _pInterp._pars._intensity > 0.00001f; }

  void Commit(float time) { _pInterp.Commit(time); }
  bool Committed() const {return _pInterp.Committed();}
  int SetParams(const AutoArray<float> &pars); 

  void setIntensity(float i)        { saturate(i, 0.0f, 1.0f); _pInterp._pars._intensity = i; }
  void setSharpness(float s)        { saturate(s, 0.f, 20.0f); _pInterp._pars._sharpness= s; }
  void setGrainSize(float g)        { saturate(g, 1.0f, 8.0f); _pInterp._pars._grainSize = g; }
  void setIntensityX0(float i)      { _pInterp._pars._intensityX0 = i;  }
  void setIntensityX1(float i)      { _pInterp._pars._intensityX1 = i;  }
  void SetMonochromatic(bool s)     { _pInterp._pars._monochromatic = s; }

  float getIntensity() const			{ return _pInterp._pars._intensity; }
  float getSharpness() const			{ return _pInterp._pars._sharpness; }
  float getGrainSize() const			{ return _pInterp._pars._grainSize; }
  float getIntensityX0() const			{ return _pInterp._pars._intensityX0; }
  float getIntensityX1() const			{ return _pInterp._pars._intensityX1; }
  bool  getMonochromatic() const	{ return _pInterp._pars._monochromatic; }

  const PPFilmGrainResources* Resources() { return static_cast<const PPFilmGrainResources *> (_resrc->GetType()); }
  // set VertexDecl, VertexShader & VertexBuffer
  void Prepare();
};

//////////////////////////////////////////////////////////////////////////

/*
  Parameters for dynamic blur post process effect
*/
struct DynamicBlurPar
{
  typedef DynamicBlurPar Par;
  
  //! image blurriness (0..no blur, 10..very blurred)
  float _blurriness;

  /// list all members
  template <template <class T, class MT, class F> class Func, class Functor>
  void ForEachMember(Functor func)
  {
    PAR_MEMBER_LIST(blurriness);
  }

};

class EngineDD11::PPDynBlurResources: public PPResources
{
  friend class EngineDD11::PPDynamicBlur;

protected:
  //! screen plane vertex buffer
  VertexStaticData11::BufferFuture _vb;
  //! shaders
  VertexShaderInfo _vsBlur;
  ComRef<ID3D11PixelShader> _psBlur;
  VertexShaderInfo _vsFinal;
  ComRef<ID3D11PixelShader> _psFinal;
  //! num verts in screen plane
  int _vertexCount;
  //! num tris in triangle strip
  int _stripSize;

public:
  PPDynBlurResources() : _vb(VertexStaticData11::BufferFuture::Null) {}
  void UnInit()
  {
    _vb = VertexStaticData11::BufferFuture(VertexStaticData11::BufferFuture::Null);
    _vsBlur.Free();
    _psBlur.Free();
    _vsFinal.Free();
    _psFinal.Free();
  }
};

//! dynamic blur effect 
class EngineDD11::PPDynamicBlur: public PostProcess//, public SerializeClass
{
  friend class EngineDD11::PPDynBlurResources;

private:
  typedef PostProcess base;
protected:
  //! reference to GaussianBlur post process
  Ref<PPGaussianBlur> _ppGaussianBlur;
  // IUnknown stuff
  Ref<PPResources> _resrc;
  bool _initialized;

  //! post-process enabled (default is false)
  bool _enabled;
  // parameters
  PPEParsInterpolator<DynamicBlurPar> _pInterp;
  
public:
  //! is effect enabled
  bool IsEnabled() { return _enabled; }
  //! enable/disable effect
  void Enable( bool enable ) { _enabled = enable; }
  //! is active = enabled && non default params
  bool IsActive()
  {
    if( IsEnabled() )
      return true;
    else
      return false;
  }
  //! set blur values
  void SetBlurrines(float blurrines) { _pInterp._pars._blurriness = blurrines; }
  //! set default values of params
  void SetDefaultValues() { SetBlurrines(0.0f); }
  //! 
  bool Init( const LazyLoadFile &hlsl, const LazyLoadFile &vs, PPGaussianBlur *ppGaussianBlur, bool firstInit );
  bool Init(PPResources *resources, PPGaussianBlur *ppGaussianBlur, bool firstInit);
  //! Release resources used by effect
  virtual bool UnInit();
  //! 
  virtual void Do( bool isLast );

  virtual LSError Serialize(ParamArchive &ar);
  virtual void LoadDefaultValues(ParamArchive &ar) {SetDefaultValues();}

  void Commit(float time) { _pInterp.Commit(time); }
  bool Committed() const {return _pInterp.Committed();}
  //! set effect parameters, if pars count is correct return 0, else return number of required pars
  int SetParams(const AutoArray<float> &pars);
  int SetParams(const float* pars, int nPars);
  const PPDynBlurResources* Resources() { return static_cast<const PPDynBlurResources*> (_resrc->GetType()); }
};


//! StretchRect - similar to DX9 StretchRect function 
class EngineDD11::PPStretchRect : public PostProcessCustomEdge
{
private:
  typedef PostProcessCustomEdge base;
  ComRef<ID3D11PixelShader> _ps;

public:
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  virtual void Do(bool isLast);
  virtual void DoDirect(const RECT *rect, int index);

  void Do(ID3D11ShaderResourceView* source, CONST RECT * sourceRect, const RenderTarget& destination, CONST RECT * destRect);
  void DoDirect(ID3D11ShaderResourceView* source, CONST RECT * sourceRect, const RenderTarget& destination, CONST RECT * destRect);
};

//-----------------------------------------------------------------------------
//! FXAA post process
class EngineDD11::PPFXAA : public PostProcess
{
  typedef PostProcess base;

  RenderTarget _backRT;
  RenderTarget _backRT1;

  VertexStaticData11::BufferFuture _vb;
  VertexShaderInfo _vsFxaa;
  AutoArray<ComRef<ID3D11PixelShader> > _psFXAAs;
  ComRef<ID3D11PixelShader> _psFxaa;
  ComRef<ID3D11PixelShader> _psSharpen;

  bool _enabled;
  int _quality;

public:
  PPFXAA(): _enabled(false), _quality(0) {}

  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs);
  virtual bool UnInit();
  virtual void Prepare();
  virtual void Do(bool isLast);

  bool IsEnabled() const { return _enabled; }
  virtual void Enable(bool enable) { _enabled = enable; }

  void SetQuality(int quality);
  int GetQuality() const { return _quality; }

  const RenderTarget &GetSharedRT0() const { return _backRT; }
  const RenderTarget &GetSharedRT1() const { return _backRT1; }
};

//-----------------------------------------------------------------------------
class EngineDD11::PPSMAA : public PostProcess
{
  typedef PostProcess base;

public:
  enum Presets { Low = 0, Medium, High, Ultra, PresetCount };

private:
  RenderTarget _backRT;
  RenderTarget _edgeRT;
  RenderTarget _blendRT;
  DepthStencil _depthBuffer;

  VertexStaticData11::BufferFuture _vb;

  ComRef<ID3D11ShaderResourceView> _areaTex;
  ComRef<ID3D11ShaderResourceView> _searchTex;

  VertexShaderInfo _edgeDetectionVS;
  AutoArray<ComRef<ID3D11PixelShader> > _edgeDetectionPS;

  VertexShaderInfo _blendingWeightVS;
  AutoArray<ComRef<ID3D11PixelShader> > _blendingWeightPS;

  VertexShaderInfo _neighborhoodBlendingVS;
  ComRef<ID3D11PixelShader> _neighborhoodBlendingPS;

  bool _enabled;
  Presets _currentPreset;

public:
  PPSMAA(): _enabled(false), _currentPreset(Medium) {}
  ~PPSMAA() { UnInit(); }

  bool IsEnabled() const { return _enabled; }
  virtual void Enable(bool enable) { _enabled = enable; }
  virtual bool Init(const LazyLoadFile &hlsl, const LazyLoadFile &vs, const RenderTarget &sharedRT0, const RenderTarget &sharedRT1);
  virtual bool UnInit();
  virtual void Prepare();
  virtual void Do(bool isLast);
  void SetQuality(int quality);
  Presets GetPreset() const { return _currentPreset; }
};

//////////////////////////////////////////////////////////////////////////

class EngineDD11::ISpecialEffect: public RefCount, public SerializeClass
{
protected:
  //! Engine
  static EngineDD11* _engine;
  //! PPE handles
  AutoArray<int> _hndls;
  //! PPE handles validity
  bool _handlesValid;
  //! Time to next update
  Time _timeStamp;
  //! Time of last change
  Time _lastUpdate;
  bool _active;

public:
  ISpecialEffect(): _handlesValid(false) {}
  ~ISpecialEffect() { DestroyHndls(); }

public:
  static void SetEngine(EngineDD11 *engine) {_engine = engine;}
  virtual bool Init() = 0;
  virtual void Update() = 0;
  virtual void SetParams(const float *pars, int nPars) = 0;
  virtual LSError Serialize(ParamArchive &ar);

public:
  void DestroyHndls();
  void Reset();
};



class EngineDD11::FireSpecEffect: public ISpecialEffect
{
	typedef ISpecialEffect base;

private:
	enum { CCPriority = 1508 };
	AutoArray<float> _pars;
public:
	bool Init();
	void SetParams(const float *pars, int nPars);
	void Update();
};



// Soldier - hit
class EngineDD11::HitSpecEffect: public ISpecialEffect
{
  typedef ISpecialEffect base;

private:
  enum { DBPriority = 410 };

public:
  bool Init();
  void SetParams(const float *pars, int nPars);
  void Update();
};

// Soldier movement - most noticeable in sprint
class EngineDD11::MovementSpecEffect: public ISpecialEffect
{
  typedef ISpecialEffect base;

private:
  AutoArray<float> _pars;
  enum { RBPriority = 120 };

public:
  bool Init();
  void SetParams(const float *pars, int nPars);
  void Update();
};

// Soldier tired
class EngineDD11::TiredSpecEffect: public ISpecialEffect
{
  typedef ISpecialEffect base;

private:
  AutoArray<float> _pars;
  AutoArray<float> _parsCa;
  enum { WPriority = 310, CCPriority = 1510 };

public:
  bool Init();
  void SetParams(const float *pars, int nPars);
  void Update();
};

//////////////////////////////////////////////////////////////////////////

} //namespace DX11

TypeIsSimple(D3D11_SUBRESOURCE_DATA);

#endif //#if _ENABLE_DX11

#endif //#ifndef __POSTPROCESSDX11_HPP
