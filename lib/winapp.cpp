// Poseidon -- windows application initialization and termination
// (C) 1997, SUMA
// Win95 version
#ifdef _WIN32

#include "wpch.hpp"
#include "winpch.hpp"

#include "resource.h"
#include <Es/Common/win.h>
#include <Es/Strings/rString.hpp>
#include <Es/Containers/staticArray.hpp>
#include <El/Debugging/debugTrap.hpp>

#define MULTI_MON_WIN 0

#if MULTI_MON_WIN
#define COMPILE_MULTIMON_STUBS 1
#include <multimon.h>
#endif

/**************************************************************************
	Global Variables
**************************************************************************/

// version dependent constants

//#define WIN_STYLE ( WS_CAPTION|WS_MINIMIZEBOX|WS_SYSMENU )
#define WIN_STYLE ( WS_CAPTION|WS_MINIMIZEBOX|WS_SYSMENU|WS_SIZEBOX )
#define WIN_EX_STYLE WS_EX_APPWINDOW

#define WIN_STYLE_NORESIZE ( WS_CAPTION|WS_MINIMIZEBOX|WS_SYSMENU )

extern bool LandEditor;

/*
State and ex-state as viewed by Spy++

Windowed:
WS_CAPTION|WS_VISIBLE|WS_CLIPSIBLINGS|WS_SYSMENU|WS_THICKFRAME|WS_OVERLAPPED|WS_MINIMIZEBOX
WS_EX_LEFT|WS_EX_LTRREADING|WS_EX_RIGHTSCROLLBAR|WS_EX_WINDOWEDGE|WS_EX_APPWINDOW

Fullscreen:
WS_CAPTION|WS_VISIBLE|WS_CLIPSIBLINGS|WS_SYSMENU|WS_THICKFRAME|WS_OVERLAPPED|WS_MINIMIZEBOX
WS_EX_LEFT|WS_EX_LTRREADING|WS_EX_RIGHTSCROLLBAR|WS_EX_TOPMOST|WS_EX_WINDOWEDGE|WS_EX_APPWINDOW
*/

/*!
  \patch 5093 Date 11/30/2006 by Ondra
  - Fixed: Window flags changed to improve full-screen window border handling.
*/
#if 1
	//#define FULL_STYLE WIN_STYLE
	//#define FULL_EX_STYLE WIN_EX_STYLE
	//#define FULL_STYLE ( WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_BORDER|WS_DLGFRAME|WS_CLIPSIBLINGS )
  #define FULL_STYLE ( WS_POPUP )
	#define FULL_EX_STYLE ( WS_EX_APPWINDOW|WS_EX_WINDOWEDGE )
#else
	#define FULL_STYLE ( 0 )
	#define FULL_EX_STYLE ( 0 )
#endif


LONG CALLBACK AppWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
LONG CALLBACK DebugWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);

#if MULTI_MON_WIN
struct MonEnumContext
{
  HMONITOR monitor;
	RECT rect;
};

static BOOL CALLBACK MonEnumProc
(
  HMONITOR hMonitor,  // handle to display monitor
  HDC hdcMonitor,     // handle to monitor-appropriate device context
  LPRECT lprcMonitor, // pointer to monitor intersection rectangle
  LPARAM dwData       // data passed from EnumDisplayMonitors
)
{
	MonEnumContext *context=(MonEnumContext *)dwData;
	// check if we want this monitor
	// we want to use first non-primary display
	if (lprcMonitor->top!=0 && lprcMonitor->left!=0)
	{
		if (context->monitor==NULL)
		{
			LogF
			(
				"Mon %x: %d,%d..%d,%d",hMonitor,
				lprcMonitor->top,lprcMonitor->left,
				lprcMonitor->bottom,lprcMonitor->right
			);
			context->rect = *lprcMonitor;
			context->monitor = hMonitor;
		}
	}
	return TRUE;
}
#endif

static RWString WAppName;

void InitWindowAppName()
{
  const char *appName = AppName;
	if( LandEditor )
	{
    appName = "Operation Flashpoint"; // hardcoded in Oxygen
	}


  // convert application name from UTF-8 to Unicode
  int len = MultiByteToWideChar(CP_UTF8, 0, appName, -1, NULL, 0);
  AUTO_STATIC_ARRAY(wchar_t, wBuffer, 1024);
  wBuffer.Resize(len);
  MultiByteToWideChar(CP_UTF8, 0, appName, -1, wBuffer.Data(), len);
  wBuffer[len - 1] = 0; // make sure result is always null terminated
  WAppName = wBuffer.Data();
}

