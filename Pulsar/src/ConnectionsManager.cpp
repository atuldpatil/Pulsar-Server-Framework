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

ConnectionsManager::ConnectionsManager()
{
	m_bResponseDirectionFlag = FALSE;

	// LOG (INFO, CONNECTIONSMANAGER, "Initializing memory exception counter lock");
	int retval = uv_rwlock_init(&m_rwlMemoryAllocationErrorCounter);
	ASSERT_THROW ((retval >= 0), "Initializing memory exception counter lock failed");

	// LOG (INFO, "Initializing responses direcion flag lock");
	retval = uv_rwlock_init(&m_rwlResponseDirectionFlagLock);
	ASSERT_THROW ((retval >= 0), "Initializing responses direction flag lock failed");

	// LOG (INFO, "Initializing responses counters locks");
	retval = uv_rwlock_init(&m_rwlResponseCountersLock1);
	ASSERT_THROW ((retval >= 0), "Initializing responses counters lock1 failed");
	retval = uv_rwlock_init(&m_rwlResponseCountersLock2);
	ASSERT_THROW ((retval >= 0), "Initializing responses counters lock2 failed");

	m_bIsServerShutDown = TRUE;
	
	// Finally let's just call this once first time through event loop so it will initialize its static structures so that next calls will be thread safe.
	GetHighPrecesionTime();
}

ConnectionsManager::~ConnectionsManager()
{
	ASSERT(m_bIsServerShutDown); // Let's not allow to destruct when server has not shut down

	// LOG (INFO, "Destroying memory allocation error counter lock");
	uv_rwlock_destroy(&m_rwlMemoryAllocationErrorCounter);

	// LOG (INFO, "Destroying responses direction flag lock");
	uv_rwlock_destroy(&m_rwlResponseDirectionFlagLock);

	// LOG (INFO, "Destroying responses counters locks");
	uv_rwlock_destroy(&m_rwlResponseCountersLock1);
	uv_rwlock_destroy(&m_rwlResponseCountersLock2);
}

const char* ConnectionsManager::GetErrorDescription(int errorcode)
{
	return uv_strerror(errorcode);
}
// Returns high precesion time in seconds
double ConnectionsManager::GetHighPrecesionTime()
{
	LARGE_INTEGER li;
	static BOOL QueryPerformanceFrequencyFailed = FALSE;
	static LONGLONG Freq = 0;

	if ((!Freq) && (!QueryPerformanceFrequencyFailed))
	{
		if(!QueryPerformanceFrequency(&li))
		{
			QueryPerformanceFrequencyFailed = TRUE;
			LOG (NOTE, "QueryPerformanceFrequency failed. Response time will not be available."); 
			return NULL;
		}
		Freq = li.QuadPart;
	}

	if (QueryPerformanceFrequencyFailed)
		return NULL;

	QueryPerformanceCounter(&li);

	double TimeElapsed = ((double)li.QuadPart/Freq);

	return TimeElapsed;
}

long long  ConnectionsManager::GetProcessPrivateBytes() 
{
	long long privatebytes;

	HANDLE current_process;
	PROCESS_MEMORY_COUNTERS_EX pmc;

	current_process = GetCurrentProcess();

	if (!GetProcessMemoryInfo(current_process, (PPROCESS_MEMORY_COUNTERS)&pmc, sizeof(pmc))) 
	{
		return -1;
	}

	privatebytes = pmc.PrivateUsage;

	return privatebytes ;
}

