#ifdef _MSC_VER
#pragma once
#endif

#ifndef _GAME_STATE_EXT_HPP
#define _GAME_STATE_EXT_HPP

#include <El/Evaluator/expressImpl.hpp>
#include <El/ParamFile/paramFile.hpp>
#include "object.hpp"
#include "AI/ai.hpp"
#include "UI/uiControls.hpp"

class INode;

#if USE_PRECOMPILATION
#include <El/Evaluator/scriptVM.hpp>
#endif

#define TYPES_OFP(XX, Category) \
  XX("OBJECT",GameObject, CreateGameDataObject, "@STR_EVAL_TYPEOBJECT", "Object", "A game object (like a soldier, vehicle or building).", Category, "GameObject", "Lcom/bistudio/JNIScripting/RVEngine$GameObject;") \
  XX("VECTOR",GameVector, NULL, "@STR_EVAL_TYPEVECTOR", "Vector", "", Category, "GameVector", "Lcom/bistudio/JNIScripting/RVEngine$GameVector;") \
  XX("TRANS",GameTrans, NULL, "@STR_EVAL_TYPETRANS", "Transformation", "", Category, "GameTrans", "Lcom/bistudio/JNIScripting/RVEngine$GameTrans;") \
  XX("ORIENT",GameOrient, NULL, "@STR_EVAL_TYPEORIENT", "Orientation", "", Category, "GameOrient", "Lcom/bistudio/JNIScripting/RVEngine$GameOrient;") \
  XX("SIDE",GameSide, CreateGameDataSide, "@STR_EVAL_TYPESIDE", "Side", "The name of the side (see <f>west</f>, <f>east</f>, <f>civilian</f> and <f>resistance</f>).", Category, "GameSide", "Lcom/bistudio/JNIScripting/RVEngine$GameSide;") \
  XX("TEXT",GameText, CreateGameDataText, "Text", "Structured text", "", Category, "GameText", "Lcom/bistudio/JNIScripting/RVEngine$GameText;") \
  XX("SCRIPT",GameScript, CreateGameDataScript, "Script", "Script", "", Category, "GameScript", "Lcom/bistudio/JNIScripting/RVEngine$GameScript;") \
  XX("TARGET",GameTarget, CreateGameDataTarget, "Target", "Target", "", Category, "GameTarget", "Lcom/bistudio/JNIScripting/RVEngine$GameTarget;") \
  XX("JCLASS",GameJavaClass, CreateGameDataJavaClass, "JClass", "JClass", "", Category, "GameJavaClass", "Lcom/bistudio/JNIScripting/RVEngine$GameJavaClass;") \

#define TYPES_VBS(XX, Category) \
  XX("SUBGROUP", GameSubgroup, CreateGameDataSubgroup, "Subgroup", "Subgroup", "Subgroup.", Category, "GameSubgroup", "Lcom/bistudio/JNIScripting/RVEngine$GameSubgroup;") \

#define TYPES_PC(XX, Category) \
  XX("CONFIG",GameConfig, CreateGameDataConfig, "Config entry", "Config", "Config file entry.", Category, "GameConfig", "Lcom/bistudio/JNIScripting/RVEngine$GameConfig;") \
  XX("DISPLAY",GameDisplay, CreateGameDataDisplay, "Display (dialog)", "Display", "Display UI object.", Category, "GameDisplay", "Lcom/bistudio/JNIScripting/RVEngine$GameDisplay;") \
  XX("CONTROL",GameControl, CreateGameDataControl, "Control", "Control", "Control UI object.", Category, "GameControl", "Lcom/bistudio/JNIScripting/RVEngine$GameControl;") \

#define TYPES_FILES(XX, Category) \
  XX("FILE",GameFile, CreateGameDataFile, "File", "File", "A config file (or class) handle.", Category, "GameFile", "Lcom/bistudio/JNIScripting/RVEngine$GameFile;")

#define TYPES_AGENTS(XX, Category) \
  XX("TEAM_MEMBER", GameTeamMember, CreateGameDataTeamMember, "Team member", "Team member", "Team member (agent or team).", Category, "GameTeamMember", "Lcom/bistudio/JNIScripting/RVEngine$GameTeamMember;")

#define TYPES_HIERARCHY(XX, Category) \
  XX("HIERARCHY_ITEM", GameHierarchyItem, CreateGameDataHierarchyItem, "Hierarchy item", "Hierarchy item", "Item of Objects Hierarchy.", Category, "GameHierarchyItem", "Lcom/bistudio/JNIScripting/RVEngine$GameHierarchyItem;")

