//-----------------------------------------------------------------------------
// File: enf_xaudio2sample.cpp
//
// Desc: Default XAudio2 sound system
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------

#include "sound/enf_xaudio2system.h"
#include "common/enf_streams.h"
#include "resource/enf_resource.h"
#include "resource/enf_iresourcemanager.h"
#include "enf_main.h"
#include "system/enf_vfilesystem.h"
#include <vorbis/codec.h>
#include <mmintrin.h>
#include <emmintrin.h>
//#include "stb_vorbis.c"

namespace enf
{
const uint NUM_STREAMING_BUFFERS = 3;
//const uint STREAMING_BUFFER_LENGTH = 2; ///<in seconds!


//----------------------------------------------------------------------
typedef char RIFFHunkID[4];

struct RIFFHunk
{
	RIFFHunkID	ID;
	uint			Length;
};

//-----------------------------------------------------------------------
uint BytesToSamples(uint bytes, uint bps)
{
	return (bytes << 3) / bps;
}

//-----------------------------------------------------------------------
uint SamplesToBytes(uint samples, uint bps)
{
	return (samples * bps) >> 3;
}

//-----------------------------------------------------------------------
class XWAVStreamContext
{
	public:
	uint				m_iDataHunk;
	uint				m_iDataLength;
	CommonFormat	m_Fmt;

	XWAVStreamContext()
	{
	}

	~XWAVStreamContext()
	{
	}

	uint Read(CInputStream& inStream, void* ptr, uint len)
	{
		uint read = inStream.Read(ptr, SamplesToBytes(len, m_Fmt.m_Format.wBitsPerSample));
		//do byteswap if needed
		if(m_Fmt.m_Format.wFormatTag == WAVE_FORMAT_PCM && m_Fmt.m_Format.wBitsPerSample == 16)
		{
			LE_SHORT_ARRAY((ushort*)ptr, read / 2);
		}

		return BytesToSamples(read, m_Fmt.m_Format.wBitsPerSample);
	}

	void Close()
	{
		m_iDataHunk = 0;
		m_iDataLength = 0;
	}

