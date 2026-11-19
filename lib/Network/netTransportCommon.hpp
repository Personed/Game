/**
  @file   netTransportCommon.cpp
  @brief  Common implementation parts of netTransportNet.cpp and netTransportXbox.cpp.

  Must be included at the end of these files.
  Copyright &copy; 2003-2004 by BIStudio (www.bistudio.com)
  @author PE
  @date   15.4.2004
*/

//---------------------------------------------------------------------------
//  support:

#if _GAMES_FOR_WINDOWS
# include "../saveGame.hpp"
#include "../soundXA2.hpp"
#endif

void decodeURLAddress ( RString address, RString &ip, int &port )
{
  const char *ptr = strrchr(address,':');
  if ( !ptr )
  {
    ip = address;
    return;
  }
  ip = address.Substring(0,ptr-address);
  port = atoi(ptr+1);
}

#if VOICE_OVER_NET

#  if _GAMES_FOR_WINDOWS

void assertVON ()
{
  if ( von ) return;
#ifdef NET_LOG_VOICE
  NetLog("assertVON: creating VoNSystemXbox instance, inPort=%u, outPort=%u",
         (unsigned)commInPort,(unsigned)commOutPort);
#endif

  // sound-system using Xbox SDK
  von = new VoNSystemXbox( (dynamic_cast<SoundSystem8*>(GSoundsys))->DirectSound(), commInPort, commOutPort );
  Assert( von );
}

/// {in|out}Port == -1 if communicator is disconnected (or output should sound on TV)
void setCommunicatorPort ( DWORD inPort, DWORD outPort, bool disablePlayback )
{
  commInPort = inPort;
  commOutPort = outPort;
  if ( von )
    von->changePort(inPort,outPort,disablePlayback);
}

#  else

/*!
\patch 5088 Date 11/15/2006 by Bebul
- Fixed: Dedicated server can retranslate Voice Over Net packets
*/

class VoNSystemRetranslate : public VoNSystem
{
public:
  virtual RefD<VoNSoundBuffer> getSoundBuffer ( VoNChannelId chId )
  {
    ErrF("VoNSystemRetranslate::getSoundBuffer called, which should be avoided");
    return NULL;
  }
  virtual bool RetranslateOnly() { return true; }
};
#if _ENABLE_VON
void assertVON ()
{
  if ( von ) return;
#ifdef NET_LOG_VOICE
  NetLog("assertVON: creating VoNSystemDS instance");
#endif
  extern bool IsDedicatedServer();
  if (IsDedicatedServer())
  {
    // dummy sound-system which can be used only for retlanslation (on dedicated server)
    von = new VoNSystemRetranslate();
  }
  else
  {
    SoundSystemXA2 *ssXA2 = dynamic_cast<SoundSystemXA2*>(GSoundsys);
    if (ssXA2)
    {
      von = new VoNSystemXA2( ssXA2->XA2Device() );
    }
  }
  Assert( von );
}
#else
void assertVON ()
{
  if ( von ) return;
  // dummy sound-system which can be used only for retlanslation (on dedicated server)
  von = new VoNSystemRetranslate();
  Assert( von );
}
#endif //_ENABLE_VON

#  endif

#else

void NetClient::SetVoiceTransmition(bool val)
{
}

void NetClient::SetVoiceToggleOn(bool val)
{
}

void NetClient::SetVoNRecThreshold(float val)
{
}
void NetClient::SetVoNTransmitterFreq(float f)
{
}
float NetClient::GetVoNTransmitterFreq() const {return 0;}
void NetClient::SetVoNTransmitterRange(float range){}
float NetClient::GetVoNTransmitterRange2() const { return 1000000.f;}

#endif  // #if VOICE_OVER_NET

NetPool *getPool ()
  // should be called inside of poolLock.enter()
{
  createPool();
  return pool;
}

/// Sets actual enumeration instance (can be NULL).
void setEnum ( NetSessionEnum *_en )
{
  poolLock.enter();
  _enum = _en;
  if ( _en )
  {
    getPool();
    GET_ENUM_PEER();
  }
  poolLock.leave();
}

#ifdef NET_LOG

static unsigned GetClientPeerId()
{
  NetPeer *peer = GET_CLIENT_PEER();
  return peer ? peer->getPeerId() : 0;
}

#endif

/// Sets actual client instance (can be NULL).
void setClient ( NetClient *_cl )
{
  poolLock.enter();
  _client = _cl;
  if ( _cl )
  {
    getPool();
    GET_CLIENT_PEER();
  }
  poolLock.leave();
}

/// Sets actual server instance (can be NULL).
void setServer ( NetServer *_srv )
{
  poolLock.enter();
  _server = _srv;
  if ( _srv )
  {
    getPool();
#if 1
    GET_ENUM_PEER();
#endif
    GET_SERVER_PEER();
  }
  poolLock.leave();
}

SOCKET GetServerSocket()
{
  NetPeer *peer = GET_SERVER_PEER();
  return peer ? peer->GetSocket() : INVALID_SOCKET;
}
//---------------------------------------------------------------------------
//  network-parameters (stored in flashpoint.cfg):

void LoadNetworkParams ( NetworkParams &data, ParamEntryPar cfg )
{
  data = defaultNetworkParams;
  if ( cfg.FindEntry("sockets") )
  {
    ParamEntryVal c = cfg>>"sockets";

#define GET_PAR(x) if (c.FindEntry(#x)) data.x = c>>#x
#define GET_PAR_U(x) if (c.FindEntry(#x)) data.x = (int)(c>>#x)

    GET_PAR(winsockVersion);
    GET_PAR_U(rcvBufSize);
    GET_PAR_U(maxPacketSize);
    GET_PAR_U(dropGap);
    GET_PAR_U(ackTimeoutA);                   ///< Multiplicative coefficient for actual ack-timeout computation.
    GET_PAR_U(ackTimeoutB);                   ///< Additive coefficient for actual ack-timeout computation (in microseconds).
    GET_PAR_U(ackRedundancy);
    GET_PAR_U(initBandwidth);
    GET_PAR_U(minBandwidth);
    GET_PAR_U(maxBandwidth);
    GET_PAR_U(minActivity);
    GET_PAR_U(initLatency);
    GET_PAR_U(minLatencyUpdate);
    GET_PAR(minLatencyMul);
    GET_PAR_U(minLatencyAdd);
    GET_PAR(goodAckBandFade);
    GET_PAR_U(outWindow);
    GET_PAR_U(ackWindow);
    GET_PAR_U(maxChannelBitMask);
    GET_PAR(lostLatencyMul);
    GET_PAR(lostLatencyAdd);
    GET_PAR_U(maxOutputAckMask);
    GET_PAR_U(minAckHistory);
    GET_PAR(maxDropouts);
    GET_PAR(midDropouts);
    GET_PAR(okDropouts);
    GET_PAR(minDropouts);
    GET_PAR(latencyOverMul);
    GET_PAR(latencyOverAdd);
    GET_PAR(latencyWorseMul);
    GET_PAR(latencyWorseAdd);
    GET_PAR(latencyOkMul);
    GET_PAR(latencyOkAdd);
    GET_PAR(latencyBestMul);
    GET_PAR(latencyBestAdd);
    GET_PAR(maxBandOverGood);
    GET_PAR(safeMaxBandOverGood);

    const int nGrowEntries = 2 * MAX_GROW_STATE + 2;
    ConstParamEntryPtr gcc = c.FindEntry("grow");
    if ( gcc && gcc->GetSize() == nGrowEntries*2 )
    {
      ParamEntryVal gc = *gcc;
      for (int i=0; i<nGrowEntries; i++)
      {
        data.grow[i].mul = gc[i*2];
        data.grow[i].add = gc[i*2+1];
      }
    }
#undef GET_PAR
#undef GET_PAR_U
  }
#ifdef NET_LOG
  static bool printFirst = true;
  if ( printFirst )
  {
    printFirst = false;
    char buf[264] = "Par: ";
    data.printParams1(buf+5);
    NetLog(buf);
    data.printParams2(buf+5);
    NetLog(buf);
    data.printParams3(buf+5);
    NetLog(buf);
  }
#endif
}

void VoiceTransmitTargets(const TransmitTarget *targets, int nAddr)
{
  #if VOICE_OVER_NET
  if (!von) return;
  RefD<VoNClient> vcl = von->getClient();
  if ( !vcl ) return;
  vcl->setTransmitTargets(targets,nAddr);
  #endif
}

void VoiceAllTargets(const TransmitTarget *targets, int nAddr)
{
  #if VOICE_OVER_NET
  if (!von) return;
  RefD<VoNClient> vcl = von->getClient();
  if ( !vcl ) return;
  vcl->setAllTargets(targets,nAddr);
  #endif
}

/// when port address was changed in VoN layer, change it also in PlayerIdentity
void ProcessPeerChanged(PeerChangedCallback proc, void *context)
{
#if VOICE_OVER_NET
  if (!von) return;
  RefD<VoNClient> vcl = von->getClient();
  if ( !vcl ) return;
  vcl->ProcessPeerChanged(proc, context);
#endif
}

RString GetVoiceTargetDiagnostics(int dpid)
{
#if VOICE_OVER_NET
  do {
    if (!von) break;
    RefD<VoNClient> vcl = von->getClient();
    if ( !vcl ) break;
    return vcl->GetTargetDiagnostics(dpid);
  } while (false);
  return RString("");
#else
  return RString("");
#endif
}

void GetVoiceCliquesDiagnostics(QOStrStream &out, Array<ConvPair> &convTable)
{
#if VOICE_OVER_NET
  if (!von) return;
  RefD<VoNClient> vcl = von->getClient();
  if ( !vcl ) return;
  return vcl->GetVoiceCliquesDiagnostics(out, convTable);
#endif
}

/// start/stop VoN KeepAlive system (sending probes,response,keepAlive packets)
void VoiceStartKeepAlive(bool start, int dpid)
{
#if VOICE_OVER_NET
  if (!von) return;
  RefD<VoNClient> vcl = von->getClient();
  if ( !vcl ) return;
  vcl->StartKeepAlive(start, dpid);
#endif
}

void VoiceSetChatChannel(VoNChatChannel chatChan)
{
#if VOICE_OVER_NET
  if (!von) return;
  RefD<VoNClient> vcl = von->getClient();
  if ( !vcl ) return;
  vcl->setChatChannel(chatChan);
#endif
}

void WhatUnitsVoNDirect(AutoArray<int> &units, Vector3Par pos, float maxDist)
{
#if VOICE_OVER_NET
  if ( !von ) return;
  von->WhatUnitsVoNDirect(units, pos, maxDist);
#endif
}

#if _GAMES_FOR_WINDOWS
void VoiceRegisterLocalTalker(int userIndex)
{
#if VOICE_OVER_NET
  if (!von) return;
  VoNClient *client = von->getClient();
  if (!client) return;
  client->RegisterLocalTalker(userIndex);
#endif
}

void VoiceUnregisterLocalTalker(int userIndex)
{
#if VOICE_OVER_NET
  if (!von) return;
  VoNClient *client = von->getClient();
  if (!client) return;
  client->UnregisterLocalTalker(userIndex);
#endif
}

void VoiceRegisterRemoteTalker(XUID xuid)
{
#if VOICE_OVER_NET
  if (!von) return;
  VoNClient *client = von->getClient();
  if (!client) return;
  client->RegisterRemoteTalker(xuid);
#endif
}

void VoiceUnregisterRemoteTalker(XUID xuid)
{
#if VOICE_OVER_NET
  if (!von) return;
  VoNClient *client = von->getClient();
  if (!client) return;
  client->UnregisterRemoteTalker(xuid);
#endif
}

#endif //#if _GAMES_FOR_WINDOWS