void ConnectionsManager::IncreaseExceptionCount(BOOL bType, char* filename, int linenumber) 
{
	uv_rwlock_wrlock(&m_rwlMemoryAllocationErrorCounter);

	char* strType;

	switch(bType)
	{
		case MEMORY_ALLOCATION_EXCEPTION:
			strType = "MEMORY_ALLOCATION_EXCEPTION";
			m_stServerStat.MemoryAllocationExceptionCount ++; 
			break;

		case CLIENT_CREATION_EXCEPTION:
			strType = "CLIENT_CREATION_EXCEPTION";
			m_stServerStat.ClientCreationExceptionCount ++; 
			break;

		case CONNECTION_CREATION_EXCEPTION:
			strType = "CONNECTION_CREATION_EXCEPTION";
			m_stServerStat.ConnectionCreationExceptionCount ++; 
			break;

		case REQUST_CREATION_EXCEPTION:
			strType = "REQUST_CREATION_EXCEPTION";
			m_stServerStat.RequestCreationExceptionCount ++; 
			break;

		case RESPONSE_CREATION_EXCEPTION:
			strType = "RESPONSE_CREATION_EXCEPTION";
			m_stServerStat.ResponseCreationExceptionCount ++; 
			break;

		default:
			ASSERT (0); // Unknown exception type
	}

	LOG (EXCEPTION, "%s at: %s(%d)", strType, filename, linenumber);

	uv_rwlock_wrunlock(&m_rwlMemoryAllocationErrorCounter);
} 

// This function runs in threads. Called by StoreMessage after it creates Response object.
int ConnectionsManager::AddResponseToQueues(Response* pResponse, ClientHandlesPtrs* pClientHandlePtrs, BOOL& bHasEncounteredMemoryAllocationException)
{
	int ResponseReferenceCount = 0;

	uv_rwlock_rdlock(&m_rwlResponseDirectionFlagLock);

	// Add this response to responses list of all its intended clients
	// A response has multiple handles and intended for single server (so that it can be forwarded to that server)...
	if (pResponse->IsForward())
	{
		// ... Thus, in case when server is peer server we add response to queue of that single server, and ...
		ResponseReferenceCount += PeerServersManager::AddResponseToQueue(pResponse, bHasEncounteredMemoryAllocationException);
	}
	else
	{
		// ... In case when the server is local server we have to add the response to queues of multiple clients.
		ResponseReferenceCount += LocalClientsManager::AddResponseToClientsQueues(pResponse, pClientHandlePtrs, bHasEncounteredMemoryAllocationException);
	}

	pResponse->SetReferenceCount(ResponseReferenceCount);  

	if (ResponseReferenceCount == 0)
	{
		DEL (pResponse);
	}
	else
	{
		pResponse->GetRequestProcessor()->IncreaseResponseObjectsQueuedCounter(); 
	}

	// Also add statistical details of this response
	AddResponseDetailsToServerStat(pResponse, ResponseReferenceCount);

	uv_rwlock_rdunlock(&m_rwlResponseDirectionFlagLock);

	return ResponseReferenceCount ;
}

// Called by threads (through AddResponseToQueue). ResponsesInQueues increased and decreased by threads and evennt loop so we need locks.
void ConnectionsManager::AddResponseDetailsToServerStat(Response* pResponse, int ResponseReferenceCount)
{
	uv_rwlock_wrlock(&m_rwlResponseCountersLock1);
	uv_rwlock_wrlock(&m_rwlResponseCountersLock2);
	
	if (ResponseReferenceCount)
	{
		ASSERT (pResponse->bAddedToStat == FALSE); // Just to ensure response is not being added more than once (when it has reference count)

		if (pResponse->IsForward() == TRUE)
			m_stServerStat.ResponsesInPeerServersQueues ++ ;
		else
			m_stServerStat.ResponsesInLocalClientsQueues ++ ;

		m_stServerStat.MemoryConsumptionByResponsesInQueue += (pResponse->GetResponse().len + sizeof (Response)); 
		pResponse->bAddedToStat = TRUE;
	}
	else
	{
		m_stServerStat.ResponsesFailedToQueue ++ ;
	}

	uv_rwlock_wrunlock(&m_rwlResponseCountersLock2);
	uv_rwlock_wrunlock(&m_rwlResponseCountersLock1);
}

