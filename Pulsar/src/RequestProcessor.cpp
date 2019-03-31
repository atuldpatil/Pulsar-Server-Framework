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

int RequestProcessor::m_NumberOfActiveProcessors=0;
std::map <USHORT, RequestProcessor*> RequestProcessor::m_VersionAndProcessor;
CommonParameters RequestProcessor::m_CommonParameters;

// IMPORTANT NOTE: We MUST initialize request processor through EVENT LOOP ONLY
RequestProcessor::RequestProcessor(USHORT version, VersionParameters& versionparameters)
{
	// NOTE: COMES HERE ONLY WHILE INITIALIZING GLOBAL OBJECTS JUST AFTER WE RUN SERVER APPLICATION
	m_pRequest = NULL;
	m_bRequestIsBeingProcessed = FALSE; 
	m_bDisconnectionIsBeingProcessed = FALSE;
	m_AsyncHandle.data = NULL ;
	m_Barrier.reference_count = 0;
	m_Version = version;
	m_VersionParameters = versionparameters;

	if (version != SPECIAL_COMMUNICATION)
	{
		// If it's not forwarded message, request or response size must be less than MAX_POSSIBLE_REQUEST_RESPONSE_SIZE.
		// Because when computing max req/resp size for forwarded response we consider MAX_POSSIBLE_REQUEST_RESPONSE_SIZE as max possible req/resp size.
		ASSERT(versionparameters.m_MaxRequestSize < MAX_POSSIBLE_REQUEST_RESPONSE_SIZE);
		ASSERT(versionparameters.m_MaxResponseSize < MAX_POSSIBLE_REQUEST_RESPONSE_SIZE);
	}

	if (m_VersionAndProcessor.find(version) == m_VersionAndProcessor.end())
	{
		m_VersionAndProcessor[version] = this;
	}
	else
	{
		// We instantiate multiple processors for same version for different threads. Hence this assertion is not valid.
		// ASSERT_THROW(FALSE, "Multiple processors for same version not allowed");
	}
}


/* Important note on thread safety:
Request processor runs in threads. We must not call ANY libuv function from request processing functions.
libuv functions are not thread safe:
https://groups.google.com/forum/#!searchin/libuv/thread$20safe/libuv/KasVxZpSFNY/sATbItcznhoJ

If we have to call thread unsafe functions (e.g. srand(), rand() functions, STL containers are not thread safe) in request processor, 
we can use our own locks. We should initiate those locks inside request processor constructor to avoid concurrent execution of InitializeLock (i.e. uv_rwlock_init of libuv).
*/

int RequestProcessor::InitializeLock(Lock& lock)
{
	return uv_rwlock_init (&lock);
}

void RequestProcessor::AcquireReadLock(Lock& lock)
{
	return uv_rwlock_rdlock(&lock);
}

void RequestProcessor::AcquireWriteLock(Lock& lock)
{
	return uv_rwlock_wrlock(&lock);
}

void RequestProcessor::ReleaseReadLock(Lock& lock)
{
	return uv_rwlock_rdunlock(&lock);
}

void RequestProcessor::ReleaseWriteLock(Lock& lock)
{
	return uv_rwlock_wrunlock(&lock);
}

void RequestProcessor::DestroyLock(Lock& lock)
{
	return uv_rwlock_destroy(&lock);
}

VersionParameters& RequestProcessor::GetVersionParameters()
{
	return m_VersionParameters;
}

CommonParameters RequestProcessor::GetCommonParameters()
{
	return m_CommonParameters;
}

void RequestProcessor::SetCommonParameters(CommonParameters& commonparams)
{
	m_CommonParameters = commonparams;
}