NetTranspSessionEnum *CreateNetSessionEnum ( ParamEntryPar cfg )
{
  NetworkParams data;
  LoadNetworkParams(data,cfg);
  NetChannelBasic::setGlobalNetworkParams(data);
  return new NetSessionEnum;
}

NetTranspClient *CreateNetClient ( ParamEntryPar cfg )
{
  NetworkParams data;
  LoadNetworkParams(data,cfg);
  NetChannelBasic::setGlobalNetworkParams(data);
  return new NetClient;
}

NetTranspServer *CreateNetServer ( ParamEntryPar cfg )
{
  NetworkParams data;
  LoadNetworkParams(data,cfg);
  NetChannelBasic::setGlobalNetworkParams(data);
  return new NetServer;
}

Ref<NetMessage> mergeMessageList ( NetMessage *msg )
{
  if ( !msg ) return NULL;
  unsigned size = 0;
  NetMessage *ptr = msg;
  NetMessage *last;
  do
  {
    size += ptr->getLength();
    last = ptr;
  } while ( (ptr = ptr->next) );
  Ref<NetMessage> composite = NetMessagePool::pool()->newMessage(size,msg->getChannel());
  if ( !composite ) return NULL;
  composite->setFrom(last);
  unsigned8 *data = (unsigned8*)composite->getData();
  for ( ptr = msg; ptr; ptr = ptr->next )
  {
    memcpy(data,ptr->getData(),ptr->getLength());
    data += ptr->getLength();
  }
  composite->setLength(size);
  return composite;
}

//---------------------------------------------------------------------------
//  VoiceServer:

#if VOICE_OVER_NET

class VoiceServer : public VoNServer
{

public:

  VoiceServer ( VoNSystem *von ) : VoNServer(von)
  {}

  /**
    External mapping (VoNIdentity -> NetChannel) is needed for packet-routing.
    This mapping must interpret identities < ExtReservedIds as local connection.
  */
  virtual RefD<NetChannel> netChannelFromIdentity ( VoNIdentity id );

};

RefD<NetChannel> VoiceServer::netChannelFromIdentity ( VoNIdentity id )
{
  Assert( m_von );
  if ( id < RESERVED_IDS )                  // local client -> use DirectChannel
  {
    m_von->localConnect();                  // establish local channels (if not set yet)
    return m_von->getServerChannel().GetRef();
  }                                         // return the server's local channel
  Assert( _server );
  return _server->playerToChannel(id);
}

#endif

//---------------------------------------------------------------------------
//  Receiving routines:

/// Regular data received by the NetServer...
NetStatus serverReceive ( NetMessage *msgPtr, NetStatus event, void *data )
{
  // Store msgPtr into Ref to prevent possible garbage collect from different thread
  // Note: it can happen for multipacket message, which is merged by
  //      msg = mergeMessageList(sup->m_splitUrgent);
  //   and this msg = NetMessagePool::pool()->newMessage can be garbage collected even before _server->insertReceived(msg), as its RefCount==1
  Ref<NetMessage> msg = msgPtr;
  // thread-safe
  if ( !_server || !msg ) return nsNoMoreCallbacks; // nothing to do
  unsigned len = msg->getLength();
  if ( !len ) return nsNoMoreCallbacks;     // message is too small
  struct sockaddr_in dist;
  msg->getDistant(dist);
  unsigned flags = msg->getFlags();
#if VOICE_OVER_NET
  if ( flags & MSG_VOICE_FLAG )             // voice-messages aren't routed yet => ignore them
    return nsNoMoreCallbacks;
#endif
  bool isMagic = (flags & MSG_MAGIC_FLAG) != 0;
  if ( isMagic )                            // magic-messages:
  {
    if ( len < 4 )
      return nsNoMoreCallbacks;             // magic-message is too small
    unsigned32 magic = *(unsigned32*)msg->getData();

#ifdef NET_LOG_CTRL_RECEIVE
#  ifdef NET_LOG_BRIEF
    NetLog("Pe(%u):rCtrlG(%u.%u.%u.%u:%u,%u,%x)",
           GetServerPeerId(),
           (unsigned)IP4(dist),(unsigned)IP3(dist),(unsigned)IP2(dist),(unsigned)IP1(dist),(unsigned)PORT(dist),
           msg->getLength(),magic);
#  else
    NetLog("Peer(%u)::serverReceive: received control message (from=%u.%u.%u.%u:%u, len=%3u, serial=%4u, flags=%04x, magic=%x)",
           GetServerPeerId(),
           (unsigned)IP4(dist),(unsigned)IP3(dist),(unsigned)IP2(dist),(unsigned)IP1(dist),(unsigned)PORT(dist),
           msg->getLength(),msg->getSerial(),flags,magic);
#  endif
#endif

    switch ( magic )
    {

      case MAGIC_DESTROY_PLAYER:            // the player itself is disconnecting..
      {
        _server->enterUsr();                // due to 'channelToPlayer()'
        int player = _server->channelToPlayer(msg->getChannel());
        if ( player < 0 )
          RptF("No player found for channel %u - MAGIC_DESTROY_PLAYER message ignored",(unsigned)msg->getChannel());
        else
          _server->finishDestroyPlayer(player);
        _server->leaveUsr();
        break;                              // don't need to lock this..
      }

    }

    return nsNoMoreCallbacks;               // unknown message => don't care
  }

    // process regular (user) message:
#ifdef NET_LOG_SERVER_RECEIVE
  NetLog("Peer(%u)::serverReceive: received user message (from=%u.%u.%u.%u:%u, len=%3u, serial=%4u, flags=%04x, ID=%x)",
         GetServerPeerId(),
         (unsigned)IP4(dist),(unsigned)IP3(dist),(unsigned)IP2(dist),(unsigned)IP1(dist),(unsigned)PORT(dist),
         len,msg->getSerial(),flags,msg->id);
#endif
  _server->enterRcv();
    // merge partial messages:
  if ( flags & MSG_PART_FLAG )
  {
    unsigned64 addrKey = sockaddrKey(dist); // key to m_support
    ChannelSupport *sup = _server->m_support.get(addrKey);
    Assert( sup );
    bool closing = (flags & MSG_CLOSING_FLAG) > 0;
    if ( flags & MSG_URGENT_FLAG )
      if ( closing )                        // the last message-part to be merged..
      {
        DoAssert( sup->m_splitUrgent.NotNull() );
        sup->m_lastSplitUrgent->next = msg;
        msg->next = NULL;
        msg = mergeMessageList(sup->m_splitUrgent);
        sup->m_splitUrgent = NULL;
      }
      else                                  // another message-part => remember it!
      {
        if ( !sup->m_splitUrgent )
          sup->m_splitUrgent = msg;
        else
          sup->m_lastSplitUrgent->next = msg;
        (sup->m_lastSplitUrgent = msg)->next = NULL;
        _server->leaveRcv();
        return nsNoMoreCallbacks;
      }
    else
      if ( closing )                        // the last message-part to be merged..
      {
        DoAssert( sup->m_split.NotNull() );
        sup->m_lastSplit->next = msg;
        msg->next = NULL;
        msg = mergeMessageList(sup->m_split);  //<- note, msg must be Ref to prevent possible garbage collect from different thread
        sup->m_split = NULL;
      }
      else                                  // another message-part => remember it!
      {
        if ( !sup->m_split )
          sup->m_split = msg;
        else
          sup->m_lastSplit->next = msg;
        (sup->m_lastSplit = msg)->next = NULL;
        _server->leaveRcv();
        return nsNoMoreCallbacks;
      }
    if (msg) LogF("serverReceive: merging user message (len=%3u, serial=%4u, flags=%04x, ID=%x)",
       msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id);
#ifdef NET_LOG_MERGE
#  ifdef NET_LOG_BRIEF
    if (msg) NetLog("Pe(%u):rMerS(%u,%u,%x,%x)",
           GetServerPeerId(),msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id);
#  else
    if (msg) NetLog("Peer(%u)::serverReceive: merging user message (len=%3u, serial=%4u, flags=%04x, ID=%x)",
           GetServerPeerId(),msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id);
#  endif
#endif
  }

  _server->insertReceived(msg);

  _server->leaveRcv();

  return nsNoMoreCallbacks;
}

//---------------------------------------------------------------------------
//  NetSessionDescriptions:

int NetSessionDescriptions::Add ()
{
  if ( _size >= MAX_SESSIONS ) return -1;
  return _size++;
}

void NetSessionDescriptions::Delete ( int i )
{
  _size--;
  for ( int j = i; j < _size; j++ )
    _data[j] = _data[j + 1];
}

void NetSessionDescriptions::Clear ()
{
  _size = 0;
}

//---------------------------------------------------------------------------
//  NetSessionEnum:

NetSessionEnum::NetSessionEnum ()
  : LockInit(_cs,"NetSessionEnum::_cs",true)
{
  _running = false;
  magicApp = 0;
  setEnum(this);
#ifdef NET_LOG_SESSION_ENUM
  NetLog("Peer(%u): creating NetSessionEnum instance",GET_ENUM_PEER()->getPeerId());
#endif
}

NetSessionEnum::~NetSessionEnum ()
{
  setEnum(NULL);
  Done();
#ifdef NET_LOG_SESSION_ENUM
  NetLog("Peer(%u): destroying NetSessionEnum instance [%08x]",GET_ENUM_PEER()->getPeerId());
#endif
//  checkPool();
}

RString NetSessionEnum::IPToGUID ( RString ip, int port )
{
  char buffer[256];
  sprintf(buffer,"%s:%d",(const char*)ip,port);
  return buffer;
}

bool NetSessionEnum::Init ( int magic )
{
#ifdef NET_LOG_SESSION_ENUM
  NetLog("Peer(%u): NetSessionEnum::Init(%d)",
         GET_ENUM_PEER()->getPeerId(),magic);
#endif
  magicApp = magic;
  setEnum(this);
  return true;
}

void NetSessionEnum::Done ()
{
  enter();
  StopEnumHosts();
  _sessions.Clear();
  leave();
}

bool NetSessionEnum::StartEnumHosts ( RString ip, int port, AutoArray<RemoteHostAddress> *hosts )
{                                           // don't need to lock this..
  _running = true;
  bool anything = false;
  setEnum(this);
#ifdef NET_LOG_START_ENUM
  NetLog("Peer(%u): NetSessionEnum::StartEnumHosts(%s,%u,%d)",
         GET_ENUM_PEER()->getPeerId(),(const char*)ip,(unsigned)port,hosts?hosts->Size():0);
#endif
  NetPeer *peer = GET_ENUM_PEER();

  if (!peer)
  {
    RptF("Error: createPeer failed");
    return false;
  }

  NetChannel *br = peer->getBroadcastChannel();
  Assert( br );

  RString ipaddr;
  struct sockaddr_in addr;

  if ( !ip.GetLength() )
  {                                         // if "ip" is not defined, try LAN broadcast first ..
    getHostAddress(addr,NULL,port);
    if ( needsRequest(addr,port) )
    {
      sendRequest(br,addr,port);            // broadcast
      anything = true;
    }
    getLocalAddress(addr,port);
    if ( needsRequest(addr,port) )
    {
      sendRequest(br,addr,port);            // localhost (for XP)
      anything = true;
    }
  }
  else
  {
    decodeURLAddress(ip,ipaddr,port);
    if ( getHostAddress(addr,ipaddr,port) && // .. else try explicit IP address
         needsRequest(addr,port) )
    {
      sendRequest(br,addr,port);
      anything = true;
    }
  }

  int i;
  if ( hosts )                            // .. and finally try all addresses from the given list
    for ( i = 0; _running && i < hosts->Size(); i++ )
    {
      const RemoteHostAddress &address = hosts->Get(i);
      port = address.port;
      decodeURLAddress(address.ip,ipaddr,port);
      if ( getHostAddress(addr,ipaddr,port) &&
           needsRequest(addr,port) )
      {
        sendRequest(br,addr,port);
        anything = true;
      }
    }


  return anything;
}

