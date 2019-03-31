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

Request::Request(uv_buf_t* request, double ArrivalTime, ClientsPool* pClientsPool, ClientHandle* pClientHandle)
{
	ASSERT(pClientHandle);

	// Put time stamp
	m_ArrivalTime = ArrivalTime; 

	m_work_t.data = this;
	m_NextRequest = NULL;
	m_bHasEncounteredMemoryAllocationException = FALSE;

	// Store request
	ASSERT ((request) && (request->base) && (request->len));

	m_Request = (*request);

	// IncreaseRequestCountForClient in CL pool. This ensures event loop to not to delete stClient while request processing is still in progress in thread.
	// Note: As in opposite DecreaseRequestResponseCountForClient has deliberately not been called from d'tor.
	// This is because we delete Request object in thread (for efficient use of memory) and 
	// call DecreaseRequestResponseCountForClient from after_request_processing_thread (for the reason given there itself)
	if (pClientsPool->IncreaseCountForClient(pClientHandle, m_Client, REQUESTCOUNT) == FALSE)
	{
		throw RequestCreationException(); // May be client is marked for deletion. We should take care of that.
	}

	m_bIsDeferred = FALSE;

	// ASSERT (m_Client == pClient ); // Object got from the handle obtained from pClient must be equal to pClient
}

void Request::DeferProcessing(BOOL bFlag)
{
	m_bIsDeferred = bFlag ? TRUE : FALSE ;
}

BOOL Request::IsDeferred()
{
	return m_bIsDeferred;
}

void Request::SetMemoryAllocationExceptionFlag()
{
	m_bHasEncounteredMemoryAllocationException = TRUE;
}

BOOL Request::GetMemoryAllocationExceptionFlag()
{
	return m_bHasEncounteredMemoryAllocationException;
}

stClient* Request::GetClient() 
{ 
	return m_Client; // Get stClient associated with this request
} 

const uv_buf_t Request::GetRequest() 
{ 
	return m_Request; 
}

double Request::GetArrivalTime() 
{ 
	return m_ArrivalTime; 
}

Request::~Request()
{
}

