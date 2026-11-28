//-----------------------------------------------------------------------------
// File: enf_engine.h
//
// Desc: Basic engine class
//
// Copyright (c) 2000-2014 Bohemia Interactive. All rights reserved
//-----------------------------------------------------------------------------

#ifndef ENF_ENGINE_H
#define ENF_ENGINE_H

#include "enf_config.h"
#include "enf_main.h"
#include "engine/enf_item.h"
#include "engine/enf_event.h"
#include "common/enf_set.h"
#include "system/enf_timer.h"
#include "system/enf_mutex.h"

namespace enf
{
/** @defgroup Engine Engine framework
@{*/

class ConsoleWidget;
class RenderFont;
class WidgetManager;
class InputDevice;
class StaticBuffer;
class EnforceScriptContext;
class ScriptContext;
class ScriptModule;
class Renderer;
class XSoundSystem;
class ParticleSystem;
class MaterialSystem;
class ResourceManager;
class DecalManager;
class TerrainManager;
class AnimationSystem;
class EntityManager;
class WorldManager;
class PhysicsManager;

class Pipeline;
class PathEngine;
class FileSystem;
class StringTable;
class Module;
class Event;
class TiXmlDocument;
class TiXmlElement;
class COutputStream;
class CThreadManager;
class CParser;
struct UserSettings;

#ifdef ENF_DEVELOPER
	#define ENF_TIME_INTERPOLATE
#endif

//-----------------------------------------------------------------------------
class PUBLIC_API ErrorHandler
{
protected:
	ErrorHandler()
	{
	}

public:
	enum HandlerResult {HR_TERMINATE, HR_CONTINUE};
	enum ErrorType {ET_CRITICAL, ET_SCRIPTEXCEPTION, ET_INFO};

	virtual ~ErrorHandler() {}
	virtual HandlerResult HandleError(ErrorType type, const char* message)=0;
};


//-----------------------------------------------------------------------------
/*!
Used for AddErrorToList as description of source of error
*/
//-----------------------------------------------------------------------------
struct ErrorSource
{
	CStr		File;
	int		Line;

	ErrorSource(CStr file, int line):
		File(file),
		Line(line)
	{
	}
};

//-----------------------------------------------------------------------------
/*
Event for consoles. Notification about user sign in/off
*/
//-----------------------------------------------------------------------------
class SignStatusEvent: public Event
{
public:
	uint m_iUser;
	CStr m_strUserName;
	bool	m_bSigned;
	ENF_DECLARE_ITEM(SignStatusEvent, Event);
	SignStatusEvent(uint user, CStr userName, bool sign): m_iUser(user), m_strUserName(userName), m_bSigned(sign) {}
};

//-----------------------------------------------------------------------------
class SetPausedEvent: public Event
{
public:
	bool	m_bPaused;
	ENF_DECLARE_ITEM(SetPausedEvent, Event);
	SetPausedEvent(bool paused): m_bPaused(paused) {}
};

//-----------------------------------------------------------------------------
class TerminationEvent: public Event
{
public:
	ENF_DECLARE_ITEM(TerminationEvent, Event);
	TerminationEvent() {}
};


//-----------------------------------------------------------------------------
/*
Event for consoles. Notification about user settings change
*/
//-----------------------------------------------------------------------------
class UserSettingsChangedEvent: public Event
{
public:
	uint m_iUser;

	ENF_DECLARE_ITEM(UserSettingsChangedEvent, Event);
	UserSettingsChangedEvent(uint user): m_iUser(user) {}
};

//-----------------------------------------------------------------------------
/*
Event for consoles. Notification about user storage change
*/
//-----------------------------------------------------------------------------
class StorageChangedEvent: public Event
{
public:
	uint m_iUser;

