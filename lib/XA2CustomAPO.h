#pragma once
#pragma comment(lib, "xapobased.lib")
#pragma comment(lib, "xapofx.lib")

#include <Es/Common/win.h>

#if defined(_WIN32) 
#include <mmsystem.h>
#endif

#include <Es/Containers/array.hpp>
#include <Es/Threads/multisync.hpp>

#include <El/SoundFile/soundFile.hpp>
#include <Es/Memory/normalNew.hpp>

#include <xaudio2.h>
#include <xapobase.h>
#include <XAPOFX.h>

//--------------------------------------------------------------------------------------
// CSampleXAPOBase
//
// Template class that provides a default class factory implementation and typesafe
// parameter passing for our sample xAPO classes
//--------------------------------------------------------------------------------------
template<typename APOClass, typename ParameterClass>
class CSampleXAPOBase: public CXAPOParametersBase
{

  typedef CXAPOParametersBase base;

public:
  static HRESULT CreateInstance( void* pInitData, UINT32 cbInitData, APOClass** ppAPO );

  //
  // IXAPO
  //
  STDMETHOD(LockForProcess) (
    UINT32 InputLockedParameterCount,
    const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *pInputLockedParameters,
    UINT32 OutputLockedParameterCount,
    const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *pOutputLockedParameters );

  STDMETHOD_(void, Process) (
    UINT32 InputProcessParameterCount,
    const XAPO_PROCESS_BUFFER_PARAMETERS *pInputProcessParameters,
    UINT32 OutputProcessParameterCount,
    XAPO_PROCESS_BUFFER_PARAMETERS *pOutputProcessParameters,
    BOOL IsEnabled);

  // Performs any effect-specific initialization.
  STDMETHOD(Initialize) (__in_bcount_opt(DataByteSize) const void* pParams, UINT32 DataByteSize);

  __forceinline float absfxa2(float g)
  {
    unsigned int *gg =(unsigned int*)&g;
    *(gg)&=2147483647u;
    return g;
  }

  // returns 1.0f for positive floats, -1.0f for negative floats
  // for zero it does not work (may gives 1.0f or -1.0f), but it's faster 
  __forceinline float signfxa2(float f) 
  {
    float r = 1.0f;
    (int&)r |= ((int&)f & 0x80000000); // mask sign bit in f, set it in r if necessary
    return r;
  }
protected:
  CSampleXAPOBase();
  ~CSampleXAPOBase(void);

  //
  // Accessors
  //
  const WAVEFORMATEX& WaveFormat(){ return m_wfx; }

  //
  // Overrides
  //
  void OnSetParameters (const void* pParams, UINT32 cbParams)
  {
    DoAssert( cbParams == sizeof( ParameterClass ) );
    cbParams;
    OnSetParameters( *(ParameterClass*)pParams );
  }

  //
  // Overridables
  //

  // Process a frame of audio. Marked pure virtual because without
  // this function there's not much point in having an xAPO.
  virtual void DoProcess(const ParameterClass& params, FLOAT32* __restrict pData, UINT32 cFrames, UINT32 cChannels ) = 0;

  // Do any necessary calculations in response to parameter changes.
  // NOT marked pure virtual because there may not be a reason to
  // do additional calculations when parameters are set.
  virtual void OnSetParameters( const ParameterClass& ) {}

private:
  // Ring buffer needed by CXAPOParametersBase
  ParameterClass  m_parameters[3];

  // Format of the audio we're processing
  WAVEFORMATEX    m_wfx;

  // Registration properties defining this xAPO class.
  static XAPO_REGISTRATION_PROPERTIES m_regProps;
};

//--------------------------------------------------------------------------------------
// CSampleAPOBase::m_regProps
//
// Registration properties for the sample xAPO classes.
//--------------------------------------------------------------------------------------
template<typename APOClass, typename ParameterClass>
__declspec(selectany) XAPO_REGISTRATION_PROPERTIES CSampleXAPOBase<APOClass, ParameterClass>::m_regProps = {
  __uuidof(APOClass),
  L"XA2CustomAPO",
  L"Bohemia Interactive",
  1,
  0,
  XAPO_FLAG_INPLACE_REQUIRED
  | XAPO_FLAG_CHANNELS_MUST_MATCH
  | XAPO_FLAG_FRAMERATE_MUST_MATCH
  | XAPO_FLAG_BITSPERSAMPLE_MUST_MATCH
  | XAPO_FLAG_BUFFERCOUNT_MUST_MATCH
  | XAPO_FLAG_INPLACE_SUPPORTED,
  1, 1, 1, 1 
};