	bool Open(CInputStream& inStream)
	{
		RIFFHunk hunk;
		if(inStream.Read(&hunk, sizeof(hunk)) != sizeof(hunk))
			return false;

		if(memcmp(hunk.ID, "RIFF", sizeof(hunk.ID)) != 0)
			return false;

		uint srcLength = LE_INT(hunk.Length);

		RIFFHunkID id;
		if(inStream.Read(&id, sizeof(id)) != sizeof(id))
			return false;

		if(memcmp(id, "WAVE", sizeof(id)))
			return false;

		srcLength -= sizeof(id);

		uint srcFmtLength = 0;

		while(srcLength)
		{
			if(inStream.Read(&hunk, sizeof(hunk)) != sizeof(hunk))
				break;

			enf_assert(srcLength >= sizeof(hunk));
			srcLength -= sizeof(hunk);

			uint hunkLength = LE_INT(hunk.Length);

			enf_assert(srcLength >= hunkLength);
			srcLength -= hunkLength;

			if(memcmp(hunk.ID, "fmt ", sizeof(hunk.ID)) == 0)
			{
				enf_assert(hunkLength <= sizeof(m_Fmt));
				inStream.Read(&m_Fmt, hunkLength);
				srcFmtLength = hunkLength;
			}
			else if(memcmp(hunk.ID, "data", sizeof(hunk.ID)) == 0)
			{
				if(hunkLength == 0)
				{
					//dummy sample?
					return false;
				}
				m_iDataHunk = inStream.GetCurrentPosition();
				m_iDataLength = hunkLength;
				break;
			}
			else
			{
				if(inStream.Skip(hunkLength) != hunkLength)
					break;
			}
		}

		if(m_iDataLength == 0)
		{
			return false;
		}

		m_Fmt.m_Format.wFormatTag = LE_SHORT(m_Fmt.m_Format.wFormatTag);
		if(m_Fmt.m_Format.wFormatTag == WAVE_FORMAT_XMA2)
		{
			int a = 0;
		}
#ifdef ENF_XENON
		if(m_Fmt.m_Format.wFormatTag == WAVE_FORMAT_ADPCM)
			return false;
#endif

		m_Fmt.m_Format.nSamplesPerSec = LE_INT(m_Fmt.m_Format.nSamplesPerSec);
		m_Fmt.m_Format.nChannels = LE_SHORT(m_Fmt.m_Format.nChannels);
		m_Fmt.m_Format.wBitsPerSample = LE_SHORT(m_Fmt.m_Format.wBitsPerSample);
		m_Fmt.m_Format.cbSize = LE_SHORT(m_Fmt.m_Format.cbSize);
		m_Fmt.m_Format.nBlockAlign = LE_SHORT(m_Fmt.m_Format.nBlockAlign);
		m_Fmt.m_Format.nAvgBytesPerSec = LE_INT(m_Fmt.m_Format.nAvgBytesPerSec);
		//m_Fmt.m_Format.nBlockAlign = m_Format.wBitsPerSample / 8 * m_Format.nChannels;
		//m_Fmt.m_Format.nAvgBytesPerSec = m_Format.nSamplesPerSec * m_Format.nBlockAlign;
		if(m_Fmt.m_Format.wFormatTag == WAVE_FORMAT_PCM)
			m_Fmt.m_Format.cbSize = sizeof(WAVEFORMATEX);
		else if(m_Fmt.m_Format.wFormatTag == WAVE_FORMAT_ADPCM)
		{
			m_Fmt.m_Format.cbSize = 32;
			m_Fmt.m_ADPCMFormat.wSamplesPerBlock = LE_SHORT(m_Fmt.m_ADPCMFormat.wSamplesPerBlock);
			if(m_Fmt.m_ADPCMFormat.wSamplesPerBlock != 32 &&
				m_Fmt.m_ADPCMFormat.wSamplesPerBlock != 64 &&
				m_Fmt.m_ADPCMFormat.wSamplesPerBlock != 128 &&
				m_Fmt.m_ADPCMFormat.wSamplesPerBlock != 256 &&
				m_Fmt.m_ADPCMFormat.wSamplesPerBlock != 512)
			{
				LogF(LL_ERROR, "ADPCM sample with wrong wSamplesPerBlock == %d", (int)m_Fmt.m_ADPCMFormat.wSamplesPerBlock);
				return false;
			}

			m_Fmt.m_ADPCMFormat.wNumCoef = LE_SHORT(m_Fmt.m_ADPCMFormat.wNumCoef);
			for(uint n = 0; n < m_Fmt.m_ADPCMFormat.wNumCoef; n++)
			{
				m_Fmt.m_ADPCMFormat.aCoef[n].iCoef1 = LE_SHORT(m_Fmt.m_ADPCMFormat.aCoef[n].iCoef1);
				m_Fmt.m_ADPCMFormat.aCoef[n].iCoef2 = LE_SHORT(m_Fmt.m_ADPCMFormat.aCoef[n].iCoef2);
			}
		}
#ifdef ENF_XENON
		else if(m_Fmt.m_Format.wFormatTag == WAVE_FORMAT_XMA2)
		{
			m_Fmt.m_XMAFormat.NumStreams = LE_SHORT(m_Fmt.m_XMAFormat.NumStreams);
			m_Fmt.m_XMAFormat.ChannelMask = LE_INT(m_Fmt.m_XMAFormat.ChannelMask);
			m_Fmt.m_XMAFormat.SamplesEncoded = LE_INT(m_Fmt.m_XMAFormat.SamplesEncoded);
			m_Fmt.m_XMAFormat.BytesPerBlock = LE_INT(m_Fmt.m_XMAFormat.BytesPerBlock);
			m_Fmt.m_XMAFormat.PlayBegin = LE_INT(m_Fmt.m_XMAFormat.PlayBegin);
			m_Fmt.m_XMAFormat.PlayLength = LE_INT(m_Fmt.m_XMAFormat.PlayLength);
			m_Fmt.m_XMAFormat.LoopBegin = LE_INT(m_Fmt.m_XMAFormat.LoopBegin);
			m_Fmt.m_XMAFormat.LoopLength = LE_INT(m_Fmt.m_XMAFormat.LoopLength);
			m_Fmt.m_XMAFormat.BlockCount = LE_SHORT(m_Fmt.m_XMAFormat.BlockCount);
		}
#endif
		else
			return false; //???

		return true;
	}
};

//-----------------------------------------------------------------------
const uint OGG_PACKET_SIZE = 16384;

struct XOGGStreamContext
{
	ogg_sync_state		oy; // sync and verify incoming physical bitstream
	ogg_stream_state	os; // take physical pages, weld into a logical stream of packets
	ogg_page				og; // one Ogg bitstream page.  Vorbis packets are inside
	ogg_packet			op; // one raw packet of data for decode

	vorbis_info			vi; // struct that stores all the static vorbis bitstream settings
	vorbis_dsp_state	vd; // central working state for the packet->PCM decoder
	vorbis_block		vb; // local working space for packet->PCM decode
	bool					synthloop;

	uint ReadPacket(CInputStream& inStream, uint packetsize)
	{
		char *buffer = ogg_sync_buffer(&oy, packetsize);
		uint bytes = inStream.Read(buffer, packetsize);
		ogg_sync_wrote(&oy, bytes);
		return bytes;
	}
	XOGGStreamContext(): synthloop(false)
	{
		ogg_sync_init(&oy);
		memset(&os, 0, sizeof(os));
		memset(&vi, 0, sizeof(vi));
		memset(&vb, 0, sizeof(vb));
	}
	
