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
/*

This is how we connect in our simulator:

In main call uv_getaddrinfo(loop, gai_req, after_getaddrinfo, ServerIPv4Address, Port, NULL);

In after_getaddrinfo, call uv_tcp_connect(&m_connect_req, &m_server, address, after_connect);

In after_connect call uv_read_start(pClient->m_tcp_handle, alloc_buffer, on_read); 
	In after_connect we also get m_tcp_handle which we use in uv_write (If unable to connect we get error here only)


So, let's plan like this:
Server code will maintain kind of a table: 	
------------------------------------------------
ServerIPAddress | ConnectionState | Connection |
------------------------------------------------
                |                 |            |
Default state of new server entry in the table is DISCONNECTED
When server disconnects, it should change status to DISCONNECTING and only after closing handle change it to DISCONNECTED
When server fails to connect, it should set status to FAILED
When server connects, it should set state to CONNECTED and Connection=connection_ptr

This is how GetServerConnection should behave:
If status is DISCONNECTED initiate connection and set status CONNECTING and return the same (When connection succeeds set status CONNECTED else set it FAILED)
If status is CONNECTING
	If status was CONNECTING for short time, just return status
	If status was CONNECTING for long time, return FAILED
If status is CONNECTED return status along with connection
If status is OVERFLOWED
	If status was OVERFLOWED for long time, disconnect connection set status to DISCONNECTING and return the same (When handle is closed it should change status to DISCONNECTED)
	If status was OVERFLOWED for short time, just return the status
If status is DISCONNECTING return DISCONNECTING
If status is FAILED
	If status was FAILED for long time initiate connection and set status CONNECTING and return the same (When connection succeeds set status CONNECTED else set it FAILED)
	If status was FAILED for short time, just return the status
*/

stPeerServer::stPeerServer(IPv4Address& oServerIPv4Address)
{
	// NOTE: DO NOT INITIALIZE ANYTHING HERE. DO IT IN Construct() INSTEAD.
	// THIS IS BECAUSE WE CANNOT CALL THIS C'TOR FROM COPY C'TOR HENCE WE HAVE Construct() FUNCTION.
	Construct(oServerIPv4Address);
}

void stPeerServer::Construct(IPv4Address& oServerIPv4Address)
{
	m_bIsServer = true;

	// for (int i=0; i<4; i++) ServerIPv4AddressBytes[i] = NULL ;
	Status = CONNECTION_UNINITIATED ;
	ResponsesForwarded=0;
	m_pPeerServersManager=NULL;
	OverflowedTime=NULL;
	DisconnectedTime=NULL;
	ConnectingTime=NULL;
	m_connection = NULL ;
	m_connect_req.data = NULL;
	m_write_req.data = this;

	m_ResponseBuffer.base = m_response_buffer;
	m_ResponseBuffer.len = sizeof(m_response_buffer);

	m_ResponseBufferIndex = 0;

	m_Version = DEFAULT_VERSION;

	m_bResponseForwardingSucceededLastTime = TRUE;

	if (uv_rwlock_init(&m_rwlResponsesQueueLock) < 0)
	{
		LOG (ERROR, "Initializing response queue lock failed");
		throw ConnectionCreationException();
	}

	m_ServerIPv4Address = oServerIPv4Address;

	// Memory allocation exception while creating responses queue will be taken care by AddResponseToQueue who tries to construct stPeerServer
	m_ResponsesQueue1 = new std::deque<class Response*>;
	m_ResponsesQueue2 = new std::deque<class Response*>;

	m_pResponsesBeingSent = new Responses ;
}

#if 1
// We need to override copy constructor because the way stl map works.
// The moment we do m_ServersInfo[ServerIPv4Address.GetIPv4Address()] in GetServerConnection, and if the server entry doesn't 
// exist in the map, it will construct stPeerServerInfo for that server. However, it also would destruct it as well. 
// This happens because before creating final stPeerServerInfo, STL map calls copy constructors and then destructs those temporary objects.
stPeerServer::stPeerServer(stPeerServer& oOriginalInfo)
{
	ASSERT(0); // With dynamic allocation to stPeerServer in server map, we don't actually need copy c'tor
	Construct(oOriginalInfo.m_ServerIPv4Address);

	// WE CANNOT CALL stPeerServer() FROM HERE. TRIED ALL THESE ->
	// (*this).stPeerServer();
	// this->stPeerServer();
	// *this = oOriginalInfo;
	// m_ResponseBuffer.base = response_buffer;
}
#endif

// Although, since we do not delete server entry from map (as per current design) it's very unlikely that d'ctor will be called,
// still we formally should have provision to delete alocated memory in c'tor.
stPeerServer::~stPeerServer()
{
	DEL (m_ResponsesQueue1);
	DEL (m_ResponsesQueue2);
	DEL (m_pResponsesBeingSent);
	uv_rwlock_destroy(&m_rwlResponsesQueueLock);
}

PeerServersManager::PeerServersManager()
{
	m_ServersConnected = 0;
	m_ServersClosing = 0;
	m_ServersConnecting = 0;

	int retval = uv_rwlock_init(&m_rwlServerSetLock);
	ASSERT_THROW ((retval >= 0), "Initializing server set lock failed");

	retval = uv_rwlock_init(&m_rwlServersInfoLock);
	ASSERT_THROW ((retval >= 0), "Initializing server info lock failed");
}

