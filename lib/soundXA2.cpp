/**
@file XAudio2 sound system
*/

#if defined _WIN32

#include "wpch.hpp"
#include "Network/networkImpl.hpp"
#include "soundXA2.hpp"

#include "keyInput.hpp"
#include "global.hpp"
#include "textbank.hpp"
#include "engine.hpp"
#include "paramFileExt.hpp"
#include "Network/network.hpp"

#include "dikCodes.h"
#include "diagModes.hpp"
#include <El/FileServer/fileServer.hpp>
#include <El/QStream/qbStream.hpp>
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include <El/FreeOnDemand/memFreeReq.hpp>
#include <El/Breakpoint/breakpoint.h>
#include <Es/Containers/rStringArray.hpp>
#include <Es/Containers/array.hpp>
#include <El/Evaluator/express.hpp>

#include "Es/Common/langExt.hpp"
#define ENABLED_NOOPT_Bebul 1
#if ENABLED_FEATURE(NOOPT)
#if _ENABLE_CHEATS
#pragma optimize ("", off)
#endif
#endif

#if defined(_WIN32) 
#include <mmsystem.h>
#include "DelayImp.h"
#endif

#define RELEASE_SUBMIX 0

#define DIAG_VOL 0
#define DIAG_STREAM 0

#define USE_ENV 0
#define ENABLE_REVERB 0
#define START_DISABLE_REVERB 0

#if DIAG_STREAM
#define TRACKED_SAMPLE "ca\\dubbing\\c0_firsttofight\\shaftoe_briefing\\shaftoe_briefing_h_3.wss"
#define METHOD_TRACKER(sample, info) if (!strcmpi(sample, TRACKED_SAMPLE)) { LogF("%s", #info); }
#else
#define METHOD_TRACKER(sample, info)
#endif

#pragma comment(lib, "X3daudio.lib")

#define DO_PERF 0
#if DO_PERF
# include "perfLog.hpp"
#endif

// left handled coordination system, axis y is up
static D3DVECTOR TopVec = { 0, 1, 0 };
// no directional sound
static D3DVECTOR FrontVec = { 0, 0, 1 };
// volume curve definition
static const X3DAUDIO_DISTANCE_CURVE_POINT Emitter_Volume_CurvePoints[4] = { 0.0f, 1.0f, 0.2f, 0.2f, 0.5f, 0.1f, 1.0f, 0.0f };
static const X3DAUDIO_DISTANCE_CURVE       Emitter_Volume_Curve          = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_Volume_CurvePoints[0], 4 };

static const X3DAUDIO_DISTANCE_CURVE_POINT Emitter_LFE_CurvePoints[5] = { 0.0f, 0.55f, 0.0625f, 0.25f, 0.125f, 0.1f, 0.25f, 0.05f, 1.0f, 0.0f };
static const X3DAUDIO_DISTANCE_CURVE       Emitter_LFE_Curve          = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_LFE_CurvePoints[0], 5 };

void WaveXA2::DoConstruct()
{
  PROFILE_SCOPE_EX(xa2DC, sound);

  _type = Free;
  _soundSys = NULL;
  _header._size = 0;            // Size of data.
  _header._frequency = 0;       // sampling rate
  _header._nChannel = 0;        // number of channels
  _header._sSize = 0;           // number of bytes per sample
  _playing = false;             // Is this one playing?
  _wantPlaying = false;
  _loaded = false;
  _loadedHeader = false;
  _loadError = false;
  _only2DEnabled = false;
  _looping = true;
  _terminated = false;
  _sourceOpen = false;
  _offset = 0.0;
  _applyOffset = false;
  _filterEnabled = false;
  _parsEx = FilterParamsEx::Empty();
  _maxEmitterDist = 0;
  //Assert(_source); // _source is uninitialized - non-null only by chance
  _source = NULL;
  memset(&_emitter, 0, sizeof(X3DAUDIO_EMITTER)); // position, velocity etc.
  _emitter.OrientTop = TopVec;
  _emitter.OrientFront = FrontVec;
  _emitter.DopplerScaler = 1;
  _emitter.pVolumeCurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_Volume_Curve;
  _emitter.pLFECurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_LFE_Curve;

  memset(&_sendList, 0, sizeof(XAUDIO2_VOICE_SENDS));
  memset(&_sendDesc, 0, sizeof(XAUDIO2_SEND_DESCRIPTOR));

  _freeBufN = 0;
  _bufQueued = 0;
  _writeBuf = 0;

  _volume = 1.0;
  _accomodation = 1.0;
  _volumeAdjust = 1;
  _enableAccomodation = true;

  _stopTreshold = FLT_MAX; // never stop

  _obstructionSet = _occlusionSet = 0;
  _obstruction = _occlusion = 1;
  _gainSet = 1;
  _freqSet = 1;
  _curPosition = 0;
  _skipped = 0;
}

WaveGlobalXA2::~WaveGlobalXA2() {}

#define DIAG_LOAD 0

WaveGlobalXA2::WaveGlobalXA2(SoundSystemXA2 *sSys, float delay)
:WaveXA2(sSys,delay)
{
  _queued = false;
}

WaveGlobalXA2::WaveGlobalXA2(SoundSystemXA2 *sSys, RString name, bool is3D)
:WaveXA2(sSys,name,is3D)
{
  _queued = false;
}

DEFINE_FAST_ALLOCATOR(WaveGlobalXA2)

/*!
\patch 5160 Date 5/17/2007 by Ondra
- Fixed: Improved radio protocol fluency by binding words together.
*/
void WaveGlobalXA2::AdvanceQueue()
{
  if(!_queue) return;
  if(_terminated)
  {
    _queue->_queued = false;
    _queue->DoPlay();
    _queued = true;
    //LogF("Sound %s activated",(const char *)_queue->Name());
    OnTerminateOnce();
  }
  // even before terminated we could preload and schedule the following sample
  else if (_playing && _queue->PreparePlaying())
  {
    // check how much is left from current sample to play
    // if it is less than a frame, start immediately
    // moreover, we want some overlap as well, to prevent sounding robotic
#if _ENABLE_CHEATS
    static int maxFrameDuration = 100;
    static int overlapCoef = 35; // 8
    const int realOverlap = 100; // how much do we really want the words to overlap
#else
    const int maxFrameDuration = 100; // max. frame duration for estimations
    const int overlapCoef = 35; // x/16
    const int realOverlap = 75; // how much do we really want the words to overlap
#endif
    int lastFrame = GEngine->GetLastFrameDuration();
    int avgFrame = GEngine->GetAvgFrameDuration(3);
    if (lastFrame>avgFrame*2) lastFrame = avgFrame*2;
    if (lastFrame>maxFrameDuration) lastFrame = maxFrameDuration;

    int overlap = ((lastFrame*overlapCoef)>>4) + realOverlap;

    // DIAG_MESSAGE(100, Format("Overlap: %d", overlap));

    // note: maths will not be accurate when playing 3D due to Doppler effect
    // TODO: check sample position

    // estimate when current sample will stop
    DWORD durationMs = toLargeInt((_header._size/_header._sSize)*1000/(_header._frequency*_freqSet));
    DWORD endTime = _startTime + durationMs;
    DWORD currentTime = GlobalTickCount();
    if (currentTime>endTime-overlap)
    {
      if (!_queue->_playing)
      {
#if 0
        if (_queue->Name().GetLength()>0)
        {
          LogF("%d:  Prestart: %s",GlobalTickCount(),cc_cast(_queue->Name()));
        }
#endif
        _queue->DoPlay();
      }
    }


  }
}

void WaveGlobalXA2::Play()
{
  if(_queued) return;

  // check termination
  IsTerminated();

  AdvanceQueue();
  
  base::Play();
}

void WaveGlobalXA2::Skip(float deltaT)
{
  if(_queued) return;

  // check termination
  IsTerminated();

  AdvanceQueue();

  base::Skip(deltaT);
}

void WaveGlobalXA2::Advance(float deltaT)
{
  if( _playing || _wantPlaying && GetTimePosition()>=0 && _soundSys->XA2Device()) return;
  Skip(deltaT);
}

void WaveGlobalXA2::PreloadQueue()
{
  if (_queue)
  {
    _queue->PreparePlaying();
  }
}

void WaveGlobalXA2::Queue(AbstractWave *wave, int repeat)
{
  WaveGlobalXA2 *after = static_cast<WaveGlobalXA2 *>(wave);
  //Assert( after->_repeat>0 );
  Assert( repeat>0 );
  while(after->_queue) after = after->_queue;
  after->_queue = this;
  /*
  LogF
  (
  "Queue %s after %s",
  (const char *)Name(),(const char *)after->Name()
  );
  */
  _queued = true;
  Repeat(repeat);
}

bool WaveGlobalXA2::IsTerminated()
{
  if( !base::IsTerminated() ) return false;
  for( WaveGlobalXA2 *next = this; next; next = next->_queue )
  {
    if( !next->_terminated ) return false;
  }
  OnTerminateOnce();
  return true;
}

#define DEFERRED 1

WaveBuffersXA2::WaveBuffersXA2()
//:_position(VZero),_velocity(VZero)
{
  D3DVECTOR vec = { 0, 0, 0 };
  _emitter.Position = vec;
  _emitter.Velocity = vec;

  _3DEnabled = true;
  _distance2D = 1;
  _isRadio = false;
  _whistling = 1;
  _enableWhistling = false;
}

WaveBuffersXA2::~WaveBuffersXA2()
{
  Assert(_buffer.IsNull());
  Assert(!_source);
}

void WaveBuffersXA2::Reset(SoundSystemXA2 *sys)
{
  // TODO: force setting all parameters
  _gainSet = 1;

  memset(&_emitter, 0, sizeof(X3DAUDIO_EMITTER));

  _volume = 1; // volume used for 3D sound
  _accomodation = 1; // current ear accomodation setting
  _obstructionSet = _occlusionSet = 0;
  _obstruction = _occlusion = 1;
  _volumeAdjust = sys->_volumeAdjustEffect;

  _kind = WaveEffect;
  _3DEnabled = true;
  _isRadio = false;
  _parsTime = 0; // force parameter reset
}

static inline float DistanceRolloff(float minDistance, float distance)
{
  return minDistance/distance;
}

#if _ENABLE_PERFLOG
#define WAV_COUNTER(name,value) \
  ADD_COUNTER(name,value); \
  static_cast<SoundSystemXA2 *>(GSoundsys)->_##name++;
#define SND_COUNTER(name,value) \
  ADD_COUNTER(name,value); \
  _##name++;
#else
#define WAV_COUNTER(name,value)
#define SND_COUNTER(name,value)
#endif

void WaveBuffersXA2::SetGain(float gain)
{
  if (!_playing || !_sourceOpen)
  {
#if DIAG_VOL
    //LogF("PresetVolume %s %d",(const char *)Name(),volumeDb);
    LogF("PresetVolume %x %d",this,volumeDb);
#endif
    _gainSet = gain;
    return;
  }

  // input is multiplicative gain
  static float minRelDiff = 1e-2f;
  if( fabs(gain - _gainSet) > minRelDiff * gain )
  {
    WAV_COUNTER(wavSV,1);
    HRESULT hr = _source->SetVolume(gain);
    //LogF("setVolume: %.4f", gain);
    CHECK_XA2_ERROR(hr, "WaveBuffersXA2::SetGain-SetVolume");

#if DIAG_VOL
    //LogF("PP: SetVolume %s %d",(const char *)Name(),volumeDb);
    LogF("PP: SetVolume %x %d",this,volumeDb);
#endif

    _gainSet = gain;
  };
}

void WaveXA2::DoUnload()
{
  if(!_loaded) return;

  PROFILE_SCOPE_EX(wavUL, sound);

  Assert(GSoundsys == _soundSys);
  Assert(_soundSys->_soundReady);

  if (_sourceOpen)
  {
    // TODO: we may store also streaming buffer
    // so that they can be reused
    if (!_stream)
    {
      _soundSys->StoreToCache(this);
    }
  }

#if DO_PERF
  if (_sourceOpen)
  {
    ADD_COUNTER(wavUL,1);
  }
#endif

  // unbind the buffer
  if (_sourceOpen)
  {
    HRESULT hr = StopSourceVoice();
    CHECK_XA2_ERROR(hr, "WaveXA2::DoUnload");

    // release audio data buffer
    _buffer.Free();
  }

  if (_streamBuffers.Size() > 0)
  {
    for (int i = 0; i < _streamBuffers.Size(); i++)
    {
      Ref<XA2Buffer> &buff = _streamBuffers[i];
      buff->Clear();
      buff = NULL;
    }
    _streamBuffers.Resize(0);
  }

  if (_sourceOpen)
  {
    // Destroy the voice. If necessary, stops it and removes it from the XAudio2 graph.
    LogXA2("Destroy source voice %d", _source);
    Assert(_source);
    //_source->FlushSourceBuffers(); 
    _source->DestroyVoice();
    _source = NULL;
    _sourceOpen = false;
  }

  _loaded = false;
  _stream.Free();

  _counters[_type]--;

  _type = Free;
  ReportStats();
}


void WaveXA2::DoDestruct()
{
  PROFILE_SCOPE_EX(wavDs, sound);
  // check if we are still playing
  CheckPlaying();
  // playing buffer should never be terminated, as this can be blocking (Xbox)
  //Assert(!_playing || _terminated);
  //DoStop();
  // unload must destroy the wave
  Unload(true);
  //DoUnload();
  // note: if Free is called immediately after Stop, it may be blocking
}


int WaveXA2::_counters[NTypes];
int WaveXA2::_countPlaying[NTypes];

void WaveXA2::AddStats( const char *name )
{
  if (!_sourceOpen) return;
  _type = _only2DEnabled ? Hw2D : Hw3D;

  _counters[_type]++;
  ReportStats();
}

void WaveXA2::ReportStats()
{
  /*
  char buf[256];
  strcpy(buf,"Snd");
  for( int i=1; i<NTypes; i++ )
  {
  sprintf(buf+strlen(buf)," %d",_counters[i]);
  }
  GEngine->ShowMessage(1000,buf);
  */
}

int WaveXA2::TotalAllocated()
{
  int total=0;
  for (int i = 1; i < NTypes; i++ ) total += _counters[i];
  return total;
}

void WaveXA2::ConvertSkippedToSamples()
{
  float skipSamples = _skipped * _header._frequency;
  if (skipSamples * _header._sSize > _header._size)
  {
    if (_looping)
    {
      int sampleSize = _header._size / _header._sSize;
      float newPos = fastFmod(skipSamples, sampleSize);
      _curPosition += toInt(newPos) * _header._sSize;
    }
    else
    {
      _curPosition = _header._size;
      // if the whole sound is already skipped, we want to terminate the sound
      _terminated = true;
    }
  }
  else
  {
    _curPosition += toLargeInt(_skipped * _header._frequency) * _header._sSize;
  }
  _skipped = 0;
}