	~XOGGStreamContext()
	{
		ogg_sync_clear(&oy);
	}

	void Close()
	{
		//don't change order. It depends on each other!
		vorbis_dsp_clear(&vd);
		vorbis_info_clear(&vi);
		ogg_stream_clear(&os);
		vorbis_block_clear(&vb);
	}

	bool InternalOpen(CInputStream& inStream, vorbis_comment& vc)
	{
		vorbis_info_init(&vi);
		ogg_sync_reset(&oy);

		uint bytes = ReadPacket(inStream, OGG_PACKET_SIZE);

		if(ogg_sync_pageout(&oy, &og) != 1)
		{
			return false;
		}

		ogg_stream_init(&os, ogg_page_serialno(&og));

		if(ogg_stream_pagein(&os, &og) < 0 ||
			ogg_stream_packetout(&os, &op) != 1 ||
			vorbis_synthesis_headerin(&vi, &vc, &op) < 0)
		{ 
			return false;
		}

		int i = 0;
		while(i < 2)
		{
			while(i < 2)
			{
				int result = ogg_sync_pageout(&oy, &og);
				if(result == 0)
					break;

				if(result == 1)
				{
					ogg_stream_pagein(&os, &og);

					while(i < 2)
					{
						result = ogg_stream_packetout(&os, &op);

						if(result == 0)
							break;

						if(result < 0)
						{
							return false;
						}
						vorbis_synthesis_headerin(&vi, &vc, &op);
						i++;
					}
				}
			}
			bytes = ReadPacket(inStream, OGG_PACKET_SIZE);

			if(bytes == 0 && i < 2)
			{
				return false;
			}
		}

		vorbis_synthesis_init(&vd, &vi);
		vorbis_block_init(&vd, &vb);
		return true;
	}

	//-----------------------------------------------------------------------
	bool Open(CInputStream& inStream)
	{
		vorbis_comment	vc;
		vorbis_comment_init(&vc);
		bool result = InternalOpen(inStream, vc);
		vorbis_comment_clear(&vc);
		return result;

	}

	//-----------------------------------------------------------------------
	uint Read(CInputStream& inStream, void* ptr, uint len)
	{
		int result;
		ogg_int16_t* convbuffer = (ogg_int16_t*)ptr;
		uint convoffset = 0;

		if(synthloop)
		{
			synthloop = false;
			goto synthloop;
		}

		// The rest is just a straight decode loop until end of stream
		while(true)
		{
			while(true)
			{
				result = ogg_sync_pageout(&oy,&og);
				if(result == 0)
					break; // need more data

				if(result < 0)
				{
					// missing or corrupt data at this page position
				}
				else
				{
					ogg_stream_pagein(&os, &og); // can safely ignore errors at this point
					while(1)
					{
						result = ogg_stream_packetout(&os, &op);

						if(result == 0)
							break;

						if(result > 0)
						{
							float** pcm;
							int samples;

							if(vorbis_synthesis(&vb, &op) == 0)
								vorbis_synthesis_blockin(&vd, &vb);

synthloop:;
							while((samples = vorbis_synthesis_pcmout(&vd, &pcm)) > 0)
							{
								//how many shorts we have to convert
								uint ssize = samples * vi.channels;

								if((ssize + convoffset) > len)
									samples = (len - convoffset) / vi.channels;

								if(vi.channels == 1) //only Mono
								{
									ogg_int16_t* ptr = convbuffer + convoffset;
									float* mono = pcm[0];

									int cnt1 = samples;

#ifdef ENF_SSE
									int al = (int)(uintptr_t)mono & 15;

									if(al != 0)
									{
										al = 4 - (al >> 2);
										cnt1 -= al;

										//unaligned data
										for(int j = 0; j < al; j++)
										{
											int val = *mono++ * 32767.f;
											*ptr++ = enf_limit(val, -32768, 32767);
										}
									}

									int cnt4 = cnt1 >> 2;

									__m128 max = _mm_set1_ps(32767.0f);
									__m128 min = _mm_set1_ps(-32768.0f);

									for(int j = 0; j < cnt4; j++)
									{									
										__m128 in = _mm_load_ps(mono);

										in = _mm_mul_ps(in, max);
										in = _mm_min_ps(in, max);
										in = _mm_max_ps(in, min);

										__m128i ints = _mm_cvtps_epi32(in);
										__m128i packed = _mm_packs_epi32(ints, _mm_setzero_si128());
										_mm_storel_epi64((__m128i*)ptr, packed);

										ptr += 4;
										mono += 4;
									}


									cnt1 &= 3;
#endif

									for(int j = 0; j < cnt1; j++)
									{
										int val = *mono++ * 32767.f;
										*ptr++ = enf_limit(val, -32768, 32767);
									}

									convoffset += samples;
								}
								else if(vi.channels == 2) //only Stereo
								{
									// convert floats to 16 bit signed ints (host order) and interleave
									float* left = pcm[0];
									float* right = pcm[1];

									for(int j = 0; j < (samples * 2); j += 2)
									{
										convbuffer[convoffset + j + 0] = enf_limit(enf_floorint(*left++ * 32767.f), -32768, 32767);
										convbuffer[convoffset + j + 1] = enf_limit(enf_floorint(*right++ * 32767.f), -32768, 32767);
									}

									convoffset += (samples * 2);
								}

								vorbis_synthesis_read(&vd, samples);

								if(convoffset >= len)
								{
									synthloop = true;
									goto getout;
								}
							}
						}
					}
					if(ogg_page_eos(&og))
						goto getout;
				}
			}
			uint bytes = ReadPacket(inStream, OGG_PACKET_SIZE);
			if(bytes == 0)
				break;

			if(convoffset >= len)
				break;
		}

getout:;
		return convoffset;
	}

