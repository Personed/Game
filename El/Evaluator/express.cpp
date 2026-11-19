#include <El/elementpch.hpp>
#include <Es/Common/win.h>

#include "express.hpp"
#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <Es/Strings/bString.hpp>
#include <Es/Framework/appFrame.hpp>

#include <El/Common/randomGen.hpp>
#include <El/Common/perfLog.hpp>

#include <El/ParamArchive/paramArchive.hpp>

#ifndef CHECK
#define CHECK(command) {LSError err = command; if (err != 0) return err;}
#endif

// core name must not contain %s specification
// however, without localization we want it

static const EnumName EvalErrorNames[]=
{
  EnumName(EvalOK, "OK"),
  EnumName(EvalGen, "GEN"),
  EnumName(EvalExpo, "EXPO"),
  EnumName(EvalNum, "NUM"),
  EnumName(EvalVar, "VAR"), // %s should be added
  EnumName(EvalBadVar, "BAD_VAR"),
  EnumName(EvalDivZero, "DIV_ZERO"),
  EnumName(EvalTg90, "TG90"),
  EnumName(EvalOpenB, "OPENB"),
  EnumName(EvalCloseB, "CLOSEB"),
  EnumName(EvalOpenBrackets, "OPEN_BRACKETS"),
  EnumName(EvalCloseBrackets, "CLOSE_BRACKETS"),
  EnumName(EvalOpenBraces, "OPEN_BRACES"),
  EnumName(EvalCloseBraces, "CLOSE_BRACES"),
  EnumName(EvalEqu, "EQU"),
  EnumName(EvalSemicolon, "SEMICOLON"),
  EnumName(EvalQuote, "QUOTE"),
  EnumName(EvalSingleQuote, "SINGLE_QUOTE"),
  EnumName(EvalOper, "OPER"),
  EnumName(EvalLineLong, "LINE_LONG"),
  EnumName(EvalType, "TYPE"),
  EnumName(EvalNamespace, "NAMESPACE"),
  EnumName(EvalDim, "DIM"),
  EnumName(EvalUnexpectedCloseB, "UNEXPECTED_CLOSEB"),
  EnumName(EvalAssertFailed,"ASSERTATION_FAILED"),
  EnumName(EvalHaltCalled,"HALT_FUNCTION"),
  EnumName(EvalForeignError,"FOREIGN"), // %s should be added
  EnumName(EvalScopeNameExists,"SCOPE_NAME_DEFINED_TWICE"),
  EnumName(EvalScopeNotFound,"SCOPE_NOT_FOUND"),
  EnumName(EvalInvalidTryBlock,"INVALID_TRY_BLOCK"),
  EnumName(EvalUnhandledException,"UNHANDLED_EXCEPTION"),  // %s should be added
  EnumName(EvalStackOverflow,"STACK OVERFLOW"),
  EnumName(EvalHandled,"HANDLED"),
  EnumName(EvalAccess,"ACCESS"),

  EnumName()
};
template<>
const EnumName *GetEnumNames(EvalError dummy)
{
  return EvalErrorNames;
}

GameTypeType::GameTypeType()
{
  _createFunction = NULL;
}

// combined types
const GameType GameStringOrArray(GameString | GameArray);
const GameType GameCodeOrString(GameCode | GameString);

// fake types need to be known only in this cpp
#define TYPES_FAKE(XX, Category) \
  XX("IF",GameIf, CreateGameDataIf, "if", "If Type", "A helper type used in <f>if</f>..<f>then</f> constructs.", Category, "NativeObject", "Lcom/bistudio/JNIScripting/NativeObject;") \
  XX("WHILE",GameWhile, CreateGameDataWhile, "while", "While Type", "A helper type used in <f>while</f>..<f>do</f> constructs.", Category, "NativeObject", "Lcom/bistudio/JNIScripting/NativeObject;")\
  XX("FOR",GameFor,CreateGameFor,"for","for type",\
  "This type handles for cycles. Usage of this type: for \"_var\" from :expr: to :expr: [step &lt;expr&gt;] do {..code..};"\
  "Second usage: for [\":initPhase:\",\":condition:\",\":updatePhase:\"] do {...code...};",Category, "NativeObject", "Lcom/bistudio/JNIScripting/NativeObject;")\
  XX("SWITCH",GameSwitch, CreateGameDataSwitch, "switch", "Switch Type", "A helper type used in <f>switch</f> constructs.", Category, "NativeObject", "Lcom/bistudio/JNIScripting/NativeObject;")\
  XX("EXCEPTION",GameException, CreateGameDataException, "exception", "Exception Type", "A helper type used in <f>try</f>-<f>catch</f> constructs.", Category, "NativeObject", "Lcom/bistudio/JNIScripting/NativeObject;")\
  XX("WITH",GameWith, CreateGameDataWith, "with", "With Type", "A helper type used in <f>while</f>..<f>do</f> constructs.", Category, "NativeObject", "Lcom/bistudio/JNIScripting/NativeObject;")\

TYPES_FAKE(DECLARE_TYPE, "Default")

const GameType FakeTypes(GameIf | GameWhile | GameFor | GameSwitch | GameException | GameWith);

bool GameState::IsSwitchType(const GameType &type) const
{
  return type & GameSwitch;
}

bool GameState::IsFakeType(const GameType &type) const
{
  return type != GameAny && (type & FakeTypes);
}

#include <Es/Memory/normalNew.hpp>

class GameDataIf: public GameDataBool
{
public:
  GameDataIf(){}
  GameDataIf( GameBoolType value ):GameDataBool(value){}
  const GameType &GetType() const {return GameIf;}
  const char *GetTypeName() const {return "if";}
  GameData *Clone() const {return new GameDataIf(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

  USE_FAST_ALLOCATOR_EXPRESS
};

class GameDataWhile: public GameData
{
protected:
  Ref<GameData> _code;

public:
  GameDataWhile() {}
  GameDataWhile(GameData *value) : _code(value) {}
  const GameType &GetType() const {return GameWhile;}
  const char *GetTypeName() const {return "while";}
  GameStringType GetString() const {return _code ? _code->GetString() : RString();}
  RString GetText() const {return _code ? _code->GetText() : RString();}
  GameData *GetCode() const {return _code;}
  bool IsEqualTo(const GameData *data) const;
  GameData *Clone() const {return new GameDataWhile(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

#ifndef ACCESS_ONLY
  LSError Serialize(ParamArchive &ar);
#endif

  USE_FAST_ALLOCATOR_EXPRESS
};

class GameDataWith: public GameData
{
protected:
  Ref<GameData> _namespace;

public:
  GameDataWith() {}
  GameDataWith(GameData *value) : _namespace(value) {}
  const GameType &GetType() const {return GameWith;}
  const char *GetTypeName() const {return "with";}
  GameStringType GetString() const {return "namespace";}
  RString GetText() const {return "namespace";}
  GameData *GetNamespace() const {return _namespace;}
  bool IsEqualTo(const GameData *data) const;
  GameData *Clone() const {return new GameDataWith(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

#ifndef ACCESS_ONLY
  LSError Serialize(ParamArchive &ar);
#endif

  USE_FAST_ALLOCATOR_EXPRESS
};

class GameDataForClass : public GameData
{
public:
  RString _variableName;
  float _first;
  float _last;
  float _step;
  GameValue _init;
  GameValue _update;
  GameValue _cond;

  GameDataForClass (void):_step(1),_first(0),_last(0) {}
  const GameType &GetType() const {return GameFor;}
  RString GetText() const {return "ForType <invisible>";}
  bool IsEqualTo(const GameData *data) const {return false;}
  const char *GetTypeName() const {return "ForType";}
  GameData *Clone() const {return new GameDataForClass (*this);}
  GameValue Cycle(const GameState *gs, GameValuePar code);

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

  USE_FAST_ALLOCATOR_EXPRESS;
};

class GameDataException: public GameData
{
  GameVarSpace *_deflevel;     ///<pointer to defined level
public:
  GameValue _excepResult;     ///<Result of throw command
  bool _thrown;               ///<true, if there exists thrown exception
  GameValue _code;

  GameDataException() : _thrown(false),_deflevel(NULL) {}
  GameDataException(GameValuePar code, GameVarSpace *deflevel) : _code(code),_thrown(false), _deflevel(deflevel) {}
  GameDataException(const GameDataException &value) : _deflevel(value._deflevel), _thrown(value._thrown), _code(value._code) {}
  const GameType &GetType() const {return GameException;}
  const char *GetTypeName() const {return "GameExceptionHelpType";}
  GameData *Clone() const {return new GameDataException(*this);}
  GameVarSpace *GetTryLevel() const {return _deflevel;}
  bool IsThrown() const {return _thrown;}

  RString GetText() const {return "ExceptionType <invisible>";}
  bool IsEqualTo(const GameData *data) const {return false;}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

  USE_FAST_ALLOCATOR_EXPRESS
};

#include <Es/Memory/debugNew.hpp>

GameData *CreateGameDataScalar(ParamArchive *ar) {return new GameDataScalar();}
GameData *CreateGameDataBool(ParamArchive *ar) {return new GameDataBool();}
GameData *CreateGameDataArray(ParamArchive *ar) {return new GameDataArray();}
GameData *CreateGameDataString(ParamArchive *ar) {return new GameDataString();}
GameData *CreateGameDataNothing(ParamArchive *ar) {return new GameDataNothing();}
GameData *CreateGameDataAny(ParamArchive *ar){Fail("Instance of any should never be created");return NULL;}
GameData *CreateGameDataNamespace(ParamArchive *ar)
{
  if (!ar) return NULL; // dynamic namespaces not yet supported

  // static namespace is identified by its name
  RString name;
  if (ar->Serialize("name", name, 1) != 0) return NULL;
  if (name.GetLength() == 0) return NULL; // dynamic namespaces not yet supported

  GameState *state = reinterpret_cast<GameState *>(ar->GetParams());
  DoAssert(state);
  return state->FindStaticNamespace(name);
}

GameData *CreateGameDataIf(ParamArchive *ar) {return new GameDataIf();}
GameData *CreateGameDataWhile(ParamArchive *ar) {return new GameDataWhile();}
GameData *CreateGameDataWith(ParamArchive *ar) {return new GameDataWith();}
GameData *CreateGameFor(ParamArchive *ar) {return new GameDataForClass;}
GameData *CreateGameDataSwitch(ParamArchive *ar) {return new GameDataSwitch;}
GameData *CreateGameDataException(ParamArchive *ar) {return new GameDataException;}
#if USE_PRECOMPILATION
GameData *CreateGameDataCode(ParamArchive *ar) {return new GameDataCode();}
#endif

TYPES_DEFAULT(DEFINE_TYPE, "Default")
TYPES_FAKE(DEFINE_TYPE, "Default")
#if USE_PRECOMPILATION
TYPES_CODE(DEFINE_TYPE, "Default")
#endif

DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataNil)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataScalar)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataBool)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataNothing)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataString)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataArray)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataNamespace)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataIf)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataWhile)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataWith)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataForClass)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataSwitch)
DEFINE_FAST_ALLOCATOR_EXPRESS(GameDataException)

const GameType &GameDataScalar::GetType() const {return GameScalar;}
const GameType &GameDataNothing::GetType() const {return GameNothing;}
const GameType &GameDataString::GetType() const {return GameString;}
const GameType &GameDataBool::GetType() const {return GameBool;}
const GameType &GameDataArray::GetType() const {return GameArray;}
const GameType &GameDataNamespace::GetType() const {return GameNamespace;}
#if USE_PRECOMPILATION
const GameType &GameDataCode::GetType() const {return GameCode;}
#endif

const GameType &GameDataSwitch::GetType() const {return GameSwitch;}

static GameValue GameValueNil(new GameDataNil(GameVoid));

// helpers only for template instantiation
extern GameTypeType GTypeGameStringOrArray;
extern GameTypeType GTypeGameCodeOrString;
template<>
class ConvertTypes<&GTypeGameStringOrArray> : public ConvertTypes<&GTypeGameAny> {};
template<>
class ConvertTypes<&GTypeGameCodeOrString> : public ConvertTypes<&GTypeGameAny> {};

template<> CachedJavaClass ConvertTypes<&GTypeGameNamespace>::_cache("com/bistudio/JNIScripting/RVEngine$GameNamespace");
template<> CachedJavaClass ConvertTypes<&GTypeGameCode>::_cache("com/bistudio/JNIScripting/NativeObject");
template<> CachedJavaClass ConvertTypes<&GTypeGameIf>::_cache("com/bistudio/JNIScripting/NativeObject");
template<> CachedJavaClass ConvertTypes<&GTypeGameWhile>::_cache("com/bistudio/JNIScripting/NativeObject");
template<> CachedJavaClass ConvertTypes<&GTypeGameWith>::_cache("com/bistudio/JNIScripting/NativeObject");
template<> CachedJavaClass ConvertTypes<&GTypeGameFor>::_cache("com/bistudio/JNIScripting/NativeObject");
template<> CachedJavaClass ConvertTypes<&GTypeGameSwitch>::_cache("com/bistudio/JNIScripting/NativeObject");
template<> CachedJavaClass ConvertTypes<&GTypeGameException>::_cache("com/bistudio/JNIScripting/NativeObject");

jweak ConvertTypes<&GTypeGameArray>::_class = NULL;
jmethodID ConvertTypes<&GTypeGameArray>::_sizeID = NULL;
jmethodID ConvertTypes<&GTypeGameArray>::_getID = NULL;

jobject GameDataSwitch::ToJObject(JNIEnv *env, GameValuePar from) const
{
  // unsupported type
  return GameValueToNativeObject(env, from);
}

#ifdef _WIN32
#define M_PI 3.1415926536
#endif


CompoundGameType CompoundGameType::operator |(const CompoundGameType &with) const
{
  CompoundGameType result;
  result.Realloc(Size() + with.Size()); // highest bound of size (will match mostly)
  // keep the result sorted
  int i = 0, j = 0;
  while (i < Size() && j < with.Size())
  {
    if (Get(i) == with.Get(j))
    {
      result.Add(Get(i)); i++; j++;
    }
    else if (Get(i) < with.Get(j))
    {
      result.Add(Get(i)); i++;
    }
    else
    {
      result.Add(with.Get(j)); j++;
    }
  }
  // copy the rest (at most one cycle do something)
  while (i < Size()) result.Add(Get(i++));
  while (j < with.Size()) result.Add(with.Get(j++));
  result.Compact();
  return result;
}

CompoundGameType CompoundGameType::operator &(const CompoundGameType &with) const
{
  CompoundGameType result;
  result.Realloc(std::max(Size(), with.Size())); // highest bound of size
  // keep the result sorted
  int i = 0, j = 0;
  while (i < Size() && j < with.Size())
  {
    if (Get(i) == with.Get(j))
    {
      result.Add(Get(i)); i++; j++;
    }
    else if (Get(i) < with.Get(j))
    {
      i++;
    }
    else
    {
      j++;
    }
  }
  result.Compact();
  return result;
}

bool CompoundGameType::operator ==(const CompoundGameType &with) const
{
  int n = Size();
  // check the size
  if (with.Size() != n) return false;
  // check all fields (sorted, so need correspond 1:1)
  for (int i=0; i<n; i++)
  {
    if (Get(i) != with.Get(i)) return false;
  }
  return true;
}

void CompoundGameType::operator |=(const CompoundGameType &with)
{
  *this = *this | with;
}

void CompoundGameType::operator &=(const CompoundGameType &with)
{
  *this = *this & with;
}

RString GameType::GetJavaType() const
{
  if (_multipleTypes) return "Object";
  return _singleType->GetJavaType();
}
RString GameType::GetSignature() const
{
  if (_multipleTypes) return "Ljava/lang/Object;";
  return _singleType->GetSignature();
}

GameType GameType::operator |(const GameType &with) const
{
  // GameVoid / GameAny need to be handled in a special way
  if (*this == GameAny) return GameAny;
  if (with == GameAny) return GameAny;
  
  GameType res = *this;
  res |= with;
  return res;
}
GameType GameType::operator &(const GameType &with) const
{
  // GameVoid / GameAny need to be handled in a special way
  if (*this == GameAny) return with;
  if (with == GameAny) return *this;
  
  GameType res = *this;
  res &= with;
  return res;
}
bool GameType::operator ==(const GameType &with) const
{
  if (_multipleTypes.IsNull())
  {
    // caution: both NULLs are possible - this means impossible type
    if (with._multipleTypes.IsNull())
    {
      return _singleType==with._singleType;
    }
    return false;
  }
  if (with._multipleTypes.IsNull())
  {
    // we already know this is not multiple types, equality not possible
    return false;
  }
  return (*_multipleTypes)==(*with._multipleTypes);
}

void GameType::operator |=(const GameType &with)
{
  // GameVoid / GameAny need to be handled in a special way
  if (*this == GameAny) return;
  if (with == GameAny) {*this = with;return;}
  
  // handle empty type on any side
  if (_singleType==NULL && _multipleTypes.IsNull()) {*this=with;return;}
  if (with._singleType==NULL && with._multipleTypes.IsNull()) return;

  // at this point something is non-NULL on both sides
  Assert(_multipleTypes || _singleType);
  Assert(with._multipleTypes || with._singleType);
  // special case - or-ed with itself
  if (_singleType)
  {
    if (with._singleType==_singleType)
    {
      return;
    }
    else if (with._singleType)
    {
      _multipleTypes = new CompoundGameType(_singleType,with._singleType);
      _singleType = NULL;
      Canonicalize(); // one side could be an empty list
      return;
    }
    else
    {
      // convert single type to multiple types
      _multipleTypes = new CompoundGameType(_singleType);
      _singleType = NULL;
      // merge both multiple types
      *_multipleTypes |= *with._multipleTypes;
      Canonicalize(); // one side could be an empty list
    }
  }
  else
  {
    // left side is multiple types
    if (with._singleType)
    {
      CompoundGameType temp(with._singleType);
      *_multipleTypes |= temp;
      Canonicalize(); // one side could be an empty list
    }
    else
    {
      *_multipleTypes |= *with._multipleTypes;
      Canonicalize(); // one side could be an empty list
    }
  }
    
}

void GameType::operator &=(const GameType &with)
{
  // GameVoid / GameAny need to be handled in a special way
  if (*this == GameAny) return;
  if (with == GameAny) {*this = with;return;}
  
  // no type on input means no type on output
  if (_singleType==NULL && _multipleTypes.IsNull()) return;
  if (with._singleType==NULL && with._multipleTypes.IsNull()) {*this = with;return;}
  
  // at this point something is non-NULL on both sides
  Assert(_multipleTypes || _singleType);
  Assert(with._multipleTypes || with._singleType);
  // special case - or-ed with itself
  if (_singleType)
  {
    if (with._singleType==_singleType)
    {
      return;
    }
    else if (with._singleType)
    {
      // not equal - result is empty (no type, "impossible")
      _multipleTypes = NULL;
      _singleType = NULL;
      return;
    }
    else
    {
      if (with._multipleTypes->FindKey(_singleType)>=0)
      {
        return;
      }
      else
      {
        _singleType = NULL;
        return;
      }
    }
  }
  else
  {
    // left side is multiple types
    if (with._singleType)
    {
      if (_multipleTypes->FindKey(with._singleType)>=0)
      {
        _singleType = with._singleType;
        _multipleTypes = NULL;
        return;
      }
      else
      {
        _singleType = NULL;
        _multipleTypes = NULL;
        return;
      }
    }
    else
    {
      *_multipleTypes &= *with._multipleTypes;
      Canonicalize();
    }
  }
    
}


/*!
\patch 1.41 Date 1/2/2002 by Jirka
- Improved: Expression evaluation optimized (faster script execution).
*/

void GameState::SetError(EvalError error, ...) const
{
  if (error == EvalOK || error == EvalHandled)
  {
    // reset the error
    _e->_error = error;
    _e->_errorText = RString();
    return;
  }
  if (_e->_error != EvalOK)
  {
    // save only the first error
    return;
  }
  _e->_error = error;
#ifndef ACCESS_ONLY
  RString errorName = FindEnumName(error);
  RString errorText = _defaultLocalizeFunctions->LocalizeString(RString("STR_EVAL_") + errorName);

  BString<MAX_EXPR_LEN> formated;
  // pass any addional string that were specified

  va_list va;
  va_start(va,error); 
  vsprintf(formated, (const char *)errorText, va);
  va_end(va);

  _e->_errorText = formated.cstr();
#if USE_PRECOMPILATION
  // error at position pos
  if (_context)
  {
    _e->_errorPos = _context->_lastInstruction;
  }
#else
  // error at position _e->_pos
  _e->_errorCarretPos = _e->_pos - _e->_pos0;
#endif
#endif // ACCESS_ONLY
}

void GameState::TypeError(const GameType &exp, const GameType &was, const char *name) const
{
  RString expName = GetTypeName(exp);
  RString wasName = GetTypeName(was);
  SetError(EvalType, (const char *)wasName, (const char *)expName);
  if (name)
  {
    // append name before error message
    RString withName = name + RString(": ") + _e->_errorText;
    _e->_errorText = withName;
  }
}

void GameState::TypeErrorOperator(const char *name, const GameType &left, const GameType &right) const
{
  // check which types are accepted as operands
  GameType leftE;
  GameType rightE;
  const GameOperators &operators = _operators[name];
  if (!_operators.IsNull(operators))
  {
    for (int i=0; i<operators.Size(); i++)
    {
      leftE |= operators[i]._operator->GetArg1Type();
      rightE |= operators[i]._operator->GetArg2Type();
    }
  }
  if ((leftE&left)!=left)
  {
    TypeError(leftE, left, name);
  }
  else if((rightE&right)!=right)
  {
    TypeError(rightE, right, name);
  }
  else
  {
    SetError(EvalGen);
  }
}

void GameState::TypeErrorFunction(const char *name, const GameType &right) const
{
  // check which types are accepted as operands
  GameType rightE;
  const GameFunctions &functions = _functions[name];
  if (!_functions.IsNull(functions))
  {
    for (int i=0; i<functions.Size(); i++)
      rightE |= functions[i]._operator->GetArgType();
  }
  if((rightE&right)!=right)
  {
    TypeError(rightE, right, name);
  }
  else
  {
    SetError(EvalGen);
  }
}

void GameState::AccessError(const char *name) const
{
  SetError(EvalAccess, name);
}