//--------------------------------------------------------------------------------------
// Name: CSampleXAPOBase::CreateInstance
// Desc: Class factory for sample xAPO objects
//--------------------------------------------------------------------------------------
template<typename APOClass, typename ParameterClass>
HRESULT CSampleXAPOBase<APOClass, ParameterClass>::CreateInstance( void* pInitData, UINT32 cbInitData, APOClass** ppAPO )
{
  DoAssert( ppAPO );
  HRESULT hr = S_OK;

  *ppAPO = new APOClass;
  if (*ppAPO != NULL && pInitData != NULL)
  {
    hr = (*ppAPO)->Initialize( pInitData, cbInitData );
  }
  else
  {
    hr = E_OUTOFMEMORY;
  }

  return hr;
}


//--------------------------------------------------------------------------------------
// Name: CSampleXAPOBase::CSampleXAPOBase
// Desc: Constructor
//--------------------------------------------------------------------------------------
template<typename APOClass, typename ParameterClass>
CSampleXAPOBase<APOClass, ParameterClass>::CSampleXAPOBase( )
  : CXAPOParametersBase( &m_regProps, (BYTE*)m_parameters, sizeof( ParameterClass ), FALSE )
{
  ZeroMemory( m_parameters, sizeof( m_parameters ) );
}

//--------------------------------------------------------------------------------------
// Name: CSampleXAPOBase::~CSampleXAPOBase
// Desc: Destructor
//--------------------------------------------------------------------------------------
template<typename APOClass, typename ParameterClass>
CSampleXAPOBase<APOClass, ParameterClass>::~CSampleXAPOBase()
{

}

template<typename APOClass, typename ParameterClass>
HRESULT CSampleXAPOBase<APOClass, ParameterClass>::Initialize(__in_bcount_opt(DataByteSize) const void* pParams, UINT32 DataByteSize)
{
  UNREFERENCED_PARAMETER(DataByteSize);

  if (pParams)
    base::SetParameters(pParams, DataByteSize);

  return S_OK;
}

//--------------------------------------------------------------------------------------
// Name: CSampleXAPOBase::LockForProcess
// Desc: Overridden so that we can remember the wave format of the signal
//       we're supposed to be processing
//--------------------------------------------------------------------------------------
template<typename APOClass, typename ParameterClass>
HRESULT CSampleXAPOBase<APOClass, ParameterClass>::LockForProcess(
  UINT32 InputLockedParameterCount,
  const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *pInputLockedParameters,
  UINT32 OutputLockedParameterCount,
  const XAPO_LOCKFORPROCESS_BUFFER_PARAMETERS *pOutputLockedParameters )
{
  HRESULT hr = CXAPOParametersBase::LockForProcess(
    InputLockedParameterCount,
    pInputLockedParameters,
    OutputLockedParameterCount,
    pOutputLockedParameters );

  if( SUCCEEDED( hr ) )
  {
    // Copy the wave format. Note that we're using memcpy rather than XMemCpy
    // because XMemCpy is really only beneficial for larger blocks of memory.
    memcpy( &m_wfx, pInputLockedParameters[0].pFormat, sizeof( WAVEFORMATEX ) );
  }
  return hr;
}

