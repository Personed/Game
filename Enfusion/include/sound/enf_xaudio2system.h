//-----------------------------------------------------------------------------
// File: enf_newsoundsystem.h
//
// Desc: SoundSystem interface
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_XAUDIO2SYSTEM_H
#define ENF_XAUDIO2SYSTEM_H

#include "enf_config.h"
#include "sound/enf_isoundsystem.h"
#include "sound/enf_xsoundshader.h"
#include "system/enf_mutex.h"
#include "system/enf_thread.h"
#include "system/enf_timer.h"
#include "common/enf_hashmap.h"
#include <es/Common/win.h>
#include <Xaudio2.h>
#include <X3daudio.h>
#include <xaudio2fx.h>

namespace enf
{
/** @defgroup SoundSystem Sound system
@{*/

class XAudio2System;
class XVoiceCallback;
class VFile;
class SoundShaderCollection;

const uint XAudio2SystemTypeID = XSoundSystemTypeID + 1;
const uint XAudio2SampleTypeID = XSoundSampleTypeID + 1;
const uint XAudio2ShaderTypeID = XSoundShaderTypeID + 1;
const uint XAudio2SoundEnvTypeID = XSoundEnvTypeID + 1;

#ifdef ENF_CONSOLE
	const uint XAUDIO2_DEFAULT_MAX_ALLOC = 1024 * 1024 * 18;
#else
	const uint XAUDIO2_DEFAULT_MAX_ALLOC = 1024 * 1024 * 64;
#endif

#ifdef ENF_XENON
	const uint MAX_SOUNDS_PER_SCENE = 24;
#else
	const uint MAX_SOUNDS_PER_SCENE = 40;
#endif

enum XAudio2SoundFlags
{
	SFX_PAUSED					= BITF(28),
	SFX_ENDING					= BITF(29),
	SFX_CHANGED_FREQUENCY	= BITF(30),
	SFX_CHANGED_VOLUME		= BITF(31)
};

//-----------------------------------------------------------------------------
class XAudio2SoundEnv: public XSoundEnv
{
public:
	ENF_DECLARE_ITEM(XAudio2SoundEnv, XSoundEnv);

	XAUDIO2FX_REVERB_I3DL2_PARAMETERS	m_Properties;

	~XAudio2SoundEnv()
	{
	}

	XAudio2SoundEnv(CStr name);
};

enum SampleType
{
	SMPT_UNKNOWN,
	SMPT_OGG,
	SMPT_WAV
};

struct CommonFormat
{
	ENF_DECLARE_SMALLOBJECT(CommonFormat);

	~CommonFormat();
	CommonFormat();
	CommonFormat(const CommonFormat& fmt);

	union
	{
		WAVEFORMATEX			m_Format;
		ADPCMWAVEFORMAT		m_ADPCMFormat;
		char						m_ADPCMBuffer[sizeof(ADPCMWAVEFORMAT) + 8 * 4];
#ifdef ENF_XENON
		XMA2WAVEFORMATEX		m_XMAFormat;
#endif
	};
};

//-----------------------------------------------------------------------------
class PUBLIC_API XAudio2Sample: public XSoundSample
{
public:
friend class XAudio2System;

	~XAudio2Sample();
	ubyte*			m_pData;
	uint				m_iLength;
	SampleType		m_SampleType;
	const CommonFormat*	m_pFormat;
	void InitBuffer(XAUDIO2_BUFFER& buff, uint flags, float start, uint beginLoop, uint endLoop, void* context);

public:
	ENF_DECLARE_ITEM(XAudio2Sample, XSoundSample);

	size_t GetSize() const
	{
		return m_iLength;
	}
	
	ENF_INLINE const WAVEFORMATEX& GetFormat() const
	{
		return m_pFormat->m_Format;
	}
	ENF_INLINE const CommonFormat* GetCommonFormat() const
	{
		return m_pFormat;
	}

