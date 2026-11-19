#ifdef _MSC_VER
#pragma once
#endif

#ifndef _NETWORK_OBJECT_HPP
#define _NETWORK_OBJECT_HPP


/*!
\file
Interface file for base objects transferred over network in multiplayer game
*/

#include "../paramArchiveExt.hpp"
#include <El/QStream/qStream.hpp>
#include <El/Time/time.hpp>
#include "Es/Containers/boolArray.hpp"

#include "../objLink.hpp"

#include "messageFactory.hpp"

#include "../cast.hpp"

#pragma warning(push)
#pragma warning(disable:4530)
#include <functional>
#pragma warning(pop)

class NetworkMessageContext;
class NetworkMessageContextWithError;
class NetworkMessageFormatBase;
class NetworkMessageFormat;
DECL_ENUM(TMError)
DECL_ENUM(NetworkMessageType)

//! message update class for objects
enum NetworkMessageClass
{
  NMCCreate = -1,
  NMCUpdateFirst = 0,	// update values are used as index into array
  NMCUpdateGeneric = NMCUpdateFirst,
  NMCUpdatePosition,
  NMCUpdateDamage, // dammage and hit update
  NMCUpdateN
};

#define REF_MSG(type) Ref<NetworkMessage>
#define REF_MSG_ARRAY(type) RefArray<NetworkMessage>

#ifndef DECL_ENUM_NETWORK_MESSAGE_ERROR_TYPE
#define DECL_ENUM_NETWORK_MESSAGE_ERROR_TYPE
DECL_ENUM(NetworkMessageErrorType)
#endif

struct NetworkMessageFormatItem;
struct NetworkUpdateInfo;
struct NetworkObjectInfo;
struct NetworkPlayerObjectInfo;

class AITeamMember;

struct MessageDiff
{
  /// indicate which values did not change and can be skipped
  AutoArray<bool> _skip;

  bool IsEmpty() const {return _skip.Size()==0;}

  /// check if all values are true (note: empty is identity as well)
  bool IsIdentity() const
  {
    // if there is any false, ForEachF will break and return false - in that case we are not identity
    return !_skip.ForEachF([](bool s){return !s;});
  }

};

/// intermediate class created so that assignment to / from MessageDiff is not assigning skipCount as well
struct MessageDiffWithCount: public MessageDiff
{
  int _skipCount;

  explicit MessageDiffWithCount(const MessageDiff &diff):MessageDiff(diff),_skipCount(0){}
  MessageDiffWithCount():_skipCount(0){}
};

struct MessageDiffCreate: public MessageDiffWithCount
{
  void Append(bool value)
  {
    _skip.Resize(_skipCount+1);
    _skip[_skipCount] = value;
    _skipCount++;
  }

};

struct MessageDiffApply: public MessageDiffWithCount
{
  MessageDiffApply(){}

  explicit MessageDiffApply(const MessageDiff &diff):MessageDiffWithCount(diff){}

  bool Get()
  {
    if (_skipCount>=_skip.Size()) return true;
    return _skip.Get(_skipCount++);
  }


};




class NetworkMessageRaw;

//! Network message (internal representation)
struct NetworkMessage : public RefCount, public SerializeClass
{
  // network message header
  //! time when message was sent
  Time time;
  //! size of raw (serialized, compressed) message - not transferred, used for statistics
  int size;	

  //! Constructor
  NetworkMessage()
  {
    size = 0;
  }
  //! Destructor
  ~NetworkMessage() {}
  //! Type of message
  virtual NetworkMessageType GetNMType() const;
  //! Error calculation for server
  virtual float CalculateErrorStatic(NetworkMessage *with) = 0;
  //! Error calculation for server
  virtual float CalculateErrorDynamic(NetworkMessage *with, float dt) = 0;
  //! Error calculation for server
  virtual float CalculateErrorInitial() = 0;
  /// difference creation for server
  virtual NetworkMessage *Clone() const = 0;
  /// difference creation for server
  virtual void CreateDiff(const NetworkMessage *refMsg, MessageDiffCreate &diff) {}
  /// difference applied on the client
  virtual void ApplyDiff(const NetworkMessage *refMsg, MessageDiffApply &diff ) {}
  
