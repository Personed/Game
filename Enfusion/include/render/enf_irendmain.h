//-----------------------------------------------------------------------------
// File: render/enf_irendmain.h
//
// Desc: Main render class, implementation
//
// Copyright (c) 2000-2006 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_IRENDERMAIN_H
#define ENF_IRENDERMAIN_H

#include "enf_config.h"
#include "render/enf_renderresources.h"
#include "render/enf_rendmain.h"
#include "common/enf_set.h"
#include "common/enf_hashmap.h"
#include "system/enf_mutex.h"
#include "system/enf_thread.h"


namespace enf
{


struct RenderDevice: public RenderDeviceInfo
{
	// Modes for this device
	Array<RenderModeInfo>	m_Modes;
};

struct RenderAdapter: public RenderAdapterInfo
{
	ENF_DECLARE_MMOBJECT(RenderAdapter);
	// Devices for this adapter
	SArray<RenderDevice, 4>	m_Devices;
};

const uint MAX_LIMIT_FRAMES = 2;

//-----------------------------------------------------------------------
struct ShaderHashFunc
{
	unsigned int operator()(const ShaderDef* sh) const
	{
		uint hashval = 0;

		hashval = HashPointer(sh->GetName().cstr());
		hashval += sh->GetDefines().GetHashValue();

		return hashval;
	}

	unsigned int compare(const ShaderDef* sh1, const ShaderDef* sh2) const
	{
		if(sh1->GetName().cstr() < sh2->GetName().cstr())
			return -1;
		
		if(sh1->GetName().cstr() > sh2->GetName().cstr())
			return 1;

		if(sh1->GetProfile() < sh2->GetProfile())
			return -1;
		
		if(sh1->GetProfile() > sh2->GetProfile())
			return 1;

		return sh1->GetDefines().Compare(sh2->GetDefines());
	}
};


//-----------------------------------------------------------------------
class PUBLIC_API IRenderer: public Renderer
{
	friend class DeviceResource;
	friend class Texture;
	friend class RenderShader;
	friend class VertexShader;
	friend class PixelShader;
	friend class RenderTarget;
	friend class VertexBuffer;
	friend class IndexBuffer;

protected:
	#ifdef ENF_STEREO_VISION
		struct NVStereoImageHeader
		{
			uint m_iSignature;
			uint m_iWidth;
			uint m_iHeight;
			uint m_iBPP;
			uint m_iFlags;

			void Init(uint width, uint height, uint bpp)
			{
				m_iSignature = 0x4433564e;	//= NV3D
				m_iWidth = width;
				m_iHeight = height;
				m_iBPP = bpp;
				m_iFlags = 0; //SIH_SWAP_EYES 0x00000001, SIH_SCALE_TO_FIT 0x00000002, SIH_SCALE_TO_FIT2 0x00000004
			}
		};
	#endif

	struct RTStack
	{
#if defined(ENF_OGL)
		GLuint			m_pDepth;
		GLuint			m_pRender;
		GLuint			m_iFB;
		GLuint			m_iRB;
		GLuint			m_iDB;
#endif
		uint				m_iRTWidth;
		uint				m_iRTHeight;
		RenderViewport	m_Viewport;

		RTStack()
#if defined(ENF_OGL)
		: m_pDepth(0),
		m_pRender(0),
		m_iFB(0), m_iRB(0), m_iDB(0)
#endif
		{
		}
	};

	//-----------------------------------------------------------------------
	//! Resource list
	DeviceResource*m_pResources;

	static const uint MAX_RT_DEPTH = 8;

	//! Render Target Stack
	RTStack			m_RTStack[MAX_RENDER_TARGETS][MAX_RT_DEPTH];
	ENF_INLINE bool ValidateMRT() const;

	SArray<RenderAdapter*,4>	m_Adapters;
	SArray<FSAADef, 8>				m_FSAADefs;

	uint				m_iFSAA;
	uint				m_iFSAAQuality;
	uint				m_iSuspended;
	uint				m_iUsed;
	IL_INT32		m_iCancelSuspend;
	bool				m_bReady;

	//CEvent			m_Cancelled;
	//CThread			m_SuspendThread;
	ENF_INLINE bool CanBeUsed() const
	{
		return (m_iUsed > 0 || m_iSuspended == 0);
	}

#ifdef ENF_HWCURSOR
	struct CursorInfo
	{
		_D3DSurface* surface;
		int x, y;
	};

