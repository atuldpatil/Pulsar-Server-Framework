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
class LocalClientsManager is responsible to manage clients connected to local server. 
Primary responsibilities are:
	1. Create socket and start listening on it
	2. Create stClient object and generate client handle for each client connected and register stClient object in ClientsPool
	3. Validate incoming requests
	4. Create request objects and queue them
	5. Send responses from responses queue for client
*/

struct stClient:public stNode
{
	uv_tcp_t m_client; // Initiated in AcceptConnection after each client connects

	/* Connection Related */
	class LocalClientsManager* m_pLocalClientsManager ;
	BOOL m_bIsAccepted, m_bIsReadStarted, m_bIsAddedToPool;
	stClient(uv_tcp_t* server, ServerStat& stServerStat, IPv4Address& ServerIPv4Address);
	uv_tcp_t* m_server ; // Listening server which is common to all clients. Gets initiated in uv_tcp_init in main.
	ClientHandle m_ClientHandle;
	bool m_bDeleted ; // Used only for debugging

	/* Request Related */
	BOOL m_bRejectedPreviousRequestBytes;
	char m_Header[HEADER_SIZE+1]; // To extract version, we need minimum HEADER_SIZE+1 bytes
	USHORT m_Version ; // Version of master protocol used by this client
	uv_buf_t m_Request;
	ULONG m_Request_Index; // Index in the m_Request.base buffer
	BOOL m_bRequestIsBeingProcessed; // Flag that when true indicates request is being processed for this client. Used to process requests synchronously.
	BOOL m_bRequestProcessingFinished;
	int m_RequestSizeFound;
	bool m_bStreaming, m_bRequestMemoryAllocatedForStreaming;

	/* Request Processing Related */
	LockRequestsResponses m_LockRequestsResponses;
	void LoopBack(int RequestLength); // Loops back request to client thus producing quick echo thru event loop itself. Only used for debugging.
	void* m_pSessionData; // Pointer to application defined session data. Application can access this via GetClientData & SetClientData.

	/* Responses Related */
	uv_write_t m_write_req ;
	std::deque<class Response*> m_ResponsesQueue1, m_ResponsesQueue2;
	uv_rwlock_t m_rwlResponsesQueueLock;
	BOOL m_bResponseQueueFull;
	uv_buf_t* m_pResponsesBuffersBeingSent;
	int m_SizeReservedForResponsesBeingSend;

	/* Disconnection Processing Related */
	uv_work_t m_disconnect_work_t;
	bool m_bDisconnectInitiated;
	BOOL m_bToBeDisconnected; // Mark this client to be disconnected and deleted
	uv_rwlock_t m_rwlLockForDisconnectionFlag;
	~stClient(); // Destructor should be called _only_ from callback of uv_close. Instance cannot be deleted elsewhere.
				 // Once called uv_close over connection handle, LIBUV gives framework callback only then instance can be deleted. 

#ifdef GENERATE_PROFILE_DATA
	friend class Profiler;
#endif

	/* Logging and Stat Related */
	ServerStat* m_pServerStat ;

	public:
		/* METHODS TO BE CALLED BY REQUEST PROCESSORS */
		void MarkToDisconnect(BOOL bIsByServer);
		BOOL IsMarkedToDisconnect();
		ClientHandle GetClientHandle();
		void SetSessionData (void* pData);
		void* GetSessionData ();
		USHORT GetVersion();
		void SetStreamingMode(bool bMode);
		class ConnectionsManager* GetConnectionsManager();
};

// LocalClientsManager deals with Clients (struct stClient per each incoming connection) as well as with peer Servers (struct stPeerServer per each remote server) 
// It has private static data shared by all connections.
class DLL_API LocalClientsManager:protected virtual CommonComponents
{
	/* Connection Related */
	uv_tcp_t m_tcp_server ;
	IPv4Address m_ServerIPv4Address;
	int m_ConnectionCallbackError;
	const char* m_HostName;
	class ClientsPool* m_pClientsPool ;
	int AcceptConnection(stClient* pClient);
	uv_getnameinfo_t m_nameinfo_t;
	static void getnameinfo_cb(uv_getnameinfo_t* req, int status, const char* hostname, const char* service);

