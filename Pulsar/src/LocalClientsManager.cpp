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

#include "Pulsar.h"

unsigned short int IPv4Address::Port;
// For thread index we have to use OS specific thread local storage syntax
static __declspec(thread) int m_ThreadIndex = -1;

int SetInternalTCPBufferSizes(SOCKET& Socket, DWORD NewBuffSize);

structLockRequestsResponses::structLockRequestsResponses()
{
	Requests = 0;
	Responses = 0;
	LastActivityTime = time(NULL); 
	uv_rwlock_init(&rwLock); 
}

structLockRequestsResponses::~structLockRequestsResponses()
{
	uv_rwlock_destroy(&rwLock); 
}

stClient::stClient(uv_tcp_t* server, ServerStat& stServerStat, IPv4Address& ServerIPv4Address)
{
	m_bIsServer = false;

	m_server = server;

	m_pLocalClientsManager = (LocalClientsManager*) m_server->data;

	m_bIsAccepted = FALSE; m_bIsReadStarted = FALSE; m_bIsAddedToPool = FALSE;

	m_bRequestIsBeingProcessed = FALSE;
	m_bToBeDisconnected = FALSE;
	m_bDisconnectInitiated = false;
	m_disconnect_work_t.data = this; // used to access stClient instance in after_disconnection_processing_thread

	m_pServerStat = &stServerStat; 

	m_pSessionData = NULL;

	// In on_client_closed, which will be called via Disconnect, we'll use this to destruct the instance
	m_client.data = this;
	m_write_req.data = this;

	if (uv_rwlock_init(&m_rwlLockForDisconnectionFlag) < 0)
	{
		LOG (ERROR, "Initializing disconnection flag lock failed");
		throw ClientCreationException();
	}

	if (uv_rwlock_init(&m_rwlResponsesQueueLock) < 0)
	{
		LOG (ERROR, "Initializing response queue lock failed");
		throw ClientCreationException();
	}

	m_Version = UNINITIALIZED_VERSION;

	stServerStat.ClientsConnectedCount++; // We must do this here (before returning anywhere in midst of this c'tor). Because we increase ClientsDisconnectedCount in d'tor

	m_ClientHandle.m_ClientRegistrationNumber = stServerStat.ClientsConnectedCount;
	m_ClientHandle.m_ServerIPv4Address = ServerIPv4Address; // IP address exist as static member of stClient. Let's copy it here as it is a part of client handle.
	m_bDeleted = false;

	m_Request_Index = 0;
	m_Request.base = m_Header;
	m_Request.len = sizeof(m_Header);

	m_bStreaming = false;
	m_bRequestMemoryAllocatedForStreaming = false;

	m_RequestSizeFound = 0;

	m_bRejectedPreviousRequestBytes = FALSE;
	m_bRequestProcessingFinished = TRUE;
	m_bResponseQueueFull = FALSE;

	int MaxPendingResponses = RequestProcessor::GetCommonParameters().MaxPendingResponses;
	// m_ResponsesBeingSentCount = 0;
	int SizeReservedForPendingResponseBuffers = sizeof(uv_buf_t) * MaxPendingResponses;
	m_pResponsesBuffersBeingSent = new uv_buf_t[MaxPendingResponses];
	for (int i=0; i<MaxPendingResponses; i++)
	{
		m_pResponsesBuffersBeingSent[i].base = NULL;
		m_pResponsesBuffersBeingSent[i].len = 0;
	}

	m_pResponsesBeingSent = new Responses ;
	// Let's keep max memory allocated to vector so that we won't get throw when we add elements to it
	m_pResponsesBeingSent->reserve (MaxPendingResponses);
	int SizeReservedForPendingResponsesQueue = sizeof(Responses) + (MaxPendingResponses* sizeof(class Response*));

	m_SizeReservedForResponsesBeingSend = SizeReservedForPendingResponseBuffers + SizeReservedForPendingResponsesQueue ;
}

// To be called _ONLY FROM_ event loop (Lock is used because value of m_bToBeDisconnected is read in IsMarkedToDisconnect() which is 
// called through threads via ClientsPool::IncreaseCountForClient)
void stClient::MarkToDisconnect(BOOL bIsByServer)
{
	LOG (DEBUG, "stClient being marked for disconnect");

	if (m_bToBeDisconnected == TRUE)
		return;

	uv_rwlock_wrlock(&m_rwlLockForDisconnectionFlag);

	m_bToBeDisconnected = TRUE;

	if (bIsByServer)
		m_pServerStat->DisconnectionsByServer++;
	else
		m_pServerStat->DisconnectionsByClients++;

	uv_rwlock_wrunlock(&m_rwlLockForDisconnectionFlag);  
}

// Called by threads (via ClientsPool::IncreaseCountForClient)
BOOL stClient::IsMarkedToDisconnect()
{
	ASSERT(m_bDeleted == false); // When object is not valid referencing bDeleted _itself_ could become invalid and causes assertion (right here in this line)
	uv_rwlock_rdlock(&m_rwlLockForDisconnectionFlag);
	BOOL bFlag = m_bToBeDisconnected;
	uv_rwlock_rdunlock(&m_rwlLockForDisconnectionFlag);
	return bFlag;
}

ClientHandle stClient::GetClientHandle() 
{
	return m_ClientHandle; 
}

void stClient::SetSessionData (void* pData)
{
	m_pSessionData = pData;
}

void* stClient::GetSessionData ()
{
	return m_pSessionData;
}

void stClient::SetStreamingMode(bool bMode)
{
	m_bStreaming = bMode;
}

USHORT stClient::GetVersion() 
{ 
	return m_Version; 
}

ConnectionsManager* stClient::GetConnectionsManager()
{
	return (ConnectionsManager*) m_pLocalClientsManager;
}

stClient::~stClient()
{
	m_bDeleted = true ;
	m_pServerStat->ClientsDisconnectedCount++;
}

LocalClientsManager::LocalClientsManager()
{
	// Initialize members
	QueuedDisconnections=0;
	m_ClientsClosing = 0;
	m_bServerStopped = FALSE; 
	m_bAllClientsDisconnectedForShutdown = FALSE;
	m_MaxRequestSizeOfAllVersions = 0; 
	m_MaxResponseSizeOfAllVersions = 0;
	memset (&m_keep_alive_work_t, NULL, sizeof(uv_work_t));
	ThreadIndexCounter = 0;
	m_ConnectionCallbackError = 0;
	m_nameinfo_t.data = this ;

	// Initialize ClientsPool connection
	m_pClientsPool = new (std::nothrow) ClientsPool;
	ASSERT_THROW(m_pClientsPool, "Error allocating memory to ClientsPool");


	// Initialize locks
	int retval = uv_rwlock_init(&m_rwlThreadIndexCounterLock);
	ASSERT_THROW ((retval >= 0), "Initializing request processor use flags lock failed");

	retval = uv_rwlock_init(&m_rwlRequestCountersLock1);
	ASSERT_THROW ((retval >= 0), "Initializing request counters lock1 failed");
	retval = uv_rwlock_init(&m_rwlRequestCountersLock2);
	ASSERT_THROW ((retval >= 0), "Initializing request counters lock2 failed");

	retval = uv_rwlock_init(&m_rwlClientSetLock);
	ASSERT_THROW ((retval >= 0), "Initializing client set lock failed");

	retval = uv_rwlock_init(&m_rwlWaitTillResponseForClientIsBeingAdded);
	ASSERT_THROW ((retval >= 0), "Initializing lock to wait till response is being added to client failed");
}

int LocalClientsManager::InitiateRequestProcessorsAndValidateParameters()
{
	int RetVal = 0;

	// Initialize request processors for all versions and then validate version parameters
	unsigned short MaxVersionNumber = 0xFFFF;

	// Important: GetNewRequestProcessor calls request processor constructor which is very likely initiate other resources (viz. DB connection)
	// So we should be using RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads instead of using MAX_WORK_THREADS, 
	// otherwise it would cause unnecessary resource consumption (viz. DB connections)
	int MaxReqProThreads = RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads;
	for (int i=0; i<MaxReqProThreads; i++)
	{
		for (int j=0; j<=MaxVersionNumber; j++) 
		{
			RequestProcessor* pRequestProcessor = NULL;
			pRequestProcessor = RequestProcessor::GetNewRequestProcessor(j);

			if (pRequestProcessor == NULL) 
				continue;

			try
			{
				m_RequestProcessors[i][j] = pRequestProcessor;
			}
			catch(std::bad_alloc&)
			{
				continue;
			}

			RetVal = m_RequestProcessors[i][j]->Initialize(loop, (ConnectionsManager*)this, &LocalClientsManager::DoPeriodicActivities, &LocalClientsManager::AddResponseToQueues);
			ASSERT_RETURN (RetVal);
		}
	}

	// To create and send keep alive response we need to have a request processor. We need to have processor of version SPECIAL_COMMUNICATION.
	// Because keep alive is response of "special communication" version with response code KEEP_ALIVE.
	// We'll use MulticastResponse method of this processor. (ProcessRequest method is used to process requests of type "forwarded responses", as that is only request server receives with version SPECIAL_COMMUNICATION)
	m_pReqProcessorToSendKepAlive = RequestProcessor::GetNewRequestProcessor(SPECIAL_COMMUNICATION);
	if (m_pReqProcessorToSendKepAlive == NULL)
		return UV_ENOMEM;

	RetVal = m_pReqProcessorToSendKepAlive->Initialize(loop, (ConnectionsManager*)this, &LocalClientsManager::DoPeriodicActivities, &LocalClientsManager::AddResponseToQueues);
	ASSERT_RETURN (RetVal);

	for (int Version=1; Version<MaxVersionNumber; Version++)
	{
		VersionParameters* VersionParams = GetVersionParameters (Version);

		if (!VersionParams)
			continue;

		if ((VersionParams->m_MaxRequestSize  <= 0) || (VersionParams->m_MaxResponseSize <= 0))
			return UV_EINVAL;

		m_MaxRequestSizeOfAllVersions = (VersionParams->m_MaxRequestSize > m_MaxRequestSizeOfAllVersions) ? VersionParams->m_MaxRequestSize : m_MaxRequestSizeOfAllVersions;
		m_MaxResponseSizeOfAllVersions = (VersionParams->m_MaxResponseSize > m_MaxResponseSizeOfAllVersions) ? VersionParams->m_MaxResponseSize : m_MaxResponseSizeOfAllVersions;
	}

	// Validate some version parameters against common parameters
	CommonParameters ComParams = RequestProcessor::GetCommonParameters();
	return 0;
}

