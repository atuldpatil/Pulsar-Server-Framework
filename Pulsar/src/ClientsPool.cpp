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

ClientsPool::ClientsPool() 
{ 
	uv_rwlock_init(&m_ClientsMapLock);

	m_bIsServerShuttingDown=FALSE;
}

ClientsPool::~ClientsPool() 
{ 
	uv_rwlock_destroy(&m_ClientsMapLock);
}

void ClientsPool::SetServerShuttingDown() 
{ 
	m_bIsServerShuttingDown = TRUE; 
} // Called only thru event loop after it reads keystrokes to shutdown

BOOL ClientsPool::IsShutdownInitiated() 
{
	return m_bIsServerShuttingDown;
}

// Called through event loop (on_new_connection). Adds client to pool, returns TRUE/FALSE
BOOL ClientsPool::AddClient(stClient* pClient)
{
	BOOL bAdded = TRUE;
	UINT64 ClientRegistrationNumber = pClient->m_ClientHandle.m_ClientRegistrationNumber;

	if (m_bIsServerShuttingDown)
		return FALSE;

	uv_rwlock_wrlock(&m_ClientsMapLock);
	try
	{
		m_ClientsMap[ClientRegistrationNumber] = pClient;
	}
	catch(std::bad_alloc&)
	{
		bAdded = FALSE;
	}
	uv_rwlock_wrunlock(&m_ClientsMapLock);

	return bAdded;
}

// Being called via event loop only (Through DisconnectAndDelete).
// Makes client value NULL in array only if it's requests count is zero and returns TRUE. Else, returns FALSE.
// Array structure only will be changed through AddClient which runs in event loop itself. So we no need to acquire Array lock here.
BOOL ClientsPool::RemoveClient(stClient* pClient)
{
	BOOL RetVal = FALSE;
	UINT64 ClientRegistrationNumber = pClient->m_ClientHandle.m_ClientRegistrationNumber;
	
	uv_rwlock_wrlock(&m_ClientsMapLock);
	if (m_ClientsMap.find(ClientRegistrationNumber) != m_ClientsMap.end())
	{
		ASSERT (m_ClientsMap[ClientRegistrationNumber] == pClient);

		// uv_rwlock_wrlock(&pClient->m_LockRequestsResponses.rwLock);  // We don't need this as we getting exclussive access on map itself (incrementor/decrementors need read access to it)
		if ((pClient->m_LockRequestsResponses.Requests == 0) && (pClient->m_LockRequestsResponses.Responses == 0))
		{
			m_ClientsMap.erase (ClientRegistrationNumber);
			RetVal = TRUE;
		}
		// uv_rwlock_wrunlock(&pClient->m_LockRequestsResponses.rwLock);  
	} 
	uv_rwlock_wrunlock(&m_ClientsMapLock);

	return RetVal;
}

unsigned int ClientsPool::GetClientsCount() 
{
	uv_rwlock_rdlock(&m_ClientsMapLock); // So that AddClient cannot resize array, but other threads can still access to read elements
	unsigned int Count = (int)m_ClientsMap.size(); 
	uv_rwlock_rdunlock(&m_ClientsMapLock); // So that AddClient cannot resize array, but other threads can still access to read elements

	return Count;
}

void ClientsPool::GetClients(Clients& vClients) 
{
	typedef std::map<UINT64, stClient*>::iterator it_type;
	it_type iterator_end =  m_ClientsMap.end();
	uv_rwlock_rdlock(&m_ClientsMapLock); // So that AddClient cannot resize array, but other threads can still access to read elements
	for(it_type iterator = m_ClientsMap.begin(); iterator != iterator_end; iterator++) 
	{
		// std::map<UINT64, stClient*> m_ClientsMap;
		UINT64 ClientRegistrationNumber = iterator->first;
		stClient* p_stClient = iterator->second;

		ASSERT(p_stClient);

		try
		{
			vClients.push_back(p_stClient);
		}
		catch(std::bad_alloc&)
		{
			p_stClient->GetConnectionsManager()->IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
		}
	}
	uv_rwlock_rdunlock(&m_ClientsMapLock); // So that AddClient cannot resize array, but other threads can still access to read elements

	return;
}

