// Copyright Matt Wells Nov 2000

// TODO: use temporary bufs for each UdpSlot to avoid mallocs for reads

// . A reliable udp/datagram server
// . non-blocking, no threads
// . works on I/O interrupts by registering callbacks with the Loop class
// . great for broadcasting to many non-local IPs
// . great for communicating with thousands of random machines to avoid
//   the connect/close overhead associated with TCP
// . the UdpSlot holds the details for one transaction
// . the UdpSlot is like a socket
// . we use transactionIds to link incoming replies w/ the initiating requests
// . the key of the UdpSlot is based on transactionId and ip/port of dest. host
// . when sending a request you supply a callback to be called on completion
//   of that transaction
// . UdpSlot's m_errno will be set if an error or timeout occurred
// . UdpSlot's m_readBuf/m_readBufSize will hold the reply
// . you register your request handlers w/ UdpServer::registerHandler()
// . msgs are routed to handling routines based on msgType in the dgram
// . you can change the protocol by changing UdpProtocol
// . UdpProtocol just contains many virtual datagram parsing functions
// . UdpProtocol's default protocol is the Mattster Protocol(see UdpProtocol.h)
// . Dns.h overrides UdpProtocol to form DnsProtocol
// . we can send up to ACK_WINDOW_SIZE dgrams before requiring the reception
//   of the first dgram we sent's ACK
// . this ACK window helps highPing/highBandwidth connections (distant hosts)
// . readPoll(), sendAckPoll(), readTimeoutPoll() can call callbacks/handlers

#ifndef GB_UDPSERVER_H
#define GB_UDPSERVER_H

#include "UdpStatistic.h"
#include "UdpProtocol.h"
#include "GbMutex.h"
#include <inttypes.h>
#include <atomic>


static const int64_t udpserver_sendrequest_infinite_timeout = 999999999999;

class UdpSlot;
class Host;

class UdpServer {
public:
	UdpServer() ;
	~UdpServer() ;

	// free send/readBufs for all slots
	void reset();

	// . returns false and sets errno on error
	// . we use this udp "port" for all this server's reads/writes
	// . we need to save and read out last transaction Id in a file to
	//   maintain the msgdb properly for incremental syncing
	// . niceness of 0 is highest priority, 1 is lowest
	// . read/writeBufSize are the socket buf's size
	// . pollTime is how often to call timePollWrapper() (in milliseconds)
	// . it should be at least the minimal slot timeout
	bool init(uint16_t port, UdpProtocol *proto, int32_t readBufSize, int32_t writeBufSize, int32_t pollTime,
	          int32_t maxSlots, bool isDns);

	// . sends a request
	// . returns false and sets g_errno on error, true on success
	// . callback will be called on reception of reply or on error
	// . we will set errno before calling callback if an error occurred
	// . errno may be ETIMEDOUT
	// . we call destroySlot(slot) after calling the callback
	// . NULLify slot->m_readBuf,slot->m_sendBuf if you don't want them
	//   to be freed by destroySlot(slot)
	// . hostId is used to lookup Host's in g_hostdb to get/put resend time
	// . if an ack isn't received after a certain time we resend the dgram
	// . if endpoint calls sendErrorReply() to reply to you your callback
	//   will be called with errno set to what he passed to sendErrorReply
	// . UdpSlot is like a udp socket
	// . "msgType" may be stored in each dgram's header depending on
	//   if you're using UdpProtocol, DnsProtocol, ...
	// . "msgType" is used to route the request to handling functions
	//   on the remote machine
	// . backoff is how long to wait for an ACK in ms before we resend
	// . we double backoff each time we wait w/o getting any ACK
	// . don't wait longer than maxWait for a resend
	// . if we try to resend a request dgram MORE than "maxResends" times,
	//   we do not resend it and we returns with g_errno set to ENOACK, 
	//   indicating we have not gotten ANY ack for a dgram. if a host dies
	//   we typically have 10-20 seconds or so before marking it as dead.
	//   BUT with this method we should realize it is fruiteless in like
	//   500 ms or so and Multicast or Proxy.cpp can re-send to another 
	//   host. for niceness=0 requests the backoff is usually constant
	//   and set to about 30 ms. so if you set maxResends to 10 that is
	//   probably at least 300 ms of resending tries.
	// . use an ip of 0 and port of 0 if you provide a hostId. use a hostid
	//   of -1 to indicate no hostid.
	bool sendRequest(char *msg,
	                 int32_t msgSize,
	                 msg_type_t msgType,
	                 uint32_t ip,
	                 uint16_t port,
	                 int32_t hostId,
	                 UdpSlot **retSlot, // can be NULL
	                 void *state, // callback state
	                 void (*callback )(void *state, UdpSlot *slot),
	                 int64_t timeout = 60000, // milliseconds
	                 int32_t niceness = 1,
	                 const char *extraInfo = NULL,
	                 int32_t maxResends = -1);