LocalClientsManager::~LocalClientsManager()
{
	// LOG (INFO, "Deleting clients pool");
	DEL(m_pClientsPool);
	
	// LOG (INFO, "Destroying request processor use flag lock");
	uv_rwlock_destroy(&m_rwlThreadIndexCounterLock);

	// LOG (INFO, "Destroying request counter locks");
	uv_rwlock_destroy(&m_rwlRequestCountersLock1);
	uv_rwlock_destroy(&m_rwlRequestCountersLock2);

	// LOG (INFO, "Destroying client set locks");
	uv_rwlock_destroy(&m_rwlClientSetLock);

	// LOG (INFO, "Destroying lock to wait till response is being added to client");
	uv_rwlock_destroy(&m_rwlWaitTillResponseForClientIsBeingAdded);
}

// This is to be called _ONLY_ from event loop. (For e.g. on_read, after_send_response and InitiateServerShutdown() calls it.)
BOOL LocalClientsManager::DisconnectAndDelete(stClient* pClient, BOOL bIsByServer)
{
	ADD2PROFILER;

	ASSERT (pClient);

	// It's quite possible that while disconnection_processing_thread in progress, event loop get another event to call this again
	if (pClient->m_bDisconnectInitiated)
		return FALSE;

	// Mark for disconnection
	pClient->MarkToDisconnect(bIsByServer);

	// Stop reading further requests for this client
	StopReading(pClient);

	// This is an additional check to improve performance. RemoveClient already checks if there are any pending requests/responses.
	// However, we are just trying to lessen the burden over it by having quick check is request is being processed and if request processing has finished.
	if ((IsRequestBeingProcessed(pClient) == TRUE) || (pClient->m_bRequestProcessingFinished != TRUE))
		return FALSE;

	// Remove from pool. If successful, disconnect and delete object
	// We must wait till response gets added to all queues
	uv_rwlock_wrlock(&m_rwlWaitTillResponseForClientIsBeingAdded);
	if ((pClient->m_bIsAddedToPool == FALSE) || (m_pClientsPool->RemoveClient(pClient) == TRUE)) // Removes if there are no requests and responses pending for this client
	{
		if (pClient->m_bIsAccepted != TRUE) 
		{
			LOG ( ERROR, "Attempting to disconnect connection which was never accepted");
			uv_rwlock_wrunlock(&m_rwlWaitTillResponseForClientIsBeingAdded);
			return FALSE;
		}

		pClient->m_bIsAddedToPool = FALSE;
		
		BOOL RetVal = uv_queue_work (loop, &pClient->m_disconnect_work_t, disconnection_processing_thread, after_disconnection_processing_thread);
		ASSERT (RetVal == 0); // uv_queue_work returns non-zero only when disconnection_processing_thread is NULL
	
		QueuedDisconnections++;

		pClient->m_bDisconnectInitiated = true; // Indicates this client successfully added to the disconnection queue.
	}
	uv_rwlock_wrunlock(&m_rwlWaitTillResponseForClientIsBeingAdded);

	if (pClient->m_bDisconnectInitiated)
		return TRUE;
	else
		return FALSE;
}

IPv4Address LocalClientsManager::GetIPAddressOfLocalServer() 
{
	return m_ServerIPv4Address;
}

void LocalClientsManager::disconnection_processing_thread(uv_work_t* work_t)
{
	stClient* pClient = (stClient*)work_t->data;
	ASSERT (pClient != NULL);

	if (pClient->m_Version == UNINITIALIZED_VERSION) // At this stage there is chance that version was not yet initialized
		return;

	// Let's try to get thread index (if already not) either here or in request processing thread
	if (m_ThreadIndex == -1)
	{
		uv_rwlock_wrlock(&pClient->m_pLocalClientsManager->m_rwlThreadIndexCounterLock);
		m_ThreadIndex = pClient->m_pLocalClientsManager->ThreadIndexCounter++;
		uv_rwlock_wrunlock(&pClient->m_pLocalClientsManager->m_rwlThreadIndexCounterLock);

		ASSERT (m_ThreadIndex < RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads);
	}

	// Get request processor associated with this thread
	RequestProcessor* pRequestProcessor = pClient->m_pLocalClientsManager->GetRequestProcessor(pClient->m_Version, m_ThreadIndex);

	if (pRequestProcessor == NULL)
	{
		LOG (ERROR, "Cannot process disconnection for version 0x%X as processor for the version is not available.", pClient->m_Version);
		return;
	}

	ASSERT (pRequestProcessor->m_bDisconnectionIsBeingProcessed == FALSE);
	pRequestProcessor->m_bDisconnectionIsBeingProcessed = TRUE;

	ASSERT (pClient->m_bDeleted == false); // When object is not valid referencing bDeleted _itself_ could become invalid and causes assertion (right here in this line)
	ASSERT (pRequestProcessor); // There is no reason for request processor to be NULL here. We initialize all processors at start only via LocalClientsManager::StartListening.

	pRequestProcessor->ProcessDisconnection(pClient->GetClientHandle(), pClient->m_pSessionData);

	pRequestProcessor->m_bDisconnectionIsBeingProcessed = FALSE;
}

void LocalClientsManager::after_disconnection_processing_thread(uv_work_t* work_t, int status)
{
	ADD2PROFILER;

	if (status < 0)
	{
		LOG (ERROR, "Error occurred in disconnection processing thread. Error code %d (%s)", status, uv_strerror(status));
		return;
	}

	stClient* pClient = (stClient*)work_t->data;
	ASSERT (pClient != NULL);

	pClient->m_pLocalClientsManager->m_ClientsClosing ++;
	uv_close((uv_handle_t*) &pClient->m_client, on_client_closed); // stClient will be closed if it has no request pending but just has stayed connected

	pClient->m_pLocalClientsManager->QueuedDisconnections--;

	// When no more clients exist, no more calls to StartDisconnectionProcess, so no more increments to QueuedDisconnections
	if ((pClient->m_pLocalClientsManager->m_pClientsPool->IsShutdownInitiated()) && (pClient->m_pLocalClientsManager->m_pClientsPool->GetClientsCount()==0) && (pClient->m_pLocalClientsManager->QueuedDisconnections==0))
	{
		LOG (NOTE, "All clients disconnected for shutting down the server.");
		pClient->m_pLocalClientsManager->m_bAllClientsDisconnectedForShutdown = TRUE; // This will enable timer to call shutdown after confirming all clients/servers connections are closed
	}
}

void LocalClientsManager::on_server_stopped(uv_handle_t* server)
{
	LocalClientsManager* pLocalClientsManager = (LocalClientsManager*)server->data;

	ASSERT (pLocalClientsManager && (&pLocalClientsManager->m_tcp_server == (uv_tcp_t *)server));

	pLocalClientsManager->m_bServerStopped = TRUE;

	LOG (NOTE, "Server service stopped.");
}

// This will be called by event loop after it reads keystrokes. 
void LocalClientsManager::InitiateServerShutdown()
{
	static bool bShutdownInitiated = false;

	if (bShutdownInitiated == false)
	{
		// This is one time call by console interactions. 
		// Hence we printing messages staright to console (Logger might not have intiated at this point) 
		LOG (INFO, "Stopping server service.");
		uv_close((uv_handle_t*)&m_tcp_server, on_server_stopped);
	}

	if (m_pClientsPool)
		m_pClientsPool->SetServerShuttingDown();

	bShutdownInitiated = DisconnectAllClients();

	if (!bShutdownInitiated) // This is true when no client was connected (so no servers were also connected) and we want to shutdown
	{
		LOG (INFO, "Waiting for server to be closed");
		m_bAllClientsDisconnectedForShutdown = TRUE; // This will enable timer to call shutdown after confirming all clients/servers connections are closed
	}

	bShutdownInitiated = true;

	LOG (NOTE, "Server Shutdown Initiated");

	return;
}

/* Returns 'true' when clients pool exists and at least one client is there in it. 'false' otherwise. */
bool LocalClientsManager::DisconnectAllClients() 
{
	bool RetVal = false;

	if (m_pClientsPool)
	{
		Clients vClients;
		m_pClientsPool->GetClients(vClients);
		int ClientsCount = (int)vClients.size();

		for (int i=0; i<ClientsCount; i++)
		{
			DisconnectAndDelete(vClients[i]);
			RetVal = true;
		}
	}
	
	return RetVal;
}

// Called by event loop through DisconnectAndDelete
void LocalClientsManager::StopReading(stClient* pClient)
{
	if (pClient->m_bIsReadStarted == TRUE)
	{
		uv_read_stop((uv_stream_t*)&pClient->m_client);
		pClient->m_bIsReadStarted = FALSE;
	}
}

