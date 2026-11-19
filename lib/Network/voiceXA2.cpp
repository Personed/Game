/**
@file  
Voice over Net objects used together with DirectSound API.
*/

#include "../wpch.hpp"
#include "../global.hpp"
#include "voiceXA2.hpp"
#include "El/Speech/debugVoN.hpp"
#include "El/Speech/vonCodec.hpp"
#include <Es/Containers/staticArray.hpp>
#include <Es/Algorithms/qsort.hpp>
#include "../XA2CustomAPO.h"

#if VOICE_OVER_NET && _ENABLE_VON && !_GAMES_FOR_WINDOWS

#define DIAG_VON 0

//----------------------------------------------------------------------
//  Replayer buffer:

#undef DS_NOTIFICATION

/**
1/4 of replayer buffer (in seconds). Larger D increases latency but reduces VoN overhead.
Effective replayer latency = 2*D.
*/
const float REPLAYER_BUFFER_D = 0.2;

/**
1/4 of capture buffer (in seconds). Larger D increases latency but reduces VoN overhead.
Effective capture latency = D.
*/

const float CAPTURE_BUFFER_D = 0.125;

/// Direct-sound timer interval in milliseconds.
const long DS_TIMER            = 25L;


#if DEBUG_VON_DIAGS
extern DWORD replayerCurPosition;
extern DWORD VoNReplStatus;
#endif
static void replayerRoutine ( HANDLE object, void *data )
{
  VoNSoundBufferXA2 *buf = (VoNSoundBufferXA2*)data;
  Assert( buf );
  // update what required the main thread through SetPosition, SetObstruction, UpdateVolumeAndAccomodation, ...
  buf->UpdateSoundPars();
  // stop/start control
  buf->doCtrlAction();
  // after all is updated, we can get the currentBuffers
  int write, free;
  buf->GetCurrentBuffers(free, write);

  // get number of queued buffers
  int queued = buf->GetQueuedBuffersCount();

  if (free > 0)
  {
    unsigned minDec = free>0 ? buf->BuffersToSamples(1) : 0;
    // lower minDec (buffer is 1/8s long, but this routine is called every 1/20 of s)
    // this should help with unnecessary dropout silence insertions in buffers
    minDec = minDec >> 1;

    // we need to give some freedom to the buffer size
    // there is some granularity
    unsigned maxDec = std::min(buf->BuffersToSamples(1), free*buf->BuffersToSamples(1));

    // if there are any queued audio buffers, we don't need to decode at this time (we don't want to insert dropout silence if no messages are available)
    // this may cause starvation in audio buffers (because there could be not enough audio left), but we don't really care, because silence will be played
    // automatically by XAudio2
    if (queued > 0)
      minDec = 0;

    // note: pos is actually never used
    unsigned pos = buf->BuffersToSamples(write);

#if defined(NET_LOG_VOICE_VERBOSE) || DEBUG_VON
    NetLog("replayerRoutine: write=%u, free=%u",write,free);
#endif
#if DEBUG_VON_DIAGS
    replayerCurPosition = write;
#endif
    // for purpose of the VoNSoundBuffer interface we need to convert data to samples
    // ask replayer object to decode another bunch of data:
    buf->advanceSafePosition(buf->newData(pos,minDec,maxDec));
  }
}

#define GSoundSysXA2 (static_cast<SoundSystemXA2 *>(GSoundsys) )

VOID CALLBACK ReplayerTimerRoutineXA2(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
  VoNSoundBufferXA2 *buf = (VoNSoundBufferXA2*)lpParam;
  Assert(buf);

  if (!buf->suspended)
    replayerRoutine(NULL, lpParam);
}

#if _USE_TIMER_QUEUE
VoNSoundBufferXA2::VoNSoundBufferXA2(HANDLE qtimer, VoNReplayer *repl, IXAudio2 *device)
#else
VoNSoundBufferXA2::VoNSoundBufferXA2 (EventToCallback *etc, VoNReplayer *repl, IXAudio2 *device)
#endif
{ 
  _playVoicesIX = 0;

#ifdef NET_LOG_VOICE
  NetLog("VoNSoundBufferXA2::VoNSoundBufferXA2");
#endif

  input = false;
  suspended = true;
  size = 0;

  // default sound format:
  format.bitsPerSample = 8;
  format.frequency     = 8000;
  format.granularity   = 1;
  format.alignment     = 1;

#if _USE_TIMER_QUEUE
  Assert(qtimer);
  _timerQueue = qtimer;
#else
  Assert( etc );
  _etc = etc;
#endif
  _ev = NULL;
  Assert( repl );
  _repl = repl;
  Assert( device );
  _device = device;
  _d = _ahead = 0;
  // until buffers are created, assume zero size
  _write = 0;
  _free = 0;

  _playerVoice2D = new PlayerVoice2D(device);
}

VoNSoundBufferXA2::~VoNSoundBufferXA2 ()
{
  destroyBuffer();
}

void VoNSoundBufferXA2::registerCallbacks ()
{
  //if (_buffers.Size() <= 0) 
  //{
  //  //(callback should work even when buffers are not created)
  //  // return; // nothing to do..
  //}

#if _USE_TIMER_QUEUE
  Assert(_timerQueue);
#else
  Assert(_etc);
#endif

  if (_ev) unregisterCallbacks();

#ifdef NET_LOG_VOICE_VERBOSE
  NetLog("Stop (registerCallbacks)");
#endif

#if _USE_TIMER_QUEUE
  BOOL err = CreateTimerQueueTimer(&_ev, _timerQueue, ReplayerTimerRoutineXA2, this, DS_TIMER, DS_TIMER,  WT_EXECUTEINTIMERTHREAD);
  Assert(err);
  (void) err;
#else
  // create & register the timer:
  _ev = CreateWaitableTimer(NULL,FALSE,NULL);
  Assert(_ev);
  _etc->addEvent(_ev,replayerRoutine,this);
  LARGE_INTEGER start = { (int64)0 };
  SetWaitableTimer(_ev,&start,DS_TIMER,NULL,NULL,FALSE);
#endif

  if (!suspended)
  {
    // no need to start playing - will be done once queued
#if defined(NET_LOG_VOICE_VERBOSE) || DEBUG_VON
    NetLog("Play (registerCallbacks)");
#endif
  }
  else
  {
    ScopeLockSection lock(GSoundSysXA2->GetLock());

    if (_sourceVoiceOpen) _sourceVoice->Stop(0, XAUDIO2_COMMIT_NOW);

    UnqueueStream(_buffers.Size() - _free);
  }
}

void VoNSoundBufferXA2::unregisterCallbacks ()
{
#if _USE_TIMER_QUEUE
  Assert(_timerQueue);
#else
  Assert(_etc);
#endif

  if (_sourceVoiceOpen) 
  {
    ScopeLockSection lock(GSoundSysXA2->GetLock());

    HRESULT hr = _sourceVoice->Stop(0, XAUDIO2_COMMIT_NOW);    
    if (hr != S_OK) 
      RptF("VoN: Error %x while stopping source %x", hr, _sourceVoice);
  }

  UnqueueStream(_buffers.Size() - _free);

  if (_ev)
  {
#if _USE_TIMER_QUEUE
    DeleteTimerQueueTimer(_timerQueue, _ev, INVALID_HANDLE_VALUE);
#else
    CancelWaitableTimer(_ev);
    _etc->removeEvent(_ev);
    CloseHandle(_ev);
#endif
    _ev = NULL;
  }

#ifdef NET_LOG_VOICE_VERBOSE
  NetLog("Stop (unregisterCallbacks)");
#endif
}

void VoNSoundBufferXA2::suspendCallbacks ()
{
#if !_USE_TIMER_QUEUE
  Assert(_etc);
  if (_ev) _etc->enableEvent(_ev, false);
#endif
}

void VoNSoundBufferXA2::resumeCallbacks ()
{
#if !_USE_TIMER_QUEUE
  Assert(_etc);
  if (_ev) _etc->enableEvent(_ev,true);
#endif
}

PlayerVoice2D::PlayerVoice2D(IXAudio2 *device) 
  : PlayerVoice() 
{
  if (!device) { _submix=NULL; return; }
  // create submix
  ScopeLockSection lock(GSoundSysXA2->GetLock());
  HRESULT hr = device->CreateSubmixVoice(&_submix, 1, 8000);
  if (hr != S_OK)
  {
    RptF("Error creating PlayerVoice2D submix");
  }
}

void PlayerVoice2D::UpdateGain(int flag2D3D)
{
  ScopeLockSection lock(GSoundSysXA2->GetLock());
  if (!_submix) return;

  // for 2D voice use radio volume
  float vonVolume = (static_cast<SoundSystemXA2 *>(GSoundsys))->_volumeAdjustVoN;
  float gain = (flag2D3D & TTChan2D) ? vonVolume : 0.001f;

  if (!voiceState.gain2DCached)
  {
    voiceState.gain2D = gain;
    voiceState.gain2DCached = true;
    _submix->SetVolume(gain);
  }
  else if (fabs(voiceState.gain2D - gain) > (voiceState.gain2DDiff * gain)) 
  {
    voiceState.gain2D = gain;
    _submix->SetVolume(gain);
  }
}

PlayerVoice3D::PlayerVoice3D(float x, float y, float z, IXAudio2 *device)
 : PlayerVoice() 
{ 
  _position = Vector3(x,y,z);
  _speed = VZero; 
  Set3DPosition(_position, _speed); 
  if (!device) { _submix=NULL; return; }
  // create submix
  ScopeLockSection lock(GSoundSysXA2->GetLock());
  HRESULT hr = device->CreateSubmixVoice(&_submix, 1, 8000);
  if (hr != S_OK)
  {
    RptF("Error creating PlayerVoice3D submix");
  }
}

void VoNSoundBufferXA2::UpdateSoundPars()
{
  if (_buffers2D3D.Size())
  {
    int flag2D3D = _buffers2D3D[(_write + _free) % _buffers2D3D.Size()];
    if (_playerVoice3D)
      _playerVoice3D->UpdateSoundPars(flag2D3D);
  }
}

void PlayerVoice3D::UpdateSoundPars(int flag2D3D)
{
  // Set3DPosition
  DWORD time = GlobalTickCount();
  if (voiceState.set3DPos)
  {
    ScopeLockSection lock(GSoundSysXA2->GetLock());

    _position = voiceState.position;
    _speed = voiceState.speed;

    Calculate3DAudio();

    voiceState.set3DPos = false;

    // parsTime means the 3D position is set (at least in the m_voiceState) so the direct speak can start working
    voiceState.parsTime = time;
  }
  // UpdateVolumeAndAccomodation
  // SetObstruction
  UpdateVolume(flag2D3D);
}

