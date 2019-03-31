/*
    Pulsar Server Framework: Framework to develop your high performance heavy duty server in C++
    Copyright (c) 2013-2019 Atul D. Patil (atuldpatil@gmail.com), 

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

/*
Module summary:
	Server applications derives from this class to define their own request processor to process requests from clients.
	Constructor takes protocol version and version specific parameters.	Once defined request processing class and override 
	necessary methods, server application shall create global instance of the derived class.
	Application must need to overrode ProcessRequest function for their request processing. This function is called by framework
	and would be running in multiple threads.
*/

typedef void (ConnectionsManager::*TimerFunction)();
typedef int  (ConnectionsManager::*AddResponseToQueuesFunction)(class Response* pResponse, ClientHandlesPtrs* pClientHandlePtrs, BOOL& bHasEncounteredMemoryAllocationException);

// Default 128k to be allocated to store "handles" in SPECIAL_COMMUNICATION version processor (i.e. for forwarded response) 
// (This in turn means, at a time a server will create and forward response for 16384 clients connected to other server)
// Processor for forwarded responses (RequestProcessor_ForwardedResponses defined in RequestProcessor.cpp) calculates and returns MaxRequestSize based on this.
#define BUFFER_SIZE_IN_KILOBYTES_FOR_HANDLES_IN_SPECIAL_COMMUNICATION 128
#define MAX_HANDLES_IN_FORWARDED_RESPONSE ((1024*BUFFER_SIZE_IN_KILOBYTES_FOR_HANDLES_IN_SPECIAL_COMMUNICATION)/sizeof(((ClientHandle*)0)->m_ClientRegistrationNumber /*We don't include IP address of server in forwarded message*/ ))  // Maximum handles in forwarded response. If there are more handles, multiple responses would be created.
#define MAX_POSSIBLE_REQUEST_RESPONSE_SIZE (1024*1024) // Server application request procesors response size cannot exceed this for their request processor

// Base class for all request versions of request processors
class DLL_API RequestProcessor
{
	uv_async_t m_AsyncHandle; 
	uv_barrier_t m_Barrier;
	Lock m_rwlTotalResponseObjectsQueuedCounterLock, m_rwlTotalResponseObjectsSentCounterLock;
	USHORT m_Version;
	VersionParameters m_VersionParameters;
	Request* m_pRequest;
	class ConnectionsManager* m_pConnectionsManager;
	TimerFunction m_pTimerFunction ;
	AddResponseToQueuesFunction m_pAddResponseToQueuesFunction;
	BOOL m_bRequestIsBeingProcessed, m_bDisconnectionIsBeingProcessed;
	long m_ResponseCountPerThread;
	int m_ResponseObjectsQueued, m_ResponseObjectsSent, m_TotalResponseObjectsQueued;
	static std::map <USHORT, RequestProcessor*> m_VersionAndProcessor; // Contains request processor for its associated version. Populated in c'tor.
	static CommonParameters m_CommonParameters;

	void SetRequest (Request* pRequest);
	void DeleteProcessor();
	int Initialize (uv_loop_t* loop, ConnectionsManager* pConnMan, TimerFunction pTimerFunction, AddResponseToQueuesFunction pAddResponseToQueuesFunction);
	VersionParameters& GetVersionParameters (); // Gets version specific parameters (e.g. MaxRequestSize, MaxResponseSize) which derived class set via constructor
	void CreateResponseAndAddToQueues(const Buffer* response, ClientHandlesPtrs& clienthandle_ptrs, USHORT version /* Version of client who is creating/storing the Response */, BOOL bIsUpdate, double RequestArrivalTime);
	void IncreaseResponseObjectsQueuedCounter();
	int GetTotalResponseObjectsQueued();
	int GetResponseObjectsSent();
	int IncreaseResponseObjectsSentCounter();
	void WaitOnBarrier();
	void StoreError (ClientHandle* clienthandle, UCHAR ErrorCode); // Used by framework to communicate errors to client using master protocol

	/* This function is central place to store error, multicast or ordinary response. This is called from relevent public functions.
	 About version parameter:
	 While storing message (response or error etc.) we need to specify which version processor has stored the response so that client 
	 can decide how to process the response. This is helpful in scenarios when for e.g. v1 processor sends response to all other clients 
	 and one of them is v2. Now since v1 woudn't know response format of v2 (and we are not supposed to modify older version processors 
	 while implementing next version) v1 processor has to mention in response what version of protocol is the response, 
	 so that v2 client can decide (either process response if it is equipped with older processors, or reject response if it is from newer processors) */
	void StoreMessage (ClientHandles* clienthandles, const Buffer* response, USHORT version, BOOL bIsUpdate);

	static int m_NumberOfActiveProcessors;
	static void on_async_handle_closed (uv_handle_t* handle);
	static void send_update_callback (uv_async_t* handle);
	static RequestProcessor* GetNewRequestProcessor (USHORT version); // Returns instance of request processor based on version

	// LocalClientsManager invokes private member functions of this class like GetRequestProcessor, ProcessRequest, ProcessDisconnection, SetParameters etc.
	// These functions are not supposed to be called by derived RequestProcessors (for specific versions) hence they are private.
	friend class LocalClientsManager ;
	friend class ConnectionsManager ;