// To be called ONLY THROUGH after_request_processing_thread after request has been processed
void LocalClientsManager::ResetRequestBuffer(stClient* pClient)
{
	if ((pClient->m_Request.base != pClient->m_Header) && ((pClient->m_bStreaming == false) || (pClient->m_bRequestMemoryAllocatedForStreaming == false)))
	{
		DEL_ARRAY (pClient->m_Request.base);
		m_stServerStat.MemoryConsumptionByClients -= (pClient->m_bRequestMemoryAllocatedForStreaming ? (GetVersionParameters(pClient->m_Version)->m_MaxRequestSize+HEADER_SIZE) : pClient->m_Request.len) ; 
		m_stServerStat.ActiveClientRequestBuffers -- ;
		pClient->m_Request.base = pClient->m_Header; 
		pClient->m_bRequestMemoryAllocatedForStreaming = false ;
	}

	pClient->m_Request.len = sizeof (pClient->m_Header); 
	pClient->m_Request_Index = 0; 
}

// Called from event loop through alloc_buffer.
void LocalClientsManager::GetRequestBuffer(stClient* pClient, uv_buf_t& request_buffer)
{
	// Check how many bytes remaining after m_Request_index in m_Request.base
	ULONG SizeAvailable = pClient->m_Request.len - pClient->m_Request_Index;

	if ((SizeAvailable == 0) && (pClient->m_RequestSizeFound > 0)) // Non zero m_RequestSizeFound indicates "we need more bytes to extract request"
	{
		ASSERT (pClient->m_Request_Index == sizeof(pClient->m_Header));

		if (pClient->m_Request.base == pClient->m_Header)
		{
			ASSERT (pClient->m_bRequestMemoryAllocatedForStreaming == false);

			// Version is present, request buffer hasn't allocated and request index is non-zero. 
			// Means we've read header for new request arrivaed, validated the same and need more memory for remaining request bytes.
			try
			{
				// This function acceses m_bStreaming which is changed through threads via SetStreamingMode.
				// However, while request is being processed this function won't be called for same client (as request processing is synchronous). 
				// Hence we don't need to have lock around it.
				int MemoryToAllocate = pClient->m_bStreaming ? (GetVersionParameters(pClient->m_Version)->m_MaxRequestSize+HEADER_SIZE) : (pClient->m_RequestSizeFound+HEADER_SIZE);

				pClient->m_Request.base = new char [MemoryToAllocate] ; 
				pClient->m_Request.len = pClient->m_RequestSizeFound+HEADER_SIZE;

				m_stServerStat.MemoryConsumptionByClients += MemoryToAllocate;
				m_stServerStat.ActiveClientRequestBuffers += 1;

				pClient->m_bRequestMemoryAllocatedForStreaming = pClient->m_bStreaming ;

				// Just copy the already read request header from m_Header to newly allocated request base. No need to increase index its already set.
				memcpy_s (pClient->m_Request.base, pClient->m_Request.len, pClient->m_Header, sizeof(pClient->m_Header)); 

				pClient->m_RequestSizeFound = 0;
			}
			catch(std::bad_alloc&)
			{
				pClient->m_Request.base = pClient->m_Header;
				pClient->m_Request.len = sizeof(pClient->m_Header); // This will cause SizeAvailable to become zero resulting in not reading further bytes and calling on_read with UV_ENOBUFS

				// No memory available to allocate request buffer for client. Cannot continue. Disconnect client.
				IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);

				// Comes here when alloc_buffer cannot allocate buffer thus causing no bytes read from libuv internal buffers.
				// If we've got memory allocation error while processing request, we MUST delete the connection sending the request.
				// This is because if we overlook memory allocation errors in request/responses creations, we may endup having lots of connections only and no space to for requests responses.
				// This could be an epic deadlock scene in server framework.
				// If it was per server sending forwarded response as request, it shouldn't come here as  result of no memory in alloc_buffer. Because in case of peer server's SPECIAL_COMMUNICATION
				// we do not free once allocated request base memory in on_read after each request is pulled (under condition (RetVal == WAIT_FOR_MORE_BYTES)) which we do for rest of clients to use server memory efficiently.
				// So we don't need to exclude peer server's client connection here. Ofcourse, if at first allocation itself if peer server as client is failing to allocate then it's worth to discnnect it before we accept any further requests from it.
				if (DisconnectAndDelete(pClient, TRUE))
					LOG (NOTE, "Client buffer is full. Unable to read request. Disconnected.");
			}
		}
		else // .base has been allocated memory and SizeAvailable is zero means we must have streaming on
		{
				ASSERT(pClient->m_bStreaming);
				ASSERT(pClient->m_bRequestMemoryAllocatedForStreaming);

				pClient->m_Request.len = (pClient->m_RequestSizeFound)+HEADER_SIZE;
				pClient->m_RequestSizeFound = 0;
		}

		// Recalculate SizeAvailable
		SizeAvailable = pClient->m_Request.len - pClient->m_Request_Index;
	}

	// If buffer was allocated, we might have it full (because previous request is still being processed) so no room available for more incoming bytes
	// If we were failed to allocate buffer (memory allocation error) then also we won't have any room available for more incoming bytes
	// In either case SizeAvailable can become zero. Only thing is it shouldn't go below zero.

	ASSERT (SizeAvailable >= 0);

	if (pClient->m_Version)
	{
		int MaxRequestSize = GetVersionParameters(pClient->m_Version)->m_MaxRequestSize+HEADER_SIZE;
		if (SizeAvailable > (ULONG)MaxRequestSize) 
			ASSERT(0);
	}

	request_buffer.base = &pClient->m_Request.base [pClient->m_Request_Index];
	request_buffer.len = SizeAvailable; // If SizeAvailable is zero it will cause libuv calling on_read with nread == UV_ENOBUFS(4060) without reading any request bytes
}

void after_write(uv_write_t* write_req, int status); // Used only for debugging

void LocalClientsManager::alloc_buffer(uv_handle_t *client, uv_buf_t* buffer)
{
	ADD2PROFILER;

	stClient* pClient = (stClient*)client->data;
	ASSERT (pClient);

	pClient->m_pLocalClientsManager->GetRequestBuffer(pClient, *buffer); 

	// Make sure when buffer is available we shouldn't have request already being processed
	if ((buffer->len) && (pClient->m_pLocalClientsManager->IsRequestBeingProcessed(pClient) == TRUE))
		ASSERT(0);

	return;
}

// When there is data in socket, libuv calls this with read data in write_req.base, 'nread' as number of bytes read and write_req.len as max size write_req.base
void LocalClientsManager::on_read (uv_stream_t *client, ssize_t nread, const uv_buf_t* read_bytes)
{
	ADD2PROFILER;

	stClient* pClient = (stClient*)client->data ;

	ASSERT(pClient->m_bDeleted == false); // When object is not valid referencing bDeleted _itself_ could become invalid and causes assertion (right here in this line)

    if ((nread <= 0)  || (pClient->m_Request.base == NULL))
	{
		if ((nread != UV_ENOBUFS) && (nread != 0))	// UV_ENOBUFS indicates our own buffer is full because previous request is still being processed
													// or no memory available to allocate for request (in which case disconnection is already done in exception)
		{
			LOG (INFO, "Error %d (%s) in on_read. Client (Version 0x%X) is being disconnected.", (int)nread, uv_strerror((int)nread), pClient->m_Version);
			pClient->m_pLocalClientsManager->DisconnectAndDelete(pClient, FALSE);
		}

		return;
    }

	if ((pClient->m_pLocalClientsManager->m_pClientsPool->IsShutdownInitiated() == TRUE) || (pClient->m_bToBeDisconnected == TRUE))
	{
		pClient->m_Request_Index = 0;
		pClient->m_pLocalClientsManager->m_stServerStat.RequestBytesIgnored += nread;
		pClient->m_bRejectedPreviousRequestBytes = TRUE;

		return;
	}

	pClient->m_pLocalClientsManager->ExtractRequestOffTheBuffer(pClient, nread);
}