PeerServersManager::~PeerServersManager()
{
	// LOG (INFO, "Destroying server set lock");
	uv_rwlock_destroy(&m_rwlServerSetLock);
	uv_rwlock_destroy(&m_rwlServersInfoLock);

	// Get from map connections of all the servers registered and delete them one by one (they were allocated in AddResponseToQueue)
	typedef std::map<IPv4Address, stPeerServer*>::iterator it_type;
	it_type iterator = m_ServersInfo.begin();
	for(iterator = m_ServersInfo.begin(); iterator != m_ServersInfo.end(); iterator++)
	{
		stPeerServer* pServersInfo = iterator->second;
		DEL(pServersInfo); 
	}
}

int PeerServersManager::AreServersClosing() 
{ 
	return m_ServersClosing; 
} // Called by event loop
		
int PeerServersManager::AreServersConnecting() 
{ 
	return m_ServersConnecting; 
} // Called by event loop

int PeerServersManager::GetServersConnectedCount() 
{ 
	return m_ServersConnected; // Called by event loop
}

void PeerServersManager::DisconnectAndCloseAllConnections()
{
	// Get from map connections of all the servers registered and disconnect them one by one (call uv_close)
	typedef std::map<IPv4Address, stPeerServer*>::iterator it_type;
	it_type iterator = m_ServersInfo.begin();
	for(iterator = m_ServersInfo.begin(); iterator != m_ServersInfo.end(); iterator++)
	{
		stPeerServer* conninfo = iterator->second;
		DisconnectServer(conninfo); 
	}
}

void PeerServersManager::on_server_closed(uv_handle_t* handle) 
{
	// Set status CONNECTION_DISCONNECTED
	stPeerServer* PeerSvr = (stPeerServer*) handle->data ;
	PeerSvr->Status = CONNECTION_DISCONNECTED;
	PeerSvr->DisconnectedTime = time(&PeerSvr->DisconnectedTime);
	PeerSvr->ResponsesForwarded = 0;
	PeerSvr->m_ResponseBufferIndex = 0;
	PeerSvr->m_connection = NULL;

	LOG (DEBUG, "Calling SendResponses by on_server_closed");
	PeerSvr->m_pPeerServersManager->m_ServersClosing --;
	PeerSvr->m_pPeerServersManager->DoPeriodicActivities(); // Whenever we set PeerSvr->Status value in callback, we have to call SendResponses. So that it can run with updated status info.
	return;
}

void PeerServersManager::DisconnectServer(stPeerServer* PeerSvr, BOOL bReduceConnectedServersCount)
{
	if ((!PeerSvr) || (!PeerSvr->m_connection)) 
		return;

	// stPeerServer* PeerSvr = (stPeerServer*) connection->data ;
	
	if ((PeerSvr->Status != CONNECTION_CONNECTED) && (PeerSvr->Status != CONNECTION_OVERFLOWED) && \
		// For connecting servers also we need to call this function. Otherwise call to this from after_connect fails.
		(PeerSvr->Status != CONNECTION_CONNECTING) && (PeerSvr->Status != CONNECTION_CONNECTING_TIMED_OUT)) 
		return;

	if (bReduceConnectedServersCount)
		PeerSvr->m_pPeerServersManager->m_ServersConnected-- ; 

	PeerSvr->Status = CONNECTION_DISCONNECTING;
	// Stop reading from this connection
	uv_read_stop((uv_stream_t*) PeerSvr->m_connection);
	uv_handle_t* handle = (uv_handle_t*)PeerSvr->m_connection;
	PeerSvr->m_pPeerServersManager->m_ServersClosing ++;

	uv_close (handle, on_server_closed);
}

void PeerServersManager::alloc_buffer(uv_handle_t* connection, uv_buf_t* buffer)
{
	stPeerServer* PeerSvr = (stPeerServer*) connection->data ;

	ASSERT (PeerSvr->m_ResponseBuffer.base == PeerSvr->m_response_buffer);

	// Here, index should always be less than length. 
	// If it's equal or greater, there is something wrong in logic somewhere.
	ASSERT (PeerSvr->m_ResponseBufferIndex < PeerSvr->m_ResponseBuffer.len);

	// Check how many bytes remaining after m_Request_index in m_ResponseBuffer.base
	size_t SizeAvailable = PeerSvr->m_ResponseBuffer.len - PeerSvr->m_ResponseBufferIndex;

	buffer->base = &PeerSvr->m_ResponseBuffer.base[PeerSvr->m_ResponseBufferIndex];
	buffer->len = (ULONG)SizeAvailable;

	// Unlike in LocalClientManager, here we strip off response in on_read itself as soon as we found it in buffer.
	// Thus, we have no reason for not doing that (unlike in local client case we don't do it if request is already being processed in threads).
	// Hence we must have some room available next time libuv calls alloc_buffer (Else we've set buffer too less than response size).
	ASSERT (SizeAvailable); 

	return;
}