	/* Application processor must override these funtions and define their own */
	
	/* Allow framework to create instances of your request processor:
	Once you derive your own request processing class (from this class) for a particular version and create global instance of the same, Framework needs a way 
	by which it can create as many instances of the request processor as it needs. (Actual number depends on number of request processing threads and other things).
	For that the framework calls GetAnotherInstance via global instance. This method typicaly have single line definition, for example if you write request processor class
	RequestProcessor_v1 for version 1, then GetAnotherInstance can be defined as:	{return new RequestProcessor_V1(1);} Thats it!
		Remember, Pulsar Server Framework uses the global instance of request processor _ONLY_ to call GetAnotherInstance. This method will be invoked by framework after application 
	calls StartServer method of ConnectionManager class (And application typically would do that via main function). So objects which cannot be initialised globally in corresponding 
	RequestProcessor's constructor (to avoid static initialization order fiasco. For example get_driver_instance call of MySQL), application may initialize such objects when 
	constructor is invoked by this method (which can be detected typically by setting a flag as parameter to c'tor)
	*/
	virtual RequestProcessor* GetAnotherInstance()=0;

	/* Key function to process requests sent by client:
		Application must override this function to process request sent by client. Once request is arrived Framework will call this function of appropriate request processor based on 
		version present in the master protocol. Actual request can be accessed by GetRequest function. The request will be Buffer having serialised protocol defined by application.
		ProcessRequest is run as a thread in threadpool. So its implementation in derived class must employ necessary thread-locks wherever necessary (by calling methods AcquireReadLock, 
		AcquireWiteLock etc). Please refer sample server application to see detailed implementation of ProcessRequest.
	*/
	virtual BOOL ProcessRequest ()=0; // This should return TRUE if it processes request successfully, FALSE otherwise 

	/* Process client disconnection:
		This function is called by framework to allow derived processor to process client disconnection.  This is also invoked by thread. Since there won't be any request 
		associated with disconnection, calls to GetClientHandle would give assertion failure. Hence we are passing client handle as parameter here.
	*/
	virtual void ProcessDisconnection (ClientHandle& clienthandle, void* pSessionData)=0;

	public:
		/* Methods serve as API for application defined request processor */

		/* Application defined constructor:
			Server application has to override their own constructor which has version and its parameters. 
			This is mandatory as this base class has no default constructor. Here, version is esentially protocol version. 
			Based on version present in master protocol of request by client, Pulsar Server Framework will call ProcessRequest method 
			of relevent version's application derived class. Remember, consstructor is called multiple times by framework but 
			from single thread (main thread). Initilazations of members to be done in constructor. SetCommonParameters also needs to be called 
			from constructors if application wants to override default values of common parameters. Common parameters are common 
			across all versions of protocols (Hence all derived classes of RequestProcessor).
		*/
		RequestProcessor(USHORT version, VersionParameters& versionparameters);

		/* Application defined destructor:
			If in derived class constructor server application has initiated things which need to be destroyed, it can override destructor.
			(e.g. memory allocations or lock initilizations). However, if not needed overriding destructor is not mandatory.
		*/
		virtual ~RequestProcessor();

		/* Thread Synchronization:
			Following functions are for locking and unlocking portion of thread-unsafe code (code which need to be executed exclussively by single thread).
			This is because Pulsar Server Framework calls some of RequestProcessor methods (especially ProcessRequest) in threads. And thread unsafe activity 
			(for example increamenting/decreamenting counter which is member variable) can be performaed after calling AcquireWriteLock and before ReleaseReadLock.
			Code after AcquireReadLock can be accessed by multiple threads however code that has called AcquireWriteLock with same Lock will wait till
			those threads acquired read locks calls ReleaseReadLock. Thus AcquireWriteLock provides exclussive access to single thread acquiring the lock till 
			it calls ReleaseWriteLock.
		*/
		int InitializeLock(Lock& lock); // Call only from constructor
		void DestroyLock(Lock& lock); // Call only from destructor

		void AcquireReadLock(Lock& lock); // Multiple threads can acquire same read lock however those trying to call AcquireWriteLock for the same lock have to wait
		void ReleaseReadLock(Lock& lock); // Once threads those acquired read lock calls this, only then thread waiting to acquire write lock can acquire the lock

		void AcquireWriteLock(Lock& lock); // Once a thread calls this and acquires the lock, all other threads trying to acquire same lock in read or write mode have to wait till it calls ReleaseWriteLock
		void ReleaseWriteLock(Lock& lock); // As mentioned above, when thread calls this, one or all other threads waiting for the lock can acquire it (depends on read/write access)

		/* Thread index:
			If method like ProcessRequest at all needs to know what thread currently is being run, it can call this.
		*/
		int GetCurrentThreadIndex ();

		/* Getting actual request as sent by client:
			Application has to call this to access actual request buffer send by client (typically through its own defined ProcessRequest function).
			Request is returned via Buffer structure which is simply a plain character array and it size. This buffer might be serilized form of application's own protocol 
			for its client to communicate with server (for example HTML, XML or even Google Protobuf etc) Application can then process the request by deserializing 
			and extracting all needed parameters out of it.
		*/
		const Buffer GetRequest();

