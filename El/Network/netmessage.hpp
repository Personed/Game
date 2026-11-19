#ifdef _MSC_VER
#  pragma once
#endif

/*
    @file   netmessage.hpp
    @brief  Network message class and support.

    Copyright &copy; 2001-2004 by BIStudio (www.bistudio.com)
    @author PE
    @since  5.12.2001
    @date   15.1.2004
*/

#ifndef _NETMESSAGE_H
#define _NETMESSAGE_H

#define SAFE_HEAP_STAT

//------------------------------------------------------------
//  NetMessage: message send/received by the network layer

class NetMessagePool;

#include <Es/Memory/normalNew.hpp>

/**
    Net-message class.
    <p>Container for send/received data. Network-layer clients should use <code>NetMessage</code>'s
    methods in most cases..
    @since  5.12.2001
    @date   9.10.2003
*/
class NetMessage : public RefCountSafe {

protected:

    /// Associated net-channel instance.
    NetChannel *channel;

    /// Network address of distant peer (network endian).
    struct sockaddr_in distant;

    /// Actual network status.
    NetStatus status;

    union {

            /// Buffer for low-level message header.
        MsgHeader *header;

            /// data buffer (used from <code>data[sizeof(MsgHeader)]</code>.
        unsigned8 *data;

        };

    /// Total length of allocated <code>data[]</code> buffer in bytes.
    unsigned totalLen;

    /// Brutto message length (including MsgHeader).
    unsigned msgLen;

    /// Reference time (absolute receive/send time) in micro-seconds.
    unsigned64 refTime;

    /// Absolute time when the message was sent (dispatched) for the 1st time..
    unsigned64 firstTime;

    /// Relative timeout time (re-send time if not acknowledged) in micro-seconds.
    unsigned64 ackTimeout;

    /// Relative too-old time (timeout for send-pending, too old messages shall not be sent). <code>0</code> if not used.
    unsigned64 sendTimeout;

    /// Remember this message for latency computation?
    bool waitForLatency;

    /// Can this message be secondary (for packet-bunch bw estimation)?
    bool canBeSecondary;

    /// Call-back routine processing message life-cycle events.
    NetCallBack *msgProcessRoutine;

    /// Data passed to msgProcessRoutine.
    void *dta;

    /// Next call-back event.
    NetStatus nextEvent;

    /// Serial number of message requested this MSG_DELAY_FLAG (heart-beat).
    MsgSerial heartBeatRequest;

    /// Incoming time of heart-beat request.
    unsigned64 heartBeatTime;

    /// Predecessor for VIM-ORDERED messages.
    Ref<NetMessage> pred;

    /// Re-initializes (recycles) the class instance.
    void init ();

    /**
        Associates the given net-channel with this message.
        @param  ch NetChannel to be associated with the message.
    */
    virtual void setChannel ( NetChannel *ch );

    friend class NetMessagePool;
    friend unsigned netMessageToUnsigned (const RefD<NetMessage> &msg);
    friend class NetChannelBasic;
	friend class DirectChannel;
    friend THREAD_PROC_RETURN THREAD_PROC_MODE udpSend ( void *param );
    friend THREAD_PROC_RETURN THREAD_PROC_MODE udpListenSend ( void *param );

    /// Next assigned NetMessage id.
    static MsgSerial nextId;

public:

    /// Message ID (unique ID within the whole application).
    MsgSerial id;

    /// Linked-list of NetMessage instances.
    Ref<NetMessage> next;

    /**
        Initializing constructor - creates message buffer of the given length.
        @param  len Maximal physical message length in bytes.
    */
    NetMessage ( unsigned len );

    /**
        Destructor.
        <p>Recycles the message's memory.
    */
    virtual ~NetMessage ();

    /**
        Sets all message data from the given piece of memory.
        In fact, only message length and flags are used.
        @param  hdr Message header (followed by message data) in network format.
    */
    virtual void setMessage ( const MsgHeader *hdr );

    /**
        Sets all control items (except message data itself) from the given message..
    */
    virtual void setFrom ( const NetMessage *msg );