// When there is data in socket, libuv calls this with read data in write_req.base, 'nread' as number of bytes read and write_req.len as max size write_req.base
// PeerServerManager::on_read gets called when server (to which this server is connected to, unlike as in LocalClientsManager::on_read where it receves clinets 
// to which it is listening to) sends back acknowledgement RESPONSE_ACKNOWLEDGEMENT_OF_FORWARDED_RESP (indicating it received forwarded response) or KEEP_ALIVE 
// or RESPONSE_ERROR (peer server encountered error)
void PeerServersManager::on_read(uv_stream_t* connection, ssize_t nread, const uv_buf_t* read_bytes)
{
	ADD2PROFILER;

	stPeerServer* PeerSvr = (stPeerServer*) connection->data ;

	ASSERT (PeerSvr->m_ResponseBuffer.base == PeerSvr->m_response_buffer);

	if (nread < 0)
	{
		// if (nread == UV_ECONNRESET)
		{
			// Disconnect
			IPv4Address ServerIPv4Address;
			ServerIPv4Address = PeerSvr->m_ServerIPv4Address;
			LOG (ERROR, "Error %d (%s) in on_read. Disconnecting server %d.%d.%d.%d", (int)nread, uv_strerror((int)nread), ServerIPv4Address[0], ServerIPv4Address[1], ServerIPv4Address[2], ServerIPv4Address[3]);
			PeerSvr->m_pPeerServersManager->DisconnectServer (PeerSvr);
		}
		return;
	}
	else
	{
		PeerSvr->m_ResponseBufferIndex += (unsigned int) nread; //  Windows x64 uses the LLP64 programming model, in which int and long remain 32 bit

		while(1) // Extract off all possible requests from incoming buffer
		{
			uv_buf_t response;
			response.base = NULL;

			// Let's use same method for parsing and validating master protocol, that we've used for stClient communication
			// UCHAR RetVal = PeerSvr->m_pPeerServersManager->ValidateProtocolAndExtractRequest (PeerSvr->m_ResponseBuffer.base, PeerSvr->m_ResponseBufferIndex, PeerSvr->m_ResponseBuffer.len, PeerSvr->m_Version, response);
			UCHAR RetVal = RequestParser::GetInstance(dynamic_cast<ConnectionsManager*>(PeerSvr->m_pPeerServersManager))->ValidateProtocolAndExtractRequest (PeerSvr->m_ResponseBuffer.base, PeerSvr->m_ResponseBufferIndex, PeerSvr->m_Version, response);
			

			if ((RetVal == REQUEST_FOUND) && (response.base != NULL))
			{
				if (RetVal == REQUEST_FOUND)
				{
					PeerSvr->m_pPeerServersManager->ProcessResponse (&response, PeerSvr);
				}

				// Remove first (ReqSize + HEADER_SIZE) bytes from the request buffer
				for (unsigned int i=(response.len + HEADER_SIZE); i<PeerSvr->m_ResponseBufferIndex; i++)
				{
					PeerSvr->m_ResponseBuffer.base[i-(response.len + HEADER_SIZE)] = PeerSvr->m_ResponseBuffer.base[i];
				}

				// Adjust request index acordingly. Remember, we might have got much more bytes than request size. 
				// Hence m_ResponseBufferIndex might have been greater than (ReqSize + HEADER_SIZE).
				// So m_ResponseBufferIndex not necessarily will be zero after stripping off the request. 
				PeerSvr->m_ResponseBufferIndex -= (response.len + HEADER_SIZE);

				ASSERT (PeerSvr->m_ResponseBufferIndex >= 0);
			}
			else if ((RetVal == INVALID_HEADER) || (RetVal == INVALID_VERSION) || (RetVal == INVALID_SIZE)) 
			{	
				PeerSvr->m_ResponseBufferIndex=0;
				LOG (ERROR, "Error in header in the acknowledgement received from other server.");
			}
			else if (RetVal == WAIT_FOR_MORE_BYTES)
			{
				break;
			}
			else
			{
				// It could be memory allocation caused we came here. No need to assert.
				LOG (ERROR, "Unknown return code by ValidateProtocolAndExtractRequest OR pRequest was NULL when RetVal was REQUEST_FOUND");
			}
		}
	}

	ASSERT (PeerSvr->m_ResponseBufferIndex < PeerSvr->m_ResponseBuffer.len);
}

void PeerServersManager::ProcessResponse(uv_buf_t* response, stPeerServer* pPeerSvr)
{
	switch(response->base[0])
	{
		case RESPONSE_KEEP_ALIVE:
			LOG (NOTE, "KeepAlive received");
			break;

		case RESPONSE_ERROR:
			LOG (ERROR, "Error received");
			break;

		case RESPONSE_ACKNOWLEDGEMENT_OF_FORWARDED_RESP:
			{
				pPeerSvr->ResponsesForwarded --;
				if (pPeerSvr->ResponsesForwarded < 1 /*RequestProcessor::GetCommonParameters().MaxPendingRequests*/)
				{
					pPeerSvr->Status = CONNECTION_CONNECTED ; // Change status from CONNECTION_OVERFLOWED to CONNECTION_CONNECTED
					pPeerSvr->m_pPeerServersManager->DoPeriodicActivities(); // Whenever we set PeerSvr->Status value in callback, we have to call SendResponses. So that it can run with updated status info.
				}
			}
			break;

		default:
			LOG (ERROR, "Unnown response received");
			break;
	}
}