void GameState::ShowError() const
{
  if (_e->_error == EvalOK || _e->_error == EvalHandled) return;
#if _ENABLE_REPORT
#if USE_PRECOMPILATION
  // show message only when running in editor
  SourceDocPos &pos = _e->_errorPos;
  const char *start = pos._content;
  const char *end = start + pos._content.GetLength();
  const char *errPos = start + pos._pos;
  // show only local context
  bool notStart = false;
  bool notEnd = false;
  if (errPos > start + 40)
  {
    notStart = true;
    start = errPos - 40;
  }
  if (end > errPos + 40)
  {
    notEnd = true;
    end = errPos + 40;
  }

  RString expr(start, end - start);
  RString before(start, errPos - start);
  RString after(errPos, end - errPos);
  RString buf;
  if (notStart) buf = RString("...");
  buf = buf + before + RString("|#|") + after;
  if (notEnd) buf = buf + RString("...");

  if (_showScriptErrors)
  {
  const int nMsg = 20;
  static InitVal<int,-1> handlesMsg[nMsg];
  GlobalShowMessage(10000, Format("'%s'\nError %s", (const char *)buf, (const char *)_e->_errorText), handlesMsg, nMsg);
  }

#ifdef _SCRIPT_DEBUGGER
  if (_debugger) 
  {
    _debugger->OnError(const_cast<GameState &>(*this), _e->_errorText);
  }
#endif

  RptF("Error in expression <%s>", (const char *)expr);
  RptF("  Error position: <%s>", (const char *)after);
  RptF("  Error %s", (const char *)_e->_errorText);

  if (pos._sourceFile.GetLength() != 0)
  {
    // filename can be very long
    if (_showScriptErrors)
    {
    const int nPos = 5;
    static InitVal<int,-1> handlesPos[nPos];
    GlobalShowMessage(10000, Format("File %s, line %d", cc_cast(pos._sourceFile), pos._sourceLine), handlesPos, nPos);
    }
    RptF("File %s, line %d", cc_cast(pos._sourceFile), pos._sourceLine);
  }

#else // USE_PRECOMPILATION

  const char *errPos = _e->_pos0 + _e->_errorCarretPos;

  // show message only when running in editor
  const char *start = _e->_subexpBeg >= 0 ? _e->_pos0 + _e->_subexpBeg : _e->_pos0;
  const char *end = _e->_pos0 + strlen(_e->_pos0);
  if (end > errPos + 256) end = errPos + 256;
  RString expr(start, end - start);
  RString before(start, errPos - start);
  RString after(errPos, end - errPos);
  RString buf = before + RString("|#|") + after;

  const int nMsg = 25;
  static InitVal<int,-1> handlesMsg[nMsg];
  GlobalShowMessage(10000, Format("'%s': Error %s", (const char *)buf, (const char *)_e->_errorText), handlesMsg, nMsg);
#ifdef _SCRIPT_DEBUGGER
  if (_debugger) 
  {
    EvalError save=_e->_error;
    RString savetext=_e->_errorText;
    if (_debugger->OnError(const_cast<GameState &>(*this),_e->_errorText)==false) _e->_pos = strchr(_e->_pos0, 0);
    _e->_error=save;
    _e->_errorText=savetext;
  }
#endif

  RptF("Error in expression <%s>",(const char *)expr);
  RptF("  Error position: <%s>",(const char *)after);
  RptF("  Error %s",(const char *)_e->_errorText);

#endif // USE_PRECOMPILATION
#endif
}

#if USE_PRECOMPILATION
void GameState::SetError(const SourceDocPos &pos, EvalError error, ...) const
{
  if (error == EvalOK || error == EvalHandled)
  {
    // reset the error
    _e->_error = error;
    _e->_errorText = RString();
    return;
  }
  if (_e->_error != EvalOK)
  {
    // save only the first error
    return;
  }
  // error at position pos
  _e->_error = error;
#ifndef ACCESS_ONLY
  RString errorName = FindEnumName(error);
  RString errorText = _defaultLocalizeFunctions->LocalizeString(RString("STR_EVAL_") + errorName);

  BString<MAX_EXPR_LEN> formated;
  // pass any addional string that were specified

  va_list va;
  va_start(va,error); 
  vsprintf(formated, (const char *)errorText, va);
  va_end(va);

  _e->_errorText = formated.cstr();
  _e->_errorPos = pos;
#endif
}

void GameState::TypeError(const SourceDocPos &pos, const GameType &exp, const GameType &was, const char *name) const
{
  RString expName = GetTypeName(exp);
  RString wasName = GetTypeName(was);
  SetError(pos, EvalType, (const char *)wasName, (const char *)expName);
  if (name)
  {
    // append name before error message
    RString withName = name + RString(": ") + _e->_errorText;
    _e->_errorText = withName;
  }
}

void GameState::TypeErrorOperator(const SourceDocPos &pos, const char *name, const GameType &left, const GameType &right) const
{
  // check which types are accepted as operands
  GameType leftE;
  GameType rightE;
  const GameOperators &operators = _operators[name];
  if (!_operators.IsNull(operators))
  {
    for (int i=0; i<operators.Size(); i++)
    {
      leftE |= operators[i]._operator->GetArg1Type();
      rightE |= operators[i]._operator->GetArg2Type();
    }
  }
  if ((leftE&left)!=left)
  {
    TypeError(pos, leftE, left, name);
  }
  else if((rightE&right)!=right)
  {
    TypeError(pos, rightE, right, name);
  }
  else
  {
    SetError(pos, EvalGen);
  }
}

void GameState::TypeErrorFunction(const SourceDocPos &pos, const char *name, const GameType &right) const
{
  // check which types are accepted as operands
  GameType rightE;
  const GameFunctions &functions = _functions[name];
  if (!_functions.IsNull(functions))
  {
    for (int i=0; i<functions.Size(); i++)
      rightE |= functions[i]._operator->GetArgType();
  }
  if((rightE&right)!=right)
  {
    TypeError(pos, rightE, right, name);
  }
  else
  {
    SetError(pos, EvalGen);
  }
}
#endif

// Scripting functions

static GameValue Soucet( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return (float)oper1+(float)oper2;
}
static GameValue Rozdil( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return (float)oper1-(float)oper2;
}
static GameValue Soucin( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return (float)oper1*(float)oper2;
}
static GameValue Podil(const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  if( (float)oper2==0 )
  {
    state->SetError(EvalDivZero);
    return 0.0f;
  }
  return (float)oper1/(float)oper2;
}
static GameValue Zbytek( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  if( (float)oper2==0 )
  {
    state->SetError(EvalDivZero);
    return 0.0f;
  }
  return (float)fmod((float)oper1,(float)oper2);
}
static inline GameValue Zbytek2( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return Zbytek(state, oper1, oper2);
}
static GameValue ValMin( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return floatMin((float)oper1,(float)oper2);
}
static GameValue ValMax( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return floatMax((float)oper1,(float)oper2);
}
static GameValue Na( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return (float)pow((float)oper1, (float)oper2);
}
static float Na(float oper1, float oper2) {return pow(oper1, oper2);}

static GameValue CmpE( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return (float)oper1==(float)oper2;
}
static GameValue CmpL( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return (float)oper1<(float)oper2;
}
static GameValue CmpG( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return (float)oper1>(float)oper2;
}
static GameValue CmpLE( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return (float)oper1<=(float)oper2;
}
static GameValue CmpGE( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return (float)oper1>=(float)oper2;
}
static GameValue CmpNE( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return (float)oper1!=(float)oper2;
}

static GameValue StrCmpNE( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return strcmpi(oper1.operator GameStringType(),oper2.operator GameStringType())!=0;
}

static GameValue StrCmpE( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return strcmpi(oper1.operator GameStringType(),oper2.operator GameStringType())==0;
}

static GameValue VoidCmpNE( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return !oper1.IsEqualTo(oper2);
}

static GameValue VoidCmpE( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return oper1.IsEqualTo(oper2);
}

static GameValue StrAdd( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  //GameValue o1=oper1;
  //RStringT s1=(const char *)(o1);
  return oper1.operator GameStringType()+oper2.operator GameStringType();
}

#if !defined _WIN32
#include <wctype.h>

static int CharUpperBuffW(wchar_t *text, int len)
{
  for (int i=0; i<len; i++) 
  {
    *text = towupper(*text);
    text++;
  }
  return len; //There is no return value reserved to indicate an error (from: man towupper)
}

static int CharLowerBuffW(wchar_t *text, int len)
{
  for (int i=0; i<len; i++) 
  {
    *text = towlower(*text);
    text++;
  }
  return len; //There is no return value reserved to indicate an error (from: man towlower)
}
#endif

static GameValue StrToUpper(const GameState *state, GameValuePar oper1)
{
  RString str = oper1;
  if (str.GetLength() == 0) return str;

  // convert to Unicode
  int wLen = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
  AUTO_STATIC_ARRAY(wchar_t, wBuffer, 1024);
  wBuffer.Resize(wLen);
  MultiByteToWideChar(CP_UTF8, 0, str, -1, wBuffer.Data(), wLen);

  // convert to upper case
  CharUpperBuffW(wBuffer.Data(), wLen);

  // back UTF-8
  int len = WideCharToMultiByte(CP_UTF8, 0, wBuffer.Data(), -1, NULL, 0, NULL, NULL);
  AUTO_STATIC_ARRAY(char, buffer, 1024);
  buffer.Resize(len);
  WideCharToMultiByte(CP_UTF8, 0, wBuffer.Data(), -1, buffer.Data(), len, NULL, NULL);
  buffer[len - 1] = 0; // make sure result is always null terminated

  RString result = buffer.Data();
  return result;
}

static GameValue StrToLower(const GameState *state, GameValuePar oper1)
{
  RString str = oper1;
  if (str.GetLength() == 0) return str;

  // convert to Unicode
  int wLen = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
  AUTO_STATIC_ARRAY(wchar_t, wBuffer, 1024);
  wBuffer.Resize(wLen);
  MultiByteToWideChar(CP_UTF8, 0, str, -1, wBuffer.Data(), wLen);

  // convert to lower case
  CharLowerBuffW(wBuffer.Data(), wLen);

  // back UTF-8
  int len = WideCharToMultiByte(CP_UTF8, 0, wBuffer.Data(), -1, NULL, 0, NULL, NULL);
  AUTO_STATIC_ARRAY(char, buffer, 1024);
  buffer.Resize(len);
  WideCharToMultiByte(CP_UTF8, 0, wBuffer.Data(), -1, buffer.Data(), len, NULL, NULL);
  buffer[len - 1] = 0; // make sure result is always null terminated

  RString result = buffer.Data();
  return result;
}

static GameValue StrToArray(const GameState *state, GameValuePar oper1)
{
  GameValue value(new GameDataArray());
  GameArrayType &array = value;

  RString str = oper1;
  if (str.GetLength() > 0)
  {
    // convert to Unicode
    int len = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
    AUTO_STATIC_ARRAY(wchar_t, wBuffer, 1024);
    wBuffer.Resize(len);
    MultiByteToWideChar(CP_UTF8, 0, str, -1, wBuffer.Data(), len);

    // copy characters to the output array
    array.Realloc(len - 1);
    array.Resize(len - 1);
    for (int i=0; i<len-1; i++) array[i] = (float)wBuffer[i];
  }
  return value;
}

static GameValue StrToString(const GameState *state, GameValuePar oper1)
{
  const GameArrayType &array = oper1;
  int wLen = array.Size();

  // read the Unicode 
  AUTO_STATIC_ARRAY(wchar_t, wBuffer, 1024);
  wBuffer.Resize(wLen+1);
  for (int i=0; i<wLen; i++) wBuffer[i] = toInt(float(array[i]));
  wBuffer[wLen] = 0;

  // convert to UTF-8
  int len = WideCharToMultiByte(CP_UTF8, 0, wBuffer.Data(), -1, NULL, 0, NULL, NULL);
  AUTO_STATIC_ARRAY(char, buffer, 1024);
  buffer.Resize(len);
  WideCharToMultiByte(CP_UTF8, 0, wBuffer.Data(), -1, buffer.Data(), len, NULL, NULL);
  buffer[len - 1] = 0; // make sure result is always null terminated

  RString result = buffer.Data();
  return result;
}

static GameValue ListAdd( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  // make return object to avoid unneccessary copy
  GameValue retValue(new GameDataArray);
  GameArrayType &ret=retValue;

  const GameArrayType &add1=oper1;
  const GameArrayType &add2=oper2;
  ret.Realloc(add1.Size()+add2.Size());

  for( int i=0; i<add1.Size(); i++ )
  {
    ret.Add(add1[i]);
  }
  for( int i=0; i<add2.Size(); i++ )
  {
    ret.Add(add2[i]);
  }
  return retValue;
}

static GameValue ListSub( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  // make return object to avoid unneccessary copy
  GameValue retValue(new GameDataArray);
  GameArrayType &ret=retValue;

  const GameArrayType &op1=oper1;
  const GameArrayType &op2=oper2;
  ret.Realloc(op1.Size());

  for( int i=0; i<op1.Size(); i++ )
  {
    // check if it is member of op2
    bool isInOp2 = false;
    for (int j=0; j<op2.Size(); j++)
    {
      if (op1[i].IsEqualTo(op2[j]))
      {
        isInOp2 = true;
        break;
      }
    }
    if (isInOp2) continue;
    // if not, add it to returned list
    ret.Add(op1[i]);
  }
  return retValue;
}


static GameValue BoolAnd( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return bool(oper1) && bool(oper2);
}
static inline GameValue BoolAnd2( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return BoolAnd(state, oper1, oper2);
}
static GameValue BoolOr( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return bool(oper1) || bool(oper2);
}
static inline GameValue BoolOr2( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  return BoolOr(state, oper1, oper2);
}


static GameValue EvalBoolCode( const GameState *state, GameValuePar oper2 )
{
#if USE_PRECOMPILATION
  GameDataCode *code = static_cast<GameDataCode *>(oper2.GetData());
  VMContext *context = state->GetVMContext();
  // 2 operands are on the stack now (OR, AND arguments)
  CallStackItem *level = new CallStackItemDataBool(context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state, "Boolean expression", code, context->IsSerializationEnabled());
  context->AddLevel(level);
  return NOTHING;
#else
  // evaluate non compiled string
  GameVarSpace local(state->GetContext(), false);
  state->BeginContext(&local);
  GameValue ret = state->EvaluateMultiple((RString)oper2, GameState::EvalContext::_default, state->GetGlobalVariables());
  state->EndContext();
  if (ret.GetType() != GameBool)
  {
    state->TypeError(GameBool, ret.GetType());
    return false;
  }
  return ret;
#endif
}

static GameValue BoolAndCode( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  if (!bool(oper1)) return false;
  return EvalBoolCode(state, oper2);
}
static GameValue BoolOrCode( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  if (bool(oper1)) return true;
  return EvalBoolCode(state, oper2);
}

static GameValue Plus( const GameState *state, GameValuePar oper1 )
{
  // make identical copy of argument
  return GameValue(oper1.GetData()->Clone());
}
static GameValue Minus( const GameState *state, GameValuePar oper1 )
{
  return -(float)oper1;
}
static GameValue Stupne( const GameState *state, GameValuePar oper1 )
{
  return (float)(2*M_PI*(float)oper1/360);
}
static float Stupne(float oper) {return (M_PI / 180) * oper;}

static GameValue Radian( const GameState *state, GameValuePar oper1 )
{
  return (float)((float)oper1*360/(2*M_PI));
}

static GameValue Random( const GameState *state, GameValuePar oper1 )
{
  float value=GRandGen.RandomValue();
  return (float)(value*(float)oper1);
}

static GameValue Sinus( const GameState *state, GameValuePar oper1 )
{
  return (float)sin(Stupne(state, oper1));
}
static float Sinus(float oper) {return sin(Stupne(oper));}

static GameValue Cosinus( const GameState *state, GameValuePar oper1 )
{
  return (float)cos(Stupne(state, (float)oper1));
}
static GameValue Tangens( const GameState *state, GameValuePar oper1 )
{
  return (float)tan(Stupne(state, (float)oper1));
}
static inline GameValue Tangens2( const GameState *state, GameValuePar oper1 )
{
  return Tangens(state, oper1);
}
static GameValue ASinus( const GameState *state, GameValuePar oper1 )
{
  return Radian(state, (float)asin((float)oper1));
}
static GameValue ACosinus( const GameState *state, GameValuePar oper1 )
{
  return Radian(state, (float)acos((float)oper1));
}
static GameValue ATangens( const GameState *state, GameValuePar oper1 )
{
  return Radian(state, (float)atan((float)oper1));
}
static inline GameValue ATangens2( const GameState *state, GameValuePar oper1 )
{
  return ATangens(state, oper1);
}
static GameValue Atan2( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  /*
  if( (float)oper1==0 && (float)oper2==0 )
  {
  state->SetError(EvalDivZero);
  return 0.0f;
  }
  */
  return Radian(state, (float)atan2((float)oper1, (float)oper2) );
}
static GameValue FLog( const GameState *state, GameValuePar oper1 )
{
  return (float)log10((float)oper1);
}
static GameValue FLn( const GameState *state, GameValuePar oper1 )
{
  return (float)log((float)oper1);
}
static GameValue Exp( const GameState *state, GameValuePar oper1 )
{
  return (float)exp((float)oper1);
}
static GameValue Odmocnina( const GameState *state, GameValuePar oper1 )
{
  return (float)sqrt((float)oper1);
}

static GameValue Abs( const GameState *state, GameValuePar oper1 )
{
  return (float)fabs((float)oper1);
}

static GameValue Floor( const GameState *state, GameValuePar oper1 )
{
  return (float)floor((float)oper1);
}

static GameValue Ceil( const GameState *state, GameValuePar oper1 )
{
  return (float)ceil((float)oper1);
}

static GameValue Round( const GameState *state, GameValuePar oper1 )
{
  return (float)floor((float)oper1+0.5f);
}

static GameValue Finite( const GameState *state, GameValuePar oper1 )
{
  return _finite(oper1)!=0;
}


static GameValue DebuggerEcho( const GameState *state, GameValuePar oper1 )
{
#ifdef _SCRIPT_DEBUGGER
  state->Echo(oper1.operator GameStringType());
#else
  state->OnEcho(oper1.operator GameStringType());
#endif
  return GameValue();
}

static GameValue AsString( const GameState *state, GameValuePar oper1 )
{
  return GameStringType(oper1.GetText());
}


static GameValue DebuggerAssert( const GameState *state, GameValuePar oper1 )
{
#ifdef _SCRIPT_DEBUGGER
  if (!bool(oper1)) state->Halt();
#else
  if (!bool(oper1)) state->SetError(EvalAssertFailed);
#endif
  return oper1;
}


static GameValue BoolNot( const GameState *state, GameValuePar oper1 )
{
  return !bool(oper1);
}
static inline GameValue BoolNot2( const GameState *state, GameValuePar oper1 )
{
  return BoolNot(state, oper1);
}

static GameValue VoidNil(const GameState *state)
{
  return GameValue(new GameDataNil(GameVoid));
}

static GameValue BoolTrue(const GameState *state)
{
  return true;
}
static GameValue BoolFalse(const GameState *state)
{
  return false;
}

static GameValue BreakScriptIntoDebugger(const GameState *state)
{
#ifdef _SCRIPT_DEBUGGER
  state->Halt();
#else
  state->SetError(EvalHaltCalled);
#endif
  return false;
}

static GameValue ScalarPi(const GameState *state)
{
  return float(M_PI);
}
static float ScalarPi() {return M_PI;}

static GameValue DisableSerialization(const GameState *state)
{
#if USE_PRECOMPILATION
  VMContext *context = state->GetVMContext();
  context->DisableSerialization();
#endif
  return NOTHING;
}

static GameValue ListCount( const GameState *state, GameValuePar oper1 )
{
  const GameArrayType &array=oper1;
  return (float)array.Size();
}

static GameValue ListSelect( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  const GameArrayType &array=oper1;
  float index = oper2;
  int sel = toInt(index);
  if( sel<0 || sel>=array.Size() )
  {
    if( sel!=array.Size() )
    {
      state->SetError(EvalDivZero);
    }
    return GameValue();
  }
  return array[sel];
}

/*!
\patch 1.44 Date 2/11/2002 by Ondra
- New: New scripting function: array resize newSize.

\patch 5164 Date 8/17/2007 by Ondra
- Fixed: Possible crash caused by malformed resize scripting command.
*/

static GameValue ListResize( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  if (oper1.GetReadOnly())
  {
    state->SetError(EvalBadVar);
    return NOTHING;
  }
  const GameArrayType &array=oper1;
  float index = oper2;
  int sel = toInt(index);

  if (sel<0)
  {
    state->SetError(EvalDivZero);
    return NOTHING;
  }

  GameArrayType &arrayNC = const_cast<GameArrayType &>(array);
  arrayNC.Resize(sel);
  return NOTHING;
}

static bool CheckArrayInValue(const GameArrayType &array1, const GameValue &value)
{
  if (value.GetType()!=GameArray) return false;
  const GameArrayType &array2 = value;
  if (&array1==&array2) return true;
  for (int i=0; i<array2.Size(); i++)
  {
    if (CheckArrayInValue(array1,array2[i])) return true;
  }
  return false;
}

/*!
\patch 1.44 Date 2/11/2002 by Ondra
- New: New scripting function: array set [index,value].
\patch_internal 1.80 Date 8/6/2002 by Ondra
- Fixed: Crash when array set [-1,value] was used.
*/

static GameValue ListSet( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  const GameArrayType &array=oper1;
  const GameArrayType &arg=oper2;
  if (arg.Size()!=2)
  {
    state->SetError(EvalDim,array.Size(),2);
    return NOTHING;
  }
  if (arg[0].GetType()!=GameScalar)
  {
    state->TypeError(GameScalar,arg[0].GetType());
    return NOTHING;
  }

  float index = arg[0];
  const GameValue &value = arg[1];
  int sel = toInt(index);
  if (sel<0)
  {
    state->SetError(EvalDivZero);
    return NOTHING;
  }

  if (oper1.GetReadOnly())
  {
    state->SetError(EvalBadVar);
    return NOTHING;
  }
  GameArrayType &arrayNC = const_cast<GameArrayType &>(array);
  // resize array as necessary
  arrayNC.Access(sel);
  // if value contains array, recursive array would be created
  // as this would lead to crashes, it need to be prevented
  if (CheckArrayInValue(array,value))
  {
    Fail("Recursive array");
    state->SetError(EvalDim);
    return NOTHING;
  }

  // set given value
  arrayNC[sel] = value;

  return NOTHING;
}

/*!
\patch 1.82 Date 8/9/2002 by Ondra
- New: Scripting: Added Functions:
call <string>,
if <bool> then <string> else <string>,
while <string> do <string>.
*/