  virtual void ResetDiff() {}
  virtual bool HasDiff() const {return false;}

  virtual void ProtectDiff() {}


  virtual void EncodeDiff(NetworkMessageRaw &dst, const NetworkMessageFormatBase *format) {}
  virtual void DecodeDiff(NetworkMessageRaw &dst, const NetworkMessageFormatBase *format) {}

  /// store diff (implemented in NetworkMessageNetworkObjectDiffed)
  virtual void StoreDiff(const MessageDiffCreate &diff) {}
  /// load diff (implemented in NetworkMessageNetworkObjectDiffed)
  virtual void LoadDiff(MessageDiffApply &diff) {}

  //! Return the size of the structure (to check structure runtime)
  virtual size_t GetStructureSize() const = 0;
  virtual bool IsSerializable() const
  {
    return false; //not serializable by default
  }
  //! Save in MP game should save NetworkServer::_initMessages
  virtual LSError Serialize(ParamArchive &ar)
  {
    // All possible init messages should overload this method
    Fail("Only selected NetworkMessages can be serialized!");
    return LSOK;
  }
  static NetworkMessage *CreateObject(ParamArchive &ar);
};

/** function is pure, but still derived classes may call base implementation */
inline float NetworkMessage::CalculateErrorStatic(NetworkMessage *with)
{
  return 0;
}
/** function is pure, but still derived classes may call base implementation */
inline float NetworkMessage::CalculateErrorDynamic(NetworkMessage *with, float dt)
{
  return 0;
}
/** function is pure, but still derived classes may call base implementation */
inline float NetworkMessage::CalculateErrorInitial()
{
  return 0;
}

/// default implementation for initial error on server
inline float NoErrorInitialFunc(NetworkMessage *msg)
{
  return FLT_MAX;
}

#include <Es/Strings/rString.hpp>

//! simple network object
/*!
- used as base class for guaranteed message structures
*/
class NetworkSimpleObject
{
public:
  //! Destructor
  virtual ~NetworkSimpleObject() {}
  //! Returns message type for this object and message class
  /*!
    \param cls messsage update class
  */
  virtual NetworkMessageType GetNMType(NetworkMessageClass cls = NMCCreate) const = 0;
  //! Create new network message for this object and message class
  /*!
  \param cls messsage update class
  */
  static NetworkMessageFormat &CreateFormat(
    NetworkMessageClass cls,
    NetworkMessageFormat &format
  ) {return format;}
  //! Message update from / to this object
  /*!
    \param ctx message context
  */
  virtual TMError TransferMsg(NetworkMessageContext &ctx) = 0;

  virtual Time GetLastSimulationTime() const;
};

/** function is pure, but still derived classes may call base implementation */
inline TMError NetworkSimpleObject::TransferMsg(NetworkMessageContext &ctx)
{
  return (TMError)0;
}

/// rename NetworkSimpleObject using common naming structure
typedef NetworkSimpleObject SimpleMessage;

/// rename NetworkMessage using common naming structure
typedef NetworkMessage NetworkMessageSimple;

struct CreatorId
{
private:
  int creatorId;

public:
  explicit CreatorId(int crt) : creatorId(crt) {}
  void SetCreator(int crt) { creatorId = crt; }
  int CreatorInt() const { return creatorId; }
  bool operator==(CreatorId id) const { return id.creatorId == creatorId; }
  bool operator!=(CreatorId id) const { return id.creatorId != creatorId; }

  CreatorId() {}

  LSError Serialize(ParamArchive &ar, const RStringB &name, int minVersion, int defValue=0)
  {
    return ar.Serialize(name, creatorId, minVersion, defValue);
  }
};

//! unique identifier of network objects
struct NetworkId
{
  //! Player id of client where network object was created
  CreatorId creator;
  //! Unique id of network object on given client
  OIDInt id;