// Main library function to be called by application.
// (Server could be equipped with multiple adapters thus cmdline gives us chance to specify on which address we want to listen on)
// Refer: https://groups.google.com/forum/#!topic/libuv/sZ4k-jKeeXM
// Parse command line for IP Address and Port values
int ConnectionsManager::StartServer (char* IPAddress, unsigned short int IPv4Port, bool bDisableConsoleWindowCloseButton)
{
	// _CrtSetDbgFlag ( _CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF );
	// Basic hygiene check.
	// For 32-bit systems, the 'de facto' standard is ILP32 - that is, int, long and pointer are all 32-bit quantities.
	// For 64-bit systems, the primary Unix 'de facto' standard is LP64 - long and pointer are 64-bit (but int is 32-bit). 
	// The Windows 64-bit standard is LLP64 - long long and pointer are 64-bit (but long and int are both 32-bit).

	ASSERT (sizeof(unsigned short) == 2);
	ASSERT (sizeof(unsigned int) == 4);
	ASSERT (sizeof(unsigned long) == 4);
	ASSERT (sizeof(unsigned __int64) == 8);
	ASSERT (sizeof(long long) == 8); 

	if (bDisableConsoleWindowCloseButton) // Disables console window's close button
	{
		// Disable console window close button
		HWND hwnd = GetConsoleWindow();
		HMENU hmenu = GetSystemMenu(hwnd, FALSE);
		EnableMenuItem(hmenu, SC_CLOSE, MF_DISABLED | MF_GRAYED);
	}

	int SizeOfClientHandle = sizeof(ClientHandle); // This gives 24 bytes

	// Start reading key strokes
	int RetVal = uv_tty_init(loop, &tty, 0, 1);
	ASSERT_RETURN(RetVal);

	RetVal = uv_tty_set_mode(&tty, 1);
	ASSERT_RETURN(RetVal);

	tty.data = this;
	RetVal = uv_read_start((uv_stream_t*)&tty, ConnectionsManager::alloc_keyboard_buffer, ConnectionsManager::read_stdin);
	ASSERT_RETURN(RetVal);
	LOG (NOTE, "Press Ctrl+P to display status. Press Ctrl+S to shutdown server."); 

	RetVal = SetConsoleCtrlHandler((PHANDLER_ROUTINE) ConnectionsManager::CtrlHandler, TRUE); // To prevent console being closed after pressing Ctrl+Break
	ASSERT_RETURN(!RetVal);

	// Start timer ticks (These are useful to run some periodic tasks)
	RetVal = uv_timer_init(loop, &tick);
	ASSERT_RETURN(RetVal);

	tick.data = this;
	RetVal = uv_timer_start(&tick, ConnectionsManager::on_timer, TIMER_INTERVAL_IN_MILLISECONDS, TIMER_INTERVAL_IN_MILLISECONDS);
	ASSERT_RETURN(RetVal);

	// Initialize logger
	m_pLogger = Logger::GetInstance();
	if (!m_pLogger)
		return UV_ENOMEM;

	m_pLogger->Start(loop);

	LOG (INFO, "Logger Started");

	// Initialize file writer
	m_pWriteToFile = WriteToFile::GetInstance(loop);
	if (!m_pWriteToFile)
		return UV_ENOMEM;

	LOG (INFO, "File writer started");

	LOG (INFO, "Object Sizes: Client:%d Request:%d Response:%d", sizeof(stClient), sizeof(Request), sizeof(Response));

	LOG (NOTE, "Handle count per forwarded response %d (%d KB allocation)", MAX_HANDLES_IN_FORWARDED_RESPONSE, BUFFER_SIZE_IN_KILOBYTES_FOR_HANDLES_IN_SPECIAL_COMMUNICATION); 

	// After starting Logger we are calling StartListening so that it can handle log output from RequestProcessor destructors in case if exception happens
	RetVal = LocalClientsManager::StartListening(IPAddress, IPv4Port);
	ASSERT_RETURN(RetVal);

	LOG (INFO, "Started accepting and listening connections...");

	// Kick off stat output
	ConnectionsManager::LogStat(FALSE); 

	m_bIsServerShutDown = FALSE;

	int ret_uv_run = uv_run(loop, UV_RUN_DEFAULT);

	m_bIsServerShutDown = TRUE;

	// Control comes here only when Shutdown() completed and event loop stops.

	// _CrtDumpMemoryLeaks();

	return ret_uv_run;
}

void ConnectionsManager::alloc_keyboard_buffer(uv_handle_t *handle, uv_buf_t* buf) 
{
	ConnectionsManager* pConnectionsManager = (ConnectionsManager*)handle->data;
	ASSERT(pConnectionsManager);
	buf->base = pConnectionsManager->keyboard_buffer;
	buf->len = KEYBOARD_BUFFER_LEN;
}