void LocalClientsManager::ExtractRequestOffTheBuffer(stClient* pClient, ssize_t nread)
{
	/*	
	Note: It is observed that when server is bombarded with enormous asynchronous requests without any delay in betwen them,
	LIBUV keeps calling only on_read contineously. This issue has been raised over here:
	https://groups.google.com/forum/#!topic/libuv/DpkBVLeOcbA		
	Enforcing CommonParameters.MaxPendingRequests will help us returning at early stage and avoiding possible memory and CPU hog 
	because of overwhelming asynchronous requests also defending possible DoS attacks.
	[Typical symptoms without this check is when attacked with millions of asynchronous requests by 111 simulteneous clients,
	First #RequestsProcessed and then #RespnsesInQueue attains constant value (Ideally they should be increasing consistant manner)
	Also, Ctrl+P becomes irresponsive (Ideally it invokes LogStat routine)]
	So, before we start, check if client has already allowed number of pending requests.

	We also should check if system shutdown has been initiated. In case of continious asynchronous requests and if
	CommonParameters.MaxPendingRequests is big enough, incoming requests still keep queuing despite shutdown requested.

	Also, Do not entertain requests for stClient marked to disconnect, as such clients may not have enough space allocated (to m_Request) 
	for incoming requests and could result in assertion failure.

	If eithr of them is true, we should return from here itself.
	*/

	pClient->m_Request_Index += (unsigned int) nread; //  Windows x64 uses the LLP64 programming model, in which int and long remain 32 bit

	uv_buf_t request;
	request.base = NULL;
	request.len = 0;

	UCHAR RetVal = RequestParser::GetInstance(dynamic_cast<ConnectionsManager*>(pClient->m_pLocalClientsManager))->ValidateProtocolAndExtractRequest (pClient->m_Request.base, pClient->m_Request_Index, pClient->m_Version, request);

	ASSERT (pClient->m_Request_Index <= pClient->m_Request.len); 

	switch(RetVal)
	{
		case REQUEST_FOUND:
		{
			ASSERT (request.base != NULL);
			ASSERT (request.base >= (pClient->m_Request.base));
			ASSERT ((request.base + request.len) <= (pClient->m_Request.base + pClient->m_Request_Index));

			unsigned int TotalRequestSize = HEADER_SIZE + request.len ;
			ASSERT (TotalRequestSize <= pClient->m_Request_Index);
			ASSERT (TotalRequestSize <= pClient->m_Request.len);

			// New request found so first reset previous request bytes rejected flag
			pClient->m_bRejectedPreviousRequestBytes = FALSE;

			Request* pRequest = NULL ;

			pRequest = pClient->m_pLocalClientsManager->CreateRequestAndQueue(&request, pClient); 

			if (pRequest == NULL)
			{
				pClient->m_Request_Index = 0;
				pClient->m_pLocalClientsManager->m_stServerStat.RequestBytesIgnored += request.len;
				pClient->m_pLocalClientsManager->m_stServerStat.RequestsRejectedByServer ++;
			}
		}
		break;

		case INVALID_HEADER:
		case INVALID_VERSION:
		case INVALID_SIZE:
		{	
			pClient->m_pLocalClientsManager->m_stServerStat.RequestBytesIgnored += pClient->m_Request_Index;
			pClient->m_Request_Index=0;
			if (pClient->m_bRejectedPreviousRequestBytes != TRUE)
				pClient->m_pLocalClientsManager->ProcessHeaderError(pClient, RetVal);
		}
		break;

		case WAIT_FOR_MORE_BYTES:
		{
			if (pClient->m_Request_Index == sizeof(pClient->m_Header))
			{
				ASSERT (pClient->m_bRequestMemoryAllocatedForStreaming ? TRUE : pClient->m_Request.base == pClient->m_Header);
				ASSERT (request.len > 0);  // We must have positive value obtained here
				ASSERT (pClient->m_Request.len == sizeof(pClient->m_Header)); 

				// When .base is pointing to either static m_Header or m_AllocationForStreaming and index is at end of it, 
				// master protocol validator must have read length of new request in request.len
				// We need to store the same so that it can be used for allocation for the request
				pClient->m_RequestSizeFound = request.len ;
			}
			else if (pClient->m_Request_Index > sizeof(pClient->m_Header))
			{
				ASSERT (pClient->m_Request.base != pClient->m_Header);
				ASSERT (request.len > 0);

				// Whether it is streaming or regular communication m_Request.len must be equal to length of request + HEADER_SIZE
				ASSERT (pClient->m_Request.len == (request.len + HEADER_SIZE)); 

				// If m_Request.base is not pointing to m_Header and if we are "waiting for more bytes", index must be less than request length 
				ASSERT (pClient->m_Request_Index < pClient->m_Request.len);
			}
			else // Index is less than sizeof(pClient->m_Header)
			{
				ASSERT (pClient->m_bRequestMemoryAllocatedForStreaming ? TRUE : pClient->m_Request.base == pClient->m_Header);
				ASSERT (pClient->m_Request.len == sizeof(pClient->m_Header));
			}
		}
		break;

		default:
		{
			// No need to assert here. pRequest could have been null when cannot allocate memory.
			LOG (ERROR, "Unknown return code by ValidateProtocolAndExtractRequest OR pRequest was NULL when RetVal was REQUEST_FOUND");
			pClient->m_pLocalClientsManager->m_stServerStat.RequestBytesIgnored += pClient->m_Request_Index;
			pClient->m_Request_Index=0;
		}
	}

	return;
}

// Called by event loop (on_read)
void LocalClientsManager::ProcessHeaderError(stClient* pClient, UCHAR ErrorCode)
{
	ADD2PROFILER;

	switch (ErrorCode)
	{
		case INVALID_HEADER: 
			LOG (ERROR, "Invalid preamble in header. Disconnecting client.");
			m_stServerStat.HeaderErrorInPreamble ++; 
			break;

		case INVALID_VERSION : 
			LOG (ERROR, "Invalid version in header. Disconnecting client.");
			m_stServerStat.HeaderErrorInVersion ++; 
			break;

		case INVALID_SIZE	 : 
			LOG (ERROR, "Invalid size in header. Disconnecting client.");
			m_stServerStat.HeaderErrorInSize ++; 
			break;
	}
	
	DisconnectAndDelete(pClient); // We are not reacting in any manner on malformed header (As it will add overhead which hampers request processing rate)
}

VersionParameters* LocalClientsManager::GetVersionParameters(unsigned short Version)
{
	ASSERT (Version != UNINITIALIZED_VERSION);

	RequestProcessor* pRequestProcessor = GetRequestProcessor(Version, 0);

	if (pRequestProcessor)
		return &pRequestProcessor->GetVersionParameters(); 

	return NULL;
}

int LocalClientsManager::GetMaxRequestSizeOfAllVersions()
{
	return m_MaxRequestSizeOfAllVersions;
}

int LocalClientsManager::GetMaxResponseSizeOfAllVersions() 
{
	return m_MaxResponseSizeOfAllVersions;
}

// To be called ONLY FROM event loop
BOOL LocalClientsManager::IsRequestBeingProcessed(stClient* pClient) 
{ 
	uv_rwlock_rdlock(&m_rwlRequestCountersLock2);
	BOOL bIsRequestBeingProcessed = pClient->m_bRequestIsBeingProcessed; 
	uv_rwlock_rdunlock(&m_rwlRequestCountersLock2);
	return bIsRequestBeingProcessed;
}

// Called thru event loop (on_read)
Request* LocalClientsManager::CreateRequestAndQueue(uv_buf_t* request, stClient* pClient)
{
	ADD2PROFILER;

	// stClient* pClient=NULL;
	Request* pRequest=NULL;

	static int RequestCount=0;

	// Create request object. When nothrow is used as argument for new, it returns a null pointer instead of throwing bad_alloc exception.
	ASSERT (&request->base[request->len-1] <= &pClient->m_Request.base[pClient->m_Request.len-1]);
	ASSERT (pClient->m_Request_Index <= pClient->m_Request.len);

	try
	{
		uv_rwlock_rdlock(&m_rwlRequestCountersLock2);
		INT64 MemoryConsumptionByRequestsInQueue = m_stServerStat.MemoryConsumptionByRequestsInQueue;
		uv_rwlock_rdunlock(&m_rwlRequestCountersLock2);

		pRequest = new Request (request, ConnectionsManager::GetHighPrecesionTime(), m_pClientsPool, &pClient->m_ClientHandle ); // Will be deleted in request_processing_thread

		m_stServerStat.RequestsArrived ++;  // Total request count till now. Never decreases.

		// We must get request length here because once thread started it deletes request.base and makes length zero
		// which could hapen so fast that call to GetRequest().len immidiate after uv_queue_work may return zero
		uv_rwlock_wrlock(&m_rwlRequestCountersLock2);

		// This assertion is very important as it makes sure no two concurrent requests are created for same client
		ASSERT(pClient->m_bRequestIsBeingProcessed == FALSE);

		pClient->m_bRequestIsBeingProcessed = TRUE;
		m_stServerStat.MemoryConsumptionByRequestsInQueue += (pRequest->GetRequest().len + sizeof(Request));
		uv_rwlock_wrunlock(&m_rwlRequestCountersLock2);

		pClient->m_bRequestProcessingFinished = FALSE;

		int RetVal = uv_queue_work (loop, &pRequest->m_work_t, request_processing_thread, after_request_processing_thread);

		ASSERT (RetVal == 0); // uv_queue_work returns non-zero only when request_processing_thread is NULL
	}
	catch(std::bad_alloc&)
	{
		// If we've got memory allocation error while processing request, we MUST delete the connection sending the request.
		// This is because if we overlook memory allocation errors in request/responses creations, we may endup having lots of connections only and no space to for requests responses.
		// This could be an epic deadlock scene in server framework.
		// At the same time let's exclude peer servers from this frequent allocation/deallocations.
		if (pClient->m_Version != SPECIAL_COMMUNICATION)
			DisconnectAndDelete(pClient); 
		IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
	}
	catch(RequestCreationException&)
	{
		IncreaseExceptionCount(REQUST_CREATION_EXCEPTION, __FILE__, __LINE__);
	}

	// We are calling DoPeriodicActivities here as well. In case when server is bombarded with requests, it may keep calling CreateRequestAndQueue (through on_read) continiously
	// and LogStat and SendResponses would be hampered, resulting too many responses pending to be sent and no stat update. By calling timet we try to address that situation.
	DoPeriodicActivities(); // Actually calls ConnectionsManager::SendResponses() which calls SendLocalClientsResponses() and SendPeerServersResponses()

	return pRequest;
}

// Checks if m_RequestProcessors map has processor exists for version and thread index
RequestProcessor* LocalClientsManager::GetRequestProcessor(unsigned short Version, int ThreadIndex)
{
	ASSERT (ThreadIndex < RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads);
	RequestProcessor* pRequestProcessor = NULL;

	if (m_RequestProcessors[ThreadIndex].find(Version) != m_RequestProcessors[ThreadIndex].end())
	{
		pRequestProcessor = m_RequestProcessors[ThreadIndex][Version];
	}

	return pRequestProcessor;
}

// Called by request processor threads. m_ThreadIndex is thread local variable and is assigned value when thread is running first time.
int LocalClientsManager::GetCurrentThreadIndex()
{
	return m_ThreadIndex;
}