#define TYPES_OFP_COMB(XX, Category) \
  XX("",GameObject | GameArray, NULL, "", "Object or Array", "<t>Object</t> or <t>Array</t>.", Category, "Object", "Ljava/lang/Object;") \
  XX("",GameObject | GameString, NULL, "", "Object or String", "<t>Object</t> or <t>String</t>.", Category, "Object", "Ljava/lang/Object;") \
  XX("",GameText | GameString, NULL, "", "Text or String", "<t>Text</t> or <t>String</t>.", Category, "Object", "Ljava/lang/Object;")

TYPES_OFP(DECLARE_TYPE, "General")
TYPES_PC(DECLARE_TYPE, "General")
#if _VBS2
  TYPES_VBS(DECLARE_TYPE, "General")
#endif
#if _ENABLE_FILE_FUNCTIONS || DOCUMENT_COMREF
  TYPES_FILES(DECLARE_TYPE, "Files")
#endif
#if _ENABLE_INDEPENDENT_AGENTS || DOCUMENT_COMREF
  TYPES_AGENTS(DECLARE_TYPE, "Agents")
#endif
#if DOCUMENT_COMREF
  TYPES_HIERARCHY(DECLARE_TYPE, "Hierarchy")
#endif

typedef TargetSide GameSideType;

typedef OLink(Object) GameObjectType;
#if USE_PRECOMPILATION
typedef LLink<ScriptVM> GameScriptType;
#else
typedef LLink<Script> GameScriptType;
#endif
typedef Vector3 GameVectorType;
typedef Matrix4 GameTransType;
typedef Matrix3 GameOrientType;
typedef RefR<INode> GameTextType;
typedef LLink<ControlsContainer> GameDisplayType;
#if _ENABLE_INDEPENDENT_AGENTS
typedef AITeamMemberLink GameTeamMemberType;
#endif

#if _ENABLE_FILE_FUNCTIONS
class GameFileType
{
private:
	int _index;

public:
	bool readOnly;

	GameFileType() {_index = -1; readOnly = false;}
	GameFileType(const GameFileType &src);
	~GameFileType();

	void operator = (const GameFileType &src);

	int GetIndex() const {return _index;}
	void SetIndex(int index);
};
#endif

#include <Es/Memory/normalNew.hpp>

/// represent a link to Object
class GameDataObject: public GameData
{
	typedef GameData base;

	GameObjectType _value;

	public:
	GameDataObject():_value(0){}
	GameDataObject( GameObjectType value ):_value(value){}
	~GameDataObject(){}

	const GameType &GetType() const {return GameObject;}
	GameObjectType GetObject() const {return _value;}

