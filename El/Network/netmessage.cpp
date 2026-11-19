/**
    @file   netmessage.cpp
    @brief  Network message class and support.

    Copyright &copy; 2001-2004 by BIStudio (www.bistudio.com)
    @author PE
    @since  5.12.2001
    @date   14.1.2004
*/

#include "El/Network/netpch.hpp"

//------------------------------------------------------------
//  NetMessage: message send/received by the network layer

MsgSerial NetMessage::nextId = 1;

NetMessage::NetMessage ( unsigned len )
{
    LockRegister(lock,"NetMessage");
    if ( len < MSG_HEADER_LEN ) len = MSG_HEADER_LEN;
    data = (unsigned8*)safeNew(totalLen=len);
    msgProcessRoutine = NULL;
    dta = NULL;
    init();
}

MsgHeader *NetMessage::getHeader () const
{
    return header;
}

unsigned16 NetMessage::getFlags () const
{
    return header->flags;
}

unsigned NetMessage::getLength () const
{
    return( msgLen - MSG_HEADER_LEN );
}

void *NetMessage::getData () const
{
    return( data ? data + sizeof(MsgHeader) : NULL );
}

void NetMessage::getDistant ( struct sockaddr_in &_distant ) const
{
    _distant = distant;
}

void NetMessage::setDistant ( struct sockaddr_in &_distant )
{
    distant = _distant;
}

MsgSerial NetMessage::getSerial () const
{
    return header->serial;
}

NetChannel *NetMessage::getChannel () const
{
    return channel;
}

unsigned64 NetMessage::getTime () const
{
    return refTime;
}

NetStatus NetMessage::getStatus () const
{
    return status;
}

void NetMessage::setFrom ( const NetMessage *msg )
{
    Assert( msg );
    channel            = msg->channel;
    distant            = msg->distant;
    status             = msg->status;
    header->flags      = msg->header->flags & ~(MSG_PART_FLAG|MSG_CLOSING_FLAG);
    header->serial     = msg->header->serial;
    header->ackOrigin  = msg->header->ackOrigin;
    header->ackBitmask = msg->header->ackBitmask;
#ifdef MSG_ID
    header->id         = msg->header->id;
#endif
    refTime            = msg->refTime;
    firstTime          = msg->firstTime;
    ackTimeout         = msg->ackTimeout;
    sendTimeout        = msg->sendTimeout;
    waitForLatency     = msg->waitForLatency;
    canBeSecondary     = msg->canBeSecondary;
    msgProcessRoutine  = msg->msgProcessRoutine;
    dta                = msg->dta;
    nextEvent          = msg->nextEvent;
    heartBeatRequest   = msg->heartBeatRequest;
    heartBeatTime      = msg->heartBeatTime;
    pred               = msg->pred;
    next               = NULL;
}

void NetMessage::init ()
    // re-initialize the message instance
{
    status = nsInvalidMessage;
    msgProcessRoutine = NULL;
    next = NULL;
    pred = NULL;
    channel = NULL;
    memset(data,0,totalLen);
    header->serial = MSG_SERIAL_NULL;
    header->flags = 0;
    msgLen = header->length = MSG_HEADER_LEN;
    header->ackOrigin = MSG_SERIAL_NULL+1;
    header->ackBitmask = 0;
    nextEvent = nsInvalidMessage;
    Zero(distant);
    waitForLatency = false;
    canBeSecondary = true;
    refTime = firstTime = sendTimeout = ackTimeout = 0L;
}

void NetMessage::setChannel ( NetChannel *ch )
{
    channel = ch;
}

void NetMessage::setMessage ( const MsgHeader *hdr )
{
    Assert( hdr );
    Assert( hdr->length );
    Assert( totalLen >= hdr->length );
    memcpy(data,(const char*)hdr,hdr->length);
    msgLen = hdr->length;
    refTime = firstTime = getSystemTime();
}

void NetMessage::setData ( const unsigned8 *_data, unsigned32 length )
{
    Assert( !length || _data );
    if ( length + MSG_HEADER_LEN > totalLen )
        {
            RptF("setData length %d>%d",length,totalLen-MSG_HEADER_LEN);
            Fail("Message data buffer overflow");
            length = totalLen - MSG_HEADER_LEN;
        }
    if ( length ) memcpy(data+MSG_HEADER_LEN,_data,length);
    msgLen = length + MSG_HEADER_LEN;
    header->length = (msgLen > MSG_MAX_LENGTH) ? MSG_MAX_LENGTH : msgLen;
}

void NetMessage::setLength ( unsigned length )
{
    if ( length + MSG_HEADER_LEN > totalLen ) length = totalLen - MSG_HEADER_LEN;
    msgLen = length + MSG_HEADER_LEN;
    header->length = (msgLen > MSG_MAX_LENGTH) ? MSG_MAX_LENGTH : msgLen;
}