void LocalClientsManager::request_processing_thread(uv_work_t* work_t)
{
	Request* pRequest = (Request*)work_t->data;
	ASSERT (pRequest != NULL);
	stClient* pClient = pRequest->GetClient();
	LocalClientsManager* pLocalClientsManager = pClient->m_pLocalClientsManager;
	ASSERT (pClient->m_Version != UNINITIALIZED_VERSION); // At this stage there is NO chance that version was not yet initialized

	uv_rwlock_wrlock(&pLocalClientsManager->m_rwlRequestCountersLock1);
	uv_rwlock_wrlock(&pLocalClientsManager->m_rwlRequestCountersLock2);
	pLocalClientsManager->m_stServerStat.RequestProcessingThreadsStarted++;
	uv_rwlock_wrunlock(&pLocalClientsManager->m_rwlRequestCountersLock2);
	uv_rwlock_wrunlock(&pLocalClientsManager->m_rwlRequestCountersLock1);

	// Let's try to get thread index (if already not) either here or in disconnection processing thread
	if (m_ThreadIndex == -1)
	{
		uv_rwlock_wrlock(&pLocalClientsManager->m_rwlThreadIndexCounterLock);
		m_ThreadIndex = pLocalClientsManager->ThreadIndexCounter++;
		uv_rwlock_wrunlock(&pLocalClientsManager->m_rwlThreadIndexCounterLock);

		ASSERT (m_ThreadIndex < RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads);
	}

	// Get request processor associated with this thread
	// USHORT Version = (pClient->m_Version != FORWARDED_RESPONSE_INDICATOR) ? pClient->m_Version : 0 ;
	BOOL bRequestProcessed = FALSE;
	
	// RequestProcessor* pRequestProcessor = pLocalClientsManager->m_RequestProcessors[m_ThreadIndex][Version];
	// Get request processor associated with this thread
	RequestProcessor* pRequestProcessor = pClient->m_pLocalClientsManager->GetRequestProcessor(pClient->m_Version, m_ThreadIndex);

	if (pRequestProcessor)
	{
		ASSERT (pRequestProcessor->m_bRequestIsBeingProcessed == FALSE);
		pRequestProcessor->m_bRequestIsBeingProcessed = TRUE;

		ASSERT (pClient->m_bDeleted == false); // When object is not valid referencing bDeleted _itself_ could become invalid and causes assertion (right here in this line)
		ASSERT (pRequestProcessor); // There is no reason for request processor to be NULL here. We initialize all processors at start only after we receive first connection.

		// Before processing request, lets store async handle and barrier
		pRequestProcessor->SetRequest(pRequest);

		// Process only if shutdown was not initiated (This helps speeding up shuting down when there are loads of pending requests)
		if (pLocalClientsManager->m_pClientsPool->IsShutdownInitiated() != TRUE)
		{
			try
			{
				bRequestProcessed = pRequestProcessor->ProcessRequest();
			}
			catch(std::bad_alloc&)
			{
				// If application doesn't catch memory exception, framework will do it and set this flag so that after_request_processing_thread 
				// can disconnect the client sending this request (for the reason mentioned in after_request_processing_thread)
				pRequest->SetMemoryAllocationExceptionFlag();  
			}
		}

		pRequestProcessor->SetRequest(NULL);
		pRequestProcessor->m_bRequestIsBeingProcessed = FALSE ;
	}
	else
	{
		LOG (ERROR, "Cannot process request for version 0x%X as processor for the version is not available.", pClient->m_Version);
	}

	uv_rwlock_wrlock(&pLocalClientsManager->m_rwlRequestCountersLock1);
	uv_rwlock_wrlock(&pLocalClientsManager->m_rwlRequestCountersLock2);
	
	if (pRequest->IsDeferred() == FALSE)
	{
		ULONG RequestLen = pRequest->GetRequest().len;
		double RequestProcessingTime = ConnectionsManager::GetHighPrecesionTime() - pRequest->GetArrivalTime();
		// RequestProcessingTime = RequestProcessingTime/(double)RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads;

		if (bRequestProcessed==FALSE)
			pLocalClientsManager->m_stServerStat.RequestsFailedToProcess++;

		pLocalClientsManager->m_stServerStat.RequestsProcesed ++;
		pLocalClientsManager->m_stServerStat.TotalRequestProcessingTime += RequestProcessingTime;
		pLocalClientsManager->m_stServerStat.TotalRequestBytesProcessed += RequestLen; // Total request size till now
		pLocalClientsManager->m_stServerStat.MemoryConsumptionByRequestsInQueue -= RequestLen;
		pLocalClientsManager->m_stServerStat.RequestsProcessedPerThread[m_ThreadIndex] ++;
	}

	pLocalClientsManager->m_stServerStat.RequestProcessingThreadsFinished++ ;

	uv_rwlock_wrunlock(&pLocalClientsManager->m_rwlRequestCountersLock2);
	uv_rwlock_wrunlock(&pLocalClientsManager->m_rwlRequestCountersLock1);
}

/*
This function is called by event loop after 'request_processing_thread' finishes its execution
*/
void LocalClientsManager::after_request_processing_thread(uv_work_t* work_t, int status)
{
	ADD2PROFILER;

	if (status < 0)
	{
		LOG (ERROR, "Error occurred in request processing thread. Error code %d (%s)", status, uv_strerror(status));
		return;
	}

	ASSERT (work_t);

	Request* pRequest = (Request*) work_t->data;
	
	ASSERT(pRequest);
	
	stClient* pClient = pRequest->GetClient(); 
	
	ASSERT (pClient);

	LocalClientsManager* pLocalClientsManager = pClient->m_pLocalClientsManager;

	ASSERT (pLocalClientsManager);

	// If we've got memory allocation error while processing request, we MUST delete the connection sending the request.
	// This is because if we overlook memory allocation errors in request/responses creations, we may endup having lots of connections only and no space to for requests responses.
	// This could be an epic deadlock scene in server framework. At the same time let's exclude peer servers from this frequent allocation/deallocations.
	if (pRequest->GetMemoryAllocationExceptionFlag() && (pClient->m_Version != SPECIAL_COMMUNICATION))
		pClient->MarkToDisconnect(TRUE);

	
	if (pRequest->IsDeferred() == FALSE)
	{
		// Important: Before we turn flag pClient->m_bRequestIsBeingProcessed to FALSE we MUST reset request buffer
		// Also, to avoid data race (resulting in garbage value of SizeAvailable in GetRequestBuffer) we must not call this from request_processing_thread
		pLocalClientsManager->ResetRequestBuffer(pClient); 
		pClient->m_bRequestIsBeingProcessed = FALSE;

		pClient->m_bRequestProcessingFinished = TRUE;

		uv_rwlock_wrlock(&pLocalClientsManager->m_rwlRequestCountersLock2);
		pLocalClientsManager->m_stServerStat.MemoryConsumptionByRequestsInQueue -= (sizeof(Request));
		uv_rwlock_wrunlock(&pLocalClientsManager->m_rwlRequestCountersLock2);

		// We MUST NOT delete Request object in request_processing_thread because it holds work_t of uv_queue_work
		DEL(pRequest);

		// We should call DecreaseRequestResponseCount here itself and not through request processing thread.
		// Because, if stClient was marked for deletion and if its counter in pool made zero from request processing thread,
		// it could get deleted (through DisconnectAndDelete) from somewhere else in event loop, before it comes here.
		// If that happens, call to DisconnectAndDelete at this place results in crash (as the stClient pointer is not valid anymore)
		pLocalClientsManager->m_pClientsPool->DecreaseCountForClient(pClient, REQUESTCOUNT); 

		// See if stClient was marked for deletion. If yes, call DisconnectAndDelete.
		if (pClient->IsMarkedToDisconnect() == TRUE)
		{
			if (pLocalClientsManager->DisconnectAndDelete(pClient) == TRUE) // DisconnectAndDelete tries to remove stClient from CL pool. If successful, proceeds disconnect and deleting stClient object
			{
				LOG (NOTE, "Client is being disconnected through after_request_processing_thread (bIsByServer TRUE)");
			}
		}
	}
	else
	{
		// LOG (NOTE, "Request processing has been deferred. Request being requeued.");
		pRequest->DeferProcessing(FALSE);
		int RetVal = uv_queue_work (pLocalClientsManager->loop, &pRequest->m_work_t, request_processing_thread, after_request_processing_thread);
		ASSERT (RetVal == 0); // uv_queue_work returns non-zero only when request_processing_thread is NULL
	}

	pLocalClientsManager->DoPeriodicActivities();
}

// This function runs in threads. Called by ConnectionsManager::AddResponseToQueues.
int LocalClientsManager::AddResponseToClientsQueues(Response* pResponse, ClientHandlesPtrs* pClientHandlePtrs, BOOL& bHasEncounteredMemoryAllocationException)
{
	int ResponseReferenceCount = 0;
	int NumberOfClients = (int) pClientHandlePtrs->size(); 

	// for (int ClientIndex=0; ClientIndex<NumberOfClients; ClientIndex++)
	for (ClientHandlesPtrs::iterator it=pClientHandlePtrs->begin(); it!=pClientHandlePtrs->end(); ++it)
	{
		stClient* pClient = NULL; 

		// If no pending requests for client, and just after increasing response counter, when event loop cannot delete client (after marking for deletion) and if AddResponseToQueue fails,
		// the client will not now have any request/response against it. But it is now marked for deletion and so pool will not thereafter add any request/responses to it. 
		// Thus neither DisconnectAndDelete will be called for it nor any request will be added, and it will remain dangling forever.
		// To avoid this situation we must make DisconnectAndDelete to wait till response for client being added.
		uv_rwlock_rdlock(&m_rwlWaitTillResponseForClientIsBeingAdded);
		if (m_pClientsPool->IncreaseCountForClient(*it, pClient, RESPONSECOUNT) == TRUE) 
		{
			if (AddResponseToQueue(pResponse, pClient, bHasEncounteredMemoryAllocationException) == TRUE)	// Returns TRUE only when response was added to queue and client added to set (if aplicable)
			{
				ResponseReferenceCount++;
			}
			else
			{
				m_pClientsPool->DecreaseCountForClient(pClient, RESPONSECOUNT);
			}
		}
		uv_rwlock_rdunlock(&m_rwlWaitTillResponseForClientIsBeingAdded);
	}

	return ResponseReferenceCount;
}