static GameValue StringCall( const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
#if USE_PRECOMPILATION
  GameDataCode *value = static_cast<GameDataCode *>(oper2.GetData());
  VMContext *context = state->GetVMContext();
  // 2 operands are over bottom of stack 
  CallStackItem *level = new CallStackItemData(context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state, "Call body", value, context->IsSerializationEnabled());
  context->AddLevel(level);
  level->GetVariables().VarLocal("_this");
  level->GetVariables().VarSet("_this", oper1, true);
  return NOTHING;
#else
  // evaluate non compiled string
  GameVarSpace local(state->GetContext(), false);
  int gshandle = state->BeginContext(&local);
  state->VarSetLocal("_this", oper1, true);
  GameValue ret = state->EvaluateMultiple((RString)oper2, GameState::EvalContext::_default, state->GetGlobalVariables());
  state->EndContext(gshandle);
  return ret;
#endif
}

/*!
\patch 1.82 Date 8/12/2002 by Ondra
- New: Scripting: Function "private" introduces local variable in the innermost scope.
*/

static GameValue StringLocal(const GameState *state, GameValuePar oper1)
{
  if (oper1.GetType()==GameString)
  {
    RString varName = oper1;
    if (varName[0]!='_')
    {
      state->SetError(EvalNamespace);
      return NOTHING;
    }
    const_cast<GameState *>(state)->VarLocal(varName);
  }
  else
  {
    const GameArrayType &array = oper1;
    for (int i=0; i<array.Size(); i++)
    {
      if (array[i].GetType()!=GameString)
      {
        state->TypeError(GameString,array[i].GetType());
        break;
      }
      RString varName = array[i];
      if (varName[0]!='_')
      {
        state->SetError(EvalNamespace);
        return NOTHING;
      }
      const_cast<GameState *>(state)->VarLocal(varName);

    }
  }
  return NOTHING;
}

static GameValue StringCallNoArg( const GameState *state, GameValuePar oper1)
{
#if USE_PRECOMPILATION
  GameDataCode *value = static_cast<GameDataCode *>(oper1.GetData());
  VMContext *context = state->GetVMContext();
  // 1 operand is over bottom of stack 
  CallStackItem *level = new CallStackItemData(context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 1, state, "Call body", value, context->IsSerializationEnabled());
  context->AddLevel(level);
  return NOTHING;
#else
  GameVarSpace local(state->GetContext(), false);
  int gshandle = state->BeginContext(&local);
  GameValue ret = state->EvaluateMultiple((RString)oper1, GameState::EvalContext::_default, state->GetGlobalVariables());
  state->EndContext(gshandle);
  return ret;
#endif
}

static GameValue StringIgnore( const GameState *state, GameValuePar oper1)
{
  return NOTHING;
}

static GameValue StringRepeat( const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  GameData *code = static_cast<GameDataWhile *>(oper1.GetData())->GetCode();
#if USE_PRECOMPILATION
  GameDataCode *condition = static_cast<GameDataCode *>(code);
  GameDataCode *statement = static_cast<GameDataCode *>(oper2.GetData());
  VMContext *context = state->GetVMContext();
  // 2 operands are over bottom of stack 
  CallStackItem *level = new CallStackItemRepeat(
    context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state,
    condition, statement, state->MaxIterations(), context->IsSerializationEnabled());
  context->AddLevel(level);
  return NOTHING;
#else
  int maxIters = state->MaxIterations();
  bool enableRepeat=true;
  for (int i=0;enableRepeat; i++)
  {
    if (maxIters && i>=maxIters)
    {
      RptF("Max iteration count exceeded in while loop");
      state->SetError(EvalGen);
      break;
    }
    // test condition
    {
      GameVarSpace local(state->GetContext(), false);
      int gshandle = state->BeginContext(&local);
      GameValue value = state->EvaluateMultiple(code->GetString(), GameState::EvalContext::_default, state->GetGlobalVariables());
      bool test = false;
      if (value.GetType()&GameBool)
      {
        test = bool(value);
      }
      else
      {
        state->TypeError(GameBool,value.GetType());
      }

      state->EndContext(gshandle);
      if (!test) break;
    }
    // perform loop body
    {
      GameVarSpace local(state->GetContext(), false);
      int gshandle = state->BeginContext(&local);
      const_cast<GameState *>(state)->Execute((RString)oper2, GameState::EvalContext::_default, state->GetGlobalVariables());
      if (state->GetLastError() != EvalOK || local._blockExit)
        enableRepeat = false;    //blockbreak - stop cycling.
      state->EndContext(gshandle);
    }
  }
  return NOTHING;
#endif
}

static GameValue StringElse( const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  GameValue arrayVal = state->CreateGameValue(GameArray);
  GameArrayType &array = arrayVal;
  array.Realloc(2);
  array.Append() = oper1;
  array.Append() = oper2;
  return arrayVal;
}

static GameValue IfBool( const GameState *state, GameValuePar oper1)
{
  bool value = oper1;
  return GameValue(new GameDataIf(value));
}

static GameValue WhileString( const GameState *state, GameValuePar oper1)
{
  return GameValue(new GameDataWhile(oper1.GetData()));
}

static GameValue StringThen( const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  bool test = oper1;
  if (test)
  {
#if USE_PRECOMPILATION
    GameDataCode *value = static_cast<GameDataCode *>(oper2.GetData());
    VMContext *context = state->GetVMContext();
    // 2 operands are over bottom of stack 
    CallStackItem *level = new CallStackItemData(context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state, "If-Then body", value, context->IsSerializationEnabled());
    context->AddLevel(level);
#else
    // evaluate non compiled string
    GameVarSpace local(state->GetContext(), false);
    int gshandle = state->BeginContext(&local);
    GameValue ret = state->EvaluateMultiple((RString)oper2, GameState::EvalContext::_default, state->GetGlobalVariables());
    state->EndContext(gshandle);
    return ret;
#endif
  }
  return NOTHING;
}

static GameValue ArrayThen( const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  bool test = oper1;
  const GameArrayType &array = oper2;

  if (array.Size()!=2)
  {
    state->SetError(EvalDim,array.Size(),2);
    return NOTHING;
  }
  if (array[0].GetType() != GameCode)
  {
    state->TypeError(GameCode, array[0].GetType());
    return NOTHING;
  }
  if (array[1].GetType() != GameCode)
  {
    state->TypeError(GameCode, array[1].GetType());
    return NOTHING;
  }

  GameValuePar expression = test ? array[0] : array[1];
#if USE_PRECOMPILATION
  GameDataCode *value = static_cast<GameDataCode *>(expression.GetData());
  VMContext *context = state->GetVMContext();
  RString scopeName = test ? "If-Then body" : "If-Else body";
  // 2 operands are over bottom of stack 
  CallStackItem *level = new CallStackItemData(context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state, scopeName, value, context->IsSerializationEnabled());
  context->AddLevel(level);
  return NOTHING;
#else
  // evaluate non compiled string
  GameVarSpace local(state->GetContext(), false);
  int gshandle = state->BeginContext(&local);
  GameValue ret = state->EvaluateMultiple((RString)expression, GameState::EvalContext::_default, state->GetGlobalVariables());
  state->EndContext(gshandle);
  return ret;
#endif
}

static GameValue NamespaceWith(const GameState *state, GameValuePar oper1)
{
  return GameValue(new GameDataWith(oper1.GetData()));
}

static GameValue NamespaceDo( const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  GameData *data = static_cast<GameDataWith *>(oper1.GetData())->GetNamespace();
  GameDataNamespace *ns = static_cast<GameDataNamespace *>(data);
#if USE_PRECOMPILATION
  GameDataCode *code = static_cast<GameDataCode *>(oper2.GetData());
  VMContext *context = state->GetVMContext();
  // 2 operands are over bottom of stack 
  CallStackItem *level = new CallStackItemWith(context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state, "With body", ns, code, context->IsSerializationEnabled());
  context->AddLevel(level);
#else
  // evaluate non compiled string
  GameDataNamespace *oldNamespace = state->SetGlobalVariables(ns);
  GameVarSpace local(state->GetContext(), false);
  int gshandle = state->BeginContext(&local);
  GameValue ret = state->EvaluateMultiple((RString)oper2, GameState::EvalContext::_default, state->GetGlobalVariables());
  state->EndContext(gshandle);
  state->SetGlobalVariables(oldNamespace);
#endif
  return NOTHING;
}

static GameValue ListCountCond( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
#if USE_PRECOMPILATION
  GameDataCode *code = static_cast<GameDataCode *>(oper1.GetData());
  GameDataArray *array = static_cast<GameDataArray *>(oper2.GetData());
  VMContext *context = state->GetVMContext();
  // 2 operands are over bottom of stack 
  CallStackItem *level = new CallStackItemArrayCount(
    context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state, 
    code, array, context->IsSerializationEnabled());
  context->AddLevel(level);
  return NOTHING;
#else
  GameVarSpace local(state->GetContext(), false);
  int count=0;
  int gshandle = state->BeginContext(&local);
  const GameArrayType &array = oper2;
  for( int i=0; i<array.Size(); i++ )
  {
    const GameValue &val=array[i];
    // set local varible - will be deleted on EndContext
    state->VarSetLocal("_x", val, true);
    if (state->EvaluateBool((RString)oper1, GameState::EvalContext::_default, state->GetGlobalVariables())) count++;
    if (state->GetLastError() || local._blockExit)
    {
      state->EndContext(gshandle);
      return GameValue();
    }
  }
  state->EndContext(gshandle);
  return (float)count;
#endif
}

static GameValue ListForEach( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
#if USE_PRECOMPILATION
  GameDataCode *code = static_cast<GameDataCode *>(oper1.GetData());
  GameDataArray *array = static_cast<GameDataArray *>(oper2.GetData());
  VMContext *context = state->GetVMContext();
  // 2 operands are over bottom of stack 
  CallStackItem *level = new CallStackItemArrayForEach(
    context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state,
    code, array, context->IsSerializationEnabled());
  context->AddLevel(level);
  return NOTHING;
#else
  GameVarSpace local(state->GetContext(), false);
  int gshandle = state->BeginContext(&local);
  const GameArrayType &array=oper2;
  for( int i=0; i<array.Size(); i++ )
  {
    const GameValue &val=array[i];
    // set local varible - will be deleted on EndContext
    state->VarSetLocal("_x",val,true);
    const_cast<GameState *>(state)->Execute((RString)oper1, GameState::EvalContext::_default, state->GetGlobalVariables());
    if( state->GetLastError() || local._blockExit)
    {
      state->EndContext(gshandle);
      return NOTHING;
    }
  }
  state->EndContext(gshandle);
  return NOTHING;
#endif
}

static GameValue ListIsIn( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  const GameArrayType &array=oper2;
  for( int i=0; i<array.Size(); i++ )
  {
    const GameValue &val=array[i];
    if (val.IsEqualTo(oper1)) return true;
  }
  return false;
}

static GameValue ListFind( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  const GameArrayType &array=oper1;
  for( int i=0; i<array.Size(); i++ )
  {
    const GameValue &val=array[i];
    if (val.IsEqualTo(oper2)) return (float)i;
  }
  return -1.0f;
}

static GameValue IsVarNull(const GameState *state, GameValuePar oper1)
{
  GameDataNamespace *globals = state->GetGlobalVariables();
  DoAssert(globals);

#if USE_PRECOMPILATION
  if (oper1.GetType() == GameCode)
  {
    // Warning: do not use this pattern elsewhere, to create CallStackItem and pass control to it is better solution (performance, error handling)
    // here, it is used only because of difficulties with VMContext::_undefinedIsNil and too much work to handle it correctly
    GameDataCode *code = static_cast<GameDataCode *>(oper1.GetData());
    GameVarSpace local(state->GetContext(), false);
    int gshandle = state->BeginContext(&local);
    GameValue result = state->Evaluate(code->GetString(), code->GetCode(), GameState::EvalContext::_default, globals);
    state->EndContext(gshandle);
    return result.GetNil();
  }
  else
  {
    Assert(oper1.GetType() == GameString);
    RString name = oper1;
    GameValue var = state->VarGet(name, false, globals);
    return var.GetNil();
  }
#else
  RString name=oper1;
  GameVarSpace *curSpace=state->GetContext();
  GameValue val;
  GameVarSpace cx(curSpace, false);
  int gshandle = state->BeginContext(&cx);
  val=state->EvaluateMultiple(name, GameState::EvalContext::_default, globals);
  state->EndContext(gshandle);
  return val.GetNil();
#endif
}

static GameValue typeOf(const GameState *state, GameValuePar oper1)
{
  return state->GetInternalTypeName(oper1.GetType());
}


static GameValue exitWith(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  bool test = oper1;
  if (test) 
  {
#if USE_PRECOMPILATION
    GameDataCode *code = static_cast<GameDataCode *>(oper2.GetData());
    VMContext *context = state->GetVMContext();
    // 2 operands are over bottom of stack 
    CallStackItem *level = new CallStackItemExitWith(
      context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state,
      code, context->IsSerializationEnabled());
    context->AddLevel(level);
    return NOTHING;
#else
    GameVarSpace *vstate=state->GetContext();
    GameVarSpace evalLevel(vstate, false);
    int gshandle = state->BeginContext(&evalLevel);
    GameValue result=state->EvaluateMultiple((RString)oper2, GameState::EvalContext::_default, state->GetGlobalVariables());
    state->EndContext(gshandle);
    vstate->_blockExit=true;
    return result;
#endif
  }
  else
    return GameValue();
}


static GameValue supportInfo(const GameState *state, GameValuePar oper1)
{
  RString mask=oper1;
  RString submask;
  SRef<GameArrayType> arr=new GameArrayType;
  bool withArgs=true;
  if (mask.GetLength()!=0 && (mask.GetLength()<3 || mask[1]!=':')) 
  {
    state->SetError(EvalForeignError,"Bad mask format");
    return GameValue();
  }
  if (mask.GetLength()!=0)
    if (mask[mask.GetLength()-1]=='*')
    {
      submask=RString((const char *)mask+2,mask.GetLength()-3);
      withArgs=false;
    }
    else
      submask=(const char *)mask+2;
  else
  {
    mask="*:*";
    withArgs=true;
  }
  if (mask.GetLength()==0 || mask[0]=='t' || mask[0]=='*')
  {
    for (int i=0;i<state->_typeNames.Size();i++) if (mask[2]=='*' || stricmp((const char *)mask+2,state->_typeNames[i]->_typeName)==0)
      arr->Append()=RString("t:",state->_typeNames[i]->_name);
  }
  if (mask.GetLength()==0 || mask[0]=='b' || mask[0]=='*')
  {
    const GameOperatorsType& oper=state->_operators;
    for (int i=0; i<oper.NTables(); i++)
    {
      AutoArray<GameOperators>  &container = oper.GetTable(i);
      for (int j=0; j<container.Size(); j++)
      {
        GameOperators &oplist=container[j];
        if (withArgs || stricmp(submask,oplist._name)==0)
          for (int k=0; k<oplist.Size();k++)
          {
            GameOperator &oper=oplist[k];
            char buff[512];
            snprintf(buff,512,"b:%s %s %s",state->GetInternalTypeName(oper._operator->GetArg1Type()).Data(),oper._name.Data(),state->GetInternalTypeName(oper._operator->GetArg2Type()).Data());
            if (!withArgs || submask.GetLength()==0 || stricmp(buff+2,submask)==0) arr->Append()=buff;
          }
      }
    }
  }
  if (mask.GetLength()==0 || mask[0]=='u' || mask[0]=='*')
  {
    const GameFunctionsType& funct=state->_functions;
    for (int i=0; i<funct.NTables(); i++)
    {
      AutoArray<GameFunctions>  &container = funct.GetTable(i);
      for (int j=0; j<container.Size(); j++)
      {
        GameFunctions &oplist=container[j];
        if (withArgs || stricmp(submask,oplist._name)==0)
          for (int k=0; k<oplist.Size();k++)
          {
            GameFunction &funct=oplist[k];
            char buff[512];
            snprintf(buff,512,"u:%s %s",funct._name.Data(),state->GetInternalTypeName(funct._operator->GetArgType()).Data());
            if (!withArgs || submask.GetLength()==0 || stricmp(buff+2,submask)==0) arr->Append()=buff;
          }
      }
    }
  }
  if (mask.GetLength()==0 || mask[0]=='n' || mask[0]=='*')
  {
    const GameNularsType& nulars=state->_nulars;
    for (int i=0; i<nulars.NTables(); i++)
    {
      AutoArray<GameNular>  &container = nulars.GetTable(i);
      for (int j=0; j<container.Size(); j++)
      {
        GameNular &nular=container[j];
        if (submask.GetLength()==0 || stricmp(submask,nular._name)==0) arr->Append()=RString("n:",nular._name);
      }

    }
  }

  return *arr;
}

static GameValue forFrom(const GameState *gs, GameValuePar oper1, GameValuePar oper2)
{
  GameDataForClass  *forclass=static_cast<GameDataForClass  *>(oper1.GetData());
  forclass->_first=oper2;
  return oper1;
}

static GameValue forTo(const GameState *gs, GameValuePar oper1, GameValuePar oper2)
{
  GameDataForClass  *forclass=static_cast<GameDataForClass  *>(oper1.GetData());
  forclass->_last=oper2;
  return oper1;
}

static GameValue forStep(const GameState *gs, GameValuePar oper1, GameValuePar oper2)
{
  GameDataForClass  *forclass=static_cast<GameDataForClass  *>(oper1.GetData());
  forclass->_step=oper2;
  return oper1;
}

static GameValue forDo(const GameState *gs, GameValuePar oper1, GameValuePar oper2)
{
  GameDataForClass  *forclass=static_cast<GameDataForClass  *>(oper1.GetData());
  return forclass->Cycle(gs, oper2);
}

#if !USE_PRECOMPILATION
class GameFunctor_CopyContextToGlobal
{
  GameState *_global;
public:
  GameFunctor_CopyContextToGlobal(GameState *st):_global(st) {}
  bool operator()(GameVariable &variable,VarBankType *container)
  {
    _global->VarSet(variable._name, variable._value, variable._readOnly, false, _global->GetGlobalVariables());
    return false;
  }
};
#endif

GameValue GameDataForClass::Cycle(const GameState *gs, GameValuePar code)
{
#if USE_PRECOMPILATION

  if (_update.GetData() && _update.GetData()->GetString().GetLength() != 0)
  {
    GameDataCode *init = static_cast<GameDataCode *>(_init.GetData());
    GameDataCode *condition = static_cast<GameDataCode *>(_cond.GetData());
    GameDataCode *update = static_cast<GameDataCode *>(_update.GetData());
    GameDataCode *statement = static_cast<GameDataCode *>(code.GetData());
    VMContext *context = gs->GetVMContext();
    // 2 operands (of forDo) are over bottom of stack 
    CallStackItem *level = new CallStackItemForC(
      context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, gs,
      init, condition, update, statement, context->IsSerializationEnabled());
    context->AddLevel(level);
    return NOTHING;
  }
  else
  {
    GameDataCode *statement = static_cast<GameDataCode *>(code.GetData());
    VMContext *context = gs->GetVMContext();
    // 2 operands (of forDo) are over bottom of stack 
    CallStackItem *level = new CallStackItemForBASIC(
      context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, gs,
      _variableName, _first, _last + 0.5f * _step, _step, statement, context->IsSerializationEnabled());
    context->AddLevel(level);
    return NOTHING;
  }

#else

  GameValue ret;
  if (_update.GetData() && _update.GetData()->GetString().GetLength() != 0)
  {
#if !defined(_MSC_VER) || _MSC_VER>1300
    GameVarSpace sp(gs->GetContext(), false);
    int gshandle = gs->BeginContext(&sp);
    gs->EvaluateMultiple((RString)_init, GameState::EvalContext::_default, gs->GetGlobalVariables());
    gs->EndContext(gshandle);
    // copying local variables one level up
    sp._vars.ForEachF(GameFunctor_CopyContextToGlobal(const_cast<GameState *>(gs)));
#endif
    GameVarSpace space(gs->GetContext(), false);
    int gshandle2 = gs->BeginContext(&space);
    bool res=gs->EvaluateMultiple((RString)_cond, GameState::EvalContext::_default, gs->GetGlobalVariables());
    while (res)
    {
      ret=gs->EvaluateMultiple((RString)code, GameState::EvalContext::_default, gs->GetGlobalVariables());
      gs->EvaluateMultiple((RString)_update, GameState::EvalContext::_default, gs->GetGlobalVariables());
      if (space._blockExit) break;
      space._vars.Clear();
      res=gs->EvaluateMultiple((RString)_cond, GameState::EvalContext::_default, gs->GetGlobalVariables());
    }
    gs->EndContext(gshandle2);
  }
  else
  {
    GameVarSpace space(gs->GetContext(), false);
    _last+=_step*0.5f;    
    int gshandle = gs->BeginContext(&space);
    for (float pt=_first;_step>=0?pt<_last:pt>_last;pt+=_step)
    {
      space.VarLocal(_variableName);
      space.VarSet(_variableName,pt);
      ret=gs->EvaluateMultiple((RString)code, GameState::EvalContext::_default, gs->GetGlobalVariables());
      if (space._blockExit) break;
      GameValue ptv;
      if (space.VarGet(_variableName,ptv)==false) break;
      if (ptv.GetNil()) break;
      pt=ptv;
      space._vars.Clear();
    }    
    gs->EndContext(gshandle);
  }
  return ret;

#endif
}

static GameValue createForBasic(const GameState *gs, GameValuePar oper1)
{
  GameStringType str = oper1;
  GameDataForClass *newfor = new GameDataForClass;
  newfor->_variableName = str;
  return GameValue(newfor);
}

static GameValue createForC(const GameState *state, GameValuePar oper1)
{
#if !defined(_MSC_VER) || _MSC_VER>1300
  const GameArrayType &array = oper1;
  if (array.Size() != 3)
  {
    state->SetError(EvalDim, array.Size(), 3);
    return NOTHING;
  }
  for (int i=0; i<3; i++)
  {
    if (array[i].GetType() != GameCode)
    {
      state->TypeError(GameCode, array[i].GetType());
      return NOTHING;
    }
  }

  GameDataForClass *newfor = new GameDataForClass;
  newfor->_init = array[0];
  newfor->_cond = array[1];
  newfor->_update = array[2];
  return GameValue(newfor);
#else
  Fail("Not supported with VC 6.0");
  return GameValue();
#endif
}

static GameValue switchForm(const GameState *state, GameValuePar oper1)
{
  GameDataSwitch *newsw= new GameDataSwitch;
  newsw->_mode=newsw->swNoTriggered;
  newsw->_test=oper1;
  return GameValue(newsw);
}

#define GetSwitchInfo(oper1) static_cast<GameDataSwitch *>(oper1.GetData());