/*!
\patch 5099 Date 12/8/2006 by Ondra
- Fixed: Dedicated server required OpenAL32.dll
*/
bool WaveXA2::LoadHeader()
{
  PROFILE_SCOPE_EX(sndLH,sound);

  if (_loadedHeader || _header._sSize > 0) return true;

  /// empty sound (pause) should never be present here
  Assert(Name().GetLength() > 0);

  _soundSys->_waves.RemoveNulls();

  // before requesting the file we should check if we are able to get the header from existing sounds
  // try to load properties from some existing sound buffer
  for (int i = 0; i < _soundSys->_waves.Size(); i++)
  {
    WaveXA2 *wave = _soundSys->_waves[i];
    if (!wave->_loadedHeader) continue;
    if (wave == this) continue;
    if (!strcmp(wave->Name(), Name()))
    {
      _header = wave->_header;
      _loadedHeader = true;
      ConvertSkippedToSamples();
      return true;
    }
  }

  if (_soundSys->LoadHeaderFromCache(this))
  {
    _loadedHeader = true;
    ConvertSkippedToSamples();
    return true;
  }

  Ref<WaveStream> stream;
  // only header is needed - whole files are cached
  DoAssert(RString::IsLowCase(Name()));

#define REQUEST_TIME 0

#if REQUEST_TIME
  LARGE_INTEGER t0;
  QueryPerformanceCounter(&t0);
#endif

  if (!SoundRequestFile(Name(), stream))
  {
#if REQUEST_TIME
    LARGE_INTEGER dt;
    QueryPerformanceCounter(&dt);
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    double dTime = (dt.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
    if (dTime > 0.001) LogF("SoundRequestFile: %f, %s", dTime, cc_cast(Name()));
#endif

    return false;
  }

  if (!stream)
  {
    RptF("Cannot load sound '%s'", (const char *)Name());
    _loadError = true;
    return false;
  }

  _loadedHeader = true;

  WAVEFORMATEX format;
  stream->GetFormat(format);
  _header._frequency = format.nSamplesPerSec;     // sampling rate
  _header._nChannel = format.nChannels;           // number of channels
  _header._sSize = format.nBlockAlign;            // number of bytes per sample
  _header._size = stream->GetUncompressedSize();

  if (_soundSys->_xa2Device)
  {
    Assert(_header._size > 0);
    if (_header._size <= 0) RptF("Warning: %s has ZERO data size", cc_cast(Name()));

    Assert(format.wFormatTag == WAVE_FORMAT_PCM);
    if (format.wFormatTag != WAVE_FORMAT_PCM)
    {
      RptF("Error: %s is corrupted", cc_cast(Name()));
      _loadError = true;
      return false;
    }

    if (format.nChannels < 1 && format.nChannels > 2)
    {
      RptF("Error: sample %s has unsupported channels count: %d", cc_cast(Name()), _header._nChannel);
      _loadError = true;
      return false;
    }

#if _ENABLE_REPORT
    // check sample frequency (recommended)
    if ((DWORD)_header._frequency > 44100)
    {
      LogF("Warning: %s has frequency %d", cc_cast(Name()), _header._frequency);
    }
#endif

  }

  ConvertSkippedToSamples();

  return true;
}

#define DISABLE_HW_ACCEL 0
#define ENABLE_VOICEMAN 1

const int StreamingAtomPart = 16*1024;

int WaveXA2::StreamingBufferSize(const WAVEFORMATEX &format, int totalSize)
{
  int bytesPerSecond = format.nAvgBytesPerSec;
  const int keepUnder = 512*1024/StreamingAtomPart*StreamingAtomPart;
  // never buffer less that 1 second
  if (bytesPerSecond > keepUnder) return (bytesPerSecond + StreamingAtomPart - 1)&~(StreamingAtomPart-1);
  // we want the buffer 2 seconds long
  const int seconds = 2;
  int minSize = bytesPerSecond*seconds;
  if (minSize>totalSize) minSize = totalSize;
  if (minSize>keepUnder) return keepUnder;
  int size=StreamingAtomPart*4;
  while (size<minSize)
  {
    size += StreamingAtomPart*4;
  }
  return size;
}

int WaveXA2::StreamingMaxInOneFill(const WAVEFORMATEX &format, int bufferSize)
{
  int bytesPerSecond = format.nAvgBytesPerSec;
  int quarterBuff = bytesPerSecond/4;
  int size = StreamingAtomPart;
  while (size<quarterBuff)
  {
    size *= 2;
  }
  if (size>StreamingAtomPart*4) return StreamingAtomPart*4;
  return size;
}

int WaveXA2::StreamingMaxInInitFill(const WAVEFORMATEX &format, int bufferSize)
{
  int bytesPerSecond = format.nAvgBytesPerSec;
  int quarterBuff = bytesPerSecond/4;
  int size = StreamingAtomPart;
  while (size<quarterBuff)
  {
    size *= 2;
  }
  if (size>StreamingAtomPart*8) return StreamingAtomPart*8;
  return size;
}

int WaveXA2::StreamingPartSize(const WAVEFORMATEX &format)
{
  return format.nChannels * StreamingAtomPart;
}

/* Frequency adjustment ratio. This value must be between XAUDIO2_MIN_FREQ_RATIO and the MaxFrequencyRatio. */
// change to 0.25 - no audio distortion when wrong speed set in sound-audio sync.
static const float MinFrequency = 0.25f; //1.0f/64.0f; //XAUDIO2_MIN_FREQ_RATIO; 
static const float MaxFrequency = 10.0f;      //XAUDIO2_MAX_FREQ_RATIO;
static inline void SaturateFrequency(float &freq) { saturate(freq, MinFrequency, MaxFrequency); };

//static __forceinline const float* GetOutputMatrix(int scrChannels, int destChannels);

/* Select output submix */
static __forceinline IXAudio2Voice* SelectOutputVoice(SoundSystemXA2 *sSystem, WaveKind kind, bool isRadio, bool isSticky)
{
  if (isSticky) return sSystem->DestinationVoice(MasteringVoice::VoicePreview);
  if (kind == WaveEffect) return sSystem->DestinationVoice(MasteringVoice::Voice3D);
  else if (kind == WaveSpeech)
  {
    return isRadio ? sSystem->DestinationVoice(MasteringVoice::Voice2DRadio) : sSystem->DestinationVoice(MasteringVoice::Voice2D);
  }
  else if (kind == WaveSpeechEx) return sSystem->DestinationVoice(MasteringVoice::Voice2DEx);

  return sSystem->DestinationVoice(MasteringVoice::VoiceNotSet);
}

static __forceinline bool SetOutputMatrix(IXAudio2Voice *voice, IXAudio2Voice *destVoice, DWORD srcChannels, DWORD destChannels, float* matrix)
{
  HRESULT hr = voice->SetOutputMatrix(destVoice, srcChannels, destChannels, matrix);

  if ( FAILED(hr) )
  {
    RptF("Error: SetOutputMatrix failed %x, srcChannels: %d, destChannels: %d", voice, srcChannels, destChannels);
    return false;
  }

  return true;
}

/* Create source voice function wrapper */
static __forceinline bool CreateSourceVoice(IXAudio2 *device, IXAudio2SourceVoice **source, IXAudio2Voice *destVoice,
                                            WAVEFORMATEX &fmt, DWORD voiceUsefilter, XAUDIO2_VOICE_SENDS *voiceSend)
{
  HRESULT hr;
  DoAssert(device);

  // voice output set
  if (voiceSend)
  {
    hr = device->CreateSourceVoice(source, &fmt, voiceUsefilter, MaxFrequency, NULL, voiceSend);
  }
  else
  {
    // set default voice output
    XAUDIO2_SEND_DESCRIPTOR desc;
    desc.Flags = XAUDIO2_SEND_USEFILTER;
    desc.pOutputVoice = destVoice;

    XAUDIO2_VOICE_SENDS voiceSends;
    voiceSends.pSends = &desc;
    voiceSends.SendCount = 1;

    hr = device->CreateSourceVoice(source, &fmt, voiceUsefilter, MaxFrequency, NULL, &voiceSends);
  }

  if ( FAILED(hr) )
  {
    RptF("Error: CreateSourceVoice failed with error: %x", hr);
    return false;
  }

  return true;
}

/* Create submix voice function wrapper */
static __forceinline bool CreateSubmixVoice(IXAudio2 *device, IXAudio2SubmixVoice **submix, IXAudio2Voice *destVoice,
                                            DWORD inChannels, DWORD inSampleRate, DWORD flags, 
                                            DWORD processingStage, XAUDIO2_VOICE_SENDS *voiceSend)
{
  HRESULT hr;
  DoAssert(device);

  // voice output set
  if (voiceSend)
  {
    hr = device->CreateSubmixVoice(submix, inChannels, inSampleRate, flags, processingStage, voiceSend);
  }
  else
  {
    if (destVoice)
    {
      // set default voice output
      XAUDIO2_SEND_DESCRIPTOR desc;
      desc.Flags = XAUDIO2_SEND_USEFILTER;
      desc.pOutputVoice = destVoice;

      XAUDIO2_VOICE_SENDS voiceSends;
      voiceSends.pSends = &desc;
      voiceSends.SendCount = 1;

      hr = device->CreateSubmixVoice(submix, inChannels, inSampleRate, flags, processingStage, &voiceSends);
    }
    else
    {
      // no destination voice set and no voiceSend set
      DoAssert(destVoice || voiceSend);
      hr = device->CreateSubmixVoice(submix, inChannels, inSampleRate, flags, processingStage, NULL);
    }
  }

  if ( FAILED(hr) )
  {
    RptF("Error: CreateSubmixVoice failed with error: %x", hr);
    return false;
  }

  return true;
}

bool WaveXA2::Load()
{
  PROFILE_SCOPE_EX(wavLo, sound);

  if( _loadError ) return true;
  if( _loaded ) return true;
  if (!_loadedHeader)
  {
    if (!LoadHeader())
    {
      return false;
    }
  }

  if (Name().GetLength() <= 0)
  {
    _header._frequency = 1000; // time given in ms
  }

  // scan all channels
  // if possible make some channels free (release cache)
  {
    _soundSys->ReserveCache(1 - _only2DEnabled,_only2DEnabled);

    // search all playing waves
    _soundSys->_waves.RemoveNulls();
  }
  {
    // first try to reuse any cached data
    if (_soundSys->LoadFromCache(this))
    {
#if DIAG_LOAD >= 2
      LogF("Cache reused %s",(const char *)Name();)
#endif
        AddStats("Cache");

      _loaded=true;
      _terminated=false;
      _playing=false;
      _wantPlaying=false;
      _streamPos=0;
      _startTime = 0;
      _stopTime = 0xffffffff;

      return true;
    }
  }

  // try to duplicate some existing sound buffer
  for(int i = 0; i < _soundSys->_waves.Size(); i++)
  {
    WaveXA2 *wave = _soundSys->_waves[i];
    if(!wave->Loaded()) continue;
    if(wave == this) continue;
    if(!strcmp(wave->Name(), Name()))
    {
      if(Duplicate(*wave))
      {
        return true;
      }
    }
  }

  Ref<WaveStream> stream;
  bool isStreaming = false;
  {
    PROFILE_SCOPE_EX(wavRq,sound);
    DoAssert(RString::IsLowCase(Name()));
    bool done = SoundRequestFile(Name(),stream);

    // if request is not satisfied, do not change buffer state
    if (!done)
    {
      //Log("Data not loaded - %s",(const char *)Name());
      return false;
    }

    // streaming
    if (stream)
    {
      isStreaming = IsStreaming(stream);
      if (!isStreaming)
      {
        // if not streaming, we need to read the whole file before proceeding
        bool dataReady = stream->RequestData(FileRequestPriority(10), 0, stream->GetUncompressedSize());
        if (!dataReady)
        {
          return false;
        }
      }
    }
  }

  DoDestruct();

  _loadError=true;
  _loaded=true;
  _playing=false;
  _wantPlaying=false;
  _startTime = 0;
  _stopTime = 0xffffffff;


  if (!stream)
  {
    RptF("Cannot load sound '%s'", (const char *)Name());
    return true;
  }

  // this should be already done?
  WAVEFORMATEX format;
  stream->GetFormat(format);
  _header._frequency = format.nSamplesPerSec;     // sampling rate
  _header._nChannel = format.nChannels;           // number of channels
  _header._sSize = format.nBlockAlign;            // number of bytes per sample
  _header._size = stream->GetUncompressedSize();

  Assert(_header._size > 0);
  if (_header._size <= 0) RptF("Warning: %s has ZERO data size", cc_cast(Name()));

  _waveFmt = format;

  IXAudio2 *device = _soundSys->XA2Device();
  {
    PROFILE_SCOPE_EX(wavCB,sound);
    if (device)
    {
#if _ENABLE_REPORT
      if (isStreaming)
      {
        //          LogF(
        //            "%s: %d buffer for %d, %d BPS",
        //            cc_cast(Name()),dsbd.dwBufferBytes,_header._size,_header._frequency*_header._sSize
        //          );
      }
#endif
      if (!isStreaming)
      {
        // buffer for non stream sample
        _buffer = new XA2Buffer();
        if (_buffer.IsNull()) { return true; }
      }
      else
      {
        // determination number of buffer for streaming
        int bufSize = StreamingPartSize(format);
        int totalSize = StreamingBufferSize(format,_header._size);
        Assert(totalSize % bufSize == 0);
        // prepare audio data buffers          
        int parts = totalSize / bufSize;
        _streamBuffers.Realloc(parts);
        _streamBuffers.Resize(parts);
        for (int i = 0; i < parts; i++) { _streamBuffers[i] = new XA2Buffer; }
      }
    }
  }
#if DIAG_LOAD
  LogF("WaveXA2 %s: CreateSoundBuffer",(const char *)Name());
#endif
  {

    // note: at this point streaming / static implementation
    // is different
    // static decompresses whole buffer once, while streaming keeps
    // memory mapped file buffer and reads from it sequentially
    if (isStreaming)
    {
      // fill buffer with start of streaming data
      _stream = stream;
      _streamPos = 0;
      _writeBuf = 0;
      _freeBufN = 0;
      //LogF("Reset stream %s",(const char *)Name());
    }
    else
    {
      if (device)
      {
        PROFILE_SCOPE_EX(wavGC,sound);

        // prepare audio buffer for non loop sample
        _buffer.AudioBuffer()->Realloc(_header._size);
        _buffer.AudioBuffer()->Resize(_header._size);

        int read = stream->GetDataClipped(_buffer.AudioBuffer()->Data(), 0, _buffer.AudioBuffer()->Size());

        if (read < _buffer.AudioBuffer()->Size())
        {
          LogF("GetData returned %d instead of %d", read, _buffer.AudioBuffer()->Size() - read);
          memset(_buffer.AudioBuffer()->Data() + read, 0, _buffer.AudioBuffer()->Size() - read);
        }
      }
    }
  }
  //_soundSys->LogChannels("Load");
  AddStats("Load");

  if (_soundSys->_xa2Device)
  {
    // radio protocol
    InitRadio();

    DWORD flags = _kind == WaveMusic ? XAUDIO2_VOICE_MUSIC : XAUDIO2_VOICE_USEFILTER;
    if (_isRadio) flags = 0;

    if (!CreateSourceVoice(_soundSys->_xa2Device, &_source, SelectOutputVoice(_soundSys, _kind, _isRadio, GetSticky()), _waveFmt, 
      flags, _sendList.SendCount > 0 ? &_sendList : NULL))
    {
      _sourceOpen = false;
      _buffer.Free();
      #if DIAG_RADIO_STREAM
      LogF("FAILED SourceVoiceCreate: %x, %s", this, cc_cast(Name()));
      #endif
      return true;
    }
    #if DIAG_RADIO_STREAM
    LogF("SUCCESS SourceVoiceCreate: %x, %s", this, cc_cast(Name()));
    #endif

    _sourceOpen = true;
  }

  // loaded successfully - flag no error
  _loadError = false;

  return true;
}

bool WaveXA2::IsStreaming(WaveStream *stream)
{
#if 0
  return false;
#else
  return stream->StreamingWanted(_looping);
#endif
}

/*!
\patch 1.43 Date 1/31/2002 by Ondra
- Fixed: Support for looping streaming sounds.
\patch 1.85 Date 9/12/2002 by Ondra
- Fixed: Streaming sounds might loop infinitely on some hardware.
*/
void WaveXA2::UnqueueStream(int processed)
{
  /*if (0 == strcmpi(cc_cast(Name()), "ca\\dubbing\\c0_firsttofight\\shaftoe_briefing\\shaftoe_briefing_h_6.wss"))
  {
    LogF("Hit");
  }*/

  if (processed <= 0) return;

  // which one are those processed?
  Assert(processed <= _streamBuffers.Size());

  int firstBusy = _writeBuf + _freeBufN;
  if (firstBusy >= _streamBuffers.Size()) firstBusy -= _streamBuffers.Size();

  XAUDIO2_VOICE_STATE state;
  _source->GetState(&state);

  processed = _bufQueued - state.BuffersQueued;

  for (int i = 0; i < processed; i++)
  {
    Ref<XA2Buffer> buff = _streamBuffers[firstBusy];
    buff->Clear();

    firstBusy++;
    if (firstBusy>=_streamBuffers.Size()) firstBusy = 0;
  }

  _bufQueued -= processed;
  _freeBufN += processed;
}

void WaveXA2::FillStream()
{
  if (_streamBuffers.Size() == 0) return;

  // check how many buffers can we un-queue
  int processed = 0;

  XAUDIO2_VOICE_STATE state;
  _source->GetState(&state);
  // how many buffers is in front
  processed = _bufQueued - state.BuffersQueued;

  // release used buffers
  UnqueueStream(processed);
  
  WAVEFORMATEX format;
  _stream->GetFormat(format);  
  int partSize = StreamingPartSize(format);
  int bufferSize = StreamingBufferSize(format, _header._size);
  int maxOneFill = StreamingMaxInOneFill(format, bufferSize) / partSize;

  int toWriteBufs = intMin(_freeBufN, maxOneFill);

  bool ready = false;
  if (_looping)
  {
    ready = _stream->RequestDataLooped(FileRequestPriority(1), _streamPos, toWriteBufs * partSize);
  }
  else
  {
    ready = _stream->RequestDataClipped(FileRequestPriority(1), _streamPos, toWriteBufs * partSize);
  }

  // no data loaded
  if (!ready) { return; }  

  // requested data are ready, so we can queue them into audio front
  for (int i = 0; i < toWriteBufs; i++)
  {
    int written = 0;

    // take reference to existing buffer
    Ref<XA2Buffer> buffR = _streamBuffers[_writeBuf];
    // allocate memory
    buffR->Realloc(partSize);
    buffR->Resize(partSize);

    if (_looping)
    {
      if (_streamPos >= (int)_header._size) _streamPos -= _header._size;
      written = _stream->GetDataLooped(buffR->Data(), _streamPos, partSize);
    }
    else
    {
      // if there is nothing more to write, do not write
      if (_streamPos >= (int)_header._size) { break; }
      written = _stream->GetDataClipped(buffR->Data(), _streamPos, partSize);
    }

    _streamPos += written;

    XAUDIO2_BUFFER xa2buffer = { 0 };
    xa2buffer.Flags = 0;
    xa2buffer.pAudioData = (BYTE*)buffR->Data();
    xa2buffer.AudioBytes = buffR->Size();
    // last buffer must be mark as last
    xa2buffer.Flags = (_streamPos >= (int)_header._size) ? XAUDIO2_END_OF_STREAM : 0;

    HRESULT hr = _source->SubmitSourceBuffer(&xa2buffer);
    CHECK_XA2_ERROR(hr, "SubmitSourceBuffer");

    // next buffer queued
    _bufQueued++;

    // is source started, call has no effect
    hr = _source->Start(0, XAUDIO2_COMMIT_NOW);
    CHECK_XA2_ERROR(hr, "Start");

    // index of used buffer
    _writeBuf++;
    if (_writeBuf >= _streamBuffers.Size()) _writeBuf = 0;
    _freeBufN--;
  }
}

#if 1

#define STAT_PLAY(x)
#define STAT_TERM(x)
#define STAT_STOP(x)
#define STAT_GETCHANNELS() 0

#else

static int NPlaying = 0;

#define STAT_PLAY(x) \
  NPlaying++; \
  GlobalShowMessage(100,"Playing %d", NPlaying); \
  LogF("%2d: Play %x:%s",NPlaying,x,(const char *)(x)->Name())

#define STAT_TERM(x) \
  LogF("   Term %x:%s",x,(const char *)(x)->Name())

#define STAT_STOP(x) \
  NPlaying--; \
  GlobalShowMessage(100,"Playing %d", NPlaying); \
  LogF("%2d: Stop %x:%s",NPlaying,x,(const char *)(x)->Name())

#define STAT_GETCHANNELS() NPlaying

#endif

bool WaveXA2::PreparePlaying()
{
  if (_playing || _terminated) return true;
  // check files needed
  bool loaded = Load();
  if (!loaded) return false;

  if (_stream)
  {
    int dummy;
    int preloadSize = InitPreloadStreamSize(dummy);
    return _stream->RequestData(FileRequestPriority(10), 0, preloadSize);
  }

  return true;
}

void WaveXA2::Play()
{
  // set flag we want to play
  // commit will play it after listener position is set
  if( _terminated ) return;

  _wantPlaying = true;
  // avoid stop unloading
  _stopTime = 0xffffffff;
}

#if 0 //_ENABLE_REPORT
/// targeted logging
static bool InterestedIn(const char *name)
{
  return strstr(name,"ah1cobra")!=NULL;
}
#else
static inline bool InterestedIn(const char *name) {return false;}
#endif

/**
@param delay delay realized by queuing a silence buffer before the one we want to play.
*/
bool WaveXA2::DoPlay(int delayMs)
{
  if(_terminated)
  {
    return true;
  }

  if (!_soundSys->XA2Device())
  {
    // we want to load the header (background loading is enough)
    // otherwise we do not know when the sound has terminated
    bool loaded = _loadedHeader;
    if (!loaded)
    {
      loaded = LoadHeader();
    }
    // we need to report the sound as "playing", to prevent handlers stuck
    if (loaded) OnPlayOnce();
    return true;
  }

  if(_playing)
  {
    // check if we are playing in correct context
#if SEPARATE_2D_CONTEXT
    if (DesiredContext() != _context)
    {
      // if not, we need to stop and start playing again
      Stop();
    }
    else
#endif
    {
      // if it is streaming, fill with data as necessary
      if (_stream) { FillStream(); }
      return true;
    }
  }

  if (!_posValid && !_only2DEnabled)
  {
    // position not set yet - cannot start playing
    return false;
  }

  if(GetTimePosition() >= 0)
  {
    bool loaded = Load();

    //PROFILE_SCOPE_EX(wavPl,sound);

    if (!loaded || !_sourceOpen) return false;

    // first byte position, _offset is defined in samples
    if (_applyOffset) 
    {
      int offset = (int)(_header._size * _offset);
      int tmp = offset % _header._sSize;
      if (tmp != 0) offset += tmp;
      _curPosition += offset;
      _applyOffset = false;
    }

    // check position
    if(_curPosition >= (int)_header._size)
    {
      if(_looping && _header._size > 0)
      { 
        while(_curPosition >= (int)_header._size) _curPosition -= _header._size;
      }
      else
      {
        _terminated = true;
        _curPosition = _header._size;
      }
    }
    else
    {
      //PROFILE_SCOPE_EX(wavSt,sound);
#if DIAG_VOL
      LogF("SP: SetVolume %s %g",(const char *)Name(),_gainSet);
#endif

      XAUDIO2_VOICE_STATE state;
      _source->GetState(&state);
      HRESULT hr = _source->SetVolume(_gainSet);
      CHECK_XA2_ERROR(hr, "WaveXA2::DoPlay - SetVolume");

      hr = _source->SetFrequencyRatio(_freqSet);
      CHECK_XA2_ERROR(hr, "WaveXA2::DoPlay - SetFrequencyRatio");

      Calculate3DAudio(true);

      // if streaming not used - data in single buffer
      if (!_stream)
      {
        _startTime = GlobalTickCount();
        _stopTime = 0xffffffff;

        // consider how much of the buffer has already been played back
        if (_curPosition>0)
        {
          DWORD alreadyPlayed = toLargeInt((_curPosition/_header._sSize)*1000/(_header._frequency*_freqSet));
          _startTime -= alreadyPlayed;
        }

        PROFILE_SCOPE_EX(wavPP,sound);
        WAV_COUNTER(wavPl,1);

        Assert(!_buffer.IsNull());

        // clear buffer
        memset(&_buffDescr, 0, sizeof(XAUDIO2_BUFFER));
        Assert((_curPosition / _header._sSize) < _buffer.AudioBuffer()->Size());

        // insert data
        _buffDescr.AudioBytes = _buffer.AudioBuffer()->Size();
        _buffDescr.pAudioData = (BYTE*)_buffer.AudioBuffer()->Data();
        _buffDescr.Flags = XAUDIO2_END_OF_STREAM;
        _buffDescr.PlayBegin = (_curPosition / _header._sSize); 
        _buffDescr.PlayLength = (_buffDescr.AudioBytes / _header._sSize) - _buffDescr.PlayBegin;
        _buffDescr.LoopCount = (_looping) ? XAUDIO2_LOOP_INFINITE : XAUDIO2_NO_LOOP_REGION;

        HRESULT hr = StartSubSourceVoice();
        CHECK_XA2_ERROR(hr, "WaveXA2::DoPlay");

        if (FAILED(hr))
        {
          _buffer.Free();
          return true;
        }
      }
      else
      {
        if (_streamBuffers.Size() > 0)
        {
          //PROFILE_SCOPE_EX(wavPS,sound);

#if DIAG_STREAM_FILL>0
          LogF("%s: Set stream start to %d",cc_cast(Name()),_curPosition);
#endif
          _bufQueued = 0;
          _writeBuf = 0;
          _streamPos = _curPosition;
          _freeBufN = _streamBuffers.Size();

          if (!FillStreamInit())
          {
            _wantPlaying = true;

            return false;
          }

          WAV_COUNTER(wavPl, 1);

          HRESULT hr = _source->Start(0, XAUDIO2_COMMIT_NOW);
          CHECK_XA2_ERROR(hr, "WaveXA2::DoPlay - Start");
        }
        _countPlaying[_type]++;
      }
    }

    _playing = true;

    STAT_PLAY(this);

    OnPlayOnce();
  }

  return true;
}

void WaveXA2::Repeat(int repeat)
{
  if (repeat < 2)
  {
    _looping = false;
  }
  else
  {
    _looping = true;
    if(_playing)
    {
      if (_sourceOpen)
      {
        WAV_COUNTER(wavPl,1);

        HRESULT hr = StopSourceVoice();
        CHECK_XA2_ERROR(hr, "WaveXA2::DoPlay");

        XAUDIO2_VOICE_STATE state;
        _source->GetState(&state);

        // update params
        _buffDescr.PlayBegin = state.SamplesPlayed;
        _buffDescr.LoopLength = XAUDIO2_LOOP_INFINITE;

        hr = StartSubSourceVoice();
        CHECK_XA2_ERROR(hr, "WaveXA2::DoPlay");
      }
    }
  }
}
void WaveXA2::Restart()
{
  // force filter applying
   _parsEx._forceUpdate = true;

  if (_playing && !_stream)
  {
    // we need to restart the sound only
    // no need to unload/load ...
    _terminated = false;

    _curPosition = 0;
    _skipped = 0;
    _buffDescr.PlayBegin = 0; // start from beginning
    _buffDescr.PlayLength = 0;  // play whole sample
    
    Assert(_buffDescr.PlayLength <= (_header._size / _header._sSize));
    Assert(_buffDescr.AudioBytes <= _header._size);

    HRESULT hr = StopSourceVoice();
    CHECK_XA2_ERROR(hr, "WaveXA2::Restart");

    hr = StartSubSourceVoice();
    CHECK_XA2_ERROR(hr, "WaveXA2::Restart");
  }
  else
  {
    bool wasPlaying = _playing;
    Stop();
    _terminated=false;
    _curPosition=0;
    _buffDescr.PlayBegin = 0;
    _skipped = 0;
    if (_stream)
    {
      _streamPos = 0;
    }
    Play();
    if (wasPlaying)
    {
      if (DoPlay()) _wantPlaying=false;
    }
  }
}

/*!
\patch 1.01 Date 15/6/2001 by Ondra.
- Fixed: playMusic [name,t] with t>96
*/
void WaveXA2::Skip(float deltaT)
{
  // note: pre-start delay is used to implement speed of sound this gives bad results if listener speed is high
  if (!_loadedHeader)
  {
    _skipped += deltaT;
    
    // check if header is loaded already to avoid skipping too much
    if (LoadHeader())
    {
      int skip = toLargeInt(deltaT * _header._frequency) * _header._sSize;

      if (skip >= _header._sSize)
      {
        _curPosition = _header._sSize;
        _terminated = true;
      }
    }

    return;
  }

  if (_playing)
  {
    StoreCurrentPosition();
  }

  // process what should be skipped
  deltaT += _skipped;
  _skipped = 0;

  Assert(_loadedHeader);
  // FIX
  int skip = toLargeInt(deltaT*_header._frequency) * _header._sSize;

  AdvanceCurrentPosition(skip);

  while (_curPosition >= (int)_header._size)
  {
    if (!_looping || _header._size <= 0)
    {
      Stop();
      _terminated = true;
      _curPosition = _header._size;
      break;
    }
    _curPosition -= _header._size;
  }

  if (_playing && !_terminated)
  {
    WAV_COUNTER(wavSV,1);

    if (_streamBuffers.Size() == 0)
    {
      HRESULT hr = StopSourceVoice();
      CHECK_XA2_ERROR(hr, "WaveXA2::Skip");

      // update position in samples
      _buffDescr.PlayBegin = _curPosition / _header._sSize;

      hr = StartSubSourceVoice();
      CHECK_XA2_ERROR(hr, "WaveXA2::Skip");
    }

    if (_stream)
    {
      LogF("Cannot advance playing stream %s",(const char *)Name());
    }
  }
}

void WaveXA2::Advance( float deltaT )
{
  if( !_playing ) Skip(deltaT);
}

float WaveXA2::GetTimePosition() const
{
  if (!_loadedHeader) return _skipped;
  float denom = _header._sSize * _header._frequency;
  if (denom <= 0) return _skipped;
  return _skipped + float(_curPosition)/(_header._sSize * _header._frequency);
}


bool WaveXA2::IsStopped() { return !_playing && !_wantPlaying; }
bool WaveXA2::IsWaiting() {return GetTimePosition() < 0; }

RString WaveXA2::GetDiagText()
{
  RString ret;
  if (GetTimePosition()<0)
  {
    float delay = -GetTimePosition();
    char buf[256];
    sprintf(buf,"Delay %.3f ",delay);
    ret = buf;
  }
  if( IsStopped() ) return ret+RString("Stopped");
  else if( IsTerminated() ) return ret+RString("Terminated");
  else if( IsWaiting() ) return ret+RString("Waiting");
  else 
  {
    if( !_loaded ) return ret+RString("Not loaded");
    if( !_sourceOpen ) return ret+RString("No buffer");
    // TODO: more diags
    if( _only2DEnabled )
    {
      char buf[512];
      sprintf
        (
        buf,
        "Playing 2D\n"
        "    vol (%.2f, adj %.2f) kind %d",
        _volume,_volumeAdjust,_kind
        );
      return ret+RString(buf);
    }
    else
    {
      char buf[512];
      sprintf
        (
        buf,
        "Playing\n"
        "    pos (%.1f,%.1f,%.1f)\n"
        "    vel (%.1f,%.1f,%.1f)\n"
        "    vol (%.3f)",
        _emitter.Position.x, _emitter.Position.y, _emitter.Position.z,
        _emitter.Velocity.x, _emitter.Velocity.y, _emitter.Velocity.z,
        _volume
        );

      return ret+RString(buf);
    }
  }
}

float WaveXA2::GetLength() const
{
  // get wave length in sec
  // Assert(_loadedHeader); //removed as it asserted at the start of MP
  if (!_loadedHeader)
  {
    if (!unconst_cast(this)->LoadHeader())
    {
      do {
        if (_loadError) 
        {
          RptF("Error: cannot load sound '%s', header cannot be loaded.", cc_cast(Name()));
          return 1;
        }
      } while(!unconst_cast(this)->LoadHeader());
    }
  }

  if (_header._sSize<=0)
  {
    RptF("%s: sSize %d",(const char *)Name(),_header._sSize);
    return 1;
  }
  if (_header._frequency<=0)
  {
    RptF("%s: frequency %d",(const char *)Name(),_header._frequency);
    return 1;
  }
  return float(_header._size/_header._sSize)/_header._frequency;
}

void WaveXA2::SetStartOffset(float percentage)
{ 
  _offset = percentage; 
  _offset = fastFmod(_offset, 1.0f);
  _applyOffset = true; 
}

float WaveXA2::GetCurrPosition() const
{
  if (!_loadedHeader) return 0.0f;

  if (_wantPlaying) return 0.0;

  if (_playing && !_wantPlaying)
  {
    if (_sourceOpen && _source)
    {
      XAUDIO2_VOICE_STATE state;
      _source->GetState(&state);

      UINT bytesPlayed = (_offset * _header._size) + (UINT)(state.SamplesPlayed * _header._sSize);

      if (!_looping)
      {
        if (bytesPlayed >= _header._size) return 1.0;
      }

      return (bytesPlayed % _header._size) / ((float)_header._size); 
    }
  }

  return 1.0;
}

// Check if wave yet stop
bool WaveXA2::IsTerminated()
{
  if (!_terminated)
  {
    if (_playing)
    {
      Assert (_sourceOpen);
      if (_sourceOpen)
      {
        // non streaming sample
        if (!_buffer.IsNull())
        {
          XAUDIO2_VOICE_STATE state;
          _source->GetState(&state);

          // is playing 
          if ((state.SamplesPlayed != 0) || (state.BuffersQueued != 0)) return false;

          // the buffer has terminated, we should be at the end ?
          _curPosition = _header._size;
          Assert(_skipped == 0);
        }
        // streaming sample
        else if (_streamBuffers.Size() > 0)
        {
          // if streaming sound is looped, it will never terminate
          if (_looping) return false;
          // if it was not played whole, it did not terminate yet
          if (_streamPos < (int)_header._size) return false;

          // check status
          XAUDIO2_VOICE_STATE state;
          _source->GetState(&state);          

          // no more buffers are prepared to be played
          if (state.BuffersQueued != 0) return false;

          // set position to end of file
          _curPosition = _header._size;

          Assert(_skipped == 0);
        }
        // set termianted for non loop stream
        if(!_looping)
        {
          STAT_TERM(this);
          _terminated = true;
          // looping wave can never be terminated
          _stopTime = GlobalTickCount();
        }
      }
    }
    else if (!_loadedHeader && _skipped > 5.0f)
    {
      // if sound is skipped for quite some time, there is a good change it has already terminated
      // we cannot be sure unless we load its header, though, because we do not know its length
      LoadHeader();
      // LoadHeader did set _terminate if appropriate
    }
    else if (_loadError)
    {
      // sound that cannot be loaded is considered terminated
      _terminated = true;
    }
  }

  return _terminated;
}

bool WaveXA2::CanBeDeleted()
{
  if (!IsTerminated())
  {
    return false;
  }
  Unload(false);
  return _buffer.IsNull();
}

int WaveXA2::InitPreloadStreamSize(int &initSize) const
{
  WAVEFORMATEX format;
  _stream->GetFormat(format);

  int bufferSize = StreamingBufferSize(format,_header._size);
  int partSize = StreamingPartSize(format);
  int maxOneFill = StreamingMaxInInitFill(format,bufferSize);

  int totalSize = _stream->GetUncompressedSize();
  initSize = bufferSize-2*partSize;
  if (initSize > maxOneFill)
  {
    initSize = maxOneFill;
  }

  int preloadSize = initSize*4;
  if (preloadSize>totalSize) preloadSize = totalSize;
  if (initSize>totalSize) initSize = totalSize;
  return preloadSize;
}

bool WaveXA2::FillStreamInit()
{
  int initSize;

  WAVEFORMATEX format;
  _stream->GetFormat(format);
  int preloadSize = InitPreloadStreamSize(initSize);

  // on first frame request more data than necessary
  // to make sure next frames will be able to fill, to prevent starving
  if (!_stream->RequestData(FileRequestPriority(5), _streamPos, preloadSize))
  {    
    return false;
  }

#if DIAG_STREAM_FILL>20
  LogF("%s: done %d B,%d B",cc_cast(Name()),_streamPos, preloadSize);

#endif

  int partSize = StreamingPartSize(format);
  int initBuffers = initSize / partSize;

  Assert(_writeBuf == 0);

  for (int i = 0; i < initBuffers; i++)
  {
    Ref<XA2Buffer> buffR = _streamBuffers[i];
    buffR->Realloc(partSize);
    buffR->Resize(partSize);

    if (_looping)
    {
      _streamPos += _stream->GetDataLooped(buffR->Data(), _streamPos, partSize);
    }
    else
    {
      _streamPos += _stream->GetDataClipped(buffR->Data(), _streamPos, partSize);
    }

    // audio buffer data description
    XAUDIO2_BUFFER xa2Buffer = { 0 };
    xa2Buffer.AudioBytes = partSize;
    xa2Buffer.pAudioData = (BYTE *) buffR->Data();
    xa2Buffer.LoopCount = XAUDIO2_NO_LOOP_REGION;

    // queue buffer
    HRESULT hr = _source->SubmitSourceBuffer(&xa2Buffer);
    CHECK_XA2_ERROR(hr, "SubmitSourceBuffer");

    if (hr != S_OK)
    {
      FreeOnDemandSystemMemoryLowLevel(partSize);
    }

    // number of queued buffers
    _bufQueued++;

    // next buffer used
    _writeBuf++;
    if (_writeBuf >= _streamBuffers.Size()) _writeBuf = 0;
  }

  // count of free buffers
  _freeBufN = _streamBuffers.Size() - initBuffers;
  return true;
}

void WaveXA2::StoreCurrentPosition()
{
  // store current position so that is available when playing the buffer
  if (!_sourceOpen) return;

  XAUDIO2_VOICE_STATE state;
  _source->GetState(&state);

  // offset in bytes = number of played samples * bytes per sample
  int samplesPlayed = state.SamplesPlayed * _header._sSize;

  if (_looping)
    samplesPlayed %= _header._size;

  Assert(_skipped==0);

  // for streaming audio store all necessary information
  if (_stream)
  {
    _curPosition = _streamPos;
    _streamPos = 0; // only one of _curPosition or _streamPos can be non-zero
  }
  else
  {
    // start from beginning
    _curPosition = samplesPlayed;
  }
}

/*!
\patch 1.27 Date 10/11/2001 by Ondra
- Fixed: speed of sound delay caused some sounds to be decayed (since 1.26).
*/
void WaveXA2::AdvanceCurrentPosition(int skip)
{
  Assert(_skipped == 0);

  if (!_stream)
  {
    int oldPos = _curPosition;
    _curPosition += skip;
    if (_curPosition < 0)
    {
      Stop();
    }
    else
    {
      if (oldPos < 0) _curPosition = 0;
    }
  }
  else
  {
    if (_playing)
    {
      LogF("Cannot advance in playing stream %s", (const char *)Name());
    }
    // note: only one of _streamPos and _curPosition should be non-zero
    Assert (_streamPos == 0 || _curPosition == 0);
    int newPos = _streamPos + _curPosition + skip;
    if (newPos < 0)
    {
      if (_header._size==INT_MAX) newPos = 0; // RadioStream are infinite long, we can rewind
      else Stop();
      //LogF("Hard rewind %s to %d",(const char *)Name(),newPos);
    }
    else
    {
      //LogF("Soft rewind %s to %d",(const char *)Name(),newPos);
    }
    _curPosition = newPos;
    _streamPos = 0;
  }
}

bool WaveXA2::DoStop()
{
  _wantPlaying = false;
  if(!_loaded)
  {
    if (_playing && !_loadError)
    {
      LogF("%x,%s: Playing, not loaded", this, (const char *)Name());
    }
    // without resetting _playing muted (volume=0) sounds which experienced _loadError were stuck, because they did not obey Stop
    _playing = false;
    return true;
  }

  if(!_playing)
  {
    if (_sourceOpen)
    {
      DWORD time = GlobalTickCount();
      if (_stopTime < time)
      {
        DWORD stopped = time - _stopTime;
        const DWORD unloadAfterStop = 2000; // in ms
        if (stopped > unloadAfterStop)
        {
          UnloadStopped(false);
        }
      }
    }
    return false;
  }

  _playing = false;

  if (_sourceOpen)
  {
    if (!_buffer.IsNull())
    {
      WAV_COUNTER(wavSt,1);
      _countPlaying[_type]--;

      StoreCurrentPosition();

      HRESULT hr = StopSourceVoice();
      CHECK_XA2_ERROR(hr, "WaveXA2::DoStop");

      if (_header._size > 0) 
      {
        while (_curPosition >= (int)_header._size) _curPosition -= _header._size;
      }

      _stopTime = GlobalTickCount();
    }
    else if (_streamBuffers.Size() >= 0)
    {
      WAV_COUNTER(wavSt,1);
      _countPlaying[_type]--;

      StoreCurrentPosition();

      HRESULT hr = StopSourceVoice();
      CHECK_XA2_ERROR(hr, "WaveXA2::DoStop - streaming");

      UnqueueStream(_streamBuffers.Size() - _freeBufN);
      _stopTime = GlobalTickCount();
      // all should be processed now, unqueue
    }
  }
  // if sound is stopped for a long time, we can unload it
  STAT_STOP(this);
  return true;
}

HRESULT WaveXA2::StopSourceVoice()
{
  Assert(_source);

  if (_sourceOpen)
  {
    //LogF("StopSourceVoice, open: %d, stream: %x, loop: %d", _sourceOpen, _stream, _looping);

    // stops consumption of audio by the current voice
    HRESULT hr = _source->Stop(0, XAUDIO2_COMMIT_NOW);
    if (hr != S_OK)
    {
      CHECK_XA2_ERROR(hr, "WaveXA2::StopSourceVoice - Stop");
      return hr;
    }

    // removes all pending audio buffers from the voice queue
    hr = _source->FlushSourceBuffers();
    CHECK_XA2_ERROR(hr, "WaveXA2::StopSourceVoice - FlushSourceBuffers");   

    return hr;
  }

  return E_FAIL;
}

HRESULT WaveXA2::StartSubSourceVoice()
{
  Assert(_source && _sourceOpen);

#if DIAG_NO_SOUND
  XAUDIO2_VOICE_STATE state;
  _source->GetState(&state);

  while (true)
  {
    _source->GetState(&state);
    if (!state.BuffersQueued) break;

    LogF("Wait for empty buffer");

    _source->Stop(0, XAUDIO2_COMMIT_NOW);
    _source->FlushSourceBuffers();
  }
#endif
  HRESULT hr = _source->SubmitSourceBuffer(&_buffDescr);
  if (hr != S_OK)
  {
    CHECK_XA2_ERROR(hr, "WaveXA2::StartAndSubmitSourceVoice - SubmitSourceBuffer");
    return hr;
  }

  hr = _source->Start(0, XAUDIO2_COMMIT_NOW);
  CHECK_XA2_ERROR(hr, "WaveXA2::StartAndSubmitSourceVoice - Start");

  return hr;
}

void WaveXA2::CheckPlaying()
{
  // check if wave has already terminated
  IsTerminated();
}

void WaveXA2::Unload()
{
  return Unload(false);
}

void WaveXA2::UnloadStopped(bool sync)
{
  if (_sourceOpen)
  {
    // OpenAL can always unload
    DoUnload();
  }
}

/**
\param sync when true, wave must be really stopped before function returns
*/
void WaveXA2::Unload(bool sync)
{
  DoStop();
  UnloadStopped(sync);
}

void WaveXA2::Stop()
{
  DoStop();
}
void WaveXA2::LastLoop()
{
  if( !_loaded ) return;
  if( !_looping ) return;
  if( _playing )
  {
    if (!_stream)
    {
      if (_sourceOpen)
      {
        // stops looping the voice when it reaches the end of the current loop region
        HRESULT hr = _source->ExitLoop(XAUDIO2_COMMIT_NOW);
        CHECK_XA2_ERROR(hr, "WaveXA2::LastLoop-ExitLoop");
      }
    }
  }
  _looping = false;
}

void WaveXA2::PlayUntilStopValue( float time )
{
  // play until some time
  _stopTreshold = time;
  if (_looping) return;

  _looping = true;
  if(!_loaded) return;

  if(_playing)
  {
    if (_sourceOpen)
    {
      WAV_COUNTER(wavPl,1);

      XAUDIO2_VOICE_STATE state;
      _source->GetState(&state);

      // continue playing at the same place where we stopped
      _buffDescr.PlayBegin = state.SamplesPlayed;
      _buffDescr.PlayLength = 0;
      _buffDescr.LoopCount = XAUDIO2_LOOP_INFINITE;

      HRESULT hr = StopSourceVoice();
      CHECK_XA2_ERROR(hr, "WaveXA2::PlayUntilStopValue");

      // return buffer back to front
      hr = StartSubSourceVoice();
      CHECK_XA2_ERROR(hr, "WaveXA2::PlayUntilStopValue");
    }
  }
}

void WaveXA2::SetStopValue( float time )
{
  if (time>_stopTreshold)
  {
    _terminated = true;
    Stop();
  }
}

float WaveBuffersXA2::CalcMinDistance() const
{
  //return floatMax(sqrt(_volume)*_accomodation*30,1e-6);
  return floatMax(_volume*_accomodation*60,1e-6);
}

static bool SoundParsNeedUpdate(int timeMs)
{
  // never update sound more than 20 fps
  const int minTimeUpdate = 50;
  if (timeMs < minTimeUpdate)
  {
    return false;
  }
  // TODO: XA2: cache values
  return true;
}
struct GenericLowPassFiter: public BaseAudioFilterEx
{
  typedef BaseAudioFilterEx base;

  bool operator() (FilterParamsEx &pars, float distToEmitor, bool forceUpdate)
  {
    if (!base::operator()(pars, distToEmitor, forceUpdate)) return false;
    
    float val = InterpolativC(distToEmitor, 0.0f, 800.0f, 12000.0f, 300.0f);
    float qVal = InterpolativC(distToEmitor, 1.5f, 800.0f, XAUDIO2_MAX_FILTER_ONEOVERQ - 0.4f, 0.4f);

    pars.Frequency = XAudio2CutoffFrequencyToRadians(val, pars._frequency);
    pars.OneOverQ = qVal;
    pars.Type = LowPassFilter;

    return true;
  }
};

struct FireInternalLowPassFilter: public BaseAudioFilterEx
{
  typedef BaseAudioFilterEx base;

  bool operator() (FilterParamsEx &pars, float distToEmitor, bool forceUpdate) 
  { 
    if (!base::operator()(pars, distToEmitor, forceUpdate)) return false;

    static float Freq = 0.250f;
    static float OverQ = 0.750f;

    pars.Frequency = Freq;//XAudio2CutoffFrequencyToRadians(Freq, SampleRate);
    pars.OneOverQ = OverQ;//XAudio2CutoffFrequencyToRadians(OverQ, SampleRate);
    pars.Type = LowPassFilter;

    return true; 
  }
};

struct VehOpenDoorsBandPassFilter: public BaseAudioFilterEx
{
  typedef BaseAudioFilterEx base;

  bool operator() (FilterParamsEx &pars, float distToEmitor, bool forceUpdate)
  {
    if (!base::operator()(pars, distToEmitor, forceUpdate)) return false;
    
    static float Freq = 0.075f;//XAudio2CutoffFrequencyToRadians(700, pars._frequency);
    static float OverQ = 0.400f;//XAudio2CutoffFrequencyToRadians(500, pars._frequency);

    pars.Frequency = Freq;
    pars.OneOverQ = OverQ;
    pars.Type = BandPassFilter;

    return true;
  }
};

struct RadioBandPassFilter: public BaseAudioFilterEx
{
  typedef BaseAudioFilterEx base;

  bool operator() (FilterParamsEx &pars, float distToEmitor, bool forceUpdate)
  {
    if (!base::operator()(pars, distToEmitor, forceUpdate)) return false;

    static float Freq = 0.55f;
    static float OverQ = 0.15f;

    pars.Frequency = Freq;
    pars.OneOverQ = OverQ;
    pars.Type = BandPassFilter;

    return true;
  }
};

struct SubmixFilter: public BaseAudioFilterEx
{
  typedef BaseAudioFilterEx base;

  bool operator() (FilterParamsEx &pars, float distToEmitor, bool forceUpdate)
  {
    if (!base::operator()(pars, distToEmitor, forceUpdate)) return false;

    if (distToEmitor > 100)
    {
      // dist 100 - 2000m
      saturate(distToEmitor, 100.0f, 2000.0f);
      float val = (log10f(distToEmitor) - 2.0f) * 0.76862178f;
      // 6000Hz - 100Hz
      const float MaxFreq = 6000.0f;
      const float MinFreq = 100.0f;
      val = MaxFreq - val * (MaxFreq - MinFreq);

      pars.Frequency = XAudio2CutoffFrequencyToRadians(val, pars._frequency);
      pars.OneOverQ = 1.0f;
      pars.Type = LowPassFilter;
    }
    else
    {
      // bypass filtering
      pars.Frequency = 1.0f;
      pars.OneOverQ = 1.0f;
      pars.Type = LowPassFilter;
    }

    return true;
  }
};

struct AudioFilterFactory
{
  GenericLowPassFiter _genericGunLPF;
  FireInternalLowPassFilter _insideVehBPF;
  SubmixFilter _submixFilter;
  VehOpenDoorsBandPassFilter _vehDoosPF;

public:
  bool SetFilter(FilterParamsEx &filter, float distance, bool forceUpdate)
  {
    switch(filter._typeEx)
    {
    case GenericLowPFilter:
      return _genericGunLPF(filter, distance, forceUpdate);

    case InternalFireLowPFilter:
      return _insideVehBPF(filter, distance, forceUpdate);

    case SubmixLowPFilter:
      return _submixFilter(filter, distance, forceUpdate);
    
    case HalfInternalBandFilter:
      return _vehDoosPF(filter, distance, forceUpdate);
    }

    return false;
  }
};

static AudioFilterFactory AFFactory;

#define MAX_LIST_DISTANCE 1

void WaveXA2::Calculate3DAudio(bool immediate)
{
  // 3d sound is computed in submix voice, other case when is set is in radio protocol, where is submix used for resampling
  if (_sendList.SendCount) { return; }

  // number of items in array
  int maxChannels = _waveFmt.nChannels * _soundSys->_outFmt.nChannels;

  if (_only2DEnabled && !immediate) { return; } 

  Assert(_soundSys);

  bool calc3D = false;
  DWORD time = GlobalTickCount();
  if (immediate || SoundParsNeedUpdate(time - _parsTime))
  {
    // only 3d
    calc3D = _3DEnabled && !_only2DEnabled;
  }

  if (calc3D)
  {
    _parsTime = GlobalTickCount();
    float distance= GetMaxDistance();// CalcMinDistance();

    /* ChannelAzimuths must have at least ChannelCount elements. The table values must be within 
    0.0f to X3DAUDIO_2PI. ChannelAzimuths is used with multi-channel emitters for matrix calculations. */
    AutoArray< float, MemAllocLocal<float, 4> > channelAzimuth;
    channelAzimuth.Realloc(_waveFmt.nChannels);
    channelAzimuth.Resize(_waveFmt.nChannels);

    for (int i = 0; i < channelAzimuth.Size(); i++) { channelAzimuth[i] = 0.0f; }

    /*Caller provided array that will be initialized with the volume level of each source channel present
    in each destination channel */
    AutoArray< float, MemAllocLocal<float, 8> > matrixCoefficients;
    matrixCoefficients.Realloc(maxChannels);
    matrixCoefficients.Resize(maxChannels);

    X3DAUDIO_DSP_SETTINGS dspSettings = { 0 };
    // the following members must be initialized before passing this structure to the calculate 
    dspSettings.SrcChannelCount = _waveFmt.nChannels;
    dspSettings.DstChannelCount = _soundSys->_outFmt.nChannels;
    dspSettings.pMatrixCoefficients = matrixCoefficients.Data();

    // more info about attributes can be found in SDK
    _emitter.pChannelAzimuths = channelAzimuth.Data();
    _emitter.ChannelCount = _waveFmt.nChannels;
    _emitter.InnerRadius = distance * 0.05f;
    _emitter.CurveDistanceScaler = floatMax(distance, 0.001f);

    // calculates DSP settings with respect to 3D parameters
    X3DAudioCalculate(_soundSys->_x3dInstance, &_soundSys->_listener, &_emitter, _soundSys->_x3dCalculateFlags, &dspSettings);

    if (debugMe)
    {
      LogF("Listener: (%.2f,%.2f)[%.2f,%.2f], Emitter: %.2f,%.2f => Left [%.2f,%.2f] Right [%.2f,%.2f]", 
        _soundSys->_listener.OrientFront.x,_soundSys->_listener.OrientFront.z,
        _soundSys->_listener.Position.x,_soundSys->_listener.Position.z,
        _emitter.Position.x,_emitter.Position.z,
        matrixCoefficients[0],matrixCoefficients[1],matrixCoefficients[2],matrixCoefficients[3]
        );
    }

    // 0 ... mastering voice
    // sets the volume level of each channel of the final output for the voice. 
    // these channels are mapped to the input channels of a specified destination voice
    SetOutputMatrix(_source, SelectOutputVoice(_soundSys, _kind, _isRadio, GetSticky()), _waveFmt.nChannels, 
      _soundSys->_outFmt.nChannels, matrixCoefficients.Data());

    // apply doppler
    float freq = _freqSet * dspSettings.DopplerFactor;
    SaturateFrequency(freq);
    HRESULT hr =_source->SetFrequencyRatio(freq);
    CHECK_XA2_ERROR(hr, "WaveXA2::Calculate3DAudio - SetFrequencyRatio");

    if (_parsEx._enable)
    {
      if (AFFactory.SetFilter(_parsEx, dspSettings.EmitterToListenerDistance, _parsEx._forceUpdate))
      {
        _source->SetFilterParameters(_parsEx.GetXA2ParsPtr());
      }
    }
  }
}

float WaveXA2::GetMinDistance() const
{
  if (_only2DEnabled)
  {
    return _distance2D;
  }
  else
  {
    return CalcMinDistance();
  }
}

static float CalcVolumeFromCurve(float distance, float maxEmitterDist)
{
  // range [0, 1]
  float nDist = distance / maxEmitterDist;

  // listener is out off emitter range
  if (nDist > 0.999) return 0;

  // volume is given by curve
  for (unsigned int i = 1; i < Emitter_Volume_Curve.PointCount; i++)
  {
    if (Emitter_Volume_CurvePoints[i].Distance > nDist)
    {
      float val = Interpolativ(nDist, 
        Emitter_Volume_CurvePoints[i-1].Distance, Emitter_Volume_CurvePoints[i].Distance, 
        Emitter_Volume_CurvePoints[i-1].DSPSetting, Emitter_Volume_CurvePoints[i].DSPSetting);

      return val;
    }
  }

  return 0;
}

/// estimate loudness of a sound before creating it
float SoundSystemXA2::GetLoudness(WaveKind kind, Vector3Par pos, float volume, float accomodation) const
{
  Vector3 vecTmp = X3DVEC_2_VEC3(_listener.Position);
  float dist = vecTmp.Distance(pos);

  float adjust = _volumeAdjustEffect;
  switch (kind)
  {
  case WaveSpeech: adjust = _volumeAdjustSpeech; break;
  case WaveSpeechEx: adjust = _volumeAdjustSpeech; break;
  case WaveMusic: adjust = _volumeAdjustMusic; break;
  }

  float minDistanceVol = floatMax(volume*adjust*accomodation, 0); // Floating point trap experienced without this saturation (volume was < 0)
  float minDistance =  floatMax(minDistanceVol * 20, 1e-6);

  const float maxLoud = 3;
  if (minDistance >= maxLoud * dist) return maxLoud;
  return DistanceRolloff(minDistance, dist);
}

void WaveXA2::SetMaxDistance(float distance)
{
  if (distance < 0.1f) distance = _volume * 1000.0f; // 0dB = 1.0 -> 1000m
  _maxEmitterDist = floatMax(0.1f, distance);
}

float WaveXA2::GetLoudness() const
{
  if (_only2DEnabled)
  {
    return GetVolume() / _distance2D;
  }
  else
  {
    if (_3DEnabled)
    {
      Vector3 vecTmp = X3DVEC_2_VEC3(_soundSys->_listener.Position);
      float dst = vecTmp.Distance(GetPosition());

#if MAX_LIST_DISTANCE
      if (_maxEmitterDist > 0.0f)
      {
        float vol = CalcVolumeFromCurve(dst, _maxEmitterDist);
        // multiply amplitude
        return (vol * _volume * _accomodation);
      }

      // when dist no set
      return 0;
#else
      float dist = _3DEnabled ? dst : _distance2D;
      float minDistance= CalcMinDistance();
      const float maxLoud = 3;

      //if( minDistance/dist>=maxLoud ) return maxLoud;
      if( minDistance>=maxLoud*dist ) return maxLoud;
      return DistanceRolloff(minDistance,dist);
#endif
    }
    else
    {
      float volume = sqrt(_volume);
      return volume;
    }
  }
}

bool WaveXA2::Get3D() const
{
  return WaveBuffersXA2::Get3D();
}

float WaveXA2::Distance2D() const
{
  const float hwFactor = 1;
  if (_only2DEnabled) return hwFactor;
  Assert(!_3DEnabled);
  return _distance2D*hwFactor;
}

void WaveXA2::Set3D(bool is3D, float distance2D)
{
  if (distance2D<0) distance2D = 1;
  if (is3D!=_3DEnabled || distance2D!=_distance2D)
  {
    //LogF("Change mode of %s to %d",(const char *)Name(),is3D);
    // stop buffer to avoid clicks during switchingIUpda

    DoStop();
    WaveBuffersXA2::Set3D(is3D,distance2D);
  }
}

void WaveXA2::SetRadio(bool isRadio)
{
  if (isRadio!=_isRadio)
  {
    DoStop();
    WaveBuffersXA2::SetRadio(isRadio);
  }
}

void WaveXA2::UpdatePosition()
{ 
  if (!_autoUpdateEnable) return;

  if( !_playing ) return;

  if (_source) Calculate3DAudio(false);
}

void WaveXA2::SetPosition(Vector3Par pos, Vector3Par vel, bool immediate)
{ 
  VEC3_2_X3DVEC(pos, _emitter.Position);
  // VEC3_2_X3DVEC(vel, _emitter.Velocity);

  // fixed: velocity set to (NaN, NaN, NaN)
  if (_finite(vel.X()) || _finite(vel.Y()) || _finite(vel.Z()))
  {
    VEC3_2_X3DVEC(vel, _emitter.Velocity);
  }

  _posValid = true;

  if( !_playing ) return;

  //LogF("SetPos %s",(const char *)Name());
  if (_source) Calculate3DAudio(immediate);
}

bool WaveBuffersXA2::GetMuted() const
{
  return _gainSet <= 1e-20f;
}

int XA2Float2MiliBel(float val)
{
  // for voltage 20*log10(gain) should be used
  // 20 dB is 10x
  if( val<=0 ) return -10000;
  int ret=toIntFloor(log10(val)*2000);
  saturate(ret,-10000,0);
  //Log("float2db %f %d",val,ret);
  return ret;
}

void WaveBuffersXA2::UpdateVolume()
{
  // for 3D sounds and 2D sounds which are part of the environment simulate obstructions
  float volumeAdjust = _volumeAdjust * _whistling; // global volume control

  if (!_only2DEnabled &&_3DEnabled || _accomodation)
  {
    // no HW obstruction/occlusion
    // we use total volume to simulate it
    float obsVolume = 1-(1-_obstruction)*0.7f;
    // assume occlusion reduces the sound much more than obstruction
    float occVolume = 1-(1-_occlusion)*0.9f;
    float volumeFactor = floatMin(obsVolume,occVolume);
    volumeAdjust *= volumeFactor;
  }

  if (_only2DEnabled || !_3DEnabled)
  {
    // some problem with setting parameters
    // convert _volume do Db
    float vol = _volume*_accomodation*volumeAdjust;
    if (!_only2DEnabled)
    {
      // this is used for sounds which are 3D, but we use them as 2D
      // typical example are sounds of my own vehicle

      // for them we want:
      // 1) never go over certain volume level
      // 2) respect ear accomodation
      // 3) respect external control
      // 2) and 3) are both controlled by _accomodation
      static float maxVolume = 0.5f;
      static float volumeCoef = 2.0f;
      float volume = sqrt(_volume) * _accomodation * volumeCoef;

      // limit volume - 1) 
      if (volume > maxVolume) volume = maxVolume;
      vol = volume * volumeAdjust;

#if _ENABLE_REPORT
      if (InterestedIn(GetDebugName()))
      {
        LogF("%s: 2D OO %d,%d",cc_cast(GetDebugName()),_obstructionSet,_occlusionSet);
      }
#endif
    }
    SetGain(vol);
#if _ENABLE_REPORT
    if (InterestedIn(GetDebugName()))
    {
      LogF("%s: 2D vol %g",cc_cast(GetDebugName()),_gainSet);
    }
#endif
  }
  else
  {
    SetGain(volumeAdjust);
#if _ENABLE_REPORT
    if (InterestedIn(GetDebugName()))
    {
      LogF("%s: vol %g",cc_cast(GetDebugName()),_gainSet);
    }
#endif
  }
}

void WaveBuffersXA2::SetObstruction(float obstruction, float occlusion, float deltaT)
{
  // change obstruction gradually
  const float minEps = 0.01f;
  if (
    fabs(occlusion - _occlusion) < minEps * floatMin(_occlusion, occlusion) &&
    fabs(obstruction - _obstruction) < minEps * floatMin(_obstruction, obstruction)
    )
  {
    return;
  }

  if (deltaT <= 0)
  {
    _occlusion = occlusion;
    _obstruction = obstruction;
  }
  else
  {
    float delta;
    delta = occlusion - _occlusion;
    const float maxSpeed = 1.5f;
    Limit(delta, -deltaT * maxSpeed, +deltaT * maxSpeed);
    _occlusion += delta;

    delta = obstruction - _obstruction;
    Limit(delta, -deltaT * maxSpeed, +deltaT * maxSpeed);
    _obstruction += delta;
  }

  UpdateVolume();
}

void WaveBuffersXA2::SetAccomodation(float acc)
{
  if( !_enableAccomodation ) return;
  if (fabs(_accomodation - acc) < 0.01f)
  {
    return;
  }
  DoAssert(acc >= 0);
  _accomodation = (acc < 0.01) ? 0 : acc;

  UpdateVolume();
}

void WaveBuffersXA2::Set3D(bool is3D, float distance2D)
{
  // disable 3D processing
  Assert(!_3DEnabled || !_only2DEnabled);
  _3DEnabled = is3D;
  _distance2D = distance2D;
}

void WaveBuffersXA2::SetRadio(bool isRadio)
{
  _isRadio = isRadio;
}

bool WaveBuffersXA2::Get3D() const
{
  Assert(!_3DEnabled || !_only2DEnabled);
  return _3DEnabled && !_only2DEnabled;
}

IXAudio2MasteringVoice *WaveXA2::DesiredContext() const
{
#if SEPARATE_2D_CONTEXT
  if (!_3DEnabled || _only2DEnabled) return _soundSys->_context2D;
  return _soundSys->_context3D;
#else
  return _soundSys->_context3D;
#endif
}

void WaveBuffersXA2::SetKind(SoundSystemXA2 * sys, WaveKind kind)
{
  _kind = kind;
  float vol = sys->_volumeAdjustEffect;
  if (_kind == WaveMusic) vol = sys->_volumeAdjustMusic;
  else if (_kind == WaveSpeech) vol = sys->_volumeAdjustSpeech;
  else if (_kind == WaveSpeechEx) vol = sys->_volumeAdjustSpeech;
  _volumeAdjust = vol;

  UpdateVolume();
}

void WaveBuffersXA2::SetSendList(const XAUDIO2_VOICE_SENDS &sendList)
{
  _sendList.SendCount = sendList.SendCount;
  _sendList.pSends = sendList.pSends;

  if (_sourceOpen && _source) 
  {
    HRESULT hr = _source->SetOutputVoices(&_sendList);
    CHECK_XA2_ERROR(hr, "WaveBuffersXA2::SetSendList - SetOututVoices")
  }
}

void WaveBuffersXA2::SetVolumeAdjust(float volEffect, float volSpeech, float volMusic)
{
  float vol = volEffect;
  if (_kind==WaveMusic) vol = volMusic;
  else if (_kind==WaveSpeech) vol = volSpeech;
  _volumeAdjust = vol;

  UpdateVolume();
}

void WaveBuffersXA2::SetVolume( float vol, bool imm )
{
  _volume=vol;

  UpdateVolume();
}

void WaveXA2::SetFrequency( float freq, bool immediate )
{
  static float minFreqDiff = 1e-4f;
  // we can assume freq is usually something around 1
  if (fabs(freq - _freqSet) > minFreqDiff * freq)
  {
    SaturateFrequency(freq);
    _freqSet = freq;

    //LogF("%s: ratio %g",(const char *)Name(),ratio);
    if( _playing && _sourceOpen )
    {
      WAV_COUNTER(wavSV,1);
      HRESULT hr = _source->SetFrequencyRatio(_freqSet);
      CHECK_XA2_ERROR(hr, "SetFrequencyRatio");
    }
  }
}

void WaveXA2::SetVolume( float volume, float freq, bool immediate )
{
  if (freq >= 0)
  {
    WaveXA2::SetFrequency(freq,immediate);
  }
  // recalculate volume using master volume
  WaveBuffersXA2::SetVolume(volume,immediate);
}

void WaveXA2::SetEarWhistling(float whistling, bool imm)
{
  _whistling = whistling;
  saturate(_whistling, 0, 1);
  if (imm) WaveBuffersXA2::UpdateVolume();
}


void WaveXA2::SetEWPars(bool enable, const Vector3 &pos, float maxDist)
{
  _enableWhistling = enable;
  _whistlingMaxDistance = maxDist;
  _whSourcePos = pos;
}

Vector3 WaveXA2::GetWhPos()
{
  // only once can be used as source
  _enableWhistling = false;
  return _whSourcePos;
}

// Sound system
SoundSystemXA2::SoundSystemXA2()
:_soundReady(false)
{
  New((HWND)0, true);
}
SoundSystemXA2::SoundSystemXA2(HWND hwnd, bool dummy)
:_soundReady(false)
{
  RegisterFreeOnDemandSystemMemory(this);
  New(hwnd, dummy);
}

bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
void SaveUserParams(ParamFile &cfg);

SoundSystemXA2::~SoundSystemXA2()
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile config;
  if (ParseUserParams(config, &globals))
  {
    SaveConfig(config);
    SaveUserParams(config);
  }

  Delete();
}

