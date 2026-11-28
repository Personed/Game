#ifdef _MSC_VER
#pragma once
#endif

#ifndef _ENGINE_DLL_HPP
#define _ENGINE_DLL_HPP

#include <Es/Common/win.h>

struct CreateEnginePars
{
	// in
	HINSTANCE hInst;
	HINSTANCE hPrev;
	int sw;
	int w,h,bpp;
	// in/out
	bool UseWindow;
  bool GenerateShaders;
};

extern bool HideCursor;

void EnableDesktopCursor(bool enable);

// import configuration dependent functions
  #if _ENABLE_DX11
    Engine *CreateEngineD3D11(CreateEnginePars &pars);
  #endif
  #if _ENABLE_DX10
    Engine *CreateEngineD3DT(CreateEnginePars &pars);
  #endif

#endif
