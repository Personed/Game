/**
@file XAudio2 sound system
*/

#ifdef _MSC_VER
#pragma once
#endif

#ifndef _SOUND_XA2_HPP
#define _SOUND_XA2_HPP

#define XAUDIO2_HELPER_FUNCTIONS

#define _WIN32_DCOM
#include <Es/Common/win.h>
#include <xaudio2.h>
#include <x3daudio.h>

#if defined(_WIN32) 
#include <mmsystem.h>
#endif

#include <El/FreeOnDemand/memFreeReq.hpp>
#include <Es/Containers/array.hpp>
#include <Es/Threads/multisync.hpp>

#include "soundsys.hpp"

#include <El/SoundFile/soundFile.hpp>
#include <Es/Memory/normalNew.hpp>

#include "WaveInput.h"

struct WaveCacheXA2;
class SoundSystemXA2;

#define LogXA2 NoLog

#if _ENABLE_REPORT
#define CHECK_XA2_ERROR(result, name) \
  if (result != S_OK) \
{ \
  LogF("%s: XAudio2 error %x", name, result); \
}
#else
#define CHECK_XA2_ERROR(result, name)
#endif

//////////////////////////////////////////////////////////////////////////

#ifndef VEC3_2_X3DVEC
#define VEC3_2_X3DVEC(vector3, x3dvector) { (x3dvector).x = (vector3).X(); (x3dvector).y = (vector3).Y(); (x3dvector).z = (vector3).Z(); }
#endif
#ifndef X3DVEC_2_VEC3
#define X3DVEC_2_VEC3(x3dvector) Vector3((x3dvector).x, (x3dvector).y, (x3dvector).z)
#endif

//////////////////////////////////////////////////////////////////////////

/// buffer used for storing audio data
struct XA2Buffer: public RefCount, private NoCopy, public AutoArray<char>
{
  XA2Buffer() : AutoArray() {}
};

/// counted ownership of the audio data buffer
class XA2BufferRef
{
  Ref<XA2Buffer> _buffRef;

public:
  XA2BufferRef() {}
  XA2BufferRef(XA2Buffer *buffer) : _buffRef(buffer) {}

  ///
  XA2Buffer* AudioBuffer(void) const { return _buffRef.GetRef(); }

  /// debugging - we want to display NULL buffers
  int GetDebugInt() const { return _buffRef.NotNull() ? (int)(UINT32 *)_buffRef.GetRef() : 0; }
  bool IsNull() const { return _buffRef.IsNull(); }
  void Free() { _buffRef.Free(); }
};

/// used to play the sound on the current settings
class WaveBuffersXA2
{
protected:
  // defines a point 3D audio source that is used with sound channel
  X3DAUDIO_EMITTER _emitter;

  float _volume; // volume used for 3D sound
  float _accomodation; // current ear accomodation setting
  float _whistling;
  bool _enableWhistling;
  float _whistlingMaxDistance;
  Vector3 _whSourcePos;

  //@{ current occlusion/obstruction levels
  float _occlusion,_obstruction;
  //@}
  float _volumeAdjust;

  WaveKind _kind;

#ifdef _WIN32

  /// remember time of last buffer settings
  DWORD _parsTime;
  /// audio data buffer - used for caching / sharing
  XA2BufferRef _buffer;

protected:
  /// represents an audio data buffer, used with IXAudio2SourceVoice::SubmitSourceBuffer
  XAUDIO2_BUFFER _buffDescr;
  /// source voice is used to submit audio data into the XAudio2 processing pipeline
  IXAudio2SourceVoice *_source;
  /// defines a set of voices to receive data from a single output voice
  XAUDIO2_VOICE_SENDS _sendList;
  XAUDIO2_SEND_DESCRIPTOR _sendDesc;
  /// is the source open?
  bool _sourceOpen;
  /// time in ms when playback has started
  DWORD _startTime;
  /// time in ms when playback has stopped
  DWORD _stopTime;

#endif

  /// Is this one playing?
  bool _playing;
  /// request to start playing
  /*
  Playing the sound may be asynchronous. If we do not succeed immediately, we need to retry later
  */
  bool _wantPlaying; 
  /// 2D sound
  bool _only2DEnabled;
  /// even 3d sound can be temporarily played as 2D
  bool _3DEnabled;
  /// audio volume accomodation enabled
  bool _enableAccomodation;
  /// radio effects switched on
  bool _isRadio;
  float _gainSet;
  float _freqSet;
  float _distance2D;
  int _occlusionSet;
  int _obstructionSet;