	void SetupFormat(WAVEFORMATEX& fmt) const
	{
		fmt.wFormatTag = WAVE_FORMAT_PCM;
		fmt.nChannels = vi.channels;
		fmt.nSamplesPerSec = vi.rate;
		fmt.nAvgBytesPerSec = vi.rate * vi.channels * 2;
		fmt.wBitsPerSample = 16;
		fmt.nBlockAlign = vi.channels * 2;
		fmt.cbSize = sizeof(WAVEFORMATEX);
	}
};


//-----------------------------------------------------------------------
class Streaming: public XSoundStreaming
{
protected:
	void*				m_pBuffers[NUM_STREAMING_BUFFERS];
	uint				m_iCurrentBuffer;
	uint				m_iLength;
	CInputStream*	m_pInStream;
	const CommonFormat*	m_pFormat;

	virtual bool IsStarted() const
	{
		return m_iCurrentBuffer != INDEX_NOT_FOUND;
	}

	void InitBuffers(const CommonFormat* fmt)
	{
		//calc length of buffer
		//m_iNumSamples = STREAMING_BUFFER_LENGTH * avgSamplesPerSec;
		
		uint length = (BufferSizeInMSecs() * SamplesToBytes(fmt->m_Format.nSamplesPerSec, fmt->m_Format.wBitsPerSample)) / 1000;
		m_iLength = length;
		for(uint n = 0; n < NUM_STREAMING_BUFFERS; n++)
		{
			if(fmt->m_Format.wFormatTag == WAVE_FORMAT_XMA2)
				m_pBuffers[n] = MemoryManager::PhysicalAlloc(length, 2048, 0, __FILE__, __LINE__);
			else
				m_pBuffers[n] = MemoryManager::Alloc16(length, __FILE__, __LINE__);
		}
	}
	bool SetupFormat(CommonFormat& fmt) const
	{
		fmt = *m_pFormat;
		return true;
	}

public:

	StreamStatus SubmitBuffer(XSoundSystem* sys, XSoundRef* soundRef, bool loop, uint operSet)
	{
		XAudio2System::Sound* sound = static_cast<XAudio2System::Sound*>(soundRef->m_pSound);

		uint toRead = BytesToSamples(m_iLength, m_pFormat->m_Format.wBitsPerSample);

		uint currentBuffer = m_iCurrentBuffer;
		bool doStart = false;
		if(currentBuffer == -1)
		{
			doStart = true;
			currentBuffer = 0;
		}
		void* ptr = m_pBuffers[currentBuffer];

		uint readSamples;
		
		StreamStatus ss = FillData(ptr, toRead, loop, readSamples);

		if(ss == STS_NO_DATA)
		{
			return STS_NO_DATA;
		}

		m_iCurrentBuffer = (currentBuffer + 1) % 3;

		XAUDIO2_BUFFER buff;
		bool looped = readSamples == toRead;
		
		if(ss == STS_END)
		{
			//LogF(LL_DEBUG, "Sound stream is starving");
			//return true;
			//we are not able to handle zero length buffer. Fake it with one-sample of silence :-(
			readSamples = m_pFormat->m_Format.nChannels;
			looped = false;
			uint bytes = SamplesToBytes(readSamples, m_pFormat->m_Format.wBitsPerSample);
			memset(ptr, 0, bytes);
			buff.AudioBytes = bytes;
		}
		else
		{
			buff.AudioBytes = SamplesToBytes(readSamples, m_pFormat->m_Format.wBitsPerSample);
		}
		buff.pAudioData = (BYTE*)ptr;

		if(!looped)
		{
			buff.Flags = XAUDIO2_END_OF_STREAM;
			buff.pContext = soundRef;
		}
		else
		{
			buff.Flags = 0;
			buff.pContext = NULL;
		}
		
		buff.LoopBegin = 0;
		buff.LoopLength = 0;
		buff.LoopCount = XAUDIO2_NO_LOOP_REGION;
		buff.PlayBegin = 0;
		buff.PlayLength = readSamples / m_pFormat->m_Format.nChannels;

		sound->GetVoice()->SubmitSourceBuffer(&buff);
		if(doStart)
		{
			static_cast<XAudio2System*>(sys)->Start(sound->GetVoice(), 0, operSet);
		}
		return looped ? STS_DATA : STS_END;
	}

