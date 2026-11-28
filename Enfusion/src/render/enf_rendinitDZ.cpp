//-----------------------------------------------------------------------------
// File: enf_rendinit.cpp
//
// Desc: Renderer device initialization for the DX11-only client.
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------

#include "enf_main.h"
#include "render/enf_irendmain.h"

// Enfusion's legacy renderer has no DX9 device initializer. The game uses
// EngineDD11 directly; keep these interface entry points as safe stubs.
enf::ERESULT enf::IRenderer::Create()
{
	return EE_ERROR;
}

bool enf::IRenderer::Init(void* hwnd)
{
	(void)hwnd;
	return false;
}