int GetMasterVolume()
{
  do {
    // obtain a handle to the mixer device
    HMIXER hMixer;
    if (mixerOpen(&hMixer, MIXER_OBJECTF_MIXER, 0, 0, 0) !=  MMSYSERR_NOERROR)
      break;
    // get the speaker line of the mixer device
    MIXERLINE ml = {0};
    ml.cbStruct = sizeof(MIXERLINE);
    ml.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;
    if ( mixerGetLineInfo((HMIXEROBJ) hMixer, &ml, MIXER_GETLINEINFOF_COMPONENTTYPE) != MMSYSERR_NOERROR)
      break;
    // get the volume control of the speaker line
    MIXERLINECONTROLS mlc = {0};
    MIXERCONTROL mc = {0};
    mlc.cbStruct = sizeof(MIXERLINECONTROLS);
    mlc.dwLineID = ml.dwLineID;
    mlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
    mlc.cControls = 1;
    mlc.pamxctrl = &mc;
    mlc.cbmxctrl = sizeof(MIXERCONTROL);
    if ( mixerGetLineControls((HMIXEROBJ) hMixer, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE) != MMSYSERR_NOERROR)
      break;
    // set the volume level
    MIXERCONTROLDETAILS mcd = {0};
    MIXERCONTROLDETAILS_UNSIGNED mcdu = {0};
    mcd.cbStruct = sizeof(MIXERCONTROLDETAILS);
    mcd.hwndOwner = 0;
    mcd.dwControlID = mc.dwControlID;
    mcd.paDetails = &mcdu;
    mcd.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
    mcd.cChannels = 1;
    if ( mixerGetControlDetails((HMIXEROBJ) hMixer, &mcd, MIXER_GETCONTROLDETAILSF_VALUE) != MMSYSERR_NOERROR)
      break;
    //RptF("Master volume get: %d", mcdu.dwValue);
    return mcdu.dwValue;
  } while (false);
  return 65535;
}