void NetSessionEnum::StopEnumHosts ()
{
#ifdef NET_LOG_STOP_ENUM
  NetLog("Peer(%u): NetSessionEnum::StopEnumHosts",GET_ENUM_PEER()->getPeerId());
#endif
  _running = false;
}

int NetSessionEnum::NSessions ()
{
  enter();
  int n = _sessions.Size();
  leave();
#ifdef NET_LOG_NSESSIONS
  NetLog("Peer(%u): NetSessionEnum::NSessions: N=%d",GET_ENUM_PEER()->getPeerId(),n);
#endif
  return n;
}

void NetSessionEnum::GetSessions ( AutoArray<SessionInfo> &sessions )
{
  enter();
//  LogF("GetSessions: Current time %d", GetTickCount());

  int i;
  int n = _sessions.Size();
  for ( i = 0; i < n; )
  {
    const NetSessionDescription &desc = _sessions[i];
    unsigned32 age = (unsigned32)(getSystemTime() / 1000) - desc.lastTime;
    if ( age > MAX_ENUM_AGE )               // message age in milliseconds
    {
//      LogF("Session %s deleted - age %d", desc.address, age);
      _sessions.Delete(i);
      n--;
    }
    else
      i++;
  }

  sessions.Resize(n);
  for ( i = 0; i < n; i++ )
  {
    const NetSessionDescription &src = _sessions[i];
    SessionInfo &dst = sessions[i];
    dst.guid               = src.address;
    dst.name               = src.name;
    dst.lastTime           = GetTickCount() - ((unsigned32)(getSystemTime() / 1000) - src.lastTime);
//    LogF("Session %s listed - age %d, last time %d",src.address,(unsigned32)(getSystemTime() / 1000) - src.lastTime, dst.lastTime);
    dst.password           = (src.password & 2)!=0;
    dst.lock               = (src.password & 1)!=0;
    dst.actualVersion      = src.actualVersion;
    dst.requiredVersion    = src.requiredVersion;
    dst.badActualVersion   = false;
    dst.badRequiredVersion = false;
    dst.badSignatures      = false;
    dst.badAdditionalMods  = false;
    dst.serverState        = src.serverState;
    dst.gameType           = src.gameType;
    dst.mission            = src.GetLocalizedMissionName();
    dst.island             = src.island;
    dst.playersPublic      = src.numPlayers;
    dst.slotsPublic        = src.maxPlayers;
    dst.ping               = src.pingTime;
    dst.platform           = " ";

    dst.mod = src.mod;
    dst.equalModRequired = (src.equalModRequired & 1) != 0;
    dst.badMod = false;

    // TODO:
    dst.dedicated = false;
    dst.language = src.language;
    dst.difficulty = src.difficulty;
    dst.timeleft = src.timeleft;

    dst.country = RString();
    dst.battleEye = false;
  }

  leave();
}

//---------------------------------------------------------------------------
//  NetClient:

NetClient::NetClient ()
    : LockInit(sndCs,"NetClient::sndCs",true),
      LockInit(rcvCs,"NetClient::rcvCs",true)
{
    // Message times:
  lastMsgReported = getSystemTime();
    // NetChannel: not set yet
  channel = NULL;
    // to be sure:
  received = NULL;
  split = lastSplit = NULL;
  splitUrgent = lastSplitUrgent = NULL;
  sent = NULL;
  sessionTerminated = false;
  whySessionTerminated = NTROther;
  magicApp = 0;
  playerNo = EXT_BOT_CLIENT;
#ifdef NET_LOG_INFO
  oldLatency = -1;
  oldThroughput = -1;
  printAge = false;
#endif
#ifdef NET_LOG_TRANSP_STAT
  nextStatLog = lastMsgReported;
#endif
  setClient(this);
#ifdef NET_LOG_CLIENT
#  ifdef NET_LOG_BRIEF
  NetLog("Pe(%u):cli",GetClientPeerId());
#  else
  NetLog("Peer(%u): creating NetClient instance",GetClientPeerId());
#  endif
#endif
}

void NetClient::sendDisconnectMsg()
{
  enterSnd();
  if ( !sessionTerminated )                 // I must notify my server..
  {
    sessionTerminated = true;
    whySessionTerminated = NTRDisconnected;
      // send MAGIC_DESTROY_PLAYER message to server:
    if ( channel )
    {
      Ref<NetMessage> out = NetMessagePool::pool()->newMessage(4,channel);
      if ( out )
      {
        unsigned32 magic = MAGIC_DESTROY_PLAYER;
        out->setFlags(MSG_ALL_FLAGS,MSG_MAGIC_FLAG);
        out->setData((unsigned8*)&magic,4);
        out->send(true);
        SLEEP_MS(DESTRUCT_WAIT);
      }
    }
  }
  leaveSnd();
}

NetClient::~NetClient ()
{
#if VOICE_OVER_NET
  if ( von ) von->setClient(NULL);
#endif
  enterSnd();
  if ( !sessionTerminated )                 // I must notify my server..
  {
    sessionTerminated = true;
    whySessionTerminated = NTRDisconnected;
      // send MAGIC_DESTROY_PLAYER message to server:
    if ( channel )
    {
      Ref<NetMessage> out = NetMessagePool::pool()->newMessage(4,channel);
      if ( out )
      {
        unsigned32 magic = MAGIC_DESTROY_PLAYER;
        out->setFlags(MSG_ALL_FLAGS,MSG_MAGIC_FLAG);
        out->setData((unsigned8*)&magic,4);
        out->send(true);
        SLEEP_MS(DESTRUCT_WAIT);
      }
    }
  }

  // TODO: race condition here, we should terminate the thread before. SLEEP_MS is also wrong
  setClient(NULL);
    // recycle all pending NetMessages:
  RemoveUserMessages();
  RemoveSendComplete();
    // destroy the NetChannel:
#ifdef NET_LOG_CLIENT
#  ifdef NET_LOG_BRIEF
  NetLog("Ch(%u):~cli",
         channel?channel->getChannelId():0);
#  else
  NetLog("Channel(%u): destroying NetClient instance",
         channel?channel->getChannelId():0);
#  endif
#endif
  if ( channel )
  {
    NetChannel *old = channel;
    channel = NULL;
    leaveSnd();
    getPool()->deleteChannel(old);
  }
  else
    leaveSnd();
//  checkPool();
  FreeMemory();
}

NetStatus clientReceive ( NetMessage *msgPtr, NetStatus event, void *data )
{
  Ref<NetMessage> msg = msgPtr;
  // thread-safe
  if ( !_client || !msg ) return nsNoMoreCallbacks; // fatal error
  _client->lastMsgReported = msg->getTime();
  unsigned len = msg->getLength();
  if ( !len ) return nsNoMoreCallbacks;     // message is too small
#if defined(NET_LOG_CLIENT_RECEIVE) || defined(NET_LOG_MERGE) || defined(NET_LOG_CTRL_RECEIVE)
  NetChannel *channel = (NetChannel*)data;
  unsigned chanId = channel ? channel->getChannelId() : 0;
  Assert( channel );
#endif
  unsigned flags = msg->getFlags();
#if VOICE_OVER_NET
  if ( flags & MSG_VOICE_FLAG )             // voice-messages aren't routed yet => ignore them
    return nsNoMoreCallbacks;
#endif
  bool isMagic = (flags & MSG_MAGIC_FLAG) != 0;
  if ( isMagic )                            // magic-messages:
  {
    if ( len < 4 )
      return nsNoMoreCallbacks;             // magic-message is too small
    unsigned32 magic = *(unsigned32*)msg->getData();

#if defined(NET_LOG_CLIENT_RECEIVE) || defined(NET_LOG_CTRL_RECEIVE)
#  ifdef NET_LOG_BRIEF
    NetLog("Ch(%u):rCtrlC(%u,%u,%04x,%x)",
           chanId,len,msg->getSerial(),flags,magic);
#  else
    NetLog("Channel(%u)::clientReceive: received control message (len=%3u, serial=%4u, flags=%04x, magic=%x)",
           chanId,len,msg->getSerial(),flags,magic);
#  endif
#endif

    switch ( magic )
    {
      case MAGIC_ACK_PLAYER:                // AckPlayer (in case of success)
        if ( len == sizeof(AckPlayerPacket) && _client)
        {
          AckPlayerPacket *app = (AckPlayerPacket*)msg->getData();
          _client->enterSnd();
          if ( _client->ackPlayer == CRNone ) // refuse duplicate messages
          {
            _client->playerNo = app->playerNo;
            _client->ackPlayer = app->result;
          }
          _client->leaveSnd();
        }
        break;                              // don't need to lock this..
              

      case MAGIC_TERMINATE_SESSION:         // Session terminate
          // check additional data (if present)
        NetTerminationReason reason = NTROther;
        _client->whySessionTerminatedStr[0] = 0; //init as empty
        if ( len >= 2*sizeof(unsigned32) )
        {
          reason = (NetTerminationReason)((unsigned32*)msg->getData())[1];
          if (len>=2*sizeof(unsigned32))
          {
            int len=((int*)msg->getData())[2];
            if (len>0)
            {
              if (len>511) len=511;
              strncpy(_client->whySessionTerminatedStr, (char*)msg->getData()+3*sizeof(unsigned32), len);
              _client->whySessionTerminatedStr[len] = 0; //zero terminate
            }
          }
        }
        _client->enterSnd();
        _client->sessionTerminated = true;
        _client->whySessionTerminated = reason;
        _client->leaveSnd();
        break;

    }

    return nsNoMoreCallbacks;
  }

    // process regular (user) message:
#ifdef NET_LOG_CLIENT_RECEIVE
  NetLog("Channel(%u)::clientReceive: received user message (len=%3u, serial=%4u, flags=%04x, ID=%x)",
         chanId,len,msg->getSerial(),flags,msg->id);
#endif
  _client->enterRcv();
    // merge partial messages:
  if ( flags & MSG_PART_FLAG )
  {
    bool closing = (flags & MSG_CLOSING_FLAG) > 0;
    if ( flags & MSG_URGENT_FLAG )
      if ( closing )                        // the last message-part to be merged..
      {
        DoAssert( _client->splitUrgent.NotNull() );
        _client->lastSplitUrgent->next = msg;
        msg->next = NULL;
        msg = mergeMessageList(_client->splitUrgent);
        _client->splitUrgent = NULL;
      }
      else                                  // another message-part => remember it!
      {
        if ( !_client->splitUrgent )
          _client->splitUrgent = msg;
        else
          _client->lastSplitUrgent->next = msg;
        (_client->lastSplitUrgent = msg)->next = NULL;
        _client->leaveRcv();
        return nsNoMoreCallbacks;
      }
    else
      if ( closing )                        // the last message-part to be merged..
      {
        DoAssert( _client->split.NotNull() );
        _client->lastSplit->next = msg;
        msg->next = NULL;
        msg = mergeMessageList(_client->split);
        _client->split = NULL;
      }
      else                                  // another message-part => remember it!
      {
        if ( !_client->split )
          _client->split = msg;
        else
          _client->lastSplit->next = msg;
        (_client->lastSplit = msg)->next = NULL;
        _client->leaveRcv();
        return nsNoMoreCallbacks;
      }
  if (msg) LogF("clientReceive: merging user message (len=%3u, serial=%4u, flags=%04x, ID=%x)",
                 msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id);
#ifdef NET_LOG_MERGE
#  ifdef NET_LOG_BRIEF
    if (msg) NetLog("Ch(%u):rMerC(%u,%u,%x,%x)",
                    chanId,msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id);
#  else
    if (msg) NetLog("Channel(%u)::clientReceive: merging user message (len=%3u, serial=%4u, flags=%04x, ID=%x)",
                    chanId,msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id);
#  endif
#endif
  }

  if (msg) _client->insertReceived(msg);

  _client->leaveRcv();

  return nsNoMoreCallbacks;
}

