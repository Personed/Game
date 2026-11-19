//-----------------------------------------------------------------------------
// File: enf_systools.cpp
//
// Desc: Implementation of application stack and assert
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------

//#include "precomp.h"
#include "enf_config.h"
#include "enf_main.h"
#include "common/enf_types.h"
#include "common/enf_strings.h"
#include <stdio.h>
#include <stdlib.h>
#include <float.h>

#ifndef __GCC__
	#include <signal.h>
#endif

#ifdef ENF_BUILD
	#include "system/enf_vfilesystem.h"
	#include "script/enf_scriptapi.h"
	#include "engine/enf_engine.h"
#endif

#ifdef __MACOSX__
	#include <gl/glfw.h>
#endif

#ifdef WIN32
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#include <Shellapi.h>
	#include <Dbghelp.h>
	#include <Shlwapi.h>
#elif defined(ENF_PS3)
	#include <cell/dbg.h>
#endif

namespace enf
{

#ifdef NDEBUG
	#undef NDEBUG
#endif  /* NDEBUG */

#define ENF_MAX_PATH 260

bool g_bDgbSymbolsInit = false;
DWORD g_bDgbSymbolsErrNo = 0;

#ifdef __MACOSX__
	int SysKey::SPACE = GLFW_KEY_SPACE;
	int SysKey::ESC = GLFW_KEY_ESC;
	int SysKey::Fn = GLFW_KEY_F1 - 1;
	int SysKey::UP = GLFW_KEY_UP;
	int SysKey::DOWN = GLFW_KEY_DOWN;
	int SysKey::LEFT = GLFW_KEY_LEFT;
	int SysKey::RIGHT = GLFW_KEY_RIGHT;
	int SysKey::LSHIFT = GLFW_KEY_LSHIFT;
	int SysKey::RSHIFT = GLFW_KEY_RSHIFT;
	int SysKey::LCTRL = GLFW_KEY_LCTRL;
	int SysKey::RCTRL = GLFW_KEY_RCTRL;
	int SysKey::LALT = GLFW_KEY_LALT;
	int SysKey::RALT = GLFW_KEY_RALT;
	int SysKey::LSUPER = GLFW_KEY_LSUPER;
	int SysKey::RSUPER = GLFW_KEY_RSUPER;
	int SysKey::TAB = GLFW_KEY_TAB;
	int SysKey::ENTER = GLFW_KEY_ENTER;
	int SysKey::BACKSPACE = GLFW_KEY_BACKSPACE;
	int SysKey::INSERT = GLFW_KEY_INSERT;
	int SysKey::DEL = GLFW_KEY_DEL;
	int SysKey::PAGEUP = GLFW_KEY_PAGEUP;
	int SysKey::PAGEDOWN = GLFW_KEY_PAGEDOWN;
	int SysKey::HOME = GLFW_KEY_HOME;
	int SysKey::END = GLFW_KEY_END;
	int SysKey::KP_n = GLFW_KEY_KP_0;
	int SysKey::KP_DIVIDE = GLFW_KEY_KP_DIVIDE;
	int SysKey::KP_MULTIPLY = GLFW_KEY_KP_MULTIPLY;
	int SysKey::KP_SUBTRACT = GLFW_KEY_KP_SUBTRACT;
	int SysKey::KP_ADD = GLFW_KEY_KP_ADD;
	int SysKey::KP_DECIMAL = GLFW_KEY_KP_DECIMAL;
	int SysKey::KP_EQUAL = GLFW_KEY_KP_EQUAL;
	int SysKey::KP_ENTER = GLFW_KEY_KP_ENTER;
	int SysKey::KP_NUM_LOCK = GLFW_KEY_KP_NUM_LOCK;
	int SysKey::CAPS_LOCK = GLFW_KEY_CAPS_LOCK;
	int SysKey::SCROLL_LOCK = GLFW_KEY_SCROLL_LOCK;
	int SysKey::PAUSE = GLFW_KEY_PAUSE;
	int SysKey::MENU = GLFW_KEY_MENU;
#elif defined(WIN32)
	int SysKey::SPACE = VK_SPACE;
	int SysKey::ESC = VK_ESCAPE;
	int SysKey::Fn = VK_F1 - 1;
	int SysKey::UP = VK_UP;
	int SysKey::DOWN = VK_DOWN;
	int SysKey::LEFT = VK_LEFT;
	int SysKey::RIGHT = VK_RIGHT;
	int SysKey::LSHIFT = VK_LSHIFT;
	int SysKey::RSHIFT = VK_RSHIFT;
	int SysKey::LCTRL = VK_LCONTROL;
	int SysKey::RCTRL = VK_RCONTROL;
	int SysKey::LALT = VK_LMENU;
	int SysKey::RALT = VK_RMENU;
	int SysKey::LSUPER = VK_LWIN;
	int SysKey::RSUPER = VK_RWIN;
	int SysKey::TAB = VK_TAB;
	int SysKey::ENTER = VK_RETURN;
	int SysKey::BACKSPACE = VK_BACK;
	int SysKey::INSERT = VK_INSERT;
	int SysKey::DEL = VK_DELETE;
	int SysKey::PAGEUP = VK_PRIOR;
	int SysKey::PAGEDOWN = VK_NEXT;
	int SysKey::HOME = VK_HOME;
	int SysKey::END = VK_END;
	int SysKey::KP_n = VK_NUMPAD0;
	int SysKey::KP_DIVIDE = VK_DIVIDE;
	int SysKey::KP_MULTIPLY = VK_MULTIPLY;
	int SysKey::KP_SUBTRACT = VK_SUBTRACT;
	int SysKey::KP_ADD = VK_ADD;
	int SysKey::KP_DECIMAL = VK_DECIMAL;
	int SysKey::KP_EQUAL = 0;
	int SysKey::KP_ENTER = 0;
	int SysKey::KP_NUM_LOCK = VK_NUMLOCK;
	int SysKey::CAPS_LOCK = 0;
	int SysKey::SCROLL_LOCK = VK_SCROLL;
	int SysKey::PAUSE = 0;
	int SysKey::MENU = VK_MENU;
#else
	int SysKey::SPACE = 0;
	int SysKey::ESC = 0;
	int SysKey::Fn = 0;
	int SysKey::UP = 0;
	int SysKey::DOWN = 0;
	int SysKey::LEFT = 0;
	int SysKey::RIGHT = 0;
	int SysKey::LSHIFT = 0;
	int SysKey::RSHIFT = 0;
	int SysKey::LCTRL = 0;
	int SysKey::RCTRL = 0;
	int SysKey::LALT = 0;
	int SysKey::RALT = 0;
	int SysKey::LSUPER = 0;
	int SysKey::RSUPER = 0;
	int SysKey::TAB = 0;
	int SysKey::ENTER = 0;
	int SysKey::BACKSPACE = 0;
	int SysKey::INSERT = 0;
	int SysKey::DEL = 0;
	int SysKey::PAGEUP = 0;
	int SysKey::PAGEDOWN = 0;
	int SysKey::HOME = 0;
	int SysKey::END = 0;
	int SysKey::KP_n = 0;
	int SysKey::KP_DIVIDE = 0;
	int SysKey::KP_MULTIPLY = 0;
	int SysKey::KP_SUBTRACT = 0;
	int SysKey::KP_ADD = 0;
	int SysKey::KP_DECIMAL = 0;
	int SysKey::KP_EQUAL = 0;
	int SysKey::KP_ENTER = 0;
	int SysKey::KP_NUM_LOCK = 0;
	int SysKey::CAPS_LOCK = 0;
	int SysKey::SCROLL_LOCK = 0;
	int SysKey::PAUSE = 0;
	int SysKey::MENU = 0;
#endif

//-----------------------------------------------------------------------------
DWORD PUBLIC_API InitDbgSymbols()
{
	if(!g_bDgbSymbolsInit)
	{
		g_bDgbSymbolsInit = true;
	#ifdef ENF_DBGHELP		
			char buff[ENF_MAX_PATH+1];
			GetModuleFileNameA(NULL, buff, ENF_MAX_PATH);
			char* end = strrchr(buff, '\\');
			if (end == NULL)
			{
				buff[0] = '.';
				buff[1] = 0;
			}
			else
				*end = 0;

			BOOL ret = ::SymInitialize(::GetCurrentProcess(), buff, TRUE);
			if (!ret)
			{
				g_bDgbSymbolsErrNo = ::GetLastError();
			}
	#endif
	}
	return g_bDgbSymbolsErrNo;
}


#ifdef ENF_DBGHELP

#define MAX_LINES 64


//-----------------------------------------------------------------------------
bool PrintStack(uint ebp, uint eip, char *text, unsigned buffLen)
{
	unsigned textOffs;
	int ret = 0;
	DWORD displ;
	
	IMAGEHLP_LINE line;
	memset(&line, 0, sizeof(IMAGEHLP_LINE));
	line.SizeOfStruct = sizeof(IMAGEHLP_LINE);

	char symbolbuff[sizeof(IMAGEHLP_SYMBOL) + 1024];
	IMAGEHLP_SYMBOL* symbol = (IMAGEHLP_SYMBOL*)symbolbuff;
	memset(symbolbuff, 0, sizeof(symbolbuff));
	symbol->SizeOfStruct = sizeof(IMAGEHLP_SYMBOL);
	symbol->MaxNameLength = 1024;
	
	STACKFRAME stackFrame;
	memset(&stackFrame, 0, sizeof(STACKFRAME));
	stackFrame.AddrPC.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Mode = AddrModeFlat;
	stackFrame.AddrFrame.Offset = ebp;
	stackFrame.AddrPC.Offset = eip;

	unsigned lines = 0;
	char msg[2048] = "\0";
	textOffs = strlen(msg);

	HANDLE cprocess = ::GetCurrentProcess();
	HANDLE cthread = ::GetCurrentThread();

	DWORD dw = InitDbgSymbols();
	if(dw != 0)
	{
		sprintf(msg, "SymInitialize:%i addr: 0x%08x\n", dw, eip);
		//		instance->AssertLogFunc(assertType, msg, instance->console);
		textOffs += strlen(msg);
		if (textOffs < buffLen)
			strcat(text, msg);
		return false;
	}

	for (;;)
	{
		ret = StackWalk(IMAGE_FILE_MACHINE_I386, cprocess, cthread, &stackFrame, NULL, NULL, NULL, NULL, NULL);
		if (!ret)
		{
			if (lines >= MAX_LINES && textOffs < buffLen)
			{
				sprintf(msg, "... not shown %i lines\n", lines - MAX_LINES);
				textOffs += strlen(msg);
				if (textOffs < buffLen)
					strcat(text, msg);
			}
			//sprintf(msg, "StackWalk:%i\n", GetLastError());
			//			instance->AssertLogFunc(assertType, msg, instance->console);
			textOffs += strlen(msg);
			if (textOffs < buffLen)
				strcat(text, msg);
			break;
		}

		if (lines > 1000)
		{
			//instance->AssertLogFunc(assertType, "huh, stackDepth vic jak 1000? to asi cyklime\n", instance->console);
			break;
		}

		ret = SymGetSymFromAddr(cprocess, stackFrame.AddrPC.Offset, &displ, symbol);
		if (!ret)
		{
			sprintf(msg, "SymGetSymFromAddr:%i, addr:0x%08x\n", GetLastError(), stackFrame.AddrPC.Offset);
			//			instance->AssertLogFunc(assertType, msg, instance->console);
			textOffs += strlen(msg);
			if (textOffs < buffLen && lines < MAX_LINES)
				strcat(text, msg);
			++lines;
		}
		else
		{
			if (SymGetLineFromAddr(cprocess, stackFrame.AddrPC.Offset, &displ, &line))
			{
				sprintf(msg, "[%s]: %s(%i)\n", symbol->Name, line.FileName, line.LineNumber);
				//				instance->AssertLogFunc(assertType, msg, instance->console);
				textOffs += strlen(msg);
				if (textOffs < buffLen && lines < MAX_LINES)
					strcat(text, msg);
				++lines;
			}
			else
			{
				sprintf(msg, "[%s]: ??? addr:0x%08x\n", symbol->Name, stackFrame.AddrPC.Offset);
				//				instance->AssertLogFunc(assertType, msg, instance->console);
				textOffs += strlen(msg);
				if (textOffs < buffLen && lines < MAX_LINES)
					strcat(text, msg);
				++lines;
			}
		}
	}
	return (lines != 0);
}
#endif

// assertion format string for use with output to stderr
static char _assertstring[] = "Assertion failed: %s, file %s, line %d\n";

//assertion string components for message box

#define PROGINTRO   "Program: "
#define FILEINTRO   "File: "
#define LINEINTRO   "Line: "
#define EXPRINTRO   "Reason: "
#define HELPINTRO   "\n(Press Retry to debug the application - JIT must be enabled)"

static const char * dotdotdot = "...";
static const char * newline = "\n";
static const char * dblnewline = "\n\n";

#define DOTDOTDOTSZ 3
#define NEWLINESZ   1
#define DBLNEWLINESZ   2

#define MAXLINELEN  60
#define ASSERTBUFSZ (MAXLINELEN * 64)

#if defined (_M_IX86)
	#define _DbgBreak() __asm { int 3 }
#elif defined (_M_ALPHA)
	void _BPT();
	#pragma intrinsic(_BPT)
	#define _DbgBreak() _BPT()
#elif defined (_M_IA64)
	void __break(int);
	#pragma intrinsic (__break)
	#define _DbgBreak() __break(0x80016)
#elif defined(ENF_PS3)
	#ifdef __SNC__
		#define _DbgBreak() __builtin_snpause();
	#else
		#define _DbgBreak() __asm__ volatile ( "tw 31,1,1" );
	#endif
#elif defined(__APPLE__)
	#define _DbgBreak() abort()
#else  /* defined (_M_IA64) */
	#define _DbgBreak() DebugBreak()
#endif  /* defined (_M_IA64) */

//-----------------------------------------------------------------------------
PUBLIC_API ErrorResponse _ShowError(const char* title, const char* intro, const char *error, const char *filename, unsigned lineno)
{
	static bool avoidRecursion = false;

	if(avoidRecursion)
		return ER_IGNORE;

	avoidRecursion = true;

	#ifdef ENF_BUILD
		char expr2[65536];
		if(enf::GetEngine() && enf::GetEngine()->GetScriptContext())
		{
			char scriptCallstack[65536];
			if(enf::GetEngine()->GetScriptContext()->GetCallStack(scriptCallstack, 65536) > 0)
			{
				strcpy(expr2, error);
				strcat(expr2, "\nScript callstack:");
				strcat(expr2, scriptCallstack);
				error = expr2;
			}
		}
	#endif

	int nCode = 0;
	char *pch;
	char assertbuf[ASSERTBUFSZ];
	char progname[ENF_MAX_PATH + 1];

	#ifdef WIN32
		::ShowCursor(true);
	#endif
	// Line 1: box intro line
	strcpy( assertbuf, intro );
	strcat( assertbuf, dblnewline );

	// Line 2: program line
	strcat( assertbuf, PROGINTRO );

	progname[ENF_MAX_PATH] = '\0';
	#ifdef WIN32
		if ( !GetModuleFileName( NULL, progname, ENF_MAX_PATH ))
	#endif
		strcpy( progname, "<program name unknown>");

	pch = (char *)progname;

	// sizeof(PROGINTRO) includes the NULL terminator
	if ( sizeof(PROGINTRO) + strlen(progname) + NEWLINESZ > MAXLINELEN )
	{
		pch += (sizeof(PROGINTRO) + strlen(progname) + NEWLINESZ) - MAXLINELEN;
		strncpy( pch, dotdotdot, DOTDOTDOTSZ );
	}

	strcat( assertbuf, pch );
	strcat( assertbuf, newline );

	// Line 3: file line
	strcat( assertbuf, FILEINTRO );

	// plenty of room on the line, just append the filename */
	strcat( assertbuf, filename );

	strcat( assertbuf, newline );

	// Line 4: line line
	strcat( assertbuf, LINEINTRO );
	#if defined(WIN32)
		_itoa( lineno, assertbuf + strlen(assertbuf), 10 );
	#endif
	strcat( assertbuf, dblnewline );

	// Line 5: message line
	strcat( assertbuf, EXPRINTRO );

	// sizeof(HELPINTRO) includes the NULL terminator

	if (strlen(assertbuf) +
		strlen(error) +
		2*DBLNEWLINESZ +
		sizeof(HELPINTRO) > ASSERTBUFSZ )
	{
		strncat( assertbuf, error,
			ASSERTBUFSZ -
			(strlen(assertbuf) +
			DOTDOTDOTSZ +
			2*DBLNEWLINESZ +
			sizeof(HELPINTRO)) );
		strcat(assertbuf, dotdotdot );
	}
	else
		strcat(assertbuf, error );

	strcat(assertbuf, dblnewline );

	int currTextLen = strlen(assertbuf);

	#ifdef ENF_DBGHELP
		DWORD _ebp, _eip;
		__asm {
			push eax
				mov _ebp, ebp
				call dummy
	dummy:
			pop eax
				mov _eip, eax
				pop eax
		}
		if(!PrintStack(_ebp, _eip, assertbuf+currTextLen, ASSERTBUFSZ-currTextLen))
		{
		}
	#elif defined(ENF_XENON) && defined(ENF_DEVELOPER)

		const DWORD maxBackTrace = 20;
		VOID* backTrace[maxBackTrace];

		// Capture a stack back trace.
		HRESULT hResult = DmCaptureStackBackTrace( _countof( backTrace ), backTrace );

		if( hResult == XBDM_NOERR )
		{
			// Find out how many valid entries are in the result.
			ULONG count = 0;
			for( ULONG i = 0; i < _countof( backTrace ) && backTrace[i]; ++i )
				count = i;

			strcat(assertbuf+currTextLen, "\nCallstack:\n");
			// Write the valid entries to the output file.
			for( ULONG i = 0; i < count; ++i )
			{
				char buffer[64];
				sprintf_s( buffer, "%p\r\n", backTrace[ i ] );
				strcat(assertbuf+currTextLen, buffer);
			}
		}
	#elif defined(ENF_PS3) && defined(ENF_DEVELOPER)
		uint count;
		int ret = cellDbgPpuThreadCountStackFrames(&count);
		if(ret == CELL_OK)
		{
			uintptr_t backtrace[16];
			count = enf_min(count - 1, 16U);
			ret = cellDbgPpuThreadGetStackBackTrace(1, count, backtrace, NULL);
			if(ret == CELL_OK)
			{
				printf("StackWalk:\n");

				for(uint n = 0; n < count; n++)
				{
					printf("%x\n", backtrace[n]);
				}
			}
		}


	#endif

	currTextLen = strlen(assertbuf);

	if((currTextLen + strlen(HELPINTRO) + 1) < ASSERTBUFSZ)
	{
		// Line 8: help line
		strcat(assertbuf, HELPINTRO);
	}

	assertbuf[ASSERTBUFSZ-1] = 0;

	#ifdef ENF_BUILD
		Error2Log(assertbuf);
	#endif
	avoidRecursion = false;

	#ifdef WIN32
		nCode = ::MessageBox(NULL, assertbuf,
			title,
			MB_ABORTRETRYIGNORE|MB_ICONHAND|MB_SETFOREGROUND|MB_TASKMODAL|MB_TOPMOST);

		if(nCode == IDABORT)
			return ER_ABORT;
		if(nCode == IDRETRY)
			return ER_RETRY;
		if(nCode == IDIGNORE)
			return ER_IGNORE;
	#else
		_OutputDebugString(assertbuf);
	#endif

	return ER_ABORT;
}

//-----------------------------------------------------------------------------
PUBLIC_API ErrorResponse _ShowError(const char* title, const char* intro, const char *error, void* ebp, void* eip)
{
	static bool avoidRecursion = false;

	if(avoidRecursion)
		return ER_IGNORE;

	avoidRecursion = true;

	#ifdef ENF_BUILD

		char expr2[65536];
		if(enf::GetEngine() && enf::GetEngine()->GetScriptContext())
		{
			char scriptCallstack[65536];
			if(enf::GetEngine()->GetScriptContext()->GetCallStack(scriptCallstack, 65536) > 0)
			{
				strcpy(expr2, error);
				strcat(expr2, "\nScript callstack:");
				strcat(expr2, scriptCallstack);
				error = expr2;
			}
		}
	#endif

	int nCode = 0;
	char *pch;
	char assertbuf[ASSERTBUFSZ];
	char progname[ENF_MAX_PATH + 1];

	#ifdef WIN32
		::ShowCursor(true);
	#endif

	// Line 1: box intro line
	strcpy( assertbuf, intro );
	strcat( assertbuf, dblnewline );

	// Line 2: program line
	strcat( assertbuf, PROGINTRO );

	progname[ENF_MAX_PATH] = '\0';
	#ifdef WIN32
		if ( !GetModuleFileName( NULL, progname, ENF_MAX_PATH ))
	#endif
		strcpy( progname, "<program name unknown>");

	pch = (char *)progname;

	// sizeof(PROGINTRO) includes the NULL terminator
	if ( sizeof(PROGINTRO) + strlen(progname) + NEWLINESZ > MAXLINELEN )
	{
		pch += (sizeof(PROGINTRO) + strlen(progname) + NEWLINESZ) - MAXLINELEN;
		strncpy( pch, dotdotdot, DOTDOTDOTSZ );
	}

	strcat( assertbuf, pch );
	strcat( assertbuf, newline );

	// Line 5: message line
	strcat( assertbuf, EXPRINTRO );

	// sizeof(HELPINTRO) includes the NULL terminator

	if (strlen(assertbuf) +
		strlen(error) +
		2*DBLNEWLINESZ +
		sizeof(HELPINTRO) > ASSERTBUFSZ )
	{
		strncat( assertbuf, error,
			ASSERTBUFSZ -
			(strlen(assertbuf) +
			DOTDOTDOTSZ +
			2*DBLNEWLINESZ +
			sizeof(HELPINTRO)) );
		strcat(assertbuf, dotdotdot );
	}
	else
		strcat(assertbuf, error );

	strcat(assertbuf, dblnewline );

	int currTextLen = strlen(assertbuf);
	#if defined(ENF_XENON) && defined(ENF_DEVELOPER)
			const DWORD maxBackTrace = 20;
		VOID* backTrace[maxBackTrace];

		// Capture a stack back trace.
		HRESULT hResult = DmCaptureStackBackTrace( _countof( backTrace ), backTrace );

		if( hResult == XBDM_NOERR )
		{
			// Find out how many valid entries are in the result.
			ULONG count = 0;
			for( ULONG i = 0; i < _countof( backTrace ) && backTrace[i]; ++i )
				count = i;

			strcat(assertbuf+currTextLen, "\nCallstack:\n");
			// Write the valid entries to the output file.
			for( ULONG i = 0; i < count; ++i )
			{
				char buffer[64];
				sprintf_s( buffer, "%p\r\n", backTrace[ i ] );
				strcat(assertbuf+currTextLen, buffer);
			}
		}

	#elif defined(ENF_DBGHELP)
		if(!PrintStack((DWORD)ebp, (DWORD)eip, assertbuf+currTextLen, ASSERTBUFSZ-currTextLen))
	#endif
	{
	}

	currTextLen = strlen(assertbuf);
	#ifdef WIN32
		if((currTextLen + strlen(HELPINTRO) + 1) < ASSERTBUFSZ)
		{
			// Line 8: help line
			strcat(assertbuf, HELPINTRO);
		}
	#endif
	assertbuf[ASSERTBUFSZ-1] = 0;

	#ifdef ENF_BUILD
		Error2Log(assertbuf);
	#endif

	#ifdef WIN32
		nCode = ::MessageBox(NULL, assertbuf,
			title,
			MB_ABORTRETRYIGNORE|MB_ICONHAND|MB_SETFOREGROUND|MB_TASKMODAL|MB_TOPMOST);

		if(nCode == IDABORT)
			return ER_ABORT;
		if(nCode == IDRETRY)
			return ER_RETRY;
		if(nCode == IDIGNORE)
			return ER_IGNORE;
	#else
		_OutputDebugString(assertbuf);
	#endif

	return ER_ABORT;
}

//-----------------------------------------------------------------------------
PUBLIC_API void _enf_assert(const char *expr, const char *filename, unsigned lineno)
{
	ErrorResponse er = _ShowError("Enforce engine", "Assertion failed", expr, filename, lineno);

	// Abort: abort the program
	if (er == ER_ABORT)
	{
		#ifdef ENF_XENON
			#ifdef ENF_DEVELOPER
				#ifndef _DEBUG
					DmCrashDump(true);
				#endif
				DebugBreak();
			#else
				return; //sigh!
			#endif
		#elif defined(ENF_PS3)
				__builtin_snpause();

				return;
		#elif defined(WIN32)
				// raise abort signal
				raise(SIGABRT);

				// We usually won't get here, but it's possible that
				//	SIGABRT was ignored.  So exit the program anyway.
				_exit(3);
		#endif
	}

	// Retry: call the debugger
	if (er == ER_RETRY)
	{
		_DbgBreak();
		// return to user code
		return;
	}

	// Ignore: continue execution
	if (er == ER_IGNORE)
		return;

	abort();
}

//-----------------------------------------------------------------------------
MsgBoxResult _MessageBox(MsgBoxType type, const char* caption, const char *text)
{
	#ifdef ENF_XENON
		return MBR_IDOK;
	#elif defined(ENF_PS3)
		return MBR_IDOK;
	#elif defined(WIN32)
		UINT wtype;

		switch(type)
		{
		case MBT_OK:
			wtype = MB_OK;
			break;
		case MBT_OKCANCEL:
			wtype = MB_OKCANCEL;
			break;
		case MBT_RETRYCANCEL:
			wtype = MB_RETRYCANCEL;
			break;
		case MBT_YESNO:
			wtype = MB_YESNO;
			break;
		case MBT_YESNOCANCEL:
			wtype = MB_YESNOCANCEL;
			break;
		case MBT_ABORTRETRYIGNORE:
			wtype = MB_ABORTRETRYIGNORE;
			break;
		}
		::ShowCursor(true);
		int res = MessageBox(NULL, text, caption, wtype);
		::ShowCursor(false);

		switch(res)
		{
		case IDABORT:
			return MBR_IDABORT;
		case IDCANCEL:
			return MBR_IDCANCEL;
		case IDCONTINUE:
			return MBR_IDCONTINUE;
		case IDIGNORE:
			return MBR_IDIGNORE;
		case IDNO:
			return MBR_IDNO;
		case IDOK:
			return MBR_IDOK;
		case IDRETRY:
			return MBR_IDRETRY;
		case IDTRYAGAIN:
			return MBR_IDTRYAGAIN;
		case IDYES:
			return MBR_IDYES;
		}
		return MBR_IDOK;
	#else
		return MBR_IDOK;
	#endif
}

//-----------------------------------------------------------------------
//TODO: move it to something like enf_system.cpp
//-----------------------------------------------------------------------
void SetFPUPrecision(FPUPrecision prec)
{
	ushort pp = 0;

	switch(prec)
	{
	case FPU_PRECISION_MINIMUM:
		pp = 0x000;
		break;

	case FPU_PRECISION_MEDIUM:
		pp = 0x200;
		break;
	case FPU_PRECISION_MAXIMUM:
		pp = 0x300;
		break;
	}

	#ifdef ENF_INTEL
		ushort sr;

		__asm 
		{
			fstcw sr;
			mov	ax, word ptr[sr]
			and	ax, 0fcffh
			or		ax, word ptr[pp]
			mov	word ptr[sr], ax
			fldcw	sr;
		}
	#elif defined(ENF_PS3)
	#elif defined(__APPLE__)
	#else
		_controlfp(_EM_INVALID | _EM_INEXACT | _EM_UNDERFLOW | _EM_OVERFLOW | _EM_DENORMAL | _EM_ZERODIVIDE, _MCW_EM );
	#endif
}


//-----------------------------------------------------------------------------
/*!
set fpu round mode -> for enf_roundint
*/
//-----------------------------------------------------------------------------
void SetFPURoundMode(FPURoundMode mode)
{
#if defined(WIN32) || defined(ENF_XENON)
	uint m = 0;
	switch (mode)
	{
		case FPU_CHOP	: m = _RC_CHOP;	break;
		case FPU_UP		: m = _RC_UP;		break;
		case FPU_DOWN	: m = _RC_DOWN;	break;
		case FPU_NEAR	: m = _RC_NEAR;	break;
	}
#endif
#ifdef ENF_INTEL
		_control87(m, _MCW_RC);
#endif	
}


//-----------------------------------------------------------------------
void _DebugBreak()
{
	#if defined(ENF_XENON) || defined(WIN32)
		::DebugBreak();
	#elif defined(ENF_PS3)
		//PS3TODO

	#endif
}

//-----------------------------------------------------------------------
void _OutputDebugString(const char* str)
{
	//do not output on retail builds of XBOX version
	#if defined(ENF_XENON) || defined(WIN32)
		#if !defined(ENF_XENON) || defined(ENF_DEVELOPER)
			::OutputDebugString(str);
		#endif
	#elif defined(ENF_PS3)
		printf(str);
	#elif defined(__APPLE__)
		printf(str);
	#endif
}

//-----------------------------------------------------------------------
void* _LoadLibrary(const char* name)
{
	#if defined(WIN32) || defined(ENF_XENON)
		return ::LoadLibrary(name);
	#else
		return NULL;
	#endif
}

//-----------------------------------------------------------------------
void* _GetProcAddress(void* module, const char* procname)
{
	#if defined(WIN32) || defined(ENF_XENON)
		return ::GetProcAddress((HMODULE)module, procname);
	#else
		return NULL;
	#endif
}

//-----------------------------------------------------------------------
short _GetAsyncKeyState(int vkey)
{
	#ifdef WIN32
		return ::GetAsyncKeyState(vkey);
	#elif defined(__MACOSX__)
		return glfwGetKey(vkey) == GLFW_PRESS ? 0x8000 : 0;
	#else
		return 0;
	#endif
}

#if defined(WIN32) || defined(ENF_XENON)
//-----------------------------------------------------------------------
int ExceptionHandler(unsigned int code, void* _ep)
{
	struct _EXCEPTION_POINTERS *ep = (struct _EXCEPTION_POINTERS *)_ep;
	const EXCEPTION_RECORD & er = *ep->ExceptionRecord;

	enf::SString<256> exceptionType;

	switch (er.ExceptionCode)
	{
	case 0xE06D7363:
		exceptionType = "Unknown C++ exception thrown.";
		break;

	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		exceptionType = "Array bounds exceeded";
		break;

	case EXCEPTION_BREAKPOINT:
		exceptionType = "Breakpoint";
		break;

	case EXCEPTION_DATATYPE_MISALIGNMENT:
		exceptionType = "Datatype misalignment";
		break;

	case EXCEPTION_FLT_DENORMAL_OPERAND:
		exceptionType = "Float denormal operand";
		break;

	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		exceptionType = "Float divide by zero";
		break;

	case EXCEPTION_FLT_INEXACT_RESULT:
		exceptionType = "Float inexact result";
		break;

	case EXCEPTION_FLT_INVALID_OPERATION:
		exceptionType = "Float invalid operation";
		break;

	case EXCEPTION_FLT_OVERFLOW:
		exceptionType = "Float overflow";
		break;

	case EXCEPTION_FLT_STACK_CHECK:
		exceptionType = "Float stack check";
		break;

	case EXCEPTION_FLT_UNDERFLOW:
		exceptionType = "Float underflow";
		break;

	case EXCEPTION_ILLEGAL_INSTRUCTION:
		exceptionType = "Illegal instruction";
		break;

	case EXCEPTION_IN_PAGE_ERROR:
		exceptionType = "In page error";
		break;

	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		exceptionType = "Integer divide by zero";
		break;

	case EXCEPTION_INT_OVERFLOW:
		exceptionType = "Integer overflow";
		break;

	case EXCEPTION_INVALID_DISPOSITION:
		exceptionType = "Invalid disposition";
		break;

	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		exceptionType = "Noncontinuable exception";
		break;

	case EXCEPTION_PRIV_INSTRUCTION:
		exceptionType = "Private instruction";
		break;

	case EXCEPTION_SINGLE_STEP:
		exceptionType = "Single step";
		break;

	case EXCEPTION_STACK_OVERFLOW:
		exceptionType = "Stack overflow";
		break;

	case EXCEPTION_ACCESS_VIOLATION:
		exceptionType.format("Access violation. Illegal %s by %x at %x",
				(er.ExceptionInformation[0] ? "write" : "read"),
				er.ExceptionAddress,
				er.ExceptionInformation[1]);
		break;

    default:
		exceptionType.format("SEH exception thrown. Exception code: %x at %x", er.ExceptionCode, er.ExceptionAddress);
	}

	#ifdef ENF_XENON
		_ShowError("Crash info", "Unhandled exception", exceptionType.cstr(), (void*)ep->ContextRecord->Gpr1, (void*)ep->ContextRecord->Iar);

		#ifdef ENF_DEVELOPER
			#ifndef _DEBUG
				DmCrashDump(true);
			#endif
			DebugBreak();
		#endif
	#else

		#ifdef ENF_BUILD
			char path[ENF_MAX_PATH + 1] = "Enforce.mdmp";

			if(IsFileSystem())
			{
				GetFileSystem()->GetAbsoluteName("$profile:Enforce.mdmp", path, false);
			}

			HANDLE minidumpFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
			if(minidumpFile != INVALID_HANDLE_VALUE)
			{
				MINIDUMP_EXCEPTION_INFORMATION expinfo;
				expinfo.ClientPointers = TRUE;
				expinfo.ExceptionPointers = ep;
				expinfo.ThreadId = GetCurrentThreadId();
			//Save minidmp
				#ifdef ENF_DONE
					::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), minidumpFile, (MINIDUMP_TYPE)(MiniDumpNormal|MiniDumpWithDataSegs), &expinfo,NULL,NULL);
				#endif
				::CloseHandle(minidumpFile);
			}
		#endif

		_ShowError("Crash info", "Unhandled exception", exceptionType.cstr(), (void*)ep->ContextRecord->Ebp, (void*)ep->ContextRecord->Eip);
	#endif

	return 0;
}
#endif