static GameValue switchFormDo(const GameState *state, GameValuePar oper1,GameValuePar oper2)
{
#if USE_PRECOMPILATION
  GameDataCode *statement = static_cast<GameDataCode *>(oper2.GetData());
  VMContext *context = state->GetVMContext();
  // 2 operands are over bottom of stack 
  CallStackItem *level = new CallStackItemSwitch(
    context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state, 
    oper1, statement, context->IsSerializationEnabled());
  context->AddLevel(level);
  return NOTHING;
#else
  GameVarSpace *previous=state->GetContext();  
  GameVarSpace eval_space(previous, false);
  eval_space.VarSet("___switch",oper1);    
  int gshandle = state->BeginContext(&eval_space);
  state->EvaluateMultiple((RString)oper2, GameState::EvalContext::_default, state->GetGlobalVariables());
  state->EndContext(gshandle);
  GameValue result;
  bool succ=eval_space.VarGet("___switch",result);
  if (succ==false || result.GetNil()==true)
  {
    state->SetError(EvalForeignError,"Invalid switch block");
    return GameValue();
  }
  if (result.GetType()!=GameSwitch) 
  {
    state->SetError(EvalForeignError,"Unable to evaluate switch block");
    return GameValue();
  }
  GameDataSwitch *sw=GetSwitchInfo(result);
  GameValuePar process = sw->_mode == sw->swTriggered ? sw->_selBranch : sw->_defBranch;
  GameVarSpace exeSpace(previous, false);
  int gshandle = state->BeginContext(&exeSpace);
  result=state->EvaluateMultiple((RString)process, GameState::EvalContext::_default, state->GetGlobalVariables());
  state->EndContext(gshandle);
  return result;
#endif
}


static GameValue switchCase( const GameState *state, GameValuePar oper1)
{
  GameValue swoper=state->VarGetLocal("___switch");
  GameDataSwitch *sw=GetSwitchInfo(swoper);  
  if (sw->_mode==sw->swNoTriggered)
  {
    if (sw->_test.IsEqualTo(oper1))
    {
      sw->_mode=sw->swTriggerNext;
    }
  }
  return swoper;
}

static GameValue switchColon( const GameState *state, GameValuePar oper1, GameValuePar oper2 )
{
  GameDataSwitch *sw=GetSwitchInfo(oper1);  
  if (sw->_mode==sw->swTriggerNext)
  {
    sw->_mode=sw->swTriggered;
    sw->_selBranch = oper2;
#if USE_PRECOMPILATION
    VMContext *context = state->GetVMContext();
    context->ThrowBreak(true);
#else
    GameVarSpace *context=state->GetContext();
    context->_blockExit=true;    
#endif
  }
  return GameValue();
}

static GameValue switchDefault( const GameState *state, GameValuePar oper1)
{
  GameValue swoper=state->VarGetLocal("___switch");
  if (swoper.GetNil())
    return NOTHING;
  GameDataSwitch *sw=GetSwitchInfo(swoper);  
  sw->_defBranch = oper1;
  return oper1;
}

static GameValue ScopeName(const GameState *state, GameValuePar oper1)
{
#if USE_PRECOMPILATION
  VMContext *context = state->GetVMContext();
  if (!context->SetScopeName(oper1))
    state->SetError(EvalScopeNameExists);  
  return NOTHING;
#else
  GameVarSpace *space=state->GetContext();
  if (space->_scopeName[0] && space->_scopeName!=(RString)oper1)  
    state->SetError(EvalScopeNameExists);  
  else
    space->_scopeName=oper1;
  return GameValue();
#endif
}



static GameValue BreakTo(const GameState *state, GameValuePar oper1)
{
#if USE_PRECOMPILATION
  VMContext *context = state->GetVMContext();
  context->ThrowBreak(false, NOTHING, oper1);
  return NOTHING;
#else
  int level=state->FindContext((RString)oper1);
  if (level==-1)
  {
    state->SetError(EvalScopeNotFound); return GameValue();
  }
  level++;
  GameVarSpace *vspc;;
  while ((vspc=state->GetContext(level))!=NULL) 
  {
    vspc->_blockExit=true;
    level++;
  }
  return GameValue();
#endif
}

static GameValue BreakOut(const GameState *state, GameValuePar oper1)
{
#if USE_PRECOMPILATION
  VMContext *context = state->GetVMContext();
  context->ThrowBreak(true, NOTHING, oper1);
  return NOTHING;
#else
  int level=state->FindContext((RString)oper1);
  if (level==-1)
  {
    state->SetError(EvalScopeNotFound); return GameValue();
  }
  GameVarSpace *vspc;;
  while ((vspc=state->GetContext(level))!=NULL) 
  {
    vspc->_blockExit=true;
    level++;
  }
  return GameValue();
#endif
}

static GameValue tryCommand(const GameState *state, GameValuePar oper1)
{
  return GameValue(new GameDataException(oper1,state->GetContext()));
}

#define GetExceptionInfo(oper1) static_cast<GameDataException *>(oper1.GetData());

static GameValue catchCommand(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  GameDataException *exp = GetExceptionInfo(oper1);     //get try block info
#if USE_PRECOMPILATION
  GameDataCode *tryBlock = static_cast<GameDataCode *>(exp->_code.GetData());
  GameDataCode *catchBlock = static_cast<GameDataCode *>(oper2.GetData());
  VMContext *context = state->GetVMContext();
  // 2 operands are over bottom of stack 
  CallStackItem *level = new CallStackItemTry(
    context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 2, state, 
    tryBlock, catchBlock, context->IsSerializationEnabled());
  context->AddLevel(level);
  return NOTHING;
#else
  GameState *gs=const_cast<GameState *>(state);         //get writtable version of gameState
  GameVarSpace *context=state->GetContext();          //get current context
  if (exp->GetTryLevel()!=context)                    //try block must be in the same level as catch block
  {
    state->SetError(EvalInvalidTryBlock);return GameValue();
  }
  GameDataException *curHandler=gs->SetCurrentExceptionHandler(exp);    //set current exception handler
  GameVarSpace sublevel(context, false);                       
  int gshandle = gs->BeginContext(&sublevel);
  GameValue result=gs->EvaluateMultiple((RString)exp->_code, GameState::EvalContext::_default, state->GetGlobalVariables());        //process try level
  gs->EndContext(gshandle);
  gs->SetCurrentExceptionHandler(curHandler);         //restore last exception handler
  if (exp->IsThrown())                                //has been exception thrown?
  {
    GameVarSpace sublevel(context, false);               
    int gshandle = gs->BeginContext(&sublevel);
    sublevel.VarSet("_exception",exp->_excepResult);   //define _exception variable
    result=gs->EvaluateMultiple((RString)oper2, GameState::EvalContext::_default, state->GetGlobalVariables());      //process catch block
    gs->EndContext(gshandle);                        //catch block can throw another exception  
  }
  return result;                                      //return result of this block
#endif
}

static GameValue throwCommand(const GameState *state, GameValuePar oper1)
{
#if USE_PRECOMPILATION
  VMContext *context = state->GetVMContext();
  context->ThrowException(oper1);
  return NOTHING;
#else
  GameState *gs=const_cast<GameState *>(state);         //get writtable version of gameState
  if (gs->ThrowException(oper1)==false)
  {
    gs->SetError(EvalUnhandledException,oper1.GetText().Data());
  }
  return NOTHING;
#endif
}

static GameValue StringCompile(const GameState *state, GameValuePar oper1)
{
#if USE_PRECOMPILATION
  RString expression = oper1;

  return GameValue(new GameDataCode(state, expression, state->GetGlobalVariables()));
#else
  return oper1;
#endif
}

static GameValue ParseNumber(const GameState *state, GameValuePar oper1)
{
  RString input = oper1;
  float result = atof(input);
  return result;
}

static GameValue VMWaitUntil(const GameState *state, GameValuePar oper1)
{
#if USE_PRECOMPILATION
  GameDataCode *condition = static_cast<GameDataCode *>(oper1.GetData());
  VMContext *context = state->GetVMContext();
  // 1 operand is over bottom of stack 
  CallStackItem *level = new CallStackItemWaitUntil(
    context->GetCallStack(), context->GetVariables(), context->_stack.Size() - 1, state, 
    condition, context->IsSerializationEnabled());
  context->AddLevel(level);
  return NOTHING;
#else
  Fail("waitUntil not supported");
  return NOTHING;
#endif
}

static GameValue SetScriptName(const GameState *state, GameValuePar oper1)
{
#if USE_PRECOMPILATION
  VMContext *context = state->GetVMContext();
  if (context && context->_displayName.GetLength() == 0) context->_displayName = oper1;
#endif
  return NOTHING;
}

GameDataNamespace *GetNamespace(GameValuePar oper)
{
  if (oper.GetType() == GameNamespace) return static_cast<GameDataNamespace *>(oper.GetData());
  return NULL;
}

GameValue NamespaceSetVariable(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  // left operand check
  GameDataNamespace *ns = GetNamespace(oper1);
  if (!ns) return NOTHING;

  // right operand check
  const GameArrayType &array = oper2;
  if (array.Size() != 2)
  {
    state->SetError(EvalDim, array.Size(), 2);
    return NOTHING;
  }
  if (array[0].GetType() != GameString)
  {
    state->TypeError(GameString, array[0].GetType());
    return NOTHING;
  }

  // set the variable
  RString name = array[0];
  name.Lower();
  if (array[1].GetNil()) // when variable is set to nil, delete it
    ns->VarDelete(name);
  else
    ns->VarSet(name, array[1]);

  return NOTHING;
}

inline bool CheckSize(const GameState *state, GameArrayType array, int size)
{
  if (array.Size() == size) return true;
  state->SetError(EvalDim, array.Size(), size);
  return false;
}

inline bool CheckType(const GameState *state, GameValuePar oper, const GameType &type)
{
  if (oper.GetType() == type) return true;
  state->TypeError(type, oper.GetType());
  return false;
}

GameValue NamespaceGetVariable(const GameState *state, GameValuePar oper1, GameValuePar oper2)
{
  // left operand check
  GameDataNamespace *ns = GetNamespace(oper1);
  if (!ns) return NOTHING;

  // right operand check
  if (oper2.GetType() == GameString)
  {
    RString name = oper2;
    name.Lower();

    // get the variable
    GameValue value;
    if (ns->VarGet(name, value)) return value;
  }
  else if (oper2.GetType() == GameArray)
  {
    const GameArrayType &array = oper2;
    if (!CheckSize(state, array, 2)) return NOTHING;
    if (!CheckType(state, array[0], GameString)) return NOTHING;
    RString name = array[0];
    name.Lower();
    GameValue var;
    if (ns->VarGet(name, var)) return var;
    return  array[1];
  }
  return NOTHING;
}

#define OPERATORS_DEFAULT(XX, Category) \
  XX(GameScalar, "^", mocnina, Na, GameScalar, GameScalar, "a", "b", "a raised to the power of b", "count^4", "", "", "", Category) \
  XX(GameScalar, "*", soucin, Soucin, GameScalar, GameScalar, "a", "b", "a multiplied by b", "iCounter*3", "", "", "", Category) \
  XX(GameScalar, "/", soucin, Podil, GameScalar, GameScalar, "a", "b", "a divided by b", "enemyStrenght/3", "", "", "", Category) \
  XX(GameScalar, "%", soucin, Zbytek, GameScalar, GameScalar, "a", "b", "The remainder of a divided by b. Note that the remainder is calculated in the real domain.", "4.5 % 3", "1.5", "", "", Category) \
  XX(GameScalar, "mod", soucin, Zbytek2, GameScalar, GameScalar, "a", "b", "The remainder of a divided by b. Note that the remainer is calculated in the real domain.", "3 mod 2", "1", "", "", Category) \
  XX(GameScalar, "atan2", soucin, Atan2, GameScalar, GameScalar, "x", "y", "Returns the arctangens of x/y. The returned value is in degrees, in the range from -180 to +180, using the signs of both parameters to determine the quadrant of the returned value.", "5 atan2 3", "59.0362", "", "", Category) \
  XX(GameScalar, "+", soucet, Soucet, GameScalar, GameScalar, "a", "b", "a plus b", "counter + 1", "", "", "", Category) \
  XX(GameScalar, "-", soucet, Rozdil, GameScalar, GameScalar, "a", "b", "a minus b", "counter - 1", "", "", "", Category) \
  XX(GameBool, ">=", compare, CmpGE, GameScalar, GameScalar, "a", "b", "a greater or equal than b", "player distance redCar < 100", "", "", "", Category) \
  XX(GameBool, "<=", compare, CmpLE, GameScalar, GameScalar, "a", "b", "a lesser or equal than b", "player distance redCar <= 100", "", "", "", Category) \
  XX(GameBool, ">", compare, CmpG, GameScalar, GameScalar, "a", "b", "a greater than b", "counter > 4", "", "", "", Category) \
  XX(GameBool, "<", compare, CmpL, GameScalar, GameScalar, "a", "b", "a lesser than b", "counter < 4", "", "", "", Category) \
  XX(GameBool, "==", compare, CmpE, GameScalar, GameScalar, "a", "b", "a equal to b", "counter == 4", "", "", "", Category) \
  XX(GameBool, "!=", compare, CmpNE, GameScalar, GameScalar, "a", "b", "a is not equal to b", "counter != 4", "", "", "", Category) \
  XX(GameBool, "==", compare, StrCmpE, GameString, GameString, "a", "b", "a equal to b (case insensitive)", "nameOfPlayer == \"John Doe\"", "", "", "", Category) \
  XX(GameBool, "!=", compare, StrCmpNE, GameString, GameString, "a", "b", "a is not equal to b (case insensitive)", "nameOfPlayer != \"John Doe\"", "", "", "", Category) \
  XX(GameString, "+", soucet, StrAdd, GameString, GameString, "stringA", "stringB", "stringA and stringB are concatenated.", "\"I\" + \" am\" + \" blind\"", "\"I am blind\"", "", "", Category) \
  XX(GameArray, "+", soucet, ListAdd, GameArray, GameArray, "arrayA", "arrayB", "arrayA and arrayB are concatenated.", "[0, 1, 2] + [1, 2, 3]", "[0, 1, 2, 1, 2, 3]", "", "", Category) \
  XX(GameArray, "-", soucet, ListSub, GameArray, GameArray, "arrayA", "arrayB", "All elements in arrayB are removed from arrayA.", "[0, 1, 2, 4, 0, 1, 2, 3, 4, 5] - [1, 2, 3]", "[0, 4, 0, 4, 5]", "", "", Category) \
  XX(GameBool, "&&", logicAnd, BoolAnd, GameBool, GameBool, "a", "b", "a and b", "alive player && alive leader player", "", "", "", Category) \
  XX(GameBool, "and", logicAnd, BoolAnd2, GameBool, GameBool, "a", "b", "a and b", "alive player and alive leader player", "", "", "", Category) \
  XX(GameBool, "||", logicOr, BoolOr, GameBool, GameBool, "a", "b", "a or b", "!alive player || !alive leader player", "", "", "", Category) \
  XX(GameBool, "or", logicOr, BoolOr2, GameBool, GameBool, "a", "b", "a or b", "not alive player or not alive leader player", "", "", "", Category) \
  XX(GameVoid, "select", function, ListSelect, GameArray, GameScalar, "array", "index", "Selects index element of the array. Index 0 denotes the first element, 1 the second, etc.", "[1, 2, 3] select 1", "2", "", "", Category) \
  XX(GameVoid, "select", function, ListSelect, GameArray, GameBool, "array", "index", "If the index is false, this selects the first element of the array. If it is true, it selects the second one.", "", "", "", "", Category) \
  XX(GameNothing, "set", function, ListSet, GameArray, GameArray, "array", "element", "Format of element is [index, value].\r\nChanges an element of the given array. If the element does not exist, index+1 is called to create it.", "array set [0, \"Hello\"]", "", "1.75", "", Category) \
  XX(GameNothing, "resize", function, ListResize, GameArray, GameScalar, "array", "count", "Changes the array size. This function can be used to add or remove elements from the array.", "array resize 2", "", "1.75", "", Category) \
  XX(GameScalar, "count", function, ListCountCond, GameCode, GameArray, "condition", "array", "Counts the elements in the array for which the given condition is true.\r\n\r\nIt is calculated as follows:\r\n\r\n1) Set the count to 0.\r\n2) For each element in the array assign an element as _x and evaluate the condition expression. If it's true, increase the count.", "\"_x > 2\" count [0, 1, 1, 2, 3, 3]", "2", "", "", Category) \
  XX(GameNothing, "forEach", function, ListForEach, GameCode, GameArray, "command", "array", "Executes the given command for each element in array.\r\nIt's executed as follows:\r\n\r\nfor each element of array an element is assigned as _x and the command is executed.", "\"_x setdammage 1\" forEach units group player", "", "", "", Category) \
  XX(GameBool, "in", function, ListIsIn, GameVoid, GameArray, "x", "array", "Checks whether x is equal to any element in the array.", "1 in [0, 1, 2]", "true", "", "", Category) \
  XX(GameScalar, "find", function, ListFind, GameArray, GameVoid, "array", "x", "Returns the position of the first array element that matches x, returns -1 if not found.", "[0, 1, 2] find 1", "1", "2.92", "", Category) \
  XX(GameAny, "then", function, StringThen, GameIf, GameCode, "if", "codeToExecute", "The code is executed when the if condition is met. If the code is executed, the last value calculated in the code is returned. If the code is not executed, <t>nothing</t> is returned.", "if (a>b) then {c=1}", "", "1.85", "", Category) \
  XX(GameAny, "then", function, ArrayThen, GameIf, GameArray, "if", "else", "The first or second element of the array is executed depending on the result of the if condition. The result of the executed expression is returned as a result (the result may be <t>nothing</t>).", "if (a>b) then {c=1} else {c=2};if (a>b) then [{c=1},{c=2}]", "", "1.85", "", Category) \
  XX(GameArray, "else", functionFirst, StringElse, GameCode, GameCode, "ifCode", "elseCode", "Constructs an array that can be processed by <f>then</f>.", "if (a>b) then {c=0} else {c=1}", "", "1.85", "", Category) \
  XX(GameNothing, "do", function, StringRepeat, GameWhile, GameCode, "while", "code", "Repeats the code while the condition is true. Note: the maximum repeat count for the loop is 10000. If the condition is still true after the loop was repeated 10000 times, the loop will be terminated and an error message is shown.", "while \"a>b\" do {a=a+1}", "", "1.85", "", Category) \
  XX(GameNothing, "do", function, NamespaceDo, GameWith, GameCode, "with", "code", "Execute the code inside the given namespace.", "with missionNamespace do {global=global+1}", "", "5501", "", Category) \
  XX(GameAny, "exitWith", function, exitWith, GameIf, GameCode, "if", "code", "if result of condition is true, evaluates code, and current block with result of code", "if (_x>5) exitWith {echo \"_x is too big\";_x}", "[when _x is greater then 5, outputs message and terminates code in current level with value of _x", "2", "", Category) \
  XX(/*returntype*/GameAny, /*name*/"call", /*priority*/function, /*C++ function*/StringCall, \
  /*lopertype*/GameVoid, /*ropertype*/GameCode, /*loper*/"pars", /*roper*/"body", \
  /*description*/"Executes the function body. Argument pars is passed as _this.", \
  /*example*/"[1,2] call {(_this select 0)+(_this select 1)}", /*exResult*/"3", \
  /*since*/"1.85", /*changed*/"", Category)\
  XX(GameFor, "from",function, forFrom, GameFor,GameScalar, "for \"_var\"","b", "Continue sequence of 'for' command.", "for \"_x\" from 10 to 20 do {..code..}", "", "", "", Category) \
  XX(GameFor, "to",function, forTo, GameFor,GameScalar, "for \"_var\" from a","b", "Continue sequence of 'for' command.", "for \"_x\" from 10 to 20 do {..code..}", "", "", "", Category) \
  XX(GameFor, "step",function, forStep, GameFor,GameScalar, "for /.../","step", "Optionaly can set step. If you want to count down, step must be specified, and set negative. Default value is 1.", "for \"_x\" from 20 to 10 step -2 do {..code..}", "", "", "", Category) \
  XX(GameAny, "do",function, forDo, GameFor,GameCode, "forCommand","code", "End of for command, starts cycle", "for \"_x\" from 20 to 10 step -2 do {..code..}", "", "", "", Category) \
  XX(GameNothing,":", function, switchColon, GameSwitch, GameCode,"a","b","see switch","","","","",Category) \
  XX(GameAny, "do",function, switchFormDo, GameSwitch, GameCode, "switch","block" , "Switch form","switch (_a) do { case 1: {block}; case 2 : {block}; default {block};}","","","",Category) \
  XX(GameAny, "catch",function, catchCommand, GameException, GameCode, "try-Block","code", "processes code, when exception is thrown in <f>try</f> block","","","","",Category) \
  XX(GameScalar, "min", soucet, ValMin, GameScalar, GameScalar, "a", "b", "The smaller of a,b", "3 min 2", "2", "", "", Category) \
  XX(GameScalar, "max", soucet, ValMax, GameScalar, GameScalar, "a", "b", "The greater of a,b", "3 max 2", "3", "", "", Category) \
  XX(GameNothing, "setVariable", function, NamespaceSetVariable, GameNamespace, GameArray, "namespace", "[name, value]", "Set variable to given value in the given namespace.", "", "", "5501", "", Category) \
  XX(GameVoid, "getVariable", function, NamespaceGetVariable, GameNamespace, GameString, "namespace", "name", "Return the value of variable in the given namespace.", "", "", "5501", "", Category) \
  XX(GameVoid, "getVariable", function, NamespaceGetVariable, GameNamespace, GameArray, "namespace", "[name, default value]", "Return the value of variable in the given namespace. If not found, default value is returned.", "", "", "80915 TOH", "", Category) \

OPERATORS_DEFAULT(JNI_OPERATOR, "Default")

  static GameOperator DefaultBinary[]=
{
  OPERATORS_DEFAULT(REGISTER_OPERATOR, "Default")
};

