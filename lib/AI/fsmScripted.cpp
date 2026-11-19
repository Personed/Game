#include "../wpch.hpp"

#include "fsmScripted.hpp"
#include <Es/Algorithms/qsort.hpp>
#include <El/PreprocC/preprocC.hpp>
#include <El/Common/perfProf.hpp>
#include <El/Evaluator/express.hpp>
#include "../paramArchiveExt.hpp"
#include "../global.hpp"
#include "../gameStateExt.hpp"

#define LOG_FSM 0

// when PROFILE_FSM is set, descriptions of expressions are passed to the evaluator
#if _ENABLE_CHEATS
#define PROFILE_FSM 1
#else
#define PROFILE_FSM 0
#endif

#if DEBUG_FSM
#include <Es/Common/win.h>
#include <shellapi.h>
#endif

static RString PreprocessStatement(RString statement)
{
  StringPreprocessor preproc;
  QOStrStream out;
  if (!preproc.Process(&out, statement)) return RString();
  return RString(out.str(), out.pcount());
}

void FSMScriptedType::StateInfo::LinkInfo::Load(ParamEntryPar cls, const FSMScriptedType &fsm, const StateInfo &from, GameDataNamespace *globals)
{
  _priority = cls >> "priority";

  _condition = PreprocessStatement(cls >> "condition");
  _action = PreprocessStatement(cls >> "action");
  ConstParamEntryPtr entry = cls.FindEntry("precondition");
  if (entry) _precondition = PreprocessStatement(*entry);
  entry = cls.FindEntry("itemno");
  if (entry) _itemNo = entry->GetInt();
  else _itemNo = -1;

  RString to = cls >> "to";
  _nextState = fsm.FindStateIndex(to);
  if (_nextState==UnchangedStateScripted)
  {
    // Error: FSM has been probably changed after the Load (old save, not compatible)
    ErrorMessage("Incompatible game FSMs in between Save and later Load.");
    return;
  }

#if USE_PRECOMPILATION
  GGameState.CompileMultiple(_condition, _conditionExp, globals);
  GGameState.CompileMultiple(_action, _actionExp, globals);
  GGameState.CompileMultiple(_precondition, _preconditionExp, globals);
#endif

#if PROFILE_FSM
  RString nameFrom = from.GetName();
  RString nameTo = fsm._states[_nextState].GetName();
  RString prefix = Format("%s: %s -> %s: ", cc_cast(fsm.GetName()), cc_cast(nameFrom), cc_cast(nameTo));
  _conditionName = prefix + RString("condition");
  _actionName = prefix + RString("action");
  _preconditionName = prefix + RString("precondition");
#endif
}

void FSMScriptedType::StateInfo::Load(ParamEntryPar cls, const FSMScriptedType &fsm, GameDataNamespace *globals)
{
  _id = cls.GetName();
  _name = cls >> "name";

  _init = PreprocessStatement(cls >> "init");
  ConstParamEntryPtr entry = cls.FindEntry("precondition");
  if (entry) _precondition = PreprocessStatement(*entry);
  entry = cls.FindEntry("itemno");
  if (entry) _itemNo = entry->GetInt();
  else _itemNo = -1;

#if USE_PRECOMPILATION
  GGameState.CompileMultiple(_init, _initExp, globals);
  GGameState.CompileMultiple(_precondition, _preconditionExp, globals);
#endif

#if PROFILE_FSM
  RString prefix = Format("%s: %s: ", cc_cast(fsm.GetName()), cc_cast(_name));
  _initName = prefix + RString("init");
  _preconditionName = prefix + RString("precondition");
#endif
}

void FSMScriptedType::StateInfo::SetFinal()
{
  _links.Realloc(1);
  _links.Resize(1);
  _links[0].LinkToFinal();
}

static int CmpLinksByPriority(const FSMScriptedType::StateInfo::LinkInfo *l0, const FSMScriptedType::StateInfo::LinkInfo *l1)
{
  return sign(l1->GetPriority() - l0->GetPriority());
}

void FSMScriptedType::StateInfo::LoadLinks(ParamEntryPar cls, const FSMScriptedType &fsm, GameDataNamespace *globals)
{
  ParamEntryVal links = cls >> "Links";
  int n = links.GetEntryCount();
  for (int i=0; i<n; i++)
  {
    ParamEntryVal entry = links.GetEntry(i);
    if (entry.IsClass())
    {
      int index = _links.Add();
      _links[index].Load(entry, fsm, *this, globals);
    }
  }
  _links.Compact();
  QSort(_links.Data(), _links.Size(), CmpLinksByPriority);
}