	void Unload();
	XAudio2Sample(ISoundSystem* sys, CStr name);
	XSoundStreaming* CreateStreaming(ISoundSystem* sys);
};

//-----------------------------------------------------------------------------
class PUBLIC_API XAudio2Shader: public XSoundShader
{
protected:
	friend class XAudio2System;
	~XAudio2Shader();
	X3DAUDIO_CONE	m_Cone;

public:
	ENF_DECLARE_ITEM(XAudio2Shader, XSoundShader);
	bool InitFromSource(SoundShaderSource* source);

	XAudio2Shader(ISoundSystem* sys, CStr name);
};

//-----------------------------------------------------------------------------
ENF_INLINEFUNC Vector3 Vec3(const X3DAUDIO_VECTOR& v)
{
	return Vector3(v.x, v.z, v.y);
}

ENF_INLINEFUNC X3DAUDIO_VECTOR Vec3(const Vector3& v)
{
	X3DAUDIO_VECTOR vec;
	vec.x = v.x; vec.y = v.z; vec.z = v.y;
	return vec;
}

enum SoundSceneFlags
{
	SSF_UPDATE_REVERB = BITF(0),
	SSF_UPDATE_LISTENER = BITF(1),
	SSF_UPDATE_SFX_VOLUME = BITF(2),
	SSF_UPDATE_MUSIC_VOLUME = BITF(3),
	SSF_UPDATE_3D = BITF(4)
};

class StreamThread: public CThread
{
protected:
	XAudio2System*		m_pSystem;
public:
	void Run();
	void Kill();

	StreamThread(XAudio2System* sys): CThread(DEFAULT_STACK_SIZE), m_pSystem(sys)
	{
	}
};

//-----------------------------------------------------------------------------
struct SourceVoiceContainer
{
	ENF_DECLARE_SMALLOBJECT(SourceVoiceContainer);

	typedef Pair<IXAudio2SourceVoice*, uint> TSourceVoiceEntry;
	Array<TSourceVoiceEntry>	m_Voices;
	const CommonFormat*	m_pFormat;
	uint						m_iFlags;
};

#define COMPARE_FUNC(v0, v1) if(v0 < v1) return -1; if(v0 > v1) return 1;

struct FormatHashFunc
{
	uint operator()(const CommonFormat* fmt) const
	{
		uint hashval = fmt->m_Format.wFormatTag << 16;
		hashval |= fmt->m_Format.nChannels << 13;
		hashval |= (fmt->m_Format.nSamplesPerSec >> 8) << 5;
		hashval |= fmt->m_Format.cbSize;
		return hashval;
	}

	int compare(const CommonFormat* fmt1, const CommonFormat* fmt2) const
	{
		COMPARE_FUNC(fmt1->m_Format.wFormatTag, fmt2->m_Format.wFormatTag);
		COMPARE_FUNC(fmt1->m_Format.nChannels, fmt2->m_Format.nChannels);
		COMPARE_FUNC(fmt1->m_Format.nSamplesPerSec, fmt2->m_Format.nSamplesPerSec);
		COMPARE_FUNC(fmt1->m_Format.nBlockAlign, fmt2->m_Format.nBlockAlign);
		COMPARE_FUNC(fmt1->m_Format.wBitsPerSample, fmt2->m_Format.wBitsPerSample);
		COMPARE_FUNC(fmt1->m_Format.wFormatTag, fmt2->m_Format.wFormatTag);
		COMPARE_FUNC(fmt1->m_Format.cbSize, fmt2->m_Format.cbSize);

#ifdef ENF_XENON
		if(fmt1->m_Format.wFormatTag == WAVE_FORMAT_XMA2)
		{
			COMPARE_FUNC(fmt1->m_XMAFormat.NumStreams, fmt2->m_XMAFormat.NumStreams);
			COMPARE_FUNC(fmt1->m_XMAFormat.ChannelMask, fmt2->m_XMAFormat.ChannelMask);
			COMPARE_FUNC(fmt1->m_XMAFormat.SamplesEncoded, fmt2->m_XMAFormat.SamplesEncoded);
			COMPARE_FUNC(fmt1->m_XMAFormat.BytesPerBlock, fmt2->m_XMAFormat.BytesPerBlock);
			COMPARE_FUNC(fmt1->m_XMAFormat.PlayBegin, fmt2->m_XMAFormat.PlayBegin);
			COMPARE_FUNC(fmt1->m_XMAFormat.PlayLength, fmt2->m_XMAFormat.PlayLength);
			COMPARE_FUNC(fmt1->m_XMAFormat.LoopBegin, fmt2->m_XMAFormat.LoopBegin);
			COMPARE_FUNC(fmt1->m_XMAFormat.LoopLength, fmt2->m_XMAFormat.LoopLength);
			COMPARE_FUNC(fmt1->m_XMAFormat.LoopCount, fmt2->m_XMAFormat.LoopCount);
			COMPARE_FUNC(fmt1->m_XMAFormat.BlockCount, fmt2->m_XMAFormat.BlockCount);
		}
#endif
		return 0;
	}
};

struct SourceVoiceContainerHashFunc
{
	uint operator()(const SourceVoiceContainer* cont) const
	{
		uint hash = reinterpret_cast<uint>(cont->m_pFormat);
		hash |= cont->m_iFlags;
		return hash;
	}