void NetMessage::setFlags ( unsigned16 andMask, unsigned16 orMask )
{
    andMask |=  (MSG_DELAY_FLAG | MSG_BANDWIDTH_FLAG | MSG_ORDERED_FLAG |
                 MSG_BUNCH_FLAG | MSG_DUMMY_FLAG);
    orMask  &= ~(MSG_DELAY_FLAG | MSG_BANDWIDTH_FLAG | MSG_ORDERED_FLAG |
                 MSG_BUNCH_FLAG | MSG_DUMMY_FLAG);
    header->flags &= andMask;
    header->flags |= orMask;
}

bool NetMessage::setBunch ( bool bunch )
{
    if ( bunch && canBeSecondary ) {
        header->flags |= MSG_BUNCH_FLAG;
        return true;
        }
    header->flags &= ~MSG_BUNCH_FLAG;
    return false;
}

void NetMessage::setOrdered ( NetMessage *_pred )
{
    if ( _pred ) {                          // set VIM & ORDERED
        if ( !(_pred->header->flags & MSG_VIM_FLAG) ) return;
        header->flags |= (MSG_ORDERED_FLAG | MSG_VIM_FLAG);
        if ( _pred->getSerial() == MSG_SERIAL_NULL )
            pred = _pred;                   // deferred
        else {
            pred = NULL;                    // I am able to set serial# of "pred" now
            header->c.control2 = _pred->getSerial();
            }
        }
    else {                                  // unset ORDERED
        header->flags &= ~MSG_ORDERED_FLAG;
        pred = NULL;
        }
}

void NetMessage::setOrderedPrevious ()
{
    Assert( channel );
    channel->AddRef();
    pred = channel->getLastVIM( (header->flags & MSG_URGENT_FLAG) != 0 );
    channel->Release();
    if ( pred ) {
        header->flags |= (MSG_ORDERED_FLAG | MSG_VIM_FLAG);
        if ( pred->header->flags & MSG_URGENT_FLAG )
            header->flags |= MSG_URGENT_FLAG;
        if ( pred->getSerial() != MSG_SERIAL_NULL ) { // not deferred
            header->c.control2 = pred->getSerial();
            DoAssert( header->c.control2 != MSG_SERIAL_NULL );
            pred = NULL;                    // I am able to set serial# of "pred" now
            }
        }
    else
        header->flags &= ~MSG_ORDERED_FLAG;
}

void NetMessage::setSendTimeout ( unsigned64 timeout )
{
    sendTimeout = timeout;
}

void NetMessage::setCallback ( NetCallBack *routine, NetStatus event, void *_dta )
{
    msgProcessRoutine = routine;
    dta = _dta;
    nextEvent = routine ? event : nsInvalidMessage;
}

void NetMessage::send ( bool urgent )
{
    if ( channel ) channel->dispatchMessage(this,urgent);
}

NetMessage::~NetMessage ()
{
#ifdef NET_LOG_DESTRUCTOR
    NetLog("~NetMessage[%08x]: serial=%4u, flags=%04x, len=(%3u,%3u)",
           (unsigned)this,header->serial,header->flags,msgLen,totalLen);
#endif
    init();
    if ( data ) {
        safeDelete(data);
        data = NULL;
        }
}

bool NetMessage::wasSent () const
{
    return( status == nsOutputSent ||
            status == nsOutputTimeout ||
            status == nsOutputAck );
}

void NetMessage::cancel ()
{
    status = nsCancel;
    if ( msgProcessRoutine && nextEvent != nsNoMoreCallbacks )
        nextEvent = (*msgProcessRoutine)(this,nsCancel,dta);
}

void NetMessage::recycle ()
{
    NetMessagePool::pool()->recycleMessage(this);
}

#include <Es/Memory/normalNew.hpp>

void* NetMessage::operator new ( size_t size )
{
    return safeNew(size);
}

void* NetMessage::operator new ( size_t size, const char *file, int line )
{
    return safeNew(size);
}

void NetMessage::operator delete ( void *mem )
{
    safeDelete(mem);
}

#include <Es/Memory/debugNew.hpp>

//------------------------------------------------------------
//  NetMessagePool: NetMessage allocator/manager


const unsigned NET_MESSAGE_POOL_TRY = 32;   ///< How many slots I'll try if no exact size-match is found..

const unsigned NET_MESSAGE_POOL_TRY_REL = 8;   ///< relative size in n/32 to try if no exact size-match is found..