// Called by LocalClientsManager::InitiateRequestProcessorsAndValidateParameters()
int RequestProcessor::Initialize (uv_loop_t* loop, ConnectionsManager* pConnMan, TimerFunction pTimerFunction, AddResponseToQueuesFunction pAddResponseToQueuesFunction)
{
	m_AsyncHandle.data = this ;

	int RetVal = uv_async_init(loop, &m_AsyncHandle, RequestProcessor::send_update_callback);
	ASSERT_RETURN(RetVal);

	RetVal = uv_barrier_init(&m_Barrier, 2); // While sending updates, barrier blocks processing thread till event loop send update
	ASSERT_RETURN(RetVal);

	RetVal = InitializeLock(m_rwlTotalResponseObjectsQueuedCounterLock);
	ASSERT_RETURN(RetVal);

	RetVal = InitializeLock(m_rwlTotalResponseObjectsSentCounterLock);
	ASSERT_RETURN(RetVal);

	m_pConnectionsManager = pConnMan;
	m_pTimerFunction = pTimerFunction;
	m_pAddResponseToQueuesFunction = pAddResponseToQueuesFunction;

	m_ResponseObjectsQueued = 0;
	m_TotalResponseObjectsQueued = 0;
	m_ResponseObjectsSent = 0;

	m_NumberOfActiveProcessors ++ ; // This counter is decreased in destructor and is used at the time of shutting down server to make sure all active processors are closed

	return RetVal;
}

void RequestProcessor::IncreaseResponseObjectsQueuedCounter()
{
	m_ResponseObjectsQueued++;
}

int RequestProcessor::GetTotalResponseObjectsQueued()
{
	AcquireReadLock (m_rwlTotalResponseObjectsQueuedCounterLock);
	int RetVal = m_TotalResponseObjectsQueued;
	ReleaseReadLock (m_rwlTotalResponseObjectsQueuedCounterLock);

	return RetVal ;
}

int RequestProcessor::IncreaseResponseObjectsSentCounter()
{
	AcquireWriteLock (m_rwlTotalResponseObjectsSentCounterLock);
	int RetVal = ++ m_ResponseObjectsSent;
	ReleaseWriteLock (m_rwlTotalResponseObjectsSentCounterLock);

	return RetVal;
}

int RequestProcessor::GetResponseObjectsSent()
{
	AcquireReadLock (m_rwlTotalResponseObjectsSentCounterLock);
	int RetVal = m_ResponseObjectsSent;
	ReleaseReadLock (m_rwlTotalResponseObjectsSentCounterLock);

	return RetVal ;
}

void RequestProcessor::WaitOnBarrier()
{
	uv_barrier_wait(&m_Barrier);
}

int RequestProcessor::GetCurrentThreadIndex()
{
	int CurrentThreadIndex = m_pConnectionsManager->GetCurrentThreadIndex();
	ASSERT (CurrentThreadIndex >= 0); // We must invoke this function only from request processing threads. If this function has been called from event loop (main thread) ThreadIndex is negative.
	return CurrentThreadIndex;
}

void RequestProcessor::on_async_handle_closed(uv_handle_t* handle)
{
	RequestProcessor* ReqProc = (RequestProcessor*)handle->data ;
	ReqProc->m_AsyncHandle.data = NULL; 
	DEL(ReqProc);
}

RequestProcessor::~RequestProcessor()
{
	// Ensure that destructor was invoked ONLY through on_async_handle_closed
	ASSERT(m_AsyncHandle.data == NULL);
}

void RequestProcessor::DeleteProcessor()
{
	uv_barrier_destroy(&m_Barrier);
	uv_close((uv_handle_t*)&m_AsyncHandle, RequestProcessor::on_async_handle_closed);
	DestroyLock(m_rwlTotalResponseObjectsQueuedCounterLock);
	DestroyLock(m_rwlTotalResponseObjectsSentCounterLock);
	m_NumberOfActiveProcessors -- ;
}

// Gets called before each request processing
void RequestProcessor::SetRequest (Request* pRequest)
{
	if (pRequest) 
		ASSERT(m_pRequest == NULL); // Request was already set? Make sure request processor not being overused
	m_pRequest = pRequest;
	m_ResponseCountPerThread = 0;
}