	int compare(const SourceVoiceContainer* cont1, const SourceVoiceContainer* cont2) const
	{
		if(cont1->m_pFormat < cont2->m_pFormat)
			return -1;
		if(cont1->m_pFormat > cont2->m_pFormat)
			return 1;
		if(cont1->m_iFlags < cont2->m_iFlags)
			return -1;
		if(cont1->m_iFlags > cont2->m_iFlags)
			return 1;
		return 0;
	}
};

//-----------------------------------------------------------------------------
class PUBLIC_API XAudio2System: public ISoundSystem
{
public:
	ENF_DECLARE_ITEM(XAudio2System, XSoundSystem);

	struct SoundScene: public ISoundSystem::XSoundScene
	{
		X3DAUDIO_LISTENER		m_Listener;
		X3DAUDIO_CONE			m_ListenerCone;
		XAUDIO2FX_REVERB_I3DL2_PARAMETERS m_ReverbParameters;
		XAUDIO2FX_REVERB_PARAMETERS m_NativeReverbParameters;
		uint						m_iFlags;
		float						m_fSfxVolume;
		float						m_fMusicVolume;
		float						m_fLocalSfxVolume;
		float						m_fLocalMusicVolume;
		IUnknown*				m_pReverbEffect;
		IXAudio2SubmixVoice*	m_pReverbSubmixVoice;
		uint64					m_iLastTime;
		Set<XSoundRef*>		m_EndedSounds;

		SoundScene();
		~SoundScene();
	};

	enum SoundState
	{
		SS_NOTSTARTED, ///<Just created
		SS_PLAYING,		///<Playing
		SS_STOPPED,		///<Stopped because it's not hearable ATM
		SS_ENDED			///<Finished, waiting for remove
	};

	class Sound: public XSound
	{
	protected:
		friend class XAudio2System;
		friend class XVoiceCallback;

		mutable IL_INT32			m_State;
		SourceVoiceContainer*	m_pVoiceContainer;
		IXAudio2SourceVoice*		m_pSourceVoice;
		float							m_fTimePosition;
		uint							m_iBeginLoop;
		uint							m_iEndLoop;

		ushort						m_iCurrentRate;
		char							m_iPriority;
		ubyte							m_pad0;
		float							m_fVolume;

		bool UpdateVolume(SoundScene* scene, uint set);

	public:
		ENF_DECLARE_SMALLOBJECT(Sound);
		Sound(uint index, uint flags, uint shaderFlags): XSound(index, flags, shaderFlags),
			m_State(SS_NOTSTARTED), m_pVoiceContainer(NULL), m_pSourceVoice(NULL), m_fTimePosition(0), m_iBeginLoop(0), m_iEndLoop(0), m_iCurrentRate(0), m_iPriority(0), m_fVolume(1.0f)
		{
		}
		IXAudio2SourceVoice* GetVoice() const
		{
			return m_pSourceVoice;
		}
		XAudio2Sample* GetSample() const
		{
			return static_cast<XAudio2Sample*>(XSound::GetSample());
		}