  void SetGain(float gain);
  void DoSetPosition(bool immediate, SoundSystemXA2 *soundSystem, WAVEFORMATEX *waweFmt); // TODO:

  float CalcMinDistance() const;

  WaveBuffersXA2();
  ~WaveBuffersXA2();

  void Reset(SoundSystemXA2 *sys);
  void UpdateVolume();
  void SetVolume(float vol, bool imm);
  bool GetMuted() const;
  float GetVolume() const { return _volume; }
  float GetFrequency() const { return _freqSet; }
  Vector3 GetPosition() const { return X3DVEC_2_VEC3(_emitter.Position); }

  /// occlusion and obstruction level control
  void SetObstruction(float obstruction, float occlusion, float deltaT);
  void SetAccomodation(float accom);
  float GetAccomodation() const { return _accomodation; }

  void SetVolumeAdjust(float volEffect, float volSpeech, float volMusic);

  void Set3D(bool is3D, float distance2D = -1);
  bool Get3D() const;
  void SetRadio(bool isRadio);

  void EnableAccomodation(bool enable)
  {
    _enableAccomodation = enable;
    if(!enable) _accomodation = 1.0;
  }

  bool AccomodationEnabled() const { return _enableAccomodation; }

  void SetKind(SoundSystemXA2* sys, WaveKind kind);

  void SetSendList(const XAUDIO2_VOICE_SENDS &sendList);

#if _ENABLE_REPORT
  virtual RString GetDebugName() const = 0;
#endif

public:
  float GetMinDistance() { return GetMinDistance(); }
};

//////////////////////////////////////////////////////////////////////////

// Base xa2 filter pars extension.
struct FilterParamsEx: public XAUDIO2_FILTER_PARAMETERS
{
  bool _enable;
  bool _forceUpdate;
  DWORD _frequency;
  FilterTypeEx _typeEx;

  static const FilterParamsEx& Empty() { static FilterParamsEx empty = CreateEmpty(); return empty; }
  // operator to cast const XAUDIO2_FILTER_PARAMETERS*
  const XAUDIO2_FILTER_PARAMETERS* GetXA2ParsPtr() { return static_cast<const XAUDIO2_FILTER_PARAMETERS *>(this); }

  __forceinline bool IsEnabled() const { return _enable; }

private:
  static FilterParamsEx CreateEmpty()
  {
    FilterParamsEx pars;
    // extended pars
    pars._typeEx = NoFilter;
    pars._enable = false;
    pars._frequency = 44100;
    pars._forceUpdate = true;

    // XAUDIO2_FILTER_PARAMETERS - set to filter being fully bypassed
    pars.Type = LowPassFilter; 
    pars.Frequency = 1.0f;
    pars.OneOverQ = 1.0f;

    return pars;
  }
};

struct BaseAudioFilterEx
{
  float _lastDistToEmitor;

  BaseAudioFilterEx(): _lastDistToEmitor(1e6f) {}
  virtual bool operator() (FilterParamsEx &pars, float distToEmitor, bool forceUpdate) 
  {
    static float MinDist2ToEmitor = 1.0f;

    bool result = forceUpdate || fabsf(distToEmitor - _lastDistToEmitor) > MinDist2ToEmitor;
    if (result) 
    {
      _lastDistToEmitor = distToEmitor;
      pars._forceUpdate = false;
    }
    return result;
  }
};

//////////////////////////////////////////////////////////////////////////


/// XAudio2 sound buffer implementation of AbstractWave
class WaveXA2: public AbstractWave, public WaveBuffersXA2
{
  friend class XA2Submix;
  friend class SoundSystemXA2;
  friend class InternetRadioSystem2;

public:
  ///  describe basic properties of the sound
  struct Header
  {
    UINT _size;           // size of data in bytes.
    LONG _frequency;      // sampling rate
    int _nChannel;        // number of channels
    int _sSize;           // number of bytes per sample

    //ALenum GetALFormat() const;
  };

protected:

  SoundSystemXA2 *_soundSys;
  // low pass distance filter used for 3D samples  
  FilterParamsEx _parsEx;
  // enable/disable low pass filter
  bool _filterEnabled;
  // max distance where is emitter audible
  float _maxEmitterDist;
  /// wave header  
  Header _header;

  // wave format
  WAVEFORMATEX _waveFmt;