void SetMasterVolume(int value)
{
  do {
    // obtain a handle to the mixer device
    HMIXER hMixer;
    if (mixerOpen(&hMixer, MIXER_OBJECTF_MIXER, 0, 0, 0) !=  MMSYSERR_NOERROR)
      break;
    // get the speaker line of the mixer device
    MIXERLINE ml = {0};
    ml.cbStruct = sizeof(MIXERLINE);
    ml.dwComponentType = MIXERLINE_COMPONENTTYPE_DST_SPEAKERS;
    if ( mixerGetLineInfo((HMIXEROBJ) hMixer, &ml, MIXER_GETLINEINFOF_COMPONENTTYPE) != MMSYSERR_NOERROR)
      break;
    // get the volume control of the speaker line
    MIXERLINECONTROLS mlc = {0};
    MIXERCONTROL mc = {0};
    mlc.cbStruct = sizeof(MIXERLINECONTROLS);
    mlc.dwLineID = ml.dwLineID;
    mlc.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
    mlc.cControls = 1;
    mlc.pamxctrl = &mc;
    mlc.cbmxctrl = sizeof(MIXERCONTROL);
    if ( mixerGetLineControls((HMIXEROBJ) hMixer, &mlc, MIXER_GETLINECONTROLSF_ONEBYTYPE) != MMSYSERR_NOERROR)
      break;
    // set the volume level
    MIXERCONTROLDETAILS mcd = {0};
    MIXERCONTROLDETAILS_UNSIGNED mcdu = {0};
    mcdu.dwValue = value; // the volume is a number between 0 and 65535
    mcd.cbStruct = sizeof(MIXERCONTROLDETAILS);
    mcd.hwndOwner = 0;
    mcd.dwControlID = mc.dwControlID;
    mcd.paDetails = &mcdu;
    mcd.cbDetails = sizeof(MIXERCONTROLDETAILS_UNSIGNED);
    mcd.cChannels = 1;
    if ( mixerSetControlDetails((HMIXEROBJ) hMixer, &mcd, MIXER_SETCONTROLDETAILSF_VALUE) != MMSYSERR_NOERROR)
      break;
    //RptF("Master volume set: %d", value);
  } while (false);
}