	// . send a reply to the host specified in "slot"
	// . slot is destroyed on error or completion of the send
	// . the "msg" will be freed unless slot->m_sendBufAlloc is set to NULL
	// . backoff is how long to wait for an ACK in ms before we resend
	// . we double backoff each time we wait w/o getting any ACK
	// . don't wait longer than maxWait for a resend
	void sendReply(char *msg, int32_t msgSize, char *alloc, int32_t allocSize, UdpSlot *slot, void *state = NULL,
	               void (*callback2)(void *state, UdpSlot *slot) = NULL);

	// . propagate an errno to the requesting machine
	// . his callback will be called with errno set to "errnum"
	void sendErrorReply(UdpSlot *slot, int32_t errnum);

	// . when a request/msg of type "msgType" is received we call the
	//   corresponding request handler on this machine
	// . use this function to register a handler for a msgType
	// . we do NOT destroy the slot after calling the handler
	// . handler MUST call sendReply() or sendErrorReply() no matter what
	// . returns true if handler registered successfully
	// . returns false on error
	// . if you want the handler to be called while in an async signal
	//   handler then set "isHandlerHot" to true
	bool registerHandler ( msg_type_t msgType, void(* handler)(UdpSlot *,int32_t) );

	// . frees the m_readBuf and m_sendBuf
	// . marks the slot as available
	// . called after callback called for a slot you used to send a request
	// . called after sendReply()/sendErrorReply() completes or has error
	void destroySlot ( UdpSlot *slot );

	// . this will wait until all fully received requests have had their
	//   reply sent to them
	// . in the meantime it will send back error replies to all new 
	//   incoming requests
	// . this will do a blocking close on the listening socket descriptor
	// . returns false if blocked, true otherwise if shutdown immediate
	// . set g_errno on error
	bool shutdown ( bool urgent );

	// try calling makeCallback() on all slots
	bool makeCallbacks(int32_t niceness);

	// cancel a transaction
	void cancel(void *state, msg_type_t msgType);

	// replace ips and ports in outstanding slots
	void replaceHost ( Host *oldHost, Host *newHost );

	int32_t getNumUsedSlots() const;
	int32_t getNumUsedSlotsIncoming() const;

	bool needBottom() const { return m_needBottom; }

	bool getWriteRegistered() const { return m_writeRegistered; }

	bool hasHandler(int i) const { return (m_handlers[i]); }

	void saveActiveSlots(int fd, msg_type_t msg_type);

	// . we need a transaction id for every transaction so we can match
	//   incoming reply msgs with their corresponding request msgs
	// . TODO: should be stored to disk on shutdown and every 1024 sends
	// . store a shutdown bit with it so we know if we crashed
	// . on crashes add 1024 or so to the read value
	// . TODO: make somewhat random cuz it's easy to spoof like it is now
	int32_t m_nextTransId;

	std::vector<UdpStatistic> getStatistics() const;

	GbMutex& getLock() { return m_mtx; }

private:
	static void readPollWrapper(int fd, void *state);
	static void timePollWrapper(int fd, void *state);
	static void sendPollWrapper(int fd, void *state);

	// these *Poll() routines must be public so wrappers can call them

	// . this is called by main/Loop.cpp when m_sock is ready for writing
	// . actually it calls sendPollWrapper()
	// . it sends as much as it can from all UdpSlots until one blocks
	//   or until it's done
	// . sends both dgrams AND ACKs
	bool sendPoll(bool allowResends, int64_t now);

	// called every 30ms to get tokens? not any more...
	void timePoll ( );

	// called by readPoll()/sendPoll()/readTimeoutPoll() to do
	// reading/sending/callbacks in that order until nothing left to do
	void process(int64_t now, int32_t maxNiceness = 100);

	// . this is called by main/Loop.cpp every second
	// . actually it calls readTimeoutPollWrapper()
	// . call the callback of reception slots that have timed out
	// . return true if we did something, like reset a slot for resend
	//   or timed a slot out so it's callback should be called
	bool readTimeoutPoll ( int64_t now ) ;

	// available linked list functions (m_availableListHead)
	void addToAvailableLinkedList_unlocked(UdpSlot *slot);
	UdpSlot* removeFromAvailableLinkedList_unlocked();

	// callback linked list functions (m_callbackListHead)
	void addToCallbackLinkedList_unlocked(UdpSlot *slot);
	bool isInCallbackLinkedList_unlocked(UdpSlot *slot);
	void removeFromCallbackLinkedList_unlocked(UdpSlot *slot);

	// active linkedlist functions (m_activeListHead)
	void addToActiveLinkedList_unlocked(UdpSlot *slot);
	void removeFromActiveLinkedList_unlocked(UdpSlot *slot);

	// . we maintain a sequential list of transaction ids to guarantee
	//   uniquness to a point
	// . if server is restarted this will go back to 0 though 
	// . the key of a UdpSlot is based on this, the endpoint ip/port and
	//   whether it's a request/reply by/from us
	int32_t getTransId_unlocked();