  bool _terminated;       // Is this one playing?
  bool _loadedHeader;     // Is this one loaded?
  bool _loaded;           // Is this one loaded?
  bool _loadError;        // Is this one loaded?
  bool _looping;

  float _skipped;   // time skipped before headers were loaded
  int _curPosition; // current playing position
  bool _posValid;   // 3d position explicitly set

  float _offset; // start position shift in bytes
  bool _applyOffset;

  // streaming audio variables
  Ref<WaveStream> _stream;

  /// current position in stream
  int _streamPos;

  /// buffers used in streaming, store audio data
  RefArray< XA2Buffer > _streamBuffers;

  /// which buffer we have already written data into
  int _writeBuf;

  /// count of free buffers - ready for writing
  int _freeBufN;

  /// number of buffers submitted into source voice
  int _bufQueued;

  /// get position from both _skipped and _curPosition
  float GetTimePosition() const;

public:
  enum Type { Free, Sw, Hw2D, Hw3D, NTypes };

protected:
  Type _type; 
  static int _counters[NTypes];
  static int _countPlaying[NTypes];

  void AddStats(const char *name);
  static void ReportStats();

public:
  static int TotalAllocated();
  static int Allocated(Type i) { return _counters[i]; }

protected:
  /// check preload size for current stream
  int InitPreloadStreamSize(int &initSize) const;
  /// size of the buffer for given format
  static int StreamingBufferSize(const WAVEFORMATEX &format, int totalSize);
  /// decompressing can be slow - avoid doing too much in one frame
  static int StreamingMaxInOneFill(const WAVEFORMATEX &format, int bufferSize);
  /// init decompressing can be even slower - avoid doing too much in one frame
  static int StreamingMaxInInitFill(const WAVEFORMATEX &format, int bufferSize);
  /// size of subsection - must be power of two
  static int StreamingPartSize(const WAVEFORMATEX &format);

  void DoConstruct();

  bool IsStreaming(WaveStream *stream);

  void DoUnload();
  /// load basic information (size...)
  bool LoadHeader();

  /// 2D and 3D sounds require a different context
  IXAudio2MasteringVoice *DesiredContext() const;

  /// once we know frequency, we want to convert time skipped into samples
  void ConvertSkippedToSamples();

  bool Load();

  void DoDestruct();

public:
  //WaveXA2();
  WaveXA2(SoundSystemXA2 *sSys, float delay); // empty wave
  WaveXA2(SoundSystemXA2 *sSys, RString name, bool is3D); // real wave
  ~WaveXA2();

  bool Duplicate(const WaveXA2 &wave); // duplicate buffer

  void CacheStore(WaveCacheXA2 &store);
  bool CacheLoad(WaveCacheXA2 &store);

  void InitRadio();

  bool DoStop(); // stop playing immediately
  /// start playing, with an optional delay
  bool DoPlay(int delayMs=0);
  /// check if wave stopped playing (useful for waves which have unload pending}
  void CheckPlaying();

  bool LoadError() const override { return _loadError; }

private:
  WaveXA2( const WaveXA2 &wave ); // duplicate buffer
  void operator = (const WaveXA2 &wave); // duplicate buffer

public:
  // set start sample, start is determined: offset * waveLength, range [0, 1]
  void SetStartOffset(float percentage);
  // return current position in percentage
  float GetCurrPosition() const;

  //void Queue(AbstractWave *wave,int repeat=1 );
  void Repeat(int repeat = 1);
  void Restart();

  //void AdvanceQueue();
  bool PreparePlaying(); // start playing
  void Play(); // start playing
  void Skip(float deltaT);
  void Advance(float deltaT); // skip if stopped
  float GetLength() const; // get wave length in sec
  void Stop(); // stop playing immediately
  /// implementation of AbstractWave::Unload
  virtual void Unload();
  /// private unload - can by sync or async
  void Unload(bool sync);
  /// we know it is stopped - unload as soon as possible
  void UnloadStopped(bool sync);

  void LastLoop(); // stop playing on the end of the loop
  void PlayUntilStopValue( float time ); // play until some time
  void SetStopValue( float time ); // play until some time

  WaveKind GetKind() const {return _kind;}
  void SetKind( WaveKind kind ) {WaveBuffersXA2::SetKind(_soundSys,kind);}

  float GetFileMaxVolume() const {return 1;}
  float GetFileAvgVolume() const {return 1;}

  void SetFrequency( float freq, bool imm );