/*!
\patch 5118 Date 1/17/2007 by Ondra
- Fixed: Crash after changing Creative XFi mode from Gaming to Entertainment
or after disabling full acceleration.
*/
void SoundSystemXA2::LoadConfig(ParamEntryPar config)
{
  float cdVolume = GetDefaultCDVolume();
  float fxVolume = GetDefaultWaveVolume();
  float speechVolume = GetDefaultSpeechVolume();
  float vonVolume = GetDefaultVoNVolume();
  if
    (
    config.FindEntry("volumeCD") && config.FindEntry("volumeFX")
    && config.FindEntry("volumeSpeech") && config.FindEntry("volumeVoN")
    )
  {
    cdVolume=config>>"volumeCD";
    fxVolume=config>>"volumeFX";
    speechVolume=config>>"volumeSpeech";
    vonVolume=config>>"volumeVoN";
  }
  if (config.FindEntry("masterVolume"))
  {
    int masterVolume = config>>"masterVolume";
    SetMasterVolume(masterVolume);
  }
  
  int maxSamples = GetSamplesCountDefault();
  if(config.FindEntry("maxSamplesPlayed"))
  {
    maxSamples=config>>"maxSamplesPlayed";
  }

  float vonRecThr = GetDefaultVoNRecThreshold();
  if (config.FindEntry("vonRecThreshold"))
  {
    vonRecThr = config >> "vonRecThreshold";
  }

  SetCDVolume(cdVolume);
  SetWaveVolume(fxVolume);
  SetSpeechVolume(speechVolume);
  SetVonVolume(vonVolume);
  SetSamplesCount(maxSamples);
  SetVoNRecThreshold(vonRecThr);

#if _FORCE_SINGLE_VOICE
  Glob.config.singleVoice = true;
#else
  Glob.config.singleVoice = false;
  ConstParamEntryPtr entry = config.FindEntry("singleVoice");
  if (entry) Glob.config.singleVoice = *entry;
#endif

  //if (_commited && _xa2Device)
  //{
  //  // if the device is already working, we need to reset it
  //  ResetDevice(); // make device reset has no sense
  //}
}

void SoundSystemXA2::SaveConfig(ParamFile &config)
{
  if (!IsOutOfMemory())
  {
    config.Add("volumeCD",GetCDVolume());
    config.Add("volumeFX",GetWaveVolume());
    config.Add("volumeSpeech",GetSpeechVolume());
    config.Add("volumeVoN",GetVonVolume());
    config.Add("masterVolume", GetMasterVolume());
    config.Add("singleVoice", Glob.config.singleVoice);
    config.Add("maxSamplesPlayed", _maxSoundsPlayed);
    config.Add("vonRecThreshold", GetVoNRecThreshold());
  }
}

void SoundSystemXA2::ResetDevice()
{
  _waves.RemoveNulls();

  if (_active)
  {
    // we must restart all playing buffers
    // stop buffers
    for (int i=0; i<_waves.Size(); i++)
    {
      WaveXA2 *wave = _waves[i];
      bool wasPlaying = wave->_playing;
      if (wasPlaying)
      {
        wave->Stop();
      }
      if (wave->_loaded)
      {
        wave->Unload();
      }
      if (wasPlaying)
      {
        wave->_wantPlaying = true;
      }
    }

    _cache.Clear();

    DestroyDevice();
    CreateDevice();
    // clear cache - release all stored sounds

    // start them again
    for (int i=0; i<_waves.Size(); i++)
    {
      WaveXA2 *wave = _waves[i];
      if (wave->_wantPlaying && wave->DoPlay())
      {
        wave->_wantPlaying = false;
      }
    }
  }
}

bool SoundSystemXA2::CanChangeEAX() const
{
  return GetNetworkManager().GetClientState() == NCSNone;
}

bool SoundSystemXA2::CanChangeHW() const
{
  return GetNetworkManager().GetClientState()==NCSNone;
}

bool SoundSystemXA2::EnableHWAccel(bool val)
{
  return true;
}

bool SoundSystemXA2::EnableEAX(bool val)
{
  return false;
}

bool SoundSystemXA2::GetEAX() const
{
  return false;
}

bool SoundSystemXA2::GetHWAccel() const
{
  return false;
}

bool SoundSystemXA2::GetDefaultEAX() const
{
  // by default always try to use EAX
  return true;
  //if (_canHW && _hwEnabled) return _canEAX && _nativeSpecifier.GetLength()>0;
  //return _canEAX;
}

bool SoundSystemXA2::GetDefaultHWAccel() const
{
  return false;
}
/*
static int OpenALExcFilter(unsigned int code, struct _EXCEPTION_POINTERS *ep)
{

LONG lDisposition = EXCEPTION_EXECUTE_HANDLER;
PDelayLoadInfo pdli = PDelayLoadInfo(ep->ExceptionRecord->ExceptionInformation[0]);

switch (ep->ExceptionRecord->ExceptionCode) {
case VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND):
RptF("Dll %s was not found", pdli->szDll);
break;

case VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND):
if (pdli->dlp.fImportByName) {
RptF("Function %s was not found in %s",
pdli->dlp.szProcName, pdli->szDll);
} else {
RptF("Function ordinal %d was not found in %s",
pdli->dlp.dwOrdinal, pdli->szDll);
}
break; 

default:
// Exception is not related to delay loading
// still, it happened while delay loading, and we want to handle it
break;
}

return lDisposition;
}
*/

/*static bool CheckOpenAL()
{
# define SUPPORT_URL "http://community.bistudio.com/wiki/ArmA-crash-OpenAL"
__try
{
HRESULT loadErr = __HrLoadAllImportsForDll("openal32.dll");
if (FAILED(loadErr))
{
WarningMessage("Missing OpenAL32.DLL\nVisit " SUPPORT_URL " to learn more.");
return false;
}
}
__except (OpenALExcFilter(GetExceptionCode(), GetExceptionInformation()))
{
// Handle the error. Errors will reach here only if
// the hook function could not fix it.
// we still might try to ask OpenAL what version it its
WarningMessage("Bad version of OpenAL32.DLL\nVisit " SUPPORT_URL " to learn more.");
return false;
}
return true;
}
*/

void SoundSystemXA2::SetSamplesCount(int val) 
{
  _maxSoundsPlayed = val;
  saturate(_maxSoundsPlayed, GetSamplesCountMin(), GetSamplesCountMax());
}

/*!
\patch_internal 1.11 Date 8/1/2001 by Ondra
- New: possibility of dummy sound system - no DirectSound objects are ever created.

\patch 5145 Date 3/23/2007 by Ondra
- Fixed: HW acceleration used only when sound card offers at least 60 simultaneous sources.
This fixes missing sounds while playing MP.
*/
void SoundSystemXA2::New(HWND winHandle, bool dummy)
{
  _context3D = NULL;
#if SEPARATE_2D_CONTEXT
  _context2D = NULL;
#endif
  _xa2Device.Free();
  _captureRunning = false;

#if !_DISABLE_GUI
  if (dummy)
#endif
  {
    LogF("No sound - command line switch");
NoSound:
    _soundReady=true; 
    _commited=false;
    _maxChannels=31;
    _maxFreq=100000;
    _minFreq=10;
    _enablePreview = true;
    _doPreview = false;
    
    _speechVolWanted = GetDefaultSpeechVolume();
    _waveVolWanted = GetDefaultWaveVolume();
    _musicVolWanted = GetDefaultCDVolume();
    _vonVolWanted = GetDefaultVoNVolume();
    _vonRecThrWanted = GetDefaultVoNRecThreshold();
    _active = true;
    _captureRunning = false;
    GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
    ParamFile config;
    ParseUserParams(config, &globals);
    LoadConfig(config);

    UpdateMixer();

    return; 
  }

#if !_DISABLE_GUI
  _enablePreview = false; // preview disabled until sound initialized
  _doPreview = false;

  D3DVECTOR zeroVec = { 0, 0, 0 };
  D3DVECTOR frontVec = { 0, 0, 1 };
  D3DVECTOR upVec = { 0, 1, 0 };

  _listener.Position = zeroVec;
  _listener.Velocity = zeroVec;
  _listener.OrientFront = frontVec;
  _listener.OrientTop = upVec;

  _listenerTime = 0;

  _volumeAdjustEffect=1;
  _volumeAdjustSpeech=1;
  _volumeAdjustMusic=1;
  _volumeAdjustVoN=1;

  _waveVolWanted = GetDefaultWaveVolume();
  _musicVolWanted = GetDefaultCDVolume();
  _speechVolWanted = GetDefaultSpeechVolume();
  _vonVolWanted = GetDefaultVoNVolume();
  _vonRecThrWanted = GetDefaultVoNRecThreshold();
  _active = true;

  _customSubmixFilter = false;

  UpdateMixer();  

  {
    GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
    ParamFile config;
    ParseUserParams(config, &globals);
    LoadConfig(config);
  }

  Assert(!_soundReady);

  _environment.type = SEPlain;
  _environment.density = 0.2;
  _environment.size = 100;
 
  CreateDevice();

  if(!_soundReady) goto NoSound;

  // TODO: _maxChannels auto-detection
  _maxChannels = 31; // XAUDIO2_MAX_QUEUED_BUFFERS
  _maxFreq = 100000;
  _minFreq = 10;

  _enablePreview = true; // preview disabled until sound initialized
  _doPreview = false;

#endif
}

const float LeftSpeaker   = 1.0f;
const float RightSpeaker  = 1.0f;
const float CentreSpeaker = 0.5f;
const float LFESpeaker    = 0.2f;

// mono samples
const float MonoChannel1[]    = { CentreSpeaker };                                            // SPEAKER_MONO
const float StereoChannels1[] = { LeftSpeaker, RightSpeaker };                                // SPEAKER_STEREO
const float TwoPOne1[]        = { LeftSpeaker, RightSpeaker, LFESpeaker };                    // SPEAKER_2POINT1
const float Quad1[]           = { LeftSpeaker, RightSpeaker, 0, 0 };                          // SPEAKER_QUAD
const float Surround1[]       = { LeftSpeaker, RightSpeaker, CentreSpeaker, 0 };              // SPEAKER_SURROUND
const float FourPOne1[]       = { LeftSpeaker, RightSpeaker, LFESpeaker, 0, 0 };              // SPEAKER_4POINT1
const float FivePOne1[]       = { LeftSpeaker, RightSpeaker, CentreSpeaker, LFESpeaker, 0, 0 };// SPEAKER_5POINT1
const float SevenPOne1[]      = { LeftSpeaker, RightSpeaker, CentreSpeaker, LFESpeaker, 0, 0, 0, 0 }; // SPEAKER_7POINT1

// stereo samples
const float MonoChannel2[]    = { CentreSpeaker * 0.5, CentreSpeaker * 0.5 };
const float StereoChannels2[] = { LeftSpeaker, 0, RightSpeaker, 0 };
const float TwoPOne2[]        = { LeftSpeaker, 0, RightSpeaker, 0, LFESpeaker * 0.5, LFESpeaker * 0.5 };
const float Quad2[]           = { LeftSpeaker, 0, RightSpeaker, 0, 0, 0, 0, 0 };
const float Surround2[]       = { LeftSpeaker, 0, RightSpeaker, 0, CentreSpeaker * 0.5, CentreSpeaker * 0.5, 0, 0 } ;
const float FourPOne2[]       = { LeftSpeaker, 0, RightSpeaker, 0, LFESpeaker * 0.5, LFESpeaker * 0.5, 0, 0, 0, 0 };
const float FivePOne2[]       = { LeftSpeaker, 0, RightSpeaker, 0, LFESpeaker * 0.5, LFESpeaker * 0.5, CentreSpeaker * 0.5, CentreSpeaker * 0.5, 0, 0, 0, 0 };
const float SevenPOne2[]      = { LeftSpeaker, 0, RightSpeaker, 0, LFESpeaker * 0.5, LFESpeaker * 0.5, CentreSpeaker * 0.5, CentreSpeaker * 0.5, 0, 0, 0, 0, 0, 0, 0, 0 };

const float *OneChannleMap[] = 
{
  MonoChannel1, 
  StereoChannels1,
  TwoPOne1,
  Quad1,
  Surround1,
  FourPOne1,
  FivePOne1,
  SevenPOne1,
  SevenPOne1
};

const float *TwoChannelMap[] =
{
  MonoChannel2, 
  StereoChannels2,
  TwoPOne2,
  Quad2,
  Surround2,
  FourPOne2,
  FivePOne2,
  SevenPOne2,
  SevenPOne2,
};

static inline const float* GetOutputMatrix(int scrChannels, int destChannels)
{
  const float *map = NULL;

  if (scrChannels < 1 || scrChannels > 2) return map;
  if (destChannels < 1 || destChannels > 8) return map;
  
  // now we support 1 or 2 source channels in samples
  const float **channelMap = (scrChannels == 1) ? OneChannleMap : TwoChannelMap;
  map = channelMap[destChannels];

  return map;
}

/* Set audio debug configuration */
static inline void SetDebugDiag(IXAudio2* audioDevice)
{
  XAUDIO2_DEBUG_CONFIGURATION debConfiguration = { 0 };

  debConfiguration.TraceMask = 0;//XAUDIO2_LOG_FUNC_CALLS;// | XAUDIO2_LOG_LOCKS;
  debConfiguration.BreakMask = XAUDIO2_LOG_ERRORS | XAUDIO2_LOG_WARNINGS;
  debConfiguration.LogTiming = 0;

  audioDevice->SetDebugConfiguration(&debConfiguration);
}

/* Audio device intialization */
static inline bool CreateAudioDevice(ComRef<IXAudio2> &audioDevice)
{
  UINT32 flags = 0;
  XAUDIO2_PROCESSOR proc = XAUDIO2_DEFAULT_PROCESSOR;

#ifdef _DEBUG
  flags |= XAUDIO2_DEBUG_ENGINE;
#endif

  HRESULT hr;
  if ( FAILED(hr = XAudio2Create(audioDevice.Init(), flags, proc)) )
  {
    RptF("Error: Create XAudio2 device failed, error %x", hr);
    audioDevice.Free();
    return false;
  }
  else
  {
#ifdef _DEBUG
    SetDebugDiag(audioDevice);
#endif
  }

  return true;
}

static WAVEFORMATEX DefaultWaveFormat()
{
  WAVEFORMATEX fmt = {0};

  fmt.nChannels = 2;
  fmt.nSamplesPerSec = 44100;
  fmt.wBitsPerSample = 16;
  fmt.nBlockAlign = (fmt.wBitsPerSample / 8) * fmt.nChannels;
  fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
  fmt.wFormatTag = WAVE_FORMAT_44S16; // (2 << 16) - 1;
  return fmt;
}

static bool SelectOutputFormat(IXAudio2 *audioDevice, DWORD &deviceIndex, WAVEFORMATEX &fmt, DWORD &channelMask)
{
  channelMask = SPEAKER_STEREO;

  deviceIndex = 0;

  UINT32 devCount = 0;
  HRESULT hr = audioDevice->GetDeviceCount(&devCount);
  if ( SUCCEEDED(hr) )
  {
    XAUDIO2_DEVICE_DETAILS details = { 0 };

    // first look for game device
    for (UINT32 i = 0; i < devCount; i++)
    {
      hr = audioDevice->GetDeviceDetails(i, &details);
      if ( FAILED(hr) ) 
      {
        RptF("Warning: Function GetDeviceDetails failed, error: %x.", hr);
        return false;
      }

      if (details.Role == DefaultGameDevice)
      {
        deviceIndex = i;
        fmt = details.OutputFormat.Format;
        channelMask = details.OutputFormat.dwChannelMask;
        return true;
      }
    }

    // second loop look for any device
    for (UINT32 i = 0; i < devCount; i++)
    {
      hr = audioDevice->GetDeviceDetails(i, &details);
      if ( FAILED(hr) ) 
      {
        RptF("Warning: Function GetDeviceDetails failed, error: %x.", hr);
        return false;
      }

      if (details.Role == GlobalDefaultDevice)
      {
        deviceIndex = i;
        fmt = details.OutputFormat.Format;
        channelMask = details.OutputFormat.dwChannelMask;
        return true;
      }
    }
  }

  RptF("Error: Audio - enumeration output devices failed.");
  fmt = DefaultWaveFormat();
  return false;
}

#define _NEW_MASTER_VOICE 1

static inline bool CreateMasteringVoices(IXAudio2 *device, DWORD deviceIndex, IXAudio2MasteringVoice** ppContext3D, IXAudio2MasteringVoice** ppContext2D)
{
  // Creates and configures a mastering voice
  if (ppContext3D)
  {
    *ppContext3D = NULL;
    if ( FAILED(
      device->CreateMasteringVoice(ppContext3D, XAUDIO2_DEFAULT_CHANNELS, XAUDIO2_DEFAULT_SAMPLERATE, 0, deviceIndex)) )
    {
      RptF("Warning: Creation MasteringVoice failed - no sound will be played.");
      return false;
    }
  }

  return true;
}

  #define ERROR_INIT_AUDIO_DEVICE  initFail = true; goto InitError; 

void SoundSystemXA2::CreateDevice()
{
  // no buffers or source can exist at this point
  _waves.RemoveNulls();

  Assert(_waves.Size() == 0);
  Assert(_cache.Size() == 0);

  HRESULT hr;
  hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
  _cleanupCOM = SUCCEEDED(hr);
  
  if ( FAILED(hr) ) RptF("Error: CoInitilizeEx (XAudio2-1st trial) return %x", hr);
  bool initFail = false;

  DWORD channelMask = SPEAKER_STEREO;
  DWORD deviceIndex;

  // Creates a new XAudio2 object and returns a pointer to its IXAudio2 interface.
  if (!CreateAudioDevice(_xa2Device))
  {
    ERROR_INIT_AUDIO_DEVICE
  }

  // Select output format
  if (!SelectOutputFormat(_xa2Device.GetRef(), deviceIndex, _outFmt, channelMask))
  {
    ERROR_INIT_AUDIO_DEVICE
  }


#if _NEW_MASTER_VOICE
  if (!_masteringVoice.CreateVoice(_xa2Device, deviceIndex, _outFmt))
  {
    ERROR_INIT_AUDIO_DEVICE
  }
#else
  if (!CreateMasteringVoices(_xa2Device, deviceIndex, &_context3D, NULL))
  {
    ERROR_INIT_AUDIO_DEVICE
  }
#endif

InitError:
  // during initialization error occur, try initialize xaudio2 without
  if (initFail)
  {
    // release interface
    if (_context3D) _context3D->DestroyVoice(), _context3D = NULL;
    if (_xa2Device) _xa2Device.Free();
    if (_cleanupCOM) CoUninitialize();

    channelMask = SPEAKER_STEREO;
    _outFmt = DefaultWaveFormat();

    RptF("Warning: Audio device creation failed, attempt to create default audio:");
    RptF("SamplesPerSec: %d, channels: %d, bitsPerSample: %d", _outFmt.nSamplesPerSec, _outFmt.nChannels, _outFmt.wBitsPerSample);

    // next chance for initialization
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    _cleanupCOM = SUCCEEDED(hr);
    if ( FAILED(hr) ) RptF("Error: CoInitilizeEx (XAudio2-2nd trial) return %x", hr);

    if (!CreateAudioDevice(_xa2Device))
    {
      _xa2Device.Free();
      goto Error;
    }

#if _NEW_MASTER_VOICE
    // clear stuff
    _masteringVoice.DestroyVoice();
    if (!_masteringVoice.CreateVoice(_xa2Device, 0, _outFmt))
    {
      goto Error;
    }
#else
    if (!CreateMasteringVoices(_xa2Device, deviceIndex, &_context3D, NULL))
    {
      ERROR_INIT_AUDIO_DEVICE
    }
#endif

    RptF("Audio device successfully created with default settings.");
  }

  // 3d sound calculation flags
  _x3dCalculateFlags = X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER | X3DAUDIO_CALCULATE_LPF_DIRECT;

  // if LFE then use it
  if ((channelMask & SPEAKER_LOW_FREQUENCY) != 0) _x3dCalculateFlags |= X3DAUDIO_CALCULATE_REDIRECT_TO_LFE;

  // Audio 3D initialization
  X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, _x3dInstance);

  _soundReady = true; 
  _commited = false;
  _customSubmixFilter = false;
  
  return;

Error:
  _soundReady = false;  
  DestroyDevice();
  return;
}

void MasteringVoice::Simulate(float deltaT, bool inside)
{
  if (!_initSuccess) return;

  if (fabsf(_volumeSound - _volumeSoundWanted) > 0.001f)
  {
    _volumeSound = _volumeSoundWanted;
    _submixSound->SetVolume(_volumeSound);
  }

  if (fabsf(_volumeRadio - _volumeRadioWanted) > 0.001f)
  {
    _volumeRadio = _volumeRadioWanted;
    _submixSpeech->SetVolume(_volumeRadio);
  }

  if (fabsf(_volumeSEffect - _volumeSEffectWanted) > 0.001f)
  {
    _volumeSEffect = _volumeSEffectWanted;
    _submixSpeechEx->SetVolume(_volumeSEffect);
  }

#if 0
  static bool EnableFilterUpdate = false;

  if (EnableFilterUpdate)
{
    FilterParamsEx radioPars = FilterParamsEx::Empty();
    RadioBandPassFilter rbpFilter;
    rbpFilter(radioPars, 0, true);
    _submixRadio->SetFilterParameters(radioPars.GetXA2ParsPtr());
  
    EnableFilterUpdate = false;
}
#endif
}

IXAudio2Voice* MasteringVoice::SendMasterVoice(DestVoice voice) const
{
  DoAssert(voice >= 0 && voice < VoiceCount);
  return _mapTable[voice < VoiceCount ? voice : VoiceNotSet];
}