//-----------------------------------------------------------------------------
const char* _ImportFromClipboard()
{
	static DString result;

	#ifdef WIN32
		if (::OpenClipboard(NULL))
		{
			HANDLE handle = ::GetClipboardData(CF_TEXT);
			if (handle)
			{
				const char* mem = (const char *)::GlobalLock(handle);
				if (mem) result = mem;
				::GlobalUnlock(handle);
			}
			::CloseClipboard();
		}
	#endif

	return result.cstr();
}

//-----------------------------------------------------------------------------
void _ExportToClipboard(const char* str)
{
	#ifdef WIN32
		if(::OpenClipboard(NULL))
		{
			uint length = enf::strlen(str) + 1;
			HGLOBAL hText = ::GlobalAlloc(GMEM_DDESHARE | GMEM_MOVEABLE, length);
			char* pText = (char*)GlobalLock(hText);
			CopyMem(pText, str, length + 1);
			GlobalUnlock(hText);

			EmptyClipboard();
			SetClipboardData(CF_TEXT, hText);
			CloseClipboard();
		}
	#endif
}

//-----------------------------------------------------------------------------
void _OpenURL(const char* urlAddress, const char* param)
{
	#if defined(WIN32) && defined(ENF_BUILD)

#ifdef ENF_DONE
		DWORD dwSize = MAX_PATH;
		char defaultBrowserPath[ MAX_PATH ] = { 0 };
		HRESULT hr = AssocQueryStringA( 0, ASSOCSTR_EXECUTABLE, ".htm", "open", defaultBrowserPath, &dwSize );

		if(SUCCEEDED(hr))
			::ShellExecute(NULL, NULL, defaultBrowserPath, urlAddress, NULL, SW_SHOWNORMAL);

	//	HINSTANCE inst = ::ShellExecute(NULL, "open", urlAddress, NULL, NULL, SW_SHOWNORMAL);
		//DWORD err = ::GetLastError();
		//if(err == 14007);
		//	inst = ::ShellExecute(NULL, "open", "iexplore.exe", urlAddress, NULL, SW_SHOWNORMAL);
		//inst = inst;
#endif

	#endif
}