	/* Keep Alive Related */
	uv_work_t m_keep_alive_work_t;
	static void send_keepalive_thread(uv_work_t* work_t);
	static void after_send_keepalive_thread(uv_work_t* work_t, int status);
	class RequestProcessor* m_pReqProcessorToSendKepAlive;
	
	/* Request Processing Related */
	std::map <USHORT, RequestProcessor*> m_RequestProcessors[MAX_WORK_THREADS];
	uv_rwlock_t m_rwlThreadIndexCounterLock; 
	int m_MaxRequestSizeOfAllVersions, m_MaxResponseSizeOfAllVersions;
	int ThreadIndexCounter;
	uv_rwlock_t m_rwlRequestCountersLock1;
	class Request* CreateRequestAndQueue(uv_buf_t* request, stClient* pClient);
	BOOL IsRequestBeingProcessed(stClient* pClient) ;
	int InitiateRequestProcessorsAndValidateParameters();
	RequestProcessor* GetRequestProcessor(unsigned short version, int threadindex);
	void ExtractRequestOffTheBuffer(stClient* pClient, ssize_t nread);
	void GetRequestBuffer(stClient* pClient, uv_buf_t& request_buffer);
	void ResetRequestBuffer(stClient* pClient);

	/* Responses Related */
	std::set <stClient*> m_RecevingClientsSet1,  m_RecevingClientsSet2;
	uv_rwlock_t m_rwlClientSetLock;
	uv_rwlock_t m_rwlWaitTillResponseForClientIsBeingAdded;
	BOOL AddResponseToQueue(Response* pResponse, stClient* pClient, BOOL& bHasEncounteredMemoryAllocationException);
	BOOL AddToClientSet(std::set<stClient*>* pClientsSet, stClient* pClient, BOOL bTobeLocked);
	
	/* Disconnection Processing Related */
	int QueuedDisconnections;
	BOOL m_bServerStopped; // Turned TRUE by on_server_stopped callback. Accessed by DoPeriodicActivities() via IsServerStopped() to proceed with shutdown when TRUE.
	int m_ClientsClosing ;
	BOOL m_bAllClientsDisconnectedForShutdown ;
	void StopReading(stClient* pClient);
	BOOL DisconnectAndDelete(stClient* pClient, BOOL bIsByServer=TRUE);
	void Shutdown(uv_tcp_t* server);
	void ProcessHeaderError(stClient* pClient, UCHAR ErrorCode);
	static void on_client_closed(uv_handle_t* client);
	static void on_server_stopped(uv_handle_t* client);

	protected:
		uv_rwlock_t m_rwlRequestCountersLock2;

		/* Calls/Callbacks to be called by ConnectionsManager */
		int StartListening(char* IPAddress, unsigned short int IPv4Port);
		void InitiateServerShutdown(); // Calls DisconnectAndDelete for each client to initiate server shutdown. Called through event loop.
		void DeleteRequestProcessors();
		void SendKeepAlive();
		unsigned int GetClientsConnectedCount();
		void SendLocalClientsResponses();
		BOOL IsServerStopped ();
		BOOL HasAllClientsDisconnectedForShutdown();
		BOOL AreClientsClosing();
		int GetActiveProcessors();
		BOOL AddResponseToClientsQueues(Response* pResponse, ClientHandlesPtrs* pClientHandlePtrs, BOOL& bHasEncounteredMemoryAllocationException);
		void AfterSendingLocalClientsResponses(stClient* pClient, Response* pResponse, int status);
		bool DisconnectAllClients();
		static void on_new_client(uv_stream_t* server, int status); 
		static void disconnection_processing_thread(uv_work_t* work_t);
		static void after_disconnection_processing_thread(uv_work_t* work_t, int status);
		static void alloc_buffer(uv_handle_t *handle, uv_buf_t* buf);
		static void request_processing_thread(uv_work_t* work_t);
		static void after_request_processing_thread(uv_work_t* work_t, int status);
		static void on_read (uv_stream_t *client, ssize_t nread, const uv_buf_t* read_bytes);

	public:
		LocalClientsManager();
		~LocalClientsManager();

		/* Methods to be called by RequestResponse */
		IPv4Address GetIPAddressOfLocalServer();
		VersionParameters* GetVersionParameters(USHORT Version);
		int GetMaxRequestSizeOfAllVersions() ;
		int GetMaxResponseSizeOfAllVersions() ;
		std::string GetHostName();
		int GetCurrentThreadIndex();
};