void PeerServersManager::after_connect(uv_connect_t* connect_req, int status)
{
	// printf("\nIn after_connect");

	stPeerServer* PeerSvr = (stPeerServer*)connect_req->data ;

	// We get handle allocated here by libuv. Let's store it in our PeerSvr structure.
	PeerSvr->m_connection = connect_req->handle ;

	// As we will be getting handle pointer in on_read, lets store PeerSvr pointer in 'data' so we can regain it there 
	PeerSvr->m_connection->data = PeerSvr ;

	// Regardless success or failure, we must decrease here counter that indicates "connections in progress" count
	PeerSvr->m_pPeerServersManager->m_ServersConnecting -- ;

	if (status < 0)
	{
		// Disconnect (When connection fails, it comes here. For e.g. unreachable server. We must however call disconnect to close handle.)
		// We must call uv_close when uv_connect fails.
		// https://groups.google.com/forum/#!topic/libuv/DUBr8DtzsWk

		PeerSvr->m_pPeerServersManager->DisconnectServer(PeerSvr, FALSE); // FALSE because call made before server was connected
	}
	else
	{
		PeerSvr->Status = CONNECTION_CONNECTED; 

		// Before we accept start reading, lets set internal buffer size to zero (so it will directly write from our allocated buffers).
		// This is to avoid internal buffer overflows.
		DWORD NewBuffSize = 0L;
		DWORD NewBuffSizeLen = sizeof(DWORD);

		if (setsockopt(PeerSvr->m_server.socket, SOL_SOCKET, SO_SNDBUF, (const char *)&NewBuffSize, NewBuffSizeLen) < 0) 
		{
			LOG(ERROR, "Error setting new send buffer size");
			return;
		}

/*
		if (setsockopt(PeerSvr->m_server.socket, SOL_SOCKET, SO_RCVBUF, (const char *)&NewBuffSize, NewBuffSizeLen) < 0) 
		{
			LOG(ERROR, "Error setting new receive buffer size");
			return;
		}
*/
		PeerSvr->m_pPeerServersManager->m_ServersConnected ++ ;

		int RetVal = uv_read_start(PeerSvr->m_connection, alloc_buffer, on_read);
		if (RetVal == -1)
			PeerSvr->m_pPeerServersManager->DisconnectServer(PeerSvr);

		LOG (DEBUG, "Calling SendResponses by after_connect");
		PeerSvr->m_pPeerServersManager->DoPeriodicActivities(); // Whenever we set PeerSvr->Status value in callback, we have to call SendResponses. So that it can run with updated status info.
	}
}

void PeerServersManager::after_getaddrinfo(uv_getaddrinfo_t* gai_req, int status, struct addrinfo* ai) 
{
	stPeerServer* PeerSvr = (stPeerServer*)gai_req->data;

	if (status < 0)
	{
		// Set status CONNECTION_DISCONNECTED
		PeerSvr->Status = CONNECTION_DISCONNECTED;
		DEL (gai_req); // --------------- allocated in InitiateConnection
		
		LOG (DEBUG, "Calling SendResponses by after_getaddrinfo");
		
		PeerSvr->m_pPeerServersManager->DoPeriodicActivities(); // Whenever we set PeerSvr->Status value in callback, we have to call SendResponses. So that it can run with updated status info.
		return;
	}

	uv_tcp_init (PeerSvr->m_pPeerServersManager->loop, &PeerSvr->m_server);

	PeerSvr->m_connect_req.data = PeerSvr; // So that we get stPeerServer in after_connect
	int RetVal = uv_tcp_connect(&PeerSvr->m_connect_req, &PeerSvr->m_server, ai->ai_addr, after_connect);
	PeerSvr->m_pPeerServersManager->m_ServersConnecting ++ ;

	if (RetVal == -1)
	{
		PeerSvr->Status = CONNECTION_DISCONNECTED;
		PeerSvr->m_pPeerServersManager->DoPeriodicActivities(); // Whenever we set PeerSvr->Status value in callback, we have to call SendResponses. So that it can run with updated status info.
		return;
	}

	DEL (gai_req); // --------------- allocated in InitiateConnection
	uv_freeaddrinfo(ai);
}

void PeerServersManager::InitiateConnection(stPeerServer* pPeerSvr)
{
	pPeerSvr->Status = CONNECTION_CONNECTING;
	pPeerSvr->ConnectingTime = time (&pPeerSvr->ConnectingTime);
	
	uv_getaddrinfo_t* gai_req = NULL;
	try
	{
		gai_req = new uv_getaddrinfo_t; // ---------- To be deleted in after_getaddrinfo
	}
	catch(std::bad_alloc&)
	{
		pPeerSvr->Status = CONNECTION_DISCONNECTED; // Do not call SendResponses as we need to call it after changing Status but ONLY from callbacks (Otherwise it might run into infinite recurssion)
		return;
	}

	char strIPAddress[64];
	char strPort[8];
	sprintf_s(strIPAddress, 64, "%hhu.%hhu.%hhu.%hhu", pPeerSvr->m_ServerIPv4Address[0], pPeerSvr->m_ServerIPv4Address[1], pPeerSvr->m_ServerIPv4Address[2], pPeerSvr->m_ServerIPv4Address[3]);
	sprintf_s(strPort, 8, "%hu", pPeerSvr->m_ServerIPv4Address.GetPort());
	gai_req->data = pPeerSvr ;
	int RetVal = uv_getaddrinfo(loop, gai_req, PeerServersManager::after_getaddrinfo, strIPAddress, strPort, NULL);
	if (RetVal == -1)
	{
		pPeerSvr->Status = CONNECTION_DISCONNECTED; // Do not call SendResponses as we need to call it after changing Status but ONLY from callbacks (Otherwise it might run into infinite recurssion)
		return;
	}
}