  // set sound parameters
  void SetVolume( float volume, float freq, bool immediate=false );

  // set whistling params
  void SetEarWhistling(float whistling, bool immediate = true);
  bool CanCauseWhistling() const { return _enableWhistling; }
  void SetEWPars(bool enable, const Vector3 &pos, float maxDist);
  float WhistlingDist() const { return _whistlingMaxDistance; }
  Vector3 GetWhPos();

  // maximum audible distance
  void SetMaxDistance(float distance);
  float GetMaxDistance() const {return _maxEmitterDist;}

  void UpdatePosition();
  // set emitter position & velocity
  void SetPosition(Vector3Par pos, Vector3Par vel, bool immediate = false);

  void SetAccomodation(float accom = 1.0) { WaveBuffersXA2::SetAccomodation(accom); }
  void SetObstruction(float obstruction, float occlusion, float deltaT=0)
  {
    WaveBuffersXA2::SetObstruction(obstruction,occlusion,deltaT);
  }

  float GetObstruction() const {return _obstruction;}
  virtual float GetOcclusion() const {return _occlusion;}

  float GetAccomodation() const {return WaveBuffersXA2::GetAccomodation();}
  void EnableAccomodation( bool enable ) {WaveBuffersXA2::EnableAccomodation(enable);}
  bool AccomodationEnabled() const {return WaveBuffersXA2::AccomodationEnabled();}
  void EnableFilter(bool enable, FilterTypeEx fType) 
  { _parsEx._forceUpdate = _parsEx._typeEx != fType; _parsEx._enable = enable; _parsEx._typeEx = fType; }

  bool Loaded() const {return _loaded;}
  bool IsTerminated();
  bool CanBeDeleted();
  bool IsStopped();
  bool IsWaiting();

  // equivalent distance of 2D sounds for position disabled sounds
  // this is necessary for loudness calculation
  float Distance2D() const;

  void Set3D( bool is3D, float distance2D);
  bool Get3D() const;
  virtual void SetRadio(bool isRadio);

  RString GetDiagText();

  bool IsMuted() const {return WaveBuffersXA2::GetMuted();}
  float GetVolume() const {return WaveBuffersXA2::GetVolume();}
  float GetFrequency() const {return WaveBuffersXA2::GetFrequency();}
  Vector3 GetPosition() const { return WaveBuffersXA2::GetPosition(); }
  float GetLoudness() const;
  float GetMinDistance() const;

private:

  // un-queue oldest count buffers which are still queued
  void UnqueueStream(int count);

  void FillStream();
  bool FillStreamInit();

  // universal functions for both streaming / static sounds
  void StoreCurrentPosition();
  void AdvanceCurrentPosition(int skip);

  void Calculate3DAudio(bool immediate);

  HRESULT StopSourceVoice();
  HRESULT StartSubSourceVoice();


#if _ENABLE_REPORT
  RString GetDebugName() const {return Name();}
#endif
};

class WaveGlobalXA2: public WaveXA2
{
  typedef WaveXA2 base;

  Ref<WaveGlobalXA2> _queue; // next wave in queue
  bool _queued; // will be started by parent channel

public:
  //WaveGlobalXA2();
  ~WaveGlobalXA2();
  WaveGlobalXA2(SoundSystemXA2 *sSys, RString name, bool is3D);
  WaveGlobalXA2(SoundSystemXA2 *sSys, float delay);

private:
  WaveGlobalXA2(const WaveXA2 &wave); // duplicate buffer
  void operator = (const WaveXA2 &wave); // duplicate buffer

public:

  void AdvanceQueue();

  void Play();
  void Skip(float deltaT);
  void Advance(float deltaT);

  bool IsTerminated();

  void PreloadQueue();
  void Queue(AbstractWave *wave,int repeat = 1);

  USE_FAST_ALLOCATOR
};

class WaveDecoderXA2: public AbstractDecoderWave, public IXAudio2VoiceCallback
{
private:
  enum { BufferCount = 4 };
  SoundSystemXA2 *_sSys;
  IXAudio2SourceVoice *_source;
  bool _isPlaying;
  int _curr;
  int _free;
  bool _wantPlay;

  XA2Buffer _buffers[BufferCount];

public:
  WaveDecoderXA2(SoundSystemXA2 *sSys);
  ~WaveDecoderXA2();