// Called by request processing threads through AddResponseToQueues (protected by m_rwlResponsesQueueLock)
BOOL LocalClientsManager::AddResponseToQueue(Response* pResponse, stClient* pClient, BOOL& bHasEncounteredMemoryAllocationException)
{
	BOOL bAdded = FALSE;
	std::deque<Response*>* pResponsesQueue;
	std::set<stClient*>* pClientsSet;

	if (m_bResponseDirectionFlag)  // This flag is already protected by read lock by caller ConnectionsManager::AddResponseToQueues
	{
		pResponsesQueue = &pClient->m_ResponsesQueue1;
		pClientsSet = &m_RecevingClientsSet1;
	}
	else
	{
		pResponsesQueue = &pClient->m_ResponsesQueue2;
		pClientsSet = &m_RecevingClientsSet2;
	}

	uv_rwlock_wrlock(&pClient->m_rwlResponsesQueueLock);
	try
	{
		unsigned int ResponseQueueSize = (UINT)pResponsesQueue->size();
		if (ResponseQueueSize < (UINT)(RequestProcessor::GetCommonParameters().MaxPendingResponses/2)) // Half of limit is available (In one of two queues)
		{
			pResponsesQueue->push_front(pResponse);

			if (AddToClientSet (pClientsSet, pClient, TRUE) == FALSE)
			{
				pResponsesQueue->pop_front();
			}
			else
			{
				bAdded = TRUE;
			}

			pClient->m_bResponseQueueFull = FALSE;
		}
		else
		{
			if (pClient->m_bResponseQueueFull == FALSE) // To reduce overlogging which could result in holding locks in logging
				LOG (ERROR, "Response queue for a client is full. Cannot add response.");
			pClient->m_bResponseQueueFull = TRUE;
		}
	}
	catch(std::bad_alloc&) // Exception will occur only when pResponsesQueue->push
	{
		bHasEncounteredMemoryAllocationException = TRUE;
		IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
	}
	uv_rwlock_wrunlock(&pClient->m_rwlResponsesQueueLock);

	return bAdded;
}

// Called from thread (AddResponseToQueue) as well as from event loop (after_send_response)
BOOL LocalClientsManager::AddToClientSet(std::set<stClient*>* pClientsSet, stClient* pClient, BOOL bTobeLocked)
{
	BOOL RetVal = TRUE;

	// Since this function gets called from threads, checking in client set if client already exists, can happen concurrent. Hence could increase performance. 
	if (bTobeLocked) uv_rwlock_rdlock(&m_rwlClientSetLock);
	const bool bIsClientInSet = pClientsSet->find(pClient) != pClientsSet->end();
	if (bTobeLocked) uv_rwlock_rdunlock(&m_rwlClientSetLock);

	if (bIsClientInSet)
		return TRUE;

	if (bTobeLocked) uv_rwlock_wrlock(&m_rwlClientSetLock);
	try
	{
		pClientsSet->insert(pClient);
	}
	catch(std::bad_alloc&)
	{
		LOG (NOTE, "Memory error adding client to receivers set. Some responses might remain in queue forever."); 
		IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
		RetVal = FALSE;
	}
	if (bTobeLocked) uv_rwlock_wrunlock(&m_rwlClientSetLock);

	return RetVal;
}

void LocalClientsManager::getnameinfo_cb(uv_getnameinfo_t* req, int status, const char* hostname, const char* service)
{
	// printf("\n\n******* hostname found %s", hostname);
	LocalClientsManager* lcm = (LocalClientsManager*)req->data;
	lcm->m_HostName = hostname;
}

std::string LocalClientsManager::GetHostName()
{
	return m_HostName;
}

// Instantiated only once through event loop
int LocalClientsManager::StartListening(char* IPAddress, unsigned short int IPv4Port)
{
	int RetVal = InitiateRequestProcessorsAndValidateParameters();
	ASSERT_RETURN (RetVal);

    uv_tcp_init(loop, (uv_tcp_t *)&m_tcp_server);

#if 1
	if (strcmp (IPAddress, "0.0.0.0") == 0)
	{
		// You have passed 0.0.0.0 as listening address. In production this will generate INCORRECT client handles.
		RetVal = UV__EINVAL ; // UV__EADDRNOTAVAIL;
		ASSERT_RETURN  (RetVal);
	}
#endif

	static struct sockaddr_in bind_addr ;
//	RetVal = uv_ip4_addr(IPAddress, IPv4Port, &bind_addr);
//	RetVal = uv_ip4_addr("192.168.1.100", IPv4Port, &bind_addr);
//	RetVal = uv_ip4_addr("127.0.0.1", IPv4Port, &bind_addr); // Binding 127.0.0.1 is the localhost address on the loopback interface; only local processes can communicate with each other using that address.
    RetVal = uv_ip4_addr("0.0.0.0", IPv4Port, &bind_addr); // Binding to 0.0.0.0 typically indicates that the process is listening on all configured IPv4 addresses on all interfaces.
	ASSERT_RETURN (RetVal);

    RetVal = uv_tcp_bind((uv_tcp_t *)&m_tcp_server, (const struct sockaddr*)&bind_addr, 0);
	ASSERT_RETURN (RetVal);

	// uv_getnameinfo_t 

	RetVal = uv_getnameinfo (loop, &m_nameinfo_t, getnameinfo_cb, (const sockaddr*)&bind_addr, NI_NAMEREQD); 
	ASSERT_RETURN (RetVal);

	// printf("\nSetting internal TCP buffer size to zero");
	// SetInternalTCPBufferSizes(server.socket, NULL);
	static char strIPAddressAndPort[128];
	sprintf_s(strIPAddressAndPort, 128, "%s:%hu", IPAddress, IPv4Port); // %hu: unsigned short int

	// By this point IP address was already validated. (Had it not been valid, bind would have failed before we come here.)

	// First let's extract out port which is separated by colon
	char* strColon = strchr(strIPAddressAndPort, ':');
		
	if (!strColon) // We MUST have port separated by colon here
		return UV_EINVAL;

	m_ServerIPv4Address.SetAddress(strIPAddressAndPort);

	m_tcp_server.data = this;

	RetVal = uv_listen((uv_stream_t*)&m_tcp_server, 256, on_new_client);
    
	if (RetVal != 0) 
	{
		uv_close((uv_handle_t*) &m_tcp_server, NULL);
		ASSERT_RETURN (RetVal);
    }

	return 0;
}

void LocalClientsManager::on_new_client(uv_stream_t* server, int status) 
{
	LocalClientsManager* pLocalClientsManager = (LocalClientsManager*) server->data ;

    if (status < 0) 
	{
		LOG (ERROR, "Error in on_new_connection. Error code %d (%s)", status, uv_strerror(status));
		// As per comment in uv_process_tcp_accept_req we shouldn't accept any further connection when we get error in connection callback
		// In fact we shouldn't continue running server once we get here as per this advice:
		// https://groups.google.com/forum/#!topic/libuv/XlHxTWkHMXc
		//

		printf ("\n\n***** Fatal error occurred while accepting connection. Error %d (%s) *****\n\n", status, status);
		LOG (EXCEPTION, "Fatal error occurred while accepting connection. Error %d (%s)", status, status);
		return;
    }

	stClient * pClient ;

	try
	{
		if (pLocalClientsManager->m_ConnectionCallbackError < 0)
		{
			LOG (EXCEPTION, "Fatal error occurred while accepting connection. Error %d (%s) This server cannot accept further connections.", pLocalClientsManager->m_ConnectionCallbackError, uv_strerror(pLocalClientsManager->m_ConnectionCallbackError));
			throw ClientCreationException();
		}

		pClient = new stClient (&pLocalClientsManager->m_tcp_server, pLocalClientsManager->m_stServerStat, pLocalClientsManager->m_ServerIPv4Address);
	}
	catch(std::bad_alloc&) // stClient has STL queue which could throw bad alloc
	{
		/*
		// Closing connection which was never accepted, coz of very low memory, needs overhead of defining separate close handle etc. 
		// Hence we abandoned this plan. Such connections are not going to be harmful anyways as they have not consumed memory.
		// So, whenever they get disconnected there won't be any memory leak etc.

		uv_tcp_t* client;

		client = new uv_tcp_t;

		uv_tcp_init(loop, &client);

		if (uv_accept(m_server, (uv_stream_t*) &client) == 0) 
		{
			uv_close((uv_handle_t*)&client, on_client_closed);
		}
		*/

		pLocalClientsManager->IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
		return ;
	}
	catch(ClientCreationException&)
	{
		pLocalClientsManager->IncreaseExceptionCount(CLIENT_CREATION_EXCEPTION, __FILE__, __LINE__);
		return;
	}

	pLocalClientsManager->m_stServerStat.MemoryConsumptionByClients += (sizeof(stClient) + pClient->m_SizeReservedForResponsesBeingSend);

	if (pLocalClientsManager->AcceptConnection(pClient) == FALSE) // Calls uv_accept (to initialize m_client) and if uv_accept successfull, calls uv_read_start (starts reading)
											  // returns FALSE if any of these calls fails. Else returns TRUE.
	{
		if (pClient->m_bIsAccepted == TRUE)
			pLocalClientsManager->DisconnectAndDelete(pClient); // stClient was not added to the pool but uv_accept was successfull. We can call DisconnectAndDelete which calls uv_close.
		else
			DEL(pClient); // It was neither accepted (so nor read started). Just delete.

		LOG (ERROR, "Error in on_new_connection: Socket accept error");

		return;
	}

	// At this stage m_bIsReadStarted and m_bIsAccepted both are TRUE

	// Store this client to ClientPool
	BOOL RetVal = pLocalClientsManager->m_pClientsPool->AddClient(pClient); // This will fail either because of low memory or because of server shutting down

	if (RetVal == FALSE)
	{
		pLocalClientsManager->DisconnectAndDelete(pClient);
		// We do not need to do assertion failure here
		LOG (ERROR, "Error adding client to pool (Either server is shutting down or Not enough memory to add)");
		return;
	}
	else
	{
		pClient->m_bIsAddedToPool = TRUE;
	}
}