		/* Deferring time consuming request:
			If server application wants certain requests to be deferred (so that it can be processed later some time) it can call this from ProcessRequest.
			Deferred request will be requeued by frammework to arrive again for processing.
		*/
		void DeferRequestProcessing();

		/* Keep memory allocated:
			In default mode, when any client connects and starts sending request, Pulsar Server Framework allocates memory of size VersionParameters.MaxRequestSize 
			(which is 65KB by default) and frees the allocation soon after processing the request. However, if streaming mode turned on, the framework keeps that much memory
			lways allocated for the connected client. Thus it reduces time consumption in allocation and deallocation, which increases performance when streaming.
		*/
		void SetStreamingMode (bool bMode);

		/* Client handle as an abstract:
			Pulsar Server Framework provides server appliction a handle representing connected client. Application can use this handle to communicate with the client.
			Especially when sending back response after processing request or sending any other communication. The handle consists of server IP address to which the client
			is conected to and framework generated registration number of the client on the connected server. Thus application do not need to bother about which server 
			instance/hardware the client has connected to. It just calls StoreResponse and framework will take care of routing the response to the relevent server and 
			then to the relevent client. This is very useful especially there is server farm and thousands of clients of the application are connected to different servers therein.
		*/
		ClientHandle GetRequestSendingClientsHandle();

		/* Protocol version:
			This returns proocol version of request sender client. Although a specific request processor is written for a particular version only, it is quite possible that
			there is a processor derived from some other version and can make calls to functions of older version processor. In that case this call helps accessing appropriate version.
		*/
		USHORT GetClientProtocolVersion();

		/* Session data:
			Application can use this to store client related session data. This data will remain in memory only till client remains connected. Application can use 
			GetSessionData to access the stored data. This is useful especially to store session related data between two consecutive requests from same client.
			However, to store permenant data application need to employ their own mechanism (e.g. database)
		*/
		void SetSessionData (void* pData);
		void* GetSessionData ();

		/* Disconnect client sending request:
			If server application wants to diconnect any client, it can call this method. ClientHandle is handle of the client to be disconnected. Again, no matter
			which server instance/hardware the client is connected, framework takes care of it and disconnects the client.
		*/
		void DisconnectClient(ClientHandle* clienthandle);

		/* Max response size:
			If various version processors have different MaxResponseSize (Specified via request oricesor constructor), this function returns maximum of them all.
		*/
		int GetMaxResponseSizeOfAllVersions();

		/* Hostname:
			GetHostName returns current server's hostname
		*/
		std::string GetHostName();
		
		/* IPv4Address:
			This function returns IPv4 address of current server
		*/
		unsigned int GetServerIPv4Address();
		
		/* Set common server parameter:
			This allows server application to specify common parameters across which are applicable across all versions.
			It is recommanded to call this function only from constructor of request processor having first version. If application
			doesn't call this, the framework continues with default values of the parameters. These are common parameters:
				int MaxPendingResponses: Maximum responses that can remain pending if client doesn't consume them in time (Default: 16)
				int MaxRequestProcessingThreads: Threads to be allocated for request processing (Max value 255, Default: 5)
				int KeepAliveFrequencyInSeconds: Duration (in seconds) which framework send keep alive to each client connected (Default: 30 seconds)
				int StatusUpdateFrequencyInSeconds: Duration by which Pulsar Server Framework keep calling ProcessLog function to update various status and logs (Default: 5 seconds)
		*/
		static void SetCommonParameters(CommonParameters& commonparams);
		
		/* Get common server parameters:
			Returns parameters set by SetCommonParameters
		*/
		static CommonParameters GetCommonParameters();

		/* Send response:
			Server application can use this function to send response to client, typically (but not necessarily always) after request processing.
			As mentioned before ClientHandle is client which might have connected to any server hardware/instance.
			Value of version equal to DEFAULT_VERSION is treated as version of client who is storing this response.
			Unless request processor wants to set different version, it should call this function without version parameter.
		*/
		void SendResponse (ClientHandle* clienthandle, const Buffer* response, USHORT version = DEFAULT_VERSION); // Request processors will use this only, even for current client

		/* Send response to multiple clients:
			Application can call this to send single response to multiple clients at a time. No limit on number of clients. 
			Also they can be clients connected to any server hardware/instance.
			Value of version equal to DEFAULT_VERSION is treated as version of client who is storing this response
			Unless request processor wants to set different version, it should call this function without version parameter
		*/
		void SendResponse (ClientHandles* clienthandles, const Buffer* response, USHORT version = DEFAULT_VERSION);

		/* Functions below are still being evolved as of in their current state, hence not documented. Application should not call them.
		*/
		void SendUpdate (ClientHandle* clienthandle, const Buffer* response, USHORT version = DEFAULT_VERSION);
		void MulticastUpdate (ClientHandles* clienthandles, const Buffer* update, USHORT version = DEFAULT_VERSION);
};