void PeerServersManager::IncreaseForwardedResponsesCount (stPeerServer* pPeerServerInfo)
{
	// By the time we call this (viz. through stClient::from after_send_response) if the server has been not connected anymore, 
	// it doesn't make any sense to increase forwarded response count as we are not going to get acknowledgement for the same.
	if (pPeerServerInfo->Status != CONNECTION_CONNECTED)
	{
		// Do nothing here just return after rdunlock. 
	}
	else // It can be anything other that CONNECTION_CONNECTED
	{
		pPeerServerInfo->ResponsesForwarded ++; 
		// This server becomes client for other server when forwarding responses. 
		// So that receiving server shouldn't ignore further forwarded responses (requests to receiving server),
		// we must set CONNECTION_OVERFLOWED flag thus proactively preventing more responses being forwarded to the server.
		if (pPeerServerInfo->ResponsesForwarded >= 1 /*RequestProcessor::GetCommonParameters().MaxPendingRequests*/)
		{
			time_t CurrentTime = time(&CurrentTime);
			// pPeerServerInfo->Status = CONNECTION_OVERFLOWED;  // We've disabled overflow
			pPeerServerInfo->OverflowedTime = CurrentTime;
		}
	}

	return;
}

BOOL PeerServersManager::AddToServerSet (std::set <stPeerServer*>* pServersSet, stPeerServer* pPeerServer, BOOL bTobeLocked)
{
	BOOL RetVal = TRUE;

	// Since this function gets called from threads, checking in server set if server already exists, can happen concurrent. Hence could increase performance. 
	if (bTobeLocked) uv_rwlock_rdlock(&m_rwlServerSetLock);
	const bool bIsServerInSet = pServersSet->find(pPeerServer) != pServersSet->end();
	if (bTobeLocked) uv_rwlock_rdunlock(&m_rwlServerSetLock);

	if (bIsServerInSet)
		return TRUE;

	if (bTobeLocked) uv_rwlock_wrlock(&m_rwlServerSetLock);
	try
	{
		pServersSet->insert (pPeerServer);
	}
	catch(std::bad_alloc&)
	{
		LOG (NOTE, "Memory error adding server to receivers set. This may result responses never being forwarded."); 
		IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
		RetVal = FALSE;
	}
	if (bTobeLocked) uv_rwlock_wrunlock(&m_rwlServerSetLock);

	return RetVal;
}

// This function runs in threads. Called by ConnectionsManager::AddResponseToQueues.
BOOL PeerServersManager::AddResponseToQueue(Response* pResponse, BOOL& bHasEncounteredMemoryAllocationException)
{
	BOOL bAdded = FALSE;
	std::deque<Response*>* pResponsesQueue;
	std::set<stPeerServer*>* pServersSet;
	IPv4Address ServerIPv4Address = pResponse->GetServersIPv4Address(); 

	// Here value of map (pointer to server) will be created, if it wasn't already exists.
	// If so we must need to create new stPeerServer and assign it to newly created value.
	// Since this piece of code runs in threads, we must guard it with locks.

	stPeerServer* pPeerServer = NULL;

	uv_rwlock_rdlock(&m_rwlServersInfoLock); // So that we won't access map while server being added to it
	if (m_ServersInfo.find(ServerIPv4Address) != m_ServersInfo.end())
		pPeerServer = m_ServersInfo[ServerIPv4Address];
	uv_rwlock_rdunlock(&m_rwlServersInfoLock);

	if (pPeerServer == NULL)
	{
		uv_rwlock_wrlock(&m_rwlServersInfoLock);
		try
		{
			// By the time a thread comes here, stPeerServer object already might have created by another thread. So we need to check again.
			if (m_ServersInfo.find(ServerIPv4Address) == m_ServersInfo.end())
			{
				m_ServersInfo[ServerIPv4Address] = new stPeerServer(ServerIPv4Address);
				m_ServersInfo[ServerIPv4Address]->m_pPeerServersManager = this;
			}

			pPeerServer = m_ServersInfo[ServerIPv4Address];
		}
		catch(std::bad_alloc&) // Exception will occur only when pResponsesQueue->push
		{
			bHasEncounteredMemoryAllocationException = TRUE; // This flag enables framwork to disconnect request sending client. When memory is low framwork disconnect connection.
			IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
		}
		catch(ConnectionCreationException&)
		{
			IncreaseExceptionCount(CONNECTION_CREATION_EXCEPTION, __FILE__, __LINE__);
		}
		uv_rwlock_wrunlock(&m_rwlServersInfoLock);
	}

	if (pPeerServer == NULL)
		return FALSE;

	if (m_bResponseDirectionFlag) // This flag is already protected by read lock by caller ConnectionsManager::AddResponseToQueues
	{
		pResponsesQueue = pPeerServer->m_ResponsesQueue1;
		pServersSet = &m_RecevingServersSet1;
	}
	else
	{
		pResponsesQueue = pPeerServer->m_ResponsesQueue2;
		pServersSet = &m_RecevingServersSet2;
	}

	uv_rwlock_wrlock(&pPeerServer->m_rwlResponsesQueueLock);
	try
	{

		// If receiving server has CONNECTION_OVERFLOWED status, we don't send response it remains pending in the queue.
		// Thus, its quite possible that pResponsesQueue could grow big especially when receiving server is slow.
		// So, let's not put MaxPendingResponses limit here as this limit could be small and better suit for clients.
		// Instead let's just check if MaxPendingRequests & MaxPendingResponses is not zero.
		// (MaxPendingRequests zero indicates receiving server isn't expecting any requests at all 
		// Neverthless, if MaxPendingRequests was zero, we wouldn't have came till this point)

		/* Please read comment above to know why line below has been commented out */
		// if (pResponsesQueue->size() < (UINT)(RequestProcessor::GetCommonParameters().MaxPendingResponses/2)) // Half of limit is available (In one of two queues) 
		if (/* (RequestProcessor::GetCommonParameters().MaxPendingRequests) && */ (RequestProcessor::GetCommonParameters().MaxPendingResponses))
		{
			pResponsesQueue->push_front(pResponse);

			if (AddToServerSet(pServersSet, pPeerServer, TRUE) == FALSE)
			{
				pResponsesQueue->pop_front();  
			}
			else
			{
				bAdded = TRUE;
			}
		}
		else
		{
			// We don't need to avoid overlogging here as in LocalClientsManager::AddResponseToQueue, because we are very less likely to come here (see condition above)
			LOG (ERROR, "Response queue for peer server %d.%d.%d.%d is full. Cannot add response.", ServerIPv4Address[0], ServerIPv4Address[1], ServerIPv4Address[2], ServerIPv4Address[3]);
		}
	}
	catch(std::bad_alloc&) // Exception will occur only when pResponsesQueue->push
	{
		bHasEncounteredMemoryAllocationException = TRUE;
		IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
	}
	uv_rwlock_wrunlock(&pPeerServer->m_rwlResponsesQueueLock);

	return bAdded;
}

