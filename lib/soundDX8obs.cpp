// River Raid - sound module
// (C) 1996 - 2002, SUMA
#ifdef _WIN32

#include "wpch.hpp"
#include "keyInput.hpp"
#include "global.hpp"
#include "textbank.hpp"
#include "engine.hpp"
#include "soundDX8.hpp"
#include "Network/network.hpp"
#include "paramFileExt.hpp"

#include "dikCodes.h"
#include <El/QStream/qbStream.hpp>
#include <El/Common/perfLog.hpp>
#include <El/Common/perfProf.hpp>
#include <El/FreeOnDemand/memFreeReq.hpp>
#include <El/Breakpoint/breakpoint.h>
#include "diagModes.hpp"
#include <El/FileServer/fileServer.hpp>

#ifdef _WIN32
#if !defined _XBOX
#include <dxerr9.h>
#include <mmsystem.h>
#pragma comment(lib,"OpenAL32.lib")
#else
#include "XboxDSP/xboxSfx.h"
#endif
#endif

#if defined _XBOX && _PROFILE
#define _USE_XACT_PROFILE 1
#endif

#if _USE_XACT_PROFILE
#include <xact.h>
#endif

#define DIAG_VOL 0
#define DIAG_STREAM 0
#define DIAG_STREAM_FILL 0 // 0...50

#if defined _WIN32 && !_DISABLE_GUI && !defined _XBOX
#pragma comment(lib,"dxerr9")
#pragma comment(lib,"eaxguid")
// CoCreateInstance used for dsound object creation - dll not needed
//#pragma comment(lib,"dsound")
#endif

#if defined _WIN32 && !defined _XBOX
#define GetPos2Flags DSBCAPS_GETCURRENTPOSITION2
#define LocHWFlags DSBCAPS_LOCHARDWARE
#define LocSWFlags DSBCAPS_LOCSOFTWARE
#else
#define GetPos2Flags 0
#define LocHWFlags 0
#define LocSWFlags 0
#endif

/*
// implement IStream interface
class BStream: public RefCount, public IStream, private QIFStreamB
{
  public:
  // IUnknown interface
  ULONG STDMETHODCALLTYPE AddRef() {return RefCount::AddRef();}
  ULONG STDMETHODCALLTYPE Release() {return RefCount::Release();}
  HRESULT STDMETHODCALLTYPE QueryInterface( REFIID iid, void ** ppvObject );

  // ISequentialStream 
  HRESULT STDMETHODCALLTYPE Read
  (
    void * pv, ULONG cb, ULONG * pcbRead
  );
  HRESULT STDMETHODCALLTYPE Write
  (
    void const* pv, ULONG cb, ULONG * pcbWritten
  );
  
  // IStream 
  HRESULT STDMETHODCALLTYPE Seek
  (
    LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER * plibNewPosition
  );
  HRESULT STDMETHODCALLTYPE SetSize
  (
    ULARGE_INTEGER libNewSize                           
  );
  HRESULT STDMETHODCALLTYPE CopyTo
  (
    IStream * pstm, ULARGE_INTEGER cb,
    ULARGE_INTEGER * pcbRead, ULARGE_INTEGER * pcbWritten
  );
  HRESULT STDMETHODCALLTYPE Commit
  (
    DWORD grfCommitFlags 
  );
  HRESULT STDMETHODCALLTYPE Revert();
  HRESULT STDMETHODCALLTYPE LockRegion
  (
    ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType  
  );
  HRESULT STDMETHODCALLTYPE UnlockRegion
  (
    ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType    
  );
  HRESULT STDMETHODCALLTYPE Stat
  (
    STATSTG * pstatstg, DWORD grfStatFlag   
  );
  HRESULT STDMETHODCALLTYPE Clone( IStream ** ppstm );

  // construction / destruction
  BStream();
  BStream(const char *name);
  BStream(const BStream &src);
  ~BStream();
  HRESULT Initialize( const char *name );
};

HRESULT BStream::QueryInterface( REFIID iid, void ** ppvObject )
{
  if (iid==IID_IUnknown)
  {
    AddRef();
    *ppvObject = (IUnknown *)this;
    return S_OK;
  }
  if (iid==IID_IStream)
  {
    AddRef();
    *ppvObject = (IStream *)this;
    return S_OK;
  }
  *ppvObject = NULL;
  return E_NOINTERFACE;
}

HRESULT BStream::Read
(
  void * pv, ULONG cb, ULONG * pcbRead
)
{
  //LogF("BStream %x Read %d from %d",this,cb,_readFrom);
  int toRead = rest();
  if (cb<=toRead)
  {
    read(pv,cb);
    if (pcbRead) *pcbRead = cb;
    return S_OK;
  }
  read(pv,toRead);
  if (pcbRead) *pcbRead = toRead;
  return S_FALSE;
}

HRESULT BStream::Write
(
  void const* pv, ULONG cb, ULONG * pcbWritten
)
{
  if (pcbWritten) *pcbWritten = 0;
  return STG_E_ACCESSDENIED;
}

HRESULT BStream::Seek
(
  LARGE_INTEGER dlibMove, DWORD dwOrigin, ULARGE_INTEGER * plibNewPosition
)
{
  //int oldPos = tellg();
  // calculate new position
  int newPos = 0;
  switch (dwOrigin)
  {
    case STREAM_SEEK_SET: newPos = dlibMove.QuadPart; break;
    case STREAM_SEEK_CUR: newPos = _readFrom+dlibMove.QuadPart; break;
    case STREAM_SEEK_END: newPos = _len+dlibMove.QuadPart; break;
    default: return STG_E_INVALIDFUNCTION;
  }
  HRESULT ret = S_OK;
  if (newPos<0)
  {
    ret = STG_E_INVALIDPOINTER;
    newPos = 0;
  }
  else if( newPos>_len)
  {
    ret = STG_E_INVALIDPOINTER;
    newPos = _len;
  }
  seekg(newPos,QIOS::beg);
  if (plibNewPosition) plibNewPosition->QuadPart = tellg();
  //LogF
  //(
  //  "BStream %x Seek %d:%d -> %d",
  //  this,dwOrigin,(int)dlibMove.QuadPart,tellg()
  //);
  return ret;
}

HRESULT BStream::SetSize
(
  ULARGE_INTEGER libNewSize                           
)
{
  return STG_E_ACCESSDENIED;
}

HRESULT BStream::CopyTo
(
  IStream * pstm, ULARGE_INTEGER cb,
  ULARGE_INTEGER * pcbRead, ULARGE_INTEGER * pcbWritten
)
{
  // TODO: implement if necessary
  return STG_E_ACCESSDENIED;
}

HRESULT BStream::Commit
(
  DWORD grfCommitFlags 
)
{
  return S_OK;
}

HRESULT BStream::Revert()
{
  return STG_E_ACCESSDENIED;
}


HRESULT BStream::LockRegion
(
  ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType  
)
{
  return STG_E_INVALIDFUNCTION;
}

HRESULT BStream::UnlockRegion
(
  ULARGE_INTEGER libOffset, ULARGE_INTEGER cb, DWORD dwLockType    
)
{
  return STG_E_INVALIDFUNCTION;
}
HRESULT BStream::Stat
(
  STATSTG * pstatstg, DWORD grfStatFlag   
)
{
  Fail("BStream::Stat");
  return STG_E_ACCESSDENIED;
}

HRESULT BStream::Clone( IStream ** ppstm )
{
  BStream *clone = new BStream(*this);
  clone->AddRef();
  *ppstm = clone;
  return S_OK;
}


BStream::BStream()
{
  //LogF(">>> Construct BStream at %x",this);
}

BStream::BStream(const char *name)
{
  //LogF(">>> Construct BStream at %x - %s",this,name);
  AutoOpen(name);
}

BStream::BStream(const BStream &src)
:QIFStreamB(src)
{
  //LogF(">>> Cloned    BStream at %x from %x",this,&src);
}

BStream::~BStream()
{
  //LogF("<<< Destruct BStream at %x",this);
}

HRESULT BStream::Initialize( const char *name )
{
  AutoOpen(name);
  if (fail()) return S_FALSE;
  return S_OK;
}

*/

// mixer - volume control

#if defined _WIN32 && !defined _XBOX
class AudioMixer
{
  bool _open;
  HMIXER _mixer;

  UINT _waveId;
  DWORD _oldVolume;

  public:
  AudioMixer();
  ~AudioMixer();

  void SetWaveVolume( float vol );
  float GetWaveVolume() const;
};

AudioMixer::AudioMixer()
{
  _open = false;
  MMRESULT mr = mixerOpen
  (
    &_mixer,0,NULL,NULL,MIXER_OBJECTF_MIXER
  );
  if (mr!=MMSYSERR_NOERROR)
  {
    return;
  }
  _open = true;
  waveOutGetVolume((HWAVEOUT)WAVE_MAPPER,&_oldVolume);

}

AudioMixer::~AudioMixer()
{
  if (_open)
  {
    waveOutSetVolume((HWAVEOUT)WAVE_MAPPER,_oldVolume);
    mixerClose(_mixer);
  }
}

float AudioMixer::GetWaveVolume() const
{
  // note: 
  DWORD dVol;
  waveOutGetVolume((HWAVEOUT)WAVE_MAPPER,&dVol);
  DWORD lVol = (dVol&0xffff);
  DWORD rVol = (dVol>>16)&0xffff;
  int avgVol = (rVol+lVol)/2;
  return avgVol*(1.0/0xffff);
}
void AudioMixer::SetWaveVolume( float vol )
{
  int iVol = toInt(0xffff*vol);
  saturate(iVol,0,0xffff);
  DWORD dVol=(DWORD)iVol|(((DWORD)iVol)<<16);
  waveOutSetVolume((HWAVEOUT)WAVE_MAPPER,dVol);
  //LogF("Set volume %x",dVol);
}
#endif


#define DO_PERF 0
#if DO_PERF
#include "perfLog.hpp"
#endif

//#include "engine.hpp"


void Wave8::DoConstruct()
{
  _type=Free;
  _soundSys=NULL;
  _header._size=0;         // Size of data.
  _header._frequency=0;      // sampling rate
  _header._nChannel=0;        // number of channels
  _header._sSize=0;           // number of bytes per sample
  _playing=false;       // Is this one playing?
  _wantPlaying = false;
  _wantUnload = false;
  _loaded=false;
  _loadedHeader = false;
  _loadError=false;
  _only2DEnabled=false;
  _looping=true;
  _terminated=false;

  _volume=1.0;
  _accomodation=1.0;
  _volumeAdjust = 1;
  _enableAccomodation=true;

  _stopTreshold = FLT_MAX; // never stop

  _obstructionSet = _occlusionSet = 0;
  _obstruction = _occlusion = 1;
  _volumeSet=0;
  _freqSet=1;
  _curPosition=0;
  _skipped = 0;
  _position=VZero;
  _velocity=VZero;
}

/*
WaveGlobal8::WaveGlobal8()
{
  _queued=false;
}
*/

WaveGlobal8::~WaveGlobal8()
{
}

#define DIAG_LOAD 0

WaveGlobal8::WaveGlobal8( SoundSystem8 *sSys, float delay )
:Wave8(sSys,delay)
{
  _queued=false;
}

WaveGlobal8::WaveGlobal8( SoundSystem8 *sSys, RString name, bool is3D )
:Wave8(sSys,name,is3D)
{
  _queued=false;
}

DEFINE_FAST_ALLOCATOR(WaveGlobal8)

/*
WaveGlobal8::WaveGlobal8( const Wave8 &wave )
:Wave8(wave)
{
  _queued=false;
}
*/

void WaveGlobal8::AdvanceQueue()
{
  if( !_queue ) return;
  if( _terminated )
  {
    if( _queue )
    {
      _queue->_queued=false;
      _queue->DoPlay();
      _queued=true;
      //LogF("Sound %s activated",(const char *)_queue->Name());
    }
    OnTerminateOnce();
  }
  // even before terminated we could preload and schedule the following sample
  else if (_playing && _queue->PreparePlaying())
  {
    // check how much is left from current sample to play
    // if it is less than a frame, start immediately
    #if 0
      static int maxOverlap = 50;
      static int overlapCoef = 8;
    #else
      const int maxOverlap = 50;
      const int overlapCoef = 8;
    #endif
    int overlap = (GEngine->GetLastFrameDuration()*overlapCoef)>>4;
    if (overlap>maxOverlap) overlap = maxOverlap;
    
    // note: maths will not be accurate when playing 3D due to Doppler effect

    // estimate when current sample will stop
    DWORD durationMs = toInt((_header._size/_header._sSize)*1000/(_header._frequency*_freqSet));
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
    
    
    #if 0 //def _XBOX
      // exact scheduling can help on Xbox
      // once the wave is ready, we can schedule it for playback
      //_queued->DoPlay()
      REFERENCE_TIME timestamp;
      _soundSys->DirectSound()->GetTime(&timestamp);
      
    #endif
  }
}

void WaveGlobal8::Play()
{
  if( _queued ) return;

  // check termination
  IsTerminated();

  AdvanceQueue();
  
  base::Play();
}

void WaveGlobal8::Skip( float deltaT )
{
  if( _queued ) return;

  // check termination
  IsTerminated();

  AdvanceQueue();

  base::Skip(deltaT);
}

void WaveGlobal8::Advance( float deltaT )
{
  if( _playing || _wantPlaying && GetTimePosition()>=0) return;
  Skip(deltaT);
}

void WaveGlobal8::PreloadQueue()
{
  if (_queue)
  {
    _queue->PreparePlaying();
  }
}

void WaveGlobal8::Queue( AbstractWave *wave,int repeat )
{
  WaveGlobal8 *after=static_cast<WaveGlobal8 *>(wave);
  //Assert( after->_repeat>0 );
  Assert( repeat>0 );
  while( after->_queue ) after=after->_queue;
  after->_queue=this;
  /*
  LogF
  (
    "Queue %s after %s",
    (const char *)Name(),(const char *)after->Name()
  );
  */
  _queued=true;
  Repeat(repeat);
}

bool WaveGlobal8::IsTerminated()
{
  if( !base::IsTerminated() ) return false;
  for( WaveGlobal8 *next = this; next; next=next->_queue )
  {
    if( !next->_terminated ) return false;
  }
  OnTerminateOnce();
  return true;
}

#define DEFERRED 1

static bool DSVerifyF( const char *file, int line, HRESULT err )
{
  if( err==DS_OK ) return true;
  const char *message="";
  switch( err )
  {
    #ifndef _XBOX
    case DSERR_BUFFERLOST:
      message="Buffer lost";
    break;
    case DSERR_PRIOLEVELNEEDED:
      message="Prior level needed";
    break;
    case DSERR_INVALIDPARAM:
      message="Invalid Param";
    break;
    #endif
    case DSERR_INVALIDCALL:
      message="Invalid Call";
    break;
    case DSERR_CONTROLUNAVAIL:
      message="Control Unavailable";
    break;
    case DSERR_GENERIC:
      message="Generic";
    break;
    default:
      message="Unknown";
    break;
  }
  //LogF("%s(%d) : DirectSound error %s",file,line,message);
  //Fail("Direct sound error");
  return false;
}