	Streaming(XAudio2Sample* sample, CInputStream* inStream): XSoundStreaming(), m_iCurrentBuffer(-1), m_iLength(0), m_pInStream(inStream), m_pFormat(NULL)
	{
		enf_assert(sample != NULL);
		enf_assert(inStream != NULL);
		memset(m_pBuffers, 0, sizeof(m_pBuffers));
		if(sample)
			m_pFormat = sample->m_pFormat;
	}
	Streaming(): XSoundStreaming(), m_iCurrentBuffer(-1), m_iLength(0), m_pInStream(NULL)
	{
		memset(m_pBuffers, 0, sizeof(m_pBuffers));
	}
	~Streaming()
	{
		for(uint n = 0; n < NUM_STREAMING_BUFFERS; n++)
		{
			if(m_pFormat->m_Format.wFormatTag == WAVE_FORMAT_XMA2)
				MemoryManager::PhysicalFree(m_pBuffers[n]);
			else
				MemoryManager::Free16(m_pBuffers[n]);
		}

		SAFE_DELETE(m_pInStream);
	}
};

//-----------------------------------------------------------------------
class StreamingCustomData: public Streaming
{
protected:
	ComPtr<XAudioDataSource>	m_pSourceData;

public:
	StreamStatus FillData(void* buffer, uint numSamples, bool loop, uint& outNumSamples)
	{
		if(m_pSourceData->DataReady(numSamples) != numSamples)
		{
			outNumSamples = 0;
			return STS_NO_DATA;
		}

		outNumSamples = m_pSourceData->FillData(buffer, numSamples, loop);
		return outNumSamples ? STS_DATA : STS_END;
	}
	
	uint BufferSizeInMSecs() const
	{
		return m_pSourceData->BufferSizeInMSecs();
	}

	StreamingCustomData(XAudio2System* sys, XAudioDataSource* source): Streaming(), m_pSourceData(source)
	{
		source->AddRef();
		uint channels, rate;
		m_pSourceData->GetSoundInfo(channels, rate);
		CommonFormat fmt;
		fmt.m_Format.wFormatTag = WAVE_FORMAT_PCM;
		fmt.m_Format.nChannels = channels;
		fmt.m_Format.nSamplesPerSec = rate;
		fmt.m_Format.nAvgBytesPerSec = rate * channels * 2;
		fmt.m_Format.nBlockAlign = channels * 2;
		fmt.m_Format.wBitsPerSample = 16;
		fmt.m_Format.cbSize = sizeof(WAVEFORMATEX);
		m_pFormat = sys->GetFormat(fmt);

		InitBuffers(m_pFormat);
	}

	~StreamingCustomData()
	{
	}
};

//-----------------------------------------------------------------------
XSoundStreaming* XAudio2System::CreateStreamingCustomData(XAudioDataSource* sourceData)
{
	return ENF_NEW StreamingCustomData(this, sourceData);
}

//-----------------------------------------------------------------------
class StreamingOGG: public Streaming
{
protected:
	XOGGStreamContext	m_Context;
	bool m_IsOpened;

public:
	StreamStatus FillData(void* buffer, uint numSamples, bool loop, uint& outNumSamples)
	{
		uint read = m_Context.Read(*m_pInStream, buffer, numSamples);
		if(read < numSamples && loop)
		{
			m_Context.Close();
			m_pInStream->Seek(0);
			m_Context.Open(*m_pInStream);
			uint toRead = numSamples - read;
			m_Context.Read(*m_pInStream, (ogg_int16_t*)buffer + read, toRead);
			outNumSamples = numSamples;
			return numSamples ? STS_DATA : STS_END;
		}
		outNumSamples = read;
		return read ? STS_DATA : STS_END;
	}