#if VOICE_OVER_NET

bool NetClient::InitVoice ( VoiceMask *voiceMask, RawMessageCallback callback, bool isBotClient )
{
  assertVON();
	if ( voiceMask ) von->defaultVoiceMask = *voiceMask;
#if _GAMES_FOR_WINDOWS
  IXAudio2* xaudio2 = NULL;
  if (GSoundsys)
    xaudio2 = ((SoundSystemXA2*)GSoundsys)->XA2Device();
  DoAssert(xaudio2 != NULL);

  // get the indices of local talkers (all signed players who can play online)
  AutoArray<int> localTalkerIndices;
  localTalkerIndices.Add(GSaveSystem.GetUserIndex());

  VoNClient *vcl = new VoNClient(von, xaudio2, isBotClient, GSaveSystem.GetUserIndex(), localTalkerIndices);
#else
  VoNClient *vcl = new VoNClient(von, isBotClient);
#endif
  
  vcl->connectPeerToPeer(VoiceCreatePeerToPeer(), callback);
	Assert( vcl );
  vcl->outChannel = playerNo;
  von->setClient(vcl);
  if ( von->getServer() )
    von->localConnect();                    // local (DirectChannel) connection
  else
    vcl->connect(channel);                  // distant (NetChannel) connection
  return true;
}

void NetClient::VoiceCapture ( bool on, int quality )
{
  if ( !von ) return;
  RefD<VoNClient> vcl = von->getClient();
  if ( vcl )
    vcl->setCapture(on,quality);
}

void NetClient::SetVoiceTransmition(bool val)
{
  if ( !von ) return;
  RefD<VoNClient> vcl = von->getClient();
  if ( vcl ) vcl->setVoiceTransmition(val);
}

void NetClient::SetVoiceToggleOn(bool val)
{
  if ( !von ) return;
  RefD<VoNClient> vcl = von->getClient();
  if ( vcl ) vcl->SetVoiceToggleOn(val);
}

void NetClient::SetVoNRecThreshold(float val) 
{
  if (!von)
    return;
  RefD<VoNClient> vcl = von->getClient();
  if ( vcl ) vcl->SetVoNRecThreshold(val);
}
void NetClient::SetVoNTransmitterFreq(float f)
{
  if (!von)
    return;
  RefD<VoNClient> vcl = von->getClient();
  if ( vcl ) vcl->SetVoNTransmitterFreq(f);
}
float NetClient::GetVoNTransmitterFreq() const
{
  if (!von)
    return 0;
  RefD<VoNClient> vcl = von->getClient();
  if ( vcl ) return vcl->GetVoNTransmitterFreq();
  return 0;
}
void NetClient::SetVoNTransmitterRange(float range)
{
  if (!von)
    return;
  von->m_maxTransmitterRange2 = Square(range);
}
float NetClient::GetVoNTransmitterRange2() const
{
  if (!von)
    return 1000000.f;
  return von->m_maxTransmitterRange2;
}

bool NetClient::IsVoicePlaying ( int player )
{
  if ( !von ) return false;
  RefD<VoNClient> vcl = von->getClient();
  if ( vcl )
    return( vcl->getPlaying(player) > 0.0f );
  return false;
}

int NetClient::CheckVoiceChannel(int dpnid, bool &audible2D, bool &audible3D) const
{
  if ( !von )
  {
    audible3D = audible2D = false;
    return 0;
  }
  return von->checkChatChannel(dpnid,audible2D,audible3D);
}

bool NetClient::IsVoiceRecording ()
{
  if ( !von ) return false;
  RefD<VoNClient> vcl = von->getClient();
  if ( vcl )
    return vcl->getRecording();
  return false;
}

void NetClient::IsVoice2D3DPlaying(int dpnid, bool &audible2D, bool &audible3D) const
{
  if ( !von )
  {
    audible3D = audible2D = false;
  }
  else von->get2D3DPlaying(dpnid,audible2D,audible3D);
}

void NetClient::SetVoNPosition(int dpnid, Vector3Par pos, Vector3Par speed)
{
  if ( !von ) return;
  von->setPosition(dpnid, pos, speed); //affects position of source
}

void NetClient::SetVoNPlayerPosition(Vector3Par pos, Vector3Par speed)
{
  if ( !von ) return;
  von->setPlayerPosition(pos, speed); //affects position of listener
}

void NetClient::SetVoNObstruction(int dpnid, float obstruction, float occlusion, float deltaT)
{
  if ( !von ) return;
  von->setObstruction(dpnid, obstruction, occlusion, deltaT);
}

void NetClient::UpdateVoNVolumeAndAccomodation(int dpnid, float volume, float accomodation)
{
  if ( !von ) return;
  von->updateVolumeAndAccomodation(dpnid, volume, accomodation);
}

void NetClient::AdvanceAllVoNSounds(float deltaT, bool paused, bool quiet)
{
  if ( !von ) return;
  von->advanceAll(deltaT, paused, quiet);
}

#if _ENABLE_CHEATS
void NetClient::VoNSay(int dpnid, int channel, int frequency, int seconds)
{
  if ( !von ) return;
  RefD<VoNClient> vcl = von->getClient();
  if ( !vcl ) return;
  vcl->VoNSay(dpnid, channel, frequency, seconds);
}
#endif

#else

bool NetClient::InitVoice ( VoiceMask *voiceMask, RawMessageCallback callback, bool isBotClient )
{
  return false;
}

void NetClient::VoiceCapture ( bool on )
{
}

bool NetClient::IsVoicePlaying ( int player )
{
  return false;
}

int NetClient::CheckVoiceChannel(int dpnid, bool &audible2D, bool &audible3D) const
{
  audible3D = audible2D = false;
  return 0;
}

bool NetClient::IsVoiceRecording ()
{
  return false;
}

int NetClient::CheckVoiceChannel(int dpnid, bool &audible2D, bool &audible3D) const
{
  audible3D = audible2D = false;
  return 0;
}

void IsVoice2D3DPlaying(int dpnid, bool &audible2D, bool &audible3D) const
{
  audible3D = audible2D = false;
}

void NetClient::SetVoNCodecQuality(int quality)
{
}

void NetClient::SetVoNPosition(int dpnid, Vector3Par pos, Vector3Par speed)
{
}

void NetClient::SetVoNPlayerPosition(Vector3Par pos, Vector3Par speed)
{
}

void NetClient::SetVoNObstruction(int dpnid, float obstruction, float occlusion, float deltaT)
{
}

void NetClient::UpdateVoNVolumeAndAccomodation(int dpnid, float volume, float accomodation)
{
}

void NetClient::AdvanceAllVoNSounds(float deltaT, bool paused, bool quiet)
{
}

#if _ENABLE_CHEATS
void NetClient::VoNSay(int dpnid, int channel, int frequency, int seconds)
{
}
#endif

#endif  // _VOICE_OVER_NET

NetStatus clientSendComplete ( NetMessage *msg, NetStatus event, void *data )
  // data -> NetClient instance
  // thread-safe
{
  NetClient *client = (NetClient*)data;
  if ( !client || !msg || client->sessionTerminated ) return nsNoMoreCallbacks;    // fatal error
#ifdef NET_LOG_CLIENT_COMPLETE
  NetLog("Channel(%u)::clientSendComplete: status=%d, len=%3u, serial=%4u, flags=%04x, msgId=%x",
         client->channel->getChannelId(),(int)msg->getStatus(),msg->getLength(),
         msg->getSerial(),(unsigned)msg->getFlags(),msg->id);
#endif
  client->enterSnd();
  msg->next = client->sent;
  client->sent = msg;
  client->leaveSnd();
  return nsNoMoreCallbacks;
}

/*!
\patch_internal 1.58 Date 5/20/2002 by Pepca
- Fixed: Too large messages are now splitted automatically in NetTransp* layer. [Sockets]
*/

Ref<NetMessage> NetClient::SendMsg ( BYTE *buffer, int bufferSize, DWORD &msgID, NetMsgFlags flags, const Ref<NetMessage> &dependOn )
{
    bool vim = (flags & NMFGuaranteed) > 0;
    bool urgent = (flags & NMFHighPriority) > 0;
    enterSnd();
    if ( !channel || !buffer || bufferSize <= 0 ) {
        leaveSnd();
        return NULL;
        }
    int maxMessage = channel->maxMessageData();
    if ( !vim && bufferSize > maxMessage ) {
        leaveSnd();
#ifdef NET_LOG_MERGE
#  ifdef NET_LOG_BRIEF
        NetLog("Ch(%u):tooLargeC(%d,%x)",
               channel->getChannelId(),bufferSize,(unsigned)flags);
#  else
        NetLog("Channel(%u): NetClient::SendMsg: trying to send too large non-guaranteed message (len=%3d, flags=%x)",
               channel->getChannelId(),bufferSize,(unsigned)flags);
#  endif
#endif
        RptF("NetClient: trying to send too large non-guaranteed message (%d bytes long)",bufferSize);
        return NULL;
        }
    Ref<NetMessage> msg;
    if ( vim ) {                            // guaranteed message
        unsigned fl = MSG_VIM_FLAG | (urgent ? MSG_URGENT_FLAG : 0);
        if ( bufferSize > maxMessage ) {    // split too big message:
#ifdef NET_LOG_MERGE
#  ifdef NET_LOG_BRIEF
            NetLog("Ch(%u):sMerC(%d,%x)",
                   channel->getChannelId(),bufferSize,(unsigned)flags);
#  else
            NetLog("Channel(%u): NetClient::SendMsg: splitting user message (len=%3d, flags=%x)",
                   channel->getChannelId(),bufferSize,(unsigned)flags);
#  endif
#endif
            int toSent = bufferSize;
            int packet;
            fl |= MSG_PART_FLAG;
            do {
                packet = (toSent > maxMessage) ? maxMessage : toSent;
                toSent -= packet;
                msg = NetMessagePool::pool()->newMessage(packet,channel);
                if ( !msg ) {
                    leaveSnd();
                    return NULL;
                    }
                msg->setFlags(MSG_ALL_FLAGS,fl | (toSent ? 0 : MSG_CLOSING_FLAG));
                msg->setOrderedPrevious();
                msg->setData((unsigned8*)buffer,packet);
                buffer += packet;
                msg->send(urgent);
                } while ( toSent );
            }
        else {                              // small message
            msg = NetMessagePool::pool()->newMessage(bufferSize,channel);
            msg->setFlags(MSG_ALL_FLAGS,fl);
            msg->setOrderedPrevious();
            msg->setData((unsigned8*)buffer,bufferSize);
            msg->send(urgent);
            }
        }
    else {                                  // common message
        msg = NetMessagePool::pool()->newMessage(bufferSize,channel);
        if ( !msg ) {
            leaveSnd();
            return NULL;
            }
        if ( dependOn ) msg->setOrdered(dependOn);
        msg->setCallback(clientSendComplete,nsOutputSent,this);
        msg->setSendTimeout(SEND_TIMEOUT);
        msg->setData((unsigned8*)buffer,bufferSize);
        msg->send();
        }
    msgID = (DWORD)msg->id;
#ifdef NET_LOG_CLIENT_SEND
    NetLog("Channel(%u): NetClient::SendMsg: len=%3d, flags=%x, msgID=%x, hdr=%08x%08x",
           channel->getChannelId(),bufferSize,(unsigned)flags,msgID,
           ((unsigned*)msg->getData())[0],((unsigned*)msg->getData())[1]);
#endif
    leaveSnd();
    return msg;
}