// Called by send_keepalive_thread (Adds stClient* to the vector if it is NOT NULL and has no pending requests/responses 
// also no activity detected for last CommonParams.KeepAliveFrequencyInSeconds period.)
void ClientsPool::GetIdleClients(ClientHandles& vClientHandles, ClientType paramType) 
{
	typedef std::map<UINT64, stClient*>::iterator it_type;
	it_type iterator_end =  m_ClientsMap.end();
	uv_rwlock_rdlock(&m_ClientsMapLock); // So that AddClient cannot resize array, but other threads can still access to read elements
	for(it_type iterator = m_ClientsMap.begin(); iterator != iterator_end; iterator++) 
	{
		// std::map<UINT64, stClient*> m_ClientsMap;
		UINT64 ClientRegistrationNumber = iterator->first;
		stClient* p_stClient = iterator->second;

		ASSERT(p_stClient);

		uv_rwlock_rdlock(&p_stClient->m_LockRequestsResponses.rwLock); // No other thread should try to change requests/responses/lastactivitytime for the _same_ client
		int RequestsResponses = p_stClient->m_LockRequestsResponses.Requests + p_stClient->m_LockRequestsResponses.Responses; 
		time_t CurrentTime;
		CurrentTime = time (NULL);

		if ((RequestsResponses == 0) && (RequestProcessor::GetCommonParameters().KeepAliveFrequencyInSeconds <= (CurrentTime - p_stClient->m_LockRequestsResponses.LastActivityTime)))
		{
			try
			{
				ClientType clientType = (p_stClient->GetVersion() == UNINITIALIZED_VERSION) ? VersionlessClient : VersionedClient;

				if (clientType == paramType)
				{
					vClientHandles.insert(p_stClient->GetClientHandle());
					p_stClient->m_LockRequestsResponses.LastActivityTime = CurrentTime;
				}
			}
			catch(std::bad_alloc&)
			{
				p_stClient->GetConnectionsManager()->IncreaseExceptionCount(MEMORY_ALLOCATION_EXCEPTION, __FILE__, __LINE__);
			}
		}
		uv_rwlock_rdunlock(&p_stClient->m_LockRequestsResponses.rwLock); 

	}
	uv_rwlock_rdunlock(&m_ClientsMapLock); // So that AddClient cannot resize array, but other threads can still access to read elements

	return;
}

// If client wasn't found or was set for deletion, return FALSE. Else increases its requests count and returns TRUE also returns associated pClient.
// This function is most likely to be called from threads. So need locks.
int ClientsPool::IncreaseCountForClient (ClientHandle* clienthandle, stClient* &pClient, BOOL RequestORResponse)
{
	int RetVal = FALSE ;

	ASSERT ((RequestORResponse == REQUESTCOUNT) || (RequestORResponse == RESPONSECOUNT));

	uv_rwlock_rdlock(&m_ClientsMapLock); // So that AddClient cannot resize map, but other threads can still access to read elements
	if ((clienthandle) && (m_ClientsMap.find(clienthandle->m_ClientRegistrationNumber) != m_ClientsMap.end()))
	{
		pClient = m_ClientsMap[clienthandle->m_ClientRegistrationNumber]; // m_ClientLockRequestsResponsesArray[clienthandle->m_ClientIndex].pClient;

		ASSERT (pClient);

		if ((pClient->GetClientHandle().m_ClientRegistrationNumber == clienthandle->m_ClientRegistrationNumber) && (pClient->IsMarkedToDisconnect() == FALSE)) 
		{
			ASSERT (pClient->GetClientHandle().m_ServerIPv4Address == clienthandle->m_ServerIPv4Address); // Getting increase request for different server is serious flaw

			uv_rwlock_wrlock(&pClient->m_LockRequestsResponses.rwLock); // No other thread should try to change client/id/requests for the _same_ client

			if (RequestORResponse == REQUESTCOUNT)
				pClient->m_LockRequestsResponses.Requests++; 
			else
				pClient->m_LockRequestsResponses.Responses++; 

			pClient->m_LockRequestsResponses.LastActivityTime = time(NULL); 

			uv_rwlock_wrunlock(&pClient->m_LockRequestsResponses.rwLock);

			RetVal = TRUE;
		}
	}
	else
	{
		if (!clienthandle)
			LOG (ERROR, "Invalid clienthandle pointer (NULL) in IncreaseCountForClient for %s", (RequestORResponse == REQUESTCOUNT) ? "REQUESTCOUNT" : "RESPONSECOUNT");
		/*
		else // This could happen when request/response intended for a client is already disconnected
			LOG (ERROR, "ClientRegistrationNumber is not present in m_ClientsMap in IncreaseCountForClient for %s", (RequestORResponse == REQUESTCOUNT) ? "REQUESTCOUNT" : "RESPONSECOUNT");
		*/
	}
	uv_rwlock_rdunlock(&m_ClientsMapLock);

	return RetVal;
}

// This function is likely to be called from threads. So need locks.
int ClientsPool::DecreaseCountForClient (stClient* pClient, BOOL RequestORResponse)
{
	int RetVal = FALSE;

	uv_rwlock_rdlock(&m_ClientsMapLock); // So that AddClient cannot resize array, but other threads can still access to read elements
	
	ASSERT (pClient);

	ASSERT ((RequestORResponse == REQUESTCOUNT) || (RequestORResponse == RESPONSECOUNT));

	uv_rwlock_wrlock(&pClient->m_LockRequestsResponses.rwLock);

	if (RequestORResponse == REQUESTCOUNT)
		pClient->m_LockRequestsResponses.Requests--;
	else
		pClient->m_LockRequestsResponses.Responses--;

	pClient->m_LockRequestsResponses.LastActivityTime = time(NULL); 

	RetVal = TRUE;

	ASSERT (pClient->m_LockRequestsResponses.Requests >= 0); // Imbalance in increase and decrease requests is serious logical flaw
	ASSERT (pClient->m_LockRequestsResponses.Responses >= 0); // Imbalance in increase and decrease responses is serious logical flaw
		
	uv_rwlock_wrunlock(&pClient->m_LockRequestsResponses.rwLock);

	uv_rwlock_rdunlock(&m_ClientsMapLock);

	return RetVal;
}