/// check if some processing does not took too much time
#define CHECK_FSM_PERF 1

#if CHECK_FSM_PERF && _ENABLE_PERFLOG

#include <El/HiResTime/hiResTime.hpp>
static const float fsmTimeLimit = 0.003;

/// check if FSM execution took longer than normal
struct GuardFSMScopeTime
{
  const FSMScriptedType *_type;
  const char *_stateName;
  const char *_action;
  SectionTimeHandle _start;

  GuardFSMScopeTime(const FSMScriptedType *type, const char *stateName, const char *action)
    :_type(type), _stateName(stateName), _action(action)
  {
    _start = StartSectionTime();
  }
  ~GuardFSMScopeTime()
  {
    float time = GetSectionTime(_start);
    if (time > fsmTimeLimit)
    {
      LogF("FSM %s, state %s %s takes %.1f ms", cc_cast(_type->GetDisplayName()), _stateName, _action, 1000 * time);
    }
  }
};

#else

/// do nothing
struct GuardFSMScopeTime
{
  GuardFSMScopeTime(FSMScriptedType *type, const char *stateName, const char *action) {}
};

#endif


void FSMScriptedType::StateInfo::Init(const FSMScriptedType *type, GameDataNamespace *globals) const
{
#if CHECK_FSM_PERF && _ENABLE_PERFLOG
  PROFILE_SCOPE_EX(fsmIn,fsm);
  if (PROFILE_SCOPE_NAME(fsmIn).IsActive())
  {
    PROFILE_SCOPE_NAME(fsmIn).AddMoreInfo(type->GetDisplayName()+"-"+_name);
  }
  
  GuardFSMScopeTime scope(type, _name, "init");
#endif

#if USE_PRECOMPILATION
  GGameState.Execute(_init,_initExp, GameState::EvalContext::_reportUndefined, globals, _initName);
#else
  GGameState.Execute(_init, GameState::EvalContext::_reportUndefined, globals);
#endif
}

FSM::State FSMScriptedType::StateInfo::Check(const FSMScriptedType *type, GameDataNamespace *globals, int &linkIx) const
{
#if CHECK_FSM_PERF && _ENABLE_PERFLOG
  GuardFSMScopeTime scope(type, _name, "update");
#endif

  ExecutePrecondition(globals);
  for (int i=0; i<_links.Size(); i++)
  {
    const LinkInfo &link = _links[i];
    link.ExecutePrecondition(globals);
    bool ok = link.EvaluateCondition(globals);
    //bool ok = true;
    if (ok)
    {
      linkIx = i; // serves as handle to get info needed for possible DebugLogFSM
      // transition to a new state
      link.ExecuteAction(globals);
      return link.GetNextState();
    }
  }

  return UnchangedStateScripted;
}

void FSMScriptedType::StateInfo::DebugLogProcessedCondition(int linkIx, int id, RString name) const
{
  if ( linkIx<_links.Size() && linkIx>=0 ) //valid index
    DebugLogFSM("DebugFSM: %d,%d \"%s\" condition: %d \"%s\"", id, _links[linkIx].GetItemNo(), cc_cast(name), linkIx, cc_cast(_links[linkIx].ConditionName()) );
}

void FSMScriptedType::RequestLoading()
{
  // open the file
  QIFStreamB in;
  in.AutoOpen(_name);
  if (in.fail())
  {
    // file does not exist or is corrupted
    WarningMessage("FSM '%s' cannot be loaded.", cc_cast(_name));
    // report and try to recover
    _initState = FSM::FinalState;
    _loaded = true;
    return;
  }
  // ask for pre-read
  in.PreReadObject(this, NULL, FileRequestPriority(300));
}

int FSMScriptedType::ProcessingThreadId() const
{
  return 0;
}

void FSMScriptedType::RequestDone(RequestContext *context, RequestResult result)
{
  // whole file is ready - we may safely open it
  WaitUntilLoaded();
  RequestableObject::RequestDone(context,result);
}