  virtual bool InitSourceVoice(int channels, int rate);
  virtual bool FillBuffer(const void *data, size_t nbytes);
  virtual __int64 GetVoicePosition() const;
  virtual bool IsBuffersStopped() const;
  virtual void Pause(bool pause);
  virtual void Play();
  virtual void Stop();

private:
  void DestroySourceVoice();

  // Called when the voice has just finished playing a contiguous audio stream.
  void WINAPI OnBufferEnd(void *pBufferContext) { _free++; }

  // Not used
  void WINAPI OnBufferStart(void *pBufferContext) { }
  void WINAPI OnStreamEnd() { }
  void WINAPI OnVoiceProcessingPassEnd() { }
  void WINAPI OnVoiceProcessingPassStart(UINT32 SamplesRequired) { }
  void WINAPI OnLoopEnd(void *pBufferContext) { }
  void WINAPI OnVoiceError(void *pBufferContext, HRESULT Error) { }
};

#include <Es/Memory/debugNew.hpp>

/// buffer data stored when buffer is not active
struct WaveCacheXA2
{
  // file name used for sound wave identification
  RString _name;

  /// audio buffer
  XA2BufferRef _buffer;

  WaveXA2::Header _header;

  // source description
  WAVEFORMATEX _waveFmt;

  ~WaveCacheXA2();
};

TypeIsMovableZeroed(WaveCacheXA2)

#include <xaudio2fx.h>
#include <El/Time/time.hpp>
#include "global.hpp"

/// used for simulation of continuous pass between 3d to 2d in XA2Submix
class LinCross
{
private:
  Time _time;
  float _timePeriod;
  float _to;
  AutoArray<float> _from;
  bool _done;

public:
  void Commit(const AutoArray<float> &origVals, float to, float timePeriod = 1.0f);
  void Simulate(AutoArray<float> &vals);
};

/* Mastering voice + submix voice */
class MasteringVoice
{
  friend class SoundSystemXA2;
public:
  enum DestVoice { Voice3D = 0, Voice2D, VoiceMusic, Voice2DEx, Voice2DRadio, VoicePreview, CustomFilter, VoiceNotSet, VoiceCount };

private:
  IXAudio2MasteringVoice* _master;
  IXAudio2SubmixVoice* _submixSound;
  IXAudio2SubmixVoice* _submixSpeech;
  IXAudio2SubmixVoice* _submixRadio;
  IXAudio2SubmixVoice* _submixSpeechEx;
  IXAudio2SubmixVoice* _submixPreview;
  IXAudio2SubmixVoice* _submixCustom;
  bool _initSuccess;

  IXAudio2Voice *_mapTable[VoiceCount];

  float _volumeRadio;
  float _volumeRadioWanted;
  float _volumeSound;
  float _volumeSoundWanted;
  float _volumeSEffect;
  float _volumeSEffectWanted;
  float _globSoundVolume;
  float _globSoundVolumeWanted;

public:
  MasteringVoice() { InitializeVoice(); }
  ~MasteringVoice() { DestroyVoice(); }

  void Simulate(float deltaT, bool inside);
  bool CreateVoice(IXAudio2 *device, DWORD deviceIndex, WAVEFORMATEX &fmt);

  void SetGlobVolume(float volume) { saturate(volume, 0.0f, 1.0f); _globSoundVolumeWanted = volume; }
  void SetRadioVolume(float volume) { saturate(volume, 0.0f, 10.0f); _volumeRadioWanted = volume; }
  void SetSoundVolume(float volume) { saturate(volume, 0.0f, 1.0f); _volumeSoundWanted = volume; }
  void SetSpecEffectVolume(float volume) { saturate(volume, 0.0f, 10.0f); _volumeSEffectWanted = volume; }

private:
  void InitializeVoice();  
  void DestroyVoice();

  // Select output voice
  __forceinline IXAudio2Voice* SendMasterVoice(DestVoice voice) const;
};

TypeIsMovableZeroed(XAUDIO2_SEND_DESCRIPTOR)

typedef AutoArray<XAUDIO2_SEND_DESCRIPTOR, MemAllocLocal<XAUDIO2_SEND_DESCRIPTOR, 8> > SendOutputVoices;

class RadioStream2 : public OggStreamBuffer
{
  static const DWORD MaxInactivityTime;  
  float _activityCheckStartTime;
public:
  // the only one Wave consumes the stream buffer data
  RefAbstractWave _wave;
  // for each itemRadio tuned to this radio there is submix playing it
  SendOutputVoices _outputVoices;
  RefArray<AbstractSubmix> _submixes;
  bool valid;
  