	//! Constructor
	NetworkId() { creator.SetCreator(0); id = 0;}
	//! Constructor
	/*!
	\param c player id of client where network object was created
	\param i unique id of network object on given client
	*/
	NetworkId(CreatorId c, OIDInt i) {creator = c; id = i;}
	//! Assignement operator
	void operator =(const NetworkId &src)
	{
		creator = src.creator;
		id = src.id;
	}
	//! Equality operator
	bool operator ==(const NetworkId &with) const
	{
		return with.creator == creator && with.id == id;
	}
	//! Nonequality operator
	bool operator !=(const NetworkId &with) const
	{
		return with.creator != creator || with.id != id;
	}
	
	//! Null (unassigned) network id
	static NetworkId Null() {return NetworkId();}
	//! Check if id value is null (unassigned)
	bool IsNull() const {return creator == CreatorId(0);}

  LSError Serialize(ParamArchive &ar); //needed to serialize AutoArray of network objects
 
  //! Serialize for MP Save/Load
  LSError Serialize(ParamArchive &ar, int minVersion)
  {
    CHECK(creator.Serialize(ar, "creator", minVersion, 0))
      CHECK(ar.Serialize("id", id, minVersion, 0))
      return LSOK;
  }
  LSError Serialize(ParamArchive &ar, const char *name, int minVersion);
};
TypeIsMovable(NetworkId);

// NetworkId with Static object id serializable
struct NetworkIdAll: public NetworkId
{
  NetworkIdAll() : NetworkId() {}
  NetworkIdAll(NetworkId &id) : NetworkId(id.creator, id.id) {}
  NetworkIdAll(CreatorId c, int i) : NetworkId(c, i) {}

  //! Serialize for MP Save/Load
  LSError Serialize(ParamArchive &ar, int minVersion)
  {
    CHECK(creator.Serialize(ar, "creator", minVersion, 0))
    CHECK(ar.Serialize("id", id, minVersion, 0))
    return LSOK;
  }
  LSError Serialize(ParamArchive &ar, const char *name, int minVersion);
};
TypeIsMovable(NetworkIdAll);


class NetworkMessageDiffed: public NetworkMessage
{
  MessageDiff _diff;
#if _ENABLE_REPORT
  struct InitBoolNoCopy: public InitVal<bool,false>
  {
    InitBoolNoCopy(){}
    InitBoolNoCopy(const InitBoolNoCopy &other) {} // we want no copy in a copy constructor (used in a Clone)
    const InitVal & operator = (bool newValue) {InitVal<bool,false>::operator =(newValue);return *this;}
  } _diffLocked;
#endif

public:
  virtual void EncodeDiff(NetworkMessageRaw &raw, const NetworkMessageFormatBase *format) override;
  virtual void DecodeDiff(NetworkMessageRaw &raw, const NetworkMessageFormatBase *format) override;

  virtual void StoreDiff(const MessageDiffCreate &diff) override;
  virtual void LoadDiff(MessageDiffApply &diff) override;

  virtual void ResetDiff() override {_diff = MessageDiff();}
  virtual bool HasDiff() const override {return !_diff.IsEmpty();}

  virtual void ProtectDiff() override;
};

class NetworkMessageWithSimulationTime : public NetworkMessageDiffed
{
public:
  Time  _simulationTime;
};

#define NETWORK_OBJECT_MSG(MessageName, XX) \
  XX(MessageName, CreatorId, objectCreator, NDTInteger, CreatorId, NCTSmallUnsigned, DEFVALUE(int, 0), DOC_MSG("ID of client, which created this object"), TRANSF) \
  XX(MessageName, OIDInt, objectId, NDTOID, OIDInt, NOIDIntComp, DEFVALUE(OIDInt, 0), DOC_MSG("Unique (for client) ID of object"), TRANSF) \
  XX(MessageName, Vector3, objectPosition, NDTVector, Vector3, NCTVectorPositionCamera, DEFVALUE(Vector3, VZero), DOC_MSG("Current position of object"), TRANSF) \
  XX(MessageName, bool, guaranteed, NDTBool, bool, NCTNone, DEFVALUE(bool, false), DOC_MSG("Message is guaranteed (must be delivered)"), TRANSF)