	StreamingOGG(XAudio2Sample* sample, CInputStream* inStream): Streaming(sample, inStream), m_IsOpened(false)
	{
		if(m_Context.Open(*inStream))
		{
			CommonFormat fmt;
			m_Context.SetupFormat(fmt.m_Format);
			m_pFormat = sample->m_pFormat = static_cast<XAudio2System*>(sample->GetOwner())->GetFormat(fmt);
			InitBuffers(sample->m_pFormat);
			m_IsOpened = true;
		}

	}
	~StreamingOGG()
	{
		m_Context.Close();
	}
};

//-----------------------------------------------------------------------
class StreamingWAV: public Streaming
{
protected:
	bool m_IsOpened;
	XWAVStreamContext	m_Context;

public:
	StreamStatus FillData(void* buffer, uint numSamples, bool loop, uint& outNumSamples)
	{
		uint read = m_Context.Read(*m_pInStream, buffer, numSamples);
		if(read < numSamples && loop)
		{
			m_pInStream->Seek(m_Context.m_iDataHunk);
			uint toRead = numSamples - read;
			m_Context.Read(*m_pInStream, (ubyte*)buffer + SamplesToBytes(read, m_Context.m_Fmt.m_Format.wBitsPerSample), toRead);
			outNumSamples = numSamples;
			return numSamples ? STS_DATA : STS_END;
		}
		outNumSamples = read;
		return read ? STS_DATA : STS_END;
	}