#define FUNCTIONS_DEFAULT(XX, Category) \
  XX(GameScalar, "sin", Sinus, GameScalar, "x", "The sine of x, the argument is in degrees.", "sin 30", "0.5", "", "", Category) \
  XX(GameScalar, "random", Random, GameScalar, "x", "The random real value from 0 to x. (0&lt;=random&lt;1)", "random 1", "", "", "", Category) \
  XX(GameScalar, "cos", Cosinus, GameScalar, "x", "The cosine of x, the argument is in degrees.", "cos 60", "0.5", "", "", Category) \
  XX(GameScalar, "tg", Tangens, GameScalar, "x", "The tangens of x, the argument is in degrees.", "tg 45", "1", "", "", Category) \
  XX(GameScalar, "tan", Tangens2, GameScalar, "x", "The tangens of x, the argument is in degrees.", "tan 45", "1", "", "", Category) \
  XX(GameScalar, "asin", ASinus, GameScalar, "x", "The arcsine of x, the result is in degrees.", "asin 0.5", "30", "", "", Category) \
  XX(GameScalar, "acos", ACosinus, GameScalar, "x", "The arccosine of x, the result is in degrees.", "acos 0.5", "60", "", "", Category) \
  XX(GameScalar, "atg", ATangens, GameScalar, "x", "The arctangens of x, the result is in degrees.", "atg 1", "45", "", "", Category) \
  XX(GameScalar, "atan", ATangens2, GameScalar, "x", "The arctangens of x, the result is in degrees.", "atan 1", "45", "", "", Category) \
  XX(GameScalar, "deg", Radian, GameScalar, "x", "Converts x from radians to degrees.", "deg 1", "57.295", "", "", Category) \
  XX(GameScalar, "rad", Stupne, GameScalar, "x", "Converts x from degrees to radians.", "rad 180", "3.1415", "", "", Category) \
  XX(GameScalar, "log", FLog, GameScalar, "x", "The base-10 logarithm of x.", "log 10", "1", "", "", Category) \
  XX(GameScalar, "ln", FLn, GameScalar, "x", "The natural logarithm of x.", "ln 10", "2.302", "", "", Category) \
  XX(GameScalar, "exp", Exp, GameScalar, "x", "The exponential value of x.", "exp 1", "2.7182", "", "", Category) \
  XX(GameScalar, "sqrt", Odmocnina, GameScalar, "x", "The square root of x.", "sqrt 9", "3", "", "", Category) \
  XX(GameScalar, "abs", Abs, GameScalar, "x", "The absolute value of x.", "abs -3", "3", "", "", Category) \
  XX(GameScalar, "floor", Floor, GameScalar, "x", "The floor value of x.", "floor 5.25 ", "5", "", "", Category) \
  XX(GameScalar, "ceil", Ceil, GameScalar, "x", "The ceil value of x.", "ceil 5.25", "6", "", "", Category) \
  XX(GameScalar, "round", Round, GameScalar, "x", "The round value of x.", "round -5.25", "-5", "", "", Category) \
  XX(GameBool, "finite", Finite, GameScalar, "x", "True, if number is finite (not infinite and valid number)", "finite 10/0", "false", "", "", Category) \
  XX(GameScalar, "+", Plus, GameScalar, "a", "unary plus: returns a", "+4", "4", "", "", Category) \
  XX(GameArray, "+", Plus, GameArray, "a", "unary plus: returns a copy of the array", "+ [0, 1, 2]", "[0, 1, 2]", "", "", Category) \
  XX(GameScalar, "-", Minus, GameScalar, "a", "unary minus: zero minus a", "- - 3", "3", "", "", Category) \
  XX(GameBool, "!", BoolNot, GameBool, "a", "not a", "!true", "false", "", "", Category) \
  XX(GameBool, "not", BoolNot2, GameBool, "a", "not a", "not false", "true", "", "", Category) \
  XX(GameScalar, "count", ListCount, GameArray, "array", "The number of elements in the array.", "count [0,0,1,2]", "4", "", "", Category) \
  XX(GameAny, "call", StringCallNoArg, GameCode, "code", "Executes the given code.", "call {\"x=2\"}", "", "1.85", "", Category) \
  XX(GameNothing, "comment", StringIgnore, GameString, "comment", "This function does nothing. It's used to create comments.", "comment \"This is a comment.\"", "", "1.85", "", Category) \
  XX(GameNothing, "private", StringLocal, GameStringOrArray, "variable", "Introduces one or more local variables in the innermost scope.", "", "", "1.85", "", Category) \
  XX(GameIf, "if", IfBool, GameBool, "condition", "The first part of the if command.", "if (a>b) then {a=b}", "", "1.85", "", Category) \
  XX(GameWhile, "while", WhileString, GameCode, "condition", "The first part of the while contruct.", "while \"x<10\" do {x=x+1}", "", "1.85", "", Category)\
  XX(GameWith, "with", NamespaceWith, GameNamespace, "namespace", "The first part of the with contruct.", "with missionNamespace do {global=global+1}", "", "5501", "", Category)\
  XX(GameString, "typeName", typeOf, GameAny, "any", "Returns type-name of expression. Type is returned as string","typeName \"hello\"", "\"string\"", "2.00", "", Category) \
  XX(GameBool, "assert", DebuggerAssert, GameBool, "codition", "Tests a condition and if the condition is false, halts the program.","assert (_x>0)", "", "2.00", "", Category)\
  XX(GameNothing, "echo", DebuggerEcho, GameString, "text", "Sends any text into the debugger console or the logfile.","echo \"Text in logfile\"", "", "2.00", "", Category)\
  XX(GameString, "str", AsString, GameAny, "any value", "Converts any variable to a string.","str(2+3)", "\"5\"", "2.00", "", Category) \
  XX(/*returntype*/GameBool, /*name*/"isNil", /*C++ function*/IsVarNull, \
  /*ropertype*/GameCodeOrString, /*roper*/"variable", \
  /*description*/"Tests whether the variable is null. The function returns true if the variable is null and false if it's not.", \
  /*example*/"if (isNil(\"_pokus\")) then {_pokus=0;}", /*exResult*/"", \
  /*since*/"2.00", /*changed*/"", Category) \
  XX(/*returntype*/GameArray, /*name*/"supportInfo", /*C++ function*/supportInfo, \
  /*ropertype*/GameString, /*roper*/"mask", \
  /*description*/"Creates list of supported operators and type. Each field of array has format: \"x:name\" where x is 't' - type, 'n' - nullary operator, 'u' - unary operator, 'b' - binary operator. 'name' is operator's/type's name (in case operator, type of input operands is included). `mask` parameter can be empty string, or one of field. In this case, function returns empty array, if operator is not included in list. `mask` can contain wildcards, for example: *:name, t:*, t:name* or *:*.", \
  /*example*/"supportInfo \"b:select*\"", /*exResult*/"[\"b:ARRAY select SCALAR\",\"b:ARRAY select BOOL\"]", \
  /*since*/"2.00", /*changed*/"", Category) \
  XX(GameFor, "for", createForBasic, GameString, "var", "Starts for sequence, use in complette form(see example).", "for \"_x\" from 1 to 10 do {debugLog _x;}", "", "", "", Category) \
  XX(GameFor, "for", createForC, GameArray, "forspec", "creates cycle, using C like style. See example.", "for [{_x=1},{_x<=10},{_x=_x+1}] do {debugLog _x;}", "", "", "", Category) \
  XX(GameSwitch, "switch",switchForm,GameAny, "exp" , "Begins switch form","switch (_a) do { case 1: {block}; case 2 : {block}; default {block};}","","","",Category) \
  XX(GameSwitch, "case",  switchCase, GameAny,"b","see switch","","","","",Category) \
  XX(GameNothing,"default",  switchDefault, GameCode,"a","see switch","","","","",Category) \
  XX(GameNothing,"scopeName",  ScopeName, GameString,"name","defines name of current scope. Name is visible in debugger, and name is also used as reference in some commands. Scope name can be defined only once per scope.","","","","",Category) \
  XX(GameNothing,"breakTo",  BreakTo, GameString,"name","Breaks block to scope named 'name'. Nil is returned.","","","","",Category) \
  XX(GameNothing,"breakOut",  BreakOut, GameString,"name","Breaks block out scope named 'name'. Nil is returned.","","","","",Category) \
  XX(GameException,"try",  tryCommand, GameCode,"code","Defines try-catch structure. This is structured exception block. Any thrown exception in try block is caught in catch block. The structured exception block has following form"\
  "<pre>try   //begin of try-catch block\n{\n   //block, that can throw exception\n}\ncatch{\n   //block, that process an exception. Exception is described in _exception variable\n}</pre>;\n"\
  ,"","","","",Category) \
  XX(GameNothing,"throw",  throwCommand, GameAny ,"expression","Throws an exception. The exception is processed by first catch block. See <f>try</f>.","throw \"invalid argument\"","","","",Category) \
  XX(GameCode, "compile", StringCompile, GameString, "expression", "Compile expression.", "_function = \"a = a + 1\"; _compiled = compile _function; call _compiled;", "", "2.60", "", Category) \
  XX(GameNothing, "waitUntil", VMWaitUntil, GameCode, "condition", "Suspend execution of script until condition is satisfied.", "_i = 0; waitUntil {_i = _i + 1; _i >= 100}", "", "2.60", "", Category) \
  XX(GameScalar, "parseNumber", ParseNumber, GameString, "string", "Parse string containing real number.", "parseNumber \"0.125\"", "", "2.92", "", Category) \
  XX(GameString, "toUpper", StrToUpper, GameString, "string", "Convert the string to upper case.", "toUpper \"ArmA\"", "\"ARMA\"", "5195", "", Category) \
  XX(GameString, "toLower", StrToLower, GameString, "string", "Convert the string to lower case.", "toLower \"ArmA\"", "\"arma\"", "5195", "", Category) \
  XX(GameArray, "toArray", StrToArray, GameString, "string", "Convert the string to the array of characters.", "toArray \"ArmA\"", "[65, 114, 109, 65]", "5195", "", Category) \
  XX(GameString, "toString", StrToString, GameArray, "characters", "Convert the array of characters to the string.", "toString [65, 114, 109, 65]", "\"ArmA\"", "5195", "", Category) \
  XX(GameNothing, "scriptName", SetScriptName, GameString, "name", "Assign a friendly name to the VM script thjis command is executed from.", "scriptName \"Trading\"", "", "5501", "", Category) \

#define FUNCTIONS_VBS(XX, Category) \

FUNCTIONS_DEFAULT(JNI_FUNCTION, "Default")
#if _VBS3
  FUNCTIONS_VBS(JNI_FUNCTION, "VBS")
#endif

static GameFunction DefaultUnary[]=
{
  FUNCTIONS_DEFAULT(REGISTER_FUNCTION, "Default")
#if _VBS3
  FUNCTIONS_VBS(REGISTER_FUNCTION, "VBS")
#endif
};

#define NULARS_DEFAULT(XX, Category) \
  XX(GameVoid, "nil", VoidNil, "Nil value. This value can be used to undefine an existing variable.", "variableToDestroy = nil", "", "", "", Category) \
  XX(GameBool, "true", BoolTrue, "Always true.", "", "", "", "", Category) \
  XX(GameBool, "false", BoolFalse, "Always false.", "", "", "", "", Category) \
  XX(GameScalar, "pi", ScalarPi, "pi (180 degrees converted to radians)", "pi", "3.1415", "", "", Category) \
  XX(GameNothing, "disableSerialization", DisableSerialization, "Disable saving of script containing this command. After this, script can work with the data types which do not support serialization (UI types).", "", "", "5501", "", Category) \
  XX(/*returntype*/GameNothing, /*name*/"halt", /*C++ function*/BreakScriptIntoDebugger, \
  /*description*/"Stops the program into a debugger.", \
  /*example*/"halt", /*exResult*/"", \
  /*since*/"2.00", /*changed*/"", Category)

NULARS_DEFAULT(JNI_NULAR, "Default")

static GameNular DefaultNular[]=
{
  NULARS_DEFAULT(REGISTER_NULAR, "Default")
};

inline int CMP( const char *Ps, const char *Ps1 )
{
  return strnicmp(Ps,Ps1,strlen(Ps1));
}

inline int CMPN( const char *Ps, const char *Ps1, int n )
{
  return strnicmp(Ps,Ps1,n);
}

GameValue::GameValue()
{
  _data=NULL; //new GameDataNothing();
}

GameValue::GameValue( GameData *data )
{
  _data=data;
}

GameValue::GameValue( GameBoolType value ){_data=new GameDataBool(value);}
GameValue::GameValue( GameScalarType value ){_data=new GameDataScalar(value);}
GameValue::GameValue( const GameArrayType &value ){_data=new GameDataArray(value);}
GameValue::GameValue( GameStringType value ){_data=new GameDataString(value);}
GameValue::GameValue( const char *value ){_data=new GameDataString(value);}

void GameValue::operator = ( bool value ){_data=new GameDataBool(value);}
void GameValue::operator = ( float value ){_data=new GameDataScalar(value);}
void GameValue::operator = ( const GameArrayType &value ){_data=new GameDataArray(value);}
void GameValue::operator = ( GameStringType value ){_data=new GameDataString(value);}
void GameValue::operator = ( const char *value ){_data=new GameDataString(value);}

const GameType &GameValue::GetType() const {return _data ? _data->GetType() : GameAny;}

LSError CompoundGameType::Serialize(ParamArchive &ar)
{
  if (ar.IsSaving())
  {
    // save the type as an array of type names
    AutoArray<RString, MemAllocLocal<RString, 16> > typeNames;
    for (int i=0; i<Size(); i++)
    {
      const GameTypeType *typeType = Get(i);
      typeNames.Add(typeType ? typeType->_name : RString()); // handle GameAny value
    }
    CHECK(ar.SerializeArray("type", typeNames, 1));
  }
  else if (ar.GetPass() == ParamArchive::PassFirst)
  {
    Clear();
    // load the type as an array of type names
    AutoArray<RString, MemAllocLocal<RString, 16> > typeNames;
    CHECK(ar.SerializeArray("type", typeNames, 1));
    // construct the type
    GameState *state = reinterpret_cast<GameState *>(ar.GetParams());
    for (int i=0; i<typeNames.Size(); i++)
    {
      if (typeNames[i].GetLength() == 0)
      {
        // empty string means GameAny
        *this = CompoundGameType(&GTypeGameAny);
        break; // will not be better
      }
      const GameTypeType *typeType = state->FindType(typeNames[i]);
      if (!typeType)
      {
        RptF("Unknown variable type %s", cc_cast(typeNames[i]));
        continue;
      }
      *this |= CompoundGameType(typeType); // TODO: optimize - avoid GameType temporary
    }
  }
  return LSOK;
}

LSError GameType::Serialize(ParamArchive &ar)
{
  if (ar.IsSaving())
  {
    if (_multipleTypes)
    {
      return _multipleTypes->Serialize(ar);
    }
    else
    {
      // handles GameAny as well - one item containing NULL
      CompoundGameType temp(_singleType);
      return temp.Serialize(ar);
    }
  }
  else
  {
    // load as multiple types
    _singleType = NULL;
    _multipleTypes = new CompoundGameType();
    LSError ok = _multipleTypes->Serialize(ar);
    if (ok==LSNoEntry)
    { // LSNoEntry is handling a special case - empty string
      _multipleTypes = NULL;
    }
    // if needed, canonicalize
    Canonicalize();
    return ok;
  }
}

void GameType::Canonicalize()
{
  if (_multipleTypes)
  {
    Assert(_singleType==NULL);
    if (_multipleTypes->Size()==1)
    {
      _singleType = _multipleTypes->Get(0);
      _multipleTypes = NULL;
    }
    else if (_multipleTypes->Size()==0)
    {
      _singleType = NULL;
      _multipleTypes = NULL;
    }
  }
}
GameData *GameData::CreateObject(ParamArchive &ar)
{
  bool nil = false;
  if (ar.Serialize("nil", nil, 1, false) != 0)
  {
    return NULL;
  }

  GameType type;
  if (ar.Serialize("type", type, 1) != 0) return NULL;
  
  if (nil)
  {
    // create nil object
    return new GameDataNil(type);
  }

  GameState *context = reinterpret_cast<GameState *>(ar.GetParams());
  return context->CreateGameData(type, &ar);
}

LSError GameData::Serialize(ParamArchive &ar)
{ 
  if (ar.IsSaving())
  {
    GameType type = GetType();
    CHECK(ar.Serialize("type", type, 1));
  }
  else
  {
    // verify the type we are loading matches
#   if 1
    GameState *gs = (GameState *)ar.GetParams();

    GameType valueType = GetType();
    GameType type;
    // the verification needs to be done in both passes
    if (ar.GetPass() == ParamArchive::PassSecond)
    {
      // integer variables are normally loaded in 1st pass only
      ar.FirstPass();
      ar.Serialize("type", type, 1);
      ar.SecondPass();
    }
    else
    {
      ar.Serialize("type", type, 1);
    }

    if (type != valueType)
    {
      ar.OnError(LSStructure, RString());
      RptF(
        "%s: Value type not matching: %s != %s",
        cc_cast(ar.GetErrorContext()),
        cc_cast(gs->GetTypeName(type)), cc_cast(gs->GetTypeName(valueType))
        );
      return LSStructure;
    }
#   endif
  }
  return (LSError)0;
}

int GameData::IAddRef()
{
  return AddRef();
}

int GameData::IRelease()
{
  return Release();
}

void GameData::GetValueType(char *buffer, int len) const
{
  strncpy(buffer, GGameState.GetTypeName(GetType()), len);
  buffer[len - 1] = 0;
}

void GameData::GetValueValue(unsigned int radix, char *buffer, int len) const
{
  strncpy(buffer, GetText(), len);
  buffer[len - 1] = 0;
}

bool GameData::IsExpandable() const
{
  return false;
}

int GameData::NItems() const
{
  return 0;
}

IDebugValueRef GameData::GetItem(int i) const
{
  return NULL;
}

RString GameDataNil::GetText() const
{
  // print all types included
  // TODO: how to get symbolic names?
  if (_type == GameAny)
  {
    return "any";
  }
  char buf[MAX_EXPR_LEN];
  strcpy(buf, "");
  for (int i=0; i<_type.Size(); i++)
  {
    if (*buf) strcat(buf, " "); // space between words

    if (_type[i] == GameScalar[0]) strcat(buf, "scalar");
    else if (_type[i] == GameBool[0]) strcat(buf, "bool");
    else if (_type[i] == GameArray[0]) strcat(buf, "array");
    else if (_type[i] == GameString[0]) strcat(buf, "string");
    else if (_type[i] == GameNothing[0]) strcat(buf, "nothing");
    else sprintf(buf + strlen(buf), "0x%x", _type[i]);
  }
  return buf;
}

bool GameDataNothing::IsEqualTo(const GameData *data) const
{
  return false; // nil is never equal to anything
  //static_cast<GameDataNil *>(data);
}

bool GameDataNil::IsEqualTo(const GameData *data) const
{
  return false; // nil is never equal to anything
  //static_cast<GameDataNil *>(data);
}

#ifndef ACCESS_ONLY
LSError GameDataNil::Serialize(ParamArchive &ar)
{
  bool nil = true;
  CHECK(ar.Serialize("nil", nil, 1));
  // nothing but type is serialized
  CHECK(ar.Serialize("type", _type, 1));
  return (LSError)0;
}
#endif

jweak GameDataScalar::_class = NULL;
jmethodID GameDataScalar::_constructorID = NULL;

jclass GameDataScalar::InitClass(JNIEnv *env)
{
  // activate JVM FPU settings
  CHECK_FPU_JVM

  // check the cached class
  jclass result = (jclass)env->NewLocalRef(_class);
  if (result == NULL) 
  {
    // class is not valid, initialize and store
    result = env->FindClass("java/lang/Float");
    _class = env->NewWeakGlobalRef(result);
    _constructorID = env->GetMethodID(result, "<init>", "(F)V");
  }
  return result;
}

jobject GameDataScalar::ToJObject(JNIEnv *env, GameValuePar from) const
{
  // activate JVM FPU settings
  SCOPE_FPU_JVM

  jclass cls = InitClass(env);
  return env->NewObject(cls, _constructorID, _value);
}

RString GameDataScalar::GetText() const
{
  BString<MAX_EXPR_LEN> buf;
  sprintf(buf,"%g",_value);
  return (const char *)buf;
}

bool GameDataScalar::IsEqualTo(const GameData *data) const
{
  float val1 = GetScalar();
  float val2 = data->GetScalar();
  return val1==val2;
}


#ifndef ACCESS_ONLY
LSError GameDataScalar::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar));
  CHECK(ar.Serialize("value", _value, 1));
  return (LSError)0;
}
#endif


jweak GameDataBool::_class = NULL;
jmethodID GameDataBool::_constructorID = NULL;

jclass GameDataBool::InitClass(JNIEnv *env)
{
  // activate JVM FPU settings
  CHECK_FPU_JVM

  // check the cached class
  jclass result = (jclass)env->NewLocalRef(_class);
  if (result == NULL) 
  {
    // class is not valid, initialize and store
    result = env->FindClass("java/lang/Boolean");
    _class = env->NewWeakGlobalRef(result);
    _constructorID = env->GetMethodID(result, "<init>", "(Z)V");
  }
  return result;
}

jobject GameDataBool::ToJObject(JNIEnv *env, GameValuePar from) const
{
  // activate JVM FPU settings
  SCOPE_FPU_JVM

  jclass cls = InitClass(env);
  return env->NewObject(cls, _constructorID, _value);
}

RString GameDataBool::GetText() const
{
  return _value ? "true" : "false";
}

bool GameDataBool::IsEqualTo(const GameData *data) const
{
  bool val1 = GetBool();
  bool val2 = data->GetBool();
  return val1==val2;
}


#ifndef ACCESS_ONLY
LSError GameDataBool::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar));
  CHECK(ar.Serialize("value", _value, 1));
  return (LSError)0;
}
#endif

/*!
\patch_internal 1.01 Date 7/10/2001 by Jirka
- removed text length limit when displaying variable value (internal build only)
*/
RString GameDataString::GetText() const
{
  return RString("\"") + _value + RString("\"");
}

bool GameDataString::IsEqualTo(const GameData *data) const
{
  RString val1 = GetString();
  RString val2 = data->GetString();
  return val1==val2;
}

jobject GameDataString::ToJObject(JNIEnv *env, GameValuePar from) const
{
  return ConvertTypes<&GTypeGameString>::FromGameValue(env, from);
}

#ifndef ACCESS_ONLY
LSError GameDataString::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar));
  CHECK(ar.Serialize("value", _value, 1));
  return (LSError)0;
}
#endif

// GameDataWhile

bool GameDataWhile::IsEqualTo(const GameData *data) const
{
  const GameDataWhile *dataWhile = static_cast<const GameDataWhile *>(data);
  if (dataWhile->GetCode() == GetCode()) return true;
  if (!GetCode() || !dataWhile->GetCode()) return false;
  if (GetCode()->GetType() != dataWhile->GetCode()->GetType()) return false;
  return GetCode()->IsEqualTo(dataWhile->GetCode());
}

jobject GameDataWhile::ToJObject(JNIEnv *env, GameValuePar from) const
{
  // unsupported type
  return GameValueToNativeObject(env, from);
}

#ifndef ACCESS_ONLY
LSError GameDataWhile::Serialize(ParamArchive &ar)
{
  // during serialization _data must point to valid object 
  if (!_code) _code = new GameDataNothing;
  CHECK(ar.Serialize("code", _code, 1));
  if (_code && _code->GetType() == GameNothing) _code = NULL;
  return (LSError)0;
}
#endif

// GameDataWith