	ENF_DECLARE_ITEM(StorageChangedEvent, Event);
	StorageChangedEvent(uint user): m_iUser(user) {}
};

//-----------------------------------------------------------------------------
/*!
Event used for reporting that render device is going to be
reset.
*/
//-----------------------------------------------------------------------------
class BeforeResetEvent: public Event
{
public:
	ENF_DECLARE_ABSTRACT_ITEM(BeforeResetEvent, Event);
};

//-----------------------------------------------------------------------------
/*!
Used after Engine::RenderFrame, before diag menu/statistics
*/
//-----------------------------------------------------------------------------
class AfterRenderEvent: public Event
{
public:
	ENF_DECLARE_ITEM(AfterRenderEvent, Event);
};

//-----------------------------------------------------------------------------
/*!
Event used for reporting that render device is after
a reset.
*/
//-----------------------------------------------------------------------------
class AfterResetEvent: public Event
{
public:
	ENF_DECLARE_ABSTRACT_ITEM(AfterResetEvent, Event);
};

//-----------------------------------------------------------------------------
class CrashLogEvent: public Event
{
public:
	TLog&	m_Log;

	ENF_DECLARE_ABSTRACT_ITEM(CrashLogEvent, Event);
	CrashLogEvent(TLog& log): m_Log(log) {}
};

//-----------------------------------------------------------------------------
/*!
Event send upon entering command into console. Use
Engine::AddEventHandler to catch this event
*/
//-----------------------------------------------------------------------------
class ConsoleEvent: public Event
{
protected:
	//!String entered into console
	const char*	Command;
public:
	ENF_DECLARE_ABSTRACT_ITEM(ConsoleEvent, Event);
	ConsoleEvent(): Command(NULL) {}
	ConsoleEvent(const char* cmd): Command(cmd) {}
	const char* GetCmd() const
	{
		return Command;
	}
};

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
enum SignInStatus
{
	SIS_NOT_SIGNED,
	SIS_SIGNING,
	SIS_SIGNED
};

//-----------------------------------------------------------------------------
enum StorageStatus
{
	SS_NOT_READY,
	SS_READY,
	SS_IGNORED
};

//-----------------------------------------------------------------------------
enum MessageBoxButtons
{
	MBB_NONE = 0,
	MBB_OK,
	MBB_YESNO,
	MBB_CUSTOM,
};


//-----------------------------------------------------------------------------
/*!
engine
*/
//-----------------------------------------------------------------------------
class PUBLIC_API Engine: public EventHandler
{
	friend class ConfigBox;

public:
	typedef Pair<DString, DString> CLIParam;

	ENF_DECLARE_MMOBJECT(Engine);

	/*!
	Returns value of param in command line. Params are prefixed by '-', the value
	follows.
	\param param NAme of parameter
	\return NULL when don't exist. The name of param, when there is no value, value when there is any.
	*/
	const char* GetParam(CStr param) const;

	/*!
	Returns CLI parameter by it's index
	\param index
	Index of parameter
	*/
	const CLIParam& GetParam(uint index) const
	{
		return m_Params[index];
	}

	void AppendParam(CStr param, CStr value)
	{
		m_Params.Insert(CLIParam(param, value));
	}

	//!Returns number of CLI parameters
	uint GetNumParams() const
	{
		return m_Params.GetCardinality();
	}

	/*!
	Gets settings for the Engine
	*/
	TiXmlDocument* GetSettings() const
	{
		return m_pSettings;
	}

	/*!
	Gets settings child element
	\param name
	Name of child element. When do not exists, it's created
	\return
	Pointer to XML element
	*/
	TiXmlElement* GetSettingsElement(const char* name) const;

	/*!
	Notifies Engine that Settings has changed. It
	notifies all Modules by Module::OnChangeSettings
	call
	*/
	void ChangeSettings();

	/*!
	Saves current settings.
	*/
	virtual void SaveSettings() { m_iSettingsChanged = 0;}

	RenderFont* GetSystemFont() const
	{
		return m_pFont;
	}

	CStr GetVersion() const;
	uint GetVersionID() const;
	virtual int HandleEvent(const Event& event);

	/*!
	Adds event handler into Engine
	\param eventhandler
	Pointer to handler
	\return
	True when inserted, false when already exists
	*/
	ebool AddEventHandler(EventHandler* eventhandler);

	/*!
	Removes event handler into Engine
	\param eventhandler
	Pointer to handler
	\return
	True when removed, false when do not exists
	*/
	ebool RemoveEventHandler(EventHandler* eventhandler);

	/*!
	Inserts functional module into Engine
	\param mod
	Pointer to Module
	\return
	True when inserted, false when already exists
	*/
	bool InsertModule(Module* mod);