#ifdef NET_TEST
const int NET_MESSAGE_POOL_GARBAGE = 10000; ///< Periodic garbageCollect() invocation.
#else
const int NET_MESSAGE_POOL_GARBAGE = 1000;  ///< Periodic garbageCollect() invocation.
#endif
const unsigned MAX_RECYCLED_MESSAGE_SIZE = 512;
                                            ///< Messages of size >= MAX_RECYCLED_MESSAGE_SIZE are disposed immediately..

unsigned netMessageToUnsigned (const RefD<NetMessage> &msg)
{
    if ( !msg ) return 0;
    return( msg->totalLen - MSG_HEADER_LEN );
}

RefD<NetMessage> ImplicitMapTraits< RefD<NetMessage> >::zombie = (NetMessage*)1;
RefD<NetMessage> ImplicitMapTraits< RefD<NetMessage> >::null;

unsigned netMessageAddressToUnsigned (const RefD<NetMessage> &msg)
{
    return (unsigned)msg.GetRef();
}

NetMessagePool::NetMessagePool () : recycled(400), used(12)
{
    LockRegister(lock,"NetMessagePool");
    garbageCounter = NET_MESSAGE_POOL_GARBAGE;
}

RefDNetMessagePool NetMessagePool::msgPool(new NetMessagePool);

RefDNetMessagePool::RefDNetMessagePool(NetMessagePool *pool)
:RefD<NetMessagePool>(pool)
{
  RegisterFreeOnDemandMemory(this);
}

size_t RefDNetMessagePool::FreeOneItem()
{
  NetMessagePool *pool = GetRef();
  if (!pool) return 0;
  return pool->freeOneItem();
}

float RefDNetMessagePool::Priority() const
{
  return 0.2f;
}
RString RefDNetMessagePool::GetDebugName() const
{
  return "NetMsg";
}

size_t RefDNetMessagePool::MemoryControlled() const
{
  NetMessagePool *pool = GetRef();
  if (!pool) return 0;
  return pool->unusedMemory();
}

NetMessagePool::~NetMessagePool ()
{
#ifdef NET_LOG_DESTRUCTOR
    NetLog("~NetMessagePool[%08x]: used=%u, recycled=%u",
           (unsigned)this,used.card(),recycled.card());
#endif
}

Ref<NetMessage> NetMessagePool::newMessage ( unsigned minLen, NetChannel *ch )
{
    enter();
    if ( --garbageCounter <= 0 ) {
#ifdef TEST_NET_FREE_MEMORY
        static int freeMemoryCounter = 10;
	if ( --freeMemoryCounter <= 0 ) {
            unsigned freed = freeMemory();
#ifdef NET_LOG
            NetLog("Freeing up the NetMessagePool memory: %u bytes",freed);
#else
            LogF("Freeing up the NetMessagePool memory: %u bytes",freed);
#endif
	    freeMemoryCounter = 10;
	    }
	else {
            garbageCounter = NET_MESSAGE_POOL_GARBAGE;
            garbageCollect();
	    }
#else
        garbageCounter = NET_MESSAGE_POOL_GARBAGE;
        garbageCollect();
#endif
        }
    RefD<NetMessage> result;
    unsigned taken = minLen;
    if ( minLen < MAX_RECYCLED_MESSAGE_SIZE )
    {
      int tryOverSize = intMax(minLen*NET_MESSAGE_POOL_TRY_REL/32,NET_MESSAGE_POOL_TRY);
      while ( !recycled.get(taken,result) && (++taken < minLen + tryOverSize) ) {}
    }

#ifdef NET_LOG_NEW_MESSAGE
    NetLog("NetMessagePool::newMessage: %s, len=(%u,%u)",(result ? "recycled" : "new"),
           minLen,taken);
#endif
    if ( !result )
        result = new NetMessage(minLen+MSG_HEADER_LEN);
    else {
        NetMessage *next = result->next;
        if ( !next )
            recycled.removeValue(result);
        else
            recycled.put(next);
        result->init();
        }
    used.put(result);
#ifdef MSG_ID
    result->header->id =
#endif
    result->id = NetMessage::nextId++;
    leave();
    result->setChannel(ch);
    return result.GetRef();
}

void NetMessagePool::recycleMessage ( NetMessage *msg )
{
    if ( !msg ) return;
    enter();
    if ( used.presentValue(msg) ) {
#ifdef NET_LOG_RECYCLE_MESSAGE
        NetLog("NetMessagePool::recycleMessage: len=%3u, serial=%4u",msg->getLength(),msg->getSerial());
#endif
        msg->init();
        if ( msg->totalLen - MSG_HEADER_LEN < MAX_RECYCLED_MESSAGE_SIZE) {
            RefD<NetMessage> old;
            recycled.put(msg,&old);
            msg->next = old;
            }
        used.removeValue(msg);
        }
    else {
#ifdef NET_LOG_RECYCLE_MESSAGE
        NetLog("NetMessagePool::recycleMessage: UNKNOWN MESSAGE len=%3u, serial=%4u",msg->getLength(),msg->getSerial());
#endif
        msg->Release();
        }
    leave();
}