bool GameDataWith::IsEqualTo(const GameData *data) const
{
  const GameDataWith *dataWith = static_cast<const GameDataWith *>(data);
  if (dataWith->GetNamespace() == GetNamespace()) return true;
  if (!GetNamespace() || !dataWith->GetNamespace()) return false;
  if (GetNamespace()->GetType() != dataWith->GetNamespace()->GetType()) return false;
  return GetNamespace()->IsEqualTo(dataWith->GetNamespace());
}

jobject GameDataWith::ToJObject(JNIEnv *env, GameValuePar from) const
{
  // unsupported type
  return GameValueToNativeObject(env, from);
}

#ifndef ACCESS_ONLY
LSError GameDataWith::Serialize(ParamArchive &ar)
{
  // only reference need to be serialized (not the contained variables)
  CHECK(ar.Serialize("namespace", _namespace, 1));
  return LSOK;
}
#endif

jobject GameDataIf::ToJObject(JNIEnv *env, GameValuePar from) const
{
  // unsupported type
  return GameValueToNativeObject(env, from);
}

jobject GameDataForClass::ToJObject(JNIEnv *env, GameValuePar from) const
{
  // unsupported type
  return GameValueToNativeObject(env, from);
}

jobject GameDataException::ToJObject(JNIEnv *env, GameValuePar from) const
{
  // unsupported type
  return GameValueToNativeObject(env, from);
}

// GameDataArray

const GameArrayType GameArrayEmpty;
GameArrayType GameArrayDummy;

jweak GameDataArray::_class = NULL;
jmethodID GameDataArray::_constructorID = NULL;
jmethodID GameDataArray::_addID = NULL;

GameDataArray::GameDataArray()
:_readOnly(false)
{
}

GameDataArray::GameDataArray(const GameArrayType &value)
:_value(value),_readOnly(false)
{
}

GameDataArray::~GameDataArray()
{
}

GameData *GameDataArray::Clone() const
{
  GameDataArray *clone = new GameDataArray();
  GameArrayType &array = clone->GetArray();

  int n = _value.Size();
  array.Realloc(n);
  array.Resize(n);

  for (int i=0; i<n; i++)
  {
    array[i].Duplicate(_value[i]);
  }

  return clone;
}

jclass GameDataArray::InitClass(JNIEnv *env)
{
  // activate JVM FPU settings
  CHECK_FPU_JVM

  // check the cached class
  jclass result = (jclass)env->NewLocalRef(_class);
  if (result == NULL) 
  {
    // class is not valid, initialize and store
    result = env->FindClass("java/util/ArrayList");
    _class = env->NewWeakGlobalRef(result);
    _constructorID = env->GetMethodID(result, "<init>", "(I)V");
    _addID = env->GetMethodID(result, "add", "(Ljava/lang/Object;)Z");
  }
  return result;
}

jobject GameDataArray::ToJObject(JNIEnv *env, GameValuePar from) const
{
  // activate JVM FPU settings
  SCOPE_FPU_JVM

  jclass cls = InitClass(env);

  // create Java object
  int size = _value.Size();
  jobject list = env->NewObject(cls, _constructorID, size);

  // fill Java objects with the array items
  for (int i=0; i<size; i++)
  {
    jobject item = _value[i].ToJObject(env);
    env->CallBooleanMethod(list, _addID, item);
  }

  return list;
}

/*!
\patch_internal 1.01 Date 7/10/2001 by Jirka
- removed text length limit when displaying variable value (internal build only)
*/
RString GameDataArray::GetText() const
{
  RString val("[");
  for (int i=0; i<_value.Size(); i++)
  {
    if (i > 0) val = val + RString(",");
    const GameValue &value = _value[i];
    if (value.GetType()==GameArray)
    {
      const GameArrayType &array = value;
      if (&array==&_value)
      {
        // prevent simple recursion
        // note: same situation may happen via an intermediate array
        // but this is almost impossible to catch
        val = val + RString("...");
        continue;
      }
    }
    val = val + _value[i].GetText();
  }
  val = val + RString("]");
  return val;
}

bool GameDataArray::IsEqualTo(const GameData *data) const
{
  const GameArrayType &val1 = GetArray();
  const GameArrayType &val2 = data->GetArray();
  return val1==val2;
}


#ifndef ACCESS_ONLY
LSError GameDataArray::Serialize(ParamArchive &ar)
{
  CHECK(base::Serialize(ar));
  CHECK(ar.Serialize("value", _value, 1));
  return (LSError)0;
}
#endif

// IDebugValue
bool GameDataArray::IsExpandable() const
{
  return true;
}

int GameDataArray::NItems() const
{
  return _value.Size();
}

IDebugValueRef GameDataArray::GetItem(int i) const
{
  return _value[i].GetData();
}

// GameDataNamespace

GameDataNamespace::GameDataNamespace(GameState *state, RString name, bool enableSerialization, bool addRef)
: _enabledSerialization(enableSerialization)
{
  _name = name;
  if (state && name.GetLength() > 0) state->RegisterStaticNamespace(this);
  // keep static instance locked even when not (yet) registered
  if (addRef) AddRef();
}

void GameDataNamespace::Reset()
{
  _variables.Clear();
}

bool GameDataNamespace::VarSet(const char *name, GameValuePar value, bool readOnly)
{
  Assert(RString::IsLowCase(name));

  if (!value.SupportSerialization() && IsSerializationEnabled())
  {
    WarningMessage("Variable '%s' does not support serialization and should not be stored in the mission namespace.",
      cc_cast(name));
  }

  // check if variable is already present
  GameVariable &var = _variables[name];
  if (NotNull(var))
  {
    // set variable value
    var._value = value;
    var._readOnly = readOnly;
    return false;
  }

  // new variable
  _variables.Add(GameVariable(name, value, readOnly));
  return true;
}

bool GameDataNamespace::VarGet(const char *name, GameValue &ret) const
{
  Assert(RString::IsLowCase(name));

  const GameVariable &var =_variables[name];
  if (NotNull(var))
  {
    ret = var._value;
    return true;
  }

  ret = GameValueNil;
  return false;
}

RString GameDataNamespace::GetText() const
{
  return "Namespace";
}

bool GameDataNamespace::IsEqualTo(const GameData *data) const
{
  return false;
}

GameData *GameDataNamespace::Clone() const
{
  // not implemented
  return NULL;
}

jobject GameDataNamespace::ToJObject(JNIEnv *env, GameValuePar from) const
{
  return ConvertTypes<&GTypeGameNamespace>::FromGameValue(env, from);
}

#ifndef ACCESS_ONLY
LSError GameDataNamespace::Serialize(ParamArchive &ar)
{
  // when GameValue is serialized, only reference need to be stored (contained variables are serialized from their primary location)
  DoAssert(ar.GetParams()); // should be GameState
  DoAssert(_enabledSerialization);
  CHECK(GameData::Serialize(ar));
  if (ar.IsSaving())
  {
    CHECK(ar.Serialize("name", _name, 1)) // used in CreateObject during loading
  }
  return LSOK;
}
#endif

GameValue::GameValue( const GameValue &value )
{
  _data=value._data;
}

void GameValue::operator = ( const GameValue &value )
{
  _data=value._data;
}

void GameValue::Duplicate( const GameValue &value )
{
  if( !value._data )
  {
    _data=NULL;
    return;
  }
  _data=value._data->Clone();
  if( !_data )
  {
    ErrF("Data not cloned: %s",(const char *)value.GetDebugText());
    _data=new GameDataNil(GameAny);
  }
}

#ifndef ACCESS_ONLY
LSError GameValue::Serialize(ParamArchive &ar)
{
  // during serialization _data must point to valid object 
  if (!_data) _data = new GameDataNothing;
  CHECK(ar.Serialize("data", _data, 1));
  if (_data && _data->GetType() == GameNothing) _data=NULL;
  return (LSError)0;
}
#endif

RString GameValue::GetText() const
{
  if( !_data ) return "<null>";
  return _data->GetText();
}


RString GameValue::GetDebugText() const
{
  if( !_data ) return "<null>";
  RString type=_data->GetTypeName();
  RString text=_data->GetText();
  return type+RString(" ")+text;
}

bool GameValue::IsEqualTo(const GameValue &value) const
{
  // check type
  if (GetNil() || value.GetNil()) return false;
  if (!GetData() || !value.GetData()) return false;
  if (value.GetType()!=GetType()) return false;
  // if it is same type, call corresponding method
  return GetData()->IsEqualTo(value.GetData());
}

/*
bool GameData::IsEqualTo(const GameData &value)
{
return false;
}
*/

jclass CachedJavaClass::Init(JNIEnv *env)
{
  // activate JVM FPU settings
  CHECK_FPU_JVM

  // check the cached _class
  jclass result = (jclass)env->NewLocalRef(_class);
  if (result == NULL) 
  {
    // _class is not valid, initialize and store
    result = env->FindClass(_name);
    _class = env->NewWeakGlobalRef(result);
    _constructorID = env->GetMethodID(result, "<init>", "()V");
    _peerID = env->GetFieldID(result, "_peer", "J");
  }
  return result;
}

jobject CachedJavaClass::FromGameValue(JNIEnv *env, const GameValue &from)
{
  // activate JVM FPU settings
  SCOPE_FPU_JVM

  jclass cls = Init(env);

  // create Java object
  jobject obj = env->NewObject(cls, _constructorID);

  // store GameValue to heap and encode pointer
  GameValue *ptr = new GameValue(from);
  jlong peer = (jlong)ptr;

  // set the encoded pointer to the new Java object
  env->SetLongField(obj, _peerID, peer);

  return obj;
}

GameValue CachedJavaClass::ToGameValue(JNIEnv *env, jobject from)
{
  // activate JVM FPU settings
  SCOPE_FPU_JVM

  Init(env); // ensure the _peerID is valid

  // get the encoded pointer from the Java object
  jlong peer = env->GetLongField(from, _peerID);

  // decode the pointer
  GameValue *ptr = (GameValue *)peer;

  // return the value
  if (ptr != NULL) return *ptr;
  return GameValue(); // fall back
}

const char *GameState::GetExpressionText() const
{
#if USE_PRECOMPILATION
  if (!_context)
  {
    Fail("Missing evaluation context");
    return "";
  }
  return _context->_doc._content;
#else
  return _e ? _e->_pos0 : "";
#endif
}

void GameState::NewType(const GameTypeType *type)
{
  _typeNames.Add(type);
}

void GameState::NewNularOps(const GameNular *f, int count)
{
  for( int i=0; i<count; i++ )
  {
    NewNularOp(f[i]);
  }
}
void GameState::NewFunctions(const GameFunction *f, int count)
{
  for( int i=0; i<count; i++ )
  {
    NewFunction(f[i]);
  }
}
void GameState::NewOperators(const GameOperator *f, int count)
{
  for( int i=0; i<count; i++ )
  {
    NewOperator(f[i]);
  }
}

void GameState::NewNularOp(const GameNular &f)
{
  GameNular o = f;

  GameNular &nt = _nulars[f._name];
  if (_nulars.IsNull(nt))
  {
    _nulars.Add(o);
  }
}
void GameState::NewFunction(const GameFunction &f)
{
  GameFunction o = f;

  GameFunctions &ft = _functions[f._name];
  if (_functions.IsNull(ft))
  {
    _functions.Add(GameFunctions(f._opName));
  }
  GameFunctions &functions = _functions[f._name];
  o._name = functions._name;

  int n = functions.Size();
  functions.Realloc(n + 1);
  functions.Resize(n + 1);
  functions[n] = o;
}
void GameState::NewOperator(const GameOperator &op)
{
  GameOperator o = op;

  GameOperators &ot = _operators[op._name];
  if (_operators.IsNull(ot))
  {
    _operators.Add(GameOperators(op._opName, op._priority));
  }
  GameOperators &operators = _operators[op._name];
  o._name = operators._name;

  int n = operators.Size();
  operators.Realloc(n + 1);
  operators.Resize(n + 1);
  operators[n] = o;
}

#if _ENABLE_COMREF
// access to documentation
const GameNular *GameState::FindNular(RString name) const
{
  const GameNular &ret = _nulars[name];
  if (_nulars.IsNull(ret)) return NULL;
  return &ret;
}

const GameFunctions *GameState::FindFunctions(RString name) const
{
  const GameFunctions &ret = _functions[name];
  if (_functions.IsNull(ret)) return NULL;
  return &ret;
}

const GameOperators *GameState::FindOperators(RString name) const
{
  const GameOperators &ret = _operators[name];
  if (_operators.IsNull(ret)) return NULL;
  return &ret;
}
#endif

struct DicContext
{
  AutoArray<RStringS> &_dic;
  bool (*_filter)(const char *word);
  DicContext
    (
    AutoArray<RStringS> &dic,
    bool (*filter)(const char *word)
    )
    :_dic(dic),_filter(filter)
  {
  }
};

inline void AddDicFunc(RString name, DicContext &context)
{
  if (context._filter(name)) context._dic.Add(name);
}

static void AddDicFuncNul(const GameNular &op, const GameNularsType *list, void *context)
{
  AddDicFunc(op._opName,*(DicContext *)context);
}
static void AddDicFuncUn(const GameFunctions &op, const GameFunctionsType *list, void *context)
{
  AddDicFunc(op._opName,*(DicContext *)context);
}
static void AddDicFuncBin(const GameOperators &op, const GameOperatorsType *list, void *context)
{
  AddDicFunc(op._opName,*(DicContext *)context);
}

void GameState::AppendNularOpList
(
 AutoArray<RStringS> &dic, bool (*filter)(const char *word)
 ) const
{
  DicContext context(dic,filter);
  _nulars.ForEach(AddDicFuncNul,&context);
}
void GameState::AppendFunctionList
(
 AutoArray<RStringS> &dic, bool (*filter)(const char *word)
 ) const
{
  DicContext context(dic,filter);
  _functions.ForEach(AddDicFuncUn,&context);
}
void GameState::AppendOperatorList
(
 AutoArray<RStringS> &dic, bool (*filter)(const char *word)
 ) const
{
  DicContext context(dic,filter);
  _operators.ForEach(AddDicFuncBin,&context);
}

GameState::GameState()
{
#if !_SUPER_RELEASE
  _profilingEnabled = false;
#endif
  _showScriptErrors = false;
  _numBasicTypes = 0;

  Init();
  _exceptionHandler = NULL;
  _globals = NULL;
  _defaultGlobalsEnabled = true;
  BeginContext(NULL);
#ifdef _SCRIPT_DEBUGGER
  _debugger=NULL;
#endif
#if USE_PRECOMPILATION
  /// temporary storage for evaluation context - to enable access to context for Game functions without changing their declaration
  _context = NULL;
#endif
}

GameState::~GameState()
{
  _typeNames.Clear();
  _functions.Clear();
  _operators.Clear();
  _nulars.Clear();
}

#if USE_PRECOMPILATION

// All defined call stack item types
#define CALL_STACK_ITEMS_CORE(XX) \
  XX("simple", CallStackItemSimple) \
  XX("data", CallStackItemData) \
  XX("with", CallStackItemWith) \
  XX("repeat", CallStackItemRepeat) \
  XX("arrayCount", CallStackItemArrayCount) \
  XX("arrayForEach", CallStackItemArrayForEach) \
  XX("forBASIC", CallStackItemForBASIC) \
  XX("forC", CallStackItemForC) \
  XX("switch", CallStackItemSwitch) \
  XX("try", CallStackItemTry) \
  XX("exitWith", CallStackItemExitWith) \
  XX("waitUntil", CallStackItemWaitUntil) \
  XX("dataBool", CallStackItemDataBool) \
  XX("waitUntilNative", CallStackItemWaitUntilNative) \

CALL_STACK_ITEMS_CORE(DEFINE_CALL_STACK_ITEM)

static CallStackItemType DefaultCallStackItemTypes[] =
{
  CALL_STACK_ITEMS_CORE(REGISTER_CALL_STACK_ITEM)
};

#endif

#if DOCUMENT_COMREF
static ComRefFunc DefaultComRefFunc[] =
{
  NULARS_DEFAULT(COMREF_NULAR, "Default")
  FUNCTIONS_DEFAULT(COMREF_FUNCTION, "Default")
  FUNCTIONS_VBS(COMREF_FUNCTION, "VBS")
  OPERATORS_DEFAULT(COMREF_OPERATOR, "Default")
};

static ComRefType DefaultComRefType[] =
{
  TYPES_DEFAULT(COMREF_TYPE, "Default")
  TYPES_FAKE(COMREF_TYPE, "Default")
  TYPES_DEFAULT_COMB(COMREF_TYPE, "Default")
#if USE_PRECOMPILATION
  TYPES_CODE(COMREF_TYPE, "Default")
  TYPES_CODE_COMB(COMREF_TYPE, "Default")
#endif
};
#endif

void GameState::Init()
{
  _typeNames.Clear();

  GameState &state = *this; // helper to make macro works
  TYPES_DEFAULT(REGISTER_TYPE, "Default")
  TYPES_FAKE(REGISTER_TYPE, "Default")
#if USE_PRECOMPILATION
  TYPES_CODE(REGISTER_TYPE, "Default")
#endif
  _numBasicTypes = _typeNames.Size();

  _functions.Clear();
  for( int i=0; i<lenof(DefaultUnary); i++ )
  {
    NewFunction(DefaultUnary[i]);
  }
  _operators.Clear();
  for( int i=0; i<lenof(DefaultBinary); i++ )
  {
    NewOperator(DefaultBinary[i]);
  }
  _nulars.Clear();
  for( int i=0; i<lenof(DefaultNular); i++ )
  {
    NewNularOp(DefaultNular[i]);
  }

#if USE_PRECOMPILATION
  _callStackItems.Clear();
  for (int i=0; i<lenof(DefaultCallStackItemTypes); i++)
  {
    NewCallStackItemType(DefaultCallStackItemTypes[i]);
  }
#endif

#if DOCUMENT_COMREF
  _comRefFunc.Clear();
  for (int i=0; i<lenof(DefaultComRefFunc); i++)
  {
    AddComRefFunction(DefaultComRefFunc[i]);
  }
  _comRefType.Clear();
  for (int i=0; i<lenof(DefaultComRefType); i++)
  {
    AddComRefType(DefaultComRefType[i]);
  }
  _comRefArray.Clear();
#endif
#ifdef _SCRIPT_DEBUGGER
  _debugger=NULL;    
#endif
}

static bool CompactFunctions(GameFunctions &functions, GameFunctionsType *container)
{
  functions.Compact();
  return false;
}

static bool CompactOperators(GameOperators &operators, GameOperatorsType *container)
{
  operators.Compact();
  return false;
}

void GameState::Compact()
{
  _typeNames.Compact();
  _nulars.Compact();
  _functions.Compact();
  _operators.Compact();
#if !defined(_MSC_VER) || _MSC_VER>1300
  // ForEachF not working with VC 6
  _functions.ForEachF(CompactFunctions);
  _operators.ForEachF(CompactOperators);
#endif

#if USE_PRECOMPILATION
  _callStackItems.Compact();
#endif

#if DOCUMENT_COMREF
  _comRefType.Compact();
  _comRefArray.Compact();
  _comRefFunc.Compact();
#endif
}

void GameState::Reset()
{
  if (_defaultGlobalsEnabled) GDefaultNamespace.Reset();
}

GameDataNamespace *GameState::FindStaticNamespace(RString name)
{
  for (int i=0; i<_staticNamespaces.Size(); i++)
  {
    GameDataNamespace *ns = _staticNamespaces[i];
    if (stricmp(ns->GetName(), name) == 0) return ns;
  }
  return NULL;
}

GameValue GameState::VarGet(const char *name, bool errorWhenUndefined, const GameDataNamespace *globals, bool localsOnly) const
{
  // convert name to lower case
  RString source = name;
  source.Lower();

  // first try to find local variable
  const GameVarSpace *space = _e->local;
  if (space)
  {
    // get variable value
    GameValue ret;
    if (space->VarGet(source, ret)) return ret;
  }
  else
  {
    if (source[0] == '_')
    {
      SetError(EvalNamespace);
      return GameValueNil;
    }
  }

  if (!localsOnly)
  {
    if (!globals)
    {
      // check if default namespace is enabled
      if (_defaultGlobalsEnabled) globals = &GDefaultNamespace;
      else
      {
        SetError(EvalNamespace);
        return GameValueNil;
      }
    }
    // get variable value
    GameValue ret;
    if (globals->VarGet(source, ret)) return ret;
  }

  //LogF("Variable %s not found",name);
  return GameValueNil;
}

bool GameState::IsVisible( const GameVariable &var ) const
{
  return var.IsReadOnly();
}

/*!
\patch_internal 1.44 Date 2/11/2002 by Ondra
- Fixed: Identifiers leader, who, group, ammo and magazine
were supposed to be reserved with no reason.
*/

bool GameState::VarReadOnly( const char *name, const GameDataNamespace *globals) const
{
  if( _e->_checkOnly )
  {
    // check reserved word list
    const char *reserved[]=
    {
      "this","player",
      NULL
    };
    for( const char **res=reserved; *res; res++ )
    {
      if( !strcmp(*reserved,name) ) return true;
    }
    return false;
  }
  if (!globals)
  {
    // check if default namespace is enabled
    if (_defaultGlobalsEnabled) globals = &GDefaultNamespace;
    else
    {
      ErrF("Global namespace not passed (when accessing variable %s)", name);
      return false;
    }
  }

  const GameVariable &var = globals->GetVariable(name);
  if (globals->IsNull(var)) return false;
  return var._readOnly;
}

GameVarSpace::GameVarSpace(bool enableSerialization)
: _parent(NULL), _enabledSerialization(enableSerialization)
#if !USE_PRECOMPILATION
,_blockExit(false)
#endif
{
  Reset();
}

/*
\patch 1.82 Date 8/12/2002 by Ondra
- New: Scripting: Multiple level variable scopes
(new scope in each call, if, while, forEach and count expression).
*/

GameVarSpace::GameVarSpace(GameVarSpace *parent, bool enableSerialization)
: _parent(parent), _enabledSerialization(enableSerialization)
#if !USE_PRECOMPILATION
,_blockExit(false)
#endif
{
  Reset();
}

GameVarSpace::~GameVarSpace()
{
  _vars.Clear();
}

void GameVarSpace::Reset()
{
  _vars.Clear();
}

#ifndef ACCESS_ONLY
LSError GameVarSpace::Serialize(ParamArchive &ar)
{
  DoAssert(ar.GetParams()); // should be GameState
  DoAssert(_enabledSerialization);
  CHECK(ar.Serialize("Variables", _vars, 1))
  return (LSError)0;
}
#endif

void GameVarSpace::VarLocal( const char *name)
{
  const GameVariable &var=_vars[name];
  if (NotNull(var))
  {
    return;
  }

  GameVariable newvar(name, GameValueNil);
  _vars.Add(newvar);
}