Response::Response(const uv_buf_t* response, ClientHandlesPtrsIterator& StartIt, ClientHandlesPtrsIterator& EndIt, USHORT version /* Version of client who is creating/storing the Response */, BOOL bIsUpdate, RequestProcessor* pRequestProcessor, double RequestArrivalTime, ConnectionsManager* pConnectionsManager)
{
	// Verify that there are handles and that the server in all handles is same
	ASSERT (StartIt != EndIt);

	 for (ClientHandlesPtrsIterator It=StartIt; It!=EndIt; ++It)
		ASSERT ((*It)->m_ServerIPv4Address == (*StartIt)->m_ServerIPv4Address);

	Initialize();

	m_pRequestProcessor = pRequestProcessor;

	m_RequestArrivalTime = RequestArrivalTime ;

	m_StartIt = StartIt;
	m_EndIt = EndIt;

	// m_pClientHandles = clienthandle_ptrs;
	// m_pLocalClientsManager = pLocalClientsManager;
	m_pConnectionsManager = pConnectionsManager;

	// Store server IP address to which this response belongs
	m_ServerIPv4Address = (*StartIt)->m_ServerIPv4Address;

	m_bIsUpdate = bIsUpdate;

	m_NumberOfHandles = (unsigned int) std::distance(StartIt, EndIt);

	m_bIsMulticast = (m_NumberOfHandles == 1) ? FALSE : TRUE;

	// Construct response. It essentially adds header to the response before copying to m_Response.
	// It doesn't make any sense to add header to NULL or zero-length response. If at all we send 'only header' response, it will be of no use to client.
	// Also, in case if this is 'to be forwarded' type of response, it will be received as request by other server and on_read rejects such 'only header' requests.
	// Hence, m_Response remains NULL and zero-lengthed in such case, which uv_write doesn't fail on, but at the same time it doesn't send also anything across the wire.
	if ((response == NULL) || (response->base == NULL) || (response->len == 0)) 
	{
		throw ResponseCreationException();
	}

	if (m_pConnectionsManager->GetIPAddressOfLocalServer() == m_ServerIPv4Address) // Response was for clients connected to this server
	{
		ConstructResponseForLocalClients(response, m_NumberOfHandles, version);
	}
	else
	{
		// MAX_HANDLES_IN_FORWARDED_RESPONSE is used to derive "maximum length of forwarded message" while validating incoming forward length.
		// It is also used to estimate memory consumption when max forwarded responses have been queued up (in servers queues).
		// Hence, this is actually a number that represents maximum multicasts being forwarded to a single server.
		// That's why we validate number of handles here just before creating response for remote client (and not in SendMulticast).
		// (This limitation is not applicable for local clients as KEEP_ALIVE and FATAL_ERROR are sent by send_keepalive_thread to limitless number of local clients)
		if (m_NumberOfHandles > MAX_HANDLES_IN_FORWARDED_RESPONSE)
		{
			LOG (ERROR, "Cannot create multicast response which is to be forwarded. Handles exceed max limit (which imposes forwarded response size limit). Please consider multicasting in batches with max handles MAX_HANDLES_IN_FORWARDED_RESPONSE (%d) in each.", MAX_HANDLES_IN_FORWARDED_RESPONSE); 
			throw ResponseCreationException();
		}

		ConstructResponseForRemoteClients(response, version);
	}

	// In craete response we verify response length etc. so lets find out response type here
	switch(version)
	{
		case SPECIAL_COMMUNICATION:
		{
			m_ResponseType = response->base[0]; // m_ResponseType would be either RESPONSE_KEEP_ALIVE, RESPONSE_ERROR, RESPONSE_ACKNOWLEDGEMENT_OF_FORWARDED_RESP, RESPONSE_FATAL_ERROR
			break;
		} 

		default:
		{
			m_ResponseType = RESPONSE_ORDINARY; 
			break;
		}
	}
}

// When response is for client(s) connected to this server, we use this constructor. This helps constructing same response intended for multiple clients (e.g. chatrooms. multicast messages etc.)
void Response::ConstructResponseForLocalClients(const uv_buf_t* response, int NumberOfClients, USHORT version) // : bIsResponseWrittenToClient((*pClients).size(), FALSE), m_write_req((*pClients).size(), write_t)
{
	// Initially plate should be clean
	ASSERT (m_Response.base == NULL);

	// At least one client has to be there
	ASSERT (NumberOfClients > 0);

	// If its response to client connected to this server:
	// preamble | version (SenderClientVersion) | size of response | response
		
	m_Response.len = response->len + HEADER_SIZE;

	VersionParameters* pVersionParams = m_pConnectionsManager->GetVersionParameters(version);

	if (!pVersionParams)
	{
		// Are we storing response on behalf of client which has a version that doesn't have request processor?
		LOG (ERROR, "Cannot create response. Version processor is not available to get version parameters."); 
		throw ResponseCreationException();
	}

	UINT MaxResponseSize = pVersionParams->m_MaxResponseSize;  
		
	if ((m_Response.len-HEADER_SIZE) > MaxResponseSize) // Response length (excluding header size) shouldn't exceed MaxResponseSize 
	{
		LOG (ERROR, "Cannot create response. Response is too long."); 
		throw ResponseCreationException();
	}

	m_Response.base = new char [m_Response.len]; // Will be freed in destructor

	base.reset (m_Response.base); // This will be automatically destructed in case of exception or response deletion

	// Convert version and length to network byte order
	USHORT version_n = htons(version);
	UINT len_n = (UINT) htonl (response->len);

	// First put preamble
	memcpy_s (m_Response.base, m_Response.len, MSG_PREAMBLE, PREAMBLE_BYTES);

	// Then version
	memcpy_s (&m_Response.base[PREAMBLE_BYTES], m_Response.len-PREAMBLE_BYTES, (UCHAR*)&version_n, VERSION_BYTES);

	// Then  size
	memcpy_s (&m_Response.base[PREAMBLE_BYTES+VERSION_BYTES], m_Response.len-(PREAMBLE_BYTES+VERSION_BYTES), (UCHAR*)&len_n, SIZE_BYTES);

	// Finally copy the response
	memcpy_s (&m_Response.base[HEADER_SIZE], m_Response.len-HEADER_SIZE, response->base, response->len);

	return;
}

