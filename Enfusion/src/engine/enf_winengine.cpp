//#include "precomp.h"
#include "engine/enf_winengine.h"
//#include "enf_wininput.h"
#include "system/enf_profiler.h"
//#include "render/enf_d3dres.h"
#include "script/enf_scriptapi.h"
//#include "render/enf_rendmain.h"
#include "materials/enf_materialsystem.h"
#include "resource/enf_particleeffectinternal.h"
//#include "sound/enf_xsoundsystem.h"
#include "common/enf_parser.h"
//#include "widgets/enf_widgets.h"
#include "resource/enf_resourcemanager.h"
//#include "pipeline/enf_defaultpipeline.h"
#include "system/enf_vfilesystem.h"
//#include "baseworld/enf_decals.h"
//#include "terrain/enf_terrainmatdef.h"
#include "tinyxml/tinyxml.h"
//#include "postprocess/enf_postprocess.h"
#include "common/enf_streams.h"
//#include "terrain/enf_terraingrassmat.h"
//#include "baseworld/enf_skydomemat.h"
//#include "materials/enf_oceanmat.h"
#include "materials/enf_materials.h"
//#include "materials/enf_postprocessmat.h"
#include "common/enf_diag.h"

namespace enf
{

#ifdef ENF_DONE
	XSoundSystem* CreateXAudio2System();
#endif

//-----------------------------------------------------------------------
extern Engine* g_Engine;

//-----------------------------------------------------------------------
/*void StartEditor(const char *fname, int line)
{
	SString<> parm;
	SString<> seditpath;
	char name[256];

	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	GetStartupInfo(&si);
	
	GetFileSystem()->GetAbsoluteName(fname, name);

	void *ptr = Settings::Open("Software\\Enforce engine 2");
	if(ptr)
	{
		Settings::ReadString(ptr, "seditor", seditpath);
		Settings::Close(ptr);
	}

	parm.format("%s %s/%d", seditpath.cstr(), name, line);

	CreateProcess(NULL, (LPSTR)parm.cstr(), NULL, NULL, false, CREATE_DEFAULT_ERROR_MODE, NULL, NULL, &si, &pi);
}
*/

//-----------------------------------------------------------------------------
/*
int WinEngine::HandleEvent(const Event& event)
{
	if(event.HasType(SignStatusEventTypeID))
	{
		const SignStatusEvent& sevent = (const SignStatusEvent&)event;
		m_strProfileName = sevent.m_strUserName;

	}
	return 0;
}
*/
//-----------------------------------------------------------------------
WinEngine::WinEngine(CStr appname, CStr cliparams, void* inst, bool wbmode, size_t staticPoolSize):
	m_hInstance(inst),
	m_hAccel(NULL),
	m_iBrightness(0),
	m_bWindowed(true),
	m_bWBMode(wbmode),
	m_hWnd(NULL),
	m_hCreatedWindow(NULL),
	Engine(appname, cliparams, staticPoolSize)
{
	char cName[MAX_COMPUTERNAME_LENGTH + 1];
	DWORD cNameSize = MAX_COMPUTERNAME_LENGTH;

	int ret = ::GetComputerName(cName, &cNameSize);
	if (!ret)
	{
		enf::strcpy(cName, "Unknown"); //TODO: localize
	}
	m_strMachineName = cName;


/*
	INITCOMMONCONTROLSEX icce;
	::CoInitialize(NULL); // assumed S_OK
	// Initialize the common controls library
	::ZeroMem(&icce, sizeof(INITCOMMONCONTROLSEX));
	icce.dwSize = sizeof(INITCOMMONCONTROLSEX);
	icce.dwICC  = ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_PROGRESS_CLASS ;
	::InitCommonControlsEx(&icce);
*/
}

//-----------------------------------------------------------------------------
void WinEngine::ShowWindow()
{
//	SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, (LPVOID)0, SPIF_SENDWININICHANGE | SPIF_UPDATEINIFILE);

	::SetActiveWindow((HWND)m_hWnd);
	::SetFocus((HWND)m_hWnd);
	::SetForegroundWindow((HWND)m_hWnd);
	::BringWindowToTop((HWND)m_hWnd);
}

//-----------------------------------------------------------------------------
// Name: WndProc()
// Desc: Static msg handler which passes messages to the application class.
//-----------------------------------------------------------------------------
LRESULT CALLBACK WndProc( HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    LRESULT lres = ((WinEngine*)GetEngine())->MsgProc( hWnd, uMsg, wParam, lParam );

	 if(lres) return lres;

	 return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

//-----------------------------------------------------------------------
void DoSetClientSize(HWND hWnd, int width, int height)
{
	// setting the client size is less obvious than it could have been
	// because in the result of changing the total size the window scrollbar
	// may [dis]appear and/or its menubar may [un]wrap (and AdjustWindowRect()
	// doesn't take neither into account) and so the client size will not be
	// correct as the difference between the total and client size changes --
	// so we keep changing it until we get it right
	//
	// normally this loop shouldn't take more than 3 iterations (usually 1 but
	// if scrollbars [dis]appear as the result of the first call, then 2 and it
	// may become 3 if the window had 0 size originally and so we didn't
	// calculate the scrollbar correction correctly during the first iteration)
	// but just to be on the safe side we check for it instead of making it an
	// "infinite" loop (i.e. leaving break inside as the only way to get out)
	for ( int i = 0; i < 4; i++ )
	{
		RECT rectClient;
		::GetClientRect(hWnd, &rectClient);

		// if the size is already ok, stop here (NB: rectClient.left = top = 0)
		if ( (rectClient.right == width) &&
			(rectClient.bottom == height) )
		{
			break;
		}

		// Find the difference between the entire window (title bar and all)
		// and the client area; add this to the new client size to move the
		// window
		RECT rectWin;
		::GetWindowRect(hWnd, &rectWin);

		const int widthWin = rectWin.right - rectWin.left,
			heightWin = rectWin.bottom - rectWin.top;

		if ( !::MoveWindow(hWnd,
			rectWin.left,
			rectWin.top,
			width + widthWin - rectClient.right,
			height + heightWin - rectClient.bottom,
			TRUE) )
		{
			break;
		}
	}
}

//-----------------------------------------------------------------------------
void AdjustWindowStyle(HWND hWnd, bool windowed)
{
	if( windowed )
	{
		// Set windowed-mode style
		SetWindowLong( hWnd, GWL_STYLE, WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_VISIBLE );
	}
	else
	{
		// Set fullscreen-mode style
		SetWindowLong( hWnd, GWL_STYLE, WS_POPUP|WS_SYSMENU|WS_VISIBLE );
	}
}

//-----------------------------------------------------------------------
bool WinEngine::Resize(uint width, uint height, uint fsaa, bool windowed, bool vsync)
{
	m_bWindowed = windowed;

	if (m_hWnd && m_hWnd != m_hCreatedWindow)
	{
		AdjustWindowStyle((HWND)m_hWnd, windowed);
		::SetWindowPos((HWND) m_hWnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOOWNERZORDER|SWP_NOSIZE|SWP_NOMOVE|SWP_SHOWWINDOW);
	}

	if(m_pRender != NULL)
	{
		RenderSetup setup = m_pRender->GetSetup();

		setup.m_iBackBufferWidth = width;
		setup.m_iBackBufferHeight = height;
		setup.m_bWindowed = windowed;
		setup.m_PresentationInterval = vsync ? 1 : 0;

		uint adapterid = 0;
		uint mode = FindBestMode (adapterid, width, height, windowed);

		RenderModeInfo modeinfo;
		if (mode == 0xFFFFFFFF || !m_pRender->GetMode(adapterid, INDEX_NOT_FOUND, mode, &modeinfo))
		{
			LogF(LL_NORMAL, "Device resize: cannot find proper mode !");
		}
		else
		{
			setup.m_BackBufferFormat = modeinfo.Format;
		}

		if(m_pRender->Reset(&setup) == EE_OK)
		{
			if (m_hWnd && m_hWnd != m_hCreatedWindow)
			{
				DoSetClientSize((HWND)m_hWnd, width, height);
			}

			for(uint n = 0; n < m_Modules.GetCardinality(); n++)
			{
				m_Modules[n]->OnResize(width, height, windowed);
			}

			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Name: MsgProc()
// Desc: Message handling function.
//-----------------------------------------------------------------------------
uint WinEngine::MsgProc( void* hWnd, uint uMsg, uint wParam, uint lParam )
{
#ifdef ENF_DONE

	//do not pass messages to input device, when we are typing into console!
	//if(m_pConsole == NULL || !m_pConsole->IsFlagSet(WF_VISIBLE))
	{
		if(GetInputDevice())
		{
			if(GetInputDevice()->HasType(WinInputDeviceTypeID))
				((WinInputDevice*)GetInputDevice())->Message((HWND)hWnd, uMsg, wParam, lParam);
		}
	}

	switch( uMsg )
	{
	case WM_SYSKEYDOWN:
		if(wParam == VK_RETURN && GetRuntimeMode())
		{
			RenderSetup setup = GetRender()->GetSetup();
			Resize(setup.m_iBackBufferWidth, setup.m_iBackBufferHeight, 0, !setup.m_bWindowed, setup.m_PresentationInterval == 1);
			return true;
		}
		break;

	case WM_KILLFOCUS:
		break;

	case WM_KEYDOWN:
		if(m_pConsole != NULL)
		{
			static Widget* prevFocus = NULL;

			if((wParam == VK_OEM_3 && MainConfig::m_bEnableConsole))
			{
				if(m_pConsole->IsFlagSet(WF_VISIBLE))
				{
					m_pConsole->ClearFlags(WF_VISIBLE);
					GetWidgetManager()->SetFocus(prevFocus);
				}
				else
				{
					prevFocus = GetWidgetManager()->GetFocus();
					GetWidgetManager()->SetFocus(m_pConsole);
					m_pConsole->SetFlags(WF_VISIBLE);
					m_pInput->Reset(IMASK_KEYBOARD);
				}
			}
		}
		break;

	case WM_CLOSE:
		m_bAppQuit = true;
		return true;

	case WM_ACTIVATEAPP:
		{
			bool active = (wParam != 0);
			// Set exclusive mode access to the mouse based on active state

			::SetFocus((HWND)m_hWnd);
			if(m_pInput)
				m_pInput->SetAcquire(active);

			//do not pause, when force update
			if(!GetForceUpdate())
				SetPaused(!active);
		}
		break;

   case WM_PAINT:
      // Handle paint messages when the app is not ready
		if(m_bWindowed)
			m_pRender->Present();
      break;

	case WM_NCHITTEST:
      // Prevent the user from selecting the menu in fullscreen mode
		if( !m_bWindowed )
			return true;
		 break;

	case WM_MENUSELECT:
		return 0;
/*
	case WM_SYSCOMMAND:
		switch( wParam )
		{
			// only a few system menu command are enabled
		case SC_CLOSE: case SC_DEFAULT: case SC_RESTORE:
		case SC_TASKLIST:
			break;
		case SC_MOVE: case SC_SIZE:
			if (m_bWindowed) break;
			return TRUE;
			// other are disabled and should do nothing

		default:
			break;
		case SC_KEYMENU:
			return TRUE; // ignore
		case SC_MINIMIZE: case SC_MAXIMIZE:
			//case 
			if (!m_bWindowed) return TRUE;
			break;
		}
		break;
*/
	case WM_SYSCOMMAND:
		// Prevent moving/sizing and power loss in fullscreen mode
		switch( wParam )
		{
		case SC_TASKLIST:
		case SC_MOVE:
		case SC_SIZE:
		case SC_MAXIMIZE:
		case SC_KEYMENU:
		case SC_MONITORPOWER:
			//if( !m_bWindowed )
				return true;
			break;
		}
		break;
	case WM_MOUSEMOVE:
		return TRUE;

	case WM_SETCURSOR:
		// Turn off window cursor. 
		::SetCursor( NULL );
		if(g_pRender && g_pRender->IsCursorVisible())
			g_pRender->ShowCursor(true, true);
		return TRUE; // Prevent Windows from setting cursor to window class cursor.
		/*
 if (GAppActive && !GAppIconic)
      { 
        //The low-order word of lParam specifies the hit-test code. 
        // The high-order word of lParam specifies the identifier of the mouse message. 
        // when cursor is above borders, we want to see a corresponding cursor
        if (!UseWindow || (lParam&0xffff)==HTCLIENT)
        {
          // notify the engine we are owning the cursor and it should change its shape
          //DIAG_MESSAGE(5000,"WM_SETCURSOR client");
          GEngine->CursorOwned(true);
          return 0;
        }
      }
		*/
		break;


	}
#endif
	return 0;
}

//-----------------------------------------------------------------------------
void WinEngine::Input(uint flags)
{
#ifdef 	ENF_DONE
	if(flags)
	{
		//when console is in foreground, suppress any other input
		if(m_pConsole != NULL && m_pConsole->IsFlagSet(WF_VISIBLE))
			return;

		if(GetInputDevice())
			GetInputDevice()->Update(flags);
	}
	ProcessDiagControl();
#endif
}

//-----------------------------------------------------------------------------
int WinEngine::UpdateFrame(bool inscene)
{
	TimeStamp timeStamp = Timer::GetTicks();

	UpdateTime(timeStamp);

	if(m_pScript)
		m_pScript->Frame(NULL);

	if(m_pResourceManager)
		m_pResourceManager->Update(timeStamp, 15);

	if(m_pMaterialSystem)
	{
		if( FAILED( RenderFrame(inscene) ) )				
			Sleep(50);
	}

	if(m_pScript)
		m_pScript->ResetStats();

	return true;
}

//-----------------------------------------------------------------------------
void WinEngine::Destroy()
{
	Engine::Destroy();

	#ifdef ENF_DONE
	if(m_hCreatedWindow != m_hWnd && m_hWnd != NULL)
		DestroyWindow((HWND)m_hWnd);
	#endif

	m_hWnd = NULL;

	#ifdef 	ENF_DONE
		BOOL res = UnregisterClass(m_strAppName.cstr(), (HINSTANCE)m_hInstance);

		// Clean up COM
		CoUninitialize();
	#endif

	g_Engine = NULL;

	delete this;
}

//-----------------------------------------------------------------------------
void WinEngine::SaveSettings()
{
#ifdef 	ENF_DONE
	if(!m_strSettingsFileName.IsEmpty())
	{
		GetSettings()->SaveFile(m_strSettingsFileName.c_str());
	}
	m_iSettingsChanged = 0;
#endif
}

//-----------------------------------------------------------------------------
bool WinEngine::ReadSettings(CStr settings, CStr profileName)
{
	VFile* configFile = NULL;
	int errCount = 0;
	while(true)
	{
		DString loadedSettings = settings;

		if(!GetFileSystem()->IsAbsoluteFileName(settings))
		{
			SString<> profTemp;
			if(settings.Length() > 0 && settings[0] != '$')
				profTemp = "$profile:";
			profTemp += settings;

			LogF(LL_ERROR, "Loading config '%s'", profTemp.cstr());
			VFile* profSettings = enf::GetFileSystem()->Open(profTemp.cstr(), enf::FILEMODE_READ);
			if(profSettings == NULL)
			{
				SString<> srcTemp;
				srcTemp = "./";
				srcTemp += settings;

				VFile* srcSettings = enf::GetFileSystem()->Open(srcTemp.cstr(), enf::FILEMODE_READ);
				if(srcSettings == NULL)
				{
					enf::LogF(enf::LL_ERROR, "Can't load settings %s", srcTemp.cstr());
					return false;
				}
				profSettings = enf::GetFileSystem()->Open(profTemp.cstr(), enf::FILEMODE_WRITE);
				if(profSettings == NULL)
				{
					srcSettings->Close();
					enf::LogF(enf::LL_ERROR, "Can't copy settings to %s", profTemp.cstr());
					return false;
				}

				enf::GetFileSystem()->CopyFile(profSettings, srcSettings);
				srcSettings->Close();
			}

			if(profSettings)
				profSettings->Close();

			loadedSettings = profTemp.cstr();
			configFile = m_pFileSystem->Open(profTemp.cstr(), FILEMODE_READ);
			m_strSettingsFileName = profTemp.cstr();
		}
		else
		{
			configFile = m_pFileSystem->Open(settings, FILEMODE_READ);
			m_strSettingsFileName = settings.cstr();
		}

		if(configFile != NULL)
		{
			bool result = m_pSettings->LoadFile(configFile);

			if(!result)
			{
				enf::LogF(LL_ERROR, "Can't load config '%s' because of '%s'", settings.cstr(), m_pSettings->ErrorDesc());
			}
			else if(m_pSettings->RootElement() == NULL)
			{
				enf::LogF(LL_ERROR, "Can't load config '%s' because of it's empty", settings.cstr());
				result = false;
			}

			configFile->Close();

			if(!result)
			{
				if(errCount > 0)
					return false;

				GetFileSystem()->Delete(loadedSettings.cstr());
				enf::LogF(LL_ERROR, "Can't load config '%s' because of it's empty", settings.cstr());
				errCount++;
				continue;
			}
		}
		else
		{
			LogF(LL_ERROR, "Config file deleted");
			return false;
		}
		break;
	}

	return true;
}

//-----------------------------------------------------------------------------
uint WinEngine::Create(void* hWnd, CStr settings, CStr profileName)
{
	if(FindModule(FileSystemTypeID) == NULL)
		InsertModule(CreateFileSystem(profileName, true));

	if (!ReadSettings(settings, profileName))
		LogF(LL_WARNING, "Enfusion setting file load failed");

	//let inform other modules about just loaded settings
	ChangeSettings();
	//ehm...
	m_iSettingsChanged = 0; 

	// Unless a substitute hWnd has been specified, create a window to
	// render into
	RECT rc;
	m_hCreatedWindow = hWnd;		//MUST be set for now

	/*
	if( hWnd == NULL)
	{
		// Register the windows class
		WNDCLASS wndClass = { 0, WndProc, 0, 0, (HINSTANCE)m_hInstance,
									LoadIcon((HINSTANCE)m_hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON) ),
									LoadCursor( NULL, IDC_ARROW ),
									(HBRUSH)GetStockObject(WHITE_BRUSH),
									NULL, m_strAppName.cstr()};
		RegisterClass( &wndClass );

		// Set the window's initial style
		DWORD dwWindowStyle = WS_POPUP|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|WS_MINIMIZEBOX|WS_VISIBLE;

		// Set the window's initial width
		SetRect( &rc, 0, 0, 640, 480 );
		AdjustWindowRect( &rc, dwWindowStyle, true );

		// Create the render window
		m_hWnd = CreateWindow( m_strAppName.cstr(), m_strAppName.cstr(), dwWindowStyle,
									CW_USEDEFAULT, CW_USEDEFAULT,
									(rc.right-rc.left), (rc.bottom-rc.top), 0L,
									NULL,
									(HINSTANCE)m_hInstance, 0L );

		enf_assert(m_hWnd);
		DoSetClientSize((HWND)m_hWnd, 640, 480);
	}
	else
*/
	{
		m_hWnd = (HWND)hWnd;
	}

	/*
	GetClientRect((HWND)m_hWnd, &rc);

	if(hWnd == NULL)	//skip this in WB
		SetForegroundWindow((HWND)m_hWnd);

	m_hAccel = LoadAccelerators( (HINSTANCE)m_hInstance, MAKEINTRESOURCE(IDR_MAIN_ACCEL) );
	*/

	m_pScript = ENF_NEW EnforceScriptContext(m_pStatic);
	g_pScript = m_pScript;

	MainConfig::m_iOptimize = 10;
	MainConfig::m_bShowConsole = false;
	if(GetParam("profile"))
		m_pScript->SetProfiler(true);
	else
		m_pScript->SetProfiler(false);

	MainConfig::m_bShowCrash = true;
	#ifdef ENF_DEVELOPER
		MainConfig::m_bEnableScreenshots = true;
		MainConfig::m_bEnableSuperScreenshots = true;
		MainConfig::m_bEnableDebugRender = true;
		MainConfig::m_bEnableConsole = true;
		MainConfig::m_bLogConsole = true;
		MainConfig::m_bList = true;
	#else
		MainConfig::m_bEnableDebugRender = false;
		MainConfig::m_bEnableConsole = false;  
		MainConfig::m_bLogConsole = false;
		//#ifdef ENF_CONSOLE
			MainConfig::m_bEnableScreenshots = false;
		//#else
			//MainConfig::m_bEnableScreenshots = true;
		//#endif
		MainConfig::m_bList = false;
	#endif

	//MainConfig::m_bNoCrash = true;

	MainConfig::m_CrashLog.Open("crash.log", LOGF_APPEND);
	
	if(MainConfig::m_bLogConsole)
		MainConfig::m_ConsoleLog.Open("console.log", LOGF_APPEND);

	//m_pScript->SetProfiler(false);

	InsertModule(CreateRenderer());

	if(!GetRender()->Init((HWND)m_hWnd))
		return false;

	Create(0, 0, 0, false);

	#ifdef ENF_DONE
		InsertModule(CreateInputDevice(m_hWnd));
	#endif

	//GetInputDevice()->AddEventHandler(ENF_NEW MyInputHandler());

//		TestThreadManager();
	return true;
}


bool VideoTest(const char* name);

//-----------------------------------------------------------------------
uint WinEngine::FindBestMode(uint adapterid, uint width, uint height, bool windowed)
{
	uint bestmode = 0xFFFFFFFF;

	//try to find resolution
	for(uint m = 0; m < 999; m++ )
	{
		RenderModeInfo mode;
		mode.Format = TEXFMT_UNKNOWN;
		if(!m_pRender->GetMode(adapterid, INDEX_NOT_FOUND, m, &mode))
			break;

		if( mode.Width == width && mode.Height == height)
		{
			if(mode.Format != TEXFMT_A8R8G8B8 &&
				mode.Format != TEXFMT_X8R8G8B8 &&
				mode.Format != TEXFMT_R8G8B8 && 
				mode.Format != TEXFMT_A2R10G10B10)
				continue;

			//in window mode, set 
			if (windowed && mode.Format == TEXFMT_A2R10G10B10)
			{
				continue;		
			}

			//choose TEXFMT_A2R10G10B10 if is possible to set it
			if (windowed == false && bestmode != 0xFFFFFFFF && mode.Format != TEXFMT_A2R10G10B10)
			{
				continue;
			}

			bestmode = m;
		}
	}

	return bestmode;
 }


//-----------------------------------------------------------------------
int WinEngine::Create(uint adapterid, uint width, uint height, bool windowed)
{	
	HRESULT hr;

	enf_assert(m_pRender != NULL);

	bool isset = false;


	/*
	modes should be now set in DayZ render

	//try to find resolution
	uint m = FindBestMode(adapterid, width, height, windowed);

	if (m != 0xFFFFFFFF)
	{
		RenderAdapterInfo adapter;
		m_pRender->GetAdapter(adapterid, &adapter);
		m_pRender->SetMode(adapterid, adapter.m_iCurrentDevice, m, windowed);
	}
	else
	{
		//exact mode not found, we can set whatever here
		if(windowed && width != 0 && height != 0)
		{
			RenderAdapterInfo adapter;
			m_pRender->GetAdapter(adapterid, &adapter);

			m = m_pRender->AddMode(adapterid, adapter.m_iCurrentDevice, width, height);

			if(m != INDEX_NOT_FOUND)
			{
				m_pRender->SetMode(adapterid, adapter.m_iCurrentDevice, m, true);
			}
		}

		//again, not found, or is fullscreen
		if (m == 0xFFFFFFFF)
		{
			//get resolution of desktop
			DEVMODE mode;
			mode.dmSize = sizeof(DEVMODE);
			mode.dmDriverExtra = 0;

			if (::EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &mode))
			{
				uint newwidth  = mode.dmPelsWidth; //::GetSystemMetrics(SM_CXFULLSCREEN);
				uint newheight = mode.dmPelsHeight; //::GetSystemMetrics(SM_CYFULLSCREEN);

				m = FindBestMode(adapterid, newwidth, newheight, windowed);

				if (m != 0xFFFFFFFF)
				{
					RenderAdapterInfo adapter;
					m_pRender->GetAdapter(adapterid, &adapter);
					m_pRender->SetMode(adapterid, adapter.m_iCurrentDevice, m, windowed);

					LogF(LL_NORMAL, "Device warning: cannot set wanted resolution (%dx%d), using system resolution (%dx%d) ", width, height, newwidth, newheight );

					width = newwidth;
					height = newheight;
				}
			}
		}
	}

	if(m == 0xFFFFFFFF)
	{
		InternalError("Device error: Can't set resolution");
		return false;
	}

	TiXmlElement* velem = GetSettingsElement("video");

	//pick the same, or nearest lower FSAA type
	int fsaa;
	if(velem->QueryIntAttribute("fsaa", &fsaa) == TIXML_SUCCESS && fsaa != 0)
	{
		static const FSAAType fsaatypes[] = {
#if defined(ENF_XENON)
			FSAATYPE_NONE,
			FSAATYPE_2_SAMPLES,
			FSAATYPE_4_SAMPLES
#elif defined(ENF_OGL)
			FSAATYPE_NONE,
			FSAATYPE_2_SAMPLES,
			FSAATYPE_4_SAMPLES
#endif
		};

		for(int f = fsaa; f >= 0; f--)
		{
			for(uint fs = 0; fs < m_pRender->GetNumFSAADefs(); fs++)
			{
				if(m_pRender->GetFSAADef(fs).Type == fsaatypes[f])
				{
					m_pRender->SetFSAA(fs, m_pRender->GetFSAADef(fs).MaxQuality - 1);
					f = -1;
					break;
				}
			}
		}
	}

	HWND hWnd = (HWND)m_hWnd;
	SetForegroundWindow(hWnd);

	if(hWnd && m_hWnd != m_hCreatedWindow)
	{
		// Prepare window for possible windowed/fullscreen change
		AdjustWindowStyle(hWnd, windowed);

		if(windowed)
		{
			DoSetClientSize(hWnd, width, height);
			int wxpos = 0;
			const char* xpos = GetEngine()->GetParam("xpos");
			if(xpos)
				wxpos = CStr(xpos).ToInt();

			int wypos = 0;
			const char* ypos = GetEngine()->GetParam("ypos");
			if(ypos)
				wypos = CStr(ypos).ToInt();

			::SetWindowPos(hWnd, HWND_TOP, wxpos, wypos, 0, 0, SWP_NOOWNERZORDER|SWP_NOZORDER|SWP_NOSIZE|SWP_SHOWWINDOW);
		}
	}
*/
	if( FAILED( hr = m_pRender->Create() ))
	{
		//most common errors
		
		InternalError("Device error: %x", hr);
		return false;
	}

	InsertModule(CreateMaterialSystem(m_pRender, m_pScript, m_bWBMode));
	InsertModule(CreateResourceManager(m_pRender));

	//for better file-system coherency
	m_pRender->BeginShaderCompile();

	//particles
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatParticle(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatParticleSprite(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatParticlePoint(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatParticleStreak(m_pMaterialSystem));

/*
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatGeneric(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatVertexLitPerpixDX9(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatDoubleSideDX9(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatVertexLitPerpix2DDX9(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatGenericSprite(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatSprite(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatSkinnedPerpixDX9(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatDoubleSideSkinnedDX9(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatHumanSkin(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatFresnelDX9(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatTreeDX9(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatPlantsDX9(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatRefract(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatSimpleRefract(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW TerrainGenMaterial(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW GrassMaterialClass(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatSkyDome(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatSkyDomePlanet(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatLensFlare(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatDecal(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatDecalTree(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatVolLight(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW OccluderMat(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatEngineCommon(m_pMaterialSystem));
	
	//ocean and water surface material class
	GetMaterialSystem()->AddMaterialClass(ENF_NEW OceanMat(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW OceanMatUnderwater(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW OceanMatDecal(m_pMaterialSystem));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatWaterPool(m_pMaterialSystem));

	//materials for postprocesses
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatFilmGrain(GetMaterialSystem()));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatDepthOfField(GetMaterialSystem()));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatRain(GetMaterialSystem()));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatFXAA(GetMaterialSystem()));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatSmartGamma(GetMaterialSystem()));
	GetMaterialSystem()->AddMaterialClass(ENF_NEW MatSSAO(GetMaterialSystem()));
	*/

	m_pRender->EndShaderCompile();

#ifdef 	ENF_DONE
	InsertModule(ENF_NEW DecalManager());
#endif

	InsertModule(CreateParticleSystem());

#ifdef 	ENF_DONE
	InsertModule(ENF_NEW PostProcessManager());
	XSoundSystem* ss = CreateXAudio2System();
	InsertModule(ss);

	if(!GetParam("nosound"))
		ss->Initialize(m_hWnd, 0, 0, 44100, 16, true);
#endif

//	VideoTest("ducks_take_off.ogv");
	//VideoTest("testvideo_422.ogx");

//return false;

	return true;
}


//-----------------------------------------------------------------------
WinEngine* CreateWinEngine(CStr appname, CStr cliparams, void* instance, bool wbmode, size_t staticPoolSize)
{
	g_Engine = ENF_NEW WinEngine(appname, cliparams, instance, wbmode, staticPoolSize);
	return (WinEngine*)g_Engine;
}

//-----------------------------------------------------------------------
void DestroyWinEngine()
{
	if (g_Engine)
	{
		g_Engine->Destroy();
		delete g_Engine;
		
		g_Engine = NULL;
	}
}

}