bool GameVarSpace::VarSet( const char *name, GameValuePar value, bool readOnly)
{
  if (!value.SupportSerialization() && IsSerializationEnabled())
  {
    RString scriptName;
#if USE_PRECOMPILATION
    VMContext *ctx = GGameState.GetVMContext();
    if (ctx) scriptName = ctx->GetDisplayName();
#endif

    WarningMessage("Variable '%s' does not support serialization. Call 'disableSerialization' in the current script (maybe '%s') if you need to use it.",
      cc_cast(name), cc_cast(scriptName));
  }

  // seek variable in this and all parents
  const GameVarSpace *seek = this;
  GameVariable *varFound = NULL;
  const GameVarSpace *space = NULL;
  while (seek)
  {
    const GameVariable &var=seek->_vars[name];
    if (NotNull(var))
    {
      varFound = const_cast<GameVariable *>(&var);
      space = seek;
      break;
    }
    seek = seek->_parent;
  }

  // note: assigning nil does not destroy a variable (news:e1g9uq$61p$1@new_server.localdomain), it led to strage issues with local variables
  if (varFound)
  {
    GameVariable &var=*varFound;

    // set variable value
    var._value=value;
    var._readOnly=readOnly;
    return false;
  }
  else
  {
    // new variable
    GameVariable newvar(name,value,readOnly);
    _vars.Add(newvar);
    return true;
  }
}

static bool StrIsLower(const char *str)
{
  if ( !str ) return true;
  do {
    if ( isupper(*str) ) return false;
  } while ( *(++str) );
  return true;
}


bool GameVarSpace::VarGet(const char *name, GameValue &ret) const
{
  DoAssert(StrIsLower(name));
  const GameVarSpace *seek = this;
  while (seek)
  {
    const GameVariable &var=seek->_vars[name];
    if (NotNull(var))
    {
      ret = var._value;
      return true;
    }
    seek = seek->_parent;
  }
  //LogF("Var %s not found",cc_cast(name));
  ret = GameValueNil;
  return false;
}

/*!
\param name variable name
\param value variable value
\param readOnly if true, variable will be marked as read only
\param forceLocal if true, variable is created in local namespace
even if it does not start with underscore.
*/

void GameState::VarSet(const char *name, GameValuePar value, bool readOnly, bool forceLocal, GameDataNamespace *globals)
{ 
#define DEBUG_PUBLIC_VARIABLES 0
#if DEBUG_PUBLIC_VARIABLES
  if (stricmp(cc_cast(name),"BIS_missionScope")==0)
    _asm nop;
#endif
  // convert name to lower case
  RString source = name;
  source.Lower();

  if (forceLocal || *name == '_')
  {
    // local variable
    GameVarSpace *space = _e->local;
    if (!space)
    {
      SetError(EvalNamespace);
      return;
    }
#ifdef _SCRIPT_DEBUGGER
    bool res = space->VarSet(source,value,readOnly);
    if (_debugger) 
    {       
      _debugger->OnVariableChanged(*this,name,value,res);
    }
#else
    space->VarSet(source, value, readOnly);
#endif
  }
  else
  {
    // global variable
    if (!globals)
    {
      // check if default namespace is enabled
      if (_defaultGlobalsEnabled) globals = &GDefaultNamespace;
      else
      {
        SetError(EvalNamespace);
        return;
      }
    }
    if (value.GetNil()) // when global variable is set to nil, delete it
    {
      globals->VarDelete(name);
    }
    else
    {
  #ifdef _SCRIPT_DEBUGGER
    bool res = globals->VarSet(source, value, readOnly);
    if (_debugger) 
    {       
      _debugger->OnVariableChanged(*this,name,value,res);
    }
  #else
    globals->VarSet(source, value, readOnly);
  #endif
  }
  }
}

void GameState::VarLocal(const char *name)
{
  GameVarSpace *space= _e->local;
  if( !space )
  {
    SetError(EvalNamespace);
    return;
  }
  RString source = name;
  source.Lower();
  space->VarLocal(source);
}

/*!
\param name variable name
\param value variable value
\param readOnly if true, variable will be marked as read only
\param forceLocal if true, variable is created in local namespace
even if it does not start with underscore.
with const variant you can set only local variables
*/
void GameState::VarSetLocal( const char *name, GameValuePar value, bool readOnly, bool forceLocal) const
{
  // convert name to lower case
  RString source = name;
  source.Lower();

  if ( forceLocal || *name=='_')
  {
    GameVarSpace *space = _e->local;
    if( !space )
    {
      SetError(EvalNamespace);
      return;
    }
    space->VarLocal(source);
    space->VarSet(source,value,readOnly);
  }
  else
  {
    SetError(EvalBadVar);
    return;
  }
}

void GameState::VarDelete(const char *name, GameDataNamespace *globals)
{
  if (*name == '_')
  {
    GameVarSpace *space = _e->local;
    if (!space)
    {
      SetError(EvalNamespace);
      return;
    }
    space->_vars.Remove(name);
  }
  else
  {
    if (!globals)
    {
      // check if default namespace is enabled
      if (_defaultGlobalsEnabled) globals = &GDefaultNamespace;
      else
      {
        SetError(EvalNamespace);
        return;
      }
    }
    globals->VarDelete(name);
  }
}

EvalError GameState::GetLastError() const
{
  return _e->_error;
}

RString GameState::GetLastErrorText() const
{
  return _e->_errorText;
}

int GameState::GetLastErrorPos(RString expression) const
{
#if USE_PRECOMPILATION
  if (expression == _e->_errorPos._content)
    return _e->_errorPos._pos;
  else
    return 0; // error in other expression (for example code variable content)
#else
  return _e->_errorCarretPos;
#endif
}

static RString ExpEval = "Expression evaluation";

/*!
\patch 1.33 Date 11/27/2001 by Ondra
- Fixed: Corrupt error message when expression type was wrong in waypoint condition.
*/

// evaluate an expression
GameValue GameState::Evaluate(const char *expression, const EvalContext &ctx, GameDataNamespace *globals) const
{
  //ADD_COUNTER(exVal,1);

  if (!globals)
  {
    // check if default namespace is enabled
    if (_defaultGlobalsEnabled) globals = &GDefaultNamespace;
    else
    {
      ErrF("Global namespace not passed during: %s", expression);
    }
  }

  // FIX: when error occurs in global context, evaluation never recovers
  if (_e->local == NULL)
  {
    _e->_error = EvalOK;
    _e->_errorText = RString();
  }

  if (_e->_error!=EvalOK) return NOTHING; //Begin context sets this to EvalOK - prevents execute code, when error was reported
  _e->_errorText = RString();

#if USE_PRECOMPILATION

  // create evaluation context
  VMContext context(false);
  context._doc._content = expression;
  context._localOnly = ctx._localOnly;
  context._undefinedIsNil = ctx._undefinedIsNil;

  // compile
  CallStackItem *level = new CallStackItemSimple(globals, NULL, GetContext(), 0, this, ExpEval, context._doc, false, context.IsSerializationEnabled()); // root
  context.Clear();
  context.AddLevel(level);
  if (_e->_error != EvalOK) return NOTHING;

  // make global variables accessible
  GameDataNamespace *oldGlobals = SetGlobalVariables(globals);

  // evaluate
  VMContext *oldContext = SetVMContext(&context);
  bool done = context.EvaluateCore(*this);
  if (!_e->_checkOnly) ShowError(); // error position is given in context of whole document
  SetVMContext(oldContext);

  SetGlobalVariables(oldGlobals);

  if (_e->_error != EvalOK) return NOTHING;

  if (done)
  {
    // If there is not 0 or 1 item on the stack left, then report error
    if (context._stack.Size() > 1)
    {
      // position of the last statement
      VMContext *oldContext = SetVMContext(&context);
      SetError(context._lastInstruction, EvalGen);
      if (!_e->_checkOnly) ShowError(); // error position is given in context of whole document
      SetVMContext(oldContext);
      return NOTHING;
    }

    if (context._stack.Size() == 0) return NOTHING;
    return context._stack[0];
  }
  else
  {
    // not finished
    return NOTHING;
  }

#else

  _e->_pos0 = _e->_pos = expression;
  _e->_subexpBeg = 0;

  //Done[index]=false;
  GameValue result;
  if( *expression )
  {
    // make global variables accessible
    GameDataNamespace *oldGlobals = SetGlobalVariables(globals);
    result=Vyhod();
    SetGlobalVariables(oldGlobals);

    CleanStack();
    if (!_e->_checkOnly) ShowError();
  }
  else result=0.0f;

  _e->_pos = NULL;
  _e->_pos0 = NULL;
  _e->_subexpBeg = -1;

  return result;

#endif
}

const char *TestLocalAssign(const char *p) {
    if (strnicmp(p, LOCAL_VAR_ASSIGN,LOCAL_VAR_ASSIGN_SZ) == 0)
    {
        int i = LOCAL_VAR_ASSIGN_SZ;
        while (p[i] && isspace(p[i])) i++;
        if (i > LOCAL_VAR_ASSIGN_SZ && p[i] == '_')
            return p+i;
    } 
    return p;
}

const char *CheckAssignment(const char *p)
{
    p = TestLocalAssign(p);
  while (*p && isalnumext(*p)) p++;
  while (*p && isspace(*p)) p++;
  if (p[0]=='=' && p[1]!='=') return p;
  return NULL;
}

const GameState::EvalContext GameState::EvalContext::_default=EvalContext();
const GameState::EvalContext GameState::EvalContext::_reportUndefined=EvalContext(false,false);

/*!
\param localOnly if true, only assignment to local variables is allowed
*/

GameValue GameState::EvaluateMultiple(const char *expression, const EvalContext &ctx, GameDataNamespace *globals) const
{
  if (!globals)
  {
    // check if default namespace is enabled
    if (_defaultGlobalsEnabled) globals = &GDefaultNamespace;
    else
    {
      ErrF("Global namespace not passed during: %s", expression);
    }
  }

  // FIX: when error occurs in global context, evaluation never recovers
  if (_e->local == NULL)
  {
    _e->_error = EvalOK;
    _e->_errorText = RString();
  }

  if (_e->_error!=EvalOK) return NOTHING; //Begin context sets this to EvalOK - prevents execute code, when error was reported

#if USE_PRECOMPILATION
  // create evaluation context
  VMContext context(false);
  context._doc._content = expression;
  context._localOnly = ctx._localOnly;
  context._undefinedIsNil = ctx._undefinedIsNil;

  // compile
  CallStackItem *level = new CallStackItemSimple(globals, NULL, GetContext(), 0, this, ExpEval, context._doc, true, context.IsSerializationEnabled()); // root
  context.Clear();
  context.AddLevel(level);
  if (_e->_error != EvalOK)
  {
    ShowError();
    return NOTHING;
  }


  // evaluate
  GameDataNamespace *oldGlobals = SetGlobalVariables(globals);
  VMContext *oldContext = SetVMContext(&context);
  bool done = context.EvaluateCore(*this);
  if (!_e->_checkOnly) ShowError(); // error position is given in context of whole document
  SetVMContext(oldContext);
  SetGlobalVariables(oldGlobals);

  if (_e->_error != EvalOK) return NOTHING;

  if (done)
  {
    // If there is not 0 or 1 item on the stack left, then report error
    if (context._stack.Size() > 1)
    {
      // position of the last statement
      VMContext *oldContext = SetVMContext(&context);
      SetError(context._lastInstruction, EvalGen);
      if (!_e->_checkOnly) ShowError(); // error position is given in context of whole document
      SetVMContext(oldContext);
      return NOTHING;
    }

    if (context._stack.Size() == 0) return NOTHING;
    return context._stack[0];
  }
  else
  {
    // not finished
    return NOTHING;
  }
#else
  //ADD_COUNTER(exExe,1);
  // command should be one or more assignements
  // scan for variable name (all before '=')
  _e->_pos = _e->_pos0 = expression;
  _e->_subexpBeg = 0;

  _e->_error=EvalOK;
  _e->_errorText = RString();

  GameValue ret = NOTHING;
  while( *_e->_pos )
  {
    if (!(ret.GetType() & GameNothing))
    {
      if( _e->_checkOnly )
      {
        TypeError(GameNothing,ret.GetType());
        break;
      }     
    }
    vynech();
#ifdef _SCRIPT_DEBUGGER
    if (_debugger) 
    { 
      if (_debugger->BeforeLineProcessed(const_cast<GameState &>(*this))==false) return ret;
    }
#endif
    // check if expression is assignment
    // assignment must start with identifier
    _e->_subexpBeg = _e->_pos - _e->_pos0;
    if (CheckAssignment(_e->_pos))
    {
      // make global variables accessible
      GameDataNamespace *oldGlobals = SetGlobalVariables(globals);
      bool ok = const_cast<GameState *>(this)->PerformAssignment(ctx._localOnly);
      SetGlobalVariables(oldGlobals);

      if (!ok)
      {
        break;
      }
      ret = NOTHING;
    }
    else // non assignment
    {
      // check for empty command

      // make global variables accessible
      GameDataNamespace *oldGlobals = SetGlobalVariables(globals);
      GameValue value=Vyhod();
      SetGlobalVariables(oldGlobals);

      CleanStack();
      if( _e->_error ) break;

      // check expression result
      // if result is nothing it's OK
      // if result is fake type it is not OK
      ret = value;
    } // single command executed
    vynech();
    char c = *_e->_pos;
    if (c==0 || (_e->local && _e->local->_blockExit)) break; //exit when break;
    if (c!=';' && (c!=',' || _e->_checkOnly))
    {
      SetError(EvalSemicolon);
      break;
    }
    _e->_pos++;
  }
  if (!_e->_checkOnly && _e->_pos && _e->_pos0) ShowError();
  // when it will go out of scope, _pos will be invalid
  _e->_pos = NULL;
  _e->_pos0 = NULL;
  _e->_subexpBeg = -1;
  return ret;
#endif
}

/*!
\patch_internal 1.46 Date 3/4/2002 by Jirka
- Fixed: if list of expressions occurred in condition, get value of first boolean one
*/

bool GameState::EvaluateBool(const char *expression, const EvalContext &ctx, GameDataNamespace *globals) const
{
  GameValue value = EvaluateMultiple(expression, ctx, globals);
  if (!(value.GetType() & GameBool))
  {
    // set and show the error
#if USE_PRECOMPILATION
    if (GetVMContext())
      TypeError(GameBool, value.GetType());
    else
    {
      VMContext context(false);
      context._doc._content = expression;
      context._localOnly = ctx._localOnly;
      context._undefinedIsNil = ctx._undefinedIsNil;
      context._lastInstruction = SourceDocPos(context._doc);

      VMContext *oldContext = SetVMContext(&context);
      TypeError(GameBool, value.GetType());
      ShowError();
      SetVMContext(oldContext);
    }
#else
    TypeError(GameBool, value.GetType());
#endif
    return false;
  }
  return bool(value);
}

bool GameState::EvaluateMultipleBool(const char *expression, const EvalContext &ctx, GameDataNamespace *globals) const
{
  GameValue value = EvaluateMultiple(expression, ctx, globals);
  if( value.GetType()&GameBool )
  {
    return bool(value);
  }

#if USE_PRECOMPILATION
  if (GetVMContext())
    TypeError(GameBool, value.GetType());
  else
  {
    VMContext context(false);
    context._doc._content = expression;
    context._localOnly = ctx._localOnly;
    context._undefinedIsNil = ctx._undefinedIsNil;
    context._lastInstruction = SourceDocPos(context._doc);

    VMContext *oldContext = SetVMContext(&context);
    TypeError(GameBool, value.GetType());
    ShowError();
    SetVMContext(oldContext);
  }
#else
  TypeError(GameBool, value.GetType());
#endif
  return false;
}


bool GameState::VarGoodName( const char *name ) const
{
  // check if variable name is correct
  // check all operator names
  //if (_functions.NotNull(_functions.Get(name))) return false;
  //if (_operators.NotNull(_operators.Get(name))) return false;
  //if (_nulars.NotNull(_nulars.Get(name))) return false;
  return true;
}

bool GameState::IdtfGoodName( const char *name ) const
{
  if( !isalphaext(*name) ) return false;
  for( const char *n=name; *n; n++ )
  {
    if( !isalnumext(*n) ) return false;
  }  
  // check all operator names
  if (_functions.NotNull(_functions.Get(name))) return false;
  if (_operators.NotNull(_operators.Get(name))) return false;
  if (_nulars.NotNull(_nulars.Get(name))) return false;
  return VarGoodName(name);
}

bool GameState::LValueGoodName(const char *name, const GameDataNamespace *globals) const
{
  // check if variable name is correct
  if (VarReadOnly(name, globals)) return false;
  return VarGoodName(name);
}

// execute command
void GameState::Execute(const char *expression, const EvalContext &ctx, GameDataNamespace *globals)
{
  GameValue ret = EvaluateMultiple(expression, ctx, globals);
  if (!(ret.GetType() & GameNothing) && !((ret.GetType() & GameBool) && ret))
  {
    if (_e->_checkOnly)
    {
      TypeError(GameNothing, ret.GetType());
    }     
  }
}

#if USE_PRECOMPILATION
  // TODO: consider passing GameEvaluator by value instead?
  DEFINE_FAST_ALLOCATOR_EXPRESS(GameEvaluator)
#endif

GameEvaluator::GameEvaluator( GameVarSpace *vars )
{
#if _DEBUG_GS_CONTEXT
  // remember the caller address (used to detect the BeginContext caller)
  DWORD *epbCur;
  __asm mov epbCur,ebp;
  callStackRET = epbCur[10];
#endif
  local=vars;
  _isUIcontext = false;
  static int newHandle = 0;
  handle = newHandle++;
}

GameEvaluator::~GameEvaluator()
{
}

GameVarSpace *GameState::GetContext() const
{
  return _e->local;
}

int GameState::BeginContext( GameVarSpace *vars, bool isUIContext ) const
{
  _contextStack.Add(new GameEvaluator(vars));
  _e=_contextStack.Last();

  _e->_error=EvalOK;
  _e->_errorText = RString();
  _e->_checkOnly=false;
  _e->_isUIcontext = isUIContext;
#if !USE_PRECOMPILATION
  _e->_errorCarretPos = 0;
  //BREDY: Pointer initialization - program may need ensure, that context is not running now.
  _e->_pos = NULL;
  _e->_pos0 = NULL;
  _e->_subexpBeg = -1;
  _e->SP=0;
#endif
  return _e->handle;
}

void GameState::EndContext( int handle ) const
{
  if (_e->_error==EvalStackOverflow) return;
  if( _contextStack.Size()<=0 )
  {
    ErrorMessage("GameState context stack underflow");
    return;
  }
  if (_contextStack[_contextStack.Size()-1]->handle != handle)
  {
    RptF("Error: Mismatched Begin/EndContexts");
    // but END it in spite of it is not matching pair
  }
  _contextStack.Delete(_contextStack.Size()-1);
  _contextStack.CompactIfNeeded(2,64);
  _e=_contextStack.Last();
}

bool GameState::CheckEvaluate(const char *expression, GameDataNamespace *globals) const
{
  _e->_error = EvalOK;
  _e->_errorText = RString();

  _e->_checkOnly=true;
  Evaluate(expression, EvalContext::_default, globals);
  _e->_checkOnly=false;
  bool ret=( _e->_error==EvalOK );
  return ret;
}

bool GameState::CheckEvaluateBool(const char *expression, GameDataNamespace *globals) const
{
  _e->_error = EvalOK;
  _e->_errorText = RString();

  _e->_checkOnly=true;
  EvaluateBool(expression, EvalContext::_default, globals);
  _e->_checkOnly=false;
  bool ret=( _e->_error==EvalOK );
  return ret;
}

bool GameState::CheckExecute(const char *expression, GameDataNamespace *globals) const
{
  _e->_error = EvalOK;
  _e->_errorText = RString();

  _e->_checkOnly=true;
  const_cast<GameState *>(this)->Execute(expression, EvalContext::_default, globals);
  _e->_checkOnly=false;
  bool ret=( _e->_error==EvalOK );
  return ret;
}

#ifndef ACCESS_ONLY
LSError GameVariable::Serialize(ParamArchive &ar)
{
  CHECK(ar.Serialize("name", _name, 1))
    //LogF("Var %s pass %d",cc_cast(_name),ar.GetPass());
    if (ar.GetPass()==ParamArchive::PassSecond)
    { // the name must be same in 1st and 2nd pass. If not, some corruption happened meanwhile
      RString name;
      ar.FirstPass();
      CHECK(ar.Serialize("name", name, 1))
        ar.SecondPass();
      if (strcmpi(name,_name))
      {
        ar.OnError(LSStructure,RString());
        RptF("Variable name changed: %s!=%s in %s",cc_cast(_name),cc_cast(name),cc_cast(ar.GetErrorContext()));
        return LSStructure;
      }
    }
    { // if value serialization fails, provide the variable name for easier debugging
      LSError err = _value.Serialize(ar);
      if (err!=LSError(0))
      {
        RptF("Error loading/saving variable %s",cc_cast(_name));
        return err;
      }
    }
    CHECK(ar.Serialize("readOnly", _readOnly, 1))
      return (LSError)0;
}
#endif

void GameVariable::GetVarName(char *buffer, int len) const
{
  strncpy(buffer, _name, len);
  buffer[len - 1] = 0;
}

IDebugValueRef GameVariable::GetVarValue() const
{
  return _value.GetData();
}

GameData *GameState::CreateGameData(const GameType &type, ParamArchive *ar) const
{
  // GameVoid is static type
  if (type == GameVoid ) return new GameDataNil(GameVoid);
  if (type == GameAny ) return new GameDataNil(GameAny);

  for (int i=0; i<_typeNames.Size(); i++)
    if (GameType(_typeNames[i]) == type) // TODO: optimize: avoid GameType temporary on the left side
    {
      if (_typeNames[i]->_createFunction) return _typeNames[i]->_createFunction(ar);
      Fail("Missing create function");
      return NULL;
    }
    Fail("Unknown data type");
    return NULL;
}

static void CatType(RString &a, RString b)
{
  if (a.GetLength()<=0) a = b;
  else if (b.GetLength()<=0) ;
  else a = a + RString(",") + b;
}

RString GameState::GetInternalTypeName(const GameType &type) const
{
  // GameAny is static type
  if (type == GameAny) return "ANY";
  if (type == GameNothing) return "NOTHING";

  RString ret = "";
  for (int i=0; i<_typeNames.Size(); i++)
  {
    GameType typeI(_typeNames[i]);  // TODO: optimize: avoid GameType temporary
    if (typeI==GameAny) continue; // skip GameAny, is satisfied to all types
    if (type & typeI)
      CatType(ret, _typeNames[i]->_name);
  }
  return ret;  
}