// When response is for client(s) connected to other server, we use this constructor. 
void Response::ConstructResponseForRemoteClients(const uv_buf_t* response, USHORT version /* Version of client who is creating/storing the Response */)  // : bIsResponseWrittenToClient(1, FALSE), m_write_req(1, write_t)
{
	m_bIsForward = TRUE;

	// Initially plate should be clean
	ASSERT (m_Response.base == NULL);

	// If its response (to be forwarded) to client connected to other server:
	// preamble | version (FFFF) | size (of response+additional fields) | version (SenderClientVersion) | number of handles | handles | response

	// So additional fields (other than standard HEADER_SIZE for local response) are: 
	//						version (SenderClientVersion)	|	number of handles	|	handles
	// Size:						short 2 bytes			+     int 4 bytes		+ (number of handles * 8 bytes m_ClientRegistrationNumber)

	// First let's store handles in an array

	unsigned int HandlesArraySize = m_NumberOfHandles * sizeof(ClientHandle().m_ClientRegistrationNumber);

	std::unique_ptr <unsigned char[]> Handles (new unsigned char [HandlesArraySize]); 

	unsigned int j=0;
	// for (unsigned int i=0, j=0; i<NumberOfHandles; i++)
	for (ClientHandlesPtrsIterator It = m_StartIt; It != m_EndIt; ++It)
	{
		UINT64 ClientRegistrationNumber = (*It)->m_ClientRegistrationNumber;
		UINT64 ClientRegistrationNumber_n = htonll (ClientRegistrationNumber);
		memcpy_s (&Handles.get()[j], HandlesArraySize-j, &ClientRegistrationNumber_n, sizeof(UINT64));
		j += sizeof(UINT64);
	}

	unsigned int AdditionalFieldsSize = VERSION_BYTES /* 2 bytes version (SenderClientVersion) */  + HANDLE_BYTES /* 4 bytes to store number of handles */ + HandlesArraySize;
	ULONG ResponseLengthWithAdditionalFields = response->len+AdditionalFieldsSize;

	m_Response.len = response->len + HEADER_SIZE + AdditionalFieldsSize;

	VersionParameters* pVersionParams = m_pConnectionsManager->GetVersionParameters(SPECIAL_COMMUNICATION);

	if (!pVersionParams)
	{
		LOG (ERROR, "Cannot create forwardable response. Version processor is not available to get version parameters for response being forwarded."); 
		throw ResponseCreationException();
	}

	UINT MaxResponseSize = pVersionParams->m_MaxResponseSize;  
		
	if ((m_Response.len-HEADER_SIZE) > MaxResponseSize) // Response length (excluding header size) shouldn't exceed MaxResponseSize 
	{
		LOG (ERROR, "Cannot create forwardable response. Response is too long."); 
		throw ResponseCreationException();
	}

	m_Response.base = new char [m_Response.len]; // Will be freed in destructor

	base.reset (m_Response.base); // This will be automatically destructed in case of exception or response deletion

	// Convert version, length and number of handles to network byte order
	USHORT ForwardResponseVersion_n = htons (SPECIAL_COMMUNICATION);
	UINT ResponseLengthWithAdditionalFields_n = (UINT) htonl (ResponseLengthWithAdditionalFields);
	USHORT version_n = htons (version);
	UINT NumberOfHandles_n = (UINT) htonl (m_NumberOfHandles);

	// First put preamble
	memcpy_s (m_Response.base, m_Response.len, MSG_PREAMBLE, PREAMBLE_BYTES);

	// Then version ('FFFF' indicating it's forwarded message)
	memcpy_s (&m_Response.base[PREAMBLE_BYTES], m_Response.len-PREAMBLE_BYTES, (UCHAR*)&ForwardResponseVersion_n, VERSION_BYTES);

	// Then  size (of response+additional fields)
	memcpy_s (&m_Response.base[PREAMBLE_BYTES+VERSION_BYTES], m_Response.len-(PREAMBLE_BYTES+VERSION_BYTES), (UCHAR*)&ResponseLengthWithAdditionalFields_n, SIZE_BYTES);

	// Then  version (SenderClientVersion)
	memcpy_s (&m_Response.base[PREAMBLE_BYTES+VERSION_BYTES+SIZE_BYTES], m_Response.len-(PREAMBLE_BYTES+VERSION_BYTES+SIZE_BYTES), (UCHAR*)&version_n, VERSION_BYTES);

	// Then  number of handles
	memcpy_s (&m_Response.base[PREAMBLE_BYTES+VERSION_BYTES+SIZE_BYTES+VERSION_BYTES], m_Response.len-(PREAMBLE_BYTES+VERSION_BYTES+SIZE_BYTES+VERSION_BYTES), (UCHAR*)&NumberOfHandles_n, HANDLE_BYTES);

	// Then handles
	memcpy_s (&m_Response.base[PREAMBLE_BYTES+VERSION_BYTES+SIZE_BYTES+VERSION_BYTES+HANDLE_BYTES], m_Response.len-(PREAMBLE_BYTES+VERSION_BYTES+SIZE_BYTES+VERSION_BYTES+HANDLE_BYTES), (UCHAR*)Handles.get(), HandlesArraySize);

	// Finally copy the response
	memcpy_s (&m_Response.base[PREAMBLE_BYTES+VERSION_BYTES+SIZE_BYTES+VERSION_BYTES+HANDLE_BYTES+HandlesArraySize], m_Response.len-(PREAMBLE_BYTES+VERSION_BYTES+SIZE_BYTES+VERSION_BYTES+HANDLE_BYTES+HandlesArraySize), response->base, response->len);

	return;
}