void FSMScriptedType::WaitUntilLoaded()
{
  if (_loaded) return;
  _loaded = true;

  // load the content now
  GameDataNamespace globals(NULL, RString(), false); // TODO: parsing namespace if access to globals needed
  ParamFile file;
  file.Parse(_name, NULL, NULL, &globals);
  ParamEntryPar cls = file >> "FSM";

  _displayName = cls >> "fsmName";

  // this namespace is used not for parsing, but evaluation (calls of Compile, CompileMultiple)
  GameDataNamespace globalsEval(NULL, RString(), false); // we do not know yet in which context the FSMs will run, so we cannot check global variables in compile time

  // States
  ParamEntryVal states = cls >> "States";
  int n = states.GetEntryCount();
  for (int i=0; i<n; i++)
  {
    ParamEntryVal entry = states.GetEntry(i);
    if (entry.IsClass())
    {
      int index = _states.Add();
      _states[index].Load(entry, *this, &globalsEval);
    }
  }

  // init state
  _initState = FindStateIndex(cls >> "initState");
  if (_initState < 0)
  {
    Fail("Wrong init state");
    _initState = 0;
  }

  // final states
  AUTO_STATIC_ARRAY(RString, finalStates, 16);
  ParamEntryVal array = cls >> "finalStates";
  for (int i=0; i<array.GetSize(); i++) finalStates.Add(array[i]);

  // links
  int index = 0;
  for (int i=0; i<n; i++)
  {
    ParamEntryVal entry = states.GetEntry(i);
    if (entry.IsClass())
    {
      StateInfo &state = _states[index++];
      RString id = state.GetId();
      bool final = false;
      for (int j=0; j<finalStates.Size(); j++)
      {
        if (stricmp(finalStates[j], id) == 0)
        {
          final = true; break;
        }
      }
      if (final) state.SetFinal();
      else state.LoadLinks(entry, *this, &globalsEval);
    }
  }
}

int FSMScriptedType::FindStateIndex(RString id) const
{
  for (int i=0; i<_states.Size(); i++)
  {
    if (stricmp(_states[i].GetId(), id) == 0) return i;
  }
  return UnchangedStateScripted;
}

FSMScriptedTypeBank FSMScriptedTypes;

FSMScripted::FSMScripted(const FSMScriptedType *type, GameDataNamespace *globals, GameVarSpace *parentVars)
: _globals(globals), _vars(parentVars, true)
{
  DoAssert(type);
  _type = type;
  _curState = InitState;
  _initNeeded = true;
  _debugLog = false;
#if DEBUG_FSM
  _threadId = 0;
#endif  
}

FSMScripted::~FSMScripted()
{
#if DEBUG_FSM
  if (_threadId != 0) PostThreadMessage(_threadId, WM_QUIT, 0, 0);
  if (_tempFile.GetLength() > 0) QIFileFunctions::Unlink(_tempFile);
#endif  
}

void FSMScripted::SetParam(RString name, GameValuePar value)
{
  RString varName = name;
  varName.Lower();
  _vars.VarLocal(varName);
  _vars.VarSet(varName, value, false);
}

// FSM implementation
void FSMScripted::SetState(State state, Context *context)
{
  // avoid direct change of state
}

const char *FSMScripted::GetStateName() const
{
  if (_curState==InitState) return "<Init>";
  return CurState().GetName();
}

int &FSMScripted::Var(int i)
{
  static int dummy = 0;
  return dummy;
}

int FSMScripted::Var(int i) const
{
  return 0;
}

#if _HELISIM_LARGE_OBJECTID
OIDInt &FSMScripted::VarId()
{
  static OIDInt dummy = 0;
  return dummy;
}
OIDInt FSMScripted::VarId() const
{
  return 0;
}
#endif // _HELISIM_LARGE_OBJECTID

Time &FSMScripted::VarTime(int i)
{
  static Time dummy(0);
  return dummy;
}

Time FSMScripted::VarTime(int i) const
{
  return Time(0);
}

void FSMScripted::SetTimeOut(float sec)
{
  _timeOut = Glob.time + sec;
}

float FSMScripted::GetTimeOut() const
{
  return _timeOut - Glob.time;
}

bool FSMScripted::TimedOut() const
{
  return Glob.time > _timeOut;
}

#if DEBUG_FSM
static RString ExportFSM(RString name)
{
  char dir[MAX_PATH];
  if (GetTempPath(MAX_PATH, dir) == 0) return RString();
  RString path;
  for (int i=0; i<100000; i++)
  {
    path = Format("%s%s %d.fsm", dir, AppName, i);
    if (!QIFileFunctions::FileExists(path)) break;
  }
  QIFStreamB in;
  in.AutoOpen(name);
  QOFStream out;
  out.open(path);
  in.copy(out);
  return path;
}

