#ifdef _MSC_VER
#pragma once
#endif

#ifndef _POSEIDON_LIB_WPCH_HPP
#define _POSEIDON_LIB_WPCH_HPP

// precompiled header files - many files use something of Windows API
#include <El/elementpch.hpp>

#include "appName.h"
// Registry key for saving player's name, IP address of server etc.
#define ConfigAppRoot "Software\\Bohemia Interactive Studio\\"
#define ConfigApp ConfigAppRoot AppName


#if _SERVER
	#include "serverConfig.h"
#elif _DEMO && !defined _DEMO_INT
	#include "demoConfig.h"
#elif _SUPER_RELEASE
  #include "retailConfig.h"
#elif _CONSOLE
	#include "consoleConfig.h"
#elif _DEDICATED_CLIENT
  #include "dedicatedClientConfig.h"
#elif _USE_AFX
	#include "afxConfig.h"
#else
	#include "normalConfig.h"
#endif

#if _BULDOZER
  #include "buldozerConfig.h"
#endif

//@NOTE: sorry, this is temporary until it proves stable (and meaningful).. Mojmir
#if !defined SERVER_CORPSE_PERFORMANCE_PATCH
# define SERVER_CORPSE_PERFORMANCE_PATCH 1
#endif
//#define GEOM_OCCLUDERS

// user general include
#include "types.hpp"
#include <El/Math/math3d.hpp>

// global diagnostics

void CCALL GlobalShowMessage( int timeMs, const char *msg, ... );

#endif