// Returns NumberOfReeferences to response if successful. FALSE if fails.
void RequestProcessor::CreateResponseAndAddToQueues(const Buffer* response, ClientHandlesPtrs& Clienthandle_ptrs, USHORT version /* Version of client who is creating/storing the Response */, BOOL bIsUpdate, double RequestArrivalTime)
{
	ClientHandlesPtrsIterator StartIt;
	ClientHandlesPtrsIterator EndIt;

	ASSERT (MAX_HANDLES_IN_FORWARDED_RESPONSE);

	size_t HandleCount = Clienthandle_ptrs.size(), SplitCount = 0;

	while(Clienthandle_ptrs.size())
	{
		StartIt = Clienthandle_ptrs.begin();
		
		if ((m_pConnectionsManager->GetIPAddressOfLocalServer() != (*StartIt)->m_ServerIPv4Address) /*Response is for clients connected to another server*/ \
			&& (Clienthandle_ptrs.size() > MAX_HANDLES_IN_FORWARDED_RESPONSE))
		{
			EndIt = StartIt;
			std::advance(EndIt, MAX_HANDLES_IN_FORWARDED_RESPONSE);
		}
		else
		{
			EndIt = Clienthandle_ptrs.end();
		}

		Response* pResponse = NULL;

		try
		{
			pResponse = new Response (response, StartIt, EndIt, version, bIsUpdate, this, RequestArrivalTime, m_pConnectionsManager);  
			SplitCount++;
		}
		catch(ResponseCreationException&)
		{
			m_pConnectionsManager->IncreaseExceptionCount(RESPONSE_CREATION_EXCEPTION, __FILE__, __LINE__);
			return;
		}
		catch(std::bad_alloc&)
		{
			if (m_pRequest) // The purpose of setting exception flag is to disconnect the client who has sent the request. If no request means no client was associated (e.g. keep alive processing)
				m_pRequest->SetMemoryAllocationExceptionFlag();
			m_pConnectionsManager->IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
			return;
		}

		//
		// AddResponseToQueues tries to add response to queues. If it fails, it deletes the response. 
		// Otherwise response will be deleted in after_send_response.
		//
		// We must note here that we don't need to pass StrtIt and EndIt to AddResponseToQueues. Because in case of forwarded response, 
		// range of handles already have been copied to response buffer and in case of local response we add reference to all clients. 
		// So range of handles is not needed in either case.
		//
		// WARNING: It is very likely that the moment we add pResponse to response queues, it was picked up by event loop, 
		// sent and deleted. Hence we shouldn't refer to pResponse hereafter in this function.
		//
		BOOL bHasEncounteredMemoryAllocationException = FALSE;
		(m_pConnectionsManager->*m_pAddResponseToQueuesFunction) (pResponse, &Clienthandle_ptrs, bHasEncounteredMemoryAllocationException); 

		if ((bHasEncounteredMemoryAllocationException) && (m_pRequest))
			m_pRequest->SetMemoryAllocationExceptionFlag();

		Clienthandle_ptrs.erase (StartIt, EndIt);
	}

	// LOG (INFO, "Response handles %d split %d times.", HandleCount, SplitCount); 

	return;
}

// This function is called by LocalClientsManager::InitiateRequestProcessorsAndValidateParameters() 
// for RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads * MaxVersionNumber(0xFFFF) times.
RequestProcessor* RequestProcessor::GetNewRequestProcessor(USHORT version)
{
	RequestProcessor* pRequestProcessor = NULL ;

	try
	{
		switch (version)
		{
			// NULL version is reserved by Pulsar framework and they don't have request processors
			case UNINITIALIZED_VERSION:
				return NULL;

			default:
				if (m_VersionAndProcessor.find(version) != m_VersionAndProcessor.end())
				{
					return m_VersionAndProcessor[version]->GetAnotherInstance();
				}
		}
	}
	catch(std::bad_alloc&)
	{
		LOG (ERROR, "Exception bad_alloc while creating new request processor for version 0x%X", version); 
	}

	return pRequestProcessor;
}

const Buffer RequestProcessor::GetRequest() 
{ 
	// As long as we are processing the request, the request object m_pRequest must remain non-null
	// Having it as NULL is serious flaw so we must quit (Rather than returning NULL and continue with the flow)
	ASSERT(m_pRequest!=NULL); 
	return m_pRequest->GetRequest(); 
}

void RequestProcessor::DeferRequestProcessing()
{
	ASSERT(m_pRequest!=NULL); 
	return m_pRequest->DeferProcessing(TRUE);
}

ClientHandle RequestProcessor::GetRequestSendingClientsHandle() 
{ 
	ASSERT(m_pRequest!=NULL); 
	return m_pRequest->GetClient()->GetClientHandle(); 
}