void FSMScripted::Debug()
{
  if (_threadId != 0)
  {
    PostThreadMessage(_threadId, WM_QUIT, 0, 0);
    _threadId = 0;
  }
  if (_tempFile.GetLength() > 0)
  {
    QIFileFunctions::Unlink(_tempFile);
    _tempFile = RString();
  }

  if (!_type) return;

  RString fsmPath = ExportFSM(_type->GetName());
  if (fsmPath.GetLength() > 0)
  {
    char exePath[MAX_PATH];
    int error = (int)FindExecutable(fsmPath, NULL, exePath);
    if (error > 32) // FindExecutable succeeded
    {
      char commandLine[1024];
      sprintf(commandLine, "\"%s\" \"%s\"", cc_cast(exePath), cc_cast(fsmPath));
      STARTUPINFO startupInfo;
      memset(&startupInfo, 0, sizeof(startupInfo));
      startupInfo.cb = sizeof(startupInfo); 
      PROCESS_INFORMATION pi;
      BOOL ok = CreateProcess(NULL, commandLine,
        NULL, NULL, // security
        FALSE, 0,
        NULL, NULL,
        &startupInfo, &pi
        );
      if (ok)
      {
        _threadId = pi.dwThreadId;
        _tempFile = fsmPath;
        if (_curState != FinalState)
        {
          // show the correct state in the debugger
          
          // wait until message queue is created
          WaitForInputIdle(pi.hProcess, INFINITE);
/*
          HANDLE processReady = CreateEvent(NULL, FALSE, FALSE, NULL);
          if (processReady)
          {
            HANDLE handle;
            if (DuplicateHandle(GetCurrentProcess(), processReady, pi.hProcess, &handle, 0, FALSE, DUPLICATE_SAME_ACCESS))
            {
              if (PostThreadMessage(_threadId, MSG_FSM_EVENT_HANDLE, (WPARAM)handle, 0))
              {
                WaitForSingleObject(processReady, INFINITE);
                PostThreadMessage(_threadId, MSG_FSM_STATE_CHANGED, _curState, 0);
              }
            }
            CloseHandle(processReady);
          }
*/
          PostThreadMessage(_threadId, MSG_FSM_STATE_CHANGED, _curState, 0);
        }
      
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
      }
    }
  }
}

#endif

bool FSMScripted::DoUpdate(Context *context)
{
  DoAssert(_type->IsLoaded());

  if (_curState == FinalState) return true;
  if (_initNeeded)
  {
    // initialization of init state
    if (LOG_FSM || DEBUG_FSM && _threadId != 0)
    {
      LogF("FSM %s: State %s (%d) reached", cc_cast(_type->GetDisplayName()), cc_cast(CurState().GetName()),_curState);
    }
    CurState().Init(_type, _globals);
    _initNeeded = false;
  }

  int linkIx = -1;
  State newState = CurState().Check(_type, _globals, linkIx);
  if (newState == UnchangedStateScripted) return false;
  
  if (newState != _curState)
  {
    // possible DebugLogFSM
    if (_debugLog)
    {
      if ( linkIx>=0 )
        CurState().DebugLogProcessedCondition(linkIx, GetId(), cc_cast(_type->GetDisplayName()));
    }
    // new state reached
    _curState = newState;
    if (_curState == FinalState)
    {
      if (LOG_FSM || DEBUG_FSM && _threadId != 0)
        LogF("FSM %s: Final state reached", cc_cast(_type->GetDisplayName()));
      return true;
    }
#if DEBUG_FSM
    if (_threadId != 0)
    {
      PostThreadMessage(_threadId, MSG_FSM_STATE_CHANGED, newState, 0);
      // consider switching focus to the debugger so that we can see the state
      // this can be very handy for fast reactive FSMs, like conversation ones
    }
#endif
    if (_debugLog)
      DebugLogFSM("DebugFSM: %d,%d \"%s\" state: %d \"%s\"", GetId(), CurState().GetItemNo(), cc_cast(_type->GetName()), newState, cc_cast(CurState().GetName()) );
  }
  if (LOG_FSM || DEBUG_FSM && _threadId != 0)
  {
    LogF("FSM %s: State %s (%d) reached", cc_cast(_type->GetDisplayName()), cc_cast(CurState().GetName()),_curState);
  }
  CurState().Init(_type, _globals);
  return false;
}

