//-----------------------------------------------------------------------------
// File: enf_winengine.h
//
// Desc: 
//
// Copyright (c) 2000-2005 Black Element Software. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_WINENGINE_H
#define ENF_WINENGINE_H

#include "enf_config.h"
#include "enf_engine.h"

namespace enf
{

//-----------------------------------------------------------------------------
class PUBLIC_API WinEngine: public Engine
{
	friend class ConfigBox;

protected:
	//static INT_PTR CALLBACK SelectDeviceProc( HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam );

protected:
	void*			m_hWnd;
	void*			m_hCreatedWindow;
	void*			m_hAccel;

	int				m_iBrightness;
	bool			m_bWindowed;
	bool			m_bWBMode;

	uint FindBestMode(uint adapterid, uint width, uint height, bool windowed);
	bool ReadSettings(CStr settings, CStr profileName);

public:
	CStr GetMachineName() const;

	void* GetWindow() const
	{
		return m_hWnd;
	}

	void*	m_hInstance;

	uint Create(void* hWnd, CStr settings, CStr profileName);

	bool Resize(uint width, uint height, uint fsaa, bool windowed, bool vsync);
	virtual void ShowWindow();
	int Create(uint adapter, uint width, uint height, bool windowed);
	int UpdateFrame(bool inscene);
	
	void Input(uint flags);

	uint MsgProc( void* hWnd, uint uMsg, uint wParam, uint lParam );

	void Destroy();
	void SaveSettings();

	WinEngine(CStr appname, CStr cliparams, void* inst, bool wbmode, size_t staticPoolSize);
};


PUBLIC_API WinEngine* CreateWinEngine(CStr appname, CStr cliparams, void* instance, bool wbmode, size_t staticPoolSize);
PUBLIC_API void DestroyWinEngine();

}

#endif