  void Include(AbstractSubmix *submix);
  void Exclude(AbstractSubmix *submix);
  bool CheckValid(bool simEnabled);  // radio with no submix should be invaliated after given time
  RadioStream2(void *curl, const char *url) : OggStreamBuffer(curl, url), valid(true), _activityCheckStartTime(0) {}
  ~RadioStream2(); // deletes itself also from the global InternetRadioSystem2 GRS2
};

RadioStream2* AddInternetRadio(const char *url);

class SoundSystemXA2;

// Submix used fro encapsulation of sourcev voices
class XA2Submix: public AbstractSubmix
{
protected:
  SoundSystemXA2* _sSystem;
  // submix voice
  IXAudio2SubmixVoice* _submix;
  // list used for setting input voices output
  XAUDIO2_VOICE_SENDS _sendList;
  // define a destination voice
  XAUDIO2_SEND_DESCRIPTOR _sendDesrc;
  // emitter settings
  X3DAUDIO_EMITTER _emitter;
  // directional sound
  X3DAUDIO_CONE _cone;
  // Receives the results from a call to X3DAudioCalculate to be sent to the low-level audio rendering function for 3D signal processing.
  X3DAUDIO_DSP_SETTINGS _dsp;
  // switch from 3d to 2d
  LinCross _lCross;
  // filter params
  FilterParamsEx _filterParsEx;

private:
  // number of voices included into submix
  int _count;
  // input channels count
  int _iChannelsCount;
  // submix voice created
  bool _submixReady;
  // structures initialization flag
  bool _initialized;
  // is directional sound enable
  bool _useCone;
  // channel positions
  AutoArray<float> _channelAzimuth;
  // volume level of each source channel 
  AutoArray<float> _matrixCoefficients;
  // 2d mode
  bool _linCrossSet;
  // output voice/submix
  MasteringVoice::DestVoice _destMasteringVoice;
  //
  DWORD _parsTime;

public:
  XA2Submix(SoundSystemXA2 *sSystem): _initialized(false) { Construct(sSystem); }
  ~XA2Submix() { Destruct(); }

public:
  void Unload() { Destruct(); }
  // Set source voice output into submix
  void Include(AbstractWave *wave, bool noSet=false);
  // Remove source voice output from submix
  void Exclude(AbstractWave *wave, bool noSet=false);
  // Calculate 3d sound
  float Calculate3D(bool enable3D, const Vector3 &position, const Vector3 &speed, float distance, float vol);
  // Enable directional sound for submix
  void EnableDirectionalSound(bool enable);
  // Set filter type
  virtual FilterTypeEx SetFilter(FilterTypeEx filter);
  // Set sound cone
  __forceinline virtual void SetOrientation(Vector3Val top, Vector3Val front)
  {
    VEC3_2_X3DVEC(top, _emitter.OrientTop);
    VEC3_2_X3DVEC(front, _emitter.OrientFront);
  }
  // Set cone params
  void SetDirSoundConePars(float innerAngle, float outerAngle, float innerVolume, float outerVolume);
  // Set submix output
  virtual void SetOutputVoiceSound(SubmixOutput sOut);
  // 
  virtual int WaveCount() const { return _count; }

  virtual void *GetHandle() {return _submixReady ? _submix : NULL; }

  void ReportVolume() const 
  { 
    if (_matrixCoefficients.Size()>=2)
      LogF("submix: %x vol=%.2f,%.2f", this, _matrixCoefficients[0], _matrixCoefficients[1]); 
  }
protected:
  // Initialize structures
  bool Construct(SoundSystemXA2 *sSystem);
  // Create IXAudio2SubmixVoice
  bool CreateSubmix();
  // Release XA2Submix
  void Destruct();
  // Select output voice
  __forceinline IXAudio2Voice* SelectDestinationVoice();
};

enum VoNResult;
class SoundSystemXA2: public RefCount, public AbstractSoundSystem, public MemoryFreeOnDemandHelper
{
  friend class WaveXA2;
  friend class WaveDecoderXA2;
  friend class WaveBuffersXA2;
  friend class VoNSoundBufferXA2;
  friend class PlayerVoice3D;
  friend class PlayerVoice2D;
  friend class XA2Submix;
  friend float GetTransmitterLoudness(PlayerVoice3D *voice, Vector3Par listenerPos);

private:
  LinkArray<WaveXA2> _waves; // store all playing waves

  AutoArray<WaveCacheXA2> _cache;