bool MasteringVoice::CreateVoice(IXAudio2 *device, DWORD deviceIndex, WAVEFORMATEX &fmt)
{
  _initSuccess = false;

  LogF("CreateVoice, channels: %d, freq: %d, wBits: %d", fmt.nChannels, fmt.nSamplesPerSec, fmt.wBitsPerSample);

  HRESULT hr = device->CreateMasteringVoice(&_master, fmt.nChannels, fmt.nSamplesPerSec, 0, deviceIndex);  
  if ( FAILED(hr) ) 
  {
    RptF("Error: Mastering voice creation failed - no sound will be played, with error: %x.", hr);
    return false;
  }

  hr = device->CreateSubmixVoice(&_submixSound, fmt.nChannels, fmt.nSamplesPerSec, 0, 2);
  if ( FAILED(hr) ) 
  {
    RptF("Error: Submix voice creation failed, with error: %x.", hr);
    return false;
  }

  hr = device->CreateSubmixVoice(&_submixSpeech, fmt.nChannels, fmt.nSamplesPerSec, 0, 2);
  if ( FAILED(hr) ) 
  {
    RptF("Error: Submix voice creation failed, with error: %x.", hr);
    return false;
  }

  // set radio submix output
  XAUDIO2_SEND_DESCRIPTOR sendDesc = {XAUDIO2_SEND_USEFILTER, _submixSpeech};
  XAUDIO2_VOICE_SENDS sendList = {1, &sendDesc};

  hr = device->CreateSubmixVoice(&_submixRadio, fmt.nChannels, fmt.nSamplesPerSec, XAUDIO2_VOICE_USEFILTER, 1, &sendList);
  if ( FAILED(hr) ) 
  {
    RptF("Error: Submix radio creation failed, with error: %x.", hr);
    return false;
  }

  // set radio filter - filter is permanent
  FilterParamsEx radioPars = FilterParamsEx::Empty();
  RadioBandPassFilter rbpFilter;
  rbpFilter(radioPars, 0, true);
  _submixRadio->SetFilterParameters(radioPars.GetXA2ParsPtr());

  hr = device->CreateSubmixVoice(&_submixSpeechEx, fmt.nChannels, fmt.nSamplesPerSec, 0, 1);
  if ( FAILED(hr) )
  {
    RptF("Error: Submix voice creation failed, with error: %x.", hr);
    return false;
  }

  hr = device->CreateSubmixVoice(&_submixPreview, fmt.nChannels, fmt.nSamplesPerSec, 0, 1);
  if ( FAILED(hr) )
  {
    RptF("Error: Submix preview creation failed, with error: %x.", hr);
    return false;
  }

  // set custom submix output, custom submix is before soundSubmix
  XAUDIO2_SEND_DESCRIPTOR sendDescC = {XAUDIO2_SEND_USEFILTER, _submixSound};
  XAUDIO2_VOICE_SENDS sendListC = {1, &sendDescC};
  hr = device->CreateSubmixVoice(&_submixCustom, fmt.nChannels, fmt.nSamplesPerSec, XAUDIO2_VOICE_USEFILTER, 1, &sendListC);
  if ( FAILED(hr) ) 
  {
    RptF("Error: Custom submix voice creation failed, with error: %x.", hr);
    return false;
  }

  _mapTable[Voice3D] = static_cast<IXAudio2Voice*>(_submixSound);
  _mapTable[Voice2D] = static_cast<IXAudio2Voice*>(_submixSpeech);
  _mapTable[Voice2DRadio] = static_cast<IXAudio2Voice*>(_submixRadio);
  _mapTable[Voice2DEx] = static_cast<IXAudio2Voice*>(_submixSpeechEx);
  _mapTable[VoicePreview] = static_cast<IXAudio2SubmixVoice*>(_submixPreview);
  _mapTable[CustomFilter] = static_cast<IXAudio2SubmixVoice*>(_submixCustom);
  _mapTable[VoiceNotSet] = static_cast<IXAudio2Voice*>(_master);

  _initSuccess = true;
  return _initSuccess;
}

void MasteringVoice::InitializeVoice()
{
  _submixSound = NULL;
  _submixSpeech = NULL;
  _submixRadio = NULL;
  _submixSpeechEx = NULL;
  _submixPreview = NULL;
  _master = NULL;
  _submixCustom = NULL;

  for (int i = 0; i < lenof(_mapTable); i++) _mapTable[i] = NULL;

  _initSuccess = false;
  _globSoundVolume = _volumeSound = _volumeRadio = _volumeSEffect = 1.0f;
  _globSoundVolumeWanted = _volumeSoundWanted = _volumeRadioWanted = _volumeSEffectWanted = 0.0f;
}

void MasteringVoice::DestroyVoice()
{
  if (_submixSound) _submixSound->DestroyVoice();
  if (_submixRadio) _submixRadio->DestroyVoice();
  if (_submixSpeech) _submixSpeech->DestroyVoice();
  if (_submixSpeechEx) _submixSpeechEx->DestroyVoice();
  if (_submixPreview) _submixPreview->DestroyVoice();
  if (_master) _master->DestroyVoice();
  if (_submixCustom) _submixCustom->DestroyVoice();

  _submixSound = NULL;
  _submixSpeech = NULL;
  _submixRadio = NULL;
  _submixSpeechEx = NULL;
  _submixPreview = NULL;
  _master = NULL;
  _submixCustom = NULL;
}

void SoundSystemXA2::DestroyDevice()
{
  // no buffers or source can exist at this point
#if _DEBUG
  _waves.RemoveNulls();

  for (int i=0; i<_waves.Size(); i++)
  {
    WaveXA2 *wave = _waves[i];
    Assert(wave->_buffer.IsNull());
    Assert(wave->_streamBuffers.Size()==0);
  }
  Assert(_cache.Size()==0);
#endif

  // Exit
  if (_xa2Device)
  {
    _masteringVoice.DestroyVoice();
    if (_context3D) _context3D->DestroyVoice();
    _context3D = NULL;

    _xa2Device.Free();

    if (_cleanupCOM) CoUninitialize();
  }
}

static const RString PauseName="";

WaveXA2::WaveXA2( SoundSystemXA2 *sSys, float delay ) // empty wave
:AbstractWave(PauseName)
{
  WaveBuffersXA2::Reset(sSys);
  DoConstruct();
  _soundSys=sSys;
  _only2DEnabled=true;

  _gainSet=0;
  _obstructionSet = _occlusionSet = 0;
  _obstruction = _occlusion = 1;
  _freqSet=1;
  _header._frequency = 1000;
  _header._sSize = 1;
  _header._size = 0;
  _curPosition = toLargeInt(delay*-1000); // delay time in ms
  _skipped = 0;
  _volumeAdjust = _soundSys->_volumeAdjustEffect;
  _looping = false; 
  _posValid = false;
  _loadedHeader = true;
  _loaded = true;
  _sourceOpen = false;
}

WaveXA2::WaveXA2( SoundSystemXA2 *sSys, RString name, bool is3D )
:AbstractWave(name)
{
  PROFILE_SCOPE_EX(wxa2c,sound);

  WaveBuffersXA2::Reset(sSys);

  DoConstruct();

  _soundSys=sSys;
  _only2DEnabled=!is3D;

  _posValid=!is3D; // 3d position explicitly set

  _gainSet=0;
  _obstructionSet = _occlusionSet = 0;
  _obstruction = _occlusion = 1;
  _freqSet=1;
  _curPosition = 0;
  _skipped = 0;
  _volumeAdjust = _soundSys->_volumeAdjustEffect;

  LoadHeader();
}

WaveXA2::~WaveXA2()
{
  DoDestruct();
}

void WaveXA2::CacheStore( WaveCacheXA2 &store )
{
#if DO_PERF
  ADD_COUNTER(wavCS,1);
#endif
  store._name = Name();
  store._buffer = _buffer;  // audio data
  store._header = _header;
  store._waveFmt = _waveFmt;
}

WaveCacheXA2::~WaveCacheXA2()
{
  if (!_buffer.IsNull())
  {
#if DO_PERF
    ADD_COUNTER(wavCD,1);
#endif

    _buffer.Free();

#if DIAG_LOAD
    if (refC==0)
    {
      LogF("Destroy cache %s",(const char *)_name);
    }
    else
    {
#if DIAG_LOAD>=2
      LogF("Release cache %s",(const char *)_name);
#endif
    }
#endif
  }
}

void WaveXA2::InitRadio()
{
  //if (_isRadio)
  //{
  //  const int SampleRate = 14000;

  //  // radio simulation - resampling via submix and band pass
  //  if (CreateSubmixVoice(_soundSys->_xa2Device, &_radioSubmix, _soundSys->DestinationVoice(WaveSpeech), _waveFmt.nChannels, 
  //    SampleRate, XAUDIO2_VOICE_USEFILTER, 0, NULL))
  //  {
  //    // set output voice description
  //    _sendDesc.Flags = 0;
  //    _sendDesc.pOutputVoice = (IXAudio2Voice *) _radioSubmix;
  //    _sendList.SendCount = 1;
  //    _sendList.pSends = &_sendDesc;

  //    // apply filter
  //    _filter.Frequency = XAudio2CutoffFrequencyToRadians(2500, SampleRate);
  //    _filter.OneOverQ = XAudio2CutoffFrequencyToRadians(1000, SampleRate);
  //    _filter.Type = BandPassFilter;

  //    HRESULT hr = _radioSubmix->SetFilterParameters(&_filter, XAUDIO2_COMMIT_NOW);

  //    if ( FAILED(hr) )
  //      RptF("Error: SetFilterParameters failed with result: %x", hr);
  //  }
  //}
    }

bool WaveXA2::CacheLoad(WaveCacheXA2 &store)
{
  //Log("CacheLoad %s",(const char *)Name());
  Assert(_buffer.IsNull());

  _buffer = store._buffer;
  _waveFmt = store._waveFmt;

  // no need to cache when active
  store._buffer = NULL;

  InitRadio();
  DWORD flags = _kind == WaveMusic ? XAUDIO2_VOICE_MUSIC : XAUDIO2_VOICE_USEFILTER;
  if (_isRadio) flags = 0;

  if (!CreateSourceVoice(_soundSys->_xa2Device, &_source, SelectOutputVoice(_soundSys, _kind, _isRadio, GetSticky()), _waveFmt, 
    flags, (_sendList.SendCount > 0) ? &_sendList : NULL))
  {
    _sourceOpen = false;
    _buffer.Free();
    return false;
  }

  _sourceOpen = true;

  return true;
}

bool WaveXA2::Duplicate(const WaveXA2 &src)
{
  if (src._stream || src._buffer.IsNull()) return false; // no duplicate possible on streaming buffers

  //Log("Duplicate %s",(const char *)src.Name());
#if DO_PERF
  WAV_COUNTER(wavDp,1);
#endif
  DoAssert( src.Loaded() );

  Assert(src._loadedHeader);

  // shared buffer data
  _buffer = src._buffer;

  _header = src._header;
  _buffDescr = src._buffDescr;  // XAUDIO2_BUFFER
  _waveFmt = src._waveFmt;      // format of waveform-audio data

  InitRadio();

  DWORD flags = _kind == WaveMusic ? XAUDIO2_VOICE_MUSIC : XAUDIO2_VOICE_USEFILTER;
  if (_isRadio) flags = 0;

  if (!CreateSourceVoice(_soundSys->_xa2Device, &_source, SelectOutputVoice(_soundSys, _kind, _isRadio, GetSticky()), _waveFmt, 
    flags, (_sendList.SendCount > 0) ? &_sendList : NULL))
  {
    _sourceOpen = false;
    _buffer.Free();
    return false;
  }

  LogXA2("CreateSource %d (Duplicate)", _source);

  _sourceOpen = true;
  _loaded = true;

  AddStats("Dup");

  return true;
}


void LinCross::Commit(const AutoArray<float> &origVals, float to, float timePeriod)
{
  _time = Glob.time + timePeriod;
  _timePeriod = floatMax(0.001f, timePeriod);
  _timePeriod = 1.0f / _timePeriod;
  _to = to;

  _from.Realloc(origVals.Size());
  _from.Resize(origVals.Size());

  // copy current values
  for (int i = 0; i < origVals.Size(); i++)
  {
    _from[i] = origVals[i];
  }

  _done = false;
}

void LinCross::Simulate(AutoArray<float> &vals)
{
  Assert(vals.Size() == _from.Size());

  if (Glob.time < _time)
  {
    float dt = (_time - Glob.time) * _timePeriod;

    // lin. interpolation
    for (int i = 0; i < vals.Size(); i++) { vals[i] = _to - ((_to - _from[i]) * dt); }
  }
  else
  {
    if (!_done)
    {
      for (int i = 0; i < vals.Size(); i++) { vals[i] = _to; }
      _done = true;
    }
  }
}

bool XA2Submix::Construct(SoundSystemXA2 *sSystem)
{
  if (!sSystem) return false;
  _sSystem = sSystem;

  // structures initialized only once
  if (!_initialized)
  {
    _count = 0;
    _submix = 0;
    _useCone = false;
    _iChannelsCount = 1;
    _linCrossSet = true;

    _channelAzimuth.Realloc(_iChannelsCount);
    _channelAzimuth.Resize(_iChannelsCount);
    for (int i = 0; i < _channelAzimuth.Size(); i++) 
    {
      _channelAzimuth[i] = X3DAUDIO_2PI;
    }

    // directional sound - more info can be found in directX SDK
    memset(&_cone, 0, sizeof(X3DAUDIO_CONE));
    // default set cone to false
    _useCone = false;

    // emitter
    memset(&_emitter, 0, sizeof(X3DAUDIO_EMITTER));
    _emitter.OrientFront = FrontVec;
    _emitter.OrientTop = TopVec;
    _emitter.ChannelCount = _iChannelsCount;
    //_emitter.InnerRadiusAngle = X3DAUDIO_PI/4.0f;
    _emitter.DopplerScaler = 1.0f;
#if MAX_LIST_DISTANCE
    _emitter.pVolumeCurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_Volume_Curve;
#endif
    _emitter.pLFECurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_LFE_Curve;
    _emitter.pCone = NULL;
    _emitter.pChannelAzimuths = _channelAzimuth.Data();

    // coefficients for separate channels
    int maxChannels = _iChannelsCount * _sSystem->_outFmt.nChannels;
    _matrixCoefficients.Realloc(maxChannels);
    _matrixCoefficients.Resize(maxChannels);

    for (int i = 0; i < _matrixCoefficients.Size(); i++) 
    {
      _matrixCoefficients[i] = 1.0f;
    }

    memset(&_dsp, 0, sizeof(_dsp));
    _dsp.DopplerFactor = 1.0f;
    _dsp.SrcChannelCount = _iChannelsCount;
    _dsp.DstChannelCount = _sSystem->_outFmt.nChannels;
    _dsp.pMatrixCoefficients = _matrixCoefficients.Data();

    memset(&_sendList, 0, sizeof(XAUDIO2_VOICE_SENDS));
    memset(&_sendDesrc, 0, sizeof(XAUDIO2_SEND_DESCRIPTOR));

    _filterParsEx = FilterParamsEx::Empty();

    _initialized = true;
    _submixReady = false;

    // default output voice
    _destMasteringVoice = MasteringVoice::Voice3D;
  }

  return true;
}

bool XA2Submix::CreateSubmix()
{
  // no submix created yet
  if (!_submixReady)
  {
    if (!_sSystem || !_sSystem->XA2Device()) return false;

    if (!CreateSubmixVoice(_sSystem->XA2Device(), &_submix, SelectDestinationVoice(), _iChannelsCount, 
      _sSystem->_outFmt.nSamplesPerSec, XAUDIO2_VOICE_USEFILTER, 0, NULL))
    {
      _submixReady = false;
      return false;
    }

    // create send list - used for input samples for defining their output   
    _sendDesrc.Flags = 0;
    _sendDesrc.pOutputVoice = _submix;    
    _sendList.SendCount = 1;
    _sendList.pSends = &_sendDesrc;

    _parsTime = 0;

    // init done
    _submixReady = true;

    return true;
  }

  return false;
}

IXAudio2Voice* XA2Submix::SelectDestinationVoice() 
{
  return _sSystem ? _sSystem->DestinationVoice(_destMasteringVoice) : NULL;
}

void XA2Submix::Destruct()
{
  if (_submixReady)
  {
    _submix->DestroyVoice();
    _submix = 0;

    _submixReady = false;

    _channelAzimuth.Clear();
    _matrixCoefficients.Clear();

    _initialized = false;

    DoAssert(_count == 0);
  }
}

void XA2Submix::Include(AbstractWave *wave, bool noSet) 
{
  if (!_submixReady)
  {
    // submix not created
    if (!CreateSubmix()) return;
  }

  Assert(_submixReady);
  
  if (_submixReady)
  {
    WaveXA2 *wxa2 = static_cast<WaveXA2 *> (wave);

    if (wxa2)
    { 
      // set current wave output to current submix
      if (!noSet) wxa2->SetSendList(_sendList); 
      // increment wave count included in submix
      _count++; 
    }
  }
}

void XA2Submix::Exclude(AbstractWave *wave, bool noSet) 
{
  WaveXA2 *wxa2 = static_cast<WaveXA2 *> (wave);
  if(!wxa2) 
  { 
    if (noSet) _count--;  // wave already released, but some submixes can be released later (int ItemRadio::Simulate)
    return;
  }
  Assert(_submixReady || wxa2->_soundSys->_xa2Device.IsNull());

  if (_submixReady)
  {
    if (wxa2) 
    { 
      static const XAUDIO2_VOICE_SENDS emptySendList = { 0, NULL };
      if (!noSet) wxa2->SetSendList(emptySendList);
      _count--; 
    }
  }
}

float XA2Submix::Calculate3D(bool enable3D, const Vector3 &position, const Vector3 &speed, float distance, float vol) 
{
  PROFILE_SCOPE_EX(calGL, sound);

  // submix not ready - no wave
  if (!_submixReady) return 1;

  DWORD time = GlobalTickCount();
  if (!SoundParsNeedUpdate(time - _parsTime)) { return _dsp.DopplerFactor; }
  _parsTime = time;

  // no sources connected into submix, destroy submix voice
  if (_count == 0 && _submixReady) 
  {
    PROFILE_SCOPE_EX(calRe, sound);
    if (_submix) _submix->DestroyVoice();
    _submix = 0;
    _submixReady = false;

    // destroy send list
    _sendList.pSends = NULL;
    _sendList.SendCount = 0;

    // no doppler effect
    return 1;
  }

  // 2d mode
  if (!enable3D)
  {
    PROFILE_SCOPE_EX(cal2D, sound);
    if (_linCrossSet)
    {
      _lCross.Commit(_matrixCoefficients, 1.0f, 0.5f);
      _linCrossSet = false;
    }

    // continuous pass from 3d to 2d sound
    _lCross.Simulate(_matrixCoefficients);

    if (_filterParsEx._enable)
    {
      if (AFFactory.SetFilter(_filterParsEx, _dsp.EmitterToListenerDistance, _filterParsEx._forceUpdate))
      {
        _submix->SetFilterParameters(_filterParsEx.GetXA2ParsPtr());
      }
    }

    SetOutputMatrix(_submix, SelectDestinationVoice(), _iChannelsCount, 
      _sSystem->_outFmt.nChannels, _matrixCoefficients.Data());

    // no doppler effect
    return 1;
  }

  {
    PROFILE_SCOPE_EX(cal3D, sound);
    _linCrossSet = true;
    
    // update position and speed
    VEC3_2_X3DVEC(position, _emitter.Position);
    VEC3_2_X3DVEC(speed, _emitter.Velocity);

    _emitter.pCone = (_useCone) ? &_cone : NULL;
    _emitter.CurveDistanceScaler = floatMax(distance, 0.001f);
    _emitter.InnerRadius = distance * 0.05f;
    _emitter.pLFECurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_LFE_Curve;

    // calculates DSP settings with respect to 3D parameters
    X3DAudioCalculate(_sSystem->_x3dInstance, &_sSystem->_listener, &_emitter, _sSystem->_x3dCalculateFlags, &_dsp);

    if (_filterParsEx._enable)
    {
      if (AFFactory.SetFilter(_filterParsEx, _dsp.EmitterToListenerDistance, _filterParsEx._forceUpdate))
      {
        _submix->SetFilterParameters(_filterParsEx.GetXA2ParsPtr());
      }
    }
  }

  // update amplitude
  for(int i = 0; i < _matrixCoefficients.Size(); i++) { _matrixCoefficients[i] *= vol; }

  // submix is connected directly into mastering voice
  SetOutputMatrix(_submix, SelectDestinationVoice(), _iChannelsCount, 
    _sSystem->_outFmt.nChannels, _matrixCoefficients.Data());

  SaturateFrequency(_dsp.DopplerFactor);
  return _dsp.DopplerFactor;
}

FilterTypeEx XA2Submix::SetFilter(FilterTypeEx filter)
{
  FilterTypeEx fType = _filterParsEx._typeEx;

  if (filter != fType)
  {
    _filterParsEx._forceUpdate = _filterParsEx._typeEx != filter;
    _filterParsEx._enable = true;
    _filterParsEx._typeEx = filter;
  }

  return fType;
}

void XA2Submix::SetOutputVoiceSound(SubmixOutput sOut) 
{
  _destMasteringVoice = (sOut == CustomFilterOutput) ? MasteringVoice::CustomFilter : MasteringVoice::Voice3D;
}

void XA2Submix::EnableDirectionalSound(bool enable)
{
  if (enable)
  {
    _useCone = enable;
    _emitter.pCone = &_cone;
  }
  else
    _emitter.pCone = NULL;
}

void XA2Submix::SetDirSoundConePars(float innerAngle, float outerAngle, float innerVolume, float outerVolume)
{
  _cone.InnerAngle = floatMin(floatMax(innerAngle, 0.0f), X3DAUDIO_2PI);
  _cone.OuterAngle = floatMin(floatMax(innerAngle, outerAngle), X3DAUDIO_2PI);
  _cone.InnerVolume = floatMin(floatMax(innerVolume, 0), 2.0f);
  _cone.OuterVolume = floatMin(floatMax(outerVolume, 0), 2.0f);
  _cone.InnerLPF = 0.5f;
  _cone.OuterLPF = 1.0f;
  _cone.InnerReverb = 1.0f;
  _cone.OuterReverb = 2.0f;
}