int PeerServersManager::GetServerConnection(stPeerServer* pPeerServer)
{
	ADD2PROFILER;

	int StatusToReturn = CONNECTION_UNINITIATED;
	
	switch (pPeerServer->Status)
	{
		case CONNECTION_UNINITIATED:
			{
				InitiateConnection(pPeerServer); // We are passing here stPeerServer associated with IPv4Address
				StatusToReturn = pPeerServer->Status ; // = CONNECTION_CONNECTING;
				break;
			}

		case CONNECTION_CONNECTING:
			{
				time_t CurrentTime = time( &CurrentTime );

				// If status was CONNECTING for long time, return FAILED
				if (CurrentTime - pPeerServer->ConnectingTime > WAIT_FOR_CONNECTION)
				{
					/*
						We cannot disconnect whilest it is connecting. Because .m_connection we get only in after_connect.
						So we've to wait for callback. There is no option.
						Also there is no way to cancel ongoing connections:
						https://groups.google.com/forum/#!topic/libuv/O0ag4r75Xqk
					*/
					// DisconnectServer(pPeerServer); 

					StatusToReturn = CONNECTION_CONNECTING_TIMED_OUT;
					break;
				}
				// If status was CONNECTING for short time, just return status
				StatusToReturn = CONNECTION_CONNECTING; 
				break;
			}

		case CONNECTION_CONNECTED:
			{
				StatusToReturn = CONNECTION_CONNECTED;
				break;
			}

		case CONNECTION_OVERFLOWED:
			{
				ASSERT (0); // We've disabled overflow so we shouldn't get it here as connection status

				time_t CurrentTime = time( &CurrentTime );
				// If status was OVERFLOWED for long time, disconnect connection set status to DISCONNECTING and return the same (When handle is closed it should change status to DISCONNECTED)
				if ((CurrentTime - pPeerServer->OverflowedTime) > MAX_OVERFLOWED_TIME)
				{
					//IPv4Address ServerIPv4Address;
					//ServerIPv4Address = pPeerServer->ServerIPv4Address;
					LOG (NOTE, "Server %d.%d.%d.%d overflowed for %d seconds. Disconnecting.", pPeerServer->m_ServerIPv4Address[0], pPeerServer->m_ServerIPv4Address[1], pPeerServer->m_ServerIPv4Address[2], pPeerServer->m_ServerIPv4Address[3], MAX_OVERFLOWED_TIME); 
					DisconnectServer(pPeerServer);
					StatusToReturn =  (pPeerServer->Status); // = CONNECTION_DISCONNECTING;
					break;
				}
				// If status was OVERFLOWED for short time, just return the status
				else
				{
					StatusToReturn =  CONNECTION_OVERFLOWED;
					break;
				}
			}

		case CONNECTION_DISCONNECTING:
			{
				StatusToReturn =  CONNECTION_DISCONNECTING;
				break;
			}

		case CONNECTION_DISCONNECTED:
			{
				time_t CurrentTime = time( &CurrentTime );

				// If status was FAILED for long time initiate connection and set status CONNECTING and return the same (When connection succeeds set status CONNECTED else set it FAILED)
				if (CurrentTime - pPeerServer->DisconnectedTime > RETRY_CONNECTION_AFTER)
				{
					InitiateConnection(pPeerServer); // We are passing here stPeerServer associated with IPv4Address
					StatusToReturn = pPeerServer->Status ; // = CONNECTION_CONNECTING;
					break;
				}
				// If status was FAILED for short time, just return the status
				else
				{
					StatusToReturn =  CONNECTION_DISCONNECTED;
					break;
				}
			}

		default:
			{
				ASSERT(0); // Invalid server connection status
			}
	}

	return StatusToReturn;
}