	Array<CursorInfo>	m_Cursors;
	uint					m_iCurrentCursor;
	bool					m_bCursorVisible;
#endif

	Set<RenderShader*>	m_Shaders;
	bool								m_bNoShaderGen;

	RenderQuery* m_pLimiterQueries[MAX_LIMIT_FRAMES + 1];
	uint m_iLimiterBottom, m_iLimiterTop;

	CMutex m_Mutex;
	CMutex m_DelayReleaseMutex;

	Set<DeviceResource*> m_DelayReleaseResources;

	uint m_iShaderCompile;

	HashMap<const ShaderDef*, RenderShader*, ShaderHashFunc>	m_ShadersHash;
	~IRenderer();
	void BeforeReset();
	void AfterReset();
	ERESULT BuildDeviceList();
	bool LoadShader(RenderShader* sh, const ShaderDef& def);
	
#ifdef ENF_DONE
	void FindGPUInfo();
#else
	void FindGPUInfo()
	{}
#endif

	#ifdef ENF_STEREO_VISION
		void FindStereoInfo();
		void DisableStereo();
	#endif

public:
	IRenderer();

	void GenerateOfflineShaders();
	bool Init(void* hwnd);
	ERESULT Create();

#ifdef ENF_DONE
	bool SetMode(uint adapter, uint device, uint mode, bool windowed);

#else	
	bool SetMode(uint adapter, uint device, uint mode, bool windowed) {return true; }
#endif

	bool GetAdapter(uint adapter, RenderAdapterInfo *adapterinfo);
	bool GetDevice(uint adapter, uint device, RenderDeviceInfo *deviceinfo);
	uint AddMode(uint adapter, uint device, uint width, uint height);
	bool GetMode(uint adapter, uint device, uint mode, RenderModeInfo *modeinfo);
	void Present();
	ERESULT BeginScene();
	void EndScene();
	void Suspend();
	void Resume();
	void BeginUse();
	void EndUse();

	uint GetNumFSAADefs() const;
	const FSAADef& GetFSAADef(uint index) const;
	bool SetFSAA(uint fsaa, uint quality);
	void DumpResources() const;

	#ifdef ENF_STEREO_VISION
		void CaptureStereoImage();
	#endif

	void SetRenderTarget(uint rtindex, RenderTarget *rt, uint face = 0, uint flags = 0);
	void PushRenderTarget(uint rtindex, RenderTarget *rt, uint face = 0, uint flags = 0);
	void PopRenderTarget(uint rtindex);
	RenderShader* GetShader(CStr name, ProfileType profile, const Define* defines = NULL, uint numdefines = 0);
	void BeginShaderCompile();
	void EndShaderCompile();

	void ReloadShaders(const char* srcshader);

	ERESULT AddResource(DeviceResource* res);
	ERESULT RemoveResource(DeviceResource* res);
	void DelayRelease(DeviceResource* res);
	void FlushDelayedResources();

	bool ExistsResource(const DeviceResource *res) const;

	RenderTarget* CreateRenderTarget(uint width, uint height, TextureFormat rtfmt, TextureFormat depthfmt, FSAAType fsaa, uint usage, uint edramoffset, uint texFlags, CStr name = "");

	Texture* CreateTexture(CStr strName, uint dwWidth, uint dwHeight, uint dwFlags);
	Texture* CreateTexture(CStr strName, Texture* clone, uint dwFlags);
	Texture* CreateTextureWithFormat(CStr name, uint width, uint height, TextureFormat format, uint flags, TextureUsage usage);
	Texture* CreateTextureFromImage(CStr name, CInputStream& instream, uint flags);

	VertexBuffer* CreateVertexBuffer(uint usage, uint len, uint stride, CStr name = "");
	IndexBuffer* CreateIndexBuffer(IndexBufferFormat fmt, uint usage, uint len, CStr name = "");

	RenderQuery* CreateQuery(RenderQueryType type);
	IVertexDeclaration CreateVertexDeclaration(const VertexDeclaration& decl);

	uint LoadCursorImage(Texture* tex, const RRect& srcRect, int hotspotX, int hotspotY);
	uint SetCursor(uint cursorImage);
	bool ShowCursor(bool show, bool force = false);
	void SetCursorPos(uint x, uint y);
	void ResetCursor();
	bool IsCursorVisible() const
	{
#ifdef ENF_HWCURSOR
		return m_bCursorVisible;
#else
		return false;
#endif
	}
	void OnEvent(const Event& event);

};

}
#endif //ENF_IRENDMAIN_H