  //@{ volume settings as wanted by the user
  float _waveVolWanted;
  float _musicVolWanted;
  float _speechVolWanted;
  float _vonVolWanted;
  float _vonRecThrWanted;
  float _vonRecThrAutoWanted;
  //@}

  //@{ relative volume adjust values - what is really applied
  float _volumeAdjustEffect;
  float _volumeAdjustSpeech;
  float _volumeAdjustVoN;
  float _volumeAdjustMusic;
  //@}

  // XA2 interface
  ComRef<IXAudio2> _xa2Device;

  // 3D mastering voice
  IXAudio2MasteringVoice* _context3D;
  // output wrapper
  MasteringVoice _masteringVoice;
  //
  XAUDIO2_FILTER_PARAMETERS _customFilterPars;
  // 
  bool _customSubmixFilter;

  // sound environment
  SoundEnvironment _environment;
  // reverb used for environment simulation
  IUnknown* _reverb;
  // reverb pars
  //XAUDIO2FX_REVERB_I3DL2_PARAMETERS _revI3DL2Pars;
  XAUDIO2_EFFECT_CHAIN _effectChain;
  XAUDIO2_EFFECT_DESCRIPTOR _revDescriptor[1];
  bool _reverbEnable;
  int _revIndex;

  /// sometimes we need to suspend all sounds (like when not having focus)
  bool _active;

  __declspec(align(16)) X3DAUDIO_LISTENER _listener;
  // X3DAudio instance handle
  X3DAUDIO_HANDLE _x3dInstance;
  // output format - master voice
  WAVEFORMATEX _outFmt;
  // X3DAudioCalculate flags
  DWORD _x3dCalculateFlags;

  DWORD _listenerTime;

  DWORD _minFreq,_maxFreq; // valid freq. range

  int _maxChannels;

  // used for microphone sensitivity auto adjustments
  WaveCapture _wCapture;
  bool _captureRunning;
  UITime _captureTime;
  AutoArray<int16> _cVals;

#if _ENABLE_PERFLOG
  int _wavSV;
  int _wavSD;
  int _wavSI;
  int _wavLD;
  int _wavLI;
  int _wavSt;
  int _wavPl;
#endif

  bool _commited; // do not play anything until all is initialized (2nd frame)
  bool _soundReady;
/*
  /// specifier which should be used when HW acceleration is enabled
  RString _hwSpecifier;
  /// specifier which should be used when HW acceleration is disabled
  RString _swSpecifier;
  /// speficier for the native device - if present, used instead of generic HW
  RString _nativeSpecifier;*/

  // preview state variables
  bool _doPreview;
  DWORD _previewTime;

  RefAbstractWave _previewSpeech;
  RefAbstractWave _previewEffect;
  RefAbstractWave _previewMusic;
  bool _enablePreview; // preview disabled until sound initialized
  bool _prevSpSampleExist;
  bool _prevEfSampleExist;
  bool _prevMuSampleExist;
  bool _cleanupCOM;

  mutable CriticalSection _lock;

  int _maxSoundsPlayed;

public:
  void New(HWND win,bool dummy);
  void Delete();
  SoundSystemXA2();
  SoundSystemXA2(HWND hwnd, bool dummy);
  ~SoundSystemXA2();

  IXAudio2 *XA2Device() const { return _xa2Device; }

  CriticalSection &GetLock() const { return _lock; }

  // return state after enable
  virtual bool EnableHWAccel(bool val);
  virtual bool EnableEAX(bool val);

  // get if feature is enabled
  virtual bool GetEAX() const;
  virtual bool GetHWAccel() const;
  virtual bool GetDefaultEAX() const;
  virtual bool GetDefaultHWAccel() const;

  virtual bool CanChangeEAX() const;
  virtual bool CanChangeHW() const;

  void LoadConfig(ParamEntryPar config);
  void SaveConfig(ParamFile &config);

  bool LoadHeaderFromCache( WaveXA2 *wave );
  bool LoadFromCache( WaveXA2 *wave );
  void StoreToCache( WaveXA2 *wave );
  void ReserveCache( int number3D, int number2D );

  void LogChannels( const char *name );

  float GetWaveDuration( const char *filename );
  // create normal or streaming wave
  void AddWaveToList(WaveGlobalXA2 *wave);
  AbstractWave *CreateWave(const char *name, bool is3D = true, bool prealloc = false);
  // create empty wave
  AbstractWave *CreateEmptyWave( float duration ); // duration in ms