void PlayerVoice3D::UpdateGain(int flag2D3D)
{
  ScopeLockSection lock(GSoundSysXA2->GetLock());
  if (!_submix) return;

  float gain = (flag2D3D & TTChan3D) ? voiceState.current3DGain : 0.001f;
  //no 3D sound until position and other params are set (from Man::UpdateSpeechPosition)
  if (!voiceState.parsTime) gain = 0.001f; 

  if (!voiceState.gain3DCached)
  {
    voiceState.gain3D = gain;
    voiceState.gain3DCached = true;
    _submix->SetVolume(gain);
  }
  else if (fabs(voiceState.gain3D - gain) > (voiceState.gain3DDiff * gain)) 
  {
    voiceState.gain3D = gain;
    _submix->SetVolume(gain);
  }      
}

void PlayerVoice3D::Set3DPosition(Vector3Par pos, Vector3Par speed)
{
  voiceState.position = pos;
  voiceState.speed = speed;
  voiceState.set3DPos = true;
}

void PlayerVoice3D::UpdateVolumeAndAccomodation(float volume, float accomodation)
{
  voiceState.volume = volume;
  voiceState.accomodation = accomodation;
}

void PlayerVoice3D::SetObstruction(float obstruction, float occlusion, float deltaT)
{
  // change obstruction gradually
  const float minEps = 0.01f;
  if (
    fabs(occlusion-voiceState.occlusion)<minEps*floatMin(voiceState.occlusion,occlusion) &&
    fabs(obstruction-voiceState.obstruction)<minEps*floatMin(voiceState.obstruction,obstruction)
    )
  {
    return;
  }

  if (deltaT<=0)
  {
    voiceState.occlusion = occlusion;
    voiceState.obstruction = obstruction;
  }
  else
  {
    float delta;
    delta = occlusion - voiceState.occlusion;
    const float maxSpeed = 1.5f;
    Limit(delta,-deltaT*maxSpeed,+deltaT*maxSpeed);
    voiceState.occlusion += delta;

    delta = obstruction - voiceState.obstruction;
    Limit(delta,-deltaT*maxSpeed,+deltaT*maxSpeed);
    voiceState.obstruction += delta;
  }
}

static inline float DistanceRolloff(float minDistance, float distance)
{
  return minDistance/distance;
}

void PlayerVoice3D::UpdateVolume(int flag2D3D)
{
  // for 3D voice (these are part of the environment) simulate obstructions
  float volumeAdjust = (static_cast<SoundSystemXA2 *>(GSoundsys))->_volumeAdjustEffect;
  const bool is3DEnabled = true; //this method is called only for 3D sounds
  if (is3DEnabled || voiceState.accomodation)
  {
    // no HW obstruction/occlusion
    // we use total volume to simulate it
    float obsVolume = 1-(1-voiceState.obstruction)*0.7f;
    // assume occlusion reduces the sound much more than obstruction
    float occVolume = 1-(1-voiceState.occlusion)*0.9f;
    float volumeFactor = floatMin(obsVolume,occVolume);
    volumeAdjust *= volumeFactor;
  }

  if (!is3DEnabled)
  {
    // some problem with setting parameters
    // convert _volume do Db
    float vol = voiceState.volume*voiceState.accomodation*volumeAdjust;
    static bool Only2DEnabled = false;
    if (!Only2DEnabled)
    {
      // simulate DirectSound attenuation for _distance2D
      // calculate volume based on distance2D
      float minDistance = CalcMinDistance();
      static float Distance2D = 1.0f;
      float atten = Distance2D<minDistance ? 1 : DistanceRolloff(minDistance,Distance2D);
      vol = atten*volumeAdjust;
    }
    SetGain(vol, flag2D3D);
  }
  else
  {
    SetGain(volumeAdjust, flag2D3D);
  }
}

float PlayerVoice3D::CalcMinDistance() const
{
  static float MinDistCoef = 30.0f; 
  return floatMax(sqrt(voiceState.volume*voiceState.accomodation)*MinDistCoef,1e-6);
}

void PlayerVoice3D::SetGain(float gain, int flag2D3D)
{
  // input is multiplicative gain
  voiceState.current3DGain = gain;
  UpdateGain(flag2D3D);
}

void PlayerVoice3D::Calculate3DAudio()
{
  SoundSystemXA2* sSystem = static_cast<SoundSystemXA2 *>(GSoundsys);
  if (!sSystem || !sSystem->_soundReady || !_submix) return;


  int srcChannels = 1;
  int dstChannels = sSystem->_outFmt.nChannels;

  float channelAzimuth = 0.0f;
  AutoArray<float, MemAllocLocal<float, 8> > matrixCoeffs;
  matrixCoeffs.Resize(srcChannels*dstChannels);

  //float minDist = CalcMinDistance();

  X3DAUDIO_EMITTER emitter = { 0 };
  static const D3DVECTOR topVec = { 0, 1, 0 };
  static const D3DVECTOR frontVec = { 1, 0, 0 };
  static const float MaxVoiceAudibilityDistance = 100;
  // volume curve definition
  static const X3DAUDIO_DISTANCE_CURVE_POINT Emitter_Volume_CurvePoints[4] = { 0.0f, 1.0f, 0.2f, 0.2f, 0.5f, 0.1f, 1.0f, 0.0f };
  static const X3DAUDIO_DISTANCE_CURVE       Emitter_Volume_Curve          = { (X3DAUDIO_DISTANCE_CURVE_POINT*)&Emitter_Volume_CurvePoints[0], 4 };

  emitter.OrientTop = topVec;
  emitter.OrientFront = frontVec;
  emitter.pChannelAzimuths = &channelAzimuth;
  emitter.ChannelCount = srcChannels;

  emitter.Position.x = _position.X();
  emitter.Position.y = _position.Y();
  emitter.Position.z = _position.Z();
  emitter.Velocity.x = _speed.X();
  emitter.Velocity.y = _speed.Y();
  emitter.Velocity.z = _speed.Z();
  emitter.DopplerScaler = 1.0f;
  emitter.pCone = NULL;
  emitter.CurveDistanceScaler = MaxVoiceAudibilityDistance;
  emitter.pVolumeCurve = (X3DAUDIO_DISTANCE_CURVE*)&Emitter_Volume_Curve;

  X3DAUDIO_DSP_SETTINGS dspSettings = { 0 };
  dspSettings.SrcChannelCount = srcChannels;
  dspSettings.pMatrixCoefficients = matrixCoeffs.Data();
  dspSettings.DstChannelCount = dstChannels;

  // 3d pars calculation
  X3DAudioCalculate(sSystem->_x3dInstance, &sSystem->_listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dspSettings);

  HRESULT hr = _submix->SetOutputMatrix(0, srcChannels, dstChannels, matrixCoeffs.Data());
  if (hr != S_OK)
  {
    RptF("PlayerVoice3D::Calculate3DAudio SetOutputMatrix failed");
  }
}

TransmitterVoice::TransmitterVoice(float x, float y, float z, float range, IXAudio2 *device)
 : PlayerVoice3D(x,y,z,device), _nextUpdateTime(0)
{ 
  _quality = 0;  // best quality as default
  if (_submix)
  {
    XAUDIO2_VOICE_DETAILS voiceDetails;
    _submix->GetVoiceDetails(&voiceDetails);

    Discretizer256APO *pDiscretizer256APO = NULL;
    Discretizer256APO::CreateInstance(NULL, 0, &pDiscretizer256APO);
    IUnknown *pIUnknown256 = NULL;
    pDiscretizer256APO->QueryInterface(__uuidof(IUnknown), (void **)&pIUnknown256);

    GaussianNoiceAPO *pGaussianNoiseAPO = NULL;
    GaussianNoiceAPO::CreateInstance(NULL, 0, &pGaussianNoiseAPO);
    IUnknown *pIUnknownGauss = NULL;
    pGaussianNoiseAPO->QueryInterface(__uuidof(IUnknown), (void **)&pIUnknownGauss);

    XAUDIO2_EFFECT_DESCRIPTOR chainDescr[] = 
    {
      { pIUnknown256, TRUE, voiceDetails.InputChannels },
      { pIUnknownGauss, TRUE, voiceDetails.InputChannels }
    };

    XAUDIO2_EFFECT_CHAIN chain = { lenof(chainDescr), chainDescr };
    _submix->SetEffectChain(&chain); 
    Discretizer256Pars dpars; dpars.voiceQuality=1.f;
    _submix->SetEffectParameters(0, &dpars, sizeof(Discretizer256Pars));
    GaussianNoisePars gpars; gpars.voiceQuality=1.f;
    _submix->SetEffectParameters(1, &gpars, sizeof(GaussianNoisePars));
    _submix->EnableEffect(0);
    _submix->EnableEffect(1);
  }
}

void TransmitterVoice::UpdateSoundPars(int flag2D3D)
{
  // update EffectParameters from time to time
  DWORD time = GlobalTickCount();
  if (time > _nextUpdateTime)
  {
    _nextUpdateTime = time + 1000; //evert second is enough
    
    Discretizer256Pars dpars; dpars.voiceQuality = 1-_quality;
    saturate(dpars.voiceQuality, 0.f, 1.f);
    _submix->SetEffectParameters(0, &dpars, sizeof(Discretizer256Pars));
    _submix->EnableEffect(0);

    GaussianNoisePars gpars; gpars.voiceQuality = dpars.voiceQuality;
    _submix->SetEffectParameters(1, &gpars, sizeof(GaussianNoisePars));
    _submix->EnableEffect(1);
  }

  base::UpdateSoundPars(flag2D3D);
}

static bool SoundParsNeedUpdate(int timeMs)
{
  // never update sound more than 20 fps
  const int minTimeUpdate = 50;
  if (timeMs<minTimeUpdate)
  {
    return false;
  }
  return true;
}

void VoNSoundBufferXA2::Set3DPosition(Vector3Par pos, Vector3Par speed)
{
  // Called from main thread!
  if (!_playerVoice3D) _playerVoice3D = new PlayerVoice3D(pos.X(), pos.Y(), pos.Z(), _device);
  if (_playerVoice3D) _playerVoice3D->Set3DPosition(pos, speed);
}

void VoNSoundBufferXA2::UpdateVolumeAndAccomodation(float volume, float accomodation)
{
  // Called from main thread!
  if (_playerVoice3D) _playerVoice3D->UpdateVolumeAndAccomodation(volume, accomodation);
}

VoNChatChannel VoNSoundBufferXA2::CheckChatChannel(bool &audible2D, bool &audible3D) const
{
  // Called from main thread!
  if (!_repl)
  {
    audible2D = audible3D = false;
    return 0;
  }
  audible2D = _repl->GetPlay2D();
  audible3D = _repl->GetPlay3D();
  return _repl->GetChatChannel();
}