//-----------------------------------------------------------------------------
void LogF(LogLevel level, const char* str,...)
{
	#if !defined(ENF_DEVELOPER) && !defined(_DEBUG)
		if (level == LL_DEBUG)
			return;
	#endif

	va_list argptr;
	va_start (argptr, str);

#ifdef ENF_BUILD
	if(level < MainConfig::m_LogLevel)
		return;
#endif

	char tmp[16384];
	int len;

	len = strlen(str);
	if(len > 16000)
	{
		memcpy(tmp, str, 16000);
		tmp[16000] = 0;
		len = 16000;
	}
	else
	{
		vsprintf (tmp, str, argptr);
		len = strlen(tmp);
	}
	va_end (argptr);

	if(len == 0)
		return;

	if(tmp[len - 1] != '\n')
	{
		tmp[len++] = '\n';
		tmp[len] = 0;
	}

#if (defined(ENF_XENON) && defined(ENF_DEVELOPER)) || (defined(ENF_PS3) && defined(ENF_DEVELOPER)) || defined(_DEBUG)
	_OutputDebugString(tmp);
#endif

	//developer wants to see it all!
#ifndef ENF_DEVELOPER
	if (level == LL_DEBUG)
		return;
#endif

#if defined(ENF_BUILD) && (!defined(ENF_CONSOLE) || defined(ENF_DEVELOPER))
	Engine* eng = IsEngine() ? GetEngine() : NULL;
	if(eng)
	{
		if(level >= LL_WARNING)
			eng->ErrorMessage(tmp);
		else
			eng->ConsoleMessage(tmp);
	}

#endif
}


}