void ConnectionsManager::read_stdin(uv_stream_t *stream, ssize_t nread, const uv_buf_t* buf) 
{
	// printf("\nI read character: %c(0x%X)", buf.base[0], buf.base[0]);
	ConnectionsManager* pConnectionsManager;
	pConnectionsManager = (ConnectionsManager*)stream->data;

    if (nread < 0) 
	{
        if (nread == UV_EOF) 
		{
            uv_close((uv_handle_t*)&pConnectionsManager->tty, NULL);
        }
    }
    else 
	{
        if (nread > 0) 
		{
			if (buf->base[0]==0x13) // Ctrl+S for Shutdown server
				pConnectionsManager->StopServer(); // Calls LocalClientsManagers InitiateServerShutdown
			else if (buf->base[0]==0x10) // Ctrl+P for Print status
				pConnectionsManager->LogStat(FALSE); 
        }
    }
}

void ConnectionsManager::StopServer()
{
	LocalClientsManager::InitiateServerShutdown();
}

// This handles Ctrl+break signal thus protects console from exiting
BOOL ConnectionsManager::CtrlHandler( DWORD fdwCtrlType ) 
{ 
  switch( fdwCtrlType ) 
  { 
    // Pass other signals to the next handler. 
    case CTRL_BREAK_EVENT: 
      return TRUE; 

	default: 
      return FALSE; 
  } 
} 

// This MUST be called only from event loop (Currently it has been called from DoPeriodicActivities and read_stdin)
void ConnectionsManager::LogStat(BOOL bCheckForRedundancy)
{
	ADD2PROFILER;

	ServerStat stServerStatCopy;
	GetCopyOfServerStat(stServerStatCopy);

	// At this point we are sure we've copied all statistics locally. Let's proceed getting additional values of the structure.

	/* Get current connections (clients and servers) */
	stServerStatCopy.ClientsConnectionsActive = GetClientsConnectedCount();
	stServerStatCopy.ServersConnected = GetServersConnectedCount();

	// Finally, pass reference of stServerStatCopy to logger
	static ServerStat stLastStat;

	// Log only if stat is different from that of last time
	if ((bCheckForRedundancy == FALSE) || (memcmp ((const void*)&stLastStat, (const void*)&stServerStatCopy, sizeof(ServerStat)) != NULL /*means its not redundant*/))
	{
		/* Compute value of Interval */
		static long long TotalTimeElapsed = 0;
		time_t CurrentTime = time(NULL); // Return the time as seconds elapsed since midnight, January 1, 1970, or -1 in the case of an error.
		static time_t PreviousTime = CurrentTime;
		if (CurrentTime < PreviousTime) // Sometime interval becomes negative ??
		{
			LOG(ERROR, "Weired. Current time is less than previous time. Not logging stat this time.");
			return;
		}

		/* Update last stat */
		stLastStat = stServerStatCopy;

		stServerStatCopy.Interval = (int)(CurrentTime-PreviousTime);
		ASSERT (stServerStatCopy.Interval >=0); // Sometime interval becomes negative ??
		PreviousTime = CurrentTime;
		TotalTimeElapsed += stServerStatCopy.Interval;
		stServerStatCopy.TotalTimeElapsed = TotalTimeElapsed;

		/* Put time stamp */
		stServerStatCopy.Time = CurrentTime;

#if 1 // Added only for debug
	static INT64 ThreadsFinishedPrevious = 0; 
	stServerStatCopy.RequestProcessingThreadsFinished ;
	if ((ThreadsFinishedPrevious) && (ThreadsFinishedPrevious == stServerStatCopy.RequestProcessingThreadsFinished))
		LOG(INFO, "No request processed in last interval") ;
	ThreadsFinishedPrevious = stServerStatCopy.RequestProcessingThreadsFinished ;
#endif

		/* Finally log the copy of stat */
		Logger::GetInstance()->LogStatistics (stServerStatCopy);
	}


	/* Reset some counters which we want to evaluate on per interval basis */
	m_stServerStat.ResponseQueuedDurationMinimum = 0;
	m_stServerStat.ResponseQueuedDurationMaximum = 0;

#ifdef GENERATE_PROFILE_DATA
	/* Uncomment this if we want profile data to be reset for each timer interval
	FunctionProfilerMap::iterator iterator_end = m_stServerStat.FunctionProfiler.end();
	for(FunctionProfilerMap::iterator iterator = m_stServerStat.FunctionProfiler.begin(); iterator != iterator_end; iterator++)
	{
		iterator->second.Frequency = 0;
		iterator->second.MaxDuration = 0;
		iterator->second.TotalDuration = 0;
	}
	*/
#endif
}