	/*!
	Finds module by its typeid. All functional modules must be
	inserted into Engine.
	\param type
	ClassName+Type ID of Module
	\return
	Pointer to module or NULL
	*/
	Module* FindModule(uint type);

	/*!
	Removes functional Module from Engine
	\param mod
	Pointer to Module
	\return
	True when removed, false when not found
	*/
	bool RemoveModule(Module* mod);

	/*!
	Broadcasts event to all modules
	\param event
	Event to be sent
	*/
	void SendEvent(const Event& event);
	
	//!Check for event recursion
	bool IsSendingEvent(uint typeID) const;

	/*!
	Shows error message box.
	\param type
	Type of message. See enf::ErrorHandler::ErrorType
	\param message
	Error message
	*/
	void FatalError(ErrorHandler::ErrorType type, const char* message);

	/*!
	Redirects error message boxes to custom handler
	\param errhandler
	Pointer to ErrorHandler or NULL
	*/
	void SetErrorHandler(ErrorHandler* errhandler)
	{
		m_pErrorHandler = errhandler;
	}

	/*!
	Redirects console output to custom COutputStream
	\param stream
	Custom output stream
	\return
	Previous stream
	*/
	COutputStream* SetOutputStream(COutputStream* stream)
	{
		COutputStream* prev = m_pOutputStream;
		m_pOutputStream = stream;
		return prev;
	}

	/*!
	Redirects console error-output to custom COutputStream
	\param stream
	Custom error-output stream
	\return
	Previous stream
	*/
	COutputStream* SetErrorStream(COutputStream* stream)
	{
		COutputStream* prev = m_pOutputStream;
		m_pErrorStream = stream;
		return prev;
	}

	int FPS(uint64 ctime);

	/*!
	Used for passing kestrokes into console. Alphabet characters are passed
	as ASCII codes, Windows key-codes are passed with 0x8000 mask.
	\param chr
	ASCII code, or Windows key-code
	*/
	//void ProcessConsole(int chr);

	/*!
	Adds string to console. It always take one line.
	The string is also passed to console.log Optionaly
	this output can be redirected by enf::Engine::SetOutputStream()
	\param str String
	*/
	void ConsoleMessage(const char *str);

	/*!
	Adds string to console with ERROR: prefix. It always take one line.
	The string is also passed to console.log Optionaly
	this output can be redirected by enf::Engine::SetErrorStream()
	\param str String 
	*/
	void ErrorMessage(const char* str);

	virtual void BeforeReset();
	virtual void AfterReset();

	virtual uint Create(void* hWnd, CStr settings, CStr profileName)
	{
		return 0;
	}

	virtual bool Resize(uint width, uint height, uint fsaa, bool windowed, bool vsync)
	{
		return true;
	}

	virtual int Create(uint adapter, uint width, uint height, bool windowed)
	{
		return 0;
	}

	/*!
	Updates input device, if any
	\param flags See InputUpdateMask
	*/
	virtual void Input(uint flags) {}

	ERESULT RenderFrame(bool inscene);

	virtual int UpdateFrame(bool inscene);
	
	//!Update system specific services, outside UpdateFrame render.
	virtual void UpdateServices() {}

	virtual void ShowWindow(){}

	//!Return OS related window handle
	virtual void* GetWindow() const
	{
		return NULL;
	}

	void UpdateTime(uint64 timeStamp);
	void ClearTime();

	/*!
	Gets elapsed time since engine start. This is not exact time and might be scaled
	Use enf::Timer::GetTime/enf::Timer::GetTicks when needs precise time.
	\return Time in milliseconds
	*/
	double GetTime() const
	{
		return m_dTime;
	}

	/*!
	General scale factor of time. Default is 1.0 (no scale at all)
	\param timescale
	Scale factor, internally limited to 0.05 - 5.0 range
	\return
	Previous scale
	*/
	double SetTimeScale(double timescale);

	/*!
	Locks engine timing to constant timeslice
	\param timeslice
	The fixed timeslice in milliseconds, or -1, when you don't need it fixed
	\return
	Previous fixed timeslice
	*/
	double SetFixedTimeSlice(double timeslice);

	/*!
	Gets elapsed time for last frame. This is not exact time,
	but slightly interpolated to minimize jerkiness when OS load
	varies. Use Timer::GetTime/Timer::GetTicks when needs precise time.
	\return
	Time in miliseconds
	*/
	double GetTimeSlice() const
	{
		return m_dTickDelta;
	}