//////////////////////////////////////////////////////////////////////////

bool SoundSystemXA2::LoadHeaderFromCache( WaveXA2 *wave )
{
  for (int i = 0; i < _cache.Size(); i++)
  {
    WaveCacheXA2 &cache = _cache[i];
    if (!strcmp(cache._name, wave->Name()))
    {
      wave->_header = cache._header;
      return true;
    }
  }
  return false;
}

bool SoundSystemXA2::LoadFromCache( WaveXA2 *wave )
{
  for (int i=0; i<_cache.Size(); i++)
  {
    WaveCacheXA2 &cache = _cache[i];
    if (!strcmp(cache._name, wave->Name()))
    {
      if (wave->CacheLoad(cache))
      {
        _cache.Delete(i);
        return true;
      }
    }
  }
  return false;
}

void SoundSystemXA2::LogChannels( const char *name )
{
}

void SoundSystemXA2::ReserveCache( int number3D, int number2D )
{
}

void SoundSystemXA2::StoreToCache( WaveXA2 *wave )
{
  if (wave->_stream)
  {
    Fail("Storing streaming buffer");
    return;
  }
  //Log("StoreToCache %s",(const char *)wave->Name());
#if DO_PERF
  ADD_COUNTER(wavSC,1);
#endif
  // note: it may make sense to have the same sound stored twice
  // if DuplicateBuffer does not work well, it may be much faster
  // this happened only when debugging in DX8 beta
  /**/
  for(int i = 0; i < _cache.Size();)
  {
    WaveCacheXA2 &cache=_cache[i];
    if( !strcmp(cache._name, wave->Name()) )
    {
#if DIAG_LOAD>=2
      LogF("WaveXA2 %s cache refresh",(const char *)_cache[i]._name);
#endif
      _cache.Delete(i);
      // move to cache beginning
      _cache.Insert(0);
      wave->CacheStore(_cache[0]);
      return;
    }
    else i++;
  }

  int size = _cache.Size();
  //int waves=_waves.Size();
  // free HW channels

  const int maxSize=128;
  while( size>=maxSize )
  {
    _cache.Delete(--size);
    //LogChannels("CacheSize");
  }

  int index=0;
  _cache.Insert(index);

  WaveCacheXA2 &cache=_cache[index];
  wave->CacheStore(cache);

#if DIAG_LOAD>=2
  LogF("WaveXA2 %s inserted into the cache",(const char *)wave->Name());
#endif
}

float SoundSystemXA2::GetWaveDuration( const char *filename )
{
  // load file header - we need to wait
  Ref<WaveStream> stream;
  DoAssert(RString::IsLowCase(filename));
  // waiting for a sound
  SoundRequestFile(filename,stream,true);

  // only header is needed - whole files are cached
  if (!stream)
  {
    RptF("Cannot find a sound file %s", filename);
    return 1;
  }

  WAVEFORMATEX format;
  stream->GetFormat(format);
  int frequency = format.nSamplesPerSec;      // sampling rate
  int sSize = format.nBlockAlign;           // number of bytes per sample
  int size = stream->GetUncompressedSize();

  if (frequency==0 || sSize==0)
  {
    RptF("Bad header in file %s",cc_cast(filename));
    return 1;
  }

  return float(size/sSize)/frequency;

}

/// structure used to port parameters from Xbox to PC
struct DSI3DL2LISTENER
{
  LONG            lRoom;                  // [-10000, 0] default: -10000 mB
  LONG            lRoomHF;                // [-10000, 0] default: 0 mB
  FLOAT           flRoomRolloffFactor;    // [0.0, 10.0] default: 0.0
  FLOAT           flDecayTime;            // [0.1, 20.0] default: 1.0 s
  FLOAT           flDecayHFRatio;         // [0.1, 2.0] default: 0.5
  LONG            lReflections;           // [-10000, 1000] default: -10000 mB
  FLOAT           flReflectionsDelay;     // [0.0, 0.3] default: 0.02 s
  LONG            lReverb;                // [-10000, 2000] default: -10000 mB
  FLOAT           flReverbDelay;          // [0.0, 0.1] default: 0.04 s
  FLOAT           flDiffusion;            // [0.0, 100.0] default: 100.0 %
  FLOAT           flDensity;              // [0.0, 100.0] default: 100.0 %
  FLOAT           flHFReference;          // [20.0, 20000.0] default: 5000.0 Hz
};


/*
static void ConvertI3DL2ToEax(
unsigned long env, float envSize, EAXREVERBPROPERTIES &eax, const DSI3DL2LISTENER &i3d
)
{

eax.ulEnvironment = env;
eax.flEnvironmentSize = envSize;
eax.flEnvironmentDiffusion = 1.0;
eax.lRoom = i3d.lRoom;
eax.lRoomHF = i3d.lRoomHF;
eax.lRoomLF = 0;
eax.flDecayTime = i3d.flDecayTime;
eax.flDecayHFRatio = i3d.flDecayHFRatio;
eax.flDecayLFRatio = 1.0f;
eax.lReflections = i3d.lReflections;
eax.flReflectionsDelay = i3d.flReflectionsDelay;
EAXVECTOR zero={0,0,0};
eax.vReflectionsPan = zero;
eax.lReverb = i3d.lReverb;
eax.flReverbDelay = i3d.flReverbDelay;
eax.vReverbPan = zero;
eax.flEchoTime = 0.25;
eax.flEchoDepth = 0;
eax.flModulationTime = 0.25;
eax.flModulationDepth = 0;

eax.flAirAbsorptionHF = -5;
eax.flHFReference = i3d.flHFReference;
eax.flLFReference = 250.0f; // Hz
eax.flRoomRolloffFactor = i3d.flRoomRolloffFactor;
// TODO: use flags to use the room size
eax.ulFlags = 0;
}
*/

// avoid small room roll-off, as such sounds could be audible at extreme distances
static DSI3DL2LISTENER EnvPlain    ={-1500,-1500,0.7f,1,0.2,-850,0.06,-900,0.1,16,100,5000};
static DSI3DL2LISTENER EnvForest   ={-3000,-1000,0.7f,2.5,0.1 ,0,0.1,-900,0.5,80,100,5000};
static DSI3DL2LISTENER EnvCityOpen ={-2500,-800 ,0.4f,5  ,0.2 ,-1000,0.04,-1000,0.1,20,100,5000};
static DSI3DL2LISTENER EnvMountains={-2000,-1500,0.4f ,10 ,0.2 ,-1500,0.1,-1000,0.1,21,100,5000};
static DSI3DL2LISTENER NoReverb    ={-10000, -10000, 1.0f, 1.00f, 1.00f, -10000, 0.000f, -10000, 0.000f,   0.0f,   0.0f, 5000.0f};

//////////////////////////////////////////////////////////////////////////

#if _ENABLE_CHEATS
// real-time tuning  via scripting
#include <El/evaluator/expressImpl.hpp>

static GameValue DebugLFE(const GameState *state, GameValuePar oper)
{
  const GameArrayType &array = oper;

  if (array.Size() == 10)
  {
    int index = 0;

    for (unsigned int i = 0; i < Emitter_LFE_Curve.PointCount; i++)
    {
      Emitter_LFE_Curve.pPoints[i].Distance = array[index++];
      Emitter_LFE_Curve.pPoints[i].DSPSetting = array[index++];
    }
  }

  return GameValue();
}

#include <El/Modules/modules.hpp>

static const GameFunction DbgUnary[]=
{
  GameFunction(GameNothing,"diag_setLFE",DebugLFE,GameArray TODO_FUNCTION_DOCUMENTATION)
};

INIT_MODULE(GameStateDbg, 3)
{
  GGameState.NewFunctions(DbgUnary,lenof(DbgUnary));
};

#endif

//////////////////////////////////////////////////////////////////////////

void SoundSystemXA2::DoSetEAXEnvironment()
{
}

void SoundSystemXA2::DoSetEAXEnvironment(XAUDIO2FX_REVERB_I3DL2_PARAMETERS &pars)
{
}

void SoundSystemXA2::DoSetEAXEnvironment(XAUDIO2FX_REVERB_PARAMETERS &pars)
{
  if (!_xa2Device || !_reverbEnable) return;

#if ENABLE_REVERB

  if (_context3D)
    _context3D->SetEffectParameters(0, &pars, sizeof(XAUDIO2FX_REVERB_PARAMETERS));

#endif
}

void SoundSystemXA2::SetEnvironment(const SoundEnvironment &env)
{
  if ((env.type == _environment.type) && (fabs(env.size - _environment.size) < 1) && (fabs(env.density - _environment.density) < 0.05))
  {
    return; // no change
  }
  _environment = env;

  DoSetEAXEnvironment();
}

/*!
\patch 1.34 Date 12/10/2001 by Ondra
- Fixed: MP: Dedicated server memory leak.
(Caused by bug in sound management with sounds disabled).
*/

void SoundSystemXA2::AddWaveToList(WaveGlobalXA2 *wave)
{
  for (int i = 0; i < _waves.Size(); i++)
  {
    if (!_waves[i])
    {
      _waves[i] = wave;
      return;
    }
  }
  _waves.Add(wave);
}

AbstractWave *SoundSystemXA2::CreateWave( const char *filename, bool is3D, bool prealloc )
{
  PROFILE_SCOPE_EX(ssCrW,sound);
#if 0
  static char buf[256] = "reload-m16";
  if (strstr(filename, buf)!=NULL)
    _asm nop;
#endif
  // TODO: use prealloc
  if( !_soundReady ) return NULL;
  // if the wave is preloaded, simply duplicate it
  char lowName[256];
  strcpy(lowName,filename);
  strlwr(lowName);
  if (!*lowName) return NULL; // empty filename

  WaveGlobalXA2 *wave = new WaveGlobalXA2(this,lowName,is3D);
  //LogF("Created %s: %x",lowName,wave);

  AddWaveToList(wave);
  return wave;
}

AbstractWave *SoundSystemXA2::CreateEmptyWave( float duration )
{
  // create a WaveXA2 with no buffers, only delay
  if( !_soundReady ) return NULL;
  WaveGlobalXA2 *wave=new WaveGlobalXA2(this,duration);
  //LogF("Created empty %x",wave);
  AddWaveToList(wave);
  return wave;
}

AbstractDecoderWave *SoundSystemXA2::CreateDecoderWave()
{
  if( !_soundReady ) return NULL;
  WaveDecoderXA2 *decoder = new WaveDecoderXA2(this);

  return decoder;
}

AbstractSubmix *SoundSystemXA2::CreateSubmix()
{
  XA2Submix *submix = new XA2Submix(this);
  return submix;
}

void SoundSystemXA2::Delete()
{
  if( !_soundReady ) return;

  _previewEffect.Free();
  _previewMusic.Free();
  _previewSpeech.Free();

#if DIAG_LOAD
  while( _cache.Size()>0 )
  {
    const WaveCacheXA2 &wc = _cache[0];
    LogF("WaveXA2 %s deleted from cache",(const char *)wc._name);
    _cache.Delete(0);
    //LogChannels("CacheSize");
  }
#endif

  if (_wCapture.DeviceOpened()) _wCapture.CloseDevice();
  _cache.Clear(); 
  _waves.Clear();

  DestroyDevice();
  _soundReady = false;
}

void SoundSystemXA2::SetListener(Vector3Par pos, Vector3Par vel, Vector3Par dir, Vector3Par up)
{
  PROFILE_SCOPE_EX(snLis,sound);

  VEC3_2_X3DVEC(pos, _listener.Position);

  Vector3 vecTmp = dir.Normalized();
  VEC3_2_X3DVEC(vecTmp, _listener.OrientFront);

  vecTmp = up.Normalized();
  VEC3_2_X3DVEC(vecTmp, _listener.OrientTop);

  VEC3_2_X3DVEC(vel, _listener.Velocity);

  _listener.pCone = 0;

  //if (dir.SquareSize() < 0.1f) _listener.OrientFront = VForward;
  //if (up.SquareSize() < 0.1f) _listener.OrientTop = VUp;
  if (!_soundReady) return;
  if (!_xa2Device) return;

  DWORD time = GlobalTickCount();
  // max 20 fps listener position update
  const DWORD minTime = 50;

  if (_listenerTime == 0 || time - _listenerTime > minTime)
  {
#if DEFERRED
    SND_COUNTER(wavLD,1);
#else
    SND_COUNTER(wavLI,1);
#endif    
    _listenerTime = time;
  }
}

static float FloatToDb(float x)
{
  if (x<=1e-5) return -100;
  float db = 20*log10(x);
  return db;
}

static bool PreviewAdvance(AbstractWave *wave, float deltaT)
{
  if (!wave) return false;
  if (!wave->IsMuted())
  {
    wave->Play();
    //LogF("Preview %s play",(const char *)wave->Name());
  }
  else
  {
    wave->Stop();
    wave->Advance(deltaT);
    //LogF("Preview %s muted",(const char *)wave->Name());
  }
  return wave->IsTerminated();
}

/*!
\patch 1.01 Date 15/6/2001 by Ondra.
- Fixed double music volume in Audio options.
*/

static int CompareVals(const int16 *a, const int16 *b)
{
  float diff = *a - *b;
  if( diff>0 ) return -1;
  if( diff<0 ) return +1;
  return 0;
}

void SoundSystemXA2::ProcessCapturedData()
{
  Assert(_wCapture.DeviceOpened());

  if (_wCapture.DeviceOpened() && _captureTime > Glob.uiTime)
  {
    if (_wCapture.CapturedSamples() > 0)
    {
      AutoArray<int16, MemAllocLocal<int16, 1024> > buffer;
      buffer.Realloc(_wCapture.CapturedBuffSize());
      buffer.Resize(_wCapture.CapturedBuffSize());

      // 4 capturing buffers are used
      for (int i = 0; i < 4; i++)
      {
        int samplesCaptured = _wCapture.GetBuffer(buffer.Data(), buffer.Size());

        if (samplesCaptured == 0) break;

        // do something with captured samples
        int16 sMax = buffer[0];
        for (int i = 1; i < samplesCaptured; i++)
        {
          if (abs(buffer[i]) > sMax) sMax = abs(buffer[i]);
        }

        _cVals.Append(sMax);
      }
    }
  }
  else
  {
    StopCapture();
    if (_cVals.Size() > 0)
    {
      QSort(_cVals.Data(), _cVals.Size(), CompareVals);
      _vonRecThrAutoWanted = _cVals[_cVals.Size() / 2] / 32768.0f;

      _cVals.Clear();
    }
  }
}


void SoundSystemXA2::SetCustomVoiceFilter(bool set)
{
  if (_customSubmixFilter == set)
    return;

  _customFilterPars.Frequency = FilterParamsEx::Empty().Frequency;
  _customFilterPars.OneOverQ = FilterParamsEx::Empty().OneOverQ;
  _customFilterPars.Type = FilterParamsEx::Empty().Type;

  if (set) {
    FilterParamsEx fParsEx;
    VehOpenDoorsBandPassFilter vFilter;
    vFilter(fParsEx, 0.0f, true);

    _customFilterPars.Frequency = fParsEx.Frequency;
    _customFilterPars.OneOverQ = fParsEx.OneOverQ;
    _customFilterPars.Type = fParsEx.Type;
  }

  if (_masteringVoice._submixCustom) {
    _masteringVoice._submixCustom->SetFilterParameters(&_customFilterPars);
  }

  _customSubmixFilter = set;
}

void SoundSystemXA2::Commit()
{
  if( !_soundReady ) return;

  // active only via video options: adjust microphone sensitivity
  if (_captureRunning)
  {
    ProcessCapturedData();
  }

  PROFILE_SCOPE_EX(snCmt,sound);
#if _ENABLE_CHEATS 
  if (CHECK_DIAG(DESound))
  {
    /*
    DIAG_MESSAGE(100,Format(
    "Alloc: HW 3D %d, HW 2D %d, SW %d, Free %d, Cache %d",
    WaveXA2::_counters[WaveXA2::Hw3D],WaveXA2::_counters[WaveXA2::Hw2D],
    WaveXA2::_counters[WaveXA2::Sw],WaveXA2::_counters[WaveXA2::Free],
    _cache.Size()
    ));
    DIAG_MESSAGE(100,Format(
    "Play:  HW 3D %d, HW 2D %d, SW %d, Free %d",
    WaveXA2::_countPlaying[WaveXA2::Hw3D],WaveXA2::_countPlaying[WaveXA2::Hw2D],
    WaveXA2::_countPlaying[WaveXA2::Sw],WaveXA2::_countPlaying[WaveXA2::Free]
    ));
    */
  }
#endif

#if 0
  XAUDIO2_PERFORMANCE_DATA performaceData = {0};
  
  _xa2Device->GetPerformanceData(&performaceData);

  LogF("Samples: %d", _waves.Size());
  LogF("SourceVoice: %d, submix voices: %d", performaceData.ActiveSourceVoiceCount, performaceData.ActiveSubmixVoiceCount);

  XAUDIO2_VOICE_DETAILS details;
  _masteringVoice._master->GetVoiceDetails(&details);
#endif

  _commited = true;
  if (_active)
  {
    for( int i=0; i<_waves.Size(); i++ )
    {
      WaveXA2 *wave = _waves[i];
      if (!wave) continue;

      if (wave->_wantPlaying)
      {
        if (wave->DoPlay())
        {
          wave->_wantPlaying = false;
        }
      }
    }
  }

#if _ENABLE_CHEATS 
  if( GInput.GetCheat2ToDo(DIK_W) )
  {
    // scan all playing waves
    LogF("Actual sound buffers:");
    int nPlay = 0;
    int nStop = 0;
    int nTerm = 0;
    for( int i=0; i<_waves.Size(); i++ )
    {
      WaveXA2 *wave=_waves[i];
      if( !wave ) continue;
      if (wave->_playing)
      {
        nPlay++;
        if (wave->_terminated) nTerm++;
      }
      else nStop++;
      const char *name = wave->Name();
      if (wave->Name().GetLength()<=0) name = "Pause";
      LogF("  %x:%s %s",wave,name,(const char *)wave->GetDiagText());
    }
    LogF("  channels stat %d",STAT_GETCHANNELS());
    LogF("  playing %d, terminated %d",nPlay,nTerm);
    LogF("  stopped %d",nStop);
    LogF("  listener");

    LogF
      (
      "    pos (%.1f,%.1f,%.1f)\n"
      "    dir (%.1f,%.1f,%.1f)\n"
      "    vel (%.1f,%.1f,%.1f)",
      _listener.Position.x,_listener.Position.y,_listener.Position.z,
      _listener.OrientFront.x,_listener.OrientFront.y,_listener.OrientFront.z,
      _listener.Velocity.x,_listener.Velocity.y,_listener.Velocity.z
      );
  }
#endif

  // there may be some preview active
  if (_doPreview && GlobalTickCount() >= _previewTime)
  {
    float deltaT = 0.1;
    if (_prevSpSampleExist && !_previewSpeech)
    {
      //float volume=GetSpeechVolCoef();
      // start first sound
      ParamEntryVal cfg = Pars >> "CfgSFX" >> "Preview" >> "speech"; 
      RStringB name = cfg[0];
      AbstractWave *wave = CreateWave(name, false, true);
      _previewSpeech = wave;
      if(wave)
      {
        wave->SetKind(WaveSpeech);
        wave->EnableAccomodation(false);
        wave->SetSticky(true);
        wave->SetVolume(cfg[1],cfg[2],true);
        wave->Repeat(1);
        wave->Play();
      }
      else
      {
        _prevSpSampleExist = false; // error - no preview possible
      }
    }
    else if(_prevEfSampleExist && PreviewAdvance(_previewSpeech,deltaT) && !_previewEffect)
    {
      // start second sound
      ParamEntryVal cfg = Pars >> "CfgSFX" >> "Preview" >> "effect"; 
      RStringB name = cfg[0];
      AbstractWave *wave = CreateWave(name, true, false);
      _previewEffect = wave;
      if(_previewEffect)
      {
        wave->EnableAccomodation(false);
        wave->SetSticky(true);
        wave->SetVolume(cfg[1],cfg[2],true);
        wave->SetKind(WaveEffect);

        D3DVECTOR listPosX3;
        listPosX3.x = _listener.Position.x + 100.0f * _listener.OrientFront.x;
        listPosX3.y = _listener.Position.y + 100.0f * _listener.OrientFront.y;
        listPosX3.z = _listener.Position.z + 100.0f * _listener.OrientFront.z;

        Vector3 listPos = X3DVEC_2_VEC3(listPosX3);

        wave->SetPosition(listPos, VZero, false);        
        wave->SetMaxDistance(1000.0f);
        wave->Play();
        wave->Repeat(1);
        wave->SetPosition(listPos, VZero, true);
      }
      else
      {
        _prevEfSampleExist = false;
      }
    }
    else if (_prevMuSampleExist && PreviewAdvance(_previewEffect,deltaT) && !_previewMusic)
    {
      ParamEntryVal cfg = Pars >> "CfgSFX" >> "Preview" >> "music"; 
      RStringB name = cfg[0];
      AbstractWave *wave = CreateWave(name,false,true);
      _previewMusic=wave;
      if(wave)
      {
        wave->SetKind(WaveMusic);
        wave->EnableAccomodation(false);
        wave->SetSticky(true);
        // FIX
        // default volume for music is 0.5
        float vol = cfg[1];
        wave->SetVolume(vol*0.5,cfg[2],true);
        wave->Repeat(1);
        wave->Play();
      }
      else
      {
        _prevMuSampleExist =  false;
      }
    }
    else if (!_prevMuSampleExist || PreviewAdvance(_previewMusic,deltaT))
    {
      _doPreview = false;
      _previewSpeech.Free();
      _previewEffect.Free();
      _previewMusic.Free();
    }
  }
}