#define DoDSVerify(x) DSVerifyF(__FILE__,__LINE__,x)
#define DoDSAssert(x) DSVerifyF(__FILE__,__LINE__,x)

#if !_RELEASE
  #define DSVerify(x) DoDSVerify(x)
  #define DSAssert(x) DoDSAssert(x)
#else
  #define DSVerify(x) (x)
  #define DSAssert(x) (x)
#endif

WaveBuffers8::WaveBuffers8()
:_position(VZero),_velocity(VZero)
{
  _3DEnabled = true;
  _distance2D = 1;
}

WaveBuffers8::~WaveBuffers8()
{
}

void WaveBuffers8::Reset( SoundSystem8 *sys )
{
  // TODO: force setting all parameters
  _volumeSet = 0;

  _position = VZero;
  _velocity = VZero;

  _volume = 1; // volume used for 3D sound
  _accomodation = 1; // current ear accomodation setting
  _obstructionSet = _occlusionSet = 0;
  _obstruction = _occlusion = 1;
  _volumeAdjust = sys->_volumeAdjustEffect;

  _kind = WaveEffect;
  _3DEnabled = true;
  _parsTime = 0; // force parameter reset
}

static inline float DistanceRolloff(float minDistance, float distance)
{
  return minDistance/distance;
}

void WaveBuffers8::DoSetI3DL2Db(bool immediate)
{
  #ifdef _XBOX
  Assert(!_only2DEnabled);
  DSI3DL2BUFFER i3dl2;
  i3dl2.lDirect = 0;
  i3dl2.lDirectHF = 0;
  i3dl2.lRoom = 0;
  i3dl2.lRoomHF = 0;
  i3dl2.flRoomRolloffFactor = 0;
  i3dl2.Obstruction.lHFLevel = _obstructionSet;
  i3dl2.Obstruction.flLFRatio = 0.1f;
  i3dl2.Occlusion.lHFLevel = _occlusionSet;
  i3dl2.Occlusion.flLFRatio = 0.1f;
  _buffer->SetI3DL2Source(&i3dl2,immediate ? DS3D_IMMEDIATE : DS3D_DEFERRED);
  #endif
}

void WaveBuffers8::SetI3DL2Db(int obstruction, int occlusion, bool imm)
{
  if (!_playing)
  {
    _obstructionSet = obstruction;
    _occlusionSet = occlusion;
    return;
  }
  if (_buffer)
  {
    _obstructionSet = obstruction;
    _occlusionSet = occlusion;
    DoSetI3DL2Db(imm);
  }
}

#if _ENABLE_PERFLOG
  #define WAV_COUNTER(name,value) \
    ADD_COUNTER(name,value); \
    static_cast<SoundSystem8 *>(GSoundsys)->_##name++;
  #define SND_COUNTER(name,value) \
    ADD_COUNTER(name,value); \
    _##name++;
#else
  #define WAV_COUNTER(name,value)
  #define SND_COUNTER(name,value)
#endif

void WaveBuffers8::SetVolumeDb( LONG volumeDb )
{
  if( !_playing || !_buffer )
  {
    #if DIAG_VOL
    //LogF("PresetVolume %s %d",(const char *)Name(),volumeDb);
    LogF("PresetVolume %x %d",this,volumeDb);
    #endif
    _volumeSet=volumeDb;
    return;
  }
  // input is 1/100 dB
  static int minDbDiff = 50;
  if( abs(volumeDb-_volumeSet)>minDbDiff )
  {
    WAV_COUNTER(wavSV,1);
    DSVerify( _buffer->SetVolume(volumeDb) );

    #if DIAG_VOL
    //LogF("PP: SetVolume %s %d",(const char *)Name(),volumeDb);
    LogF("PP: SetVolume %x %d",this,volumeDb);
    #endif
    _volumeSet=volumeDb;
  }
}

void Wave8::DoUnload()
{
  if( !_loaded ) return;
  // if _wantUnload should be reset before Unload is called
  Assert(!_wantUnload);

  PROFILE_SCOPE_EX(wavUL,sound);
  
  Assert( GSoundsys==_soundSys );
  Assert( _soundSys->_soundReady );

  #if 1
    if (_buffer)
    {
      // TODO: we may store also streaming buffer
      // so that thay can be reused
      if (!_stream)
      {
        _soundSys->StoreToCache(this);
      }
    }
  #endif

  #if DO_PERF
  if (_buffer)
  {
    ADD_COUNTER(wavUL,1);
  }
  #endif

  #if defined _XBOX && _ENABLE_REPORT
    // if the sound is cached, releasing it should cause no problems
    int count = _buffer->AddRef();_buffer->Release();
    if (count==1)
    {
      // we have the last reference
      DWORD stat = 0;
      _buffer->GetStatus(&stat);
      if (stat&DSBSTATUS_PLAYING)
      {
        LogF("Wave %s released while playing",(const char *)Name());
      }
    }
  #endif

  #ifndef _XBOX
    _buffer3D.Free();
  #endif
  _buffer.Free();
  _loaded=false;
  _stream.Free();

  _counters[_type]--;
  _type=Free;
  ReportStats();

  //LogF("%s unloaded",(const char *)Name());
}


void Wave8::DoDestruct()
{
  PROFILE_SCOPE_EX(wavDs,sound);
  // check if we are still playing
  CheckPlaying();
  // playing buffer should never be terminated, as this can be blocking (Xbox)
  //Assert(!_playing || _terminated);
  //DoStop();
  // unload must destoy the wave
  Unload(true);
  DoAssert(!_wantUnload);
  //DoUnload();
  // note: if Free is called immediatelly after Stop, it may be blocking
}


int Wave8::_counters[NTypes];
int Wave8::_countPlaying[NTypes];

void Wave8::AddStats( const char *name )
{
  if (!_buffer) return;
  #ifndef _XBOX
  DSBCAPS caps;
  caps.dwSize=sizeof(caps);
  _buffer->GetCaps(&caps);
  if( caps.dwFlags&(DSBCAPS_LOCHARDWARE|DSBCAPS_LOCDEFER) )
  {
    _type= _only2DEnabled ? Hw2D : Hw3D;
    //LogF("%s %s: Hw",name,(const char *)Name());
  }
  else
  {
    _type=Sw;
    //LogF("%s %s: Sw",name,(const char *)Name());
  }
  #else
    _type= _only2DEnabled ? Hw2D : Hw3D;
  #endif

  _counters[_type]++;
  ReportStats();
}

void Wave8::ReportStats()
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

int Wave8::TotalAllocated()
{
  int total=0;
  for( int i=1; i<NTypes; i++ ) total+=_counters[i];
  return total;
}

void Wave8::ConvertSkippedToSamples()
{
  float skipSamples = _skipped*_header._frequency;
  if (skipSamples*_header._sSize>_header._size)
  {
    if (_looping)
    {
      int sampleSize = _header._size/_header._sSize;
      float newPos = fastFmod(skipSamples,sampleSize);
      _curPosition += toInt(newPos)*_header._sSize;
    }
    else
    {
      _curPosition = _header._size;
      // if the whole sound is already skipped, we want to terminate the sound
      _terminated=true;
    }
  }
  else
  {
    _curPosition += toInt(_skipped*_header._frequency)*_header._sSize;
  }
  _skipped = 0;
}

bool Wave8::LoadHeader()
{
  if( _loadedHeader || _header._sSize>0 ) return true;
  
  /// empty sound (pause) should never be present here
  Assert(Name().GetLength()>0);

  _soundSys->_waves.RemoveNulls();
    
  // before requesting the file we should check if we are able to get the header from existing sounds
  // try to load properties from some existing sound buffer
  for( int i=0; i<_soundSys->_waves.Size(); i++ )
  {
    Wave8 *wave=_soundSys->_waves[i];
    if (!wave->_loadedHeader) continue;
    if (wave==this) continue;
    if (!strcmp(wave->Name(),Name()))
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
  
  #if DIAG_LOAD>=2
    LogF("Wave8 %s: Load file header",(const char *)Name());
  #endif
  Ref<WaveStream> stream;

  // only header is needed - whole files are cached
  DoAssert(RString::IsLowCase(Name()));
  if (!SoundRequestFile(Name(),stream))
  {
    //Log("Headers not loaded - %s",(const char *)Name());
    return false;
  }
  //Log("Headers loaded - %s",(const char *)Name());

  _loadedHeader = true;
  
  if( !stream )
  {
    RptF("Cannot load sound '%s'", (const char *)Name());
    _loadError = true;
    return true;
  }
  
  WAVEFORMATEX format;
  stream->GetFormat(format);
  _header._frequency = format.nSamplesPerSec;      // sampling rate
  _header._nChannel = format.nChannels;        // number of channels
  _header._sSize = format.nBlockAlign;           // number of bytes per sample
  _header._size = stream->GetUncompressedSize();
  
  ConvertSkippedToSamples();
  
  #if DIAG_STREAM
    if (IsStreaming(stream))
    {
      LogF("%s: Open stream, size %d)",(const char *)Name(),_size);
    }
  #endif
  return true;
}

#define DISABLE_HW_ACCEL 0
#define ENABLE_VOICEMAN 1

const int StreamingAtomPart = 16*1024;

int Wave8::StreamingBufferSize(const WAVEFORMATEX &format, int totalSize)
{
  int bytesPerSecond = format.nAvgBytesPerSec;
  // buffer should be 4 seconds long
  int keepUnder = 512*1024;
  if (bytesPerSecond>keepUnder) return bytesPerSecond;
  int minSize = bytesPerSecond*4;
  if (minSize>totalSize) minSize = totalSize;
  if (minSize>keepUnder) return keepUnder;
  int size=StreamingAtomPart*4;
  while (size<minSize)
  {
    size *=2;
  }
  return size;
}
int Wave8::StreamingMaxInOneFill(const WAVEFORMATEX &format, int bufferSize)
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

int Wave8::StreamingMaxInInitFill(const WAVEFORMATEX &format, int bufferSize)
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

int Wave8::StreamingPartSize(const WAVEFORMATEX &format)
{
  return StreamingAtomPart;
}

void Wave8::GetBufferDesc(DSBUFFERDESC &dsbd, WAVEFORMATEX &format, bool streaming) const
{
  memset(&dsbd, 0, sizeof(dsbd));

  Assert(_loadedHeader);
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = _header._nChannel;
  format.nSamplesPerSec = _header._frequency;
  format.nAvgBytesPerSec = _header._frequency*_header._sSize;
  format.nBlockAlign = _header._sSize;
  format.wBitsPerSample = _header._sSize*8/_header._nChannel;
  format.cbSize = 0;

  dsbd.dwSize = sizeof(dsbd);
  dsbd.dwFlags = DSBCAPS_CTRLFREQUENCY|DSBCAPS_CTRLVOLUME;
  dsbd.dwFlags |= GetPos2Flags;

  if (_soundSys->_hwEnabled)
  {
    #if !ENABLE_VOICEMAN
    dsbd.dwFlags |= LocHWFlags;
    #else
    dsbd.dwFlags |= DSBCAPS_LOCDEFER;
    #endif
  }
  else
  {
    dsbd.dwFlags |= LocSWFlags;
  }
  if( !_only2DEnabled )
  {
    dsbd.dwFlags|=DSBCAPS_CTRL3D|DSBCAPS_MUTE3DATMAXDISTANCE;
    #ifndef _XBOX
    dsbd.guid3DAlgorithm=DS3DALG_DEFAULT; // surround/quadro if available
    #endif
  }

  if (streaming)
  {
    dsbd.dwBufferBytes = StreamingBufferSize(format,_header._size);
  }
  else
  {
    #ifdef _XBOX
      dsbd.dwBufferBytes = 0;
    #else
      dsbd.dwBufferBytes = _header._size;
    #endif
  }
  dsbd.lpwfxFormat = &format;
}

bool Wave8::Load()
{
  PROFILE_SCOPE_EX(wavLo,sound);
  Assert(!_wantUnload);

  if( _loadError ) return true;
  if( _loaded ) return true;
  if (!_loadedHeader)
  {
    if (!LoadHeader())
    {
      return false;
    }
  }
  if (Name().GetLength()<=0)
  {
    _header._frequency = 1000; // time given in ms
  }

  // scan all channels
  // if possible make some channels free (release cache)
  {
    _soundSys->ReserveCache(1-_only2DEnabled,_only2DEnabled);

    // search all playing waves
    _soundSys->_waves.RemoveNulls();
  }
  {
  // first try to reuse any cached data
  if( _soundSys->LoadFromCache(this) )
  {
    #if DIAG_LOAD>=2
    LogF("Cache reused %s",(const char *)Name());
    #endif
    AddStats("Cache");

    _loaded=true;
    _terminated=false;
    _playing=false;
    _wantPlaying=false;
    _wantUnload = false;
    _streamPos=0;
    _startTime = 0;
    _stopTime = 0xffffffff;
    
    _lastLoopSet = false;
    _lastLoopVerified = false;
    return true;
  }
  }

  if (_soundSys->_ds)
  {
    // try to duplicate some existing sound buffer
    for( int i=0; i<_soundSys->_waves.Size(); i++ )
    {
      Wave8 *wave=_soundSys->_waves[i];
      if( !wave->Loaded() ) continue;
      if( wave==this ) continue;
      if( !strcmp(wave->Name(),Name()) )
      {
        if( Duplicate(*wave) )
        {
          return true;
        }
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
    
    if (stream)
    {
      isStreaming = IsStreaming(stream);
      if (!isStreaming)
      {
        // if not streaming, we need to read the whole file before proceeding
        bool dataReady = stream->RequestData(FileRequestPriority(10),0,stream->GetUncompressedSize());
        if (!dataReady)
        {
          return false;
        }
      }
    }
  }
  //Log("Data loaded - %s",(const char *)Name());
  
  //LogF("Wave8 %s loaded",(const char *)Name());
  DoDestruct();

  _loadError=true;
  _loaded=true;
  _playing=false;
  _wantPlaying=false;
  _wantUnload = false;
  _startTime = 0;
  _stopTime = 0xffffffff;
  
  if( !stream )
  {
    RptF("Cannot load sound '%s'", (const char *)Name());
    return true;
  }
  
  // this should be already done?
  WAVEFORMATEX format;
  stream->GetFormat(format);
  _header._frequency = format.nSamplesPerSec;      // sampling rate
  _header._nChannel = format.nChannels;        // number of channels
  _header._sSize = format.nBlockAlign;           // number of bytes per sample
  _header._size = stream->GetUncompressedSize();

  // Set up the direct sound buffer. 
  DSBUFFERDESC dsbd;
  GetBufferDesc(dsbd,format,isStreaming);

  IMySound *ds = _soundSys->DirectSound();

  {
    PROFILE_SCOPE_EX(wavCB,sound);
    if (ds)
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
      #ifdef _XBOX
      int memSize = isStreaming ? dsbd.dwBufferBytes : 4*1024;
      // avoid failure of CreateSoundBuffer if possible
      FreeOnDemandGarbageCollectSystemMemoryLowLevel(memSize+32*1024);
      #else
      int memSize = dsbd.dwBufferBytes;
      #endif
      IDirectSoundBuffer *temp = NULL;
      for(;;)
      {
        ADD_COUNTER(dsCSB,1);
        HRESULT hr;
        {
          PROFILE_SCOPE_EX(dsCSB,sound);
          hr = ds->CreateSoundBuffer(&dsbd,&temp,NULL);
        }
        if (SUCCEEDED(hr)) break;
        if (hr==E_OUTOFMEMORY)
        {
          PROFILE_SCOPE_EX(dsFOD,sound);
          if (FreeOnDemandSystemMemoryLowLevel(memSize)) continue;
        }
        RptF("Cannot create sound buffer %s: %x", (const char *)Name(),hr);
        _loadError = true;
        return true;
      }
      #ifndef _XBOX
      HRESULT hr = temp->QueryInterface(IID_IDirectSoundBuffer8, (void **)_buffer.Init());
      if (FAILED(hr))
      {
        RptF("Cannot create sound buffer %s: %x", (const char *)Name(),hr);
        temp->Release();
        _loadError = true;
        return true;
      }
      #else
      *_buffer.Init() = temp;
      #endif
    }
  }
  #if DIAG_LOAD
    LogF("Wave8 %s: CreateSoundBuffer",(const char *)Name());
  #endif

  #ifndef _XBOX
  if( ds &&!_only2DEnabled )
  {
    if( _buffer->QueryInterface(IID_IDirectSound3DBuffer8,(void **)_buffer3D.Init()) )
    {
      RptF("Cannot create 3D sound buffer %s", (const char *)Name());
      _loadError = true;
      _buffer.Free();
      return true;
    }
  }
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
      _lastLoopSet = false;
      _lastLoopVerified = false;
      //LogF("Reset stream %s",(const char *)Name());
    }
    else
    {
      PROFILE_SCOPE_EX(wavGD,sound);
      #ifndef _XBOX
        if (ds)
        {
          BYTE *pbData;
          // Ok, lock the sucker down, and copy the memory to it.
          DWORD dwLength=0;
          // caution: following line caused VisualC++ debugger to hang
          // do not step through it - use breakpoint instead
          //PROFILE_SCOPE_EX(wavD1,sound);
          if( _buffer->Lock(0,_header._size,(void **)&pbData,&dwLength,NULL,0,0L)!=DS_OK )
          {
            RptF("Cannot lock sound buffer");
          }
          else
          {
            PROFILE_SCOPE_EX(wavGC,sound);
            int read = stream->GetDataClipped(pbData,0,dwLength);
            if (read<(int)dwLength)
            {
              LogF("GetData returned %d instead of %d",read,dwLength-read);
              memset(pbData+read,0,dwLength);
            }
            // Ok, now unlock the buffer, we don't need it anymore.
            if( _buffer->Unlock(pbData,dwLength,NULL,0)!=DS_OK )
            {
              RptF("Cannot unlock sound buffer");
            }
          }
        }
      #else
        // no need to copy data - we may use the memory we have
        {
          PROFILE_SCOPE_EX(wavNB,sound);
          _data = new BufferData(_header._size);
        }
        {
          PROFILE_SCOPE_EX(wavGC,sound);
          int read = stream->GetDataClipped(_data->Data(),0,_data->Size());
          if (read<(int)_header._size)
          {
            LogF("GetData returned %d instead of %d",read,_data->Size());
            memset(_data->Data()+read,0,_data->Size()-read);
          }
        }

        {
          PROFILE_SCOPE_EX(wavBD,sound);
          HRESULT hr = _buffer->SetBufferData(_data->Data(),_data->Size());
          if (FAILED(hr))
          {
            _data.Free();
            _buffer.Free();
            _loadError = true;
            return true;
          }
        }
      #endif
    }
  }
  //_soundSys->LogChannels("Load");
  AddStats("Load");

  // silent sound

  _loadError=false;
  return true;
}