// Called by event loop (Comes here every TIMER_INTERVAL_IN_MILLISECONDS millisecond)
void ConnectionsManager::on_timer(uv_timer_t *req)
{
	ConnectionsManager* pConnectionsManager = (ConnectionsManager*)req->data;
	pConnectionsManager->DoPeriodicActivities();
}

// Called by event loop via on timer as well as from many other places in event loop 
// (e.g. after responses are sent, after request is processed, after connection made, after request created etc.)
void ConnectionsManager::DoPeriodicActivities()
{
	ADD2PROFILER;

	time_t CurrentTime = time(NULL);

	static time_t LastLogStatTime = CurrentTime;
	static time_t LastKeepAliveTime = CurrentTime;

	// Make sure we run SendResponses every 5 milliseconds (after getting at least one connection which ensures locks have been initialized)
	if (GetResponsesInQueue())
		SendResponses();

	// Counter is in miliseconds. We have to convert STATUS_INTERVAL_IN_SECONDS to miliseconds.
	INT64 StatusInterval = RequestProcessor::GetCommonParameters().StatusUpdateFrequencyInSeconds;
	if (StatusInterval <= (CurrentTime - LastLogStatTime))
	{
		LastLogStatTime = CurrentTime;
		LogStat(TRUE);
	}

	INT64 KeepAliveInSeconds = RequestProcessor::GetCommonParameters().KeepAliveFrequencyInSeconds;
	// We deliberately keeping keepalive duration "<" time difference, instead of "<=".
	// It is better to call SendKeepAlive by a little delay than exact match so that it can make up with last keep alive activity recording time in GetIdleClients of ClientsPool 
	// (where we do "<=" to compare time difference with keepalive duration). Little difference in timings can skip a cycle of keep alive, that may makes client to think server
	// is disconnected.
	if (KeepAliveInSeconds < (CurrentTime - LastKeepAliveTime))
	{
		LastKeepAliveTime = CurrentTime;
		SendKeepAlive();
	}

#ifdef MEMORY_FOOTPRINT_DEBUG // DEFINE ONLY FOR DEBUGGING
	static bool bclientsconnected = false;

	if ((bclientsconnected == true) && (GetProcessPrivateBytes() > (1024*1024*1283) /*1.25 GB*/))
	{
		LOG (NOTE, "Memory usage went beyond 1 GB. Disconnecting all clients");
		DisconnectAllClients();
		bclientsconnected = false;	
	}

	if (bclientsconnected == false)
	{
		/*
		Note: Windows 8 not always will reduce application memory to what it was when it started.
		Big chunk of memory will remain allocated. It is discussed here:
		Hence, let's not rely on amount of memory consumption by application. Instead let's make sure we disconnected all clients before we relaunch them.
		*/
		// if (GetProcessPrivateBytes() < (1024*1024*93) /* 93 MB */)
		if (GetClientsConnectedCount() <= 2) // Two servers
		{
			LOG (NOTE, "All clients disconnected. Launching clients.");
			// LOG (NOTE, "Memory usage is below 93 MB. Launching clients.");

			Sleep(5000);
			ShellExecute(NULL, NULL, "E:\\Projects\\I-CONTACT\\Server\\DEVELOPMENT\\Output\\Debug_Win32\\HubbleDemoTestClient.exe", NULL, NULL, SW_SHOWNORMAL);
			Sleep(1000);
			ShellExecute(NULL, NULL, "E:\\Projects\\I-CONTACT\\Server\\DEVELOPMENT\\Output\\Debug_Win32\\HubbleDemoTestClient.exe", NULL, NULL, SW_SHOWNORMAL);
			Sleep(1000);
			ShellExecute(NULL, NULL, "E:\\Projects\\I-CONTACT\\Server\\DEVELOPMENT\\Output\\Debug_Win32\\HubbleDemoTestClient.exe", NULL, NULL, SW_SHOWNORMAL);
			// WinExec("E:\\Projects\\I-CONTACT\\Server\\DEVELOPMENT\\Output\\Debug_Win32\\HubbleDemoTestClient.exe", SW_SHOW);

			bclientsconnected = true;
		}
	}
#endif

	// Finally check if flag for "All Clients Disconnected For Shutdown" was set true (We set it when shutdown initiated, no clients in pool and no disconnections are pending in queue)
	// If yes, check if all server/clients are closed and then shutdown.
	if (HasAllClientsDisconnectedForShutdown() == TRUE) 
	{
		int ResponsesInQueue = 0 ;
		// Even after all clients disconnected, There could be still responses pending mostly responses being forwarded.
		ResponsesInQueue = GetResponsesInQueue();

		if (ResponsesInQueue == 0)
		{
			// After client is removed from pool we start disconnection process thread and when it finishes we call close client and increase m_ClientsClosing. 
			// In after close client m_ClientsClosing gets decreases.
			if ((IsServerStopped() == TRUE) && (AreClientsClosing() == 0)) 
			{
				static bool bRequestProcessorsDeleted = false;
				if (!bRequestProcessorsDeleted)
				{
					// WE MUST DELETE REQUEST PROCESSORS THROUGH TIMER ONLY BECAUSE OF SYNC OBJECTS IT HOLDS
					// Also we must delete processors only after we close all clients (because in on_client_closed we may use processor to fetch version parameters)
					DeleteRequestProcessors();
					bRequestProcessorsDeleted = true;
				}
				else if (GetActiveProcessors() == 0)
				{
					// At this stage, clients are disconnected and closed, request processors are also deleted
					// Let's proceed with disconnecting and closing peer servers
					static bool bConnectionsDisconnectedAndClosed = false;
					if (!bConnectionsDisconnectedAndClosed)
					{
						DisconnectAndCloseAllConnections();
						bConnectionsDisconnectedAndClosed = true;
					}
					/* 
						We have to wait till we get callbacks of servers being closed and conections in progress.
						There is no way to cancel ongoing connections:
						https://groups.google.com/forum/#!topic/libuv/O0ag4r75Xqk
					*/
					else if ((AreServersClosing() == 0) && (AreServersConnecting() == 0))
					{
						/* Last thing to do is stop logger */
						LOG (INFO, "Stopping logger, file writer and event loop.");
						if (m_pLogger->Stop() && m_pWriteToFile->Stop())
						{
							static BOOL b_stdin_close_initiated = FALSE;
							if (b_stdin_close_initiated == FALSE)
							{
								uv_close((uv_handle_t*)&tty, after_close_read_stdin);
								b_stdin_close_initiated = TRUE;
							}
						}
					}
					else
					{
						if (AreServersConnecting())
							LOG (NOTE, "Shutdown event is waiting. %d connections are still being connected", AreServersConnecting());

						if (AreServersClosing())
							LOG (NOTE, "Shutdown event is waiting. %d connections are still being closed", AreServersClosing());
						// LOG(WARNING, "Shut down event is waiting till peer server connections are being closed or are in progress") ;
					}
				}
			}
		}
	}

	return;
}