// Called by ConnectionsManager::SendResponses()
void PeerServersManager::SendPeerServersResponses()
{
	ADD2PROFILER;

	/* Get server set and check size */
	std::set<stPeerServer*>* pServersSet;

	if (m_bResponseDirectionFlag)
	{
		pServersSet = &m_RecevingServersSet2;
	}
	else
	{
		pServersSet = &m_RecevingServersSet1;
	}


	// uv_rwlock_rdlock(&m_rwlServerSetLock);
	int servers_count = (int)pServersSet->size(); 
	// uv_rwlock_rdunlock(&m_rwlServerSetLock);

	if (servers_count == 0)
		return;

	/* Send forwarded responses for Clients connected to peer servers */
	std::set<stPeerServer*>::iterator itServersSet = pServersSet->begin();
	std::set<stPeerServer*>::iterator itServersSetEnd = pServersSet->end();

	while (itServersSet != itServersSetEnd)
	{
		std::set<stPeerServer*>::iterator current = itServersSet++;

		stPeerServer* pPeerServer = *current;

		int ConnStatus = GetServerConnection(pPeerServer);

		ASSERT (ConnStatus != CONNECTION_UNINITIATED); // We don't expect connection status as uninitiated

		if ((ConnStatus == CONNECTION_CONNECTING) || (ConnStatus == CONNECTION_OVERFLOWED))
		{
			ASSERT (ConnStatus != CONNECTION_OVERFLOWED); // We've disabled overflow so we shouldn't get it here as connection status
			continue; // continue with next server
		}

		if (pPeerServer->m_pResponsesBeingSent->size()) // Forwarded response for this server was already queued
		{
			pServersSet->erase(current); // By using direction flag, we are erasing from different set than that is being added to by threads. So no need of m_rwlServerSetLock here.
			continue;
		}

		std::deque<Response*>* pResponsesQueue = (m_bResponseDirectionFlag) ? (pPeerServer->m_ResponsesQueue2) : (pPeerServer->m_ResponsesQueue1);

		// There must be responses in queue and there must be references to responses
		const int ResponseQueueSize = (int)pResponsesQueue->size();
		ASSERT (ResponseQueueSize > 0);

		try
		{
			pPeerServer->m_pResponsesBeingSent->reserve(ResponseQueueSize);
			pPeerServer->m_pResponsesBuffersBeingForwarded.reserve(ResponseQueueSize);
		}
		catch(std::bad_alloc&)
		{
			IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
			continue;
		}

		for (int i=0; pResponsesQueue->size(); i++)
		{
			Response* pResponse = pResponsesQueue->back();

			ASSERT (pResponse); 
			ASSERT (pResponse->GetReferenceCount()); // There must be references to responses
			ASSERT (pResponse->IsForward() == TRUE); // We MUST NOT receive here response being forwarded 

			if (ConnStatus != CONNECTION_CONNECTED)
			{
				pResponse->ForwardError = ConnStatus ;
			}

			pResponse->QueuedTime = ConnectionsManager::GetHighPrecesionTime();

			pPeerServer->m_pResponsesBeingSent->push_back(pResponse); // This won't throw std:bad_alloc as max response memory is already reserved above
			pPeerServer->m_pResponsesBuffersBeingForwarded.push_back(pResponse->GetResponse());  // This won't throw std:bad_alloc as max response memory is already reserved above

			pResponsesQueue->pop_back();
		}

		// Unlike in LocalClientsManager::SendLocalClientsResponses here we must always have response queue size zero
		ASSERT (pResponsesQueue->size() == 0);
		std::deque<class Response*>().swap(*pResponsesQueue);

		int RetVal_uv_write = 0 ;
		const int NumberOfBuffers = (int)pPeerServer->m_pResponsesBuffersBeingForwarded.size();

		ASSERT ((int)pPeerServer->m_pResponsesBeingSent->size() == NumberOfBuffers);

#ifndef NO_WRITE
		if (ConnStatus == CONNECTION_CONNECTED)
		{
			RetVal_uv_write = uv_write(&pPeerServer->m_write_req, pPeerServer->m_connection, pPeerServer->m_pResponsesBuffersBeingForwarded.data(), NumberOfBuffers, ConnectionsManager::after_send_responses); 
		}
		else // when (ConnStatus == CONNECTION_DISCONNECTED or CONNECTION_DISCONNECTING or CONNECTION_CONNECTING_TIMED_OUT)
#endif
		{
			// We've to manually set RetVal_uv_write to UV__ECONNRESET here. This will result in calling after_send_response with STATUS = UV__ECONNRESET
			RetVal_uv_write = UV_ECONNRESET ;
		}

		if (RetVal_uv_write >= 0)
		{
			m_stServerStat.ResponsesBeingSent += NumberOfBuffers;
		}
		else
		{
			after_send_response_called_by_send_response = TRUE;
			pPeerServer->m_write_req.handle = (ConnStatus == CONNECTION_CONNECTED) ? pPeerServer->m_connection : NULL; // In case if GetServerConnection fails we don't get connection
			ConnectionsManager::after_send_responses(&pPeerServer->m_write_req, RetVal_uv_write);
			after_send_response_called_by_send_response = FALSE;
		}


		// We come here when there is no response left in server's queue or response written was successful.
		// In either case we must remove server from set. It will be added back to the set in after_send_response or in AddResponseToQueue
		pServersSet->erase(current); // By using direction flag, are erasing from different set than that is being added to by threads. So no need of m_rwlClientSetLock here.
	}

	return ;
}