void VoNSoundBufferXA2::SetObstruction(float obstruction, float occlusion, float deltaT)
{
  // Called from main thread!
  if (_playerVoice3D) _playerVoice3D->SetObstruction(obstruction, occlusion, deltaT);
}

void VoNSoundBufferXA2::createBuffer (unsigned len)
{
  Assert(_ev == NULL);

#ifdef NET_LOG_VOICE
  NetLog("VoNSoundBufferXA2::createBuffer(): len=%u, freq=%d, bps=%d",
    len,(int)format.frequency,(int)format.bitsPerSample);
#endif
  size = _d = _ahead = 0;
  suspended = true;

  // use
  _bufferSize = ((len >> 2) + format.granularity - 1) / format.granularity * format.granularity;

  int bufferCount = (len + _bufferSize-1) / _bufferSize;
  len = bufferCount * _bufferSize;

  _sourceVoice = NULL;
  _sourceVoiceOpen = false;

  { 
    // sound buffers can be created always (even when source creation failed)

    ScopeLockSection lock(GSoundSysXA2->GetLock());

    _buffers2D3D.Realloc(bufferCount);
    _buffers2D3D.Resize(bufferCount);
    _buffersLen.Realloc(bufferCount);
    _buffersLen.Resize(bufferCount);

    size = len;
    _free = bufferCount;
    _write = 0;
    // no need to provide any data - buffer will not start until we queue something
    if (size)
    {
      _d = (len + 2) >> 2;                 // D = 1/4 of buffer length (in samples)
      _ahead = (8 * _d) >> 2;             // 8/4 of D
    }
  }

  DWORD time = GlobalTickCount();
  m_activeBufferStartTime = time; //initialize in order to make time running during muted
}

int VoNSoundBufferXA2::GetQueuedBuffersCount()
{
  ScopeLockSection lock(GSoundSysXA2->GetLock());

  // number of sourceVoice queued
  if (_sourceVoiceOpen)
  {
    XAUDIO2_VOICE_STATE state;
    _sourceVoice->GetState(&state);
    return state.BuffersQueued;
  }
  return 0;
}

bool VoNSoundBufferXA2::AreBuffersReady()
{
  return _buffers.Size() > 0;
}

void VoNSoundBufferXA2::GetCurrentBuffers(int &free, int &write)
{
  if (_buffers.Size()<=0)
  {
    //we should make time running (throw muted buffers continuously)
    if (!_buffersLen.Size()) return;
    DWORD time = GlobalTickCount();
    int bufTimeElapsed = time-m_activeBufferStartTime;
    int busyIx = (_write + _free) % _buffersLen.Size();
    while ( bufTimeElapsed >= _buffersLen[busyIx] && _free<_buffersLen.Size() )
    {
      _free++; //"unqueued"
      bufTimeElapsed -= _buffersLen[busyIx];
      busyIx = (busyIx+1) % _buffersLen.Size();
    }
    m_activeBufferStartTime = time-bufTimeElapsed; //next buffer will start now
    write=_write; free = _free;
    return;
  }

  ScopeLockSection lock(GSoundSysXA2->GetLock());

  // sourceVoice buffering
  int queued = 0, processed = _buffers.Size();
  if (_sourceVoiceOpen)
  {
    XAUDIO2_VOICE_STATE state;
    _sourceVoice->GetState(&state);
    queued = state.BuffersQueued;
    processed = _sourceVoiceQueued - queued;

    if (!queued && !processed) processed = _buffers.Size() - _free;
  }

  UnqueueStream(processed);

  if (_playerVoice3D)
    _playerVoice3D->UpdateGain(_buffers2D3D[(_write + _free) % _buffers2D3D.Size()]);
  if (_playerVoice2D)
    _playerVoice2D->UpdateGain(_buffers2D3D[(_write + _free) % _buffers2D3D.Size()]);

  write = _write;
  free = _free;
}

void VoNSoundBufferXA2::suspendBuffer ()
{
  if (_buffers.Size() <= 0) 
  {
    suspended = true;
    suspendCallbacks();
    return; // nothing else to do..
  }

  ScopeLockSection lock(GSoundSysXA2->GetLock());

  HRESULT hr = S_OK;
  if (_sourceVoiceOpen) hr = _sourceVoice->Stop(0, XAUDIO2_COMMIT_NOW);
  Verify(hr == S_OK);

  UnqueueStream(_buffers.Size() - _free);

#if defined(NET_LOG_VOICE_VERBOSE) || DEBUG_VON
  NetLog("Stop (suspendBuffer)");
#endif
  suspended = true;
  suspendCallbacks();
}

void VoNSoundBufferXA2::resumeBuffer ()
{
  if (_buffers.Size() <= 0)
  {
#if _USE_TIMER_QUEUE
    suspended = false;
#endif
    return;
  }
  resumeCallbacks();
  suspended = false;

#if defined(NET_LOG_VOICE_VERBOSE) || DEBUG_VON
  NetLog("Play (resumeBuffer)");
#endif
}

void VoNSoundBufferXA2::destroyBuffer ()
{
#if defined(NET_LOG_VOICE) || DEBUG_VON
  NetLog("VoNSoundBufferXA2::destroyBuffer");
#endif
  suspended = true;
  unregisterCallbacks();
  size = _d = _ahead = 0;
  if (_buffers.Size() <= 0) return; // nothing else to do..

  ScopeLockSection lock(GSoundSysXA2->GetLock());

  // release allocated memory for audio buffers
  for (int i = 0; i < _buffers.Size(); i++)
  {
    _buffers[i]->Clear();
    _buffers[i].Free();
  }

  _buffers.Clear();

  // release allocated memory for audio buffers description
  for (int i = 0; i < _buffersDescr.Size(); i++)
  {
    _buffersDescr[i].Free();
  }

  _buffersDescr.Clear();

  if (_sourceVoiceOpen) 
  {
    _sourceVoice->Stop(0, XAUDIO2_COMMIT_NOW);
    _sourceVoice->FlushSourceBuffers();
    _sourceVoice->DestroyVoice();

    _sourceVoice = NULL;
    _sourceVoiceOpen = false; 
  }
#if DIAG_VON
  LogF("VoNSoundBufferXA2::destroyBuffer");
#endif
}

unsigned VoNSoundBufferXA2::newData (unsigned &pos, unsigned minSamples, unsigned maxSamples)
{
  Assert( _repl );
  return _repl->decode(this, pos, minSamples, maxSamples);
}

void VoNSoundBufferXA2::advanceSafePosition (unsigned inc)
{
  // we do not really care - buffers are updated when queued
}

void VoNSoundBufferXA2::lockData (unsigned pos, unsigned len, CircularBufferPointers &ptr)
{
  Zero(ptr);
  // we need to process data even when muted
  // if (m_buffers.Size()<=0) return;

  // lock max. possible
  // is less will be used, we will queue less
  ptr.len1 = len;

  // convert to bytes
  if (format.bitsPerSample > 8)
  {
    pos *= 2;
    len *= 2;
  }
  // position is ignored
  // we use CircularBufferPointers to allocate a temporary memory
  // TODO: use one memory buffer and recycle it if possible
  void *mem = safeNew(len);
  // note: because of union used in CircularBufferPointers following two branches generate the same code
  if (format.bitsPerSample > 8)
  {
    ptr.ptr1.bps16 = (int16 *)mem;
  }
  else
  {
    ptr.ptr1.bps8 = (int8 *)mem;
  }
}

void VoNSoundBufferXA2::UnqueueStream(int processed)
{
  if (processed <= 0) return;

  // which one are those processed?
  Assert(processed <= _buffers.Size() - _free);

  int firstBusy = _write + _free;
  if (firstBusy >= _buffers.Size()) firstBusy -= _buffers.Size();

  ScopeLockSection lock(GSoundSysXA2->GetLock());

  for (int i = 0; i < processed; i++)
  {
    int ix = (firstBusy + i) % _buffers.Size();

    // remove buffer from queue ... ? where is stored    
    if ((_buffers2D3D[ix] & TTBufQueued) && _sourceVoiceOpen)
    {
      _buffers[ix]->Clear();
      _sourceVoiceQueued--;
    }
  }

  _free += processed;
}

static bool ValidateXA2Buffer(const XAUDIO2_BUFFER &buff)
{
  return (0 > buff.AudioBytes && buff.AudioBytes < XAUDIO2_MAX_BUFFER_BYTES && 
    buff.Flags == 0 && buff.pAudioData != 0 &&
    buff.LoopBegin == 0 && buff.LoopLength == 0 && buff.LoopCount == 0 && 
    buff.PlayBegin == 0 && buff.PlayLength == 0);
}