void ConnectionsManager::after_close_read_stdin (uv_handle_t* handle)
{
	ConnectionsManager* pConnectionsManager = (ConnectionsManager*)handle->data;
	uv_close((uv_handle_t*)&pConnectionsManager->tick, after_timer_closed);
}

void ConnectionsManager::after_timer_closed(uv_handle_t* handle)
{
	ConnectionsManager* pConnectionsManager = (ConnectionsManager*)handle->data;
	pConnectionsManager->Shutdown();
}

// Called by event loop (through timer) when m_bAllClientsDisconnectedForShutdown becomes TRUE
void ConnectionsManager::Shutdown()
{
// 	DEL(m_pLogger); // After framework DLL implementation, Logger instance is created by application and it will be global static. So DEL is not applicable
	DEL(m_pWriteToFile);
	uv_stop(loop);
}

// Called by on_timer before it calls SendResponses and also after it detects "All Clients Disconnected For Shutdown" was set true.
int ConnectionsManager::GetResponsesInQueue()
{
	uv_rwlock_rdlock(&m_rwlResponseCountersLock2);
	int ResponsesInQueue = m_stServerStat.ResponsesInPeerServersQueues + m_stServerStat.ResponsesInLocalClientsQueues;
	uv_rwlock_rdunlock(&m_rwlResponseCountersLock2);

	return ResponsesInQueue;
}