DECLARE_NET_INDICES_NO_NMT(NetworkObject, NETWORK_OBJECT_MSG, NetworkMessageWithSimulationTime)



#include <El/Time/time.hpp>

//! updateable network object
/*!
  Base class for all objects with automatic update over network in multiplayer game
*/
class NetMessage;

class SerializedObject : public RemoveOLinks
{
  public:
  // soft link support
  //! id type
  typedef ObjectId LinkId;
  typedef ObjectIdAutoDestroy LinkIdStorage;

  //! load object that was unloaded
  static SerializedObject *RestoreLink(const LinkId &id);
  static bool RequestRestoreLink(const LinkId &id, bool noRequest=false);

  static LinkId GetNullId();

  //! get unique ID (for unloading purposed)
  virtual LinkId GetLinkId() const {return GetNullId();}

  //
  template <class Type,class Traits>
  static LSError LoadPtrRef(ParamArchive &ar, Type * &link)
  {
    link = dynamic_cast<Type *>(Type::LoadRef(ar));
    return LSOK;
  }
  template <class Type,class Traits>
  static LSError SavePtrRef(ParamArchive &ar,  Type * &link)
  {
    return link->SaveRef(ar);
  }

  //
  template <class Type>
  static LSError LoadRefRef(ParamArchive &ar, Ref<Type> &link)
  {
    link = dynamic_cast<Type *>(Type::LoadRef(ar));
    return LSOK;
  }
  template <class Type>
  static LSError SaveRefRef(ParamArchive &ar, Ref<Type> &link)
  {
    return link->SaveRef(ar);
  }

  //
  template <class Type>
  static LSError LoadLLinkRef(ParamArchive &ar, LLink<Type> &link)
  {
    link = dynamic_cast<Type *>(Type::LoadRef(ar));
    return LSOK;
  }
  template <class Type,class Traits>
  static LSError SaveLLinkRef(ParamArchive &ar, LLink<Type> &link)
  {
    return link->SaveRef(ar);
  }

  //
  template <class Type,class Traits>
  static LSError LoadLinkRef(ParamArchive &ar, SoftLinkPerm<Type,Traits> &link)
  {
    link = dynamic_cast<Type *>(Type::LoadRef(ar));
    return LSOK;
  }
  template <class Type,class Traits>
  static LSError SaveLinkRef(ParamArchive &ar,  SoftLinkPerm<Type,Traits> &link)
  {
    return link->SaveRef(ar);
  }

  //
  template <class Type, class Traits>
  static LSError LoadLinkRef(ParamArchive &ar, SoftLink<Type,Traits> &link);
  template <class Type, class Traits>
  static LSError SaveLinkRef(ParamArchive &ar,  SoftLink<Type,Traits> &link);

  //! lock -- disable unloading
  virtual void LockLink() const {}
  //! unlock -- allow unloading
  virtual void UnlockLink() const {}

  //! check if object can be unloaded
  virtual bool NeedsRestoring() const {return false;}

  //! return team member interface
  virtual AITeamMember *GetTeamMember() {return NULL;}

};


class NetworkObject : public SerializedObject, public NetworkSimpleObject
{
protected:
  #if _ENABLE_CHEATS
  //! time when last position update from server arrived
  Time _lastUpdateTime[NMCUpdateN];
  #endif

  //! time when prediction should stop (calculated by GetMaxPredictionTime() function)
  Time _maxPredictionTime;

#if _EXT_CTRL
  int _externalController; //by HLA or AAR, see enum in hla_base.hpp
#endif

public:
  NetworkObject();
  ~NetworkObject();

  //! All updates depends on this message (to be handled after)
  RefR<NetMessage, RefCountSafe> _fence;
#if _EXT_CTRL
  void SetExternalController(int controller = 1) {_externalController = controller;}
  int GetExternalController() const {return _externalController;}
  bool IsExternalControlled() const {return (_externalController > 0);}
  bool AllowExternalControl(int ctrl) const {return _externalController == ctrl;}
#endif