HWND AppInit
(
	HINSTANCE hInst,HINSTANCE hPrev,int sw, bool fullscreen,
	int x, int y, int width, int height
)
{
	// try to create window on secondary display
	RECT rect;
	rect.top = y;
	rect.left = x;
	#if MULTI_MON_WIN
	MonEnumContext context;
	context.monitor = NULL;
	EnumDisplayMonitors(NULL,NULL,MonEnumProc,(LPARAM)&context);
	if (context.monitor)
	{
		rect.top  = context.rect.top;
		rect.left = context.rect.left;
	}
	#endif

  InitWindowAppName();

	if (!hPrev)
	{
		//***  Register a class for the main application window

		WNDCLASSW cls;
		cls.hCursor        = LoadCursor(0,IDC_ARROW);
		cls.hIcon          = LoadIcon(hInst, "APPICON");
		cls.lpszMenuName   = NULL;
		cls.lpszClassName  = WAppName;
		cls.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
		cls.hInstance      = hInst;
		cls.style          = CS_VREDRAW | CS_HREDRAW;
		cls.lpfnWndProc    = (WNDPROC)AppWndProc;
		cls.cbClsExtra     = 0;
		cls.cbWndExtra     = 0;
		
		if( !RegisterClassW(&cls) ) return NULL;

		WNDCLASS clsDebug;
		clsDebug.hCursor        = LoadCursor(0,IDC_ARROW);
		clsDebug.hIcon          = LoadIcon(hInst, "APPICON");
		clsDebug.lpszMenuName   = NULL;
		clsDebug.lpszClassName  = "DEBUG WINDOW";
		clsDebug.hbrBackground  = (HBRUSH)GetStockObject(BLACK_BRUSH);
		clsDebug.hInstance      = hInst;
		clsDebug.style          = CS_VREDRAW | CS_HREDRAW;
		clsDebug.lpfnWndProc    = (WNDPROC)DebugWndProc;
		clsDebug.cbClsExtra     = 0;
		clsDebug.cbWndExtra     = 0;
		if( !RegisterClass(&clsDebug) ) return NULL;
	}

	HWND hwndApp;
	if( !fullscreen )
	{
		//RECT rect;
		int scrW=width;
		int scrH=height;
		
		//rect.left=0;
		//rect.top=0;
		rect.right = scrW+rect.left;
		rect.bottom = scrH+rect.top;
		AdjustWindowRectEx(&rect,WIN_STYLE,FALSE,WIN_EX_STYLE);

		hwndApp = CreateWindowExW(  
			WIN_EX_STYLE,
			WAppName,           // Class name
			WAppName,           // Caption
			WIN_STYLE,
			rect.left, rect.top,   // Position
			rect.right-rect.left,rect.bottom-rect.top, // Size
			NULL,                   // Parent window (no parent)
			NULL,                   // use class menu
			hInst,               // handle to window instance
			NULL                    // no params to pass on
		);
    // verify client coordinates
    #if _DEBUG
		RECT client;
		BOOL retVal=GetClientRect(hwndApp,&client);
    if (retVal)
    {
      Assert(client.right-client.left==scrW);
      Assert(client.bottom-client.top==scrH);
    }
    #endif
	}
	else
	{
		int scrW=GetSystemMetrics(SM_CXSCREEN);
		int scrH=GetSystemMetrics(SM_CYSCREEN);
		
		//RECT rect;
		//rect.left=0;
		//rect.top=0;
		rect.right=scrW + rect.left;
		rect.bottom=scrH + rect.top;
		BOOL retVal=AdjustWindowRectEx(&rect,FULL_STYLE,FALSE,FULL_EX_STYLE);
		
		hwndApp = CreateWindowExW(  
			FULL_EX_STYLE,
			WAppName,           // Class name
			WAppName,           // Caption
			FULL_STYLE,
			rect.left, rect.top,   // Position
			rect.right-rect.left,rect.bottom-rect.top,             // Size
			NULL,                   // Parent window (no parent)
			NULL,                   // use class menu
			hInst,               // handle to window instance
			NULL                    // no params to pass on
		);
		RECT client;
		retVal=GetClientRect(hwndApp,&client);

		if( client.bottom<scrH )
		{	
			int enlarge=scrH-client.bottom;
			rect.bottom+=enlarge;
			client.bottom+=enlarge;
			SetWindowPos(
				hwndApp,HWND_TOP,
				rect.left,rect.top,
				rect.right-rect.left,rect.bottom-rect.top,
				0
			);
		}
	}
	
		ShowWindow(hwndApp,sw);
		UpdateWindow(hwndApp);
    if (sw!=SW_MINIMIZE) SetFocus(hwndApp);
	
	return hwndApp;
}