void Response::Initialize()
{
	// Initialize response to null
	m_Response.base = NULL ;
	m_Response.len = NULL ;

	// Initialize other variables
	m_ReferenceCount = 0 ;
	ForwardError = 0 ; // Enables SendResponses to set error codes related to forward response (to other server)
	bAddedToStat = FALSE ; 
	ResponseSentCount = 0;

	QueuedTime = 0;

	m_bIsForward = FALSE;
	m_bIsMulticast = FALSE;
	m_bIsUpdate = FALSE;
}

// Called by ConnectionsManager::AddResponseToQueues to indicate how many local clients this response was added to
void Response::SetReferenceCount(int ReferenceCount)
{
	m_ReferenceCount = ReferenceCount;
}

int Response::GetReferenceCount()
{
	return m_ReferenceCount;
}

RequestProcessor* Response::GetRequestProcessor()
{
	return m_pRequestProcessor;
}

Response::~Response()
{
}

BOOL Response::IsFatalErrorForLocallyConnectedClient()
{
	if ((IsForward() == FALSE) && (m_ResponseType == RESPONSE_FATAL_ERROR))
		return TRUE ;

	return FALSE ;
}

// Each response is associated with a server (local or remote)
IPv4Address Response::GetServersIPv4Address()
{ 
	return m_ServerIPv4Address;
}

double Response::GetRequestArrivalTime() 
{ 
	return m_RequestArrivalTime; 
}

int Response::GetResponseType() 
{ 
	return m_ResponseType; 
} 

const uv_buf_t Response::GetResponse() 
{ 
	return m_Response ;  // Returns copy of uv_buf_t structure containing response
}

BOOL Response::IsMulticast()
{
	return m_bIsMulticast;
}

BOOL Response::IsForward()
{
	return m_bIsForward;
}

BOOL Response::IsUpdate()
{
	return m_bIsUpdate;
}

ConnectionsManager* Response::GetConnectionsManager()
{
	return m_pConnectionsManager;
}