void VoNSoundBufferXA2::unlockData (unsigned processed, CircularBufferPointers &ptr)
{
  if (_buffers.Size() <= 0) 
  {
    // we should make time flowing even when VoN voice is muted
    int freq = format.frequency ? format.frequency : 1;
    _buffersLen[_write] = (1000 * processed) / freq;
    _write++; _free--; //simulate as if new VoN data were queued

    if (_write >= _buffers2D3D.Size()) _write = 0;

    if (format.bitsPerSample > 8)
    {
      safeDelete(ptr.ptr1.bps16);
      ptr.ptr1.bps16 = 0;
    }
    else
    {
      safeDelete(ptr.ptr1.bps8);
      ptr.ptr1.bps8 = 0;
    }
    ptr.len1 = 0;

    return;
  }

  int freq = format.frequency ? format.frequency : 1;
  _buffersLen[_write] = (1000 * processed) / freq;

  ScopeLockSection lock(GSoundSysXA2->GetLock());

  // when unlocking, queue the data
  Ref<XA2Buffer> bufR = _buffers[_write];

  Assert(bufR.NotNull());

  // audio data description
  Ref<XA2DescBuffer> xa2DescBuffer = _buffersDescr[_write];

  Assert(xa2DescBuffer.NotNull());

  void *data = format.bitsPerSample > 8 ? (void *)ptr.ptr1.bps16 : (void *)ptr.ptr1.bps8;

  Assert(processed <= ptr.len1);
  if (format.bitsPerSample > 8) processed *= 2;

  // copy audio data
  bufR->Copy((const char*)data, processed);

#if DEBUG_VON_SAVE_BUFFERS
  int toWrite = format.bitsPerSample > 8 ? processed/2 : processed;
  if (GDebugVoNSaveBuffers.GetReplayBuf().MessagesEnabled())
  {
    for (int i=0; i<toWrite; ++i)
    {
      GDebugVoNSaveBuffers.GetReplayBuf().Add( ((DebugVoNSaveBuffers::SoundBuffer::TElementType *)data)[i]);
    }
  }
#endif

  xa2DescBuffer->Flags = 0;
  xa2DescBuffer->LoopBegin = 0;
  xa2DescBuffer->LoopCount = 0;
  xa2DescBuffer->LoopLength = 0;
  xa2DescBuffer->PlayBegin = 0;
  xa2DescBuffer->PlayLength = 0;
  xa2DescBuffer->pContext = 0;
  xa2DescBuffer->AudioBytes = bufR->Size();
  xa2DescBuffer->pAudioData = (BYTE*) bufR->Data();

  bool isPlaying = false;

  XAUDIO2_VOICE_STATE state = { 0 };

  // check is source is playing
  if (_sourceVoiceOpen) 
  { 
    _sourceVoice->GetState(&state);
    isPlaying = (state.BuffersQueued != 0 || state.SamplesPlayed != 0);

    // queue current buffer
    if (_repl->result()!=VonSilence && _repl->result()!=VonDropout)
    {
      HRESULT hr = _sourceVoice->SubmitSourceBuffer(xa2DescBuffer.GetRef());
      CHECK_XA2_VOICE_ERROR(hr, "VoNSoundBufferXA2::unlockData - SubmitSourceBuffer");
    }
    _buffers2D3D[_write] |= TTBufQueued;
    _sourceVoiceQueued++;
  }

  Assert(_free > 0);

  int oldWrite = _write++;
  _free--;

  if (_write >= _buffers.Size()) _write = 0;

  HRESULT hr = S_OK;

  // make sure the buffers start playing
  if (_sourceVoiceOpen && !isPlaying && (_buffers2D3D[oldWrite] & (TTChan2D|TTChan3D) ))
  {
    hr = _sourceVoice->Start(0, XAUDIO2_COMMIT_NOW);
    CHECK_XA2_VOICE_ERROR(hr, "VoNSoundBufferXA2::unlockData - Start");
  }

  if (format.bitsPerSample > 8)
  {
    safeDelete(ptr.ptr1.bps16);
    ptr.ptr1.bps16 = 0;
  }
  else
  {
    safeDelete(ptr.ptr1.bps8);
    ptr.ptr1.bps8 = 0;
  }
  ptr.len1 = 0;

  Verify(hr == S_OK);
}

void VoNSoundBufferXA2::update2D3D(int play2D3D)
{
  if (_buffers2D3D.Size() <= 0) return;
  _buffers2D3D[_write] = play2D3D;
}

void VoNSoundBufferXA2::startSource()
{
  if (!_buffers2D3D.Size()) return; //unable to start it. createBuffer was unsuccessful before
  if (!_device) return;

  // source voice
  if (!_sourceVoiceOpen) 
  {
    WAVEFORMATEX waveFmt;
    waveFmt.wFormatTag = WAVE_FORMAT_PCM;
    waveFmt.nChannels = 1;
    waveFmt.wBitsPerSample = format.bitsPerSample > 8 ? 16 : 8;
    waveFmt.nSamplesPerSec = format.frequency;
    waveFmt.nBlockAlign = format.bitsPerSample / 8;
    waveFmt.nAvgBytesPerSec = waveFmt.nSamplesPerSec * waveFmt.nBlockAlign;
    waveFmt.cbSize = sizeof(WAVEFORMATEX);

    ScopeLockSection lock(GSoundSysXA2->GetLock());
    HRESULT hr = _device->CreateSourceVoice(&_sourceVoice, &waveFmt);

    if (hr == S_OK)
    {
      _sourceVoiceOpen = true;
      _sourceVoiceQueued = 0;
    }
    else
    {
      RptF("startVoice: cannot create source, error %x", hr);
      _sourceVoiceOpen = false;
    }
  
    //createBuffer if it didn't exist
    Verify(_buffers.Size() == 0);

    int bufferCount = _buffers2D3D.Size();
    int len = bufferCount * _bufferSize;

    _buffers.Realloc(bufferCount);
    _buffers.Resize(bufferCount);

    _buffersDescr.Realloc(bufferCount);
    _buffersDescr.Resize(bufferCount);

    for (int i = 0; i < bufferCount; i++)
    {
      _buffers[i] = new XA2Buffer;
      _buffersDescr[i] = new XA2DescBuffer;
    }

    // no need to provide any data - buffer will not start until we queue something
    if (len)
    {
      _d = (len + 2) >> 2;                 // D = 1/4 of buffer length (in samples)
      _ahead = (8 * _d) >> 2;             // 8/4 of D
    }

    if (_buffers.Size() <= 0)
    {
      // we should keep time running even in this case
      _repl->ForceVoNMessagesDiscarding();

      RptF("StartVoice: VoN sound buffer was not created!");
    }
  }
}

void VoNSoundBufferXA2::stopSource()
{
  if (!_sourceVoiceOpen) return; //already stopped

  HRESULT hr = S_OK;

  ScopeLockSection lock(GSoundSysXA2->GetLock());

  // 1. stop source
  hr = _sourceVoice->Stop(0, XAUDIO2_COMMIT_NOW);
  Verify(hr == S_OK);

  // 2. unqueue stream (if no source remains open after this call)
  if (_buffers.Size() > 0)  // something was playing
  {
    UnqueueStream(_buffers.Size() - _free);
  }

  // 3. unbind the buffer from the given sources
  hr = _sourceVoice->FlushSourceBuffers();
  Verify(hr == S_OK);

  // 4. delete the buffer (only when no source uses it)
  if (_buffers.Size() > 0)  // something was playing
  {
    for (int i = 0; i < _buffers.Size(); i++)
    {
      // clear audio data
      _buffers[i]->Clear();
      _buffers[i].Free();
    }

    // TODO: check if _bufferDescr is deleted

    _buffers.Clear();
    _buffersDescr.Clear();

    DWORD time = GlobalTickCount();
    m_activeBufferStartTime = time; //initialize in order to make time running during muted
  }

  // 5. delete source
  _sourceVoice->DestroyVoice();
  _sourceVoice = 0;
  _sourceVoiceOpen = false;
  if (_playerVoice2D)
    _playerVoice2D->voiceState.InvalidateCache(TTChan2D);
  if (_playerVoice3D)
    _playerVoice3D->voiceState.InvalidateCache(TTChan3D);
}

// Only schedule startVoice (it is called from another thread, it should be MT safe)
void VoNSoundBufferXA2::setStartVoice(PlayerVoice *voice)
{
  enterSoundBuffer();

  RefArray<PlayerVoice, MemAllocSafe>  &playVoices = _playVoices[_playVoicesIX];
  bool done = false;
  for (int i = 0; i < playVoices.Size(); i++)
  {
    if (playVoices[i].GetRef()==voice) { done = true; break; }
  }
  if (!done) playVoices.Add(voice);

  leaveSoundBuffer();
}

void VoNSoundBufferXA2::clearVoices()
{
  enterSoundBuffer();
  _playVoices[_playVoicesIX].Resize(0); //clear _playVoices to add into
  leaveSoundBuffer();
}

void VoNSoundBufferXA2::switchVoices()
{
#define DEBUG_SWITCH_VOICES 0
#if DEBUG_SWITCH_VOICES
  for (int i=0; i<_playVoices[_playVoicesIX].Size(); i++)
  {
    int j=0;
    for (; j<debugVoices.Size(); j++)
    {
      if (debugVoices[j]==_playVoices[_playVoicesIX][i]) break;
    }
    if (j==debugVoices.Size()) RptF("START VOICE: %x", _playVoices[_playVoicesIX][i]);
  }
  for (int i=0; i<debugVoices.Size(); i++)
  {
    int j=0;
    for (; j<_playVoices[_playVoicesIX].Size(); j++)
    {
      if (debugVoices[i]==_playVoices[_playVoicesIX][j]) break;
    }
    if (j==_playVoices[_playVoicesIX].Size()) RptF("STOP VOICE: %x", debugVoices[i]);
  }
  debugVoices = _playVoices[_playVoicesIX];
#endif

  _playVoicesIX = (_playVoicesIX+1)%2; //advance _playVoices to play
}

// Process scheduled control action (stop/start voice)
void VoNSoundBufferXA2::doCtrlAction()
{
  AutoArray<XAUDIO2_SEND_DESCRIPTOR> startVoices;

  enterSoundBuffer();
  RefArray<PlayerVoice, MemAllocSafe>  &playVoices = _playVoices[(_playVoicesIX+1)%2];

  for (int i = 0; i < playVoices.Size(); i++)
  {
    if (playVoices[i]->Submix())
    {
      XAUDIO2_SEND_DESCRIPTOR voice = {0, playVoices[i]->Submix()};
      startVoices.Add(voice);
    }
  }
  leaveSoundBuffer();
  
  if ( startVoices.Size() )
  {
    startSource();
    XAUDIO2_VOICE_SENDS sendList = {startVoices.Size(), startVoices.Data()};
    ScopeLockSection lock(GSoundSysXA2->GetLock());
    if (_sourceVoiceOpen)
    {
      HRESULT hr = _sourceVoice->SetOutputVoices(&sendList);
      if (hr != S_OK)
      {
        RptF("SetOutputVoices has failed!");
      }
    }
  }
  else
  {
    stopSource();
  }
}

#include "Es/Memory/normalNew.hpp"

void* VoNSoundBufferXA2::operator new ( size_t size )
{
  return safeNew(size);
}

void* VoNSoundBufferXA2::operator new ( size_t size, const char *file, int line )
{
  return safeNew(size);
}

void VoNSoundBufferXA2::operator delete ( void *mem )
{
  safeDelete(mem);
}

#include "Es/Memory/debugNew.hpp"

//----------------------------------------------------------------------
//  Capture buffer:

/*!
\patch 5110 Date 12/21/2006 by Ondra
- Fixed: Voice recording did not work with some sound-cards/drivers.
*/
static void captureRoutine ( HANDLE object, void *data )
{
  VoNCaptureBufferXA2 *buf = (VoNCaptureBufferXA2*)data;
  Assert( buf );

  if (!buf->_wCapture.DeviceOpened()) return;

  Assert(buf->m_data.Data());

  //for(;;)
  {
    ScopeLockSection lock(GSoundSysXA2->GetLock());

    // captured samples
    unsigned samples = buf->_wCapture.CapturedSamples();

    unsigned int spaceLeftInBuf = buf->m_data.Size() - buf->m_dataUsed;
    unsigned int toCapture = intMin(samples, spaceLeftInBuf);

    // if we should get next recorded buffer
    bool getBuffer = false;
    if (buf->suspended)
    {
      //buffer is suspended (recording is stopped)
      //we want to get buffer even if another buffer is not ready yet
      getBuffer = samples > 0 && (samples <= spaceLeftInBuf || toCapture >= buf->_wCapture.CapturedBuffSize());
    }
    else
    {
      getBuffer = samples > 0 && (toCapture > buf->_wCapture.CapturedBuffSize());
    }

    if (getBuffer)
    {
      Assert(buf->m_dataUsed >= 0);

      // there is free memory at least for one buffer
      unsigned int writeB = buf->_wCapture.GetBuffer(buf->m_data.Data() + buf->m_dataUsed, toCapture);

      buf->m_dataUsed += writeB;
    }
    /*else
      break;*/
  }

  int minEnc = 0;
  int maxEnc = buf->m_dataUsed;

#ifdef NET_LOG_VOICE_VERBOSE
  NetLog("captureRoutine: samples=%u, dataUsed=%u, min=%u, max=%u",
    samples, m_dataUsed, minEnc, maxEnc);
#endif

  // ask recorder object to encode another bunch of data:
  buf->advanceSafePosition(buf->newData(buf->m_nextPtr, minEnc, maxEnc));
}


VOID CALLBACK CaptureTimerRoutineXA2(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
  //VoNSoundBufferXA2 *buf = (VoNSoundBufferXA2*)lpParam;
  //Assert(buf);
  //if (!buf->suspended)
  // LUKE - we want to call capture routine even if buffer is suspended (because there can be still some recorded data to send)
    captureRoutine(NULL, lpParam);
}
//////////////////////////////////////////////////////////////////////////

WaveCapture::WaveCapture() : _deviceOpened(false), _firstBuffer(true), _hWaveIn(0)
{
}

bool WaveCapture::OpenDevice(const WAVEFORMATEX &waveFmt, unsigned len)
{
  _waveFmt = waveFmt;
  _length = len / BUFFERS_USED;
  _samplesCaptured = 0;

  // opens the given waveform-audio input device for recording
  MMRESULT mmRes = waveInOpen(&_hWaveIn, WAVE_MAPPER, &waveFmt, (DWORD_PTR) waveInProc, (DWORD_PTR) this, CALLBACK_FUNCTION);

  if (mmRes != MMSYSERR_NOERROR)
  {
    _deviceOpened = false;
    return false;
  }

  _deviceOpened = true;

  if (!PrepareBuffers())
  {
    UnPrepareBuffers();
    CloseDevice();

    return false;
  }

  //Stop();

  return true;
}

void WaveCapture::CloseDevice()
{
  if (!_hWaveIn) return;

  UnPrepareBuffers();

  _buffReady.Clear();

  // closes the given waveform-audio input device
  waveInClose(_hWaveIn);

  _deviceOpened = false;

  // free memory used for capturing audio data
  for (int i = 0; i < BUFFERS_USED; i++)
  {
    _data[i].Clear();
  }

  _hWaveIn = NULL;
}

void WaveCapture::Start()
{
  if (_hWaveIn)
  {  
    _firstBuffer = true;
    waveInStart(_hWaveIn);
  }
}

void WaveCapture::Stop()
{
  if (_hWaveIn)
  {
    waveInStop(_hWaveIn);
#if DEBUG_VON_SAVE_BUFFERS
    GDebugVoNSaveBuffers.GetRecordBuf().Save();
#endif
  }
}

void CALLBACK WaveCapture::waveInProc(HWAVEIN hwi, UINT uMsg, DWORD dwInstance, DWORD dwParam1, DWORD dwParam2)
{
  switch(uMsg)
  {
  case WIM_CLOSE:
    break;

  case WIM_DATA:
    {
      WaveCapture *ptrWWC = (WaveCapture *) dwInstance;
      ptrWWC->ProcessData((WAVEHDR *) dwParam1);
    }
    break;

  case WIM_OPEN:
    break;

  default:
    break;
  }
}

void WaveCapture::ProcessData(WAVEHDR *waveHdr)
{
  if (_deviceOpened)
  {
    Assert(_free <= BUFFERS_USED);

    // captured samples
    _samplesCaptured += (waveHdr->dwBytesRecorded / sizeof(short));

    // audio buffer filled
    _buffReady[_write] = waveHdr;

#if DEBUG_VON_SAVE_BUFFERS
    int idx = waveHdr->dwUser;
    int samplesRecorded = (waveHdr->dwBytesRecorded / sizeof(short));
    int size = min (samplesRecorded, _data[idx].Size());
    for (int i=0; i<size; ++i)
    {
      GDebugVoNSaveBuffers.GetRecordBuf().Add(_data[idx][i]);
    }
    GDebugVoNSaveBuffers.GetRecordBuf().AddMessage(Format("WaveCapture - ProcessData buffer %d, totalCaptured: %d, recorded: %d, buffer length %d", waveHdr->dwUser, _samplesCaptured, samplesRecorded,  waveHdr->dwBufferLength));
#endif

    //LogF("ProcessData %d, recorded: %d, buffer length %d", _samplesCaptured, waveHdr->dwBytesRecorded,  waveHdr->dwBufferLength);

    _write++;
    _write %= BUFFERS_USED;

    _free--;

    //LogF("Fill buffer = buffer used: %d, buffers free: %d", waveHdr->dwUser, _free);
  }
}

bool WaveCapture::PrepareBuffers()
{
  MMRESULT mmRes = MMSYSERR_NOERROR;

  ZeroMemory(&_waveHdr, sizeof(WAVEHDR) * BUFFERS_USED);

  for (int i = 0; i < BUFFERS_USED; i++)
  { 
    // audio buffers
    _data[i].Resize(_length);
    _data[i].Realloc(_length);

    _waveHdr[i].lpData = (LPSTR)_data[i].Data();
    _waveHdr[i].dwBufferLength = _data[i].Size() * (_waveFmt.wBitsPerSample / 8);
    _waveHdr[i].dwUser = i; // WAVEHDR index
    _waveHdr[i].dwFlags = 0;

    // function prepares a buffer for waveform-audio input
    mmRes = waveInPrepareHeader(_hWaveIn, &_waveHdr[i], sizeof(WAVEHDR));

    if (mmRes != MMSYSERR_NOERROR)
    {
      break;
    }

    // sends an input buffer to the given waveform-audio input device, 
    // when the buffer is filled, the application is notified.
    mmRes = waveInAddBuffer(_hWaveIn, &_waveHdr[i], sizeof(WAVEHDR));

    if (mmRes != MMSYSERR_NOERROR)
    {
      break;
    }
  }

  if (mmRes == MMSYSERR_NOERROR) 
  {
    _deviceOpened = true;
  }
  else
  {
    _deviceOpened = false;
    UnPrepareBuffers();
  }

  _buffReady.Realloc(BUFFERS_USED);
  _buffReady.Resize(BUFFERS_USED);

  _write = 0;
  _free = BUFFERS_USED;

  return _deviceOpened;
}

bool WaveCapture::UnPrepareBuffers()
{
  MMRESULT mmRes = MMSYSERR_NOERROR;

  if (_hWaveIn && _deviceOpened)
  {
    // stops waveform-audio input
    mmRes = waveInStop(_hWaveIn);

    // stops input on the given waveform-audio input device and resets the current position to zero
    // all pending buffers are marked as done and returned to the application
    mmRes = waveInReset(_hWaveIn);

    for (int i = 0; i < BUFFERS_USED; i++)
    {
      // cleans up the preparation performed by the waveInPrepareHeader function
      mmRes = waveInUnprepareHeader(_hWaveIn, &_waveHdr[i], sizeof(WAVEHDR));
    }
  }

  return true;
}

unsigned WaveCapture::GetBuffer(int16 *ptr, unsigned length)
{
  Assert(ptr);
  Assert(length);

  if (_free == BUFFERS_USED) return 0;

  int firstBusy = _write + _free;
  if (firstBusy >= BUFFERS_USED) firstBusy -= BUFFERS_USED;

  // first queued buffer
  WAVEHDR *wHdr = _buffReady[firstBusy];

  Assert(wHdr);

  int samplesCaptured = wHdr->dwBytesRecorded / sizeof(int16);

  if (length >= (unsigned)samplesCaptured)
  {
    Assert(wHdr->dwUser < BUFFERS_USED);

    AutoArray<short> &aBuf = _data[wHdr->dwUser];

    int size = std::min((int)length, aBuf.Size());
    size = std::min(size, samplesCaptured);

    if (_firstBuffer)
    {
      _firstBuffer = false;
      // perform fade in (there is often some garbage in the first buffer)
      for (int i = 0; i < size; ++i)
      {
        float coef = i/(float)size;
        ptr[i] = aBuf[i] * coef * coef;
      }
    }
    else
    {
      // just copy
      for (int i = 0; i < size; ++i)
      {
      ptr[i] = aBuf[i];
    }
    }

    _free++;

    //FIX: backup it, as waveInAddBuffer changes it in Vista OS (due to this BUG the captureRoutine consumed 100% CPU in MP on Vista)
    //DWORD bytesRecorded = wHdr->dwBytesRecorded;

    // queue new buffer
    MMRESULT mmRes = waveInAddBuffer(_hWaveIn, wHdr, sizeof(WAVEHDR));

    if (mmRes != MMSYSERR_NOERROR)
    {
      // TODO:
      // wHdr->dwUser ... 
      // waveInUnprepareHeader(_hWaveIn, wHdr, sizeof(WAVEHDR));
    }

    _samplesCaptured -= samplesCaptured;

#if DEBUG_VON_SAVE_BUFFERS
    GDebugVoNSaveBuffers.GetRecordBuf().AddMessage(Format("WaveCapture - getting buffer %d, samples %d", wHdr->dwUser, samplesCaptured));
#endif

    return samplesCaptured;
  }

  return 0;
}

//////////////////////////////////////////////////////////////////////////

#if _USE_TIMER_QUEUE
VoNCaptureBufferXA2::VoNCaptureBufferXA2 ( HANDLE qtimer, VoNRecorder *rec )
#else
VoNCaptureBufferXA2::VoNCaptureBufferXA2 ( EventToCallback *etc, VoNRecorder *rec )
#endif
{
#ifdef NET_LOG_VOICE
  NetLog("VoNCaptureBufferXA2::VoNCaptureBufferXA2");
#endif
  input = true;
  suspended = true;
  m_ev = NULL;
  size = 0;
  // default sound format:
  format.bitsPerSample = 16;
  format.frequency     = 8000;
  format.granularity   = 1;
  format.alignment     = 1;

#if _USE_TIMER_QUEUE
  Assert(qtimer);
  m_timerQueue = qtimer;
#else
  Assert( etc );
  m_etc = etc;
#endif

  Assert( rec );
  m_rec = rec;
  m_ev = NULL;
  m_d = m_ahead = 0;
  m_nextPtr = 0;
}