void NetClient::GetSendQueueInfo ( int &nMsg, int &nBytes, int &nMsgG, int &nBytesG )
{
  enterSnd();
  if ( channel )
    channel->getOutputQueueStatistics(nMsg,nBytes,nMsgG,nBytesG);
  else
  {
    nMsg = nBytes = nMsgG = nBytesG = 0;
  }
  leaveSnd();
}

void NetClient::GetConnectionLimits(int &maxBandwidthPerClient)
{
  maxBandwidthPerClient = INT_MAX;
}

void NetClient::SetConnectionLimits(int maxBandwidthPerClient)
{
}

bool NetClient::GetConnectionInfo ( int &latencyMS, int &throughputBPS )
{
  enterSnd();
  if ( !channel )
  {
    leaveSnd();
    return false;
  }
  latencyMS     = (int)(channel->getLatency() / 1000);
  throughputBPS = (int)channel->getOutputBandWidth();;
#ifdef NET_LOG_INFO
  if ( latencyMS != oldLatency ||
       throughputBPS != oldThroughput )
  {
    NetLog("Channel(%u): NetClient::GetConnectionInfo: %d ms,%d B/s",
           channel->getChannelId(),latencyMS,throughputBPS);
    oldLatency = latencyMS;
    oldThroughput = throughputBPS;
    printAge = true;
  }
#endif
  leaveSnd();
  return true;
}

bool NetClient::GetLocalAddress( in_addr &addr ) const
{
  struct sockaddr_in local;
  if (!getLocalAddress(local, 0)) return false;
  addr = local.sin_addr;
  return true;
}

bool NetClient::GetLocalAddress(in_addr &addr, int &port) const
{
  enterSnd();
  if ( !channel )
  {
    leaveSnd();
    return false;
  }
  struct sockaddr_in saddr;
  channel->getLocalAddress(saddr);
  addr = saddr.sin_addr;
  port = ntohs(saddr.sin_port);
  leaveSnd();
  return true;
}


bool NetClient::GetDistantAddress(in_addr &addr, int &port) const
{
  enterSnd();
  if ( !channel )
  {
    leaveSnd();
    return false;
  }
  struct sockaddr_in saddr;
  channel->getDistantAddress(saddr);
  addr = saddr.sin_addr;
  port = saddr.sin_port;
  leaveSnd();
  return true;
}
  
bool NetClient::GetServerAddress( sockaddr_in &address ) const
{
  enterSnd();
  if ( !channel )
  {
    leaveSnd();
    return false;
}
  channel->getDistantAddress(address);
  leaveSnd();
  return true;
}

int NetClient::GetVoicePort() const
{
#if VOICE_OVER_NET
  if (von)
  {
    RefD<VoNClient> vcl = von->getClient();
    if ( vcl )
    {
      return vcl->GetPort();
    }
  }
#endif
  return 0;
}

SOCKET NetClient::GetVoiceSocket() const
{
#if VOICE_OVER_NET
  if (von)
  {
    RefD<VoNClient> vcl = von->getClient();
    if ( vcl )
      return vcl->GetSocket();
  }
#endif
  return INVALID_SOCKET;
}

void NetClient::SetNetworkParams ( ParamEntryPar cfg )
{
  NetworkParams data;
  LoadNetworkParams(data,cfg);
  enterSnd();
  if ( !channel )
  {
    leaveSnd();
    return;
  }
  channel->setNetworkParams(data);
  leaveSnd();
}

float NetClient::GetLastMsgAge ()
{
  enterSnd();
  float result = 1.0;
  if ( channel )
  {
    unsigned64 last = channel->getLastMessageArrival();
    unsigned64 now = getSystemTime();
    if ( now < last )
    {
      LogF("Negative time delta in NetClient::GetLastMsgAge: now = %8x%08x, last = %8x%08x",
           (unsigned)(now>>32),(unsigned)(now&0xffffffff),
           (unsigned)(last>>32),(unsigned)(last&0xffffffff));
      result = 0;
    }
    else
    {
      unsigned64 delta = now - last;
#if _ENABLE_REPORT
      static unsigned overflowCounter = 0;
      if ( delta > 200000000 )
      {
        if ( !(overflowCounter++ & 0x1f) )
        {
          RptF("Overflow in NetClient::GetLastMsgAge: now = %8x%08x, last = %8x%08x, delta = %.0f",
               (unsigned)(now>>32),(unsigned)(now&0xffffffff),
               (unsigned)(last>>32),(unsigned)(last&0xffffffff),
               (double)(1.e-6f * delta));
          char buf[4096];
          channel->channelDump(buf);
          RptF(buf);
        }
      }
      else
        overflowCounter = 0;
#endif
      result = 1.e-6f * (unsigned)delta;
    }
  }
  leaveSnd();
  return result;
}

float NetClient::GetLastMsgAgeReported ()
{
  return( 1.e-6f * (getSystemTime() - lastMsgReported) );
}

void NetClient::LastMsgAgeReported ()
{
  lastMsgReported = getSystemTime();
}

/*!
\patch_internal 1.54 Date 4/5/2002 by Pepca
- Fixed: Bot-client cannot be disconnected by itself (due to line-drop). [Sockets]
*/

bool NetClient::IsSessionTerminated ()
{
  enterSnd();
  if ( !channel )
  {
    leaveSnd();
    return true;
  }
      // check NetChannel connectivity:
  if ( !amIBot && channel->dropped() )
  {
    sessionTerminated = true;
    whySessionTerminated = NTRTimeout;
  }
  leaveSnd();
  return sessionTerminated;
}

NetTerminationReason NetClient::GetWhySessionTerminated ()
{
  NetTerminationReason reason;
  enterSnd();
  reason = whySessionTerminated;

  leaveSnd();
  return reason;
}

RString NetClient::GetWhySessionTerminatedStr ()
{
  RString reason;
  enterSnd();
  reason = whySessionTerminatedStr;

  leaveSnd();
  return reason;
}

void NetClient::ProcessUserMessages ( UserMessageClientCallback *callback, void *context )
{
  if ( !callback ) return;
  Ref<NetMessage> msg;
  enterRcv();
#ifdef NET_LOG_CLIENT_PROCESS
  int n = 0;
  msg = received;
  while ( msg )
  {
    n++;
    msg = msg->next;
  }
  if ( n )
  {
    NetLog("Channel(%u): NetClient::ProcessUserMessages: processed %d messages:",channel->getChannelId(),n);
    msg = received;
    while ( msg )
    {
      NetLog("Channel(%u): Processed: len=%3u, serial=%4u, flags=%04x, msgID=%x, hdr=%08x%08x",
             channel->getChannelId(),msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id,
             ((unsigned*)msg->getData())[0],((unsigned*)msg->getData())[1]);
      msg = msg->next;
    }
  }
#endif
  while ( received )
  {
    msg = received;
    received = msg->next;
    msg->next = NULL;
    leaveRcv();
#if 0
    LogF("NetClient::ProcessUserMessages: len=%3u, serial=%4u, flags=%04x",
         msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags());
#endif
    (*callback)((char*)msg->getData(),msg->getLength(),context);
    enterRcv();
  }
  leaveRcv();
}

void NetClient::insertReceived ( NetMessage *msg )
  // must be called inside enterRcv()
{
  if (!msg) return;
  if ( received )
  {
    MsgSerial s = msg->getSerial();
    if ( s < received->getSerial() )
    {
      msg->next = received;
      received = msg;
    }
    else
    {
      NetMessage *ptr = received;
      while ( ptr->next && ptr->next->getSerial() < s )
        ptr = ptr->next;
      msg->next = ptr->next;
      ptr->next = msg;
    }
  }
  else
  {
    msg->next = NULL;
    received = msg;
  }
}

void NetClient::RemoveUserMessages ()
{
  enterRcv();
  Ref<NetMessage> tmp;
  while ( received )
  {
    tmp = received->next;
    received->next = NULL;                  // don't need this message anymore, somebody will recycle it later
    received = tmp;
  }
  leaveRcv();
}

void NetClient::ProcessSendComplete ( SendCompleteCallback *callback, void *context )
{
  if ( !callback ) return;
  enterSnd();
  NetMessage *msg;
#ifdef NET_LOG_CLIENT_COMPLETE
  int n = 0;
  msg = sent;
  while ( msg )
  {
    n++;
    msg = msg->next;
  }
  if ( n )
  {
    NetLog("Channel(%u): NetClient::ProcessSendComplete: processed %d messages:",channel->getChannelId(),n);
    msg = sent;
    while ( msg )
    {
      NetLog("Channel(%u): Processed: ok=%d(%d), len=%3u, serial=%4u, flags=%04x, MsgID=%x, hdr=%08x%08x",
             channel->getChannelId(),(int)(msg->getStatus()!=nsOutputObsolete),(int)msg->getStatus(),
             msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id,
             ((unsigned*)msg->getData())[0],((unsigned*)msg->getData())[1]);
      msg = msg->next;
    }
  }
#endif
  msg = sent;
  while ( msg ) {
      (*callback)((DWORD)msg->id,msg->wasSent(),context);
      msg = msg->next;
      }
  RemoveSendComplete();
  leaveSnd();
}

void NetClient::RemoveSendComplete ()
{
  enterSnd();
  Ref<NetMessage> tmp;
  while ( sent )
  {
    tmp = sent->next;
    sent->next = NULL;                      // don't need this message anymore, somebody will recycle it later
    sent = tmp;
  }
  leaveSnd();
}

/*!
\patch_internal 1.53 Date 4/24/2002 by Pepca
- Added: NetClient::GetStatistics, NetServer::GetStatistics - internal NetChannel
statistics in textual form. Alt+;
*/

#ifdef NET_LOG_TRANSP_STAT
static int getStatisticsCount = 0;
#endif

RString NetClient::GetStatistics ()
{
  enterSnd();
  if ( !channel )
  {
    leaveSnd();
    return RString("No NetChannel is connected to this NetClient");
  }
  int latencyAve;
  unsigned latencyAct,latencyMin;
  int throughputAve;
  latencyAve = (int)(channel->getLatency(&latencyAct,&latencyMin) / 1000);
  latencyAct /= 1000;
  latencyMin /= 1000;
  EnhancedBWInfo enhanced;
  throughputAve = (int)channel->getOutputBandWidth(&enhanced);
  int nMsg, nBytes, nMsgG, nBytesG;
  channel->getOutputQueueStatistics(nMsg,nBytes,nMsgG,nBytesG);
  ChannelStatistics stat;                 // internal statistics of the NetChannel
  Zero(stat);
  channel->getInternalStatistics(stat);
  leaveSnd();
  char buf[256];
  bool kbps = (throughputAve < (9<<17));
  sprintf(buf,"ping%4dms(%4u,%4u) BW%c%c%c%5d%cb(%4u,%4u,%4u) lost%4.1f%%%%(%3u) queue%4dB(%4d) ackWait%3u(%3.1f,%3.1f)",
              latencyAve,latencyAct,latencyMin,(char)(enhanced.growMode+'c'),(char)(enhanced.growModePing+'c'),
              (char)(enhanced.growModeLost+'c'),kbps?((throughputAve+64)>>7):((throughputAve+65536)>>17),
              kbps?'K':'M',(enhanced.actBW+64)>>7,(enhanced.goodBW+64)>>7,(enhanced.sentBW+64)>>7,
              stat.ackTotal?(stat.ackLost*100.0)/stat.ackTotal:0.0,stat.ackLost,
              nBytes,nBytesG,stat.revisitedNo,1e-6*stat.revisitedAveAge,1e-6*stat.revisitedMaxAge);
#ifdef NET_LOG_TRANSP_STAT
  unsigned64 now = getSystemTime();
  if ( (!amIBot && now >= nextStatLog) || getStatisticsCount > 100 )
  {
    NetLog("Channel(%u): NetClient(%d)[%d] - %s",channel->getChannelId(),playerNo,getStatisticsCount,buf);
    getStatisticsCount = 0;
    nextStatLog = now + STAT_LOG_INTERVAL;
  }
  else
    getStatisticsCount++;
#endif
  return RString(buf);
}

