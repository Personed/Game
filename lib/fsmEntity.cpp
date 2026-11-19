#include "wpch.hpp"

#include "fsmEntity.hpp"

#include <Es/Algorithms/qsort.hpp>
#include <Es/Common/delegate.hpp>
#include <El/Common/randomGen.hpp>
#include <Es/ReportStack/reportStack.hpp>

#include "paramFileExt.hpp"
#include "paramArchiveExt.hpp"
#include "global.hpp"

#if _SUPER_RELEASE
#define LOG_FSM 0
#define DEBUG_SEAGULL 0
#elif _PROFILE || _DEBUG
# define LOG_FSM 0
# define DEBUG_SEAGULL 0
#else
#define LOG_FSM 0
#define DEBUG_SEAGULL 0
#endif

#if DEBUG_SEAGULL
#include "seaGull.hpp"
extern volatile int debugID;
#endif

#if DEBUG_FSM
#include <Es/Common/win.h>
#include <shellapi.h>
#endif

static bool TestNegation(RString &functionName)
{
  //find first alphabetic char
  int i=0;
  int size=functionName.GetLength();
  for (; i<size; i++)
  {
    if (isalpha((unsigned char)functionName[i])) break;
  }
  if (!i) return false;
  //there are some non alpha characters, test whether it is negation "1-xxx" form
  RString prefix = functionName.Substring(0,i);
  functionName = functionName.Substring(i,size); //change the functionName not to contain prefix
  int k=0;
  char *prefixMut = prefix.MutableData();
  for (int j=0; j<i; j++)
  {
    char c = prefix[j];
    if (!((c >= 0x9 && c <= 0xD) || (c == 0x20))) //!isspace
      prefixMut[k++]=prefix[j];
  }
  if (k!=i) prefixMut[k]=0; //end it
  if (prefix=="1-") return true;
  return false;
}

static bool IsSpecialCode(RString code)
{
  // now, only scripts are handled as a special code
  static const char *prefix = "script:";
  if (strnicmp(code, prefix, strlen(prefix)) == 0) return true;

  return false;
}

void FSMEntityCondition::Load(ParamEntryPar cls, const FSMEntityTypeName &type)
{
  RString functionName = cls >> "function";
  if (IsSpecialCode(functionName))
  {
    _function = NULL;
    _text = functionName;
  }
  else
  {
    // the function can have 1-funcName prefix, which should set negateFunction to true
    negateFunction = TestNegation(functionName);
    _function = (*type._mapCondition)(functionName);
    if (!_function)
    {
      Fail("Wrong condition");
    }
  }
  ParamEntryVal parameters = cls >> "parameters";
  int n = parameters.GetSize();
  _parameters.Realloc(n);
  _parameters.Resize(n);
  for (int i=0; i<n; i++) _parameters[i] = parameters[i];
  _threshold = cls >> "threshold";
  if (_threshold < 0)
  {
    Fail("Wrong threshold");
    _threshold = 0;
  }
}

bool FSMEntityCondition::Process(const FSMEntity *fsm, Entity *context, const AutoArray<float> &thresholds) const
{
  float value;

  if (_function)
  {
    value = (*_function)(context,fsm, _parameters.Data(), _parameters.Size());
    if (negateFunction) value = 1-value;
  }
  else
  {
    value = context->FSMUnhandledCondition(_text);
  }
  return value >= thresholds[_threshold];
}

void FSMEntityAction::Load(ParamEntryPar cls, const FSMEntityTypeName &type)
{
  RString functionName = cls >> "function";
  if (IsSpecialCode(functionName))
  {
    _functionIn = NULL;
    _functionOut = NULL;
    _text = functionName;
  }
  else
  {
    _functionIn = (*type._mapActionIn)(functionName);
    _functionOut = (*type._mapActionOut)(functionName);
    if (!_functionIn || !_functionOut)
    {
      Fail("Wrong action");
    }
  }
  ParamEntryVal parameters = cls >> "parameters";
  int n = parameters.GetSize();
  _parameters.Realloc(n);
  _parameters.Resize(n);
  for (int i=0; i<n; i++) _parameters[i] = parameters[i];
  
  ParamEntryVal thresholds = cls >> "thresholds";
  n = thresholds.GetSize();
  _thresholds.Resize(0);
  for (int i=0; i<n; i++)
  {
    const IParamArrayValue &item = thresholds[i];
    if (item.GetItemCount() == 3)
    {
      int index = _thresholds.Add();
      _thresholds[index]._index = *item.GetItem(0);
      _thresholds[index]._min = *item.GetItem(1);
      _thresholds[index]._max = *item.GetItem(2);
    }
    else
    {
      RptF("Wrong thresholds structure");
    }
  }
  _thresholds.Compact();
}