//--------------------------------------------------------------------------------------
// Name: CSampleXAPOBase::Process
// Desc: Overridden to call this class's typesafe version
//--------------------------------------------------------------------------------------
template<typename APOClass, typename ParameterClass>
void CSampleXAPOBase<APOClass, ParameterClass>::Process(
  UINT32 InputProcessParameterCount,
  const XAPO_PROCESS_BUFFER_PARAMETERS *pInputProcessParameters,
  UINT32 OutputProcessParameterCount,
  XAPO_PROCESS_BUFFER_PARAMETERS *pOutputProcessParameters,
  BOOL IsEnabled)
{
  DoAssert( IsLocked() );
  DoAssert( InputProcessParameterCount == 1 );
  DoAssert( OutputProcessParameterCount == 1 );
  DoAssert( pInputProcessParameters[0].pBuffer == pOutputProcessParameters[0].pBuffer );

  UNREFERENCED_PARAMETER( OutputProcessParameterCount );
  UNREFERENCED_PARAMETER( InputProcessParameterCount );
  UNREFERENCED_PARAMETER( pOutputProcessParameters );
  UNREFERENCED_PARAMETER( IsEnabled );

  if (IsEnabled)
  {
    ParameterClass* pParams;
    pParams = (ParameterClass*)BeginProcess();
    if (pInputProcessParameters[0].BufferFlags == XAPO_BUFFER_SILENT)
    {
      memset(pInputProcessParameters[0].pBuffer, 0,
        pInputProcessParameters[0].ValidFrameCount * m_wfx.nChannels * sizeof(FLOAT32));
    }
    else if( pInputProcessParameters[0].BufferFlags == XAPO_BUFFER_VALID)
    {
      DoProcess(*pParams, (FLOAT32* __restrict)pInputProcessParameters[0].pBuffer,
        pInputProcessParameters[0].ValidFrameCount, m_wfx.nChannels);
    }
    EndProcess();
  }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

class EnvelopeFollower
{
public:
  EnvelopeFollower(): envelope(0) {}

  void Setup( double attackMs, double releaseMs, int sampleRate );

  template<class T, int skip>
  void Process( size_t count, const T *src );

  double envelope;

protected:
  double a;
  double r;

  double maxIn;
};

//----------

inline void EnvelopeFollower::Setup( double attackMs, double releaseMs, int sampleRate )
{
  a = pow( 0.01, 1.0 / ( attackMs * sampleRate * 0.001 ) );
  r = pow( 0.01, 1.0 / ( releaseMs * sampleRate * 0.001 ) );

  maxIn = 0;
}

template<class T, int skip>
void EnvelopeFollower::Process( size_t count, const T *src )
{
  while( count-- )
  {
    double v=::fabs( *src );

#if _ENABLE_CHEATS
    if (v > maxIn) 
      maxIn = v;
#endif

    src+=skip;
    if( v>envelope )
      envelope = a * ( envelope - v ) + v;
    else
      envelope = r * ( envelope - v ) + v;
  }
}

struct LimiterAPOPars
{
  float attackMs;
  float releaseMs;
  int sampleRate;
  double limit;
};

/** http://musicdsp.org/showone.php?id=265 */
class __declspec( uuid("{e2bc74e3-0982-44b1-bb4f-89dec5d07973}")) 
LimiterAPO : public CSampleXAPOBase<LimiterAPO, LimiterAPOPars>
{
  typedef CSampleXAPOBase<LimiterAPO, LimiterAPOPars> base;

  double _limit;
  enum { MAX_CHANNELS = 2 };
  EnvelopeFollower _envelopes[MAX_CHANNELS];

public:

  LimiterAPO():_limit(1) { ZeroMemory(_envelopes, lenof(_envelopes) * sizeof(EnvelopeFollower)); }

  // Performs any effect-specific initialization.
  STDMETHOD(Initialize) (__in_bcount_opt(DataByteSize) const void* pParams, UINT32 DataByteSize)
  {
    base::Initialize(pParams, DataByteSize);    
    return S_OK;
  }

  void OnSetParameters(const void* pParams, UINT32 cbParams);
  void DoProcess(const LimiterAPOPars&, FLOAT32* __restrict pData, UINT32 cFrames, UINT32 cChannels);
};

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct Discretizer256Pars {
  float voiceQuality;
  Discretizer256Pars() : voiceQuality(1.f) {}
};

class __declspec( uuid("{d7a33a3a-5187-4496-bf34-5454beba18ed}")) 
Discretizer256APO: public CSampleXAPOBase<Discretizer256APO, Discretizer256Pars>
{
  typedef CSampleXAPOBase<Discretizer256APO, Discretizer256Pars> base;
  enum { Coef2Div = 127, CoefMax = 128 };
  float coefs[CoefMax];

public:

  Discretizer256APO():base() { OnSetParameters(NULL, 0); }

  // Performs any effect-specific initialization.
  STDMETHOD(Initialize) (__in_bcount_opt(DataByteSize) const void* pParams, UINT32 DataByteSize)
  {
    base::Initialize(pParams, DataByteSize);    
    return S_OK;
  }

  void OnSetParameters(const void* pParams, UINT32 cbParams);
  void DoProcess(const Discretizer256Pars&, FLOAT32* __restrict pData, UINT32 cFrames, UINT32 cChannels);
};

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct GaussianNoisePars {
  float voiceQuality; //in range [0,1], where 1 means perfect
  GaussianNoisePars() : voiceQuality(1.f) {}
};

class __declspec( uuid("{41642A95-768C-4746-947E-9DDA8DFE207E}")) 
GaussianNoiceAPO: public CSampleXAPOBase<GaussianNoiceAPO, GaussianNoisePars>
{
  typedef CSampleXAPOBase<GaussianNoiceAPO, GaussianNoisePars> base;
  enum { NoiseCount = 16384 };
  static bool  initialized;
  static float GaussNoise[NoiseCount];

  int sampleIx;
public:

  GaussianNoiceAPO():base(), sampleIx(0) { OnSetParameters(NULL, 0); }

  // Performs any effect-specific initialization.
  STDMETHOD(Initialize) (__in_bcount_opt(DataByteSize) const void* pParams, UINT32 DataByteSize)
  {
    base::Initialize(pParams, DataByteSize);
    return S_OK;
  }

  void OnSetParameters(const void* pParams, UINT32 cbParams);
  void DoProcess(const GaussianNoisePars&, FLOAT32* __restrict pData, UINT32 cFrames, UINT32 cChannels);
};

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct CompressorAPOParams
{
  float _gain;
  float _attackPhase;
  float _releasePhase;
  float _ratio;
};

class __declspec( uuid("{5EB8D611-FF96-429d-8365-2DDF89A7C1CD}")) 
CompressorAPO : public CSampleXAPOBase<CompressorAPO, CompressorAPOParams>
{
  typedef CSampleXAPOBase<CompressorAPO, CompressorAPOParams> base;

  float _treshold;
  float _r;
  float _ratios[8];    
  float _dt;
  float _attackPhase;
  float _releasePhase;

public:
  CompressorAPO();
  ~CompressorAPO() {}

  // Performs any effect-specific initialization.
  STDMETHOD(Initialize) (__in_bcount_opt(DataByteSize) const void* pParams, UINT32 DataByteSize)
  {
    base::Initialize(pParams, DataByteSize);    
    return S_OK;
  }

  void OnSetParameters(const void* pParams, UINT32 cbParams);

  void DoProcess(const CompressorAPOParams&, FLOAT32* __restrict pData, UINT32 cFrames, UINT32 cChannels);
};

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct MonoAPOParams
{
  float _ratio;
};

class __declspec( uuid("{a311539c-608f-4bde-982e-7a2981170728}")) 
MonoAPO : public CSampleXAPOBase<MonoAPO, MonoAPOParams>
{
  typedef CSampleXAPOBase<MonoAPO, MonoAPOParams> base;

  float _ratio;

public:
  MonoAPO();
  ~MonoAPO() {}

  // Performs any effect-specific initialization.
  STDMETHOD(Initialize) (__in_bcount_opt(DataByteSize) const void* pParams, UINT32 DataByteSize)
  {
    base::Initialize(pParams, DataByteSize);
    return S_OK;
  }

  void OnSetParameters(const void* pParams, UINT32 cbParams);

  void DoProcess(const MonoAPOParams&, FLOAT32* __restrict pData, UINT32 cFrames, UINT32 cChannels);
};

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

class XA2APOEffectBase: public RefCount
{
protected:
  // IUnknown reference
  ComRef<IXAPO> _effect;
  // processing index
  int _index;

public:
  virtual ~XA2APOEffectBase() { ReleaseEffect(); }

  __forceinline IUnknown* GetIUnknown() const { return static_cast<IUnknown*>(_effect.GetRef()); }

  virtual HRESULT CreateEffect(int index)
  {
    // effect index
    _index = index;
    return S_OK;
  }

  virtual void ReleaseEffect() { _effect.Free(); }

  __forceinline HRESULT EnableEffect(IXAudio2Voice *voice)
  {
    if (voice && _effect)
      return voice->EnableEffect(_index);

    return E_FAIL;
  }

  __forceinline HRESULT DisableEffect(IXAudio2Voice *voice)
  {
    if (voice && _effect)
      return voice->DisableEffect(_index);

    return E_FAIL;
  }

protected:
  __forceinline HRESULT SetEffectParameters(IXAudio2Voice *voice, const void *pars, int sizeInBytes)
  {
    if (_effect)
      return voice->SetEffectParameters(_index, pars, sizeInBytes);

    return E_FAIL;
  }
};

template<class Pars>
class XA2APOEffect: public XA2APOEffectBase
{
protected:
  // effect parameters
  Pars _pars;

public:
  __forceinline const Pars& GetPars() const { return _pars; }

  virtual HRESULT CreateEffect(int index)
  {
    XA2APOEffectBase::CreateEffect(index);
    // clear pars by default
    ZeroMemory(&_pars, sizeof(_pars));

    return S_OK;
  }

  __forceinline HRESULT SetEffectParameters(IXAudio2Voice *voice) 
  {
    if (voice)
      return XA2APOEffectBase::SetEffectParameters(voice, &_pars, sizeof(Pars));

    return E_FAIL;
  }

  __forceinline HRESULT SetEffectParameters(IXAudio2Voice *voice, Pars &pars) 
  {
    _pars = pars;
    return SetEffectParameters(voice);
  }

  __forceinline static float dB2Amplitude(float dB) { return pow(10, dB / 20); }
};