void AppStateChangeBeg(HWND hwndApp, bool fullscreen, bool enableSize, int x, int y, int w, int h)
{
  if (fullscreen)
  {
    // Hide the window to avoid animation of blank windows
    ShowWindow( hwndApp, SW_HIDE );

    SetWindowLong(hwndApp, GWL_STYLE, FULL_STYLE);
  }
  else
  {
	  int winStyle = enableSize ? WIN_STYLE : WIN_STYLE_NORESIZE;
    SetWindowLong(hwndApp, GWL_STYLE, winStyle);
  }
}

void AppStateChangeEnd(HWND hwndApp, bool fullscreen, bool enableSize, int x, int y, int w, int h)
{
  if (!fullscreen)
  {
    WINDOWPLACEMENT wp;
    memset(&wp,0,sizeof(wp));
    wp.length = sizeof(wp);
    wp.flags = 0;
    wp.showCmd = SW_SHOWNORMAL;

    RECT &rect = wp.rcNormalPosition;
    rect.top = y;
    rect.left = x;
	  rect.right = x+w;
	  rect.bottom = y+h;
	  int winStyle = enableSize ? WIN_STYLE : WIN_STYLE_NORESIZE;
	  AdjustWindowRectEx(&rect,winStyle,FALSE,WIN_EX_STYLE);

    SetWindowPlacement(hwndApp,&wp);
    //SetWindowPos( hwndApp, HWND_NOTOPMOST, 0, 0, rect.right-rect.left, rect.bottom-rect.top, SWP_NOMOVE | SWP_NOREDRAW | SWP_FRAMECHANGED);
    SetWindowPos( hwndApp, HWND_NOTOPMOST, 0, 0, 0,0, SWP_NOMOVE|SWP_NOSIZE | SWP_NOREDRAW | SWP_FRAMECHANGED);
    
    RedrawWindow(hwndApp,NULL,NULL,RDW_FRAME|RDW_INVALIDATE);
  }
  else
  {
  
  }

  if (!IsWindowVisible( hwndApp )) ShowWindow(hwndApp, SW_SHOW);
}

BOOL AppDone(HWND hwndApp, HINSTANCE hInstance)
{
  if (DestroyWindow(hwndApp))
  {
    if (UnregisterClass("DEBUG WINDOW", hInstance))
    {
      if (UnregisterClassW(WAppName, hInstance))
      {
        return TRUE;
      }
    }
  }
  return FALSE;
}

RWString UTFToUnicode(RString text);

// MSI has its own window class - force our own attributes

void InitWindow( HINSTANCE hInst, HWND hwnd, bool withCommandLine, const char *appendix)
{
	// set our text
	if( LandEditor )
	{
    if (IsWindowUnicode(hwnd))
      SetWindowTextW(hwnd, L"Buldozer");
    else
		  SetWindowTextA(hwnd, "Buldozer");
	}
	else
	{
    if (IsWindowUnicode(hwnd))
    {
      RWString title = WAppName + UTFToUnicode(appendix);
      RWString commandLine = GetCommandLineW();
      if (withCommandLine && !commandLine.IsEmpty()) title = title +  + L" " + GetCommandLineW();
      SetWindowTextW(hwnd, title);
    }
    else
    {
      RString title = RString(AppName) + appendix;
      RString commandLine = GetCommandLineA();
      if (withCommandLine && !commandLine.IsEmpty()) title = title + " " +commandLine;
		  SetWindowTextA(hwnd, AppName);
	  }
	}
	// set our own icon
	HICON icon=LoadIcon(hInst,"APPICON");
	SetClassLong(hwnd,GCL_HICON,(LONG)icon);
}

#endif //_WIN32