	void destroySlot_unlocked(UdpSlot *slot);

	// . send as many dgrams as you can from slot's m_sendBuf
	// . returns false and sets errno on error, true otherwise
	bool doSending_unlocked(UdpSlot *slot, bool allowResends, int64_t now);

	// . calls a m_handler request handler if slot->m_callback is NULL
	//   which means it was an incoming request
	// . otherwise calls slot->m_callback because it was an outgoing
	//   request
	bool makeCallback(UdpSlot *slot);

	// . picks the slot that is most caught up to it's ACKs
	// . picks resends first, however
	// . then we send a dgram from that slot
	UdpSlot *getBestSlotToSend_unlocked(int64_t now);

	// . reads a pending dgram on the udp stack
	// . returns -1 on error, 0 if blocked, 1 if completed reading dgram
	// . called by readPoll()
	int32_t readSock(UdpSlot **slot, int64_t now);

	void sendReply_unlocked(char *msg, int32_t msgSize, char *alloc, int32_t allocSize, UdpSlot *slot, void *state = NULL,
	                        void (*callback2)(void *state, UdpSlot *slot) = NULL);

	void sendErrorReply_unlocked(UdpSlot *slot, int32_t errnum);

	// . we have up to 1 handler routine for each msg type
	// . call these handlers for the corresponding msgType
	// . msgTypes go from 0 to 64 i think (see UdpProtocol.h dgram header)
	void (* m_handlers[MAX_MSG_TYPES])(UdpSlot *slot, int32_t niceness);

	mutable GbMutex m_mtx; //mutex protecting this instance.

	// when a call to sendto() blocks we set this to true so Loop.cpp
	// will know to manually call sendPoll() rather than counting
	// on receiving a fd-ready-for-writing signal for this UdpServer
	bool m_needToSend;

	// our listening/sending udp socket and port
	int m_sock;
	uint16_t m_port;

	// for defining your own protocol on top of udp
	UdpProtocol *m_proto;

	bool m_isShuttingDown;

	// did we have to give back control before we called all of the
	bool m_needBottom;

	bool m_writeRegistered;

	// . how many requests are we handling at this momment
	// . does not include requests whose replies we are sending, only
	//   those whose replies have not yet been generated
	// . starts counting as soon as first dgram of request is recvd
	int32_t m_requestsInWaiting;

	// like m_requestsInWaiting but requests which spawn other requests
	int32_t m_msg07sInWaiting;
	int32_t m_msg25sInWaiting;
	int32_t m_msg39sInWaiting;
	int32_t m_msg20sInWaiting;
	int32_t m_msg0csInWaiting;
	int32_t m_msg0sInWaiting;

	// but alloc MAX_UDP_SLOTS of these in init so we don't blow the stack
	UdpSlot *m_slots;
	int32_t m_maxSlots;

	// routines
	UdpSlot *getEmptyUdpSlot_unlocked(key96_t k, bool incoming);
	void freeUdpSlot_unlocked(UdpSlot *slot);

	void addKey_unlocked(key96_t key, UdpSlot *ptr);

	// verified these are only called from within _ass routines that
	// turn them interrupts off before calling this
	UdpSlot *getUdpSlot_unlocked(key96_t k);

	// . hash table for converting keys to slots
	// . if m_ptrs[i] is NULL, ith bucket is empty
	UdpSlot **m_ptrs;
	int32_t m_numBuckets;
	uint32_t m_bucketMask;
	char *m_buf;     // memory to hold m_ptrs
	int32_t m_bufSize;

	// linked list of available slots (uses UdpSlot::m_next)
	UdpSlot *m_availableListHead;

	// linked list of slots in use
	UdpSlot *m_activeListHead;
	UdpSlot *m_activeListTail;

	// linked list of callback candidates
	UdpSlot *m_callbackListHead;
	UdpSlot *m_callbackListTail;

	int32_t m_numUsedSlots;
	std::atomic<int32_t> m_numUsedSlotsIncoming;


	bool m_isDns;

public:
	// stats
	std::atomic<int64_t> m_eth0BytesIn;
	std::atomic<int64_t> m_eth0BytesOut;
	std::atomic<int64_t> m_eth0PacketsIn;
	std::atomic<int64_t> m_eth0PacketsOut;
	std::atomic<int64_t> m_eth1BytesIn;
	std::atomic<int64_t> m_eth1BytesOut;
	std::atomic<int64_t> m_eth1PacketsIn;
	std::atomic<int64_t> m_eth1PacketsOut;

	std::atomic<int64_t> m_outsiderPacketsIn;
	std::atomic<int64_t> m_outsiderPacketsOut;
	std::atomic<int64_t> m_outsiderBytesIn;
	std::atomic<int64_t> m_outsiderBytesOut;
};

extern class UdpServer g_udpServer;

extern int32_t g_dropped;

#endif // GB_UDPSERVER_H