void RequestProcessor::SetStreamingMode (bool bMode)
{
	ASSERT(m_pRequest!=NULL); 
	m_pRequest->GetClient()->SetStreamingMode(bMode); 
}

USHORT RequestProcessor::GetClientProtocolVersion()
{
	ASSERT(m_pRequest!=NULL); 
	return m_Version;
}

void RequestProcessor::SetSessionData (void* pData)
{
	ASSERT(m_pRequest!=NULL); 
	m_pRequest->GetClient()->SetSessionData(pData); 
}

void* RequestProcessor::GetSessionData ()
{
	ASSERT(m_pRequest!=NULL); 
	return m_pRequest ? m_pRequest->GetClient()->GetSessionData() : NULL ; 
}

void RequestProcessor::DisconnectClient(ClientHandle* clienthandle) 
{ 
	ASSERT(m_pRequest!=NULL); 
	char ErrorAndErrorCode;
	uv_buf_t response ;

	ErrorAndErrorCode = RESPONSE_FATAL_ERROR;

	response.base = &ErrorAndErrorCode;
	response.len = 1 ;
	LOG (NOTE, "Application requested client disconnection");
	SendResponse (clienthandle, &response, SPECIAL_COMMUNICATION);
}

int RequestProcessor::GetMaxResponseSizeOfAllVersions()
{
	return m_pConnectionsManager->GetMaxResponseSizeOfAllVersions();
}

std::string RequestProcessor::GetHostName()
{
	return m_pConnectionsManager->GetHostName();
}

unsigned int RequestProcessor::GetServerIPv4Address()
{
	return m_pConnectionsManager->GetIPAddressOfLocalServer();
}

/* All the functions below are to write to client. 
These functions essentially calls StoreMessage ultimately. StoreMessage actually constructs response object and adds it to responses list.
It doesn't wait till it writes to the socket. This is by design by keeping performance as main concern. 
Hence we cannot really return if write fails for the response. Regardless, there is no guarantee that successful socket write (uv_write) means
successful response delivery to client (Ref: https://groups.google.com/forum/#!topic/libuv/hbvMnWOnDV4) Hence, we cannot rely on uv_write return value. 
Requests processors have to have employ their own mechanism to chk if client was connected (e.g. thru disconnection handler) and to ensure response
delivery is successful (e.g. ack from client)
*/

void RequestProcessor::StoreError (ClientHandle* clienthandle, UCHAR ErrorCode)
{
	char ErrorAndErrorCode[2];
	uv_buf_t response ;

	ErrorAndErrorCode[0] = RESPONSE_ERROR;
	ErrorAndErrorCode[1] = ErrorCode;

	response.base = ErrorAndErrorCode;
	response.len = 2 ;

	return SendResponse (clienthandle, &response, SPECIAL_COMMUNICATION);
}

void RequestProcessor::SendResponse (ClientHandle* clienthandle, const Buffer* response, USHORT version)
{
	try
	{
		ClientHandles clienthandles;
		clienthandles.insert(*clienthandle); // This could throw bad_alloc
		SendResponse (&clienthandles, response, version);
	}
	catch(std::bad_alloc&)
	{
		m_pRequest->SetMemoryAllocationExceptionFlag();
		m_pConnectionsManager->IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
		LOG (ERROR, "Exception while allocating memory in StoreResponse"); 
	}

	return;
}

void RequestProcessor::SendResponse (ClientHandles* clienthandles, const Buffer* response, USHORT version)
{
	USHORT Version ;

	if (version == DEFAULT_VERSION)
		Version = m_Version;
	else
		Version = version;

	StoreMessage(clienthandles, response, Version, FALSE);

	return;
}

void RequestProcessor::send_update_callback (uv_async_t* handle)
{
	RequestProcessor* pReqProcessor = (RequestProcessor*)handle->data;

	// Run response sending cycle (uv_write & after_write) once after we put response to responses list	
	TimerFunction pTimerFunction = pReqProcessor->m_pTimerFunction;
	(pReqProcessor->m_pConnectionsManager->*pTimerFunction)();

	pReqProcessor->WaitOnBarrier(); 
}