	RString GetText() const;
	bool IsEqualTo(const GameData *data) const;
	const char *GetTypeName() const {return "object";}
	GameData *Clone() const {return new GameDataObject(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

	LSError Serialize(ParamArchive &ar);	

	USE_FAST_ALLOCATOR_EXPRESS
};

/// represent a link to Target
class GameDataTarget: public GameData
{
	typedef GameData base;

	LLink<Target> _value;

	public:
	GameDataTarget():_value(0){}
	GameDataTarget( LLink<Target> value ):_value(value){}
	~GameDataTarget(){}

	const GameType &GetType() const {return GameTarget;}
	LLink<Target> GetTarget() const {return _value;}

	RString GetText() const;
	bool IsEqualTo(const GameData *data) const;
	const char *GetTypeName() const {return "target";}
	GameData *Clone() const {return new GameDataTarget(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

	LSError Serialize(ParamArchive &ar);	

	USE_FAST_ALLOCATOR_EXPRESS
};

class GameDataScript: public GameData
{
	typedef GameData base;

	GameScriptType _value;

	public:
	GameDataScript():_value(NULL){}
	GameDataScript( GameScriptType value ):_value(value){}
	~GameDataScript(){}

	const GameType &GetType() const {return GameScript;}
	GameScriptType GetScript() const {return _value;}

	RString GetText() const;
	bool IsEqualTo(const GameData *data) const;
	const char *GetTypeName() const {return "script";}
	GameData *Clone() const {return new GameDataScript(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

	LSError Serialize(ParamArchive &ar);	

	USE_FAST_ALLOCATOR_EXPRESS
};

class GameDataJavaClass : public GameData
{
protected:
  typedef GameData base;

  static jweak _objectClass;

  RString _name;
//  RString _path;
  jclass _class;

  void LoadClass();
  void UnloadClass(JNIEnv *env);

public:
  GameDataJavaClass() : _class(NULL) {}
  GameDataJavaClass(RString name);
  ~GameDataJavaClass();

  GameValue Call(RString method, const GameArrayType &args) const;

  const GameType &GetType() const {return GameJavaClass;}

  RString GetText() const;
  bool IsEqualTo(const GameData *data) const;
  const char *GetTypeName() const {return "jclass";}
  GameData *Clone() const;

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

  LSError Serialize(ParamArchive &ar);

  USE_FAST_ALLOCATOR_EXPRESS
};

class GameDataSide: public GameData
{
	typedef GameData base;

	GameSideType _value;

	public:
	GameDataSide():_value(TSideUnknown){}
	GameDataSide( GameSideType value ):_value(value){}
	~GameDataSide(){}

	const GameType &GetType() const {return GameSide;}
	GameSideType GetSide() const {return _value;}

	RString GetText() const;
	bool IsEqualTo(const GameData *data) const;
	const char *GetTypeName() const {return "side";}
	GameData *Clone() const {return new GameDataSide(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

	LSError Serialize(ParamArchive &ar);

	USE_FAST_ALLOCATOR_EXPRESS
};

#if _ENABLE_FILE_FUNCTIONS
class GameDataFile: public GameData
{
	typedef GameData base;

	GameFileType _value;

	public:
	GameDataFile(){}
	GameDataFile( GameFileType value ):_value(value){}
	~GameDataFile(){}

	const GameType &GetType() const {return GameFile;}
	GameFileType GetFile() const {return _value;}

	RString GetText() const;
	bool IsEqualTo(const GameData *data) const;
	const char *GetTypeName() const {return "file";}
	GameData *Clone() const {return new GameDataFile(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

	LSError Serialize(ParamArchive &ar);

	USE_FAST_ALLOCATOR_EXPRESS
};
#endif

class GameDataText: public GameData
{
	typedef GameData base;

	GameTextType _value;

	public:
	GameDataText(){}
	GameDataText( GameTextType value ):_value(value){}
	~GameDataText(){}

	const GameType &GetType() const {return GameText;}
	GameTextType GetValue() const {return _value;}

	RString GetText() const;
	bool IsEqualTo(const GameData *data) const;
	const char *GetTypeName() const {return "text";}
	GameData *Clone() const {return new GameDataText(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

	LSError Serialize(ParamArchive &ar);

	USE_FAST_ALLOCATOR_EXPRESS
};

struct GameConfigType
{
  ConstParamEntryPtr entry;
  AutoArray<RString> path;
};

class GameDataConfig: public GameData
{
  typedef GameData base;

  GameConfigType _value;

public:
  GameDataConfig() {}
  GameDataConfig(const GameConfigType &value) : _value(value) {}
  ~GameDataConfig() {}

  const GameType &GetType() const {return GameConfig;}
  const GameConfigType &GetConfig() const {return _value;}

  RString GetText() const;
  bool IsEqualTo(const GameData *data) const;
  const char *GetTypeName() const {return "config";}
  GameData *Clone() const {return new GameDataConfig(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

  LSError Serialize(ParamArchive &ar);

  USE_FAST_ALLOCATOR_EXPRESS
};

class GameDataDisplay : public GameData
{
  typedef GameData base;

  GameDisplayType _value;

public:
  GameDataDisplay() {}
  GameDataDisplay(const GameDisplayType &value) : _value(value) {}
  ~GameDataDisplay() {}

  const GameType &GetType() const {return GameDisplay;}
  const GameDisplayType &GetDisplay() const {return _value;}

  RString GetText() const;
  bool IsEqualTo(const GameData *data) const;
  const char *GetTypeName() const {return "display";}
  GameData *Clone() const {return new GameDataDisplay(*this);}

  /// Serialization of GameDataDisplay is not supported
  virtual bool SupportSerialization() const {return false;}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

  LSError Serialize(ParamArchive &ar);

  USE_FAST_ALLOCATOR_EXPRESS
};

class GameDataControl : public GameData
{
  typedef GameData base;

  //! control identifier (for 2D Controls)
  LLink<Control> _control;
  //! control identifier (for Object (3D) Controls)
  OLinkO(ControlObject) _object;

public:
  GameDataControl() {}
  GameDataControl(Control *control) : _control(control) {}
  GameDataControl(ControlObject *object) : _object(object) {}
  ~GameDataControl() {}

  const GameType &GetType() const {return GameControl;}
  const IControl *GetControl() const
  {
    if (_control) return _control;
    return _object;
  }
  IControl *GetControl()
  {
    if (_control) return _control;
    return _object;
  }

  RString GetText() const;
  bool IsEqualTo(const GameData *data) const;
  const char *GetTypeName() const {return "control";}
  GameData *Clone() const
  {
    if (_control) return new GameDataControl(_control);
    else return new GameDataControl(_object);
  }

  /// Serialization of GameDataControl is not supported
  virtual bool SupportSerialization() const {return false;}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

  LSError Serialize(ParamArchive &ar);

  USE_FAST_ALLOCATOR_EXPRESS
};

#if _ENABLE_INDEPENDENT_AGENTS
class GameDataTeamMember : public GameData
{
  typedef GameData base;

  GameTeamMemberType _value;

public:
  GameDataTeamMember() {}
  GameDataTeamMember(const GameTeamMemberType &value) : _value(value) {}
  ~GameDataTeamMember() {}

  const GameType &GetType() const {return GameTeamMember;}
  const GameTeamMemberType &GetTeamMember() const {return _value;}

  RString GetText() const;
  bool IsEqualTo(const GameData *data) const;
  const char *GetTypeName() const {return "teamMember";}
  GameData *Clone() const {return new GameDataTeamMember(*this);}

  jobject ToJObject(JNIEnv *env, GameValuePar from) const;

  LSError Serialize(ParamArchive &ar);

  USE_FAST_ALLOCATOR_EXPRESS
};
#endif

#include <Es/Memory/debugNew.hpp>

inline TargetSide GetSide( GameValuePar oper )
{
	if( oper.GetType()!=GameSide ) return TSideUnknown;
	return static_cast<GameDataSide *>(oper.GetData())->GetSide();
}

class EntityType;

//@{ used to avoid GameValueExt constructor ambiguity
enum GameValExtObjectHelper {GameValExtObject};
enum GameValExtTargetHelper {GameValExtTarget};
enum GameValExtINodeHelper {GameValExtINode};
enum GameValExtSideHelper {GameValExtSide};

/// construct game-value from various non-basic types
class GameValueExt: public GameValue
{
	public:

  //if conversion to GameValueExt is ambiguous, use the GameValExtObjectHelper explicitly
  GameValueExt( Object *value, GameValExtObjectHelper dummy=GameValExtObject) {_data=new GameDataObject(value);}
  GameValueExt( Target *value, GameValExtTargetHelper dummy=GameValExtTarget) {_data=new GameDataTarget(value);}
#if USE_PRECOMPILATION
  GameValueExt( ScriptVM *value ) {_data=new GameDataScript(value);}
#else
	GameValueExt( Script *value ) {_data=new GameDataScript(value);}
#endif
	GameValueExt( GameSideType value, GameValExtSideHelper dummy=GameValExtSide ) {_data=new GameDataSide(value);}
	//GameValueExt( Vector3Par value );
	//GameValueExt( Matrix4Par value );
	//GameValueExt( Matrix3Par value );
#if _ENABLE_FILE_FUNCTIONS
	GameValueExt( GameFileType value ) {_data=new GameDataFile(value);}
#endif
	GameValueExt( GameTextType value, GameValExtINodeHelper dummy=GameValExtINode ) {_data=new GameDataText(value);}
  GameValueExt(const GameConfigType &value) {_data = new GameDataConfig(value);}
  GameValueExt(ControlsContainer *value) {_data = new GameDataDisplay(value);}
  GameValueExt(Control *value) {_data = new GameDataControl(value);}
  GameValueExt(ControlObject *value) {_data = new GameDataControl(value);}
#if _ENABLE_INDEPENDENT_AGENTS
  GameValueExt(AITeamMember *value) {_data = new GameDataTeamMember(value);}
#endif
};

Object *GetValObject( GameValuePar oper );
#if _ENABLE_INDEPENDENT_AGENTS
AITeamMember *GetValTeamMember( GameValuePar oper );
#endif
bool WriteValue(GameValuePar var, QOStrStream &out);

inline GameValue CreateGameSide( TargetSide side ) {return GameValueExt(side);}
inline bool CheckSize(const GameState *state, GameArrayType array, int size);
inline bool CheckType(const GameState *state, GameValuePar oper, const GameType &type);

void OnPlayerConnecting(int dpnid, RString uid, int playerId, RString name);
void OnPlayerConnecting(int dpnid, RString uid, int playerId, RString name, GameValuePar codeValue);

typedef float VECT2[2];
int Quick_Hull_2D(int Nb, const VECT2 *Pts, int *Ind);

//! Write message into debug.log (used for DebugLogFSM command)
void DebugLogFSM( const char *format, ... );

#endif