		SoundState GetState() const
		{
			return (SoundState)IL_READ(m_State);
		}

		~Sound();
		virtual float GetPosition() const;
		virtual void SetPosition(float position, XSoundRef* ref);
		virtual SoundState Update(XAudio2System* sys, SoundScene* scene, XSoundRef* soundRef, double time, uint64 timeStamp, uint operSet)=0;
		int GetPriority() const { return (int)m_iPriority; }
	};

	class AmbientSound: public Sound
	{
	public:
		ENF_DECLARE_SMALLOBJECT(AmbientSound);

		virtual SoundType GetType() const
		{
			return ST_AMBIENT;
		}

		virtual SoundState Update(XAudio2System* sys, SoundScene* scene, XSoundRef* soundRef, double time, uint64 timeStamp, uint operSet);

		AmbientSound(uint nsound, uint flags, XAudio2Shader* shader);
		~AmbientSound();
	};

	class SpatialSound: public Sound
	{
	protected:
		friend class XAudio2System;

		const X3DAUDIO_CONE*	m_pCone;
		float					m_fRange;
		float					m_fMinRange;
		float					m_fDoppler;
		uint64				m_iLast3DUpdate;

		bool IsHearable(XAudio2System* sys, XSoundRef* ref, bool wasHearable);

	public:
		ENF_DECLARE_SMALLOBJECT(SpatialSound);

		bool UpdateTime(float timeSlice);
		virtual SoundType GetType() const
		{
			return ST_SPATIAL;
		}		
		float GetRange() const
		{
			return m_fRange;
		}
		float GetMinRange() const
		{
			return m_fMinRange;
		}

		float GetDoppler() const
		{
			return m_fDoppler;
		}

		virtual SoundState Update(XAudio2System* sys, SoundScene* scene, XSoundRef* soundRef, double time, uint64 timeStamp, uint operSet);

		SpatialSound(uint nsound, uint flags, XAudio2Shader* shader);
		~SpatialSound();
	};
	Array<CommonFormat*>	m_pFormats;
	HashMap<const CommonFormat*, uint, FormatHashFunc> m_pFormatsHashMap;

	Set<SourceVoiceContainer*>	m_SourceVoices;
	HashMap<SourceVoiceContainer*,SourceVoiceContainer*, SourceVoiceContainerHashFunc>	m_SourceVoicesHashMap;

	XAudio2System();
	virtual ~XAudio2System();
	bool OnChangeSettings(TiXmlElement* xmlnode);
	void OnInsert(TiXmlElement* xmlnode);

	uint NotificationProc();
	void AddStreamingSound(XSoundRef* ref);
	void RemoveStreamingSound(XSoundRef* ref);


	XSoundShader* CreateShader(CStr shaderName);

	VFile* OpenSampleFile(XAudio2Sample* sample, enum SampleType& stype);
	bool Load(XSoundSample* sample, uint shaderFlags);
	void EndSounds(uint sceneIndex);
	SoundHandle Play(uint sceneIndex, XSoundShader* sc, uint flags, XSoundSource* source, float position = 0.0f);
	SoundHandle PlayFromSource(uint sceneIndex, XSoundShader* shader, uint flags, XSoundSource* source, XAudioDataSource* dataSource = NULL);
	bool StopSound(SoundHandle id, uint flags);
	bool IsStopped(SoundHandle id) const;
	bool StartSound(SoundHandle id);

	void EndSounds(XSoundSource* ent);
	bool EndSound(SoundHandle id);
	bool ExitSoundLoop(SoundHandle id);

	bool SetFrequency(SoundHandle id, uint freq);
	uint GetSampleFrequency(SoundHandle id);

	bool SetVolume(SoundHandle id, float volume);
	bool SetPosition(SoundHandle id, float position);
	float GetPosition(SoundHandle id) const;
	float GetLength(SoundHandle id) const;
	bool SetSoundCone(SoundHandle id, const XSoundCone& cone);