	virtual void Destroy();

	ENF_INLINE StaticBuffer* GetStaticMem() const
	{
		return m_pStatic;
	}

	ENF_INLINE EnforceScriptContext* GetScriptContext() const
	{
		return m_pScript;
	}

	ENF_INLINE Pipeline* GetPipeline() const
	{
		return m_pPipeline;
	}

	ENF_INLINE InputDevice* GetInputDevice() const
	{
		return m_pInput;
	}

	ENF_INLINE MaterialSystem* GetMaterialSystem() const
	{
		return m_pMaterialSystem;
	}

	ENF_INLINE ResourceManager* GetResourceManager() const
	{
		return m_pResourceManager;
	}

	ENF_INLINE WidgetManager* GetWidgetManager() const
	{
		return m_pWidgetManager;
	}

	ENF_INLINE Renderer* GetRender() const
	{
		return m_pRender;
	}

	ENF_INLINE FileSystem* GetFileSystem() const
	{
		return m_pFileSystem;
	}

	ENF_INLINE XSoundSystem* GetSoundSystem() const
	{
		return m_pXSoundSystem;
	}

	ENF_INLINE DecalManager* GetDecalManager() const
	{
		return m_pDecalManager;
	}

	ENF_INLINE AnimationSystem* GetAnimationSystem() const
	{
		return m_pAnimationSystem;
	}

	ENF_INLINE EntityManager* GetEntityManager() const
	{
		return m_pEntityManager;
	}

	ENF_INLINE WorldManager* GetWorldManager() const
	{
		return m_pWorldManager;
	}
	ENF_INLINE PhysicsManager* GetPhysicsManager() const
	{
		return m_pPhysicsManager;
	}

	// Video capture is unavailable in the DX11 client.
	virtual uint OpenVideo(const char* name = NULL) { (void)name; return 0; }

	/*!
	Stops video grabbing
	*/
	virtual void CloseVideo() {}

	/*!
	Appends frame to vide stream
	\param currenttime
	Current time in milliseconds
	*/
	virtual void AppendVideoFrame(double currenttime) 
	{}

	void SetCurrent();
	
	CThreadManager* GetThreadManager(uint index) const
	{
		return m_pThreadManagers[index];
	}

	bool DoQuit() const
	{
		return m_bAppQuit;
	}

	void CancelQuit()
	{
		m_bAppQuit = false;
	}

	//!System related reason for game pause (Xbox360 Guide screen for example)
	virtual bool IsPaused() const 
	{ 
		return m_bPaused; 
	}

	void SetPaused(bool paused);

	//!Respects video settings in XML
	void SetRuntimeMode(bool runtime)
	{
		m_bRuntimeMode = runtime;
	}

	bool GetRuntimeMode() const
	{
		return m_bRuntimeMode;
	}

	CStr GetProfileName() const
	{
		return m_strProfileName.cstr();
	}

	CStr GetMachineName() const
	{
		return m_strMachineName.cstr();
	}

	virtual SignInStatus GetSignInStatus() const
	{
		return SIS_SIGNED;
	}

	virtual uint SignIn(uint user)
	{
		return 0;
	}

	virtual uint GetCurrentSignedUser() const 
	{ 
		return 0; 
	}

	virtual uint GetSignedUsers() const 
	{ 
		return 1; 
	}

	virtual uint GetOnlineUsers() const 
	{ 
		return 1; 
	}

	bool IsUserSigned(uint user) const 
	{ 
		return (GetSignedUsers() & (1 << user)) != 0; 
	}
	
	bool IsUserOnline(uint user) const 
	{ 
		return (GetOnlineUsers() & (1 << user)) != 0; 
	}

	virtual uint ShowDeviceUI()
	{
		return 0;
	}

	virtual StorageStatus GetStorageStatus() const
	{
		return SS_READY;
	}

	virtual uint ShowMessageBoxUI(CStr szTitle, CStr szText, uint cButtons, CStr* pszbuttons, uint focusButton, MessageBoxButtons buttons)
	{
		return (unsigned)-1;
	}

	//!tells to engine whether we are loading something, so we want to allow update in background instead of process suspend
	ENF_INLINE void BeginLoad()
	{
		m_iLoading++;
	}