void LocalClientsManager::on_client_closed(uv_handle_t* client) 
{
	ADD2PROFILER;

	// Handle is closed. Now delete stClient object.
	stClient * pClient = (stClient*) client->data ;
	LocalClientsManager* pLocalClientsManager = pClient->m_pLocalClientsManager;
	pLocalClientsManager->m_stServerStat.MemoryConsumptionByClients -= (sizeof(stClient)+pClient->m_SizeReservedForResponsesBeingSend);

	if (pClient->m_Request.base != pClient->m_Header)
	{
		DEL_ARRAY (pClient->m_Request.base);
		VersionParameters* pVP = pLocalClientsManager->GetVersionParameters(pClient->m_Version);
		pLocalClientsManager->m_stServerStat.MemoryConsumptionByClients -=  (pClient->m_bRequestMemoryAllocatedForStreaming ? (pVP->m_MaxRequestSize+HEADER_SIZE) : pClient->m_Request.len) ;
		pLocalClientsManager->m_stServerStat.ActiveClientRequestBuffers -- ;
	}

	DEL (pClient->m_pResponsesBeingSent);
	DEL_ARRAY (pClient->m_pResponsesBuffersBeingSent);

	// Destroy lock for disconnection flag
	uv_rwlock_destroy(&pClient->m_rwlLockForDisconnectionFlag);
	uv_rwlock_destroy(&pClient->m_rwlResponsesQueueLock);

	// LOG (INFO, "Deleting client object. stClient #%d", pClient->ClientRegistrationNumber);
	DEL(pClient);
	pLocalClientsManager->m_ClientsClosing --;

	// printf("\nClients closing %d", m_ClientsClosing);
	return;
}

int LocalClientsManager::AcceptConnection(stClient* pClient)
{
	uv_tcp_init(loop, &pClient->m_client);

	if (uv_accept((uv_stream_t*)&m_tcp_server, (uv_stream_t*) &pClient->m_client) == 0) 
	{
		pClient->m_bIsAccepted = TRUE;
		// By default Nagle's algorithm is used, if you want to disable it you need to call uv_tcp_nodelay(handle, 1). 
		// To go back to using Nagle, call the function with a 0.

		uv_tcp_nodelay(&pClient->m_client, 1); // Disable Nagle

		// Before we start reading, lets set internal buffer size to zero (so it will directly write from our allocated buffers).
		// This is to avoid internal buffer overflows.
		DWORD NewBuffSize = 0L;
		DWORD NewBuffSizeLen = sizeof(DWORD);

		if (setsockopt(pClient->m_client.socket, SOL_SOCKET, SO_SNDBUF, (const char *)&NewBuffSize, NewBuffSizeLen) < 0) 
		{
			LOG(ERROR, "Error setting new send buffer size");
			return FALSE;
		}

		if (setsockopt(pClient->m_client.socket, SOL_SOCKET, SO_RCVBUF, (const char *)&NewBuffSize, NewBuffSizeLen) < 0) 
		{
			LOG(ERROR, "Error setting new receive buffer size");
			return FALSE;
		}

		int RetVal = uv_read_start((uv_stream_t*) &pClient->m_client, alloc_buffer, on_read);

		if (RetVal == -1)
		{
			LOG (ERROR, "Error reading from new connection");
			return FALSE;
		}
		else
		{
			pClient->m_bIsReadStarted = TRUE;
		}
	}
	else 
	{
		LOG (ERROR, "Error accepting new connection");
		return FALSE;
	}

	return TRUE;
}

void LocalClientsManager::DeleteRequestProcessors()
{
	// Deleting request processors
	LOG (INFO, "Deleting request processors");
	unsigned short MaxVersionNumber= 0xFFFF;

	for (int i=0; i<RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads; i++)
	{
		/*
		for (int j=0; j<=MaxVersionNumber; j++) // m_MaxVersionNumber valid value always >= 1 and at zero index we have forward request processor (version: FORWARDED_RESPONSE_INDICATOR)
		{
			RequestProcessor* pRequestProcessor = GetRequestProcessor(j, i);
			if (pRequestProcessor)
				pRequestProcessor->DeleteProcessor();  
		}
		*/

		// Enumerate all processors against each thread and delete them
		m_RequestProcessors[i];
		typedef std::map<USHORT, RequestProcessor*>::iterator it_type;
		it_type iterator_end =  m_RequestProcessors[i].end();
		for(it_type iterator = m_RequestProcessors[i].begin(); iterator != iterator_end; iterator++) 
		{
			USHORT version = iterator->first;
			RequestProcessor* pRequestProcessor = iterator->second;
			ASSERT(pRequestProcessor);
			pRequestProcessor->DeleteProcessor();  
		}

		m_RequestProcessors[i].clear();   
	}

	// Finally delete the one we used to send keep alive signals
	m_pReqProcessorToSendKepAlive->DeleteProcessor();
}

// Called through DoPeriodicActivities
void LocalClientsManager::SendKeepAlive()
{
	if (m_keep_alive_work_t.data != NULL)
	{
		LOG (NOTE, "Couldn't run keep alive. Last one was still in progress."); 
		return;
	}

	m_keep_alive_work_t.data = this ;
	int RetVal = uv_queue_work (loop, &m_keep_alive_work_t, send_keepalive_thread, after_send_keepalive_thread);
	ASSERT (RetVal == 0); // uv_queue_work returns non-zero only when disconnection_processing_thread is NULL
}

void LocalClientsManager::send_keepalive_thread(uv_work_t* work_t)
{
	ConnectionsManager* pConnectionsManager = (ConnectionsManager*)work_t->data;
	LocalClientsManager* pLocalClientsManager = (LocalClientsManager*)work_t->data;

	ASSERT (pConnectionsManager);

	try
	{
		for (ClientType Type = VersionedClient; Type <= VersionlessClient; Type=(ClientType(1+(int)Type)))
		{
			ClientHandles clienthandles;
			pLocalClientsManager->m_pClientsPool->GetIdleClients(clienthandles, Type);

			unsigned int RecepientsCount = (UINT)clienthandles.size(); 

			if (RecepientsCount)
			{
				uv_buf_t response;
				unsigned char response_code = (Type == VersionedClient) ? RESPONSE_KEEP_ALIVE : RESPONSE_FATAL_ERROR /* To be used to disconnect versionless idle connections */;

				response.base = (char *)&response_code;
				response.len = 1;

				pLocalClientsManager->m_pReqProcessorToSendKepAlive->SendResponse (&clienthandles, &response);

				// for (unsigned int i=0; i<RecepientsCount; i++) // Let's avoid log in iteration as it could hold write lock which can hamper event loop performance.
				if (response_code == RESPONSE_KEEP_ALIVE) 
					LOG (INFO,  "Queuing keep alive for some client(s)");
				else
					LOG (NOTE, "Versionless client(s) idle for too long. Disconnecting clients.");
			}
		}
	}
	catch(std::bad_alloc&)
	{
		pConnectionsManager->IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
	}
}

void LocalClientsManager::after_send_keepalive_thread(uv_work_t* m_keep_alive_work_t, int status)
{
	LOG (NOTE, "Done with keep alive thread");
	m_keep_alive_work_t->data = NULL ;
}

unsigned int LocalClientsManager::GetClientsConnectedCount()
{
	return m_pClientsPool->GetClientsCount();
}

BOOL LocalClientsManager::IsServerStopped ()
{
	return m_bServerStopped;
}

BOOL LocalClientsManager::HasAllClientsDisconnectedForShutdown()
{
	return m_bAllClientsDisconnectedForShutdown;
}

BOOL LocalClientsManager::AreClientsClosing()
{
	return m_ClientsClosing;
}

int LocalClientsManager::GetActiveProcessors()
{
	return RequestProcessor::m_NumberOfActiveProcessors;
}