// Called by ConnectionsManager::DoPeriodicActivities()
void ConnectionsManager::SendResponses()
{
	ADD2PROFILER;

	/* Toggle direction flags */
	uv_rwlock_wrlock(&m_rwlResponseDirectionFlagLock);
	m_bResponseDirectionFlag = m_bResponseDirectionFlag ? FALSE : TRUE ;
	uv_rwlock_wrunlock(&m_rwlResponseDirectionFlagLock);

	SendLocalClientsResponses();
	SendPeerServersResponses();
}

// Event loop (uv_process_tcp_write_req) calls this after writing to socket. Status will be either 0 or error code.
// If we've called it from SendResponse after error, we've set the status to uv_write return code (0 or error value)
void ConnectionsManager::after_send_responses(uv_write_t* write_req, int status) 
{
	ADD2PROFILER;

	stNode* pNode = (stNode*) write_req->data;

	// bool bIsServer = pNode->IsServer();

	Responses* pResponsesSent = pNode->m_pResponsesBeingSent;
	ASSERT(pResponsesSent);

	int ResponsesSentCount = (int) pResponsesSent->size();
	ASSERT(ResponsesSentCount);

	ConnectionsManager* pConnectionsManager = NULL;

	Responses::iterator it = pResponsesSent->begin();
	for (int i=0; it != pResponsesSent->end(); i++)
	{
		Response * pResponse = (*it++);
		pConnectionsManager = pResponse->GetConnectionsManager();
		pConnectionsManager->AfterSendingResponse(pResponse, pNode, status);
	}

	if (pNode->IsServer()) 
	{
		// If it was server responses forwarded to, we must empty responses being sent using swap (otherwise vector keeps memory allocated)
		// We reserve space dynamically in SendPeerServersResponses
		// This is because server's have no queue limit of responses being forwarded
		stPeerServer* pPeerServer = (stPeerServer*) pNode; 
		std::vector<uv_buf_t>().swap(pPeerServer->m_pResponsesBuffersBeingForwarded);
		Responses().swap(*pResponsesSent);
	}
	else
	{
		// In case of clients, we should not use swap as it affects capacity (reserved in client's constructor). Instead use clear.
		// This is because clients has fixed limit as to how much response we can queue.
		(*pResponsesSent).clear();
	}

	// To avoid recursion, we are calling SendResponses only when we come here via callback
	if (pConnectionsManager->after_send_response_called_by_send_response == FALSE)
	{
		pConnectionsManager->m_stServerStat.ResponsesBeingSent -= ResponsesSentCount; // -= ResponseLength ; // pServerConnection->write_queue_size ;
		pConnectionsManager->DoPeriodicActivities(); // This in turn calls SendResponses and also takes care of logging and other things
	}

	return;
}