bool Wave8::IsStreaming(WaveStream *stream)
{
  //WAVEFORMATEX format;
  //stream->GetFormat(format);
  //int totalSize = stream->GetUncompressedSize();
  return stream->StreamingWanted(_looping);
}

inline int StreamBufferRoundDistance( int a, int b, int size )
{
  int d = a-b;
  if (d<0) d += size;
  return d;
}

/*!
\patch 1.43 Date 1/31/2002 by Ondra
- Fixed: Support for looping streaming sounds.
\patch 1.85 Date 9/12/2002 by Ondra
- Fixed: Streaming sounds might loop infinitelly on some hardware.
*/


void Wave8::FillStream()
{
  // get write and play cursors
  if (!_buffer) return;
  DWORD play,write;
  _buffer->GetCurrentPosition(&play,&write);
  
#if DIAG_STREAM_FILL>40
  LogF(
    "%s: %d, pos %d,%d (%d, est Play %d)",
    (const char *)Name(),GlobalTickCount(),play,write,
    GlobalTickCount()-_startTime,(GlobalTickCount()-_startTime)*_header._frequency/1000*_header._sSize
  );
#endif // 0
  // time per sample in ms 1000/_header._frequency

  //write = play;

  // we may write anywhere between write and play, not between play and write

  // check what has been actually played

  WAVEFORMATEX format;
  _stream->GetFormat(format);
  int bufferSize = StreamingBufferSize(format,_header._size);
  int partSize = StreamingPartSize(format);
  int maxOneFill = StreamingMaxInOneFill(format,bufferSize);
  
  int toWrite = StreamBufferRoundDistance(play,_writePos,bufferSize);
  // wait until we may write enough data
  if (toWrite<partSize)
  {
#if DIAG_STREAM_FILL>40
    LogF("%s: FillStream - No space (%d,%d)",cc_cast(Name()),toWrite,partSize);
#endif // 0
    return;
  }
  #if _ENABLE_REPORT
  int initSize;
  InitPreloadStreamSize(initSize);
  const int starveLimit = intMax(bufferSize-(initSize-2*partSize),bufferSize*7/8);
  if (toWrite>starveLimit)
  {
    LogF(
      "%s: FillStream starving, %d of %d (%d)",
      cc_cast(Name()),toWrite,bufferSize,starveLimit
    );
    // log what can be causing the starving
    //GFileServer->LogWaitingRequests();
  }
  #endif

  int writeToPlayDist = StreamBufferRoundDistance(write,play,bufferSize);
  int maxWrite = bufferSize - writeToPlayDist;
  if (maxWrite>maxOneFill)
  {
    maxWrite = maxOneFill;
  }

  /*
  LogF
  (
    "Sound write: write %d, play %d, toWrite %d, writeToPlayDist %d (max %d,%d)",
    write,play,toWrite,writeToPlayDist, maxWrite, StreamingBufferSize
  );
  */
  
  if (toWrite>maxWrite)
  {
    // I think this should not be possible
    if (toWrite>bufferSize - writeToPlayDist)
    {
      LogF
      (
        "Sound overflow?: write %d, play %d, toWrite %d, writeToPlayDist %d (max %d,%d)",
        write,play,toWrite,writeToPlayDist, bufferSize - writeToPlayDist, bufferSize
      );
    }
    toWrite = maxWrite;
  }

  // StreamingPartSize must be power of two
  // round to nearest smaller multiply of StreamingPartSize
  toWrite &= ~(partSize-1);
  if (toWrite<partSize)
  {
#if DIAG_STREAM_FILL>40
    LogF("%s: FillStream - No space (%d,%d)",cc_cast(Name()),toWrite,partSize);
#endif // 0
    return;
  }

  // note: we might request data a bit earlier
  // and we could prioritize it by the time left
  // check if data are ready
  // when continuing stream, use highest priority, as any drop will be audible
  bool ready = false;
  if (_looping)
  {
    ready = _stream->RequestDataLooped(FileRequestPriority(1),_streamPos,toWrite);
  }
  else
  {
    ready = _stream->RequestDataClipped(FileRequestPriority(1),_streamPos,toWrite);
  }
  
  if (!ready)
  {
#if DIAG_STREAM_FILL>10
    LogF("%s: FillStream - No data, free: %d of %d",cc_cast(Name()),toWrite,bufferSize);
#endif // 0
    return;
  }
  
  void *data,*data2;
  DWORD size=0,size2=0;

  #if DIAG_STREAM
    LogF(
      "Filling stream %d..%d, play pos %d",
      _writePos,_writePos+toWrite,play
    );
  #endif
  HRESULT hr = _buffer->Lock(_writePos,toWrite,&data,&size,&data2,&size2,0);
  if (SUCCEEDED(hr))
  {
    //LogF("Streaming data %d:%d to %d",_streamPos,size,_writePos);

    int actualWrite = _writePos;
    #if DIAG_STREAM
    LogF("%s: Load stream %d:%d (of %d)",(const char *)Name(),_streamPos,size,_header._size);
    #endif
    if (_looping)
    {
      actualWrite += _stream->GetDataLooped(data,_streamPos,size);
    }
    else
    {
      actualWrite += _stream->GetDataClipped(data,_streamPos,size);
    }
    _streamPos += size;
    _writePos += size;
    if (size2>0)
    {
      #if DIAG_STREAM
      LogF("%s: Load stream %d:%d",(const char *)Name(),_streamPos,size2);
      #endif
      if (_looping)
      {
        actualWrite += _stream->GetDataLooped(data2,_streamPos,size2);
      }
      else
      {
        actualWrite += _stream->GetDataClipped(data2,_streamPos,size2);
      }
      _streamPos += size2;
      _writePos += size2;

      //LogF("Streaming data2 %d",size);
    }

    if ((int)_writePos>=bufferSize) _writePos -= bufferSize;
    if (actualWrite>=bufferSize) actualWrite -= bufferSize;


    _buffer->Unlock(data,size,data2,size2);

#if DIAG_STREAM_FILL>0
    LogF("%s: FillStream %d (%d)",cc_cast(Name()),_writePos,toWrite);
#endif // 0

    if (_streamPos>=(int)_header._size)
    {
      if (!_looping)
      {
        if (!_lastLoopSet)
        {
          _lastLoopSet = true;
          // record position where last actual data were written
          _endPos = actualWrite;
        }
        else if (!_lastLoopVerified)
        {
          // wait until playback gets far enough from _endPos
          // so that we are able to check when _endPos is actually reached
          // to be sure we can 
          if (StreamBufferRoundDistance(play,_endPos,bufferSize)>bufferSize/2)
          {
            _lastLoopVerified = true;
          }
        }
        else
        {
          // if we are close after the _endPos, it means playback
          // has finished last round and may be safely terminated
          if (StreamBufferRoundDistance(play,_endPos,bufferSize)<bufferSize/4)
          {
            // end of buffer reached - stop
            WAV_COUNTER(wavSt,1);
            _buffer->Stop();
          }
        }
      }
      else
      {
        _streamPos -= _header._size;
      }
    }
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

#ifdef _XBOX
  // no volume fix required on Xbox
  #define MaxVolFix(x) false
#else
  // volume fix is required on PC with HW acceleration
  #define MaxVolFix(x) (x)
#endif

bool Wave8::PreparePlaying()
{
  if (_playing || _terminated) return true;
  // check files needed
  bool loaded = Load();
  if (!loaded) return false;

  if (_stream)
  {
    int dummy;
    int preloadSize = InitPreloadStreamSize(dummy);
    return _stream->RequestData(FileRequestPriority(10),0,preloadSize);
  }
  
  return true;
}

void Wave8::Play()
{
  // set flag we want to play
  // commit will play it after listener position is set
  if( _terminated ) return;
  if(_wantUnload) return; // if it is scheduled for unloading, do not play it any more
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

bool Wave8::DoPlay()
{
  Assert(!_wantUnload);
  if( _terminated )
  {
    return true;
  }

  if (!_soundSys->DirectSound())
  {
    return true;
  }

  if( _playing )
  {
    // if it is streaming, fill with data as necessary
    if (_stream) FillStream();
    return true;
  }

  if (!_posValid && !_only2DEnabled)
  {
    // position not set yet - cannot start playing
    return false;
  }
  if( GetTimePosition()>=0 )
  {
    //LogF("Wave %s, pos %d",(const char *)Name(),_curPosition);
    bool loaded = Load();

    //PROFILE_SCOPE_EX(wavPl,sound);
    if (!loaded) return false;
    if( _curPosition>=(int)_header._size )
    {
      if( _looping && _header._size>0 )
      { 
        while( _curPosition>=(int)_header._size ) _curPosition-=_header._size;
      }
      else
      {
        _terminated=true;
        _curPosition=_header._size;
      }
    }
    else
    {
      {
        //PROFILE_SCOPE_EX(wavSt,sound);
        #if DIAG_VOL
        LogF("SP: SetVolume %s %d",(const char *)Name(),_volumeSet);
        #endif
        //LogF("Start play %s, vol %d",(const char *)Name(),_volumeSet);
        if (_buffer)
        {
          DWORD status;
          _buffer->GetStatus(&status);
          // if the buffer is still playing, we cannot use it yet
          if( status&DSBSTATUS_PLAYING ) return false;
          /**/
          WAV_COUNTER(wavSV,2);
          DSVerify( _buffer->SetVolume(_volumeSet) );
          if (!_only2DEnabled)
          {
            if (InterestedIn(Name()))
            {
              LogF("%s: OO %d,%d",cc_cast(Name()),_obstructionSet,_occlusionSet);
            }
            DoSetI3DL2Db(true);
          }
          DSVerify( _buffer->SetFrequency(CalcFrequencyDword()) );
          /**/
        }
        
        //LogF("SetPos %s",(const char *)Name());
        DoSetPosition(true,MaxVolFix(_soundSys->_hwEnabled));
      }

      if (_buffer)
      {
        _startTime = GlobalTickCount();
        _stopTime = 0xffffffff;
        // consider how much of the buffer has already been played back

        if (_curPosition>0)
        {
          DWORD alreadyPlayed = toInt((_curPosition/_header._sSize)*1000/(_header._frequency*_freqSet));
          _startTime -= alreadyPlayed;
        }

        if (!_stream)
        {
          //PROFILE_SCOPE_EX(wavPP,sound);
          #if 0
          if (_curPosition>0)
          {
            LogF("%s: set curpos to %d",(const char *)Name(),_curPosition);
          }
          #endif
          WAV_COUNTER(wavPl,1);
          DSVerify( _buffer->SetCurrentPosition(_curPosition) );
          if (_looping) DSVerify( _buffer->Play(0,0,DSBPLAY_LOOPING) );
          else DSVerify( _buffer->Play(0,0,0) );
        }
        else
        {
          //PROFILE_SCOPE_EX(wavPS,sound);
          // start play-back
          // perform initial fill on the stream

          _streamPos = _curPosition;
          //LogF("Set stream %s start to %d",(const char *)Name(),_streamPos);
          if (!FillStreamInit())
          {
            return false;
          }
          WAV_COUNTER(wavPl,1);
          DSVerify( _buffer->SetCurrentPosition(0) );
          DSVerify( _buffer->Play(0,0,DSBPLAY_LOOPING) );
        }
        _countPlaying[_type]++;
      }
      if (_only2DEnabled)
      {
//        LogF(
//          "%d: Wave started %s, buffer %p, freq %.2f, start %d, end %d",
//          GlobalTickCount(),(const char *)Name(),_buffer.GetRef(),_freqSet,
//          _startTime,_startTime+toInt((_header._size/_header._sSize)*1000/(_header._frequency*_freqSet))
//        );
      }
    }
    _playing=true;
    STAT_PLAY(this);
    OnPlayOnce();
  }
  return true;
}

void Wave8::Repeat(int repeat)
{
  if (repeat<2)
  {
    _looping=false;
  }
  else
  {
    _looping = true;
    if( _playing )
    {
      if (_buffer)
      {
        WAV_COUNTER(wavPl,1);
        DSVerify( _buffer->Play(0,0,DSBPLAY_LOOPING) );
      }
    }
  }
}
void Wave8::Restart()
{
  bool wasPlaying = _playing;
  
  Stop();
  _terminated=false;
  if (_playing)
  {
    LogF("%x: %s: Still playing",this,(const char *)Name());
  }
  //_playing=false;
  //STAT_STOP(this); // restarting - reset stats
  _curPosition=0;
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

/*!
\patch 1.01 Date 15/6/2001 by Ondra.
 - Fixed: playMusic [name,t] with t>96
*/

void Wave8::Skip( float deltaT )
{
  // note: pre-start delay is used to implement speed of sound
  // this gives bad results if listener speed is high
  if (!_loadedHeader)
  {
    _skipped += deltaT;
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
  int skip=toLargeInt(deltaT*_header._frequency)*_header._sSize;
  AdvanceCurrentPosition(skip);
  while( _curPosition>=(int)_header._size )
  {
    if (!_looping || _header._size<=0)
    {
      Stop();
      _terminated=true;
      _curPosition=_header._size;
      break;
    }
    _curPosition-=_header._size;
  }
  if( _playing && !_terminated )
  {
    WAV_COUNTER(wavSV,1);
    DSVerify( _buffer->SetCurrentPosition(_curPosition) );
    if (_stream)
    {
      LogF("Cannot advance playing stream %s",(const char *)Name());
    }
  }
}

void Wave8::Advance( float deltaT )
{
  if( !_playing ) Skip(deltaT);
}

float Wave8::GetTimePosition() const
{
  if (!_loadedHeader) return _skipped;
  float denom = _header._sSize*_header._frequency;
  if (denom<=0) return _skipped;
  return _skipped + float(_curPosition)/(_header._sSize*_header._frequency);
}


bool Wave8::IsStopped() {return !_playing && !_wantPlaying;}
bool Wave8::IsWaiting() {return GetTimePosition()<0;}

RString Wave8::GetDiagText()
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
    if( !_buffer ) return ret+RString("No buffer");
    // TODO: more diags
    if( _only2DEnabled )
    {
      char buf[512];
      sprintf
      (
        buf,
        "Playing 2D\n"
        "    vol %d (%.2f, adj %.2f) kind %d",
        _volumeSet/100,_volume,_volumeAdjust,_kind
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
        "    vol %d - (%.3f)",
        _position[0],_position[1],_position[2],
        _velocity[0],_velocity[1],_velocity[2],
        _volumeSet/100,_volume
      );
      return ret+RString(buf);
    }
  }
}

float Wave8::GetLength() const
{
  // get wave length in sec
  Assert(_loadedHeader);
  if (!_loadedHeader)
  {
    if (!unconst_cast(this)->LoadHeader())
    {
      LogF("Waiting for header of %s",(const char *)Name());
      do {} while(!unconst_cast(this)->LoadHeader());
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

bool Wave8::IsTerminated()
{
  if (!_terminated)
  {
    if (_playing)
    {
      if (_buffer)
      {
        DWORD status;
        DSVerify( _buffer->GetStatus(&status) );
        if( status&DSBSTATUS_PLAYING ) return false;
        DWORD temp;
        DSVerify( _buffer->GetCurrentPosition(&temp,NULL) );
        _curPosition=temp;
        Assert(_skipped==0);
      }
      STAT_TERM(this);
      _terminated=true;
      _stopTime = GlobalTickCount();
    }
    else if (!_loadedHeader && _skipped>5.0f)
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

bool Wave8::CanBeDeleted()
{
  if (!IsTerminated())
  {
    return false;
  }
  if (!_wantUnload)
  {
    Unload(false);
  }
  return !_buffer;
}

int Wave8::InitPreloadStreamSize(int &initSize) const
{
  WAVEFORMATEX format;
  _stream->GetFormat(format);
  int bufferSize = StreamingBufferSize(format,_header._size);
  int partSize = StreamingPartSize(format);
  int maxOneFill = StreamingMaxInInitFill(format,bufferSize);

  int totalSize = _stream->GetUncompressedSize();
  initSize = bufferSize-2*partSize;
  if (initSize>maxOneFill)
  {
    initSize = maxOneFill;
  }
  
  int preloadSize = initSize*4;
  if (preloadSize>totalSize) preloadSize = totalSize;
  if (initSize>totalSize) initSize = totalSize;
  return preloadSize;
}

bool Wave8::FillStreamInit()
{
  // fill buffer with start of streaming data
  DWORD size=0;
  void *data;

  int initSize;
  WAVEFORMATEX format;
  _stream->GetFormat(format);
  int bufferSize = StreamingBufferSize(format,_header._size);
  int preloadSize = InitPreloadStreamSize(initSize);
  
  // on first frame request more data than necessary
  // to make sure next frames will be able to fill, to prevent starving
  _writePos = 0;
  if (!_stream->RequestData(FileRequestPriority(10),_streamPos,preloadSize))
  {
#if DIAG_STREAM_FILL>10
    LogF("%s: FillStreamInit - No data, free %d",cc_cast(Name()),bufferSize);
#endif // 0
    return false;
  }
  #if DIAG_STREAM_FILL>20
    LogF("%s: done %d,%d",cc_cast(Name()),_streamPos,preloadSize);
    
  #endif
  if( _buffer->Lock(0,initSize,&data,&size,NULL,0,0L)!=DS_OK )
  {
    RptF("Cannot lock sound buffer");
  }
  else
  {
    //LogF("Init fill from %d (%d)",_streamPos,size);

    #if DIAG_STREAM
    LogF
    (
      "%s: Init stream %d:%d of %d",
      (const char *)Name(),_streamPos,size,bufferSize
    );
    #endif

#if DIAG_STREAM_FILL>0
    LogF("%s: FillStreamInit %d (%d)",cc_cast(Name()),_streamPos, size);
#endif // 0

    DoAssert((int)size<=preloadSize);
    // there should be no difference between looped and non-looped
    Assert((int)size<=_stream->GetUncompressedSize());
    if (_looping)
    {
      _streamPos += _stream->GetDataLooped(data,_streamPos,size);
    }
    else
    {
      _streamPos += _stream->GetDataClipped(data,_streamPos,size);
    }

    _writePos = size;
    if ((int)_writePos>=bufferSize) _writePos -= bufferSize;

    if( _buffer->Unlock(data,size,NULL,0)!=DS_OK )
    {
      RptF("Cannot unlock sound buffer");
    }
  }
  return true;
}

void Wave8::StoreCurrentPosition()
{
  // store current position so that is is available when playing the buffer
  if (!_buffer) return;

  DWORD temp;
  DSVerify( _buffer->GetCurrentPosition(&temp,NULL) );
  _curPosition=temp;
  Assert(_skipped==0);
  // for streaming audio store all neccessary information
  if (_stream)
  {
    WAVEFORMATEX format;
    _stream->GetFormat(format);
    int bufferSize = StreamingBufferSize(format,_header._size);
    // check how much data is written ahead of the current playing pos position
    int leftInBuffer = StreamBufferRoundDistance(_writePos,_curPosition,bufferSize);
    _streamPos -= leftInBuffer;
    _curPosition = _streamPos;
    _streamPos = 0;
    
    if (!_lastLoopVerified)
    {
      _lastLoopSet = false;
    }

    //LogF("Stored stream position %d (%d ahead, %d)",_curPosition,leftInBuffer,leftInBuffer2);
  }
}

/*!
\patch 1.27 Date 10/11/2001 by Ondra
- Fixed: speed of sound delay caused some sounds to be decayed (since 1.26).
*/
void Wave8::AdvanceCurrentPosition(int skip)
{
  Assert(_skipped==0);
  if (!_stream)
  {
    int oldPos = _curPosition;
    _curPosition += skip;
    if( _curPosition<0 )
    {
      Stop();
    }
    else
    {
      
      if (oldPos<0) _curPosition = 0;
    }
  }
  else
  {
    //LogF("Advance stream %s",(const char *)Name());
    if (_playing)
    {
      LogF("Cannot advance in playing stream %s",(const char *)Name());
    }
    // note: only one of _streamPos and _curPosition should be non-zero
    Assert (_streamPos==0 || _curPosition==0);
    int newPos = _streamPos + _curPosition + skip;
    if (newPos<0)
    {
      Stop();
      _curPosition = newPos;
      //LogF("Hard rewind %s to %d",(const char *)Name(),newPos);
    }
    else
    {
      _curPosition = newPos;
      //LogF("Soft rewind %s to %d",(const char *)Name(),newPos);
    }
  }
}


bool Wave8::DoStop()
{
  _wantPlaying = false;
  if( !_loaded )
  {
    if (_playing && !_loadError)
    {
      LogF("%x,%s: Playing, not loaded",this,(const char *)Name());
    }
    return true;
  }
  if( !_playing )
  {
    if (_buffer)
    {
      DWORD time = GlobalTickCount();
      if (_stopTime<time)
      {
        DWORD stopped = time-_stopTime;
        const DWORD unloadAfterStop = 2000; // in ms
        if (stopped>unloadAfterStop)
        {
          UnloadStopped(false);
        }
      }
    }
    return false;
  }
  Assert(!_wantUnload);
  _playing=false;
  if (_buffer)
  {
    WAV_COUNTER(wavSt,1);
    _countPlaying[_type]--;
    DSVerify( _buffer->Stop() );
    StoreCurrentPosition(); // 
    _writePos = 0;
    _stopTime = GlobalTickCount();
    
    if (_only2DEnabled)
    {
//      LogF(
//        "%d: Wave stopped %s, buffer %p",
//        GlobalTickCount(),(const char *)Name(),_buffer.GetRef()
//      );
    }
    //if (_stream) LogF("Stopped stream %s",(const char *)Name());

  }
  // if sound is stopped for a long time, we can unload it
  STAT_STOP(this);
  return true;
}

void Wave8::CheckPlaying()
{
  // check if wave has already terminated
  IsTerminated();
}

bool Wave8::CheckUnload()
{
  if (_wantUnload)
  {
    // check if we can unload (this will always be true on PC, but not onm Xbox)
    DWORD stat = 0;
    if (_buffer) _buffer->GetStatus(&stat);
    if (stat&DSBSTATUS_PLAYING)
    {
      //Log("Cannot unload %s - busy",(const char *)Name());
    }
    else
    {
      //Log("Unloading %s",(const char *)Name());
      _wantUnload = false;
      DoUnload();
      return true;
    }
    return false;
  }
  Fail("Why checking unload, when unload is not wanted?");
  return true;
}

void Wave8::Unload()
{
  return Unload(false);
}

void Wave8::UnloadStopped(bool sync)
{
  if (_buffer)
  {
    if (!sync)
    {
      DWORD stat = 0;
      _buffer->GetStatus(&stat);
      if (stat&DSBSTATUS_PLAYING)
      {
        // add this wave into the list of waves waiting for unload opportunity
        _wantUnload = true;
        _soundSys->_wavesUnload.Add(this);
      }
      else
      {
        DoUnload();
      }
    }
    else
    {
      DoUnload();
    }
  }
}

/**
\param sync when true, wave must be really stopped before function returns
*/
void Wave8::Unload(bool sync)
{
  DoStop();
  if (_wantUnload)
  {
    LogF("No need to unload, already unloaded");
    return;
  }
  UnloadStopped(sync);
}

void Wave8::Stop()
{
  DoStop();
}
void Wave8::LastLoop()
{
  if( !_loaded ) return;
  if( !_looping ) return;
  if( _playing )
  {
    if (!_stream)
    {
      if (_buffer)
      {
        WAV_COUNTER(wavPl,1);
        DSVerify( _buffer->Play(0,0,0) );
      }
    }
  }
  _looping=false;
}

void Wave8::PlayUntilStopValue( float time )
{
  // play until some time
  _stopTreshold = time;
  if( _looping ) return;
  _looping = true;
  if( !_loaded ) return;
  if( _playing )
  {
    if (_buffer)
    {
      WAV_COUNTER(wavPl,1);
      DSVerify( _buffer->Play(0,0,DSBPLAY_LOOPING) );
    }
  }
}
void Wave8::SetStopValue( float time )
{
  //LogF("%s: SetStopValue %.2f, %.2f",(const char *)Name(),time,_stopTreshold);
  if (time>_stopTreshold)
  {
    _terminated = true;
    Stop();
  }
}

// Volume is 0 to 1
// frequency is multiplied by original frequency

const int DBOffset=0;

static LONG float2dbLong( float val )
{
  // 20 dB is 10x
  // val = 10^(dbi/20)
  // log10(val)=dbi/20
  // log10(val)*20=dbi;
  if( val<=0 ) return DSBVOLUME_MIN;
  int ret=toIntFloor(log10(val)*2000)+DBOffset;
  saturate(ret,DSBVOLUME_MIN,DSBVOLUME_MAX);
  //Log("float2db %f %d",val,ret);
  return ret;
}

float WaveBuffers8::CalcMinDistance() const
{
  return floatMax(sqrt(_volume*_accomodation)*30,1e-6);
}

static bool SoundParsNeedUpdate(const DS3DBUFFER &a, const DS3DBUFFER &b, int timeMs)
{
  // never update sound more than 20 fps
  const int minTimeUpdate = 50;
  if (timeMs<minTimeUpdate)
  {
    return false;
  }
  
  //float time = timeMs * 0.001f;
  // TODO: weight based on relative volume
  float minDistDiff = fabs(a.flMinDistance-b.flMinDistance)/floatMax(a.flMinDistance,b.flMinDistance);
  if (minDistDiff>0.1f) return true;
  float posDist2 = Square(a.vPosition.x-b.vPosition.x)+Square(a.vPosition.y-b.vPosition.y)+Square(a.vPosition.z-b.vPosition.z);
  if (posDist2>0.5f) return true;
  float velDist2 = Square(a.vVelocity.x-b.vVelocity.x)+Square(a.vVelocity.y-b.vVelocity.y)+Square(a.vVelocity.z-b.vVelocity.z);
  if (velDist2>0.5f) return true;
  return false;
}
/*
\patch 1.47 Date 3/11/2002 by Ondra
- Workaround: Distance falloff limits do not work properly on SB Live and Audigy cards
due to driver or hardware bug. As a result, footsteps and other quiet sounds were
heard from large distances. Alternate method of distance limits handling is used
on HW accelerated cards.
*/

void WaveBuffers8::DoSetPosition( bool immediate, bool hwAcceleration )
{
  if( _only2DEnabled ) return;  
  if( !GetBuffer3D() ) return;

  Vector3Val pos=_position;
  Vector3Val vel=_velocity;
  
  DS3DBUFFER pars;
  pars.dwSize=sizeof(pars);
  pars.vPosition.x=pos[0],pars.vPosition.y=pos[1],pars.vPosition.z=pos[2];
  pars.vVelocity.x=vel[0],pars.vVelocity.y=vel[1],pars.vVelocity.z=vel[2];
  //pars.vVelocity.x=0,pars.vVelocity.y=0,pars.vVelocity.z=0;
  pars.dwInsideConeAngle=360;
  pars.dwOutsideConeAngle=360;
  pars.vConeOrientation.x=pars.vConeOrientation.y=0,pars.vConeOrientation.z=1;
  pars.lConeOutsideVolume=0;

  #ifdef _XBOX
    pars.flDistanceFactor=1.0;
    pars.flRolloffFactor=1.0;
    pars.flDopplerFactor=1.0;
  #endif

  pars.flMinDistance= CalcMinDistance();
  if (hwAcceleration)
  {
    pars.flMaxDistance=DS3D_DEFAULTMAXDISTANCE;
    //pars.flMaxDistance=pars.flMinDistance*10000;
  }
  else
  {
    pars.flMaxDistance=pars.flMinDistance*100;
  }
  pars.dwMode=_3DEnabled ? DS3DMODE_NORMAL : DS3DMODE_DISABLE;

  //LogF("  %x: set mode %d (%s)",this,pars.dwMode,immediate ? "Imm" : "Defer");
  //LogF("  min %.4f,max %.3f",pars.flMinDistance,pars.flMaxDistance);

  #if DEFERRED
  if( !immediate )
  {
    DWORD time = GlobalTickCount();
    if (/*hwAcceleration ||*/ _parsTime==0 || SoundParsNeedUpdate(pars,_pars,time-_parsTime))
    {
      WAV_COUNTER(wavSD,1);
      DSVerify( GetBuffer3D()->SetAllParameters(&pars,DS3D_DEFERRED) );
      _pars = pars;
      _parsTime = time;
    }
  }
  else
  #endif
  {
    WAV_COUNTER(wavSI,1);
    if (_parsTime!=0 && _pars.dwMode!=pars.dwMode)
    {
      // dx8 bug: deferred setting override immediate
      // at least for dwMode
      // if we see a different dwMode was set using deferred, set it using deferred again
      GetBuffer3D()->SetMode(pars.dwMode,DS3D_DEFERRED);
    }
    DSVerify( GetBuffer3D()->SetAllParameters(&pars,DS3D_IMMEDIATE) );
    _pars = pars;
    _parsTime = GlobalTickCount();
  }
}

float Wave8::GetMinDistance() const
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

/// estimate loudness of a sound before creating it
float SoundSystem8::GetLoudness(
  WaveKind kind, Vector3Par pos, float volume, float accomodation
) const
{
  float dist = _listenerPos.Distance(pos);
  
  float adjust = _volumeAdjustEffect;
  switch (kind)
  {
    case WaveSpeech: adjust = _volumeAdjustSpeech; break;
    case WaveMusic: adjust = _volumeAdjustMusic; break;
  }
  float minDistance = floatMax(sqrt(volume*adjust*accomodation)*30,1e-6);
  const float maxLoud = 3;
  if( minDistance>=maxLoud*dist ) return maxLoud;
  return DistanceRolloff(minDistance,dist);
}

float Wave8::GetLoudness() const
{
  if (_only2DEnabled)
  {
    return GetVolume()/_distance2D;
  }
  else
  {
    float dist = _3DEnabled ? _soundSys->_listenerPos.Distance(GetPosition()) : _distance2D;
    float minDistance= CalcMinDistance();
    const float maxLoud = 3;
    //if( minDistance/dist>=maxLoud ) return maxLoud;
    if( minDistance>=maxLoud*dist ) return maxLoud;
    return DistanceRolloff(minDistance,dist);
  }
}

bool Wave8::Get3D() const
{
  return WaveBuffers8::Get3D();
}

float Wave8::Distance2D() const
{
  #ifdef _XBOX
  const float hwFactor = 1;
  #else
  const float hwFactor = 1;
  #endif
  if (_only2DEnabled) return hwFactor;
  Assert(!_3DEnabled);
  return _distance2D*hwFactor;
}

void Wave8::Set3D(bool is3D, float distance2D)
{
  if (distance2D<0) distance2D = 1;
  if (is3D!=_3DEnabled || distance2D!=_distance2D)
  {
    //LogF("Change mode of %s to %d",(const char *)Name(),is3D);
    // stop buffer to avoid clicks during switching
    DoStop();
    WaveBuffers8::Set3D(is3D,distance2D);
  }
}

void Wave8::SetRadio(bool isRadio)
{
}

void Wave8::SetPosition( Vector3Par pos, Vector3Par vel, bool immediate )
{
  _position=pos;
  _velocity=vel;
  // IDirectSound3DBuffer implementation
  _posValid = true;

  if( !_playing ) return;

  //LogF("SetPos %s",(const char *)Name());
  DoSetPosition(immediate,MaxVolFix(_soundSys->_hwEnabled));
}



bool WaveBuffers8::GetMuted() const
{
  return _volumeSet<=-10000;
}

void WaveBuffers8::UpdateVolume()
{
  if (_only2DEnabled || !_3DEnabled)
  {
    // some problem with setting parameters
    // convert _volume do Db
    float vol = _volume*_accomodation*_volumeAdjust;
    if (!_only2DEnabled)
    {
      // simulate DirectSound attenuation for _distance2D
      // calculate volume based on distance2D
      float minDistance = CalcMinDistance();
      float atten = _distance2D<minDistance ? 1 : DistanceRolloff(minDistance,_distance2D);
      vol = atten*vol;
      SetI3DL2Db(0,0,!_playing);
      #if _ENABLE_REPORT
      if (InterestedIn(GetDebugName()))
      {
        LogF("%s: 2D OO %d,%d",cc_cast(GetDebugName()),_obstructionSet,_occlusionSet);
      }
      #endif
    }
    SetVolumeDb(float2dbLong(vol));
    #if _ENABLE_REPORT
    if (InterestedIn(GetDebugName()))
    {
      LogF("%s: 2D vol %d",cc_cast(GetDebugName()),_volumeSet);
    }
    #endif
  }
  else
  {
    // instead of volume use 3D MinDistance factor
    // we can use EAX/I3DL2 obstruction/occlusion, or we can lower total volume
    float volumeAdjust = _volumeAdjust;
    #ifdef _XBOX
      LONG obstructionDb = float2dbLong(_obstruction);
      LONG occlusionDb = float2dbLong(_occlusion);
      SetI3DL2Db(obstructionDb,occlusionDb,!_playing);
      #if _ENABLE_REPORT
      if (InterestedIn(GetDebugName()))
      {
        LogF("%s: OO %d,%d",cc_cast(GetDebugName()),_obstructionSet,_occlusionSet);
      }
      #endif
    #else
      // no HW obstruction/occlusion
      // we use total volume to simulate it
      float volumeFactor = 1-(1-_occlusion*_obstruction)*0.33f;
      volumeAdjust *= volumeFactor;
    #endif
    
    SetVolumeDb(float2dbLong(volumeAdjust));
    #if _ENABLE_REPORT
    if (InterestedIn(GetDebugName()))
    {
      LogF("%s: vol %d",cc_cast(GetDebugName()),_volumeSet);
    }
    #endif
    
  }
}

void WaveBuffers8::SetObstruction(float obstruction, float occlusion, float deltaT)
{
  // change obstruction gradually
  const float minEps = 0.01f;
  if (
    fabs(occlusion-_occlusion)<minEps && fabs(obstruction-_obstruction)<minEps
  )
  {
    return;
  }
  
  if (deltaT<=0)
  {
    _occlusion = occlusion;
    _obstruction = obstruction;
  }
  else
  {
    float delta;
    delta = occlusion - _occlusion;
    const float maxSpeed = 1.5f;
    Limit(delta,-deltaT*maxSpeed,+deltaT*maxSpeed);
    _occlusion += delta;
    
    delta = obstruction - _obstruction;
    Limit(delta,-deltaT*maxSpeed,+deltaT*maxSpeed);
    _obstruction += delta;
  }
  UpdateVolume();
}

void WaveBuffers8::SetAccomodation( float acc )
{
  if( !_enableAccomodation ) return;
  if (fabs(_accomodation-acc)<0.01f)
  {
    return;
  }
  DoAssert(acc>=0);
  _accomodation=floatMax(acc,0);

  UpdateVolume();
}

void WaveBuffers8::Set3D( bool is3D, float distance2D )
{
  // disable 3D processing
  _3DEnabled = is3D;
  _distance2D = distance2D;
}

bool WaveBuffers8::Get3D() const
{
  return _3DEnabled && !_only2DEnabled;
}

void WaveBuffers8::SetKind( SoundSystem8 * sys, WaveKind kind )
{
  _kind = kind;
  float vol = sys->_volumeAdjustEffect;
  if (_kind==WaveMusic) vol = sys->_volumeAdjustMusic;
  else if (_kind==WaveSpeech) vol = sys->_volumeAdjustSpeech;
  _volumeAdjust = vol;
  UpdateVolume();
}

void WaveBuffers8::SetVolumeAdjust
(
  float volEffect, float volSpeech, float volMusic
)
{
  float vol = volEffect;
  if (_kind==WaveMusic) vol = volMusic;
  else if (_kind==WaveSpeech) vol = volSpeech;
  _volumeAdjust = vol;
  UpdateVolume();
}

void WaveBuffers8::SetVolume( float vol, bool imm )
{
  _volume=vol;;
  UpdateVolume();
}

DWORD Wave8::CalcFrequencyDword() const
{
  Assert(_loadedHeader);
  DWORD iFreq=toInt(_freqSet*_header._frequency);
  if( iFreq<_soundSys->_minFreq ) iFreq=_soundSys->_minFreq;
  else if( iFreq>_soundSys->_maxFreq ) iFreq=_soundSys->_maxFreq;
  return iFreq;
}

void Wave8::SetFrequency( float freq, bool immediate )
{
  static float minFreqDiff = 1e-4f;
  // we can assume freq is usually something around 1
  if (fabs(freq-_freqSet)>minFreqDiff)
  {
    _freqSet=freq;
    //LogF("%s: ratio %g",(const char *)Name(),ratio);
    if( _playing && _buffer )
    {
      WAV_COUNTER(wavSV,1);
      DSVerify( _buffer->SetFrequency(CalcFrequencyDword()) );
    }
  }
}

void Wave8::SetVolume( float volume, float freq, bool immediate )
{
  if (freq>=0)
  {
    Wave8::SetFrequency(freq,immediate);
  }
  // recalculate volume using master volume
  WaveBuffers8::SetVolume(volume,immediate);
}

// Sound system

SoundSystem8::SoundSystem8()
:_soundReady(false)
{
  #ifndef _XBOX
  CoInitialize(NULL);
  #endif
  New((HWND)0,true);
}
SoundSystem8::SoundSystem8(HWND hwnd, bool dummy)
:_soundReady(false)
#ifdef _XBOX
, _fxdesc(NULL)
#endif
{
  RegisterFreeOnDemandSystemMemory(this);
  #ifndef _XBOX
  CoInitialize(NULL);
  #endif
  New(hwnd,dummy);
}

bool ParseUserParams(ParamFile &cfg, GameDataNamespace *globals);
void SaveUserParams(ParamFile &cfg);

SoundSystem8::~SoundSystem8()
{
  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile config;
  if (ParseUserParams(config, &globals))
  {
    SaveConfig(config);
    SaveUserParams(config);
  }
  
  Delete();
  #ifndef _XBOX
  CoUninitialize();
  #endif
}

void SoundSystem8::LoadConfig(ParamEntryPar config)
{
  float cdVolume = GetDefaultCDVolume();
  float fxVolume = GetDefaultWaveVolume();
  float speechVolume = GetDefaultSpeechVolume();
  if
  (
    config.FindEntry("volumeCD") && config.FindEntry("volumeFX")
    && config.FindEntry("volumeSpeech")
  )
  {
    cdVolume=config>>"volumeCD";
    fxVolume=config>>"volumeFX";
    speechVolume=config>>"volumeSpeech";
  }
  SetCDVolume(cdVolume);
  SetWaveVolume(fxVolume);
  SetSpeechVolume(speechVolume);

  #ifndef _XBOX
  // by default enable both EAX and HW acceleration as long as HW supports it
  _eaxEnabled = _eaxWanted = config.ReadValue("soundEnableHW",true);
  _hwEnabled = _hwWanted = config.ReadValue("soundEnableEAX",true);
  #else
  _hwEnabled = true;
  _eaxEnabled = true;
  #endif

#if _FORCE_SINGLE_VOICE
  Glob.config.singleVoice = true;
#else
  Glob.config.singleVoice = false;
  ConstParamEntryPtr entry = config.FindEntry("singleVoice");
  if (entry) Glob.config.singleVoice = *entry;
#endif
}

void SoundSystem8::SaveConfig(ParamFile &config)
{
  if (!IsOutOfMemory())
  {
    config.Add("volumeCD",GetCDVolume());
    config.Add("volumeFX",GetWaveVolume());
    config.Add("volumeSpeech",GetSpeechVolume());
    config.Add("singleVoice", Glob.config.singleVoice);
    #ifndef _XBOX
    config.Add("soundEnableEAX",_eaxWanted);
    config.Add("soundEnableHW",_hwWanted);
    #endif
    
    // config.Save(name);
  }
}

bool SoundSystem8::CanChangeEAX() const
{
  return GetNetworkManager().GetClientState()==NCSNone;
}

bool SoundSystem8::CanChangeHW() const
{
  return GetNetworkManager().GetClientState()==NCSNone;
}

bool SoundSystem8::EnableHWAccel(bool val)
{
  #ifndef _XBOX
  if (!_canHW) return false;
  if (_hwWanted==val) return true;
  _hwEnabled = _hwWanted = val;
  if (_hwEnabled)
  {
    _canEAX = true;
    if (_eaxWanted)
    {
      EnableEAX(true);
    }
  }

  // we must restart all playing buffers
  _waves.RemoveNulls();
  for (int i=0; i<_waves.Size(); i++)
  {
    Wave8 *wave = _waves[i];
    if (!wave->_playing) continue;
    wave->Stop();
    wave->_wantPlaying = true;
    if (wave->DoPlay())
    {
      wave->_wantPlaying = false;
    }
  }
  // clear cache - release all stored sounds
  _cache.Clear();
  _cacheUniqueBuffers = 0;
  #endif

  return _hwEnabled;
}

bool SoundSystem8::EnableEAX(bool val)
{
  #ifndef _XBOX
  if (!_canEAX || !_hwEnabled) return false;
  if (_eaxWanted==val) return true;
  _eaxEnabled = _eaxWanted = val;
  if (_eaxEnabled)
  {
    if (!InitEAX())
    {
      _eaxEnabled = false;
    }
  }
  else
  {
    DeinitEAX();
  }
  #endif
  return _eaxEnabled;
}

bool SoundSystem8::GetEAX() const
{
  return _eaxEnabled;
}

bool SoundSystem8::GetHWAccel() const
{
  return _hwEnabled;
}

bool SoundSystem8::GetDefaultEAX() const
{
  return _canEAX;
}

bool SoundSystem8::GetDefaultHWAccel() const
{
  return _canHW;
}



/*!
\patch_internal 1.11 Date 8/1/2001 by Ondra
- New: possibility of dummy sound system - no DirectSound objects are ever created.
*/

void SoundSystem8::New(HWND winHandle, bool dummy)
{
  #if !_DISABLE_GUI
  if (dummy)
  #endif
  {
    LogF("No sound - command line switch");

    _soundReady=true; 
    _commited=false;
    #ifndef _XBOX
    _canHW = false;
    _canEAX = false;
    #endif
    _maxChannels=31;
    _maxFreq=100000;
    _minFreq=100;
    _enablePreview = true;
    _doPreview = false;
    _hwEnabled = false;
    _eaxEnabled = false;

    _speechVolWanted = GetDefaultSpeechVolume();
    _waveVolWanted = GetDefaultWaveVolume();
    _musicVolWanted = GetDefaultCDVolume();

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

  _cacheUniqueBuffers = 0;

  _listenerPos=VZero;
  _listenerVel=VZero;
  _listenerDir=VForward;
  _listenerUp=VUp;

  _duplicateWorks=true;

  #ifndef _XBOX
  _mixer = new AudioMixer();
  #endif
  _volumeAdjustEffect=1;
  _volumeAdjustSpeech=1;
  _volumeAdjustMusic=1;

  _waveVolWanted = GetDefaultWaveVolume();
  _musicVolWanted = GetDefaultCDVolume();
  _speechVolWanted = GetDefaultSpeechVolume();

  UpdateMixer();  

  GameDataNamespace globals(NULL, RString(), false); // save - no global namespace
  ParamFile config;
  ParseUserParams(config, &globals);
  LoadConfig(config);

  Assert( !_soundReady );

  _eaxEnv.type = SEPlain;
  _eaxEnv.density = 0.2;
  _eaxEnv.size = 100;

  #ifndef _XBOX

  HRESULT hr = DS_OK;
  hr = CoCreateInstance
  (
    CLSID_DirectSound8, NULL, CLSCTX_INPROC, 
    IID_IDirectSound8, (void**)_ds.Init()
  );
  if( FAILED(hr) || !_ds )
  {
    LogF("No DirectSound");
    return;
  }
  _ds->Initialize(NULL);

  if( FAILED(_ds->SetCooperativeLevel(winHandle,DSSCL_EXCLUSIVE)) )
  {
    LogF("Cannot set DirectSound cooperative level.");
    _ds.Free();
    return;
  }

  DSBUFFERDESC dsbd;
  // Set up the primary direct sound buffer.
  memset(&dsbd,0,sizeof(dsbd));
  dsbd.dwSize = sizeof(dsbd);
  dsbd.dwFlags = DSBCAPS_PRIMARYBUFFER|DSBCAPS_CTRL3D;
  

  if( _ds->CreateSoundBuffer(&dsbd,_primary.Init(),NULL) )
  {
    ErrorMessage("Cannot create primary sound buffer.");
  }

  WAVEFORMATEX wfx;
  wfx.wFormatTag=WAVE_FORMAT_PCM;
  wfx.nChannels=2;
  if (_hwEnabled)
  {
    wfx.nSamplesPerSec=44100;
  }
  else
  {
    wfx.nSamplesPerSec=22050;
  }
  wfx.wBitsPerSample=16;
  wfx.nBlockAlign=(short)(wfx.wBitsPerSample/8*wfx.nChannels);
  wfx.nAvgBytesPerSec=wfx.nBlockAlign*wfx.nSamplesPerSec;
  wfx.cbSize=0;
  
  if( FAILED(_primary->SetFormat(&wfx)) )
  {
    ErrorMessage("Cannot set primary buffer format.");
  }

  if( FAILED(_primary->QueryInterface(IID_IDirectSound3DListener8, (void **)_listener.Init())) )
  {
    ErrorMessage("Primary buffer is not 3D.");
  }
  

  if( FAILED(_primary->Play(0,0,DSBPLAY_LOOPING)) )
  {
    ErrorMessage("Cannot start primary buffer.");
  }

  #else

  #if 0 //_ENABLE_REPORT && !_RELEASE || _PROFILE
    g_dwDirectSoundDebugLevel = 3;
    g_dwDirectSoundDebugBreakLevel = 3;
  #endif

  #if _USE_XACT_PROFILE
    XACT_RUNTIME_PARAMETERS pars;
    pars.dwMax2DHwVoices = 0;
    pars.dwMax3DHwVoices = 0;
    pars.dwMaxConcurrentStreams = 0;
    pars.dwMaxNotifications = 0;
    pars.dwInteractiveAudioLookaheadTime = 250;
    IXACTEngine *xact;
    XACTEngineCreate(&pars, &xact);
  #endif

  DirectSoundCreate(NULL,_ds.Init(),NULL);
  QIFStream in;
  in.open("xboxSFX.bin");
  in.PreReadSequential();
  DSEFFECTIMAGELOC dsploc;
  dsploc.dwI3DL2ReverbIndex = GraphI3DL2_I3DL2Reverb;
  dsploc.dwCrosstalkIndex = GraphXTalk_XTalk;
  //dsploc.dwI3DL2ReverbIndex = I3DL2_CHAIN_I3DL2_REVERB;
  //dsploc.dwCrosstalkIndex = I3DL2_CHAIN_XTALK;

  if (in.fail() || in.rest()==0)
  {
    ErrorMessage("Cannot load DSP image");
  }
  else for (;;)
  {
    AutoArray<char> buf;
    WriteAutoArrayChar writeBuf(buf);
    in.Process(writeBuf);
    HRESULT hr = _ds->DownloadEffectsImage(buf.Data(),buf.Size(),&dsploc,&_fxdesc);
    if (!FAILED(hr)) break;
    if (!FreeOnDemandSystemMemoryLowLevel(128*1024))
    {
      ErrorMessage("DownloadEffectsImage failed");
      break;
    }
  }

  DirectSoundUseFullHRTF();

  const int mixBinHeadroom = 0;
  for (int i=0; i<DSMIXBIN_COUNT; i++)
  {
    _ds->SetMixBinHeadroom(i,mixBinHeadroom);
  }
  static const DSI3DL2LISTENER noReverb = {DSI3DL2_ENVIRONMENT_PRESET_NOREVERB};
  _ds->SetI3DL2Listener(&noReverb,DS3D_IMMEDIATE);
  #endif


  _soundReady=true; 
  _commited=false;
   // check valid freq. range

  #ifndef _XBOX
    // check HW acceleration level
    DSCAPS caps;
    caps.dwSize=sizeof(caps);
    _ds->GetCaps(&caps);
    _canHW = false;
    _canEAX = false;
    #if !DISABLE_HW_ACCEL
    // if card supports too few channels, do not use it's HW feutures
    if( caps.dwMaxHwMixingAllBuffers>=16 && caps.dwMaxHw3DAllBuffers>=16 )
    {
      _canHW = true;
      _canEAX = true;
      LogF("Using DirectSound3D HW");
    }
    else
    #endif
    {
      _hwEnabled = false;
      _eaxEnabled = false;
      _canEAX = false;
    }
  #else
    _hwEnabled = true;
    _eaxEnabled = true;
  #endif

  #ifndef _XBOX 
  if (_hwEnabled)
  { 
    _minFreq=caps.dwMinSecondarySampleRate;
    _maxFreq=caps.dwMaxSecondarySampleRate;
  }
  else
  {
    // no need to check hw caps if we do not use HW
    _minFreq=20;
    _maxFreq=100000;
  }
  #else
    _minFreq=DSBFREQUENCY_MIN;
    _maxFreq=DSBFREQUENCY_MAX;
  #endif
  // TODO: _maxChannels autodetection
  _maxChannels=31;
  if( _maxFreq==0 ) _maxFreq=100000;
  if( _minFreq==0 ) _minFreq=100;

  _enablePreview = true; // preview disabled until sound initialized
  _doPreview = false;

  if (_hwEnabled && _eaxEnabled)
  {
    _eaxEnabled = InitEAX();
  }
  #endif
}

static const RString PauseName="";

Wave8::Wave8( SoundSystem8 *sSys, float delay ) // empty wave
:AbstractWave(PauseName)
{
  WaveBuffers8::Reset(sSys);
  DoConstruct();
  _soundSys=sSys;
  _only2DEnabled=true;

  // note: only wav files may be streaming
  _volumeSet=-10000;
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
}

Wave8::Wave8( SoundSystem8 *sSys, RString name, bool is3D )
:AbstractWave(name)
{
  WaveBuffers8::Reset(sSys);
  DoConstruct();
  _soundSys=sSys;
  _only2DEnabled=!is3D;

  _posValid=!is3D; // 3d position explicitly set

  _volumeSet=-10000;
  _obstructionSet = _occlusionSet = 0;
  _obstruction = _occlusion = 1;
  _freqSet=1;
  _curPosition = 0;
  _skipped = 0;
  _volumeAdjust = _soundSys->_volumeAdjustEffect;

  LoadHeader();
}



Wave8::~Wave8()
{
  DoDestruct();
}

void Wave8::CacheStore( WaveDSCache8 &store )
{
  #if DO_PERF
    ADD_COUNTER(wavCS,1);
  #endif
  store._name=Name();
  #ifndef _XBOX
  store._buffer=_buffer;
  store._buffer3D=_buffer3D;
  store._header = _header;
  #else
  store._data = _data;
  #endif
}

WaveDSCache8::~WaveDSCache8()
{
  #ifndef _XBOX
  if (_buffer)
  {
    #if DO_PERF
      ADD_COUNTER(wavCD,1);
    #endif

    _buffer3D.Free();
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
  #else
  _data.Free();
  #endif
}

bool Wave8::CacheLoad( WaveDSCache8 &store )
{
  //Log("CacheLoad %s",(const char *)Name());
  #if DO_PERF
  if (_buffer)
  {
    ADD_COUNTER(wavCL,1);
  }
  #endif
  #ifndef _XBOX
    _buffer=store._buffer;
    _buffer3D=store._buffer3D;
    store._buffer.Free();
    store._buffer3D.Free();
    return true;
  #else
    _data = store._data;

    DSBUFFERDESC dsbd;
    WAVEFORMATEX format;
    GetBufferDesc(dsbd,format,false);
    // for replication we need very little memory, but we need some
    FreeOnDemandGarbageCollectSystemMemoryLowLevel(4*1024+32*1024);
    for(;;)
    {
      HRESULT hr = _soundSys->_ds->CreateSoundBuffer(&dsbd,_buffer.Init(),NULL);
      if (!FAILED(hr)) break;
      if (hr==E_OUTOFMEMORY)
      {
        if (FreeOnDemandSystemMemoryLowLevel(4*1024)) continue;
      }
      Fail("CreateSoundBuffer failed");
      return false;
    }
    HRESULT hr = _buffer->SetBufferData(_data->Data(),_data->Size());
    if (FAILED(hr))
    {
      Fail("SetBufferData failed");
      _buffer.Free();
      return false;
    }
    return true;
  #endif

}

bool Wave8::Duplicate( const Wave8 &src )
{
  if (src._stream) return false; // no duplicate possible on streaming buffers

  //Log("Duplicate %s",(const char *)src.Name());
  #if DO_PERF
  WAV_COUNTER(wavDp,1);
  #endif
  DoAssert( src.Loaded() );
  // try to duplicate hw buffer once
  
  //__int64 start = Rdtsc();

  #ifdef _XBOX
    Assert(src._loadedHeader);
    _data = src._data;

    _header=src._header;

    //_maxVolume=src._maxVolume;
    //_avgVolume=src._avgVolume;

    _loaded=true;

    DSBUFFERDESC dsbd;
    WAVEFORMATEX format;
    GetBufferDesc(dsbd,format,false);

    for(;;)
    {
      HRESULT hr = _soundSys->_ds->CreateSoundBuffer(&dsbd,_buffer.Init(),NULL);
      if (!FAILED(hr)) break;
      if (hr==E_OUTOFMEMORY)
      {
        if (FreeOnDemandSystemMemoryLowLevel(4*1024)) continue;
      }
      Fail("CreateSoundBuffer failed");
      return false;
    }

    HRESULT hr = _buffer->SetBufferData(_data->Data(),_data->Size());
    if (FAILED(hr))
    {
      Fail("SetBufferData failed");
      _buffer.Free();
      return false;
    }


    // we have real duplicate - shares sound buffer memory
    
    return true;
  #else
    if (!_soundSys->_duplicateWorks ) return false;
    IDirectSoundBuffer *temp = NULL;
    HRESULT hr = _soundSys->_ds->DuplicateSoundBuffer(src._buffer,&temp);
    if (FAILED(hr) )
    {
      _soundSys->_duplicateWorks=false;
      // note: DuplicateSoundBuffer fails on most HW accelerators
      Log("Cannot duplicate sound buffer");
      // create wave from a scratch
      //Load(); //,Name());
      return false;
    }
    // check if we can query 8 interface
    hr = temp->QueryInterface(IID_IDirectSoundBuffer8,(void **)_buffer.Init());
    if (FAILED(hr))
    {
      temp->Release();
      _soundSys->_duplicateWorks=false;
      Log("Cannot duplicate DirectSound8 buffer");
      return false;
    }

    // we have real duplicate - shares sound buffer memory
    if( src._buffer3D )
    { // obtain 3D buffer interface if necessary
      if( _buffer->QueryInterface(IID_IDirectSound3DBuffer8,(void **)_buffer3D.Init()) )
      {
        ErrorMessage("Cannot create 3D sound buffer");
      }
    }


    Assert(src._loadedHeader);
    _header=src._header;

    //_maxVolume=src._maxVolume;
    //_avgVolume=src._avgVolume;

    _loaded=true;

    AddStats("Dup");
    //_soundSys->LogChannels("Duplicate");
    return true;
  #endif
}

bool SoundSystem8::LoadHeaderFromCache( Wave8 *wave )
{
  for( int i=0; i<_cache.Size(); i++ )
  {
    WaveDSCache8 &cache=_cache[i];
    if( !strcmp(cache._name,wave->Name()) )
    {
      wave->_header = cache._header;
      return true;
    }
  }
  return false;
}

bool SoundSystem8::LoadFromCache( Wave8 *wave )
{
  for( int i=0; i<_cache.Size(); i++ )
  {
    WaveDSCache8 &cache=_cache[i];
    if( !strcmp(cache._name,wave->Name()) )
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

void SoundSystem8::LogChannels( const char *name )
{
  #ifndef _XBOX
  DSCAPS caps;
  caps.dwSize=sizeof(caps);
  _ds->GetCaps(&caps);
  LogF
  (
    "%10s: Free tot %3d 3D %3d   Total tot %3d 3D %3d",
    name,
    caps.dwFreeHwMixingAllBuffers,caps.dwFreeHw3DAllBuffers,
    caps.dwMaxHwMixingAllBuffers,caps.dwMaxHw3DAllBuffers
  );
  #endif
}

void SoundSystem8::ReserveCache( int number3D, int number2D )
{
  #if !ENABLE_VOICEMAN
  // with voice manager there is no need to evict sounds from cache
  if (!_hwEnabled) return;
  // with HW acceleration: free HW channels
  // otherwise: keep some reasonable number of SW channels
  int size=_cache.Size();
  //int waves=_waves.Size();
  while( --size>=0 )
  {
    DSCAPS caps;
    caps.dwSize=sizeof(caps);
    _ds->GetCaps(&caps);
    // note: some HW reports negative free hw buffers
    WaveDSCache8 &cache=_cache[size];
    if( (int)caps.dwFreeHwMixingAllBuffers<number2D )
    {
      // this should be very rare
      #if DIAG_LOAD
      LogF
      (
        "Cache deleted %s (all %d, 3D %d)",
        (const char *)cache._name,
        caps.dwFreeHwMixingAllBuffers,
        caps.dwFreeHw3DAllBuffers
      );
      #endif
      _cache.Delete(size);
      #if DIAG_LOAD
      LogChannels("CacheDel2D");
      #endif
    }
    else if( (int)caps.dwFreeHw3DAllBuffers<number3D )
    {
      if( cache._buffer3D )
      {
        #if DIAG_LOAD
        LogF
        (
          "Cache 3D deleted %s (all %d, 3D %d)",
          (const char *)cache._name,caps.dwFreeHw3DAllBuffers,
          caps.dwFreeHwMixingAllBuffers,
          caps.dwFreeHw3DAllBuffers
        );
        #endif
        _cache.Delete(size);
        #if DIAG_LOAD
        LogChannels("CacheDel3D");
        #endif
      }
    }
  }
  #endif
}

void SoundSystem8::StoreToCache( Wave8 *wave )
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
  for( int i=0; i<_cache.Size();  )
  {
    WaveDSCache8 &cache=_cache[i];
    if( !strcmp(cache._name,wave->Name()) )
    {
      #if DIAG_LOAD>=2
        LogF("Wave8 %s cache refresh",(const char *)_cache[i]._name);
      #endif
      _cache.Delete(i);
      // move to cache beginning
      _cache.Insert(0);
      wave->CacheStore(_cache[0]);
      return;
    }
    else i++;
  }
  /**/


  int size=_cache.Size();
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
  WaveDSCache8 &cache=_cache[index];
  wave->CacheStore(cache);

  #if DIAG_LOAD>=2
  LogF("Wave8 %s inserted into the cache",(const char *)wave->Name());
  #endif
}

float SoundSystem8::GetWaveDuration( const char *filename )
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

bool SoundSystem8::InitEAX()
{
  #if !_DISABLE_GUI && !defined _XBOX
  WAVEFORMATEX wfx;

  wfx.wFormatTag=WAVE_FORMAT_PCM;
  wfx.nChannels=1;
  wfx.nSamplesPerSec=22050;
  wfx.wBitsPerSample=16;
  wfx.nBlockAlign=(short)(wfx.wBitsPerSample/8*wfx.nChannels);
  wfx.nAvgBytesPerSec=wfx.nBlockAlign*wfx.nSamplesPerSec;
  wfx.cbSize=0;

  // Set up the direct sound buffer. 
  DSBUFFERDESC dsbd;
  memset(&dsbd, 0, sizeof(DSBUFFERDESC));
  dsbd.dwSize = sizeof(DSBUFFERDESC);
  dsbd.dwFlags = DSBCAPS_CTRLFREQUENCY|DSBCAPS_CTRLVOLUME;
  dsbd.dwFlags |= LocHWFlags;
  dsbd.dwFlags |= DSBCAPS_CTRL3D;
  dsbd.dwFlags |= GetPos2Flags;
  //dsbd.dwFlags |= DSBCAPS_MUTE3DATMAXDISTANCE;
  dsbd.guid3DAlgorithm=DS3DALG_DEFAULT; // surround/quadro if available

  dsbd.dwBufferBytes = 22050*2; // 1 sec buffer
  dsbd.lpwfxFormat = &wfx;

  HRESULT hr;
  IDirectSoundBuffer *temp = NULL;
  hr =_ds->CreateSoundBuffer(&dsbd,&temp,NULL);
  if (FAILED(hr))
  {
    RptF("CreateSoundBuffer (EAX) failed: %x",hr);
    return false;
  }
  
  hr = temp->QueryInterface(IID_IDirectSoundBuffer8,(void **)_eaxFakeListener.Init());
  if (FAILED(hr))
  {
    temp->Release();
    RptF("QueryInterface IDirectSoundBuffer8 (EAX) failed: %x",hr);
    return false;
  }

  if( _eaxFakeListener->QueryInterface(IID_IKsPropertySet,(void **)_eaxListener.Init()) )
  {
    RptF("Cannot get IID_IKsPropertySet (EAX)");
    _eaxFakeListener.Free();
    return false;
  }
  // check if EAX is supported
  DWORD support;
  const DWORD supportNeed = KSPROPERTY_SUPPORT_GET|KSPROPERTY_SUPPORT_SET;
  hr = _eaxListener->QuerySupport
  (
    DSPROPSETID_EAX_ListenerProperties,DSPROPERTY_EAXLISTENER_ALLPARAMETERS,&support
  );
  if (FAILED(hr) || (support&supportNeed)!=supportNeed)
  {
    _eaxFakeListener.Free();
    _eaxListener.Free();
    return false;
  }
  // 
  
  return true;
  #elif defined _XBOX
  DoSetEAXEnvironment();
  return true;
  #else
  return true;
  #endif
}

void SoundSystem8::DeinitEAX()
{
  #ifndef _XBOX
  if (_eaxListener)
  {
    #if !_DISABLE_GUI
    // set no EAX
    DWORD val = -10000;
    _eaxListener->Set
    (
      DSPROPSETID_EAX_ListenerProperties,DSPROPERTY_EAXLISTENER_ROOM,
      NULL,0,&val,sizeof(val)
    );
    #endif
  }
  _eaxFakeListener.Free();
  _eaxListener.Free();
  #endif
}

#ifdef _XBOX
  static DSI3DL2LISTENER EnvPlain    ={-1500,-1500,0,1,0.2,-850,0.06,-900,0.1,16,100,5000};
  static DSI3DL2LISTENER EnvForest   ={-3000,-1000,0.1,2.5,0.1 ,0,0.1,-900,0.5,80,100,5000};
  static DSI3DL2LISTENER EnvCityOpen ={-2500,-800 ,3.5,5  ,0.2 ,-1000,0.04,-1000,0.1,20,100,5000};
  static DSI3DL2LISTENER EnvMountains={-2000,-1500,10 ,10 ,0.2 ,-1500,0.1,-1000,0.1,21,100,5000};

  #if _ENABLE_CHEATS
  // real-time tuning  via scripting
  #include <El/evaluator/expressImpl.hpp>

  static GameValue DebugEnvPars(const GameState *state, GameValuePar oper1, GameValuePar oper2)
  {
    RString envName = oper1;
    const GameArrayType &array = oper2;
    DSI3DL2LISTENER *env = NULL;
    if (!strcmpi(envName,"plain")) env = &EnvPlain;
    if (!strcmpi(envName,"forest")) env = &EnvForest;
    if (!strcmpi(envName,"city")) env = &EnvCityOpen;
    if (!strcmpi(envName,"mountains")) env = &EnvMountains;
    if (!env)
    {
      GlobalShowMessage(1000,"Unknown environment %s",cc_cast(envName));
      return GameValue();
    }
    if (array.Size()!=12)
    {
      state->SetError(EvalDim,array.Size(),12);
      return GameValue();
    }
    for (int i=0; i<array.Size(); i++)
    {
      if (array[i].GetType()!=GameScalar)
      {
        state->TypeError(array[i].GetType(),GameScalar);
        return GameValue();
      }
    }

    env->lRoom = toInt(array[0]);
    env->lRoomHF = toInt(array[1]);
    env->flRoomRolloffFactor = array[2];
    env->flDecayTime = array[3];
    env->flDecayHFRatio = array[4];
    env->lReflections = toInt(array[5]);
    env->flReflectionsDelay = array[6];
    env->lReverb = toInt(array[7]);
    env->flReverbDelay = array[8];
    env->flDiffusion = array[9];
    env->flDensity = array[10];
    env->flHFReference = array[11];
    
    (static_cast<SoundSystem8 *>(GSoundsys))->DoSetEAXEnvironment();
    return GameValue();
  }

  #include <El/Modules/modules.hpp>

  static const GameOperator DbgBinary[]=
  {
    GameOperator(GameNothing,"diag_setenv",function,DebugEnvPars,GameString,GameArray),
  };

  INIT_MODULE(GameStateDbgEAX, 3)
  {
    GGameState.NewOperators(DbgBinary,lenof(DbgBinary));
    //GGameState.NewFunctions(DbgUnary,lenof(DbgUnary));
  };
  
  #endif
  
#endif

void SoundSystem8::DoSetEAXEnvironment()
{
  if (!_ds) return;
  
  PROFILE_SCOPE_EX(snEAX,sound);

  #ifndef _XBOX
  #if !_DISABLE_GUI
  // TODO: convert SoundEnvironment to EAX2 properties
  //EAXLISTENERPROPERTIES eaxProp;
  DWORD type = EAX_ENVIRONMENT_GENERIC;
  switch (_eaxEnv.type)
  {
    case SEPlain: type = EAX_ENVIRONMENT_PLAIN; break;
    case SEMountains: type = EAX_ENVIRONMENT_MOUNTAINS; break;
    case SEForest: type = EAX_ENVIRONMENT_CITY; break;
    case SECity: type = EAX_ENVIRONMENT_CITY; break;
    case SERoom: type = EAX_ENVIRONMENT_ROOM; break;
  } 
  _eaxListener->Set
  (
    DSPROPSETID_EAX_ListenerProperties,DSPROPERTY_EAXLISTENER_ENVIRONMENT,
    NULL,0,&type,sizeof(type)
  );
  DWORD size = _eaxEnv.size;
  if (size<2) size = 2;
  if (size>100) size = 100;
  _eaxListener->Set
  (
    DSPROPSETID_EAX_ListenerProperties,DSPROPERTY_EAXLISTENER_ENVIRONMENTSIZE,
    NULL,0,&size,sizeof(size)
  );
  #endif
  #else
    // set I3DL2 reverberation

    static const DSI3DL2LISTENER noReverb = 
    {
      DSI3DL2_ENVIRONMENT_PRESET_NOREVERB
    };
    const DSI3DL2LISTENER *pars = &noReverb;

    switch (_eaxEnv.type)
    {
      default:
      case SEPlain: pars = &EnvPlain; break;
      case SEMountains: pars = &EnvMountains; break;
      case SEForest: pars = &EnvForest; break;
      case SERoom:
      case SECity: pars = &EnvCityOpen; break;
    } 
    _ds->SetI3DL2Listener(pars,DS3D_IMMEDIATE);
  #endif
}

void SoundSystem8::SetEnvironment( const SoundEnvironment &env )
{
  if
  (
    env.type==_eaxEnv.type &&
    fabs(env.size-_eaxEnv.size)<1 &&
    fabs(env.density-_eaxEnv.density)<0.05
  )
  {
    // no change
    return;
  }
  _eaxEnv = env;
  #ifndef _XBOX
  if (!_eaxListener) return;
  #endif

  DoSetEAXEnvironment();
}

/*!
\patch 1.34 Date 12/10/2001 by Ondra
- Fixed: MP: Dedicated server memory leak.
(Caused by bug in sound management with sounds disabled).
*/

void SoundSystem8::AddWaveToList(WaveGlobal8 *wave)
{
  for (int i=0; i<_waves.Size(); i++)
  {
    if (!_waves[i])
    {
      _waves[i] = wave;
      return;
    }
  }
  _waves.Add(wave);
}

AbstractWave *SoundSystem8::CreateWave( const char *filename, bool is3D, bool prealloc )
{

  // TODO: use prealloc
  if( !_soundReady ) return NULL;
  // if the wave is preloaded, simply duplicate it
  char lowName[256];
  strcpy(lowName,filename);
  strlwr(lowName);
  if (!*lowName) return NULL; // empty filename

  WaveGlobal8 *wave=new WaveGlobal8(this,lowName,is3D);
  //LogF("Created %s: %x",lowName,wave);
  AddWaveToList(wave);
  return wave;
}

AbstractWave *SoundSystem8::CreateEmptyWave( float duration )
{
  // create a Wave8 with no buffers, only delay
  if( !_soundReady ) return NULL;
  WaveGlobal8 *wave=new WaveGlobal8(this,duration);
  //LogF("Created empty %x",wave);
  AddWaveToList(wave);
  return wave;
}

void SoundSystem8::Delete()
{
  if( !_soundReady ) return;

  #ifndef _XBOX
  _eaxFakeListener.Free();
  _eaxListener.Free();
  #endif

  _previewEffect.Free();
  _previewMusic.Free();
  _previewSpeech.Free();

  #if DIAG_LOAD
  while( _cache.Size()>0 )
  {
    const WaveDSCache8 &wc = _cache[0];
    LogF("Wave8 %s deleted from cache",(const char *)wc._name);
    _cache.Delete(0);
    //LogChannels("CacheSize");
  }
  #endif

  _cache.Clear(); 
  _cacheUniqueBuffers=0;
  _waves.Clear();

  // wait until all unloading waves are unloaded
  for( int i=0; i<_wavesUnload.Size(); i++ )
  {
    Wave8 *wave = _wavesUnload[i];
    if (!wave) continue;
    while (!wave->CheckUnload()) {}
  }
  // all unloading waves finished - release them
  _wavesUnload.Clear();

  #ifndef _XBOX
  if (_primary) _primary->Stop();
  _primary.Free();
  _listener.Free();
  #endif

  _ds.Free();
  _soundReady=false;

  #ifndef _XBOX
  _mixer.Free();
  #endif
}



void SoundSystem8::SetListener(Vector3Par pos, Vector3Par vel, Vector3Par dir, Vector3Par up)
{
  PROFILE_SCOPE_EX(snLis,sound);
  _listenerPos=pos;
  _listenerVel=vel;
  _listenerDir=dir.Normalized();
  _listenerUp=up.Normalized();
  if (_listenerDir.SquareSize()<0.1f) _listenerDir = VForward;
  if (_listenerUp.SquareSize()<0.1f) _listenerUp = VUp;
  if (!_soundReady) return;
  #ifndef _XBOX
  if (!_listener) return;
  #else
  if (!_ds) return;
  #endif

  #if DEFERRED
    const int flags=DS3D_DEFERRED;
  #else
    const int flags=DS3D_IMMEDIATE;
  #endif

  DWORD time = GlobalTickCount();
  // max 20 fps listener position update
  const DWORD minTime = 50;
  if (/*_hwEnabled ||*/ _listenerTime==0 || time-_listenerTime>minTime)
  {
    #if DEFERRED
      SND_COUNTER(wavLD,1);
    #else
      SND_COUNTER(wavLI,1);
    #endif
    DS3DLISTENER pars;
    pars.dwSize=sizeof(pars);
    pars.vPosition.x=pos[0],pars.vPosition.y=pos[1],pars.vPosition.z=pos[2];
    pars.vVelocity.x=vel[0],pars.vVelocity.y=vel[1],pars.vVelocity.z=vel[2];
    pars.vOrientFront.x=dir[0],pars.vOrientFront.y=dir[1],pars.vOrientFront.z=dir[2];
    pars.vOrientTop.x=up[0],pars.vOrientTop.y=up[1],pars.vOrientTop.z=up[2];
    pars.flDistanceFactor=1.0;
    pars.flRolloffFactor=1.0;
    pars.flDopplerFactor=1.0;
    #ifndef _XBOX
    _listener->SetAllParameters(&pars,flags);
    #else
    _ds->SetAllParameters(&pars,flags);
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

void SoundSystem8::Commit()
{
  if( !_soundReady ) return;

  PROFILE_SCOPE_EX(snCmt,sound);
  #if DEFERRED
    {
      PROFILE_SCOPE_EX(snCDS,sound);
      #ifndef _XBOX
      if (_listener)
      {
        _listener->CommitDeferredSettings();
      }
      #else
      if (_ds)
      {
        _ds->CommitDeferredSettings();
        DirectSoundDoWork();
      }
      #endif
      #if 0 //_ENABLE_PERFLOG
        static int maxScopeTime = 300;
        int scopeTime = PROFILE_SCOPE_NAME(snCDS).TimeSpentNorm();
        if (scopeTime>maxScopeTime)
        {
          // we detected interesting (slow) scope - report it
          LogF("Slow snCDS - %d",scopeTime);
          LogF(
            "  wavSV %d, wavSD %d, wavSI %d, wavLD %d, wavLI %d, wavSt %d, wavPl %d",
            _wavSV,_wavSD,_wavSI,_wavLD,_wavLI,_wavSt,_wavPl
          );
          saturateMax(maxScopeTime,scopeTime/2);
        }
        _wavSV=_wavSD=_wavSI=_wavLD=_wavLI=_wavSt=_wavPl=0;
      #endif
    }
  #endif
	#if _ENABLE_CHEATS 
		if (CHECK_DIAG(DESound))
		{
		  #if  defined(_XBOX)
        DSOUTPUTLEVELS levels;
        _ds->GetOutputLevels(&levels,true);
        const float maxRMS = 1<<23;
        DIAG_MESSAGE(
          100,Format(
            "RMS L %+4.1f, R %+4.1f",
            FloatToDb(levels.dwAnalogLeftTotalRMS/maxRMS),
            FloatToDb(levels.dwAnalogRightTotalRMS/maxRMS)
          )
        );
        DIAG_MESSAGE(
          100,Format(
            "Max L %+4.1f, R %+4.1f",
            FloatToDb(levels.dwAnalogLeftTotalPeak/maxRMS),
            FloatToDb(levels.dwAnalogRightTotalPeak/maxRMS)
          )
        );
      #endif
      DIAG_MESSAGE(100,Format(
        "Alloc: HW 3D %d, HW 2D %d, SW %d, Free %d, Cache %d",
        Wave8::_counters[Wave8::Hw3D],Wave8::_counters[Wave8::Hw2D],
        Wave8::_counters[Wave8::Sw],Wave8::_counters[Wave8::Free],
        _cache.Size()
      ));
      DIAG_MESSAGE(100,Format(
        "Play:  HW 3D %d, HW 2D %d, SW %d, Free %d",
        Wave8::_countPlaying[Wave8::Hw3D],Wave8::_countPlaying[Wave8::Hw2D],
        Wave8::_countPlaying[Wave8::Sw],Wave8::_countPlaying[Wave8::Free]
      ));
		}
  #endif
  _commited=true;
  for( int i=0; i<_waves.Size(); i++ )
  {
    Wave8 *wave = _waves[i];
    if (!wave) continue;
    if (wave->_wantPlaying)
    {
      if (wave->DoPlay())
      {
        wave->_wantPlaying=false;
      }
    }
  }
  // check all waves waiting for Unload
  for( int i=0; i<_wavesUnload.Size(); i++ )
  {
    Wave8 *wave = _wavesUnload[i];
    if (!wave) continue;
    if (wave->CheckUnload())
    {
      _wavesUnload.DeleteAt(i);
      i--;
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
      Wave8 *wave=_waves[i];
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

    #ifndef _XBOX
    if (_listener)
    {
      DS3DLISTENER pars;
      pars.dwSize=sizeof(pars);
      _listener->GetAllParameters(&pars);

      LogF
      (
        "    pos (%.1f,%.1f,%.1f) (%.1f,%.1f,%.1f)\n"
        "    dir (%.1f,%.1f,%.1f) (%.1f,%.1f,%.1f)\n"
        "    vel (%.1f,%.1f,%.1f) (%.1f,%.1f,%.1f)",
        _listenerPos[0],_listenerPos[1],_listenerPos[2],
        pars.vPosition.x,pars.vPosition.y,pars.vPosition.z,
        _listenerDir[0],_listenerDir[1],_listenerDir[2],
        pars.vOrientFront.x,pars.vOrientFront.y,pars.vOrientFront.z,
        _listenerVel[0],_listenerVel[1],_listenerVel[2],
        pars.vVelocity.x,pars.vVelocity.y,pars.vVelocity.z
      );
    }
    #else
    LogF
    (
      "    pos (%.1f,%.1f,%.1f)\n"
      "    dir (%.1f,%.1f,%.1f)\n"
      "    vel (%.1f,%.1f,%.1f)",
      _listenerPos[0],_listenerPos[1],_listenerPos[2],
      _listenerDir[0],_listenerDir[1],_listenerDir[2],
      _listenerVel[0],_listenerVel[1],_listenerVel[2]
    );
    #endif

  }
#endif

  // there may be some preview active
  if (_doPreview && GlobalTickCount()>=_previewTime)
  {
    float deltaT = 0.1;
    if (!_previewSpeech )
    {
      //float volume=GetSpeechVolCoef();
      // start first sound
      ParamEntryVal cfg = Pars >> "CfgSFX" >> "Preview" >> "speech"; 
      RStringB name = cfg[0];
      AbstractWave *wave = CreateWave(name,false,true);
      _previewSpeech = wave;
      if( wave )
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
        _doPreview = false; // error - no preview possible
      }
    }
    else if( PreviewAdvance(_previewSpeech,deltaT) && !_previewEffect )
    {
      // start second sound
      ParamEntryVal cfg = Pars >> "CfgSFX" >> "Preview" >> "effect"; 
      RStringB name = cfg[0];
      AbstractWave *wave = CreateWave(name,true,false);
      _previewEffect = wave;
      if( _previewEffect )
      {
        wave->EnableAccomodation(false);
        wave->SetSticky(true);
        wave->SetVolume(cfg[1],cfg[2],true);
        wave->SetPosition
        (
          _listenerPos+_listenerDir*50,_listenerVel,true
        );
        wave->Repeat(1);
        wave->Play();
      }
    }
    else if (PreviewAdvance(_previewEffect,deltaT) && !_previewMusic)
    {
      ParamEntryVal cfg = Pars >> "CfgSFX" >> "Preview" >> "music"; 
      RStringB name = cfg[0];
      AbstractWave *wave = CreateWave(name,false,true);
      _previewMusic=wave;
      if( wave )
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
    }
    else if (PreviewAdvance(_previewMusic,deltaT))
    {
      _doPreview = false;
      _previewSpeech.Free();
      _previewEffect.Free();
      _previewMusic.Free();
    }
  }
}

float SoundSystem8::Priority() const
{
  return 0.4;
}
RString SoundSystem8::GetDebugName() const
{
  return "Sounds";
}
size_t SoundSystem8::MemoryControlled() const
{
  size_t total = 0;
  for (int i=0; i<_cache.Size(); i++)
  {
    const WaveDSCache8 &item = _cache[i];
    #ifndef _XBOX
    total += item._header._size;
    #else
    if (item._data) total += item._data->Size();
    #endif
  }
  return total;
}
size_t SoundSystem8::FreeOneItem()
{
  size_t released = 0;
  int size=_cache.Size();
  if (size==0) return released;
  WaveDSCache8 &item = _cache[size-1];
  #ifndef _XBOX
  released = item._header._size;
  #else
  if (item._data) released = item._data->Size();
  #endif

  _cache.Delete(size-1);
  return released;
}


#ifndef _XBOX
inline float expVol( float vol )
{
  if (vol<=0) return 1e-10; // muted -> -200 db
  // vol in range 0..10
  //return exp(vol*0.5-5);
  //return exp((vol*2-20)*0.5);
  // max. volume is 0
  return exp((vol-10)*0.7);
}
#else
inline float expVol( float vol )
{
  if (vol<=0) return 1e-10; // muted -> -200 db
  // vol in range 0..10
  // max. volume is 10
  static float volCoef = 0.5f;
  return exp((vol-10)*volCoef);
}
#endif

void SoundSystem8::PreviewMixer()
{
  if (!_enablePreview) return;
  _doPreview=true;
  _previewTime = GlobalTickCount()+200;
}

void SoundSystem8::StartPreview()
{
  if (_previewMusic)
  {
    TerminatePreview();
  }
  PreviewMixer();
}

void SoundSystem8::TerminatePreview()
{
  _previewEffect.Free();
  _previewMusic.Free();
  _previewSpeech.Free();
  _doPreview = false;
}

void SoundSystem8::FlushBank(QFBank *bank)
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

void SoundSystem8::UpdateMixer()
{
  #ifndef _XBOX
    float waveVolExp = expVol(_waveVolWanted);
    float speechVolExp = expVol(_speechVolWanted);
    float musicVolExp = expVol(_musicVolWanted);

    float maxVolExp = floatMax(musicVolExp,waveVolExp,speechVolExp);
    float maxVol = floatMax(_waveVolWanted,_speechVolWanted,_musicVolWanted);
    
    _volumeAdjustEffect = waveVolExp/maxVolExp;
    _volumeAdjustSpeech = speechVolExp/maxVolExp;
    _volumeAdjustMusic = musicVolExp/maxVolExp;
  #else
    float maxVol = floatMax(_musicVolWanted,_waveVolWanted,_speechVolWanted);
    float waveVol = _waveVolWanted+10-maxVol;
    float speechVol = _speechVolWanted+10-maxVol;
    float musicVol = _musicVolWanted+10-maxVol;
    static float defSpeechCoef = 0.36f;
    static float defMusicCoef = 0.36f;
    _volumeAdjustEffect = expVol(waveVol);
    _volumeAdjustSpeech = expVol(speechVol)*defSpeechCoef;
    _volumeAdjustMusic = expVol(musicVol)*defMusicCoef;
  #endif

  //LogF("Update _volumeAdjust %g",_volumeAdjust);

  #ifndef _XBOX
  if (!_mixer) return;
  #endif
  
  for (int i=0; i<_waves.Size(); i++)
  {
    Wave8 *wave = _waves[i];
    if (wave) wave->SetVolumeAdjust(
      _volumeAdjustEffect,_volumeAdjustSpeech,_volumeAdjustMusic
    );
  }

  //LogF("Update %g,%g,%g, maxVol %g",_waveVolExp,_speechVolExp,_musicVolExp,maxVol);
  // TODO: scale accordingly to have both in same scale
  #ifndef _XBOX
  _mixer->SetWaveVolume(maxVol*0.1);
  #endif
}

float SoundSystem8::GetSpeechVolume()
{
  return _speechVolWanted;
}
void SoundSystem8::SetSpeechVolume(float val)
{
  _speechVolWanted=val;
  UpdateMixer();
}
float SoundSystem8::GetWaveVolume()
{
  return _waveVolWanted;
}
void SoundSystem8::SetWaveVolume(float val)
{
  _waveVolWanted=val;
  UpdateMixer();
}

float SoundSystem8::GetCDVolume() const
{
  return _musicVolWanted;
}
void SoundSystem8::SetCDVolume(float val)
{
  _musicVolWanted=val;
  UpdateMixer();
}


void SoundSystem8::Activate(bool){}

#endif