	void SetSceneEffectsVolume(uint sceneIndex, float volume);
	void SetSceneMusicVolume(uint sceneIndex, float volume);
	void SetMasterMusicVolume(float volume);
	void SetMasterEffectsVolume(float volume);
	float GetMasterMusicVolume() const;
	float GetMasterEffectsVolume() const;

	bool LoadEnvironments(CStr name);
	uint FindEnvironment(CStr name) const;
	bool SetEnvironment(uint sceneIndex, uint env1, uint env2, float lerp);

	bool IsInitialized() const;
	bool Initialize( void* hWnd, uint speakers, uint dwPrimaryChannels, uint dwPrimaryFreq, uint dwPrimaryBitRate, bool eax );

	void SetListener(uint sceneIndex, const ListenerParameters& params);
	void UpdateSounds(uint sceneIndex);

	bool Start(IXAudio2SourceVoice* sourceVoice, uint flags, uint opetSet);
	void Stop(IXAudio2SourceVoice* sourceVoice, uint flags, uint opetSet);

	const CommonFormat* GetFormat(const CommonFormat& fmt);
	IXAudio2* GetXAudio2() const
	{
		return m_pXAudio2;
	}

private:
	friend class XVoiceCallback;
	friend class XAudio2Shader;
	friend class StreamThread;

	IXAudio2*					m_pXAudio2;
	IXAudio2MasteringVoice* m_pMasteringVoice;
	CMutex						m_StreamingSetMutex;
	CMutex						m_EndedSetMutex;
	CMutex						m_Mutex;
	CMutex						m_VoiceMutex;

	CEvent						m_StopStreaming;
	StreamThread				m_StreamThread;
	void DumpActiveSounds();

protected:

	friend class LoadSampleResource;

	XVoiceCallback*			m_pCallback;
	X3DAUDIO_HANDLE			X3DInstance;

	void EnqueueEndedSound(XSoundRef* ref);
	XSoundRef* DequeueEndedSound(uint sceneIndex);
	void CheckLists();

	mutable volatile long	m_iOperSet;
	uint	m_iOutputChannels;
	uint	m_iOutputChannelMask;
	IL_INT32	m_iNumActiveVoices;
	uint	m_iNumCreatedVoices;
	uint m_iUpdateFrame;

	float	m_fMasterSfxVolume;
	float	m_fMasterMusicVolume;

	Set<XSoundRef*>	m_StreamingSounds;

	bool LoadRIFF(XAudio2Sample* sample, CInputStream& stream);
	bool LoadOGG(XAudio2Sample* sample, CInputStream& stream);

	bool EnsureFreeVoice(uint sceneIndex, const XSoundShader* xshader, const XSoundSource* source);
	XAudio2Sample* FindOrCreateSample(CStr name);
	Sound* AllocSound(uint scene, SoundType st, uint flags, XAudio2Shader* shader, SoundHandle& handle);
	void FreeSound(XSoundRef* ref);
	IXAudio2SourceVoice* CreateSourceVoice(Sound* snd, SoundScene* scene, const CommonFormat* fmt, uint flags, bool useFilter, bool useReverb, uint operSet);
	void DestroySourceVoice(Sound* sound);

	bool IsHearable(float soundRange, uint sceneIndex, const Vector3& pos, bool wasHearable) const;

	ENF_INLINE const SoundScene* GetScene(uint sceneIndex) const
	{
		enf_assert(sceneIndex < MAX_SOUND_SCENES);
		return static_cast<SoundScene*>(m_pSoundScenes[sceneIndex]);
	}
	ENF_INLINE SoundScene* GetScene(uint sceneIndex)
	{
		enf_assert(sceneIndex < MAX_SOUND_SCENES);
		return static_cast<SoundScene*>(m_pSoundScenes[sceneIndex]);
	}

	void DoCommit()
	{
	}
	void Commit(uint operSet);
	uint GetOperationSet() const;
	void GetStats(XSoundSystemStats& stats);
	float GetSampleTimeLength(CStr sampleName);
	void InsertShaderClass(SoundShaderClass* sclass);
	XSoundStreaming* CreateStreamingCustomData(XAudioDataSource* sourceData);
};

//@}
}

#endif //ENF_XAUDIO2SYSTEM_H