	StreamingWAV(XAudio2Sample* sample, CInputStream* inStream): Streaming(sample, inStream), m_IsOpened(false)
	{
		if(m_Context.Open(*inStream))
		{
			m_pFormat = sample->m_pFormat = static_cast<XAudio2System*>(sample->GetOwner())->GetFormat(m_Context.m_Fmt);
			InitBuffers(sample->m_pFormat);
			m_IsOpened = true;
		}

	}
	~StreamingWAV()
	{
	}
};

//-----------------------------------------------------------------------------
XAudio2Sample::XAudio2Sample(ISoundSystem* sys, CStr name):
	XSoundSample(sys),
	m_pData(NULL),
	m_iLength(0),
	m_SampleType(SMPT_UNKNOWN),
	m_pFormat(NULL)
{
	m_strName = name.cstr();
	MemStats::m_iSoundSystem += sizeof(*this);
}

//-----------------------------------------------------------------------
XAudio2Sample::~XAudio2Sample()
{
	MemStats::m_iSoundSystem -= sizeof(*this);
	Unload();
}

//-----------------------------------------------------------------------
void XAudio2Sample::Unload()
{
	enf_assert(!IsPlaying());
	AssertMainThread();
	ClearFlags(SSF_LOADED);
	if(m_pData)
	{
		if(m_pOwner)
		{
			m_pOwner->LRURemove(this);
		}

		if(GetFormat().wFormatTag == WAVE_FORMAT_XMA2)
			MemoryManager::PhysicalFree(m_pData);
		else
			MemoryManager::Free16(m_pData);
		m_pData = NULL;
	}
}

//-----------------------------------------------------------------------
void XAudio2Sample::InitBuffer(XAUDIO2_BUFFER& buff, uint flags, float start, uint beginLoop, uint endLoop, void* context)
{
	buff.Flags = XAUDIO2_END_OF_STREAM;
	buff.AudioBytes = m_iLength;
	buff.pAudioData = m_pData;


	const CommonFormat* fmt = GetCommonFormat();
#ifdef ENF_XENON
	if(fmt->m_Format.wFormatTag == WAVE_FORMAT_XMA2)
	{
		beginLoop &= ~0x7f;
		endLoop &= ~0x7f;
		beginLoop = fmt->m_XMAFormat.LoopBegin;
		endLoop = beginLoop + fmt->m_XMAFormat.LoopLength;
	}
#endif

	if(start != 0)
	{
		//change start position			
		uint begin = enf_floorint(start * fmt->m_Format.nSamplesPerSec);
		//for XMA, we have to align on 128 sample boundary
		if(fmt->m_Format.wFormatTag == WAVE_FORMAT_XMA2)
		{
			begin &= ~0x7f;
		}
		//hack :( We cannot start loop after loop region
		//TODO: wrap start time inside loop!
		if((flags & SFX_ONCE) == 0 && (beginLoop != 0 && endLoop != 0) && beginLoop < begin)
		{
			begin = beginLoop;
		}

		buff.PlayBegin = begin;
/*		if(fmt->m_Format.wFormatTag == WAVE_FORMAT_XMA2)
			buff.PlayLength = (fmt->m_XMAFormat.PlayLength - begin) & ~0x7f;
		else
			buff.PlayLength = m_iNumSamples - begin;*/
	}
	else
	{
		buff.PlayBegin = 0;
	}
	buff.PlayLength = 0;


	if(flags & SFX_ONCE)
	{
		buff.LoopBegin = 0;
		buff.LoopLength = 0;
		buff.LoopCount = XAUDIO2_NO_LOOP_REGION;
	}
	else
	{
		buff.LoopBegin = beginLoop;
		buff.LoopLength = endLoop - beginLoop;
		buff.LoopCount = XAUDIO2_LOOP_INFINITE;
	}
	buff.pContext = context;
}

//-----------------------------------------------------------------------
bool XAudio2System::LoadRIFF(XAudio2Sample* sample, CInputStream& stream)
{
	XWAVStreamContext ctx;

	if(ctx.Open(stream))
	{
		stream.Seek(ctx.m_iDataHunk);

		sample->m_pFormat = GetFormat(ctx.m_Fmt);
		if(ctx.m_Fmt.m_Format.wFormatTag == WAVE_FORMAT_XMA2)
			sample->m_pData = (ubyte*)MemoryManager::PhysicalAlloc(ctx.m_iDataLength, 2048, 0, __FILE__, __LINE__);
		else
			sample->m_pData = (ubyte*)MemoryManager::Alloc16(ctx.m_iDataLength, __FILE__, __LINE__);

		sample->m_iLength = ctx.m_iDataLength;
		uint totalSamples = BytesToSamples(ctx.m_iDataLength, ctx.m_Fmt.m_Format.wBitsPerSample);
		ctx.Read(stream, sample->m_pData, totalSamples);
		uint numSamples = totalSamples / ctx.m_Fmt.m_Format.nChannels;
		sample->m_iNumSamples = numSamples;
		sample->m_fTimeLength = (float)numSamples / (float)ctx.m_Fmt.m_Format.nSamplesPerSec;
		ctx.Close();
		return true;
	}
	//if(shader->GetFlags() & SF_3D && channels != 1)
	//	return false;

	return false;
}

//-----------------------------------------------------------------------
bool XAudio2System::LoadOGG(XAudio2Sample* sample, CInputStream& stream)
{
	XOGGStreamContext context;

	uint size = stream.GetSize();
	if(context.Open(stream))
	{
		CommonFormat fmt;
		context.SetupFormat(fmt.m_Format);
		sample->m_pFormat = GetFormat(fmt);

		uint bitrate = context.vi.bitrate_nominal;
		if(context.vi.bitrate_upper == 0 && context.vi.bitrate_lower == 0)
			bitrate = (bitrate * 4) / 5;
		else
			bitrate = enf_max(context.vi.bitrate_nominal, context.vi.bitrate_upper);

		uint seconds = (size * 8 / bitrate) + 1;
		uint numSamples = seconds * fmt.m_Format.nSamplesPerSec * fmt.m_Format.nChannels;

		numSamples = (numSamples + 65535) & 0xffff0000;

#if defined(ENF_XENON) || defined(ENF_PS3)
		//4MB sample is really too much. Use XMA, or stream!
		uint memEstimate = numSamples * sizeof(ogg_int16_t);
		if(memEstimate > (4096 * 1024))
		{
			enf::LogF(LL_ERROR, "Non-stramed sample %s is too big %d", sample->GetName().cstr(), memEstimate);
			context.Close();
			return false;
		}

#endif
		ogg_int16_t* ptr = (ogg_int16_t*)MemoryManager::Alloc(numSamples * sizeof(ogg_int16_t), __FILE__, __LINE__);
		uint totalSamples = 0;
		while(true)
		{
			int toRead = 65536;

			if((totalSamples + toRead) > numSamples)
			{
				ptr = (ogg_int16_t*)REALLOC(ptr, (numSamples + toRead) * sizeof(ogg_int16_t));
				numSamples += toRead;
			}

			int read = context.Read(stream, ptr + totalSamples, toRead);
			totalSamples += read;
			if(read < toRead)
			{
				break;
			}
		}

		sample->m_pData = (ubyte*)MemoryManager::Alloc16(totalSamples * sizeof(ogg_int16_t), __FILE__, __LINE__);
		memcpy(sample->m_pData, ptr, totalSamples * sizeof(ogg_int16_t));
		MemoryManager::Free(ptr);

		sample->m_iLength = totalSamples * sizeof(ogg_int16_t);
		sample->m_iNumSamples = totalSamples / fmt.m_Format.nChannels;
		sample->m_fTimeLength = (float)(totalSamples / context.vi.channels) / (float)context.vi.rate;

		context.Close();
		return true;
	}
	context.Close();
	return false;
}

//-----------------------------------------------------------------------
VFile* XAudio2System::OpenSampleFile(XAudio2Sample* sample, SampleType& stype)
{
	enf_assert(sample);

	VFile* vf = NULL;

	if(sample->GetFileHandle() == BAD_FILE_HANDLE)
	{
		char name[MAX_FILENAME_LENGTH];
		int nlen = sample->GetName().Length();
		memcpy(name, sample->GetName().cstr(), nlen);
		name[nlen++] = '.';

		static const char* types[] = {"ogg", "xma", "wav", NULL};
		static const SampleType stypes[] = {SMPT_OGG, SMPT_WAV, SMPT_WAV, SMPT_UNKNOWN};

		for(int n = 0; types[n]; n++)
		{
			strcpy(name + nlen, types[n]);

			vf = GetFileSystem()->Open(name, FILEMODE_READ);

			if(vf != NULL)
			{
				stype = stypes[n];
				break;
			}
		}

		if(vf)
		{
			sample->m_SampleType = stype;
			sample->SetFileHandle(vf->GetHandle());
			return vf;
		}
	}
	else
	{
		vf = GetFileSystem()->Open(sample->GetFileHandle(), FILEMODE_READ);
		stype = sample->m_SampleType;
	}


	return vf;
}

//-----------------------------------------------------------------------
XSoundStreaming* XAudio2Sample::CreateStreaming(ISoundSystem* _sys)
{
	XAudio2System* sys = XAudio2System::Cast(_sys);
	XSoundStreaming* streaming = NULL;

	SampleType stype;

	VFile* vf = sys->OpenSampleFile(this, stype);
	if(vf)
	{
		switch(stype)
		{
		case SMPT_OGG:
			streaming = ENF_NEW StreamingOGG(this, ENF_NEW CFileInputStream(vf));
			break;
		case SMPT_WAV:
			streaming = ENF_NEW StreamingWAV(this, ENF_NEW CFileInputStream(vf));
			break;
		}
	}

	return streaming;
}

//-----------------------------------------------------------------------
class PUBLIC_API LoadSampleResource: public LoadResource
{
protected:
	ComPtr<XAudio2Sample>	m_pSample;
	bool res;

public:
	ENF_DECLARE_ITEM(LoadSampleResource, LoadResource);
	ENF_DECLARE_SMALLOBJECT(LoadSampleResource);