// Called by event loop (after request processing is done)
// There are these cases involved in here:
// For each 1. Local response and 2. To Be Forwarded response
// 1. uv_write returns negative error code
// 2. uv_write returns 0 but fails afterwards (calls callback with negative error code)
// 3. uv_write returns 0 and succeeds
void LocalClientsManager::SendLocalClientsResponses() 
{
	ADD2PROFILER;

	/* Get client set and check size */
	std::set<stClient*>* pClientsSet;

	if (m_bResponseDirectionFlag)
	{
		pClientsSet = &m_RecevingClientsSet2;
	}
	else
	{
		pClientsSet = &m_RecevingClientsSet1;
	}

	// While threads are adding clients to set1, SendLocalClientsResponses will busy sending responses from set2 (and vice versa) so we don't need lock here. 
	// uv_rwlock_rdlock(&m_rwlClientSetLock); 
	int clients_count = (int)pClientsSet->size(); 
	// uv_rwlock_rdunlock(&m_rwlClientSetLock);

	if (clients_count == 0)
		return;

	/* Send responses for local Clients */
	std::set<stClient*>::iterator itClientsSet = pClientsSet->begin();
	std::set<stClient*>::iterator itClientsSetEnd = pClientsSet->end();

	while (itClientsSet != itClientsSetEnd)
	{
		std::set<stClient*>::iterator current = itClientsSet++;

		stClient* pClient = *current;

		ASSERT (pClient);

		if (pClient->m_pResponsesBeingSent->size()) // Response for this client was already queued (m_write_req.data (aka m_pResponseBeingSent))
		{
			pClientsSet->erase(current); // By using direction flag, we are erasing from different set than that is being added to by threads. So no need of m_rwlClientSetLock here.
			continue;
		}

		for (int i=0; (i<RequestProcessor::GetCommonParameters().MaxPendingResponses) && (pClient->m_pResponsesBuffersBeingSent[i].base); i++)
		{
			pClient->m_pResponsesBuffersBeingSent[i].base = NULL;
			pClient->m_pResponsesBuffersBeingSent[i].len = 0;
		}

		std::deque<Response*>* pResponsesQueue = (m_bResponseDirectionFlag) ? (&pClient->m_ResponsesQueue2)	: (&pClient->m_ResponsesQueue1);

		// There must be responses in queue
		const int ResponseQueueSize = (int)pResponsesQueue->size();
		ASSERT (ResponseQueueSize > 0);

		// Make sure we haven't changed capacity of responses being sent. Because we don't want it to throw bad_alloc when we add elements to it.
		ASSERT (pClient->m_pResponsesBeingSent->capacity() >= RequestProcessor::GetCommonParameters().MaxPendingResponses);
		// Also make sure responses in queue aren't exceeding limit
		ASSERT (ResponseQueueSize <=  RequestProcessor::GetCommonParameters().MaxPendingResponses);

		for (int i=0; pResponsesQueue->size(); i++)
		{
			Response* pResponse = pResponsesQueue->back();

			ASSERT (pResponse); 
			ASSERT (pResponse->GetReferenceCount()); //  There must be references to responses
			ASSERT (pResponse->IsForward() == FALSE); // We MUST NOT receive here response being forwarded 

			if (pResponse->IsFatalErrorForLocallyConnectedClient()) // FATAL_ERROR signifies client to be disconnected
			{
				if (i==0)
				{
					LOG (ERROR, "Client disconnection requested. Marking client for disconnect (Version 0x%X)", pClient->GetVersion());
					pClient->MarkToDisconnect(TRUE);
				}
				else
				{
					if (pClient->IsMarkedToDisconnect() == FALSE)
						break;
				}
			}

			pResponse->QueuedTime = ConnectionsManager::GetHighPrecesionTime();

			pClient->m_pResponsesBeingSent->push_back(pResponse); // This won't throw std:bad_alloc as max response memory is already allocated in stClient c'tor
			pClient->m_pResponsesBuffersBeingSent[i] = pResponse->GetResponse(); 

			pResponsesQueue->pop_back();
		}

		// If one of the response at middle of queue has indicated "fatal error" we won't have it in "being sent" responses,
		// resulting in response queue still having some responses pending
		if (pResponsesQueue->size() == 0)
			std::deque<class Response*>().swap(*pResponsesQueue);

		int RetVal_uv_write = 0 ;
		const int NumberOfBuffers = (int)pClient->m_pResponsesBeingSent->size();

#ifndef NO_WRITE
		if ((RetVal_uv_write == 0) && (pClient->IsMarkedToDisconnect() == FALSE))
		{
			// pClient->m_pResponseBeingSent->QueuedTime = ConnectionsManager::GetHighPrecesionTime();
			RetVal_uv_write = uv_write(&pClient->m_write_req, (uv_stream_t*)&pClient->m_client, pClient->m_pResponsesBuffersBeingSent, NumberOfBuffers, ConnectionsManager::after_send_responses);
		}
		else
#endif
		{
			// We've to manually set RetVal_uv_write to UV__ECONNABORTED here. This will result in calling after_send_response with STATUS = UV__ECONNRESET
			RetVal_uv_write = UV_ECONNABORTED;
		}

		if (RetVal_uv_write >= 0)
		{
			m_stServerStat.ResponsesBeingSent += NumberOfBuffers;
		}
		else
		{
			after_send_response_called_by_send_response = TRUE;
			// In case when uv_write succeeds, libuv assigns handle to req.handle so tht we get it in callback
			// But in case of failure we've to assign it on our own
			pClient->m_write_req.handle = (uv_stream_t*) &pClient->m_client; 
			ConnectionsManager::after_send_responses(&pClient->m_write_req, RetVal_uv_write);
			after_send_response_called_by_send_response = FALSE;
		}

		// We come here when there is no response left in client's queue or response written was successful.
		// In either case we must remove client from set. It will be added back to the set in after_send_response or in AddResponseToQueue
		pClientsSet->erase(current); // By using direction flag, we are erasing from different set than that is being added to by threads. So no need of m_rwlClientSetLock here.
	}

	return ;
}

void LocalClientsManager::AfterSendingLocalClientsResponses(stClient* pClient, Response* pResponse, int status) 
{
	ADD2PROFILER;

	int ResponseLength = pResponse->GetResponse().len;

	ASSERT (pResponse->IsForward() == FALSE); // We must receive here only local clients responses

	switch (status)
	{
		case WRITE_OK: // LIBUV calls after_send_response with 0 as status when send is successful
		{
			LOG (DEBUG, "Response sent successfully");

			int ResponseType = pResponse->GetResponseType();

			// Before we delete the pResponse, to know statistics let's find out how many responses sent and of what type
			switch(ResponseType)
			{
				case RESPONSE_KEEP_ALIVE	: m_stServerStat.ResponsesKeepAlives ++; break;
				case RESPONSE_ERROR			: m_stServerStat.ResponsesErrors ++; break;
				case RESPONSE_ACKNOWLEDGEMENT_OF_FORWARDED_RESP	: m_stServerStat.ResponsesAcknowledgementsOfForwardedResponses ++; break;
				case RESPONSE_FATAL_ERROR	: m_stServerStat.ResponsesFatalErrors ++; break;
				case RESPONSE_ORDINARY		: m_stServerStat.ResponsesOrdinary ++; break;
				default						: ASSERT(0); break;
			}

			if (pResponse->IsForward()) 
				m_stServerStat.ResponsesForwarded ++;

			if (pResponse->IsMulticast())
				m_stServerStat.ResponsesMulticasts ++; 

			if (pResponse->IsUpdate())
				m_stServerStat.ResponsesUpdates ++;

			m_stServerStat.ResponsesSent ++; // Total responses sent for all clients
			m_stServerStat.TotalResponseBytesSent += ResponseLength;
			ASSERT(m_stServerStat.TotalResponseBytesSent > 0);
		}
		break;

		default:   // Some Error: Either uv_write returns negative values OR libuv encountered error after uv_write was zero (successful)
		{
			// According to this thread we MUST disconnect connection for which uv_write is not successful
			// https://groups.google.com/forum/#!topic/libuv/nJa3WeiVs2U
			m_stServerStat.ResponsesFailedToSend++; // Total responses (local) failed to send

#ifndef NO_WRITE
			if (pClient->IsMarkedToDisconnect() == FALSE) // To avoid overlogging
			{
				LOG (ERROR, "Unable to send response. Marking client for disconnect. Error code %d (%s) (Version 0x%X Is called by SendResponse %d)", status, uv_strerror(status), pClient->GetVersion(), pClient->m_pLocalClientsManager->after_send_response_called_by_send_response);
				pClient->MarkToDisconnect(TRUE); 
			}
#endif
		}
		break;
    }

	// It's appropriate to call DecreaseCountForClient here. Because if we call it in SendResponse it could enable stClient to be deleted. 
	// Before event loop calls after_send_response if client gets deleted (through DisconnectClient) from somewhere else, 
	// then the stClient pointer we receive in after_send_response won't be valid anymore.
	std::deque<class Response*> *pResponseQueueLocked, *pResponseQueueUnlocked;

	m_pClientsPool->DecreaseCountForClient(pClient, RESPONSECOUNT); 

	// aAlthough response was not deleted, we should make pClient->m_pResponseBeingSent (aka m_write_req.data) NULL so SendResponse next time can learn it was sent.
	// pClient->m_pResponseBeingSent = NULL;

	// Add pClient to client sets
	std::set <stClient*> *pClientsSetUnlocked, *pClientsSetLocked;

	pResponseQueueLocked = m_bResponseDirectionFlag ? &pClient->m_ResponsesQueue1 : &pClient->m_ResponsesQueue2;
	pResponseQueueUnlocked = m_bResponseDirectionFlag ? &pClient->m_ResponsesQueue2 : &pClient->m_ResponsesQueue1;
	pClientsSetLocked = m_bResponseDirectionFlag ? &m_RecevingClientsSet1 : &m_RecevingClientsSet2;
	pClientsSetUnlocked = m_bResponseDirectionFlag ? &m_RecevingClientsSet2 : &m_RecevingClientsSet1;

	int UnlockedQueueSize = (int)pResponseQueueUnlocked->size();

	if (UnlockedQueueSize != 0)
	{
		AddToClientSet (pClientsSetUnlocked, pClient, FALSE); //pClientsSetUnlocked->insert(pClient);
	}

	uv_rwlock_rdlock(&pClient->m_rwlResponsesQueueLock);
	int LockedQueueSize = (int)pResponseQueueLocked->size();
	if (LockedQueueSize != 0)
	{
		AddToClientSet (pClientsSetLocked, pClient, TRUE); // pClientsSetLocked->insert(pClient);
	}
	uv_rwlock_rdunlock(&pClient->m_rwlResponsesQueueLock);

	// If stClient was marked for deletion. If yes, call DisconnectAndDelete
	if (pClient->IsMarkedToDisconnect() == TRUE) // pClient is NULL for FORWARDED response
	{
		// stClient has been marked for deletion. And MarkToDelete has been called only from event loop.
		// Hence, client was already marked for deletion before we get values of UnlockedQueueSize & LockedQueueSize.
		// So there is no chance that the values could get incremented thereafter (as marked to delete client cannot be added responses to).
		int Responses = UnlockedQueueSize + LockedQueueSize;
		if ((Responses == 0)) // We are putting this additional check just for performance improvement. DisconnectAndDelete call is safe without this also.
		{
			if (DisconnectAndDelete(pClient)) // DisconnectAndDelete tries to remove stClient from CL pool. If successful, proceeds disconnect and deleting stClient object.
				LOG (NOTE, "Client is being disconnected through AfterSendingLocalClientsResponses (bIsByServer TRUE)");
		}
	}

	return;
}