RString GameState::GetTypeName(const GameType &type) const
{
  // GameVoid is static type
  if (type == GameAny) return _defaultLocalizeFunctions->LocalizeString("STR_EVAL_TYPEANY");
  if (type == GameNothing) return _defaultLocalizeFunctions->LocalizeString("STR_EVAL_TYPENOTHING");

  RString ret = "";
  for (int i=0; i<_typeNames.Size(); i++)
  {
    GameType typeI(_typeNames[i]);  // TODO: optimize: avoid GameType temporary
    if (typeI==GameAny) continue; // skip GameAny, is satisfied to all types
    if (type & typeI)
      CatType(ret, _defaultLocalizeFunctions->Localize(_typeNames[i]->_typeName));
  }
  return ret;
}



GameValue GameState::CreateGameValue(const GameType &type) const
{
  GameData *data = CreateGameData(type, NULL);
  if( !data ) return GameValue();
  return GameValue(data);
}

#if DOCUMENT_COMREF || _ENABLE_COMREF
static RString XMLEncode(RString in)
{
  RString out;
  for (const char *ptr = in; *ptr != 0; ptr++)
  {
    switch (*ptr)
    {
    case '&':
      out = out + RString("&amp;");
      break;
    case '\'':
      out = out + RString("&apos;");
      break;
    case '\"':
      out = out + RString("&quot;");
      break;
    case '<':
      out = out + RString("&lt;");
      break;
    case '>':
      out = out + RString("&gt;");
      break;
    default:
      out = out + Format("%c", *ptr);
      break;
    }
  }
  return out;
}
#endif

#if DOCUMENT_COMREF

RString GameState::ComRefTypeName(const GameType &type) const
{
  for (int i=0; i<_comRefType.Size(); i++)
  {
    if (type == _comRefType[i].type) return _comRefType[i].name;
  }
  return RString();
}

#include <Es/Algorithms/qsort.hpp>

static char GetFirstLetter(const RString &name)
{
  if (name.GetLength() == 0) return '#';
  char c = name[0];
  if (!isalpha(c)) return '#';
  return toupper(c);
}

static int CmpComRefFunc(const ComRefFunc *f1, const ComRefFunc *f2)
{
  char letter1 = GetFirstLetter(f1->name);
  char letter2 = GetFirstLetter(f2->name);
  int diff = letter1 - letter2;
  if (diff != 0) return diff;
  diff = stricmp(f1->name, f2->name);
  if (diff != 0) return diff;
  diff = f1->loperType - f2->loperType;
  if (diff != 0) return diff;
  return f1->roperType - f2->roperType;
}

bool GameState::ComRefDocument(const char *file)
{
  FILE *out = fopen(file, "wb");
  if (out==NULL) return false; //something wrong

  // header
  fprintf(out, "<?xml version=\"1.0\" encoding=\"iso-8859-1\"?>\r\n");
  fprintf(out, "\r\n");

  fprintf(out, "<comref>\r\n");
  fprintf(out, "<commandlist>\r\n");
  fprintf(out, "<name> BIS Game Engine Scripting Commands</name>\r\n");
  QSort(_comRefFunc.Data(), _comRefFunc.Size(), CmpComRefFunc);
  for (int i=0; i<_comRefFunc.Size(); i++)
  {
    ComRefFunc &func = _comRefFunc[i];
    char letter = GetFirstLetter(func.name);
    fprintf(out, "\t<command letter=\"%c\">\r\n", letter);

    fprintf(out, "\t\t<name>%s</name>\r\n", (const char *)XMLEncode(func.name));

    if (func.loper.GetLength() > 0)
      fprintf(out, "\t\t<loper>%s</loper>\r\n", (const char *)XMLEncode(func.loper));

    if (func.loperType != GameType())
      fprintf(out, "\t\t<lopertype>%s</lopertype>\r\n", (const char *)ComRefTypeName(func.loperType));

    if (func.roper.GetLength() > 0)
      fprintf(out, "\t\t<roper>%s</roper>\r\n", (const char *)XMLEncode(func.roper));

    if (func.roperType != GameType())
      fprintf(out, "\t\t<ropertype>%s</ropertype>\r\n", (const char *)ComRefTypeName(func.roperType));

    if (func.since.GetLength() > 0)
      fprintf(out, "\t\t<since>%s</since>\r\n", (const char *)func.since);

    if (func.changed.GetLength() > 0)
      fprintf(out, "\t\t<changed>%s</changed>\r\n", (const char *)func.changed);

    if (func.returnType != GameType())
      fprintf(out, "\t\t<returntype>%s</returntype>\r\n", (const char *)ComRefTypeName(func.returnType));

    if (func.description.GetLength() > 0)
      fprintf(out, "\t\t<description>%s</description>\r\n", (const char *)func.description);

    if (func.example.GetLength() > 0)
      fprintf(out, "\t\t<example>%s</example>\r\n", (const char *)XMLEncode(func.example));

    if (func.exampleResult.GetLength() > 0)
      fprintf(out, "\t\t<exResult>%s</exResult>\r\n", (const char *)XMLEncode(func.exampleResult));

    fprintf(out, "\t\t<category>%s</category>\r\n", (const char *)XMLEncode(func.category));

    fprintf(out, "\t</command>\r\n");
  }
  fprintf(out, "</commandlist>\r\n");

  fprintf(out, "<typelist>\r\n");
  for (int i=0; i<_comRefType.Size(); i++)
  {
    ComRefType &type = _comRefType[i];
    fprintf(out, "\t<type>\r\n");

    fprintf(out, "\t\t<name>%s</name>\r\n", (const char *)XMLEncode(type.name));
    fprintf(out, "\t\t<description>%s</description>\r\n", (const char *)type.description);
    fprintf(out, "\t\t<category>%s</category>\r\n", (const char *)XMLEncode(type.category));

    fprintf(out, "\t</type>\r\n");
  }
  fprintf(out, "</typelist>\r\n");

  fprintf(out, "<arraylist>\r\n");
  for (int i=0; i<_comRefArray.Size(); i++)
  {
    ComRefArray &array = _comRefArray[i];
    fprintf(out, "\t<array>\r\n");

    fprintf(out, "\t\t<name>%s</name>\r\n", (const char *)XMLEncode(array.name));
    fprintf(out, "\t\t<format>%s</format>\r\n", (const char *)array.format);
    fprintf(out, "\t\t<description>%s</description>\r\n", (const char *)array.description);
    fprintf(out, "\t\t<category>%s</category>\r\n", (const char *)XMLEncode(array.category));

    fprintf(out, "\t</array>\r\n");
  }
  fprintf(out, "</arraylist>\r\n");
  fprintf(out, "</comref>\r\n");

  fclose(out);
  return true;
}

static int CmpComRefFuncWordFile(const ComRefFunc *f1, const ComRefFunc *f2)
{
  return stricmp(f1->name, f2->name);
}

//the following "#if 1" section is problematic (couldn't be compiled in OFP2 debug
//and generates a lot of warnings in OFP2 release)
#if !ALLOC_DEBUGGER && !_SUPER_RELEASE && !defined _LINUX
#include <malloc.h>
#include <fstream>
using namespace std;

bool GameState::GenerateUltraeditWordFile(const char *targetfile, const char *langName, const char *extensionList, bool preprocessor)
{
  char *outfilename=strcat(strcpy((char *)alloca(strlen(targetfile)+10),targetfile),".tmp");
  ifstream file(targetfile,ios::in);
  ofstream outfile(outfilename,ios::out|ios::trunc);
  if (!file) return false;
  int linesize=1024*1024;
  char *line=new char [linesize];
  int lindex=1;
  while (!file.eof())
  {
    file.getline(line,linesize);
    if (line[0]=='/' && line[1]=='L')
    {
      sscanf(line+2,"%d",&lindex);
      lindex++;
      char *c=strchr(line,'"');
      if (c)
      {
        c++;
        int ll=strlen(langName);
        if (strncmp(c,langName,ll)==0 && c[ll]=='"')
        {
          lindex--;
          do
          {
            file.getline(line,linesize);
          }
          while (!file.eof() && (line[0]!='/' || line[1]!='L'));
          break;
        }
      }
    }
    outfile<<line<<"\n";
  }
  outfile<<"/L"<<lindex<<"\""<<langName<<"\" Line Comment = // Block Comment On = /* Block Comment Off = */ String Chars = \" Nocase File Extensions = "<<extensionList<<"\n";
  outfile<<"/Delimiters = ,;[] \t(){}\"+-=<>^\n";
  outfile<<"/Function String = \"%[ ^t]++^([a-zA-Z_0-9]+^)[ ^t]++[=][ ^n^r^t]++{\"\n";
  outfile<<"/Function String 1 = \"%#define[ ^t]+^([a-zA-Z_0-9]+([a-zA-Z_0-9, ^t]++)^)\"\n";
  outfile<<"/Indent Strings = \"{\"\n";
  outfile<<"/Unindent Strings = \"}\"\n";
  outfile<<"/C1\""<<langName<<" Keywords\"\n";
  QSort(_comRefFunc.Data(), _comRefFunc.Size(), CmpComRefFuncWordFile);
  RString last;
  for (int i=0; i<_comRefFunc.Size(); i++)
  {
    ComRefFunc &func = _comRefFunc[i];
    if (func.name!=last)
    {
      outfile<<func.name.Data()<<"\n";
      last=func.name;
    }
  }
  if (preprocessor) 
  {
    outfile<<"#define #error #include #elif #if #line #else #ifdef #pragma #endif #ifndef #undef";
  }
  outfile<<"\n\n";
  while (!file.eof())
  {
    outfile<<line<<"\n";
    file.getline(line,linesize);
  }
  delete [] line;
  file.close();
  outfile.close();
  remove(targetfile);  
  return rename(outfilename,targetfile)==0;
}
#endif

#endif

const GameTypeType *GameState::FindType(const char* name) const
{
  for (int i=0; i<_typeNames.Size(); i++)
  {
    if (stricmp(_typeNames[i]->_name, name) == 0) return _typeNames[i];
  }
  return NULL;
}

int GameState::FindTypeIndex(const GameTypeType* type) const
{
  for (int i=0; i<_typeNames.Size(); i++)
  {
    if (_typeNames[i] == type) return i;
  }
  return -1;
}

#if _ENABLE_COMREF
static RString FormatType(const GameType &type, bool useAsName = false)
{
  RString result;

  const AutoArray<const GameTypeType *> &types = GGameState.GetTypes();
  for (int i=0; i<types.Size(); i++)
  {
    GameType typeI(types[i]);  // TODO: optimize: avoid GameType temporary
    if (typeI==GameAny) continue; // skip GameAny, is satisfied to all types
    if (type & typeI)
    {
      if (result.GetLength() > 0) result = result + RString(", ");
      RString displayName = types[i]->_comRefName;
      if (useAsName && displayName.GetLength() > 0)
      {
        // first letter to lowercase
        char *ptr = displayName.MutableData();
        *ptr = tolower(*ptr);
        // single name is enough
        return displayName;
      }
      result = result + Format("<a href=\"type:%s\">%s</a>", cc_cast(types[i]->_name), cc_cast(displayName));
    }
  }

  return result;
}

#include <El/XML/xml.hpp>

/// Translate XML description to HTML
class DescriptionParser : public SAXParser
{
protected:
  RString &_result;
  RString _currentTag;

public:
  DescriptionParser(RString &result)
    : _result(result)
  {
    _trim = false;
  }
  void OnStartElement(RString name, XMLAttributes &attributes);
  void OnEndElement(RString name);
  void OnCharacters(RString chars);
};

void DescriptionParser::OnStartElement(RString name, XMLAttributes &attributes)
{
  if (strcmp(name, "f") == 0 || strcmp(name, "t") == 0 || strcmp(name, "ar") == 0 || strcmp(name, "cl") == 0)
  {
    _currentTag = name;
    return;
  }
  if (strcmp(name, "br") == 0)
  {
    // new line
    _result = _result + RString("<br>&nbsp;&nbsp;&nbsp;");
    return;
  }
  if (strcmp(name, "nibr") == 0)
  {
    // new line without indent
    _result = _result + RString("<br>");
    return;
  }
  if (strcmp(name, "tab") == 0)
  {
    // tabulator
    _result = _result + RString("&nbsp;&nbsp;&nbsp;");
    return;
  }
  if (strcmp(name, "i") == 0)
  {
    // italic
    _result = _result + RString("<i>");
    return;
  }
  if (strcmp(name, "b") == 0)
  {
    // bold
    _result = _result + RString("<b>");
    return;
  }
  // ignore other tags
}

void DescriptionParser::OnEndElement(RString name)
{
  if (strcmp(name, "f") == 0 || strcmp(name, "t") == 0 || strcmp(name, "ar") == 0 || strcmp(name, "cl") == 0)
  {
    _currentTag = RString();
    return;
  }
  if (strcmp(name, "br") == 0)
  {
    // new line
    return;
  }
  if (strcmp(name, "nibr") == 0)
  {
    // new line without indent
    return;
  }
  if (strcmp(name, "tab") == 0)
  {
    // tabulator
    return;
  }
  if (strcmp(name, "i") == 0)
  {
    // italic
    _result = _result + RString("</i>");
    return;
  }
  if (strcmp(name, "b") == 0)
  {
    // bold
    _result = _result + RString("</b>");
    return;
  }
  // ignore other tags
}

static RString FormatScriptingHelp(RString name)
{
#if _ENABLE_COMREF
  RString lwName = name;
  lwName.Lower();
  const GameNular *nular = GGameState.FindNular(lwName);
  if (nular) return Format("nular:%s", cc_cast(name));
  const GameFunctions *functions = GGameState.FindFunctions(lwName);
  if (functions && functions->Size() > 0) return Format("function:%s:0", cc_cast(name));
  const GameOperators *operators = GGameState.FindOperators(lwName);
  if (operators && operators->Size() > 0) return Format("operator:%s:0", cc_cast(name));
#endif
  return RString();
}

void DescriptionParser::OnCharacters(RString chars)
{
  if (strcmp(_currentTag, "f") == 0)
  {
    // link to other function
    RString href = FormatScriptingHelp(chars);
    if (href.GetLength() > 0) _result = _result + Format("<a href=\"%s\">%s</a>", cc_cast(href), cc_cast(chars));
    return;
  }
  if (strcmp(_currentTag, "t") == 0)
  {
    // link to the type
    _result = _result + Format("<a href=\"type:%s\">%s</a>", cc_cast(chars), cc_cast(chars));
    return;
  }
  if (strcmp(_currentTag, "ar") == 0)
  {
    // link to array description
    _result = _result + chars; // TODO:
    return;
  }
  if (strcmp(_currentTag, "cl") == 0)
  {
    // link to the class
    _result = _result + chars; // TODO:
    return;
  }
  _result = _result + chars;
}

/// using DescriptionParser translate XML to HTML
static RString TranslateXML(RString source)
{
  RString result;
  QIStrStream in(cc_cast(source), source.GetLength());
  DescriptionParser parser(result);
  parser.Parse(in);
  return result;
}

RString GameTypeType::FormatHelp() const
{
  RString result = "<html><body>";
  // the title (name)
  result = result + Format("<h1>%s</h1><br>", cc_cast(XMLEncode(_comRefName)));
  // description
  result = result + RString("<h2>Description:</h2>");
  result = result + Format("<p>%s</p>", cc_cast(TranslateXML(_description)));
  // category
  result = result + RString("<br><h2>Category:</h2>");
  result = result + Format("<p>%s</p>", cc_cast(XMLEncode(_category)));

  result = result + RString("</body></html>");
  return result;
}

RString GameNular::FormatHelp() const
{
  RString result = "<html><body>";
  // the title (name, arguments)
  result = result + Format("<h1>%s</h1><br>", cc_cast(XMLEncode(_opName)));
  // compatibility
  /*
  if (_since.GetLength() > 0)
  {
    result = result + RString("<h2>Compatibility:</h2>");
    result = result + Format("<p>%Version %s required.</p>", cc_cast(_since));
    result = result + Format("<p>%Version %s.</p>", cc_cast(_changed));
  }
  */
  // returned value
  result = result + RString("<h2>Type of returned value:</h2>");
  result = result + Format("<p>%s</p>", cc_cast(FormatType(_operator->GetRetType())));
  // description
  result = result + RString("<h2>Description:</h2>");
  // TODO: translate XML to HTML
  result = result + Format("<p>%s</p>", cc_cast(TranslateXML(_description)));
  // example
  if (_example.GetLength() > 0)
  {
    result = result + RString("<br><h2>Example:</h2>");
    if (_exampleResult.GetLength() > 0)
      result = result + Format("<p>%s, result is %s</p>", cc_cast(XMLEncode(_example)), cc_cast(XMLEncode(_exampleResult)));
    else
      result = result + Format("<p>%s</p>", cc_cast(XMLEncode(_example)));
  }
  // category
  result = result + RString("<br><h2>Category:</h2>");
  result = result + Format("<p>%s</p>", cc_cast(XMLEncode(_category)));

  result = result + RString("</body></html>");
  return result;
}

RString GameFunction::GetOperName() const
{
  return _roper.GetLength() > 0 ? XMLEncode(_roper) : FormatType(_operator->GetArgType(), true);
}

RString GameFunction::FormatHelp() const
{
  RString result = "<html><body>";
  // the title (name, arguments)
  RString oper = GetOperName();
  result = result + Format("<h1>%s %s</h1><br>", cc_cast(XMLEncode(_opName)), cc_cast(oper));
  // operand types
  result = result + RString("<h2>Operand types:</h2>");
  result = result + Format("<p>%s: %s</p>", cc_cast(oper), cc_cast(FormatType(_operator->GetArgType())));
  // compatibility
  /*
  if (_since.GetLength() > 0)
  {
    result = result + RString("<h2>Compatibility:</h2>");
    result = result + Format("<p>%Version %s required.</p>", cc_cast(_since));
    result = result + Format("<p>%Version %s.</p>", cc_cast(_changed));
  }
  */
  // returned value
  result = result + RString("<h2>Type of returned value:</h2>");
  result = result + Format("<p>%s</p>", cc_cast(FormatType(_operator->GetRetType())));
  // description
  result = result + RString("<h2>Description:</h2>");
  // TODO: translate XML to HTML
  result = result + Format("<p>%s</p>", cc_cast(TranslateXML(_description)));
  // example
  if (_example.GetLength() > 0)
  {
    result = result + RString("<br><h2>Example:</h2>");
    if (_exampleResult.GetLength() > 0)
      result = result + Format("<p>%s, result is %s</p>", cc_cast(XMLEncode(_example)), cc_cast(XMLEncode(_exampleResult)));
    else
      result = result + Format("<p>%s</p>", cc_cast(XMLEncode(_example)));
  }
  // category
  result = result + RString("<br><h2>Category:</h2>");
  result = result + Format("<p>%s</p>", cc_cast(XMLEncode(_category)));

  result = result + RString("</body></html>");
  return result;
}

RString GameOperator::GetLOperName() const
{
  return _loper.GetLength() > 0 ? XMLEncode(_loper) : FormatType(_operator->GetArg1Type(), true);
}

RString GameOperator::GetROperName() const
{
  return _roper.GetLength() > 0 ? XMLEncode(_roper) : FormatType(_operator->GetArg2Type(), true);
}

RString GameOperator::FormatHelp() const
{
  RString result = "<html><body>";
  // the title (name, arguments)
  RString loper = GetLOperName();
  RString roper = GetROperName();
  result = result + Format("<h1>%s %s %s</h1><br>", cc_cast(loper), cc_cast(XMLEncode(_opName)), cc_cast(roper));
  // operand types
  result = result + RString("<h2>Operand types:</h2>");
  result = result + Format("<p>%s: %s</p>", cc_cast(loper), cc_cast(FormatType(_operator->GetArg1Type())));
  result = result + Format("<p>%s: %s</p>", cc_cast(roper), cc_cast(FormatType(_operator->GetArg2Type())));
  // compatibility
  /*
  if (_since.GetLength() > 0)
  {
    result = result + RString("<h2>Compatibility:</h2>");
    result = result + Format("<p>%Version %s required.</p>", cc_cast(_since));
    result = result + Format("<p>%Version %s.</p>", cc_cast(_changed));
  }
  */
  // returned value
  result = result + RString("<h2>Type of returned value:</h2>");
  result = result + Format("<p>%s</p>", cc_cast(FormatType(_operator->GetRetType())));
  // description
  result = result + RString("<h2>Description:</h2>");
  // TODO: translate XML to HTML
  result = result + Format("<p>%s</p>", cc_cast(TranslateXML(_description)));
  // example
  if (_example.GetLength() > 0)
  {
    result = result + RString("<br><h2>Example:</h2>");
    if (_exampleResult.GetLength() > 0)
      result = result + Format("<p>%s, result is %s</p>", cc_cast(XMLEncode(_example)), cc_cast(XMLEncode(_exampleResult)));
    else
      result = result + Format("<p>%s</p>", cc_cast(XMLEncode(_example)));
  }
  // category
  result = result + RString("<br><h2>Category:</h2>");
  result = result + Format("<p>%s</p>", cc_cast(XMLEncode(_category)));

  result = result + RString("</body></html>");
  return result;
}
#endif

#ifdef _SCRIPT_DEBUGGER
void GameState::DebuggerContext(IScriptDebugger *debugger) const 
{
  debugger->_context.PrepareContext(_contextStack.Data(),_contextStack.Size()-1,_e->_pos0,_e->_pos-_e->_pos0,NULL);
}

void GameState::Halt() const
{
  if (_debugger) _debugger->OnUserBreakpoint(const_cast<GameState &>(*this));
}

void GameState::Echo(const char *text) const
{
  if (_debugger) _debugger->OnConsoleOutput(const_cast<GameState &>(*this),text);
  OnEcho(text);
}
#endif

#if !USE_PRECOMPILATION
bool GameState::ThrowException(GameValuePar exception)
{
  GameVarSpace *context=GetContext();          //get current context
  GameDataException *exp=GetCurrentExceptionHandler(); //get current exception handler
  if (exp==NULL)
  {
    return false;                           //no exception handler defined, report as error
  }
  exp->_excepResult=exception;
  GameVarSpace *untilContext=exp->GetTryLevel();    //get try context
  GameVarSpace *p=context;                 //traverse all contexts until try context found
  while (p!=untilContext && p)
  {
    p->_blockExit=true;                     //mark all context inside try context as finished
    p=const_cast<GameVarSpace *>(p->_parent);
  }
  if (p==NULL) return false;                //if try-context has not been reached, report as error
  exp->_thrown=true;
  return true;                            //okay, now we can continue in program
}
#endif

GameState GGameState;
GameDataNamespace GDefaultNamespace(&GGameState, "default", true); // can be serialized