void NetMessagePool::garbageCollect ()
{
#ifdef NET_LOG_GARBAGE_COLLECT
    unsigned count = 0;
    unsigned size = 0;
    unsigned start = used.card();
#endif
    IteratorState it;
    enter();
    RefD<NetMessage> msg;
    if ( used.getFirst(it,msg) )
        do {
            while ( msg->RefCounter() > 2 && // look for NetMessage-s referenced only once
                    used.getNext(it,msg) ) ;
            if ( !msg ) break;
#ifdef NET_LOG_GARBAGE_COLLECT
            count++;
            size += msg->totalLen + sizeof(NetMessage);
#endif
            recycleMessage(msg);
            } while ( used.getNext(it,msg) );
#ifdef NET_LOG_GARBAGE_COLLECT
    NetLog("NetMessagePool::garbageCollect: %u (of %u) messages has been recycled (%u bytes)",
           count,start,size);
#endif
    leave();
}

unsigned NetMessagePool::unusedMemory ()
{
  unsigned size = 0;
  enter();
  IteratorState it;
  RefD<NetMessage> msg;
  if ( recycled.getFirst(it,msg) )
    do
      while ( msg )
      {                                     // count one message
        size += msg->totalLen + sizeof(NetMessage);
        msg = (NetMessage*)msg->next;
      }
    while ( recycled.getNext(it,msg) );
  leave();
  return size;
}

unsigned NetMessagePool::freeOneItem ()
{
  unsigned size = 0;
  enter();
  IteratorState it;
  RefD<NetMessage> msg;
  if ( recycled.getFirst(it,msg) )
  {
    MsgSerial oldestId = msg->id;
    RefD<NetMessage> oldest = msg;
    while ( recycled.getNext(it,msg) )
      if ( msg->id < oldestId )
      {
        oldestId = msg->id;
        oldest = msg;
      }
      // I'm going to free the whole "oldest-headed" list
    NetMessage *tmp = oldest;
    while ( tmp )
    {
      size += tmp->totalLen + sizeof(NetMessage);
      tmp = tmp->next;
    }
    recycled.removeValue(oldest);
  }
  leave();
  return size;
}

unsigned NetMessagePool::freeMemory ()
{
    garbageCounter = NET_MESSAGE_POOL_GARBAGE;
    garbageCollect();
    unsigned size = 0;
    enter();
    IteratorState it;
    NetMessage *tmp;
    RefD<NetMessage> msg;
    if ( recycled.getFirst(it,msg) )
        do {
            tmp = msg;
            msg->Release();
            while ( tmp ) {                     // count one message
                if ( tmp->RefCounter() != 1 ) {
                    LogF("NetMessagePool::freeMemory: message has RefCounter!=1");
                    break;
                    }
                size += tmp->totalLen + sizeof(NetMessage);
                tmp = tmp->next;
                }
            msg->AddRef();
            recycled.removeValue(msg);
            } while ( recycled.getNext(it,msg) );
    leave();
    return size;
}

#ifdef SAFE_HEAP_STAT

StatisticsByName *NetMessagePool::getStatistics ()
{
    StatisticsByName *stat = new StatisticsByName;
    Assert( stat );
    enter();
    IteratorState it;
    char buf[16];
    RefD<NetMessage> msg;
    if ( recycled.getFirst(it,msg) )
        do {
            sprintf(buf,"%4u",msg->totalLen);
            int count = 0;
            while ( msg ) {
                count++;
                msg = (NetMessage*)msg->next;
                }
            stat->Count(buf,count);
            } while ( recycled.getNext(it,msg) );
    leave();
    return stat;
}

StatisticsByName *NetMessagePool::getStatisticsUsed ()
{
    StatisticsByName *stat = new StatisticsByName;
    Assert( stat );
    enter();
    IteratorState it;
    char buf[16];
    RefD<NetMessage> msg;
    if ( used.getFirst(it,msg) )
        do {
            sprintf(buf,"%4u",msg->totalLen);
            stat->Count(buf);
            } while ( used.getNext(it,msg) );
    leave();
    return stat;
}

StatisticsByName *messagePoolStatistics ()
{
    if ( !NetMessagePool::pool() ) return NULL;
    return NetMessagePool::pool()->getStatistics();
}

StatisticsByName *messagePoolStatisticsUsed ()
{
    if ( !NetMessagePool::pool() ) return NULL;
    return NetMessagePool::pool()->getStatisticsUsed();
}

#endif