VoNCaptureBufferXA2::~VoNCaptureBufferXA2 ()
{
  destroyBuffer();
}

void VoNCaptureBufferXA2::startCapture()
{
  if (!_wCapture.DeviceOpened()) return;
  ScopeLockSection lock(GSoundSysXA2->GetLock());
  _wCapture.Start();

#if DIAG_VON
  LogF("Capturing started");
#endif
}

void VoNCaptureBufferXA2::stopCapture()
{
  if (!_wCapture.DeviceOpened()) return;  
  ScopeLockSection lock(GSoundSysXA2->GetLock());
  _wCapture.Stop();

#if DIAG_VON
  LogF("Capturing stopped");
#endif
}

void VoNCaptureBufferXA2::registerCallbacks ()
{
  if (!_wCapture.DeviceOpened()) return;                  // nothing to do..
#if _USE_TIMER_QUEUE
  Assert(m_timerQueue);
#else
  Assert( m_etc );
#endif
  if ( m_ev ) unregisterCallbacks();
  stopCapture();

#if _USE_TIMER_QUEUE
  BOOL err = CreateTimerQueueTimer(&m_ev, m_timerQueue, CaptureTimerRoutineXA2, this, DS_TIMER, DS_TIMER, WT_EXECUTEINTIMERTHREAD);
  Assert(err);
  (void) err;
#else
  // create & register the timer:
  m_ev = CreateWaitableTimer(NULL,FALSE,NULL);
  Assert( m_ev );
  m_etc->addEvent(m_ev,captureRoutine,this);
  LARGE_INTEGER start = { (int64)0 };
  SetWaitableTimer(m_ev,&start,DS_TIMER,NULL,NULL,FALSE);
#endif
  if ( !suspended )
  {
    startCapture();
  }
}

void VoNCaptureBufferXA2::unregisterCallbacks ()
{
#if _USE_TIMER_QUEUE
  Assert (m_timerQueue);
#else
  Assert( m_etc );
#endif
  stopCapture();

  if ( m_ev )
  {
#if _USE_TIMER_QUEUE
    DeleteTimerQueueTimer(m_timerQueue, m_ev, INVALID_HANDLE_VALUE);
#else
    CancelWaitableTimer(m_ev);
    m_etc->removeEvent(m_ev);
    CloseHandle(m_ev);
#endif
    m_ev = NULL;
  }

  if ( !suspended )
  {
    startCapture();
  }
}

void VoNCaptureBufferXA2::suspendCallbacks ()
{
  Assert(_wCapture.DeviceOpened());
  stopCapture();
#if !_USE_TIMER_QUEUE
  Assert( m_etc );
  if ( m_ev ) m_etc->enableEvent(m_ev,false);
#endif
}

void VoNCaptureBufferXA2::resumeCallbacks ()
{
#if !_USE_TIMER_QUEUE
  Assert( m_etc );
  if ( m_ev ) m_etc->enableEvent(m_ev,true);
#endif
  Assert(_wCapture.DeviceOpened());
  startCapture();
}

void VoNCaptureBufferXA2::createBuffer ( unsigned len )
{
  size = m_d = m_ahead = 0;
  m_ev = NULL;
#if _ENABLE_MP
  // check if there is a support for capture - for this we need valid device, though
  //alcIsExtensionPresent(NULL,"ALC_EXT_CAPTURE");

  /*const char *defaultCapture = alcGetString(NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER);
  m_device = alcCaptureOpenDevice(defaultCapture, format.frequency, AL_FORMAT_MONO16, len);*/

  WAVEFORMATEX wave;
  wave.nChannels = 1;
  wave.wBitsPerSample = 16;
  wave.wFormatTag = WAVE_FORMAT_PCM;
  wave.nBlockAlign = wave.nChannels * wave.wBitsPerSample / 8;
  wave.nSamplesPerSec = format.frequency;
  wave.nAvgBytesPerSec = wave.nSamplesPerSec * wave.nBlockAlign;
  wave.cbSize = sizeof(WAVEFORMATEX);

#ifdef NET_LOG_VOICE
  NetLog("VoNCaptureBufferXA2::createBuffer(): len=%u, freq=%d, bps=%d",
  len,(int) wave.nSamplesPerSec,(int)wave.wBitsPerSample);
#endif

  // create input audio device
  _wCapture.OpenDevice(wave, len);

#endif

  if (_wCapture.DeviceOpened())
  {
#if DIAG_VON
    LogF("Created capture device");
#endif
    m_data.Realloc(len);
    m_data.Resize(len);

    // set local variables:
    size = len;
    m_d = (len + 2) >> 2;                   // D = 1/4 of buffer length (in samples)
    m_ahead = (5 * m_d) >> 2;               // 5/4 of D
    m_nextPtr = 0;
    m_dataUsed = 0;
    // assume our buffer is zero-initialized:
#ifdef NET_LOG_VOICE
    NetLog("VoNCaptureBufferXA2::createBuffer() succeeded");
#endif
  }
  else 
  {
    m_data.Clear();
#ifdef NET_LOG_VOICE
    NetLog("VoNCaptureBufferXA2::createBuffer() failed: 0x%x, 0x%x",(unsigned)resCap,(unsigned)resBuf);
#endif
  }
}

void VoNCaptureBufferXA2::suspendBuffer ()
{
  if (!_wCapture.DeviceOpened()) return;                  // nothing to do..
  suspendCallbacks();
  stopCapture();
  m_nextPtr = 0;
  suspended = true;
}

void VoNCaptureBufferXA2::resumeBuffer ()
{
  if (!_wCapture.DeviceOpened()) return;                  // nothing to do..
  resumeCallbacks();
  startCapture();
  suspended = false;
}

void VoNCaptureBufferXA2::destroyBuffer ()
{
  if (!_wCapture.DeviceOpened()) return;                  // nothing to do..
#ifdef NET_LOG_VOICE
  NetLog("VoNCaptureBufferXA2::destroyBuffer: _capture.count=%d, m_buffer.count=%d",m_capture.GetRefCount(),m_buffer.GetRefCount());
#endif
  stopCapture();
  unregisterCallbacks();

  ScopeLockSection lock(GSoundSysXA2->GetLock());

  size = m_d = m_ahead = 0;
  //alcCaptureCloseDevice(m_device);
  _wCapture.CloseDevice();
#if DIAG_VON
  Log("Closed capture device %p",_wCapture);
#endif
  m_data.Clear();
}

unsigned VoNCaptureBufferXA2::newData ( unsigned &pos, unsigned minSamples, unsigned maxSamples )
{
  Assert( m_rec );
  return m_rec->encode(this, pos, minSamples, maxSamples);
}

void VoNCaptureBufferXA2::advanceSafePosition ( unsigned inc )
{
  // we may delete inc from the data intended for encoding
  Assert((int)inc <= m_dataUsed);
  int size = m_data.Size();
  m_data.Delete(0,inc);
  m_dataUsed -= inc;
  // we want to resize the buffer to the original size again
  m_data.Resize(size);
}

void VoNCaptureBufferXA2::lockData ( unsigned pos, unsigned len, CircularBufferPointers &ptr )
{
  Zero(ptr);
  Assert((int)len <= m_data.Size());
  // return pointer into the temporary buffer
  ptr.ptr1.bps16 = m_data.Data();
  ptr.len1 = len;
}

void VoNCaptureBufferXA2::unlockData ( unsigned processed, CircularBufferPointers &ptr )
{
}

#include "Es/Memory/normalNew.hpp"

void* VoNCaptureBufferXA2::operator new ( size_t size )
{
  return safeNew(size);
}

void* VoNCaptureBufferXA2::operator new ( size_t size, const char *file, int line )
{
  return safeNew(size);
}

void VoNCaptureBufferXA2::operator delete ( void *mem )
{
  safeDelete(mem);
}

#include "Es/Memory/debugNew.hpp"

//----------------------------------------------------------------------
//  VoN-system:

VoNChannelId ExplicitMapTraits<VoNChannelId,RefD<VoNSoundBufferXA2> >::keyNull = VOID_CHANNEL;
VoNChannelId ExplicitMapTraits<VoNChannelId,RefD<VoNSoundBufferXA2> >::zombie  = RESERVED_CHANNEL;

VoNSystemXA2::VoNSystemXA2(IXAudio2 *device)
{
  _device = device;
#if _USE_TIMER_QUEUE
  m_timerQueue = CreateTimerQueue();
#else
  // create & init the ETC object:
  m_etc = new EventToCallback;
#endif
}

VoNSystemXA2::~VoNSystemXA2 ()
{
  stopClient();
#if _USE_TIMER_QUEUE
  DeleteTimerQueue(m_timerQueue);
#endif
}

void VoNSystemXA2::stopClient()
{
  _capture = NULL;
  _bufs.reset();
}

VoNResult VoNSystemXA2::setReplay (VoNChannelId chId, bool create)
// this method is called by VoNClient when the replayer is created/destroyed.
// Create: a sound-buffer has to be established and connected to the new replayer.
// Destroy: sound-buffer (including its notification mechanism) has to be destroyed.
{
#if !_USE_TIMER_QUEUE
  Assert( m_etc );
#endif
  Assert(m_client);
  RefD<VoNSoundBufferXA2> buf;

  if (create)                             // creating
  {
    if (IsDedicatedServer())
    {
      ErrF("VoNSystemXA2: cannot create replayer-object on dedicated server!");
      return VonError;
    }

    VoNReplayer *r = m_client->getReplayer(chId);
    if (!r) return VonError;              // unknown channel => ignore it
    if (_bufs.get(chId,buf))             // buffer exists => error
    {
      ErrF("VoNSystemXA2: Existing replayer buffer (%d)!",(int)chId);
      return VonError;
    }
    // new replayer buffer:
    Assert(_device);
#if _USE_TIMER_QUEUE
    buf = new VoNSoundBufferXA2(m_timerQueue,r,_device);
#else
    buf = new VoNSoundBufferXA2(m_etc,r,_device);
#endif
    _bufs.put(chId, buf);
    // prepare the buffer:
    buf->format.frequency = r->getFrequency();
    buf->format.bitsPerSample = outBps;
    buf->format.granularity = r->getCodec()->estFrameLen();
    buf->createBuffer( (unsigned)( REPLAYER_BUFFER_D * r->getFrequency() ) << 2 );
    buf->registerCallbacks();               // start notification mechanism for the new buffer
    buf->resumeBuffer();
#ifdef NET_LOG_VOICE
    NetLog("VoNSystemXA2: replayer object created for player=%d",chId);
#endif
  }
  else                                      // destroying
  {
    if (_bufs.get(chId,buf))             // buffer exists => destroy it
    {
      buf->destroyBuffer();
      _bufs.removeKey(chId);
    }
    else
    {
      ErrF("VoNSystemXA2: Unknown replayer buffer (%d)!",(int)chId);
      return VonError;
    }
#ifdef NET_LOG_VOICE
    NetLog("VoNSystemXA2: replayer object destroyed for player=%d",chId);
#endif
  }

  return VonOK;
}