void FSMEntityAction::ProcessIn(FSMEntity *fsm, Entity *context, AutoArray<float> &thresholds) const
{
  for (int i=0, n=_thresholds.Size(); i<n; i++)
  {
    thresholds[_thresholds[i]._index] = _thresholds[i]._min + (_thresholds[i]._max - _thresholds[i]._min) * GRandGen.RandomValue();
  }
  if (_functionIn)
  {
    (*_functionIn)(context, fsm, _parameters.Data(), _parameters.Size());
  }
  else
  {
    context->FSMUnhandledAction(_text);
  }
}

void FSMEntityAction::ProcessOut(FSMEntity *fsm, Entity *context) const
{
  if (_functionOut) (*_functionOut)(context, fsm, _parameters.Data(), _parameters.Size());
}

void FSMEntityType::StateInfo::LinkInfo::Load(ParamEntryPar cls, const FSMEntityTypeName &type, const FSMEntityType &fsm)
{
  REPORTSTACKITEM(cls.GetName());
  _priority = cls >> "priority";
  _condition.Load(cls >> "condition", type);
  _action.Load(cls >> "action", type);
  RString to = cls >> "to";
  _nextState = fsm.FindStateIndex(to);
}

void FSMEntityType::StateInfo::Load(ParamEntryPar cls, const FSMEntityTypeName &type)
{
  _id = cls.GetName();
  _name = cls >> "name";
#if LOG_FSM
  LogF("FSM: loaded state %s", _name.Data());
#endif
  _action.Load(cls >> "Init", type);
}

static int CmpLinksByPriority(const FSMEntityType::StateInfo::LinkInfo *l0, const FSMEntityType::StateInfo::LinkInfo *l1)
{
  return sign(l1->GetPriority() - l0->GetPriority());
}

void FSMEntityType::StateInfo::LoadLinks(ParamEntryPar cls, const FSMEntityTypeName &type, const FSMEntityType &fsm)
{
  ParamEntryVal links = cls >> "Links";
  int n = links.GetEntryCount();
  for (int i=0; i<n; i++)
  {
    ParamEntryVal entry = links.GetEntry(i);
    if (entry.IsClass())
    {
      int index = _links.Add();
      _links[index].Load(entry, type, fsm);
    }
  }
  _links.Compact();
  QSort(_links.Data(), _links.Size(), CmpLinksByPriority);
}

void FSMEntityType::StateInfo::SetFinal()
{
  _links.Realloc(1);
  _links.Resize(1);
  _links[0].LinkToFinal();
}

void FSMEntityType::StateInfo::Init(FSMEntity *fsm, Context *context, AutoArray<float> &thresholds)
{
  _action.ProcessIn(fsm, reinterpret_cast<Entity *>(context), thresholds);
}

void FSMEntityType::StateInfo::Exit(FSMEntity *fsm, Context *context)
{
  _action.ProcessOut(fsm, reinterpret_cast<Entity *>(context));
}

void FSMEntityType::StateInfo::LinkInfo::LinkToFinal()
{
  _priority = 1; _nextState = FSM::FinalState; 
  _condition._threshold=0;
  _condition._function = ::CreateEntityConditionFunc(&Entity::FSMTrue);
  _action._functionIn = ::CreateEntityActionFunc(&Entity::FSMNothingIn);
  _action._functionOut = ::CreateEntityActionFunc(&Entity::FSMNothingOut);
}

FSM::State FSMEntityType::StateInfo::Check(FSMEntity *fsm, Context *context, AutoArray<float> &thresholds)
{
  for (int i=0; i<_links.Size(); i++)
  {
    LinkInfo &link = _links[i];
    if (link.GetCondition().Process(fsm, reinterpret_cast<Entity *>(context), thresholds))
    {
      // transition to a new state
      link.GetAction().ProcessIn(fsm, reinterpret_cast<Entity *>(context), thresholds);
      link.GetAction().ProcessOut(fsm, reinterpret_cast<Entity *>(context));
      return link.GetNextState();
    }
  }
  return UnchangedStateEntity;
}