void ConnectionsManager::AfterSendingResponse(Response* pResponse, stNode* pNode, int status)
{
	ConnectionsManager* pConnectionsManager = pResponse->GetConnectionsManager();

	int ResponseLength = pResponse->GetResponse().len;
	int ResponseReferenceCount = pResponse->GetReferenceCount();

	if (pNode->IsServer() == false)
	{
		stClient* pClient = (stClient*) pNode;
		ASSERT (pClient);
		pConnectionsManager->AfterSendingLocalClientsResponses(pClient, pResponse, status);
	}
	else
	{
		stPeerServer* pPeerServerInfo = (stPeerServer*) pNode; 
		ASSERT (pPeerServerInfo);
		ASSERT (pResponse->IsForward() == TRUE); // Non-forwarded response went into server's queue?
		ASSERT (pPeerServerInfo->m_ServerIPv4Address == pResponse->GetServersIPv4Address());
		pConnectionsManager->AfterSendingPeerServersResponses(pPeerServerInfo, pResponse, status);
	}

	ASSERT (ResponseReferenceCount >= 0);

	++(pResponse->ResponseSentCount) ;
	int referencecount = pResponse->GetReferenceCount();
	if (pResponse->ResponseSentCount == referencecount) // If we've sent response to all intended clients/servers, it's time to delete the response
	{
		// Whether it is uv_write returned non-negative value or libuv calls after_send_response with status as WRITE_OK,
		// it doesn't gurantees that client has received the response:
		// https://groups.google.com/forum/#!topic/libuv/hbvMnWOnDV4
		// Hence, we do not need to worry about deleting response after failure (as success, too, is not a gurantee of message delivery)
		// Also we should remember we disconnect connection for which uv_write fails:
		// https://groups.google.com/forum/#!topic/libuv/nJa3WeiVs2U
		// So, it doesn't make sense to keep response pending in queue for connection which is not valid anymore.

		uv_rwlock_wrlock(&pConnectionsManager->m_rwlResponseCountersLock2);

		if (pResponse->IsForward() == TRUE)
			pConnectionsManager->m_stServerStat.ResponsesInPeerServersQueues -- ;
		else
			pConnectionsManager->m_stServerStat.ResponsesInLocalClientsQueues -- ;

		pConnectionsManager->m_stServerStat.MemoryConsumptionByResponsesInQueue -= (ResponseLength + sizeof (Response)); 
		uv_rwlock_wrunlock(&pConnectionsManager->m_rwlResponseCountersLock2);

		// Before deleting find out how long the response was in queue. Update stat accordingly.
		if (pResponse->QueuedTime)
		{
			double QueuedDuration = pConnectionsManager->GetHighPrecesionTime() - pResponse->QueuedTime ;
			pConnectionsManager->m_stServerStat.ResponseQueuedDurationMinimum = pConnectionsManager->m_stServerStat.ResponseQueuedDurationMinimum ? pConnectionsManager->m_stServerStat.ResponseQueuedDurationMinimum : 0xFFFFFFFF;
			pConnectionsManager->m_stServerStat.ResponseQueuedDurationMinimum = min (pConnectionsManager->m_stServerStat.ResponseQueuedDurationMinimum, QueuedDuration);
			pConnectionsManager->m_stServerStat.ResponseQueuedDurationMaximum = max (pConnectionsManager->m_stServerStat.ResponseQueuedDurationMaximum, QueuedDuration);
		}

		DEL (pResponse); // We MUST delete response _ONLY_ in after_send_response as its being used by uv_write(libuv)
	}

	return;
}

// Called by threads (Response::CreateResponse)
INT64 ConnectionsManager::GetMemoryConsumptionByResponsesInQueue()
{
	uv_rwlock_rdlock(&m_rwlResponseCountersLock1);
	uv_rwlock_rdlock(&m_rwlResponseCountersLock2);
	INT64 MemoryConsumptionByResponsesInQueue = m_stServerStat.MemoryConsumptionByResponsesInQueue;
	uv_rwlock_rdunlock(&m_rwlResponseCountersLock2);
	uv_rwlock_rdunlock(&m_rwlResponseCountersLock1);
	return MemoryConsumptionByResponsesInQueue;
}

void ConnectionsManager::GetCopyOfServerStat (ServerStat& stServerStatCopy)
{
	// Make sure synchronized counters are updated
	uv_rwlock_rdlock(&m_rwlResponseCountersLock2);
	uv_rwlock_rdlock(&m_rwlRequestCountersLock2);

	// First let's copy the statistics to local structure. 
	// Note that although the ServerStat has std::map, this will copy the map containts also and we don't need to worry about it:
	stServerStatCopy = m_stServerStat;

	uv_rwlock_rdunlock(&m_rwlRequestCountersLock2);
	uv_rwlock_rdunlock(&m_rwlResponseCountersLock2);

	return;
}

VersionParameters* ConnectionsManager::GetVersionParameters(USHORT version) 
{
	return LocalClientsManager::GetVersionParameters(version);
}