	ENF_INLINE void EndLoad()
	{
		enf_assert(m_iLoading);
		--m_iLoading;
	}

	ENF_INLINE bool IsLoading() const
	{
		return m_iLoading > 0;
	}

	ENF_INLINE uint GetFrameCount() const
	{
		return (uint)m_iFrameCount;
	}

	//! Enable or disable force update
	void SetForceUpdate( bool bForceUpdate )
	{ 
		m_bAlwaysUpdate = bForceUpdate; 
	}; 

	//! Determine current force update state
	const bool GetForceUpdate( void ) const
	{ 
		return m_bAlwaysUpdate; 
	}; 

protected:
	CMutex								m_Mutex;
	DString								m_strSettingsFileName;
	TiXmlDocument*				m_pSettings;
	int										m_iSettingsChanged;

	IL_INT32							m_iFrameCount;
	uint									m_iLoading;
	bool									m_bPaused;
	bool									m_bAlwaysUpdate;

	CThreadManager*				m_pThreadManagers[8];
	StaticBuffer*					m_pStatic;
	EnforceScriptContext*	m_pScript;
	Pipeline*							m_pPipeline;
	PathEngine*						m_pPathEngine;

	FileSystem*						m_pFileSystem;
	InputDevice*					m_pInput;
	Renderer*							m_pRender;
	DecalManager*					m_pDecalManager;
	XSoundSystem*					m_pXSoundSystem;
	ParticleSystem*				m_pParticleSystem;
	MaterialSystem*				m_pMaterialSystem;
	ResourceManager*			m_pResourceManager;
	WidgetManager*				m_pWidgetManager;
	AnimationSystem*			m_pAnimationSystem;
	EntityManager*				m_pEntityManager;
	WorldManager*					m_pWorldManager;
	PhysicsManager*				m_pPhysicsManager;

	Set<CLIParam>					m_Params;
	Array<Module*>				m_Modules;
	Array<uint>						m_SentEvents;
	Set<EventHandler*>		m_EventHandlers;
	SString<256>					m_strFrameStats; // String to hold frame stats

	SString<256>					m_strAppName;
	SString<32>						m_strMachineName;
	SString<64>						m_strProfileName;

	bool									m_bAppQuit;
	uint64								m_iLastTime;
	double								m_dTickDelta;
	double								m_dTime;
	double								m_dFixedTimeSlice;
	double								m_dTimeScale;

#ifdef ENF_TIME_INTERPOLATE
	static const uint MAX_TIMES = 4;
	uint64								m_iTimes[MAX_TIMES];
	uint									m_iBottomTime;
	uint									m_iTopTime;
#endif

	COutputStream*				m_pOutputStream;
	COutputStream*				m_pErrorStream;
	ErrorHandler*					m_pErrorHandler;

	ConsoleWidget*				m_pConsole;

	RenderFont*						m_pFont;

	SString<1024>					m_strDiagBuff;
	uint64								m_iLastDiagTime;
	int										m_iDiagActiveMenu;
	bool									m_bDiagShowMenu;
	bool									m_bRuntimeMode;

	void ParseParams(const char* cliparams);
	void ProcessStatistics(uint64 timeStamp);
	void ProcessDiagMenu(uint64 timeStamp);
	void ProcessDiagControl();

	virtual void OnBeginScene() 
	{}

	virtual void OnRender();

	virtual void OnEndScene() 
	{}

	// Internal constructor
	Engine(CStr name, CStr cliparams, size_t staticPoolSize);
};

//-----------------------------------------------------------------------------
class PUBLIC_API ScriptConsoleHandler: public EventHandler
{
protected:
	Engine*	m_pEngine;
	ScriptContext* m_pContext;

	bool ProcessCommand(CParser& tp, ScriptModule* mod);

public:
	ScriptConsoleHandler(): m_pEngine(NULL), m_pContext(NULL)
	{
	}

	ScriptConsoleHandler(Engine* eng, ScriptContext* ctx): m_pEngine(eng), m_pContext(ctx)
	{
	}

	int HandleEvent(const Event& ev);
};


PUBLIC_API Engine* GetEngine();
PUBLIC_API bool IsEngine();
//@}
}

#endif //ENF_ENGINE_H