float SoundSystemXA2::Priority() const
{
  return 0.4;
}
RString SoundSystemXA2::GetDebugName() const
{
  return "Sounds";
}
size_t SoundSystemXA2::MemoryControlled() const
{
  size_t total = 0;
  for (int i=0; i<_cache.Size(); i++)
  {
    const WaveCacheXA2 &item = _cache[i];
    total += item._header._size;
  }
  return total;
}

size_t SoundSystemXA2::FreeOneItem()
{
  size_t released = 0;
  int size=_cache.Size();
  if (size==0) return released;
  WaveCacheXA2 &item = _cache[size-1];
  released = item._header._size;

  _cache.Delete(size-1);
  return released;
}

static inline float expVol( float vol )
{
  if (vol<=0) return 1e-10; // muted -> -200 db
  // vol in range 0..10
  //return exp(vol*0.5-5);
  //return exp((vol*2-20)*0.5);
  // max. volume is 0
  return exp((vol-10.0f)*0.21f);
  //return exp((vol-10.0f)*0.7f);
}

void SoundSystemXA2::PreviewMixer()
{
  if (!_enablePreview) return;
  
  //if (_doPreview) TerminatePreview();
  
  _doPreview=true;
  
  _prevEfSampleExist = true;
  _prevMuSampleExist = true;
  _prevSpSampleExist = true;

  _previewTime = GlobalTickCount()+10;
}

void SoundSystemXA2::StartPreview()
{
  if (_previewMusic)
  {
    TerminatePreview();
  }
  PreviewMixer();
}

void SoundSystemXA2::TerminatePreview()
{
  _previewEffect.Free();
  _previewMusic.Free();
  _previewSpeech.Free();
  _doPreview = false;
}

void SoundSystemXA2::FlushBank(QFBank *bank)
{
  // release any cached waves from this bank
  if (bank)
  {
    for (int i=0; i<_cache.Size(); i++)
    {
      if (!bank->Contains(_cache[i]._name)) continue;
      _cache.Delete(i);
      i--;
    }
  }
  _waves.RemoveNulls();
}

#define _LINEAR_RUN_ 1

/*!
\patch 5164 Date 6/4/2007 by Ondra
- Fixed: System audio mixer settings no longer changed by ingame volume options.
*/
void SoundSystemXA2::UpdateMixer()
{
  // volume slide bar has range [0, 10]
  // expVol(10) return value: 1 
  // expVol(6.71059272) return value: 0.01
  // _volumeAdjustEffect = 10 -> volume * 10, where 10 is max
  
  const float VolMult   = 1.5f;
  const float RadioMult = 1.5f;
  const float MusiCmult = 1.5f;
  const float VoNMult   = 1.5f;

#if _LINEAR_RUN_

  float waveVolLin    = _waveVolWanted;
  float speechVolLin  = _speechVolWanted;
  float musicVolLin   = _musicVolWanted;
  float vonVolLin     = _vonVolWanted;

  _volumeAdjustEffect = VolMult   * (waveVolLin*0.1f);
  _volumeAdjustSpeech = RadioMult * (speechVolLin*0.1f);
  _volumeAdjustMusic  = MusiCmult * (musicVolLin*0.1f);
  _volumeAdjustVoN    = VoNMult   * (vonVolLin*0.1f);

#else

  float waveVolExp = expVol(_waveVolWanted);
  float speechVolExp = expVol(_speechVolWanted);
  float musicVolExp = expVol(_musicVolWanted);

  float maxVol = 10;
  float maxVolExp = expVol(maxVol);

  _volumeAdjustEffect = VolMult * (waveVolExp/maxVolExp);
  _volumeAdjustSpeech = RadioMult * (speechVolExp/maxVolExp);
  _volumeAdjustMusic = MusiCmult * (musicVolExp/maxVolExp);

#endif



  //LogF("Update _volumeAdjust %g",_volumeAdjust);

  for (int i=0; i<_waves.Size(); i++)
  {
    WaveXA2 *wave = _waves[i];
    if (wave) wave->SetVolumeAdjust(_volumeAdjustEffect,_volumeAdjustSpeech,_volumeAdjustMusic);
  }

  //LogF("Update %g,%g,%g, maxVol %g",_waveVolExp,_speechVolExp,_musicVolExp,maxVol);
}

float SoundSystemXA2::GetSpeechVolume()
{
  return _speechVolWanted;
}
void SoundSystemXA2::SetSpeechVolume(float val)
{
  _speechVolWanted=val;
  UpdateMixer();
}
void SoundSystemXA2::SetVonVolume(float val)
{
  _vonVolWanted=val;
  UpdateMixer();
}

void SoundSystemXA2::StartCapture(float time)
{
  if (!_wCapture.DeviceOpened())
  {
    /* Common values are 8.0 kHz, 11.025 kHz, 22.05 kHz, and 44.1 kHz. */
    WAVEFORMATEX wFmt = { 0 };
    wFmt.wFormatTag = WAVE_FORMAT_PCM;
    wFmt.nChannels = 1;
    wFmt.wBitsPerSample = 16;
    wFmt.nBlockAlign = wFmt.wBitsPerSample / (8 * wFmt.nChannels);
    wFmt.nSamplesPerSec = 11025;
    wFmt.nAvgBytesPerSec = wFmt.nSamplesPerSec * wFmt.nBlockAlign;

    bool openResult = _wCapture.OpenDevice(wFmt, wFmt.nSamplesPerSec / 4);

    if (!openResult)
      RptF("Recording audio device initialization failed.");

    _cVals.Realloc(toInt(time) * 4 * (wFmt.nSamplesPerSec / _wCapture.CapturedBuffSize()));
  }

  _wCapture.Start();
  _captureRunning = true;
  _captureTime = Glob.uiTime + time;
  _vonRecThrAutoWanted = _vonRecThrWanted;
}

void SoundSystemXA2::StopCapture() 
{
  _wCapture.Stop();
  _captureRunning = false;
}

void SoundSystemXA2::SetVoNRecThreshold(float val)
{
  _vonRecThrWanted = _vonRecThrAutoWanted = val;
  GNetworkManager.SetVoNRecThreshold(val);
}

float SoundSystemXA2::GetWaveVolume()
{
  return _waveVolWanted;
}
void SoundSystemXA2::SetWaveVolume(float val)
{
  _waveVolWanted=val;
  UpdateMixer();
}

float SoundSystemXA2::GetCDVolume() const
{
  return _musicVolWanted;
}
void SoundSystemXA2::SetCDVolume(float val)
{
  _musicVolWanted=val;
  UpdateMixer();
}


void SoundSystemXA2::Activate(bool active)
{
  if (_active == active) return;
  _active = active;
  if (!active)
  {
    // when not active, we need to stop all buffer which are currently playing
    _waves.RemoveNulls();
    for (int i=0; i<_waves.Size(); i++)
    {
      WaveXA2 *wave = _waves[i];
      if (!wave->_playing) continue;
      wave->Stop();
    }
  }
  else
  {
    // when active, there is nothing to do - all sounds call Play on regular basis
  }
}

WaveDecoderXA2::WaveDecoderXA2(SoundSystemXA2 *sSys)
: _sSys(sSys), _source(NULL), _isPlaying(false), _curr(0), _free(0), _wantPlay(false)
{

}

WaveDecoderXA2::~WaveDecoderXA2()
{
  DestroySourceVoice();
}

bool WaveDecoderXA2::InitSourceVoice(int channels, int rate)
{
  if (_sSys->_xa2Device && !_source)
  {
    //Set the wave format
    WAVEFORMATEX wfm;
    memset(&wfm, 0, sizeof(wfm));

    wfm.cbSize          = sizeof(wfm);
    wfm.nChannels       = channels;
    wfm.wBitsPerSample  = 16;                                       //Ogg vorbis is always 16 bit
    wfm.nSamplesPerSec  = rate;
    wfm.nAvgBytesPerSec = wfm.nSamplesPerSec * wfm.nChannels * 2;
    wfm.nBlockAlign     = 2 * wfm.nChannels;
    wfm.wFormatTag      = WAVE_FORMAT_PCM;

    _wantPlay = false;
    _isPlaying = false;
    _curr = 0;

    if (_sSys->_xa2Device->CreateSourceVoice(&_source, &wfm, 0, XAUDIO2_DEFAULT_FREQ_RATIO, this) == S_OK)
{
      _free = BufferCount;

    return  true;
  }

    return false;
}

  return false;
}

void WaveDecoderXA2::DestroySourceVoice()
{
  if (_source)
  {
    if (_isPlaying)
    {
      Stop();
      _source->FlushSourceBuffers();
  }

    _source->DestroyVoice();
    _source = NULL;
  }

  for (int i = 0; i < BufferCount; i++)
  {
    _buffers[i].Clear();
  }

  _isPlaying = false;
}
  
bool WaveDecoderXA2::FillBuffer(const void *data, size_t nbytes)
{
  if (!_source) return  false;
  if (!_isPlaying && !_wantPlay) return false;

  if (_free == 0) return false;
  _free--;
  
  PROFILE_SCOPE_EX(fbOgg, ogg);

  XA2Buffer &buffer = _buffers[_curr];
  buffer.Realloc(nbytes);
  buffer.Resize(nbytes);

  memcpy((BYTE*)buffer.Data(), data, nbytes);

  XAUDIO2_BUFFER xa2buffer = { 0 };
  xa2buffer.Flags = 0;
  xa2buffer.pAudioData = (BYTE*)buffer.Data();
  xa2buffer.AudioBytes = buffer.Size();

  _source->SubmitSourceBuffer(&xa2buffer);  
  if (_wantPlay)
  {
    _source->Start(0, XAUDIO2_COMMIT_NOW);
    _wantPlay = false;
    _isPlaying = true;
  }

  _curr++;
  _curr %= BufferCount;

  return true;
}

__int64 WaveDecoderXA2::GetVoicePosition() const
{
  if (!_source) return 0;

  XAUDIO2_VOICE_STATE state;
  _source->GetState(&state);

  return state.SamplesPlayed;
    }

bool WaveDecoderXA2::IsBuffersStopped() const
{
  if (_source)
  {
    XAUDIO2_VOICE_STATE state;
    _source->GetState(&state);

    return (state.BuffersQueued == 0);
  }

  return true;
}

void WaveDecoderXA2::Stop()
{
  if (_source && _isPlaying)
  {
    _source->Stop(0, XAUDIO2_COMMIT_NOW);
    _isPlaying = false;
    _wantPlay = false;
    }
}

void WaveDecoderXA2::Play()
    {
  if (_source)
  {
    XAUDIO2_VOICE_STATE state;
    _source->GetState(&state);

    if (state.BuffersQueued != 0) _source->Start(0, XAUDIO2_COMMIT_NOW);
    _wantPlay = true;
    _isPlaying = true;
  }
}

void WaveDecoderXA2::Pause(bool pause)
{
  if (!_source) return;

  if (pause)
{
    Stop();
  }
  else
  {
    _wantPlay = true;
  }
}

#include <El/Credits/credits.hpp>
#ifndef _CONSOLE
#include <libcurl/include/curl/curl.h>
#endif

class InternetRadioSystem2 : public InternetRadioSystem
{
  // lock guarding the _radioStreams array access
  PoCriticalSection lock; 
  inline void enter() const { lock.enter(); }
  inline void leave() const { lock.leave(); }

  AutoArray<RadioStream2 *> _radioStreams;
  ThreadId threadId;
  char _errorBuffer[CURL_ERROR_SIZE];
  int _runningHandles;  // should be equal to _radioStreams.Size() most of time

public:
  bool TerminateInternetRadioPollThread;
  CURLM *multi_handle;
  
  InternetRadioSystem2();
  ~InternetRadioSystem2();

  // include/exclude source waves into/from itemRadio submixes (set sendLists)
  void Simulate(bool simEnabled);
  // curl_multi_perform inside critical section
  CURLMcode CurlPerform()
  {
    CURLMcode curlCode = CURLM_OK;
    enter();
    curlCode = curl_multi_perform(multi_handle, &_runningHandles);
    leave();
    return curlCode;
  }

  RadioStream2* AddRadio(RString url);
  // mark the valid=false and remove from _radioStreams
  void Invalidate(CURL *easy_handle);
  // remove from _radioStreams
  void Remove(RadioStream2 *stream);
  // 
  bool RequestUrl(const char *url, Ref<WaveStream> &stream);
  int RunningHandles() const { return _runningHandles; }
  void Terminate() { TerminateInternetRadioPollThread=true; }
};

Ref<InternetRadioSystem2> GRS2;

void CreateInternetRadioSystem()
{
  GRS2 = new InternetRadioSystem2;
}

THREAD_PROC_RETURN THREAD_PROC_MODE InternetRadioPollThread ( void *param )
{
  if (!GRS2) return 0;
  CURLMcode curlCode = CURLM_OK;
  while (!GRS2->TerminateInternetRadioPollThread)
  {
    int ready = 1; //in order to call curl_multi_perform when CURLM_CALL_MULTI_PERFORM
    if (curlCode!=CURLM_CALL_MULTI_PERFORM) curl_multi_wait(GRS2->multi_handle, NULL, 0, 100, &ready); // perform each 100ms or when some transfer is ready
    curlCode = GRS2->CurlPerform(); // curl_multi_perform inside critical section

    if ( !GRS2->RunningHandles() )
    {
      Sleep(100); //we need to wait, as If no extra file descriptors are provided in curl_multi_wait and libcurl has no file descriptor to offer to wait for, this function will return immediately
      continue;
    }
    // check possible errors (or connections done)
    int msgsInQueue;
    CURLMsg *msg = curl_multi_info_read(GRS2->multi_handle, &msgsInQueue);
    while (msg)
    {
      switch (msg->msg)
      {
      case CURLMSG_NONE:
        LogF("CURLMSG_NONE");
        break;
      case CURLMSG_LAST:
        LogF("CURLMSG_LAST");
        break;
      case CURLMSG_DONE:
        GRS2->Invalidate(msg->easy_handle);
        break;
      }
      msg = curl_multi_info_read(GRS2->multi_handle, &msgsInQueue);
    }
  }
  return 0;
}

InternetRadioSystem2::InternetRadioSystem2() 
  : TerminateInternetRadioPollThread(false)
{
  GIRadioSystem = this;
  multi_handle = curl_multi_init(); 
  poThreadCreate(&threadId,32*1024,&InternetRadioPollThread,NULL);
}

InternetRadioSystem2::~InternetRadioSystem2()
{
  TerminateInternetRadioPollThread = true;
  for (int i=0; i<_radioStreams.Size(); i++)
  {
    curl_multi_remove_handle(multi_handle, (CURL *)_radioStreams[i]->GetHandle());
    curl_easy_cleanup((CURL *)_radioStreams[i]->GetHandle());
  }
  curl_multi_cleanup(multi_handle);
  // wait until thread finished
  if ( WaitForSingleObject(threadId,1000) != WAIT_OBJECT_0 )
    _asm nop;
}

bool InternetRadioSystem2::RequestUrl(const char *url, Ref<WaveStream> &stream)
{
  enter();
  for (int i=0; i<_radioStreams.Size(); i++)
  {
    if ( !strcmp(url, _radioStreams[i]->Name()) )
    {
      if (!_radioStreams[i]->Ready())
      {
        leave();
        return false;
      }
      CreateWaveRadioStream(_radioStreams[i], stream);
      leave();
      return true;
    }
  }
  leave();
  return false;
}

void InternetRadioSystem2::Invalidate(CURL *easy_handle)
{
  enter();
  for (int i=0; i<_radioStreams.Size(); i++)
  {
    if (_radioStreams[i]->GetHandle()==easy_handle)
    {
      _radioStreams[i]->valid = false;
      _radioStreams.Delete(i,1); // destructor is to be called from ItemRadio simulation (main thread)
      break;
    }
  }
  leave();
}

void InternetRadioSystem2::Remove(RadioStream2 *stream)
{
  enter();
  for (int i=0; i<_radioStreams.Size(); i++)
  {
    if (_radioStreams[i]==stream)
    {
      _radioStreams.Delete(i,1);
      curl_multi_remove_handle(multi_handle, (CURL *)stream->GetHandle());
      curl_easy_cleanup((CURL *)stream->GetHandle());
      break;
    }
  }
  leave();
}

static size_t write_radio_callback(void *buffer, size_t size, size_t nmemb, void *data)
{
  OggStreamBuffer *stream = reinterpret_cast<OggStreamBuffer *>(data);
  if (stream)
  {
    return stream->Write(buffer, size*nmemb); // If that amount differs from the amount passed to your function, it'll signal an error to the library. This will abort the transfer and return CURLE_WRITE_ERROR.
  }
  return 0;
}

RadioStream2 *InternetRadioSystem2::AddRadio(RString url)
{
#ifndef _CONSOLE
  enter();
  for (int i=0; i<_radioStreams.Size(); i++)
  {
    if ( !strcmp(url, _radioStreams[i]->Name()) )
    {
      RadioStream2 *ret = _radioStreams[i];  // after leave() the i index content can be invalidated
      leave();
      return ret;
    }
  }

  RadioStream2 *oggStream = NULL;
  CURL *curl = curl_easy_init();

  curl_easy_setopt(curl,CURLOPT_ERRORBUFFER,_errorBuffer);
#if _DEBUG
  curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); 
#endif
  curl_easy_setopt(curl, CURLOPT_URL, cc_cast(url));

  //curl_easy_setopt(curl,  CURLOPT_TIMEOUT,20);
  //curl_easy_setopt(curl,  CURLOPT_CONNECTTIMEOUT,20);
  /* no need to use CURLOPT_SSL_VERIFYPEER, CURLOPT_SSL_VERIFYHOST - defaults are good */

  oggStream = new RadioStream2(curl, url);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, oggStream);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_radio_callback);

  _radioStreams.Add(oggStream);
  curl_multi_add_handle(multi_handle, curl);
  leave();
  return oggStream;
#else
  // any failure: return a NULL
  RptF("Disabled for Binarize");
  return NULL;
#endif
}

void InternetRadioSystem2::Simulate(bool simEnabled)
{ // Main Thread only
  enter();
  for (int i=0; i<_radioStreams.Size(); i++)
  {
    RadioStream2 *radioStream = _radioStreams[i];
    if (!radioStream->CheckValid(simEnabled))
    { // stream with no submixes to play should be invalidated after given time
      radioStream->valid = false;
      _radioStreams.Delete(i,1);
      i--; // the index i points to new cell after GRS2->Remove
      curl_multi_remove_handle(multi_handle, (CURL *)radioStream->GetHandle());
      curl_easy_cleanup((CURL *)radioStream->GetHandle());
      #if DIAG_RADIO_STREAM
      LogF("Stream %s invalidated!", cc_cast(radioStream->Name()));
      #endif
      continue;
    }
    if (radioStream->valid) //when invalid, some submixes could be already destroyed
    {
      WaveXA2 *wxa2 = static_cast<WaveXA2 *> (radioStream->_wave.GetRef());
      if (wxa2)
      {
        XAUDIO2_VOICE_SENDS sendList;

        RefArray<AbstractSubmix> &submixes = radioStream->_submixes;
        SendOutputVoices &outputVoices = radioStream->_outputVoices;
        outputVoices.Resize(0);

        for (int j=0; j<submixes.Size(); j++)
        {
          if (submixes[j]->GetHandle()) 
          {
            XAUDIO2_SEND_DESCRIPTOR desc = {0, static_cast<IXAudio2Voice *>(submixes[j]->GetHandle())};
            outputVoices.Add( desc );
          }
        }
        if (outputVoices.Size())
        {
          sendList.SendCount = outputVoices.Size();
          sendList.pSends = outputVoices.Data();
          #if DIAG_RADIO_STREAM
          LogF("wave: %x playing: %x, count: %d", wxa2, submixes[0].GetRef(), outputVoices.Size());
          #endif
        }
        else
        { // set empty
          sendList.pSends = NULL;
          sendList.SendCount = 0;
        }
        wxa2->SetSendList(sendList);
      }
    }
  }
  leave();
}

RadioStream2::~RadioStream2()
{
  if (GRS2) GRS2->Remove(this);
  #if DIAG_RADIO_STREAM
  LogF("Stream %s DESTRUCTED", cc_cast(Name()));
  #endif
}

void RadioStream2::Include(AbstractSubmix *submix)
{
  for (int i=0; i<_submixes.Size(); i++)
    if (_submixes[i]==submix) return; //already present
  _submixes.Add(submix);
}

void RadioStream2::Exclude(AbstractSubmix *submix)
{
  for (int i=0; i<_submixes.Size(); i++)
  {
    if (_submixes[i]==submix) 
    { // found
      _submixes.DeleteAt(i);
      break;
    }
  }
}

const DWORD RadioStream2::MaxInactivityTime = 5*1000; // set more for debug purposes (5s enough in release)
bool RadioStream2::CheckValid(bool simEnabled)
{
  if (_submixes.Size() && simEnabled) // when no simulation, stream should be deactivated after a while
  {
    _activityCheckStartTime = 0; //reset
  }
  else
  {
    if (_activityCheckStartTime)
    {
      if ( GetTickCount()>_activityCheckStartTime+MaxInactivityTime )
      { // remove curl handles and invalidate the stream (destructed in ItemRadio simulation)
        return false;
      }
    }
    else
    {
      _activityCheckStartTime = ::GetTickCount();
      #if DIAG_RADIO_STREAM
      LogF("Stream %s is not played, count down started!", cc_cast(Name()));
      #endif
    }
  }
  return true;
}

RadioStream2* AddInternetRadio(const char *url)
{
  return GRS2 ? GRS2->AddRadio(url) : NULL;
}

void SoundSystemXA2::UpdateRadioSubmixes(bool simEnabled)
{
  if (GRS2) GRS2->Simulate(simEnabled);
}

#endif

