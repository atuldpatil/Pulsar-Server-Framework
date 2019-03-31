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
	Sample class definitation to demonstrate how request processing class can be derived from class RequestProcessor of Pulsar Server Framework.
*/

#include "Pulsar_SampleServer.h"
#include "RequestProcessor_v1.h"

std::map<UINT64, IPv4Address> RequestProcessor_v1::m_ClientsMap ;
typedef std::map<UINT64, IPv4Address>::iterator ClientsMapIterator;

Lock RequestProcessor_v1::m_rwlClientsMapLock;

/*
	Constructor of our defined processor class (to process our VERSION_1 protocol), derived from Pulsar Server Framework's RequestProcessor class
*/
RequestProcessor_v1::RequestProcessor_v1(USHORT version) : RequestProcessor(version, VersionParameters(MAX_REQUEST_SIZE, MAX_RESPONSE_SIZE))
{
	BOOL static bLocksInitiated = FALSE;

	// Remember constructor is called multiple times by framework's main thread.
	// So we must need to make sure lock is not initialized multiple times.
	if (!bLocksInitiated)
	{
		InitializeLock (m_rwlClientsMapLock);
		bLocksInitiated = TRUE;
	}

	m_Version = version;
	RequestsCount = 0;

	CommonParameters commonparams;
	commonparams.KeepAliveFrequencyInSeconds = KEEP_ALIVE_IN_SECONDS;
	commonparams.MaxPendingResponses = MAX_PENDING_RESPONSES_PER_CLIENT;
	commonparams.MaxRequestProcessingThreads = NUMBER_OF_REQUESTPROCESSING_THREADS;
	commonparams.StatusUpdateFrequencyInSeconds = STATUS_INTERVAL_IN_SECONDS;

	// Note persists in server status updates. Unlike INFO it doesn't vanish.
	LOG (NOTE, "Request processing threads: %d", commonparams.MaxRequestProcessingThreads);

	// Let's set common server parameters. Please note this is single time call. 
	// We don't need to call again (even from constructors of other version processor classes)
	SetCommonParameters(commonparams);

	return;
}

/* 
	Destructor of derived processor
*/
RequestProcessor_v1::~RequestProcessor_v1() 
{
	// In desructor, we only need to destroy lock which we have initialized in the constructor
	DestroyLock(m_rwlClientsMapLock);
}

/* 
	Framework calls this when client send server a request 
*/
BOOL RequestProcessor_v1::ProcessRequest ()
{
	/*
		Framework strips off master protocol and we get here buffer as it was send us by client.
		As per our demo protocol first byte in the buffer would be request code
	*/
	const Buffer req_buffer = GetRequest();
	char RequestCode = req_buffer.base[0] ;

	int RetVal = TRUE;
	switch(RequestCode)
	{
		case REGISTER:
			RetVal = ProcessRegister();
			break;

		case ECHO:
			ProcessEcho();
			break;

		default:
			// ERROR persists in server status updates. Unlike INNFO it doesn't vanish.
			LOG (ERROR,  "Unknown request received.");
			break;
	}

	return RetVal;
}

int RequestProcessor_v1::ProcessRegister()
{
	BOOL bException = FALSE;

	// In Register request we just store client handle in our map and respond client that it is registered
	ClientHandle clienthandle = GetRequestSendingClientsHandle();

	try
	{
		// Since ProcessRequest() runs in threads and therefore m_ClientsMap could be accessed by multiple threads processing same request at a time, 
		// we need to acquire lock. 
		// As when registering client we will be adding element to map we need here write lock.
		AcquireWriteLock(m_rwlClientsMapLock);	

		m_ClientsMap[clienthandle.m_ClientRegistrationNumber] = clienthandle.m_ServerIPv4Address;

		ReleaseWriteLock(m_rwlClientsMapLock);	
	}
	catch(std::bad_alloc&)
	{
		LOG(EXCEPTION, "Memory exception occurred");
		// In case of any severe error or exception let's disconnect the client
		DisconnectClient(&clienthandle);
		bException = TRUE;
	}

	if (bException) 
		return FALSE;

	// After successful registration let's respond to client accordingly.
	// As per our protocol, first byte is RESPONSE code. So let's here modify the request itself (where we've request code: REGISTER), as rest all is same.
	GetRequest().base[0] = REGISTERED;
	SendResponse (&clienthandle, &GetRequest());

	return TRUE;
}

void RequestProcessor_v1::ProcessEcho()
{
	/*
		In processing echo, we'll send same buffer (as received from client) to all connected clients.
	*/

	const Buffer request_buffer = GetRequest();

	static int EchoCount=0;

	request_buffer.base[0] = ECHOED; // As per our protocol, first byte of buffer is code: ECHOED. Rest all is same as request.
	Buffer response_buffer = request_buffer;

	SendToAllClients(response_buffer);

	// INFO appears only for single time in server status updates. Unlike NOTE or ERROR it doesn't persist.
	LOG (INFO, "Echo request processed (Requests count %d)", ++RequestsCount);
}

void RequestProcessor_v1:: SendToAllClients(Buffer response_buffer)
{
	/*
		In this function we will just iterate through all the connected clients and send the response buffer to each one of them one by one
	*/
	int RetVal = FALSE;
	
	ClientHandles clienthandles;

	// As we are aware ProcessRequest is running in multiple threads. And we have to access static m_ClientsMap here so let's acquire read locks.
	AcquireReadLock(m_rwlClientsMapLock);	

	for(ClientsMapIterator iterator = m_ClientsMap.begin(); iterator != m_ClientsMap.end(); iterator++) 
	{
		UINT64 ReceivingClientRegistrationNumber = iterator->first;
		IPv4Address ServerIPv4Address = iterator->second;

		ClientHandle ReceivingClientHandle;
		ReceivingClientHandle.m_ClientRegistrationNumber = ReceivingClientRegistrationNumber;
		ReceivingClientHandle.m_ServerIPv4Address = ServerIPv4Address; 

		// Uncomment the code below if we don't want to send response back to client sending request
		/*
		if (ReceivingClientHandle == GetRequestSendingClientsHandle())
			 continue ;
		*/

		try
		{
			clienthandles.insert(ReceivingClientHandle); 
		}
		catch(std::bad_alloc&)
		{
			LOG(MEMORY_ALLOCATION_EXCEPTION, "Memory exception occurred");
		}
	}

	ReleaseReadLock(m_rwlClientsMapLock);	

	// As we going to send same buffer to multiple clients, we will use StoreMulticast
	SendResponse (&clienthandles, &response_buffer);

	return;
}

/* 
	To process when client disconnects
*/
void RequestProcessor_v1::ProcessDisconnection (ClientHandle& clienthandle, void* pSessionData)
{
	// Just like in register client we client added to map, here we will remove it
	// Similar to what we did there, here too we need write lock
	AcquireWriteLock(m_rwlClientsMapLock);	
	m_ClientsMap.erase (clienthandle.m_ClientRegistrationNumber);
	ReleaseWriteLock(m_rwlClientsMapLock);	

	return ;
}

/* 
	We have class RequestProcessor_v1 to process VERSION_1 protocol requests. As per Pulsar Server Framework's guidelines we need to create global instance of the class.
*/
RequestProcessor_v1 theRequestProcessor_v1_GlobalInstance (VERSION_1);