    /**
        Sets internal (user) data of the message.
        @param  _data Data array.
        @param  length Array length (excluding message header).
    */
    virtual void setData ( const unsigned8 *_data, unsigned32 length );

    /**
        Sets message length (netto).
        @param  length Netto message size in bytes (excluding all headers).
    */
    virtual void setLength ( unsigned length );

    /**
        Sets message flags.
        Ignores MSG_DELAYED_FLAG, MSG_FRACTION_FLAG and MSG_ORDERED_FLAG (see other methods
        to set these ones).
        @param  andMask This mask is AND-ed with actual flags first.
        @param  orMask And then this mask is OR-ed together...
    */
    virtual void setFlags ( unsigned16 andMask, unsigned16 orMask );

    /**
        Marks this message as "secondary" for packet-bunch bw estimation.
        @param  bunch Can this message be "secondary".
        @return The message was really marked as secondary.
    */
    virtual bool setBunch ( bool bunch );

    /**
        Sets/unsets this message as "VIM & ORDERED".
        This message will depend on "pred" message.
        @param  pred Predecessor message which will be received before this one. <code>NULL</code>
                     for ORDERED unset.
    */
    virtual void setOrdered ( NetMessage *pred );

    /**
        Sets this message as "VIM & ORDERED", depending on previous VIM message.
    */
    virtual void setOrderedPrevious ();

    /**
        Sets send-timeout for this message (in micro-seconds).
    */
    virtual void setSendTimeout ( unsigned64 timeout );

    /**
        Sets event callback for this message.
        @param  routine Callback-routine (or <code>NULL</code>).
        @param  event Next event to respond to.
		@param	_dta Data passed to call-back routine.
    */
    virtual void setCallback ( NetCallBack *routine, NetStatus event, void *_dta );

    /**
        Dispatch this message to the associated output NetChannel.
        @param  urgent Send the message as urgent?
    */
    virtual void send ( bool urgent =false );

    /**
        Access to message header structure. Use with care!
        @return Pointer to message header data
    */
    virtual MsgHeader *getHeader () const;

    /**
        Access to message flags.
    */
    virtual unsigned16 getFlags () const;

    /**
        Returns length of user-part of the message (EXCLUDING system header).
    */
    virtual unsigned getLength () const;

    /**
        Access to message data. Safe!
        @return Pointer to message data.
    */
    virtual void *getData () const;

    /**
        Retrieves the distant network address (network endian).
    */
    virtual void getDistant ( struct sockaddr_in &_distant ) const;

    /**
        Sets the distant network address (network endian). Valid for control channels only..
    */
    virtual void setDistant ( struct sockaddr_in &_distant );

    /**
        Serial number of the message.
        @return Serial number of the message (or <code>MSG_SERIAL_NULL</code> if
                not assigned yet).
    */
    virtual MsgSerial getSerial () const;

    /**
        Associated net-channel.
        @return Associated net-channel or <code>NULL</code> if the message isn't
                in processing mode.
    */
    virtual NetChannel *getChannel () const;

    /**
        Reference time (receive/send time) in micro-seconds.
        @return Reference time in micro-seconds.
    */
    virtual unsigned64 getTime () const;

    /**
        Retrieves the actual message status.
        @return The actual message status.
    */
    virtual NetStatus getStatus () const;

    /**
        Was this message successfully sent?
        Acknowledgments doesn't matter..
    */
    virtual bool wasSent () const;

    /**
        Cancels the message.
    */
    virtual void cancel ();

    /**
        Recycles the message.
        Returns it to the message-pool for re-use.
    */
    virtual void recycle ();

    bool WasReceived() const
    {
      return status==nsOutputAck;
    }

    /**
        MT-safe new operator.
    */
    static void* operator new ( size_t size );

    static void* operator new ( size_t size, const char *file, int line );

    /**
        MT-safe delete operator.
    */
    static void operator delete ( void *mem );

#ifdef __INTEL_COMPILER

    static void operator delete ( void *mem, const char *file, int line
 );

#endif

    };

#include <Es/Memory/debugNew.hpp>

//------------------------------------------------------------
//  NetMessagePool: NetMessage allocator/manager