unsigned NetClient::FreeMemory ()
{
  if ( !NetMessagePool::pool() ) return 0;
  unsigned ret = NetMessagePool::pool()->freeMemory();
  SafeMemoryCleanUp();
  return ret;
}

//---------------------------------------------------------------------------
//  NetServer:

NetServer::NetServer ()
  : LockInit(usrCs,"NetServer::usrCs",true),
    LockInit(rcvCs,"NetServer::rcvCs",true),
    LockInit(sndCs,"NetServer::sndCs",true)
{
    // to be sure:
  enterUsr();
  received = NULL;
  sent = NULL;
  m_enumResponse = true;
  session.actualVersion = 0;
  session.requiredVersion = 0;
  session.serverState = 0;
  session.maxPlayers = 0;
  session.numPlayers = 0;
  session.password = false;
  session.port = 0;
  session.name[0] = (char)0;
  session.gameType[0] = (char)0;
  session.SetMissionName(LocalizedString());
  session.island[0] = (char)0;
  session.mod[0] = 0;
  session.equalModRequired = 0;
  botId = 0;
  magicApp = 0;
#ifdef NET_LOG_INFO
  oldLatency = -1;
  oldThroughput = -1;
#endif
#ifdef NET_LOG_TRANSP_STAT
  nextStatLog = getSystemTime();
  forceLog = inGetConnection = false;
#endif
  setServer(this);
    // create a new NetPeer:
  poolLock.enter();
  sessionPort = GET_SERVER_PEER() ? GET_SERVER_PEER()->getPort() : 0;
                                            // should be XBOX_GAME_PORT on Xbox
  poolLock.leave();
  leaveUsr();
#ifdef NET_LOG_SERVER
#  ifdef NET_LOG_BRIEF
  NetLog("Pe(%u):srv",GetServerPeerId());
#  else
  NetLog("Peer(%u): creating NetServer instance",GetServerPeerId());
#  endif
#endif
}

/*!
\patch_internal 1.53 Date 4/26/2002 by Pepca
- Fixed: Casual crashes in game-server termination. [Sockets]
*/

void NetServer::disconnectAllPlayers()
{
  // do not respond to enumeration responses any more, as we are shutting down
  m_enumResponse = false;
  // kick-off all the players:
  IteratorState it;
  enterUsr();
  RefD<NetChannel> ch;
  if ( users.getFirst(it,ch) )
    do
      destroyPlayer(ch,NTRDisconnected);
    while ( users.getNext(it,ch) );
  leaveUsr();
  SLEEP_MS(DESTRUCT_WAIT);                  // waits till all players are gone..
}

NetServer::~NetServer ()
{
#if VOICE_OVER_NET
  if ( von ) von->setServer(NULL);
#endif

#ifdef NET_LOG_SERVER
#  ifdef NET_LOG_BRIEF
  NetLog("Pe(??):~srv");
#  else
  NetLog("Peer(??): destroying NetServer instance");
#  endif
#endif
  CancelAllMessages();
  setServer(NULL);
    // recycle all pending NetMessages:
  RemoveUserMessages();
  RemoveSendComplete();
    // remove other data:
  RemovePlayers();
  RemoveVoicePlayers();
//  checkPool();
  FreeMemory();
}

void NetServer::GetSendQueueInfo ( int to, int &nMsg, int &nBytes, int &nMsgG, int &nBytesG )
{
  enterUsr();
  RefD<NetChannel> channel;
  if ( users.get(to,channel) )
    channel->getOutputQueueStatistics(nMsg,nBytes,nMsgG,nBytesG);
  else
  {
    nMsg = nBytes = nMsgG = nBytesG = 0;
  }
  leaveUsr();
}

/*!
\patch_internal 1.53 Date 4/29/2002 by Pepca
- Added: DEDICATED_STAT_LOG - net-statistics logging on dedicated server. [Sockets]
*/