	size_t GetDataSize() const
	{
		return m_pSample->GetSize();
	}
	LoadSampleResource(XAudio2Sample* sample, int loadPriority): LoadResource(sample), m_pSample(sample), res(false)
	{
		enf_assert(sample);
		sample->AddRef();
	}
	~LoadSampleResource()
	{
		m_pResource->FinishLoading(this);
	}
	CStr GetName() const
	{
		return m_pSample->GetName();
	}

	bool DoLoad()
	{
		SampleType stype;

		XAudio2System* sys = static_cast<XAudio2System*>(m_pSample->GetOwner());
		VFile* vf = sys->OpenSampleFile(m_pSample, stype);

		if(vf == NULL)
		{
			return false;
		}

		res = false;
		if(vf)
		{
			CFileInputStream stream(vf);

			switch(stype)
			{
			case SMPT_OGG:
				res = sys->LoadOGG(m_pSample, stream);
				break;

			default:
				res = sys->LoadRIFF(m_pSample, stream);
				break;
			}
		}

		if(!res)
			LogF(LL_ERROR, "Cannot load sample '%s'", m_pSample->GetName());
		return res;
	}
	void Apply()
	{
		if(res)
		{
			AssertMainThread();
			XAudio2System* sys = static_cast<XAudio2System*>(m_pSample->GetOwner());
			m_pSample->SetLoaded();
			m_pSample->Touch(0);
			sys->LRUInsert(m_pSample);
		}
		else
		{
			m_pSample->SetFlags(SSF_CANNOT_LOAD);
		}
	}
};

//-----------------------------------------------------------------------
bool XAudio2System::Load(XSoundSample* _sample, uint shaderFlags)
{
	enf_assert(_sample);

	//is already loaded/loading
	if(_sample->IsLoaded())
		return true;
	//we tried it once and we failed
	if(_sample->CannotLoad())
		return false;
	//already submited
	if(_sample->IsLoading())
		return true;

	XAudio2Sample* sample = XAudio2Sample::Cast(_sample);

	SampleType stype;
	bool res = false;

	if(m_pXAudio2 == NULL)
	{
		//don't need to fuck around when we don't have device to play it
		sample->SetLoaded();
		return true;
	}
#ifdef ENF_FORCE_SYNC
	shaderFlags |= SSFX_FORCELOAD;
#endif
	if(shaderFlags & SSFX_FORCELOAD)
	{
		VFile* vf = OpenSampleFile(sample, stype);
		
		if(vf)
		{
			CFileInputStream stream(vf);

			switch(stype)
			{
			case SMPT_OGG:
				res = LoadOGG(sample, stream);
				break;

			default:
				res = LoadRIFF(sample, stream);
				break;
			}
		}

		if(res)
		{
			sample->SetLoaded();
			//LRUInsert(sample);
		}
		else
		{
			sample->SetFlags(SSF_CANNOT_LOAD);
			sample->ClearFlags(SSF_LOADED);
		}
	}
	else
	{
		LoadSampleResource* ld = ENF_NEW LoadSampleResource(sample, 0);
		GetResources()->AddLoadResource(ld);
	}

	return res;
}


}