bool FSMScripted::Update(Context *context)
{
  if (!_type->IsLoaded()) return false; // not ready yet, wait a bit

#if _ENABLE_PERFLOG
  if (!_type->_counterInitialized)
  {
    _type->_counterInitialized = true;
    _type->_counter.Init(_type->GetDisplayName().Substring(0, 16), "FSM", &GPerfProfilers, PROF_COUNT_SCALE);
  }
  ScopeProfiler scopeCounter(_type->_counter, _type->_counter.TimingEnabled());
#endif

	PROFILE_SCOPE_EX(fsmSc,*);

  if (_curState == InitState) _curState = _type->_initState; // patch the init state
  
  if (PROFILE_SCOPE_NAME(fsmSc).IsActive())
  {
    PROFILE_SCOPE_NAME(fsmSc).AddMoreInfo(Format("%s:%s",cc_cast(_type->GetDisplayName()),cc_cast(GetStateName())));
  }

  int gshandle = GGameState.BeginContext(&_vars);
  ApplyContext(GGameState, context);
  bool finished = DoUpdate(context);
  GGameState.EndContext(gshandle);
  return finished;
}

bool FSMScripted::IsExclusive(Context *context)
{
  int gshandle = GGameState.BeginContext(&_vars);
  GameValue value;
  _vars.VarGet("_fsmexclusive",value);
  GGameState.EndContext(gshandle);
  if (value.GetType()!=GameBool)
  {
    return false;
  }
  return bool(value);
}

void FSMScripted::Refresh(Context *context)
{
  if (!_type->IsLoaded()) return; // not ready yet, wait a bit
  if (_curState == InitState) _curState = _type->_initState; // patch the init state

  int gshandle = GGameState.BeginContext(&_vars);
  ApplyContext(GGameState, context);
  if (_curState != FinalState) CurState().Init(_type, _globals);
  _initNeeded = false;
  GGameState.EndContext(gshandle);
}

void FSMScripted::Enter(Context *context)
{
  // not implemented
}

void FSMScripted::Exit(Context *context)
{
  // not implemented
}

bool FSMScripted::Evaluate(GameValue &result, const char *expression)
{
  int gshandle = GGameState.BeginContext(&_vars);
  result = GGameState.Evaluate(expression, GameState::EvalContext::_default, _globals);
  GGameState.EndContext(gshandle);
  return true;
}

LSError FSMScripted::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))

  if (ar.IsSaving())
  {
    // we need to access the type and its structure
    if (_type)
    {
      unconst_cast(_type.GetRef())->WaitUntilLoaded();
      if (_curState == InitState) _curState = _type->_initState; // patch the init state
    }

    RString name = _type ? _type->GetName() : RString();
    CHECK(ar.Serialize("name", name, 22))

    RString id;
    if (_curState != FinalState) id = CurState().GetId();
    CHECK(ar.Serialize("curState", id, 1))
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    // try to recover the namespace
    _globals = GGameState.GetGlobalVariables();

    RString name;
    CHECK(ar.Serialize("name", name, 22, RString()))
    if (name.GetLength() > 0) _type = FSMScriptedTypes.New(name);

    // we need to access the type and its structure
    if (_type) unconst_cast(_type.GetRef())->WaitUntilLoaded();

    RString id;
    CHECK(ar.Serialize("curState", id, 1))
    _curState = FinalState;
    if (id.GetLength() > 0) 
    {
      _curState = _type->FindStateIndex(id);
      if (_curState == UnchangedStateScripted) 
      {
        ErrF("Incompatible game FSMs in between Save and later Load.");
        return LSStructure; 
      }
    }
  }
  CHECK(::Serialize(ar, "timeOut", _timeOut, 1))
  CHECK(ar.Serialize("initNeeded", _initNeeded, 1, false))

  // set archive context to gamestate
  void *old = ar.GetParams();
  ar.SetParams(&GGameState);
  DoAssert(_vars.IsSerializationEnabled());
  CHECK(ar.Serialize("Vars", _vars._vars, 1))
  ar.SetParams(old);

  return LSOK;
}

DEFINE_SERIALIZE_TYPE_INFO(FSMScripted, FSM)

/*
FSM *CreateFSMFSMScripted()
{
  return new FSMScripted(SerializeConstructor);
}

FSMScripted::FSMTypeInfoFSMScripted::FSMTypeInfoFSMScripted()
{
  _typeName = "FSMScripted";
  _createFunc = &CreateFSMFSMScripted;

  // connect to the list of infos in the root
  _next = FSM::_typeInfoRoot;
  FSM::_typeInfoRoot = this;
}

FSMScripted::FSMTypeInfoFSMScripted FSMScripted::_typeInfo;
*/