bool NetServer::GetConnectionInfo ( int to, int &latencyMS, int &throughputBPS )
{
  enterUsr();
  RefD<NetChannel> channel;
  if ( !users.get(to,channel) )
  {
    leaveUsr();
    return false;
  }
  latencyMS     = (int)(channel->getLatency() / 1000);
  throughputBPS = (int)channel->getOutputBandWidth();
#ifdef NET_LOG_INFO
  if ( latencyMS != oldLatency ||
       throughputBPS != oldThroughput )
  {
    NetLog("Channel(%u): NetServer::GetConnectionInfo(player=%d): %d ms,%d B/s",
           channel->getChannelId(),to,latencyMS,throughputBPS);
    oldLatency = latencyMS;
    oldThroughput = throughputBPS;
  }
#endif
    // check the channel condition:
  bool dropped = (to != botId) && channel->dropped();
  if ( dropped )
    finishDestroyPlayer(to);
#ifdef DEDICATED_STAT_LOG
  if ( IsDedicatedServer() )
  {
    unsigned64 now = getSystemTime();
#ifdef NET_LOG_TRANSP_STAT
    if ( true )
    {
      const bool doConsole = (now >= nextConsoleLog);
#else
    if ( now >= nextConsoleLog )
    {
      const bool doConsole = true;
#endif
      if ( doConsole )
        nextConsoleLog = now + CONSOLE_LOG_INTERVAL;
      IteratorState it;
      if ( users.getFirst(it,channel,&to) )
        do
          if ( to != botId )
          {
            RString stat = GetStatistics(to);
            if ( doConsole )
              ConsoleF("Player %d: %s",to,(const char*)stat); // debug log - need not to be localized
          }
        while ( users.getNext(it,channel,&to) );
    }
  }
#else
#  ifdef NET_LOG_TRANSP_STAT
  IteratorState it;
  forceLog = false;
  inGetConnection = true;
  for ( users.getFirst(it,channel,&to); channel; users.getNext(it,channel,&to) )
    if ( to != botId ) GetStatistics(to);
  inGetConnection = false;
#  endif
#endif
  leaveUsr();
  return true;
}

void NetServer::GetConnectionLimits(int &maxBandwidthPerClient)
{
  maxBandwidthPerClient = NetChannelBasic::par.maxBandwidth;
}

void NetServer::SetConnectionLimits(int maxBandwidthPerClient)
{
  NetChannelBasic::par.maxBandwidth = maxBandwidthPerClient;
}

float NetServer::GetLastMsgAgeReliable ( int player )
{
  float result = 1.0;

  enterUsr();
  RefD<NetChannel> channel;
  if ( users.get(player,channel) )
  {
    unsigned64 last = channel->getLastMessageArrival();
    unsigned64 now = getSystemTime();
    if ( now < last )
    {
      LogF("Negative time delta in NetServer::GetLastMsgAgeReliable: now = %8x%08x, last = %8x%08x",
           (unsigned)(now>>32),(unsigned)(now&0xffffffff),
           (unsigned)(last>>32),(unsigned)(last&0xffffffff));
      result = 0;
    }
    else
    {
      unsigned64 delta = now - last;
#if _ENABLE_REPORT
      if ( delta > 200000000 )
        RptF("Overflow in NetServer::GetLastMsgAgeReliable: now = %8x%08x, last = %8x%08x, delta = %.0f",
             (unsigned)(now>>32),(unsigned)(now&0xffffffff),
             (unsigned)(last>>32),(unsigned)(last&0xffffffff),
             (1.e-6 * delta));
#endif
      result = 1.e-6f * (unsigned)delta;
    }
  }

  leaveUsr();
  return result;
}

void NetServer::SetNetworkParams ( ParamEntryPar cfg )
{
  NetworkParams data;
  LoadNetworkParams(data,cfg);
  enterUsr();
  IteratorState it;
  RefD<NetChannel> channel;
  if ( !users.getFirst(it,channel) )
  {
    leaveUsr();
    return;
  }
  channel->setNetworkParams(data);
  leaveUsr();
}

NetStatus destroyPlayerCallback ( NetMessage *msg, NetStatus event, void *data )
  // data = player number
{
  poolLock.enter();
  if ( _server )
    _server->finishDestroyPlayer((int)data);
  poolLock.leave();
  return nsNoMoreCallbacks;
}

/*!
\patch 1.82 Date 8/19/2002 by Ondra
- Fixed: MP: Reason of disconnection was often not displayed on clients.
*/

void NetServer::destroyPlayer ( NetChannel *ch, NetTerminationReason reason, const char *reasonStr )
{
  Assert( ch );
#ifdef NET_LOG_SERVER_KICKOFF
  NetLog("Channel(%u): NetServer::destroyPlayer: player=%d",ch->getChannelId(),channelToPlayer(ch));
#endif
  // cancel all other pending messages, the kick is the only necessary
  ch->cancelAllMessages();
  // send MAGIC_TERMINATE_SESSION message to the client:
  char buf[1024];
  unsigned32 *magic = (unsigned32*) buf;
  int reasonLen = (reasonStr ? strlen(reasonStr) : 0);
  int msgLen = 3*sizeof(unsigned32)+reasonLen;
  Ref<NetMessage> out = NetMessagePool::pool()->newMessage(msgLen,ch);
  if ( !out ) return;
  magic[0] = MAGIC_TERMINATE_SESSION;
  magic[1] = reason;
  magic[2] = reasonLen;
  if (reasonStr) strcpy(buf+3*sizeof(unsigned32), reasonStr);
  out->setFlags(MSG_ALL_FLAGS,MSG_MAGIC_FLAG);
  out->setData((unsigned8*)buf,msgLen);
  //RptF("NetServer::destroyPlayer: player=%d",channelToPlayer(ch));
  out->setCallback(destroyPlayerCallback,nsOutputSent,(void*)channelToPlayer(ch));
  out->send(true);
}

void NetServer::logUsers()
{
  char usersText[1024];

  IteratorState it;
  int key;
  RefD<NetChannel> channel;
  sprintf(usersText,"%d: ",users.card());
  if ( users.getFirst(it,channel,&key) )
    do
      sprintf(usersText+strlen(usersText)," %d",key);
    while ( users.getNext(it,channel,&key) );
  RptF("NetServer::logUsers %s",usersText);
}

void NetServer::finishDestroyPlayer ( int player )
{
  enterUsr();
  RefD<NetChannel> ch;
  if ( !users.get(player,ch) )
  {
    RptF("NetServer::finishDestroyPlayer(%d): users.get failed",player);
    //logUsers();
    DoAssert( users.checkIntegrity() );
    leaveUsr();
    return;
  }
    // delete player from my structures:
    
  // moved to ProcessPlayers
#if 0 //VOICE_OVER_NET
  if ( von.NotNull() && von->getServer().NotNull() )
    von->getServer()->disconnect(player);
#endif
  users.removeKey(player);
  struct sockaddr_in daddr;
  ch->getDistantAddress(daddr);
  m_support.removeKey(sockaddrKey(daddr));
  //logUsers();
  DoAssert( users.checkIntegrity() );
  getPool()->deleteChannel(ch);
  int index;
  for ( index = 0; index < _createPlayers.Size(); index++ )
    if ( _createPlayers[index].player == player )
    {
      RptF("NetServer::finishDestroyPlayer(%d): DESTROY immediately after CREATE, both cancelled",player);
      _createPlayers.Delete(index);
      leaveUsr();
      return;
    }
  index = _deletePlayers.Add();
  DeletePlayerInfo &info = _deletePlayers[index];
  info.player = player;
  LogF("NetServer:finishDestroyPlayer (waiting for ProcessPlayers) - session.numPlayers=%d, playerId=%d, |users|=%u",
       session.numPlayers,info.player,users.card());
  leaveUsr();
}

void NetServer::KickOff ( int player, NetTerminationReason reason, const char *reasonStr )
{
  enterUsr();
  RefD<NetChannel> channel;
  if ( !users.get(player,channel) )
  {
    RptF("NetServer::KickOff: player=%d - !users.get",player);
    leaveUsr();
    return;
  }
#ifdef NET_LOG_SERVER_KICKOFF
  NetLog("Channel(%u): NetServer::KickOff: player=%d",channel->getChannelId(),player);
#endif
  //RptF("NetServer::KickOff: player=%d",player);
  destroyPlayer(channel,reason,reasonStr);
  leaveUsr();
}

bool NetServer::Init (
  RString name, RString password, int maxPlayers, int port,
  int language, int difficulty,
  MPVersionInfo &versionInfo, bool equalModRequired, int magic
)
{
  if ( !sessionPort )
  {
#if _ENABLE_DEDICATED_SERVER
    if ( IsDedicatedServer() )
    {
      ConsoleF(LocalizeString(IDS_DS_SERVER_CREATE_FAIL), port);
    }
#endif
    return false;
  }
    // initialize session attributes:
  enterUsr();
  _password = password;
  magicApp = magic;
  session.actualVersion = versionInfo.versionActual;
  session.requiredVersion = versionInfo.versionRequired;
  session.equalModRequired = equalModRequired ? 1 : 0;
  session.maxPlayers = maxPlayers;
  session.numPlayers = 0;
  session.password = (password.GetLength() > 0)*2;
  session.port = sessionPort;
  strncpy(session.name, name, sizeof(session.name));
  session.name[sizeof(session.name)-1] = (char)0;
  strncpy(session.mod,versionInfo.mod,sizeof(session.mod));
  session.mod[sizeof(session.mod)-1] = (char)0;
  sessionName = name;
  session.gameType[0] = 0;
  session.SetMissionName(LocalizedString());
  session.island[0] = 0;
  session.serverState = 0;
  session.language = language;
  session.timeleft = 0;
  session.difficulty = difficulty;
#ifdef DEDICATED_STAT_LOG
  nextConsoleLog = getSystemTime();
#endif
#ifdef NET_LOG_SERVER
#  ifdef NET_LOG_BRIEF
  NetLog("Pe(%u):srv(%d,'%s',%d)",
         GetServerPeerId(),sessionPort,session.name,magic);
#  else
  NetLog("Peer(%u): NetServer::Init: local port=%d, sessionName='%s', magic=%d",
         GetServerPeerId(),sessionPort,session.name,magic);
#  endif
#endif
  leaveUsr();
  return true;
}

void NetServer::UpdateMaxPlayers(int maxPlayers)
{
  session.maxPlayers = maxPlayers;
}

void NetServer::UpdateLockedOrPassworded(bool lock, bool passworded)
{
  session.password = (lock ? 1 : 0) + (passworded ? 2 : 0);
}


NetStatus serverSendComplete ( NetMessage *msg, NetStatus event, void *data )
  // data -> NetServer instance
  // thread-safe
{
  NetServer *server = (NetServer*)data;
  if ( !server || !msg ) return nsNoMoreCallbacks;    // fatal error
#ifdef NET_LOG_SERVER_COMPLETE
  NetLog("Channel(%u)::serverSendComplete: status=%d, len=%3u, serial=%4u, flags=%04x, msgId=%x",
         msg->getChannel()->getChannelId(),
         (int)msg->getStatus(),msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id);
#endif
  server->enterSnd();
  msg->next = server->sent;
  server->sent = msg;
  server->leaveSnd();
  return nsNoMoreCallbacks;
}

Ref<NetMessage> NetServer::SendMsg ( int to, BYTE *buffer, int bufferSize, DWORD &msgID, NetMsgFlags flags, const Ref<NetMessage> &dependOn )
{
  if ( !buffer || bufferSize <= 0 )
  {
    RptF("NetServer: trying to send empty message to %d",to);
    return NULL;
  }
  RefD<NetChannel> channel;
  Ref<NetMessage> msg;
  bool vim = (flags & NMFGuaranteed) > 0;
  bool urgent = (flags & NMFHighPriority) > 0;
  bool setCallback = (flags & NMFSetCallback) > 0;
  NetPeer *peer = GET_SERVER_PEER();
  int maxMessage = peer ? peer->maxMessageData() : 0;
  if ( (!vim || to == DPNID_ALL_PLAYERS_GROUP) && bufferSize > maxMessage )
  {
#ifdef NET_LOG_MERGE
    unsigned chId = 0;
    if ( to != DPNID_ALL_PLAYERS_GROUP &&
         users.get(to,channel) )
      chId = channel->getChannelId();
#  ifdef NET_LOG_BRIEF
    NetLog("Ch(%u):tooLargeS(%d,%d,%x)",
           chId,to,bufferSize,(unsigned)flags);
#  else
    NetLog("Channel(%u): NetServer::SendMsg: trying to send too large non-guaranteed message (to=%d, len=%3d, flags=%x)",
           chId,to,bufferSize,(unsigned)flags);
#  endif
#endif
    RptF("NetServer: trying to send too large non-guaranteed message (%d bytes long, max %d allowed)",bufferSize,maxMessage);
    return NULL;
  }
  unsigned16 fl = 0;
  if ( vim )    fl |= MSG_VIM_FLAG;
  if ( urgent ) fl |= MSG_URGENT_FLAG;

  enterUsr();
  if ( to == DPNID_ALL_PLAYERS_GROUP )
  {                                         // broadcast to all players
#ifdef NET_LOG_SERVER_SEND
    NetLog("Peer(%u): NetServer::SendMsg: broadcast to all players",GetServerPeerId());
#endif
    IteratorState it;
    if ( users.getFirst(it,channel) )
      do
      {
        msg = NetMessagePool::pool()->newMessage(bufferSize,channel);
        if ( !msg )
        {
          leaveUsr();
          RptF("NetServer: pool()->newMessage failed when sending to %d",to);
          return NULL;
        }
        msg->setFlags(MSG_ALL_FLAGS,fl);
        if ( fl )
        {
          if (setCallback) msg->setCallback(serverSendComplete,nsOutputSent,this);
          msg->setOrderedPrevious();        // VIM is set ORDERED automatically
        }
        else                                // common messages
        {
          msg->setCallback(serverSendComplete,nsOutputSent,this);
          if ( dependOn ) msg->setOrdered(dependOn);
          msg->setSendTimeout(SEND_TIMEOUT);
        }
        msg->setData((unsigned8*)buffer,bufferSize);
        msg->send(urgent);
#ifdef NET_LOG_SERVER_SEND
        NetLog("Channel(%u): NetServer::SendMsg: to=%d, len=%3d, flags=%x, msgID=%x, hdr=%08x%08x",
               channel->getChannelId(),to,bufferSize,(unsigned)flags,(unsigned)msg->id,
               ((unsigned*)msg->getData())[0],((unsigned*)msg->getData())[1]);
#endif
      } while ( users.getNext(it,channel) );
  }

  else                                      // single recipient
  {
    if ( !users.get(to,channel) )
    {
#ifdef NET_LOG_SERVER_SEND
      NetLog("Peer(%u): NetServer::SendMsg: cannot find channel #%d, users.card=%u",
             GetServerPeerId(),to,users.card());
#endif
      RptF("NetServer::SendMsg: cannot find channel #%d, users.card=%u",
           to,users.card());
      RptF("NetServer: users.get failed when sending to %d",to);
      //logUsers();
      DoAssert( users.checkIntegrity() );
      leaveUsr();
      return NULL;
    }
    if ( bufferSize > maxMessage )          // split too big message:
    {
#ifdef NET_LOG_MERGE
#  ifdef NET_LOG_BRIEF
      NetLog("Ch(%u):sMerS(%d,%d,%x,%d)",
             channel->getChannelId(),to,bufferSize,(unsigned)flags,maxMessage);
#  else
      NetLog("Channel(%u): NetServer::SendMsg: splitting user message (to=%d, len=%3d, flags=%x, max=%3d)",
             channel->getChannelId(),to,bufferSize,(unsigned)flags,maxMessage);
#  endif
#endif
      int toSent = bufferSize;
      int packet;
      fl |= MSG_PART_FLAG;
      do
      {
        packet = (toSent > maxMessage) ? maxMessage : toSent;
        toSent -= packet;
        msg = NetMessagePool::pool()->newMessage(packet,channel);
        if ( !msg )
        {
          leaveSnd();
          RptF("NetServer: pool()->newMessage failed when sending to %d",to);
          return NULL;
        }
        msg->setFlags(MSG_ALL_FLAGS,fl | (toSent ? 0 : MSG_CLOSING_FLAG));
        if (setCallback && !toSent) //set callback only in one of the msg chunks (in last one)
          msg->setCallback(serverSendComplete,nsOutputSent,this);
        msg->setOrderedPrevious();
        msg->setData((unsigned8*)buffer,packet);
        buffer += packet;
        msg->send(urgent);
      } while ( toSent );
    }
    else                                    // small message
    {
      msg = NetMessagePool::pool()->newMessage(bufferSize,channel);
      if ( !msg )
      {
        leaveUsr();
        RptF("NetServer: pool()->newMessage failed when sending to %d",to);
        return NULL;
      }
      msg->setFlags(MSG_ALL_FLAGS,fl);
      if ( fl )
      {
        if (setCallback) msg->setCallback(serverSendComplete,nsOutputSent,this);
        msg->setOrderedPrevious();          // VIM is set ORDERED automatically
      }
      else                                  // common messages
      {
        msg->setCallback(serverSendComplete,nsOutputSent,this);
        if ( dependOn ) msg->setOrdered(dependOn);
        msg->setSendTimeout(SEND_TIMEOUT);
      }
      msg->setData((unsigned8*)buffer,bufferSize);
      msg->send(urgent);
    }
#ifdef NET_LOG_SERVER_SEND
    NetLog("Channel(%u): NetServer::SendMsg: to=%d, len=%3d, flags=%x, msgID=%x, hdr=%08x%08x",
           channel->getChannelId(),to,bufferSize,(unsigned)flags,(unsigned)msg->id,
           ((unsigned*)msg->getData())[0],((unsigned*)msg->getData())[1]);
#endif
  }

  msgID = (DWORD)msg->id;
  leaveUsr();
  return msg;
}

void NetServer::CancelAllMessages ()
{
  poolLock.enter();
  if ( GET_SERVER_PEER(false) )
    GET_SERVER_PEER(false)->cancelAllMessages();
  poolLock.leave();
}

void NetServer::UpdateSessionDescription (
  int state, RString gameType, const LocalizedString &mission, RString island,
  int timeleft, int difficulty
)
{
  enterUsr();
  session.serverState = state;
  strncpy(session.gameType,gameType,LEN_GAMETYPE_NAME);
  session.gameType[LEN_GAMETYPE_NAME-1] = (char)0;
  session.SetMissionName(mission);
  strncpy(session.island,island,LEN_ISLAND_NAME);
  session.island[LEN_ISLAND_NAME-1] = (char)0;

  session.timeleft = timeleft;
  session.difficulty = difficulty;

  leaveUsr();
}

bool NetServer::GetURL ( char *address, DWORD addressLen )
{
  poolLock.enter();
  bool result = (GET_SERVER_PEER() != NULL);
  if ( result )
  {
    Assert( address );
    struct sockaddr_in local;
    getLocalAddress(local,sessionPort);
    sprintf(address,"%u.%u.%u.%u:%u",
            (unsigned)IP4(local),(unsigned)IP3(local),(unsigned)IP2(local),(unsigned)IP1(local),(unsigned)sessionPort);
  }
  poolLock.leave();
  return result;
}

bool NetServer::GetServerAddress(sockaddr_in &address)
{
  getLocalAddress(address, sessionPort);
  return true;
}

bool NetServer::GetClientAddress(int client, sockaddr_in &address)
{
  RefD<NetChannel> channel;
  if (!users.get(client, channel)) return false;
  channel->getDistantAddress(address);
  return true;
}

int NetServer::channelToPlayer ( NetChannel *ch )
{
  if ( !ch ) return -1;
  IteratorState it;
  int i;
  enterUsr();
  RefD<NetChannel> itch;
  if ( users.getFirst(it,itch,&i) )
    do
      if ( (NetChannel*)itch == ch ) break;
    while ( users.getNext(it,itch,&i) );
  if ( !itch ) i = -1;
  leaveUsr();
  return i;
}

void NetServer::ProcessUserMessages ( UserMessageServerCallback *callback, void *context )
{
  if ( !callback ) return;
  Ref<NetMessage> msg;
  enterRcv();
#ifdef NET_LOG_SERVER_PROCESS
  int n = 0;
  msg = received;
  while ( msg )
  {
    n++;
    msg = msg->next;
  }
  if ( n )
  {
    NetLog("Peer(%u): NetServer::ProcessUserMessages: processed %d messages:",GetServerPeerId(),n);
    msg = received;
    while ( msg )
    {
      NetLog("Channel(%u): NetServer::ProcessUserMessages: len=%3u, serial=%4u, flags=%04x, msgID=%x, hdr=%08x%08x",
             msg->getChannel()->getChannelId(),msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id,
             ((unsigned*)msg->getData())[0],((unsigned*)msg->getData())[1]);
      msg = msg->next;
    }
  }
#endif
  while ( received )
  {
    msg = received;
    received = msg->next;
    msg->next = NULL;
      // process the "msg" message:
    enterUsr();
    int player = channelToPlayer(msg->getChannel());
    leaveUsr();
    leaveRcv();
    if ( player == -1 )
      RptF("No player found for channel %u - message ignored",(unsigned)msg->getChannel());
    else
      (*callback)(player,(char*)msg->getData(),msg->getLength(),context);
    enterRcv();
  }
  leaveRcv();
}

void NetServer::insertReceived ( NetMessage *msg )
    // must be called inside enterRcv()
{
  if (!msg)
  {
    received = NULL;
    return;
  }

  if ( received )
  {
    MsgSerial s = msg->getSerial();
    if ( s < received->getSerial() )
    {
      msg->next = received;
      received = msg;
    }
    else
    {
      NetMessage *ptr = received;
      while ( ptr->next && ptr->next->getSerial() < s )
        ptr = ptr->next;
      msg->next = ptr->next;
      ptr->next = msg;
    }
  }
  else
  {
    msg->next = NULL;
    received = msg;
  }
}

void NetServer::RemoveUserMessages ()
{
  enterRcv();
  Ref<NetMessage> tmp;
  while ( received )
  {
    tmp = received->next;
    received->next = NULL;                  // don't need this message anymore, somebody will recycle it later
    received = tmp;
  }
  leaveRcv();
}

void NetServer::ProcessSendComplete ( SendCompleteCallback *callback, void *context )
{
  if ( !callback ) return;
  enterSnd();
  NetMessage *msg;
#ifdef NET_LOG_SERVER_COMPLETE
  int n = 0;
  msg = sent;
  while ( msg )
  {
    n++;
    msg = msg->next;
  }
  if ( n )
  {
    NetLog("Peer(%u): NetServer::ProcessSendComplete: processed %d messages:",GetServerPeerId(),n);
    msg = sent;
    while ( msg )
    {
      NetLog("Channel(%u): NetServer::ProcessSendComplete: ok=%d(%d), len=%4u, serial=%3u, flags=%04x, MsgID=%x, hdr=%08x%08x",
             msg->getChannel()->getChannelId(),(int)(msg->getStatus()!=nsOutputObsolete),(int)msg->getStatus(),
             msg->getLength(),msg->getSerial(),(unsigned)msg->getFlags(),msg->id,
             ((unsigned*)msg->getData())[0],((unsigned*)msg->getData())[1]);
      msg = msg->next;
    }
  }
#endif
  msg = sent;
  while ( msg )
  {
    (*callback)((DWORD)msg->id,msg->wasSent(),context);
    msg = msg->next;
  }
  RemoveSendComplete();
  leaveSnd();
}

void NetServer::RemoveSendComplete ()
{
  enterSnd();
  Ref<NetMessage> tmp;
  while ( sent )
  {
    tmp = sent->next;
    sent->next = NULL;                      // don't need this message anymore, somebody will recycle it later
    sent = tmp;
  }
  leaveSnd();
}

void NetServer::ProcessPlayers ( CreatePlayerCallback *callbackCreate, DeletePlayerCallback *callbackDelete, void *context )
{
  int i;
  enterUsr();
  if ( _deletePlayers.Size() || _createPlayers.Size() )
    LogF("NetServer::ProcessPlayers(): users.card=%u, session.numPlayers=%d, created=%d, deleted=%d",
         users.card(),session.numPlayers,_createPlayers.Size(),_deletePlayers.Size());
  for ( i = 0; i < _deletePlayers.Size(); i++ )
  {
    DeletePlayerInfo &info = _deletePlayers[i];
    //RptF("NetServer::ProcessPlayers(): delete=%d",info.player);
    callbackDelete(info.player,context);

#if VOICE_OVER_NET
  if ( von.NotNull() && von->getServer().NotNull() )
    von->getServer()->disconnect(info.player);
#endif
    
    session.numPlayers--;
  }
  if ( session.numPlayers < 0 )
    session.numPlayers = 0;
  _deletePlayers.Clear();
  for ( i = 0; i < _createPlayers.Size(); i++ )
  {
    CreatePlayerInfo &info = _createPlayers[i];
    //RptF("NetServer::ProcessPlayers(): create=%d,%s",info.player,info.name);
    callbackCreate(info.player,info.botClient,info.privateSlot,info.name,info.mod,info.inaddr,context);
    session.numPlayers++;
  }
  _createPlayers.Clear();
  leaveUsr();
}

void NetServer::RemovePlayers ()
{
  enterUsr();
  _createPlayers.Clear();
  _deletePlayers.Clear();
  leaveUsr();
}

RString NetServer::GetStatistics ( int player )
{
  enterUsr();
  RefD<NetChannel> channel;
  if ( !users.get(player,channel) )
  {
    leaveUsr();
    return Format("Unknown player ID = %d",player);
  }
  int latencyAve;
  unsigned latencyAct,latencyMin;
  int throughputAve;
  latencyAve = (int)(channel->getLatency(&latencyAct,&latencyMin) / 1000);
  latencyAct /= 1000;
  latencyMin /= 1000;
  EnhancedBWInfo enhanced;
  throughputAve = (int)channel->getOutputBandWidth(&enhanced);
  int nMsg, nBytes, nMsgG, nBytesG;
  channel->getOutputQueueStatistics(nMsg,nBytes,nMsgG,nBytesG);
  ChannelStatistics stat;                 // internal statistics of the NetChannel
  Zero(stat);
  channel->getInternalStatistics(stat);
  leaveUsr();
  char buf[256];
  bool kbps = (throughputAve < (9<<17));
  sprintf(buf,"ping%4dms(%4u,%4u) BW%c%c%c%5d%cb(%4u,%4u,%4u) lost%4.1f%%%%(%3u) queue%4dB(%4d) ackWait%3u(%3.1f,%3.1f)",
              latencyAve,latencyAct,latencyMin,(char)(enhanced.growMode+'c'),(char)(enhanced.growModePing+'c'),
              (char)(enhanced.growModeLost+'c'),kbps?((throughputAve+64)>>7):((throughputAve+65536)>>17),
              kbps?'K':'M',(enhanced.actBW+64)>>7,(enhanced.goodBW+64)>>7,(enhanced.sentBW+64)>>7,
              stat.ackTotal?(stat.ackLost*100.0)/stat.ackTotal:0.0,stat.ackLost,
              nBytes,nBytesG,stat.revisitedNo,1e-6*stat.revisitedAveAge,1e-6*stat.revisitedMaxAge);
#ifdef NET_LOG_TRANSP_STAT
  unsigned64 now = getSystemTime();
  if ( player != botId && (inGetConnection && forceLog || now >= nextStatLog) )
  {
    forceLog = true;
    NetLog("Channel(%u): NetServer(%d)[%d] - %s",channel->getChannelId(),player,getStatisticsCount,buf);
    getStatisticsCount = 0;
    if ( now >= nextStatLog )
      nextStatLog = now + STAT_LOG_INTERVAL;
  }
  else
    getStatisticsCount++;
#endif
  return RString(buf);
}

unsigned NetServer::FreeMemory ()
{
  if ( !NetMessagePool::pool() ) return 0;
  unsigned ret = NetMessagePool::pool()->freeMemory();
  SafeMemoryCleanUp();
  return ret;
}

RefD<NetChannel> NetServer::playerToChannel ( int player )
{
  RefD<NetChannel> channel;
  enterUsr();
  users.get(player,channel);
  leaveUsr();
  return channel;
}

#if VOICE_OVER_NET

bool NetServer::InitVoice ()
{
  assertVON();
  von->setServer( new VoiceServer(von) );
  RefD<VoNClient> vcl = von->getClient();
  if ( vcl )
    von->localConnect();
  return true;
}

void NetServer::GetTransmitTargets ( int from, AutoArray<int,MemAllocSA> &to )
{
  if ( !von ) return;
  RefD<VoNServer> vsr = von->getServer();
  if ( vsr )
    vsr->getChannel(from,to);
}

void NetServer::SetTransmitTargets ( int from, AutoArray<int,MemAllocSA> &to )
{
  if ( !von ) return;
  RefD<VoNServer> vsr = von->getServer();
  if ( vsr )
    vsr->setChannel(from,to);
}

void NetServer::UpdateTransmitTargets()
{
  if ( !von ) return;
  RefD<VoNServer> vsr = von->getServer();
  if ( vsr )
    vsr->updateTransmitTargets();
}

void NetServer::GetVoNTopologyDiag(QOStrStream &out, Array<ConvPair> &convTable)
{
  if ( !von ) return;
  RefD<VoNServer> vsr = von->getServer();
  if ( vsr )
    vsr->GetVoNTopologyDiag(out, convTable);
}

void NetServer::ProcessVoicePlayers ( CreateVoicePlayerCallback *callback, void *context )
{
}

void NetServer::RemoveVoicePlayers ()
{
}

#else

bool NetServer::InitVoice ()
{
  return false;
}

void NetServer::GetTransmitTargets ( int from, AutoArray<int,MemAllocSA> &to )
{
}

void NetServer::SetTransmitTargets ( int from, AutoArray<int,MemAllocSA> &to )
{
}

void NetServer::UpdateTransmitTargets()
{
}

void NetServer::GetVoNTopologyDiag(QOStrStream &out, Array<ConvPair> &convTable)
{
}

void NetServer::ProcessVoicePlayers ( CreateVoicePlayerCallback *callback, void *context )
{
}

void NetServer::RemoveVoicePlayers ()
{
}

#endif  // _VOICE_OVER_NET