FSM::State FSMEntityType::StateInfo::CheckAtomic(FSMEntity *fsm, Context *context, AutoArray<float> &thresholds)
{
  if (_links.Size()!=1) return UnchangedStateEntity;
  LinkInfo &link = _links[0];
  const FSMEntityCondition &cond = link.GetCondition();
  if (!cond._function || (*cond._function) != &Entity::FSMTrue || cond.negateFunction || 1.0f < thresholds[cond._threshold])
    return UnchangedStateEntity;
  
  // transition to a new state
  link.GetAction().ProcessIn(fsm, reinterpret_cast<Entity *>(context), thresholds);
  link.GetAction().ProcessOut(fsm, reinterpret_cast<Entity *>(context));
  return link.GetNextState();
}

FSMEntityType::FSMEntityType(const FSMEntityTypeName &name)
: _name(name)
{
  REPORTSTACKITEM(name._name);
  Load(Pars >> "CfgFSMs" >> name._name, name);
}

void FSMEntityType::Load(ParamEntryPar cls, const FSMEntityTypeName &type)
{
  // States
  ParamEntryVal states = cls >> "States";
  int n = states.GetEntryCount();
  for (int i=0; i<n; i++)
  {
    ParamEntryVal entry = states.GetEntry(i);
    if (entry.IsClass())
    {
      int index = _states.Add();
      _states[index].Load(entry, type);
    }
  }

  // init state
  _initState = FindStateIndex(cls >> "initState");
  if (_initState < 0)
  {
    RptF("Wrong init state");
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
      else state.LoadLinks(entry, type, *this);
    }
  }
}

int FSMEntityType::FindStateIndex(RString id) const
{
  for (int i=0; i<_states.Size(); i++)
  {
    if (stricmp(_states[i].GetId(), id) == 0) return i;
  }
  return UnchangedStateEntity;
}

// FSM implementation
FSMEntity::FSMEntity(FSMEntityType *type)
{
  DoAssert(type);
  _type = type;
  _curState = type->_initState;
  _initNeeded = true;
  _isExclusive = false;
  _debugLog = false;

#if DEBUG_FSM
  _threadId = 0;
#endif  

  // initialize thresholds
  int maxIndex = -1;
  for (int i=0, n=type->_states.Size(); i<n; i++)
  {
    const FSMEntityAction &init = type->_states[i].GetAction();
    for (int j=0, m=init._thresholds.Size(); j<m; j++)
    {
      saturateMax(maxIndex, init._thresholds[j]._index);
    }
    const AutoArray<FSMEntityType::StateInfo::LinkInfo> &links = type->_states[i].GetLinks();
    for (int j=0, m=links.Size(); j<m; j++)
    {
      const FSMEntityCondition &condition = links[j].GetCondition();
      saturateMax(maxIndex, condition._threshold);
      const FSMEntityAction &action = links[j].GetAction();
      for (int k=0, mn=action._thresholds.Size(); k<mn; k++)
      {
        saturateMax(maxIndex, action._thresholds[k]._index);
      }
    }
  }
  _thresholds.Realloc(maxIndex + 1);
  _thresholds.Resize(maxIndex + 1);
  for (int i=0, n=maxIndex + 1; i<n; i++) _thresholds[i] = 0.5;
}

FSMEntity::~FSMEntity()
{
#if DEBUG_FSM
  if (_threadId != 0) PostThreadMessage(_threadId, WM_QUIT, 0, 0);
  if (_tempFile.GetLength() > 0) QIFileFunctions::Unlink(_tempFile);
#endif  
}

void FSMEntity::SetState(State state, base::Context *context)
{
  // avoid direct change of state
}

const char *FSMEntity::GetStateName() const
{
  return CurState().GetName();
}

RString FSMEntity::GetDebugName() const
{
  return _type->GetName()._name;
}

int &FSMEntity::Var(int i)
{
  static int dummy = 0;
  return dummy;
}

int FSMEntity::Var(int i) const
{
  return 0;
}

#if _HELISIM_LARGE_OBJECTID
OIDInt &FSMEntity::VarId()
{
  static OIDInt dummy = 0;
  return dummy;
}

OIDInt FSMEntity::VarId() const
{
  return 0;
}
#endif // _HELISIM_LARGE_OBJECTID

Time &FSMEntity::VarTime(int i)
{
  static Time dummy(0);
  return dummy;
}

Time FSMEntity::VarTime(int i) const
{
  return Time(0);
}