void PeerServersManager::AfterSendingPeerServersResponses(stPeerServer* pPeerServer, Response* pResponse, int status)
{
	ADD2PROFILER;

	int ResponseLength = pResponse->GetResponse().len;

	ASSERT ((pResponse) && (pResponse->IsForward() == TRUE)); // We must receive here only forwarded responses

	BOOL bResponseForwardingSucceededLastTime = TRUE;

	// To avoid overlogging we decided to keep flag m_bResponseForwardingSucceededLastTime to keep track if server had succeeded last time
	// so that this time we could avoid redundant logging. However, this didn't work as many of the time we come here when pPeerServerInfo is NULL
	/*
	if (pPeerServerInfo) // We might get here as a result when GetServerConnection returned error leaving pPeerServerInfo NULL
	{
		bResponseForwardingSucceededLastTime = pPeerServerInfo->m_bResponseForwardingSucceededLastTime;
		pPeerServerInfo->m_bResponseForwardingSucceededLastTime = (status == WRITE_OK) ? TRUE : FALSE;
	}
	*/

	switch (status)
	{
		case WRITE_OK: // LIBUV calls after_send_response with 0 as status when send is successful
		{
			LOG (INFO, "Response forwarded to  %d.%d.%d.%d", pResponse->GetServersIPv4Address()[0], pResponse->GetServersIPv4Address()[1], pResponse->GetServersIPv4Address()[2], pResponse->GetServersIPv4Address()[3]);

			// Increase forwarded pResponse counter for the server, if the pResponse was to be forwarded
			IncreaseForwardedResponsesCount (pPeerServer);
	
			m_stServerStat.ResponsesForwarded ++;

			m_stServerStat.ResponsesSent ++; // Total responses sent for all clients
			m_stServerStat.TotalResponseBytesSent += ResponseLength;
			ASSERT(m_stServerStat.TotalResponseBytesSent > 0);
		}
		break;

		default:   // Some Error: Either uv_write returns negative values OR libuv encountered error after uv_write was zero (successful)
		{
			// According to this thread we MUST disconnect connection for which uv_write is not successful
			// https://groups.google.com/forum/#!topic/libuv/nJa3WeiVs2U

			m_stServerStat.ResponsesFailedToForward++; // Total responses failed to send for all clients

#ifndef NO_WRITE
			// Response forwarding failed so disconnect relevent server connection
			DisconnectServer(pPeerServer);
#endif
			// Get extended error information related to forward and lets log this error

			char strServerError[256]; 
			if (bResponseForwardingSucceededLastTime) 
				sprintf_s(strServerError, 256, "Unable to forward response to server %d.%d.%d.%d (Is called by SendResponse %d)", pResponse->GetServersIPv4Address()[0], pResponse->GetServersIPv4Address()[1], pResponse->GetServersIPv4Address()[2], pResponse->GetServersIPv4Address()[3], after_send_response_called_by_send_response);

			switch (pResponse->ForwardError)
			{
				case CONNECTION_CONNECTING_TIMED_OUT:
				{
					if (bResponseForwardingSucceededLastTime) strcat_s(strServerError, 256, "(CONNECTION_CONNECTING_TIMED_OUT)");
					m_stServerStat.ForwardErrorConnectingTimedout++;
					break;
				}

				case CONNECTION_OVERFLOWED:
				{
					if (bResponseForwardingSucceededLastTime) strcat_s(strServerError, 256, "(CONNECTION_OVERFLOWED)");
					m_stServerStat.ForwardErrorOverflowed++;       
					break;
				}

				case CONNECTION_DISCONNECTING:
				{
					if (bResponseForwardingSucceededLastTime) strcat_s(strServerError, 256, "(CONNECTION_DISCONNECTING)");
					m_stServerStat.ForwardErrorDisconnecting++;    
					break;
				}

				case CONNECTION_DISCONNECTED:
				{
					if (bResponseForwardingSucceededLastTime) strcat_s(strServerError, 256, "(CONNECTION_DISCONNECTED)");
					m_stServerStat.ForwardErrorDisconnected++;
					break;
				}

				default:
				{
					// In case of forwarded response, comes here when uv_write returns -1 and ForwardError == 0 or when libuv calls this function when write failed
					if (bResponseForwardingSucceededLastTime) sprintf_s(strServerError, 256, "(ERROR_WRITING_TO_SERVER) Code %d", status); 
					m_stServerStat.ForwardErrorWritingServer++;    
					break;
				}
				// NO MAN'S LAND. DO NOT WRITE ANY CODE HERE.
			}

			if (bResponseForwardingSucceededLastTime) LOG (ERROR, strServerError);

			break;
		}
		// NO MAN'S LAND. DO NOT WRITE ANY CODE HERE.
    }

	std::deque<class Response*> *pResponseQueueLocked, *pResponseQueueUnlocked;
	
	// uv_rwlock_rdlock(&m_rwlServersInfoLock); // So that we won't access map while server being added to it
	// pPeerServer->m_pResponseBeingForwarded = NULL;
	// uv_rwlock_rdunlock(&m_rwlServersInfoLock);

	// Add server to set
	std::set <stPeerServer*> *pServersSetUnlocked, *pServersSetLocked;

	pResponseQueueLocked = m_bResponseDirectionFlag ? pPeerServer->m_ResponsesQueue1 : pPeerServer->m_ResponsesQueue2 ;
	pResponseQueueUnlocked = m_bResponseDirectionFlag ? pPeerServer->m_ResponsesQueue2 : pPeerServer->m_ResponsesQueue1 ;
	pServersSetLocked = m_bResponseDirectionFlag ? &m_RecevingServersSet1 : &m_RecevingServersSet2;
	pServersSetUnlocked = m_bResponseDirectionFlag ? &m_RecevingServersSet2 : &m_RecevingServersSet1;

	if (pResponseQueueUnlocked->size() != 0)
	{
		AddToServerSet (pServersSetUnlocked, pPeerServer, FALSE);// pServersSetUnlocked->insert(TargetServerIPv4Address);
	}

	uv_rwlock_rdlock(&pPeerServer->m_rwlResponsesQueueLock);
	if (pResponseQueueLocked->size() != 0)
	{
		AddToServerSet (pServersSetLocked, pPeerServer, TRUE);// pServersSetLocked->insert(TargetServerIPv4Address);
	}
	uv_rwlock_rdunlock(&pPeerServer->m_rwlResponsesQueueLock);

	return;
}