// If request processing thread wants to send intermittent updates to client(s) they can call these.
// It calls uv_async_send which results in getting callback from libuv 
// Value of version equal to DEFAULT_VERSION is treated as version of client who is storing this response
// Called from request processing threads
void RequestProcessor::SendUpdate (ClientHandle* clienthandle, const Buffer* update, USHORT version) 
{
	try
	{
		ClientHandles clienthandles;
		clienthandles.insert(*clienthandle); // This could throw bad_alloc
		MulticastUpdate (&clienthandles, update, version);
	}
	catch(std::bad_alloc&)
	{
		m_pRequest->SetMemoryAllocationExceptionFlag();
		m_pConnectionsManager->IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
		LOG (ERROR, "Exception while allocating memory in SendUpdate"); 
	}

	return;
}

void RequestProcessor::MulticastUpdate (ClientHandles* clienthandles, const Buffer* update, USHORT version)
{
	ASSERT (m_pRequest!=NULL);

	int RetVal = FALSE;

	USHORT Version ;

	if (version == DEFAULT_VERSION)
		Version = m_Version;
	else
		Version = version;

	StoreMessage(clienthandles, update, Version, TRUE);

	return;
}

/*
StoreMessage actually constructs response object and adds it to responses list of each of its intended Clients.
		It doesn't wait till it writes to the socket. This is by design by keeping performance as main concern. 
		Hence we cannot really return if write fails for the response. Regardless, there is no gurantee that even if uv_write is successful response
		delivery to client is successful (Ref: https://groups.google.com/forum/#!topic/libuv/hbvMnWOnDV4) Hence, we cannot rely on uv_write return value. 
		Requests processors have to have employ their own mechanism to chk if client was connected (e.g. thru disconnection handler) and to ensure response
		delivery is successful (e.g. ack from client) Hence we keeping return value as void.
*/
void RequestProcessor::StoreMessage (ClientHandles* p_clienthandles, const Buffer* response, USHORT version, BOOL bIsUpdate)
{
	double ArrivalTime = m_pRequest ? m_pRequest->GetArrivalTime() : ConnectionsManager::GetHighPrecesionTime();

	ClientHandles& clienthandles = (*p_clienthandles);

	UINT MaxResponseSize ;

	// We also take care of response size while creating response in Response::CreateResponse. But there it is with additional fields.
	// If we proactively filter lengthy response here itself, it won't go till that point.
	VersionParameters* pVersionParams = m_pConnectionsManager->GetVersionParameters(version);

	if (pVersionParams)
		MaxResponseSize = pVersionParams->m_MaxResponseSize;

	m_ResponseObjectsQueued = 0;
	m_TotalResponseObjectsQueued = 0;
	m_ResponseObjectsSent = 0;

	if ((pVersionParams == NULL) || (clienthandles.size() == NULL) || (response->base == NULL) || (response->len == 0) || (response->len  > MaxResponseSize))  
	{
		LOG (ERROR, "Cannot store message. Either no client(s) to store message to OR message attributes are invalid.");
		return;
	}

	try
	{
		// clienthandles could have many handles which are for other servers. We got to first create map <ServerIPv4Address, ClientHandles> to get consolidated handles for server.
		// (Using unsorted_map could increase performance but its complicated when vector is key)
		mapServersAndHandles ServersAndHandles;
		// unsigned int handlecount = (UINT)clienthandles.size();

		//for (unsigned int i=0; i<handlecount; i++)
		for (ClientHandles::iterator it=clienthandles.begin(); it!=clienthandles.end(); ++it)
		{
			IPv4Address ServerIPAddress = (*it).m_ServerIPv4Address;
			// ClientHandlesPtrs* pClientHandlesPtrs = &ServersAndHandles[clienthandles[i].m_ServerIPv4Address]; // Code when clienthandles was vector
			ClientHandlesPtrs* pClientHandlesPtrs = &ServersAndHandles[ServerIPAddress];
			ASSERT (pClientHandlesPtrs); // If pClientHandlesPtrs is NULL ServersAndHandles map should have thrown bad_alloc
			// pClientHandlesPtrs->insert(&(clienthandles[i]));  // Code when clienthandles was vector   
			ClientHandle* pClientHandle = (ClientHandle*)&(*it); 
			pClientHandlesPtrs->insert(pClientHandle);
		}

		// At this point, against IPv4Address in map we have vector of clienthandle pointers. 
		// Traverse through map and construct Response object with pClient only for current server, and with clienthandles for remote server (response to be forwarded to)
		for(mapServersAndHandles::iterator iterator = (ServersAndHandles).begin(); iterator != (ServersAndHandles).end(); iterator++)
		{
			ClientHandlesPtrs clienthandle_ptrs =  iterator->second;

			// It is not possible we have ip address but don't have any clienthandle against it
			ASSERT (clienthandle_ptrs.size() != 0);

			// For responses which need to be forward we receive IP address as part of handle. But we donot receive port. So we should set it here.
			// (NOW WE HAVE STATIC PORT COMMON FOR ALL SO WE DON'T NEED THIS CODE ANYMORE)
			//for (unsigned int i=0; i<clienthandle_ptrs.size(); i++)
			//	clienthandle_ptrs[i]->m_ServerIPv4Address.SetPort(GetClientHandle().m_ServerIPv4Address.GetPort());

			CreateResponseAndAddToQueues(response, clienthandle_ptrs, version, bIsUpdate, ArrivalTime);
		}

		//if ((!bIsUpdate) && (++m_ResponseCountPerThread) > 1)
		//	LOG (NOTE, "Thread storing multiple responses. This may result in memory denial for some responses. Consider using SendUpdate or MulticastUpdate instead.");
	}
	catch(std::bad_alloc&)
	{
		if (m_pRequest) // The purpose of setting exception flag is to disconnect the client who has sent the request. If no request means no client was associated (e.g. keep alive processing)
			m_pRequest->SetMemoryAllocationExceptionFlag();

		m_pConnectionsManager->IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
		return;
	}


#if 1
	if (bIsUpdate)
	{
		uv_async_send (&m_AsyncHandle); // send_update_callback will call SendResponse
		WaitOnBarrier();
	}
#else
	// Here we tried to hold this thread TILL WE GET MESSAGE SENT CALLBACK in event loop.
	// However this idea was NOT AT ALL GOOD. Because libuv calls uv_write callback only after response goes out of the server (or if sending fails)
	// Now if a client is slow in receiving response, this means to hold current thread for that single client till it receives response.
	// From server perspective this would be big performance penalty. Few such slow clients and all the responses and request would be on hold.
	// (Because one request processing thread goes on hold, this further means request queue would go on increasing)
	// (Additionally, at peak memory when we start getting exceptions, this was giving trouble. May be because our simulators were halting resulting they never received response. So we never get callback in server). 
	// All in all, we decided to not work on it anymore.
	int ResponseObjectsSent = 0;
	int TotalResponseObjectsQueued = 0;

	if (bIsUpdate)
	{
		uv_rwlock_wrlock (&m_rwlTotalResponseObjectsQueuedCounterLock);
		m_TotalResponseObjectsQueued = m_ResponseObjectsQueued;
		uv_rwlock_wrunlock (&m_rwlTotalResponseObjectsQueuedCounterLock);

		ResponseObjectsSent = GetResponseObjectsSent();
		TotalResponseObjectsQueued = GetTotalResponseObjectsQueued();

		// If it is update and created some responses, then we'll have to wait here till those responses are sent by event loop
		if (TotalResponseObjectsQueued)
		{
			ASSERT (ReferenceCount); // Some objects queued but no references ?
			ASSERT (ResponseObjectsSent <= TotalResponseObjectsQueued);

			if (ResponseObjectsSent < TotalResponseObjectsQueued)
			{
				uv_async_send (&m_AsyncHandle); // send_update_callback will call SendResponse
				WaitOnBarrier();
			}
		}
	}
#endif

	// It doesn't make any sense if we return ReferenceCount. Because ReferenceCount could be just 1 if it was forwarded response inspite number of recepients were more than one.
	// Even if all recepients are local, having ReferenceCount equal to all number of recepient doesn't mean they have received the response. It only means response has been queued 
	// up successfully. But for application it doesn't have any significance.
	// return ReferenceCount;
	return;
}