extern unsigned netMessageToUnsigned (const RefD<NetMessage> &msg);

extern unsigned netMessageAddressToUnsigned (const RefD<NetMessage> &msg);

#ifdef _WIN32
template ImplicitMap<unsigned,RefD<NetMessage>,netMessageToUnsigned,true,MemAllocSafe>;
#endif

template <>
struct ImplicitMapTraits< RefD<NetMessage> >
{
    static RefD<NetMessage> zombie;
    static RefD<NetMessage> null;
};

#ifdef SAFE_HEAP_STAT

class QOStream;

#include <El/Statistics/statistics.hpp>

#endif

#include <El/FreeOnDemand/memFreeReq.hpp>

/**
    NetMessage allocator/manager.
    <p>Maintains recycle-pool of NetMessage instances. Can be shared between NetPool-s
    and threads.
    @since  5.12.2001
    @date   25.5.2002
*/

class RefDNetMessagePool: public RefD<NetMessagePool>, public MemoryFreeOnDemandHelper
{
	public:
	RefDNetMessagePool(){}
	RefDNetMessagePool(NetMessagePool *pool);
	~RefDNetMessagePool(){}

	// implementation of MemoryFreeOnDemandHelper abstract interface
	virtual size_t FreeOneItem();
	virtual float Priority() const;
  virtual RString GetDebugName() const;
  //! how much memory is controlled by this manager
	virtual size_t MemoryControlled() const;

};

class NetMessagePool : public RefCountSafe {

protected:
  /// Critical section to lock object. RefCountSafe lock cannot be used due to possible DeadLock. 
  PoCriticalSection lock;

  /// Enter the object's critical section.
  inline void enter() const
  { lock.enter(); }

  /// Leave the object's critical section.
  inline void leave() const
  { lock.leave(); }

    /// Non-public constructor (singleton pattern).
    NetMessagePool ();

    /// Singleton instance.
    static RefDNetMessagePool msgPool;

    /// Should be called after complete network layer shutdown is finished!
    virtual ~NetMessagePool ();

    /// Recycled message pool (key is message length).
    ImplicitMap<unsigned,RefD<NetMessage>,netMessageToUnsigned,true,MemAllocSafe> recycled;

    /// Container for used message instances.
    ImplicitMap<unsigned,RefD<NetMessage>,netMessageAddressToUnsigned,true,MemAllocSafe> used;

    /// Counter for automatic garbage-collection of messages.
    int garbageCounter;

public:

    /**
        Public access to the singleton..
        @return The only instance of <code>NetMessagePool</code> class.
    */
    static NetMessagePool *pool ()
    { return msgPool.GetRef(); }

    /**
        Returns a new NetMessage. Exclusive access to this instance is guaranteed!
        @param  minLen Minimal message length in bytes (<b>excluding</b> low-level message header!).
        @param  ch NetChannel the new message will be associated with.
    */
    virtual Ref<NetMessage> newMessage ( unsigned minLen, NetChannel *ch );

    /**
        Recycles the message instance. After this call the message instance can be re-used again..
        @param  msg Reference to the recycled NetMessage.
    */
    virtual void recycleMessage ( NetMessage *msg );

    /**
        Collects all allocated messages which should be recycled.
    */
    virtual void garbageCollect ();

    /**
        Computes total amount of allocated but unused memory in the pool.
        Doesn't run garbageCollect()!
    */
    virtual unsigned unusedMemory ();

    /**
        Frees one item of used pool memory.
        In fact I'm releasing one size-list (the one used less recently).
    */
    virtual unsigned freeOneItem ();

    /**
        Frees all recycled memory.
        @return Amount of freed memory in bytes.
    */
    virtual unsigned freeMemory ();

#ifdef SAFE_HEAP_STAT

    virtual StatisticsByName *getStatistics ();

    virtual StatisticsByName *getStatisticsUsed ();

#endif

    };

#ifdef SAFE_HEAP_STAT

StatisticsByName *messagePoolStatistics ();

StatisticsByName *messagePoolStatisticsUsed ();

#endif

#endif