VoNResult VoNSystemXA2::setSuspend (VoNChannelId chId, bool suspend )
// this method is called by VoNClient when the replayer is suspended/resumed.
// Suspend: stop the sound-buffer replay & notification mechanism.
// Resume: restart the sound-buffer replay & notification mechanism.
{
  RefD<VoNSoundBufferXA2> buf;
  if (!_bufs.get(chId, buf))
    return VonError;
#if defined(NET_LOG_VOICE) || DEBUG_VON
  NetLog("VoNSystemXA2: replayer object %s for player=%d",suspend?"suspended":"resumed",chId);
#endif

  if (suspend)                            // suspend
    buf->suspendBuffer();
  else                                      // resume
    buf->resumeBuffer();

  return VonOK;
}

static int GetVoiceFrequency(int quality, int &codecQuality)
{
  DoAssert(quality>0);
  if (quality<=10)
  {
    codecQuality = quality;
    return 8000;
  }
  if (quality<=20)
  {
    codecQuality = quality-10;
    return 16000;
  }
  codecQuality = quality-20;
  return 32000;
}


VoNResult VoNSystemXA2::setCapture ( bool on, int quality )
// if called for the 1st time:
// create VoNCaptureBufferXA2 & associate it with VoNRecorder
{
#if !_USE_TIMER_QUEUE
  Assert( m_etc );
#endif
  Assert( m_client );

  if ( !_capture )                         // capture buffer was not created yet
  {
    if ( IsDedicatedServer() )
    {
      ErrF("VoNSystemXA2: cannot create capture-object on dedicated server!");
      return VonError;
    }
    VoNRecorder *r = m_client->getRecorder();
    if ( !r )                               // voice was swithed on for the 1st time => create codec & recorder
    {
      CodecInfo info;
      Zero(info);
      // set default codec parameters:
#if 1
      strcpy(info.name,"Speex");
      info.type = ScVoice;
      int codecQuality=3;
      info.nominalFrequency = GetVoiceFrequency(quality,codecQuality);
#else
      strcpy(info.name,"LPC");
      info.type = ScVoice;
      strcpy(info.name,"PCM");
      info.type = ScUncompressed;
      strcpy(info.name,"DPCM");
      info.type = ScUniversal;
#endif
      VoNCodec *codec = findCodec(info);
      Assert( codec );
      codec->setVoiceMask(defaultVoiceMask);
      r = new VoNRecorder(m_client,m_client->outChannel,codec, GSoundsys?GSoundsys->GetVoNRecThreshold():VoNRecorder::DEFAULT_REC_THRESHOLD);
      r->SetCodecQuality(codecQuality);
      m_client->setRecorder(r);
    }
    Assert( r );
#if _USE_TIMER_QUEUE
    _capture = new VoNCaptureBufferXA2(m_timerQueue, r);
#else
    _capture = new VoNCaptureBufferXA2(m_etc,r);
#endif
    int freq = r->getFrequency();
    // prepare the buffer:
    _capture->format.frequency     = freq;
    _capture->format.bitsPerSample = inBps;
    _capture->createBuffer( (unsigned)( CAPTURE_BUFFER_D * freq ) << 2 );
    _capture->registerCallbacks();
#ifdef NET_LOG_VOICE
    NetLog("VoNSystemXA2: capture buffer was created: %u Hz, %u bps",_capture->format.frequency,_capture->format.bitsPerSample);
#endif
  }

  // capture on/off:
  if ( on )
    _capture->resumeBuffer();
  else
    _capture->suspendBuffer();

  return VonOK;
}

RefD<VoNSoundBuffer> VoNSystemXA2::getSoundBuffer ( VoNChannelId chId )
{
  RefD<VoNSoundBufferXA2> buf;
  _bufs.get(chId,buf);
  return buf.GetRef();
}

/// Set position of player (affects source)
void VoNSystemXA2::setPosition(int dpnid, Vector3Par pos, Vector3Par speed)
{
  //ScopeLockSection lock(GSoundSysXA2->GetLock()); //HOTFIX: possible crash when _source3D vanishes during call in VoN thread

  RefD<VoNSoundBufferXA2> buf;
  _bufs.get(dpnid, buf);
  if (buf) buf->Set3DPosition(pos, speed);
}

void VoNSystemXA2::WhatUnitsVoNDirect(AutoArray<int> &units, Vector3Par pos, float maxDist)
{
  IteratorState iterator;
  RefD<VoNSoundBufferXA2> buf;
  for (bool avail=_bufs.getFirst(iterator, buf); avail; avail=_bufs.getNext(iterator, buf))
  {
    const VoiceXA2StateEAX *state = buf->GetVoiceState();
    if (state && state->parsTime)
    { //position was set already
      float dist2 = state->position.Distance2(pos);
      if (dist2 <= Square(maxDist)) 
      {
        units.Add(buf->GetChannel());
      }
    }
  }
}

/// Set position of player (it affects source)
void VoNSystemXA2::setObstruction(int dpnid, float obstruction,float occlusion,float deltaT)
{
  RefD<VoNSoundBufferXA2> buf;
  _bufs.get(dpnid, buf);
  if (buf) buf->SetObstruction(obstruction,occlusion,deltaT);
}

/// Set volume and ear accomodation of player (it affects source)
void VoNSystemXA2::updateVolumeAndAccomodation(int dpnid, float volume,float accomodation)
{
  RefD<VoNSoundBufferXA2> buf;
  _bufs.get(dpnid, buf);
  if (buf) buf->UpdateVolumeAndAccomodation(volume, accomodation);
}

VoNChatChannel VoNSystemXA2::checkChatChannel(int dpnid, bool &audible2D, bool &audible3D)
{
  audible2D = audible2D = false;
  RefD<VoNSoundBufferXA2> buf;
  _bufs.get(dpnid, buf);
  if (buf)
  {
    return buf->CheckChatChannel(audible2D,audible3D);
  }
  return 0;
}

void VoNSystemXA2::get2D3DPlaying(int dpnid, bool &audible2D, bool &audible3D)
{ // used for CheatShowVoNBubbles
  audible2D = audible3D = false;
  RefD<VoNSoundBufferXA2> buf;
  _bufs.get(dpnid, buf);
  if (buf)
  {
    return buf->get2D3DPlaying(audible2D, audible3D);
  }
}

//////////////////////////////////////////////////////////////////////////
/// VoiceManager
/// Only some M most loud voices can get the source
//////////////////////////////////////////////////////////////////////////

#include "../textbank.hpp"
#include "../ui/chat.hpp"

struct SortVoice
{
  RefD<VoNSoundBufferXA2> _buf;
  RefD<PlayerVoice> _voice;
  float _loudness;
  bool  _unsetReceiver;

  SortVoice() : _unsetReceiver(false) {}
  explicit SortVoice(PlayerVoice* voice, VoNSoundBufferXA2 *buf, float loudness);
  float Loudness() const {return _loudness;}
};

TypeIsMovableZeroed(SortVoice);

// buffer has its 2D and 3D parts. SortVoice need to distinguish between these two.
SortVoice::SortVoice(PlayerVoice* voice, VoNSoundBufferXA2 *buf, float loudness)
:_voice(voice), _buf(buf), _loudness(loudness), _unsetReceiver(false)
{}

// Coefficients to get some stable measure to estimate "relative volume" of incomming voices
// Channel coefficients
const float VoiceCoefCCDirect  = 0.3f;
const float VoiceCoefCCGlobal = 0.3f;
const float VoiceCoefCCVehicle = 0.6f;
const float VoiceCoefCCItemTransmitter = 0.3f;
// Other coefficients
const float VoiceCoefIsPlaying = 0.25f;
const float MinVoN2DDistance = 0.5f;

float VoNSoundBufferXA2::GetLoudness(int voice2D3D)
{
  float volume = 0;
  if ( _playerVoice3D || (voice2D3D&TTChan2D) ) //3D position was already set OR it is 2D voice
  {
    extern ChatChannel VoNChatChannel2ChatChannel(int channel);
    ChatChannel bufChan = VoNChatChannel2ChatChannel(_repl->GetChatChannel());

    switch (bufChan)
    {
    case CCGlobal: volume += VoiceCoefCCGlobal; break;
    case CCSystem: volume += VoiceCoefCCGlobal; break;
    case CCVehicle: volume += VoiceCoefCCVehicle; break;
    case CCDirect: volume += VoiceCoefCCDirect; break;
    case CCItemTransmitter: volume += VoiceCoefCCItemTransmitter; break;
    }
    if ( voice2D3D&TTChan2D )
    {
      // add some bonus for voices currently playing (in order to add some Hysteresis)
      if (_playerVoice2D->Playing()) volume += VoiceCoefIsPlaying;
      if (!_repl->GetPlay2D()) return 0; // only 3D can be heard
      if (bufChan == CCItemTransmitter) return 0; // only 3D can be heard (or from the radio, which is played different way)
      if (_playerVoice3D)
      {
        float distance = GetListenerPos().Distance(_playerVoice3D->voiceState.position);
        if (distance < MinVoN2DDistance) distance=MinVoN2DDistance;
        float distVol = 2/distance;
        volume += distVol;
      }
      // combine with the radio volume set in Audio options
      float vonVolume = (static_cast<SoundSystemXA2 *>(GSoundsys))->_volumeAdjustVoN;
      volume *= vonVolume;
#if DEBUG_VON_SORTING
      LogF("2DVoice: distVol=%.2f finalVolume=%.2f", distVol, volume);
#endif
    }
    else if (_playerVoice3D)
    {
      //TODO: is DirectChannel louder than Global? take into account it should be...
      // add some bonus for voices currently playing (in order to add some Hysteresis)
      if (!_repl->GetPlay3D()) return 0; // only 2D can be heard
      if (_playerVoice3D->Playing()) volume += VoiceCoefIsPlaying;

      if (bufChan!=CCDirect) volume *= 0.5f; // direct sound going with 2D speaking is 1/2 attenuated
      float volumeAdjust = (static_cast<SoundSystemXA2 *>(GSoundsys))->_volumeAdjustEffect;
      float volumeAdjustAll = volumeAdjust;

      float obsVolume = 1-(1-_playerVoice3D->voiceState.obstruction)*0.7f;
      // assume occlusion reduces the sound much more than obstruction
      float occVolume = 1-(1-_playerVoice3D->voiceState.occlusion)*0.9f;

      float volumeFactor = floatMin(obsVolume,occVolume);
      volumeAdjust *= volumeFactor;
      // calculate volume based on distance
      float minDistance = _playerVoice3D->CalcMinDistance();
      float distance = GetListenerPos().Distance(_playerVoice3D->Position());
      float atten = distance<minDistance ? 1 : DistanceRolloff(minDistance,distance);
      float vol = atten*volumeAdjust;
      volume += vol;
      // combine with the effects volume set in Audio options
      volume *= volumeAdjustAll;
      //RptF("3DVoice: vol=%.2f finalVolume=%.2f (dist=%.1f)", vol, volume, distance);
#if DEBUG_VON_SORTING
      LogF("3DVoice: vol=%.2f finalVolume=%.2f", vol, volume);
#endif
    }
    else return 0;
  }
#if DEBUG_VON_SORTING
  if ( voice2D3D&TTChan2D ) LogF("    2DVoice %.2f", volume);
  else                      LogF("    3DVoice %.2f", volume);
#endif

  return volume;
}