  AbstractDecoderWave *CreateDecoderWave();

  AbstractSubmix *CreateSubmix();
  void Activate( bool active );

  /// estimate loudness of a sound before creating it
  float GetLoudness(WaveKind kind, Vector3Par pos, float volume, float accomodation) const;

  virtual void SetEnvironment(const SoundEnvironment &env); // TODO:

  void DoSetEAXEnvironment();
  void DoSetEAXEnvironment(XAUDIO2FX_REVERB_I3DL2_PARAMETERS &pars);
  void DoSetEAXEnvironment(XAUDIO2FX_REVERB_PARAMETERS &pars);

  void SetCDVolume(float val);
  float GetCDVolume() const;
  float GetDefaultCDVolume() const {return 6.5f;}

  void SetWaveVolume(float val );
  float GetWaveVolume();
  float GetDefaultWaveVolume() {return 8.5f;}

  void SetSpeechVolume(float val );
  float GetSpeechVolume();
  float GetDefaultSpeechVolume() {return 5.5f;}

  void SetVonVolume(float val);
  float GetVonVolume() const {return _vonVolWanted;}
  float GetDefaultVoNVolume() const {return 6.5f;}

  /// threshold fore recorded data to be considered of voice (values 0.0f - 1.0f - lower value = more data are considered as voice)
  virtual void SetVoNRecThreshold(float val);
  virtual float GetVoNRecThreshold() const {return _vonRecThrWanted;}
  virtual float GetDefaultVoNRecThreshold() const {return 0.03f;}

  // microphone sensitivity adjustments
  void StartCapture(float time);
  void StopCapture();
  bool CaptureRunning() const { return _captureRunning; }
  float GetThreasholdWanted() const { return _vonRecThrAutoWanted; }
  void ProcessCapturedData();

  float GetVolumeAdjustEffect() const {return _volumeAdjustEffect;}
  float GetVolumeAdjustSpeech() const {return _volumeAdjustSpeech;}
  float GetVolumeAdjustMusic() const {return _volumeAdjustMusic;}
  float GetVolumeAdjustVoN() const {return _volumeAdjustVoN;}

  void UpdateMixer();
  void PreviewMixer();

  void StartPreview();
  void TerminatePreview();

  void FlushBank(QFBank *bank);

  void SetListener(Vector3Par pos, Vector3Par vel, Vector3Par dir, Vector3Par up);
  Vector3 GetListenerPosition() const {return Vector3(_listener.Position.x, _listener.Position.y, _listener.Position.z); }

  void Commit();

  void SimulateMasterVoice(float deltaT, bool inside) { _masteringVoice.Simulate(deltaT, inside); }
  void UpdateRadioSubmixes(bool simEnabled);
  __forceinline void UpdateGlobalVolume(float volume) { _masteringVoice.SetGlobVolume(volume); }
  __forceinline void UpdateGlobalSoundVolume(float volume) { _masteringVoice.SetSoundVolume(volume); }
  __forceinline void UpdateGlobalRadioVolume(float volume) { _masteringVoice.SetRadioVolume(volume); }
  __forceinline void UpdateGlobalSpeechExVolume(float volume) { _masteringVoice.SetSpecEffectVolume(volume); }

  __forceinline IXAudio2Voice* DestinationVoiceSound() const { return _masteringVoice.SendMasterVoice(MasteringVoice::Voice3D); }
  __forceinline IXAudio2Voice* DestinationVoice(MasteringVoice::DestVoice voice) const { return _masteringVoice.SendMasterVoice(voice); }

  virtual void SetCustomVoiceFilter(bool set);

private:
  void CreateDevice();
  void DestroyDevice();

  // unload all waves, destroy and create a device, load waves again
  void ResetDevice();

public:
  // implementation of MemoryFreeOnDemandHelper
  virtual float Priority() const;
  virtual RString GetDebugName() const;
  virtual size_t MemoryControlled() const;
  virtual size_t FreeOneItem();


  //@{ samples count
  virtual int GetSamplesCountMin() const { return 16; }
  virtual int GetSamplesCountMax() const { return 128; }
  virtual int GetSamplesCount() const { return _maxSoundsPlayed; }
  virtual int GetSamplesCountDefault() const { return 32; }
  virtual void SetSamplesCount(int val);
  //@}
};

/// Conversion of "gain" to MiliBel (used also in VoiceOAL)
int Float2MiliBel( float val );

#endif