  //! Set unique id of network object
  virtual void SetNetworkId(NetworkId &id) = 0;
  //! Return unique id of network object
  virtual NetworkId GetNetworkId() const = 0;
  //! Returns current position of this object
  virtual Vector3 GetCurrentPosition() const = 0;
  //! Returns speaker position for this object
  virtual Vector3 GetSpeakerPosition() const {return GetCurrentPosition();}
  //! Enable / disable random lip movement (enabled during direct speaking chat)
  virtual void SetRandomLip(bool set = true) {}
  //! Return if object is local on this client
  virtual bool IsLocal() const = 0;
  //! Set if object is local on this client
  virtual void SetLocal(bool local = true) = 0;
  //! Destroy (remote) object
  virtual void DestroyObject() = 0;

  //! Get debugging name (debugging only)
  virtual RString GetDebugName() const {return RString();}

  //! The object is no longer in any usage and can be deleted (used for ClientCameraPositionObject)
  virtual bool IsObsolete() const { return false; }

  //! How long is client side prediction allowed to predict this object
  virtual float GetMaxPredictionTime() const;

  #if _ENABLE_CHEATS
  float GetLastPositionUpdateAge() const;
  float GetLastUpdateAge(NetworkMessageClass cls) const;
  #endif

  //! Check if prediction should be frozen
  virtual bool CheckPredictionFrozen() const;

  virtual float UpdateDistance() const {return FLT_MAX;}

  static NetworkMessageFormat &CreateFormat
  (
    NetworkMessageClass cls,
    NetworkMessageFormat &format
  );
  virtual TMError TransferMsg(NetworkMessageContext &ctx) = 0;

  /// used when create message is transmitted for an existing object
  virtual bool OnSendCreateMessage(NetworkMessageContext &ctx) {return true;}

  virtual bool RejectMsg(NetworkMessageClass cls, NetworkMessage *msg) {return false;}

  //! Calculate difference between given message and current state
  /*!
    \param ctx context for given network message
    \return calculated error (difference)
  */
  virtual float CalculateError(NetworkMessageContextWithError &ctx);

  virtual bool CheckSendUpdateToOwner(NetworkMessageClass cls) {return false;}
  virtual bool CheckSendUpdateToServer(NetworkMessageClass cls) {return true;}

  //! Calculate coeficient error must be multiplied by
  /*!
    - coeficient is 1 for near objects, for distant objects decrease to zero
    \param position current position of object
    \param cameraPosition camera position on receiving client
  */
  virtual float CalculateErrorCoef(Vector3Par position, Vector3Par cameraPosition, float maxDistance, bool updating) const;
  //! Calculate difference between current and initial state
  virtual float CalculateErrorInitial(NetworkMessageClass cls)
  {
    return FLT_MAX;
  }
  
  //! check if object state is good
  virtual bool CheckIntegrity() const;

  virtual void ForEachChildren(const std::function<bool(const NetworkObject *)> &func) const {}

  USE_CASTING_ROOT;
};

NetworkObject *NetIdToNetObj(const char *netId);
RString NetObjToNetId(NetworkObject *obj);

inline RString NetworkIdToNetId(const NetworkId &id)
{
  return Format("%d:%"OID_FORMAT"", id.creator.CreatorInt(), id.id);
}

// Data structure for transfer of the list of variables
struct IVariables
{
  virtual GameValue GetVariableValue(const RString &name) const = 0;

  float CalculateError(EntityAI *veh, const IVariables &vars2) const;
};


struct Variables: public IVariables
{
  struct Variable
  {
    RString _name;
    AutoArray<char> _value;
    ClassIsMovable(Variable)
  };

  NetworkId _object;
  AutoArray<Variable> _variables;

  float CalculateError(const Variables & with) const;
  TMError Save(EntityAI *value);
  TMError Load(EntityAI *value) const;

  const Variable *FindVariable(const RString &name) const;

  GameValue GetVariableValue(const RString &name) const override;

};

#endif