static int CompareVoices( const SortVoice *v0, const SortVoice *v1 )
{
  float diff=v0->_loudness-v1->_loudness;
  if( diff>0 ) return -1;
  if( diff<0 ) return +1;
  return 0;
}

float VoNSoundBufferXA2::PlayingVolumeCoef()
{
  if (_repl->getPlaying() <= 0) //no speech
  {
    DWORD time = ::GetTickCount();
    if (time>_lastPlayingTime)
    {
      return Interpolativ((time-_lastPlayingTime)/1000.0f, 0,2,1,0);
    }
    else _lastPlayingTime = ::GetTickCount(); //probably not set yet
  }
  else //reset the timestamp
  {
    _lastPlayingTime = ::GetTickCount();
  }
  return 1;
}

void VoNSoundBufferXA2::get2D3DPlaying(bool &audible2D, bool &audible3D)
{ // used for CheatShowVoNBubbles only
  audible2D = audible3D = false;
  if (_sourceVoiceOpen) {
    enterSoundBuffer();
    RefArray<PlayerVoice, MemAllocSafe>  &playVoices = _playVoices[_playVoicesIX];
    for (int i=0; i<playVoices.Size(); i++)
      if (playVoices[i]->Is2D()) audible2D=true;
      else audible3D=true;
    leaveSoundBuffer();
  }
}

float GetTransmitterLoudness(PlayerVoice3D *voice, Vector3Par listenerPos)
{
  float volume = 0;
  if (voice->Playing()) volume = VoiceCoefIsPlaying;

  float volumeAdjust = (static_cast<SoundSystemXA2 *>(GSoundsys))->_volumeAdjustEffect;
  float volumeAdjustAll = volumeAdjust;

  float obsVolume = 1-(1-voice->voiceState.obstruction)*0.7f;
  // assume occlusion reduces the sound much more than obstruction
  float occVolume = 1-(1-voice->voiceState.occlusion)*0.9f;

  float volumeFactor = floatMin(obsVolume,occVolume);
  volumeAdjust *= volumeFactor;
  // calculate volume based on distance
  float minDistance = voice->CalcMinDistance();
  float distance = listenerPos.Distance(voice->Position());
  float atten = distance<minDistance ? 1 : DistanceRolloff(minDistance,distance);
  float vol = atten*volumeAdjust;
  volume += vol;
  // combine with the effects volume set in Audio options
  volume *= volumeAdjustAll;

  return volume;
}


#if _ENABLE_CHEATS
char GVoNSoundDiag[4096];
#endif
void VoNSystemXA2::advanceAll(float deltaT, bool paused, bool quiet)
{
  //Voice Manager implementation
  DWORD time = ::GetTickCount();
  int diff = time-m_listenerPosSetTime;
  if (diff<0 || diff>2000) // voices cannot be updated when listener position is not being set
  {
    m_listenerPos = Vector3(-100000.0f,-100000.0f,-100000.0f); //far far away, but no QNANs
  }
  AUTO_STATIC_ARRAY(SortVoice,sort,128);
  // sort voices by intensity (as heard by the listener)
  IteratorState iterator;
  RefD<VoNSoundBufferXA2> buf;
  const float LimitMuteMinimal = 0.001f;

  for (bool avail=_bufs.getFirst(iterator, buf); avail; avail=_bufs.getNext(iterator, buf))
  {
    buf->clearVoices(); // clear the _playVoices[ix] which are to be set in this frame
    buf->SetListenerPos(m_listenerPos);
    float coef = buf->PlayingVolumeCoef();  //when buffer is not playing, it interpolates gradualy to zero
    float loudness2D = coef * buf->GetLoudness(TTChan2D);
    SortVoice sv2D(buf->_playerVoice2D, buf, loudness2D);
    float loudness3D = coef * buf->GetLoudness(TTChan3D);
    SortVoice sv3D(buf->_playerVoice3D, buf, loudness3D);

#define MUTE_3D_WHEN_BOTH_PRESENT 1
#ifdef MUTE_3D_WHEN_BOTH_PRESENT
    // now we would prefer 2D voice over 3D if both are present (mute 3D)
    if (sv2D.Loudness() > LimitMuteMinimal && sv3D.Loudness() > LimitMuteMinimal)
    {
      //take maximum of loudness for this voice
      if (sv3D.Loudness() > sv2D.Loudness()) sv2D._loudness = sv3D.Loudness();
      sv3D._loudness = 0.0f; // mute 3D
    }
#endif
    //voices can be added even when muted
    sort.Add(sv2D);
    sort.Add(sv3D);

    // check for possible ItemTransmitter sounds (if buffer is transmitting on some freq nearby radio is tuned to)
    extern ChatChannel VoNChatChannel2ChatChannel(int channel);
    ChatChannel bufChan = VoNChatChannel2ChatChannel(buf->_repl->GetChatChannel());
    if ( bufChan==CCItemTransmitter )
    {
      VoNResult res = buf->_repl->result();
      if (res!=VonSilence && res!=VonDropout) //do not play when no sound data are available
      {
        // get frequency
        float freq = buf->_repl->m_transmitterFreq;
        // get all nearby transmitters processing on the given frequency
        bool ForEachNearbyTransmitterFreqv(const std::function<bool(TransmitterVoice *voice, float f)> &func);
        float maxRange = m_maxTransmitterRange2;
        ForEachNearbyTransmitterFreqv([this, &sort, buf, freq, maxRange](TransmitterVoice *voice, float f) -> bool
        {
          const float limit = 0.1f;
          if ( fabs(freq-f)<limit )
          {
            if ( buf->_playerVoice3D->Position().Distance2(voice->Position()) < maxRange )
            {
              SortVoice sv(voice, buf, GetTransmitterLoudness(voice, m_listenerPos));
              sv._unsetReceiver = true;
              sort.Add(sv);
              //RptF("Playing on %.1f 2D=%.2f 3D=%.2f", freq, sv.Loudness(), sv.Loudness() );
            }
          }
          return false; // continue
        });
      }
    }
  }
  QSort(sort.Data(),sort.Size(),CompareVoices);

#if DEBUG_VON_SORTING
  LogF("... QSort voices: %d", sort.Size());
#endif
  // There can be limited number of XA2 sources for VoN sounds
  int played=sort.Size();
  if (played>Glob.config.maxVoNSounds) played=Glob.config.maxVoNSounds;
  // set _playing for all voices acording to whether should be muted or not
  for( int i=0; i<sort.Size(); i++ )
  {
    SortVoice &voice=sort[i];
    if (voice._voice) voice._voice->_playing = (i<played);
  }

#if _ENABLE_CHEATS
  *GVoNSoundDiag = 0; //terminate
  static DWORD lastTimeStamp = GetTickCount();
  DWORD curTimeStamp = GetTickCount();
  bool updateDiag = false;
  bool firstEntry = true;
  if (lastTimeStamp + 500 < curTimeStamp)
  {
    lastTimeStamp = curTimeStamp;
    updateDiag = true;
  }
#endif

  // Update quality of each CCItemTransmitter
  // Note: more players can speak on the same frequency => more buffers are sharing the same PlayerVoice3D in the list of SortVoices
  for( int i=0; i<played; i++ )
  {
    if ( !sort[i]._buf->_playerVoice3D || !sort[i]._unsetReceiver ) continue;
    // compute the quality of this particular PlayerVoice
    TransmitterVoice *receiversVoice = static_cast<TransmitterVoice *>(sort[i]._voice.GetRef());
    receiversVoice->_quality = 0; //reset
    for ( int j=i; j<played; j++ )
    {
      sort[j]._unsetReceiver = false; //mark as processed
      if (sort[j]._voice.GetRef() == receiversVoice)
      {
        Vector3 transmitterPos = sort[j]._buf->_playerVoice3D->Position();
        float quality = receiversVoice->voiceState.position.Distance2(transmitterPos) / m_maxTransmitterRange2; // range [0,1]
        saturateMax(receiversVoice->_quality, quality);  //the worst quality rules them all
      }
    }
    // final APO parameters are set inside TransmitterItem simulation
  }

  // Play all voices that have been chosen
  for( int i=0; i<played; i++ )
  {
    SortVoice &voice=sort[i];
    if (voice.Loudness()>=LimitMuteMinimal)
    {
      // if the voice was inactive, XA2 source will be assigned to it
      voice._buf->setStartVoice(voice._voice); 

#if _ENABLE_CHEATS

      if (updateDiag)
      {
        char newDiag[1024];
        const VoiceXA2StateEAX *state = voice._buf->GetVoiceState();
        if (state)
        {
          sprintf(newDiag, "%sVoN %x (%s) - (%.1f m), dB %.1f, obs %.1f/%.1f dB", 
            (firstEntry ? "" : "\n"),
            voice._buf->GetChatChannel(),
            (voice._voice->Is2D() ? "2D" : "3D"),
            -1 /*voice._voice->GetListenerPos().Distance(state->position)*/, //TODO
            log10(voice._loudness)*20,
            log10(state->obstruction)*20, log10(state->occlusion)*20
            );
          strcat(GVoNSoundDiag, newDiag);
        }

        firstEntry = false;
      }
#endif
    }
    else
    {
      if (voice._voice) voice._voice->_playing = false;
    }
  }

  // make the just added voices available to replayerRoutine
  for (bool avail=_bufs.getFirst(iterator, buf); avail; avail=_bufs.getNext(iterator, buf))
  {
    buf->switchVoices();
  }
}

#endif