void FSMEntity::SetTimeOut(float sec)
{
  _timeOut = Glob.time + sec;
}

float FSMEntity::GetTimeOut() const
{
  return _timeOut - Glob.time;
}

bool FSMEntity::TimedOut() const
{
  return Glob.time > _timeOut;
}

bool FSMEntity::IsExclusive(Context *context)
{
  return _isExclusive;
}

bool FSMEntity::Update(base::Context *context)
{
  if (_curState == FinalState) return true;
  if (_type->_states.Size()<=0) return true; // bad FSM - no state at all

  if (_initNeeded)
  {
    // initialization of init state
#if LOG_FSM
    LogF("FSM of %s: State %s reached", cc_cast(reinterpret_cast<Entity *>(context)->GetDebugName()), cc_cast(CurState().GetName()));
#endif
    CurState().Init(this, context, _thresholds);
    _initNeeded = false;
  }

  
  State newState = CurState().Check(this, context, _thresholds);
  if (newState == UnchangedStateEntity) return false;

  if (ProceedTo(context,newState))
    return true;

  // special case: if there is only one edge from this state and the edge is "true"

  // simple mechanism to prevent infinite loop
  State startLoop = _curState; // once we reach the starting state again, we are in a loop
  int toVisit = _type->_states.Size(); // we cannot visit more states than all states in the FSM
  // toVisit works when not started in a loop, but may allow several loops to run
  for(;;)
  {
    State newState = CurState().CheckAtomic(this, context, _thresholds);
    if (newState == UnchangedStateEntity)
      break;
    if (ProceedTo(context,newState))
      return true;
    if (newState==startLoop || --toVisit<0)
      break;
  }
  return false;
}

bool FSMEntity::ProceedTo(base::Context *context, State newState)
{
  // new state reached
  CurState().Exit(this,context);
  _curState = newState;
  if (_curState == FinalState)
  {
#if LOG_FSM || DEBUG_SEAGULL
#if DEBUG_SEAGULL
    if (((SeaGullAuto *)context)->ID()==debugID)
#endif 
    LogF("FSM of %s: Final state reached", cc_cast(reinterpret_cast<Entity *>(context)->GetDebugName()));
#endif
    return true;
  }
#if LOG_FSM
  LogF("FSM of %s: State %s reached in time %f", cc_cast(reinterpret_cast<Entity *>(context)->GetDebugName()), cc_cast(CurState().GetName()), Glob.time.toFloat());
#endif
#if DEBUG_SEAGULL
  if (((SeaGullAuto *)context)->ID()==debugID)
    LogF("FSM of %s: State %s reached in time %f", cc_cast(reinterpret_cast<Entity *>(context)->GetDebugName()), cc_cast(CurState().GetName()), Glob.time.toFloat());
#endif 
  CurState().Init(this,context, _thresholds);
  return false;
}

void FSMEntity::Refresh(base::Context *context)
{
  if (_curState != FinalState) CurState().Init(this,context, _thresholds);
  _initNeeded = false;
}

void FSMEntity::Enter(base::Context *context)
{
  // not implemented
}

void FSMEntity::Exit(base::Context *context)
{
  // not implemented
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

  ParamEntryVal cfg = Pars >> "CfgFSMs" >> name;

  QOFStream out;
  out.open(path);
  cfg.Save(out, 0, "\r\n");
  out.close();
  return path;
}

void FSMEntity::Debug()
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

  RString fsmPath = ExportFSM(_type->GetName()._name);
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
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
      }
    }
  }
}

#endif

LSError FSMEntity::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar))

  if (ar.IsSaving())
  {
    RString id;
    if (_curState != FinalState) id = CurState().GetId();
    CHECK(ar.Serialize("curState", id, 1))
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    RString id;
    CHECK(ar.Serialize("curState", id, 1))
    _curState = FinalState;
    if (id.GetLength() > 0) _curState = _type->FindStateIndex(id);
  }
  CHECK(::Serialize(ar, "timeOut", _timeOut, 1))
  CHECK(ar.Serialize("initNeeded", _initNeeded, 1, false))
  CHECK(ar.Serialize("isExclusive", _isExclusive, 1, false))
  CHECK(ar.SerializeArray("thresholds", _thresholds, 1))
  return LSOK;
}

FSMEntityTypeBank FSMEntityTypes;

DEFINE_SERIALIZE_TYPE_INFO(FSMEntity, FSM)
