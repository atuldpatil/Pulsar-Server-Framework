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

//
// This is how logger is planned to work:
//
// Log will be called throughout server code as and when needed
// Log keeps populating info/note/error maps
//
// LogStat will be called by event loop at predefined interval
// LogStat keeps appending stat queue with stats
//
// Logger thread keeps contineously running (It starts when ConnectionsManager::StartServer calls Logger::Start). 
// When it sees new element in stat map, it will call log processing function with stat structure and other maps (info/note/errors)
//
// Once processing is done, Logger thread erases info map, and deletes the stat structure from stat queue. 
// It then gets next stat from queue and calls again log processing function
// If there is nothing in stat queue, it waits for 100 millisecond
//
// Called from GetInstance (which is called through ConnectionsManager c'tor)

Logger* Logger::m_LoggerInstance=NULL;

Logger::Logger()
{
	ASSERT(m_LoggerInstance == NULL); // Only one instance allowed. Logger is having single instance. (Because we cannot run uv_queue_work from threads)
	m_LoggerInstance = this;

	uv_rwlock_init(&m_rwlStatQueue);
	uv_rwlock_init(&m_rwlErrorsMap);
	uv_rwlock_init(&m_rwlExceptionsMap);
	uv_rwlock_init(&m_rwlNotesMap);
	uv_rwlock_init(&m_rwlDebugMap);
	uv_rwlock_init(&m_rwlInfoMap  );

	m_log_work_t.data = m_LoggerInstance;

	m_bStopLoggerThread = FALSE;
	m_bLoggerThreadStopped = TRUE;
}

int Logger::Start(uv_loop_t* loop)
{
	ASSERT(loop); // When calling first time make sure to call with valid loop value.
	int RetVal = uv_queue_work (loop, &m_log_work_t, log_processing_thread, after_log_processing_thread);
	ASSERT (RetVal == 0); // uv_queue_work returns non-zero only when disconnection_processing_thread is NULL
	m_bLoggerThreadStopped = FALSE;
	return RetVal;
}

// Called from DoPeriodicActivities event
BOOL Logger::Stop()
{
	m_bStopLoggerThread = TRUE;

	if (m_bLoggerThreadStopped) 
		return TRUE;
	else
		return FALSE;
}

Logger::~Logger()
{
	ASSERT (m_bLoggerThreadStopped == TRUE); // Application should call Stop() before deleting logger

	uv_rwlock_destroy(&m_rwlStatQueue);
	uv_rwlock_destroy(&m_rwlErrorsMap);
	uv_rwlock_destroy(&m_rwlExceptionsMap);
	uv_rwlock_destroy(&m_rwlNotesMap);
	uv_rwlock_destroy(&m_rwlDebugMap);
	uv_rwlock_destroy(&m_rwlInfoMap  );
}

// Called from ConnectionsManager c'tor
Logger* Logger::GetInstance() 
{ 
	ASSERT_MSG ((m_LoggerInstance != NULL), "ERROR: Logger instance not found"); // Must have been instantiated before calling this

	return m_LoggerInstance;
}

/* ----------------------------------------------------------------------------------------------------------------------------------
Logger thread keeps contineously running (It starts when Logger gets instantiated in main function). 
When it sees new element in stat map, it will call log processing function with stat structure and other maps (info/note/errors)

Once processing is done, Logger thread erases info map, and deletes the stat structure from stat queue. 
	It then gets next stat from queue and calls again log processing function
	If there is nothing in stat queue, it waits for 100 millisecond
*/
void Logger::log_processing_thread (uv_work_t* log_work_t)
{
	Logger * pLogger = (Logger*) log_work_t->data;
	BOOL bToQuitAfterQueueCheck = FALSE;

	while(1)
	{
		// Get size. If non zero, get next element in the queue and remove it from queue.
		// We MUST do all three in single lock to avoid writer to modify the queue causing inconsistancy
		uv_rwlock_wrlock(&pLogger->m_rwlStatQueue);
		int QSize = static_cast<int> (pLogger->m_StatQueue.size());

		if ((!QSize) && (bToQuitAfterQueueCheck == FALSE))
		{
			// If no elements in queue, shrink the queue (or it may keep large amount of memory occupied unnecessarily)
			std::queue<ServerStat>(pLogger->m_StatQueue).swap(pLogger->m_StatQueue);

			uv_rwlock_wrunlock(&pLogger->m_rwlStatQueue);

			Sleep(333); // When all the elements in queue are done, take a little pause...
			if (pLogger->m_bStopLoggerThread == TRUE)
				bToQuitAfterQueueCheck = TRUE;

			continue;
		}

		ServerStat stServerStat;

		if (QSize)
		{
			stServerStat = pLogger->m_StatQueue.front();  
			pLogger->m_StatQueue.pop();
		}
		uv_rwlock_wrunlock(&pLogger->m_rwlStatQueue);

		if (QSize)
		{
			// Compute some additional stat (based on existing statistical data)
			pLogger->ComputeAdditionalStat(stServerStat);
			pLogger->GetCopyAndProcessLog(stServerStat); 
		}
		else if (bToQuitAfterQueueCheck == TRUE) // No element in queue and its time to quit
		{
			break;
		}
	}
}

void Logger::after_log_processing_thread (uv_work_t* log_work_t, int status)
{
	Logger* pLogger = (Logger*) log_work_t->data;
	pLogger->m_bLoggerThreadStopped = TRUE;
}

// Called by log processing thread
void Logger::ComputeAdditionalStat (ServerStat& stServerStat)
{
	/* Compute RequestsArrivedPerSecond, RequestsProcessedPerSecond & AverageRequestsSize in last time interval */
	static INT64 PreviousRequestsArrived = stServerStat.RequestsArrived ;
	static INT64 PreviousRequestsProcessed = stServerStat.RequestsProcesed ;
	static double PreviousTotalRequestProcessingTime = stServerStat.TotalRequestProcessingTime;
	static INT64 PreviousTotalRequestBytesProcessed = stServerStat.TotalRequestBytesProcessed ;
	static INT64 PreviousResponsesSent = stServerStat.ResponsesSent ;
	
	int RequestsArrivedInLastInterval = (int)(stServerStat.RequestsArrived - PreviousRequestsArrived);
	int RequestsProcessedInLastInterval = (int)(stServerStat.RequestsProcesed - PreviousRequestsProcessed);
	double RequestProcessingTimeInLastInterval = stServerStat.TotalRequestProcessingTime - PreviousTotalRequestProcessingTime;
	int RequestsSizeCumulativeInLastInterval = (int)(stServerStat.TotalRequestBytesProcessed >= PreviousTotalRequestBytesProcessed) ? (int)(stServerStat.TotalRequestBytesProcessed - PreviousTotalRequestBytesProcessed) : (int)(((UINT64)0xFFFFFFFFFFFFFFFF-PreviousTotalRequestBytesProcessed)+stServerStat.TotalRequestBytesProcessed);
	int ResponsesSentInLastInterval = (int)(stServerStat.ResponsesSent - PreviousResponsesSent) ;
	
	if (stServerStat.Interval) 
	{
		stServerStat.RequestsArrivedPerSecond = RequestsArrivedInLastInterval/stServerStat.Interval;
		stServerStat.RequestsProcessedPerSecond = RequestsProcessedInLastInterval/stServerStat.Interval; 
		stServerStat.AverageRequestProcessingTime = RequestProcessingTimeInLastInterval/RequestsProcessedInLastInterval;
		stServerStat.AverageRequestsSize = (RequestsProcessedInLastInterval) ? (RequestsSizeCumulativeInLastInterval/RequestsProcessedInLastInterval) : 0 ;
	}

	PreviousRequestsArrived = stServerStat.RequestsArrived ;
	PreviousRequestsProcessed = stServerStat.RequestsProcesed ;
	PreviousTotalRequestBytesProcessed = stServerStat.TotalRequestBytesProcessed ;
	PreviousResponsesSent = stServerStat.ResponsesSent ;
	PreviousTotalRequestProcessingTime = stServerStat.TotalRequestProcessingTime;

	
	/* Compute actual and max estimated handle count */
	::GetProcessHandleCount(GetCurrentProcess(), &stServerStat.ActualHandleCount);

	int NumberOfConnections = stServerStat.ClientsConnectionsActive; 
	static int NumberOfMaxConnections = 0;
	NumberOfMaxConnections = max(NumberOfMaxConnections, NumberOfConnections);

	int InitialHandleCount = 86 ; /* Initial handle count when server just starts and no one yet connected */
	int ConnectionsHandleCount = 3*NumberOfConnections; /* 3 handles per connection */ 
	int BarriersHandleCount = (RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads*2); /* Barriers are equal to number of thread. Two handles per barrier */  
	int ThreadsHandleCount = RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads; /* A handle for each thread */
	int LocksHandleCount = NumberOfMaxConnections /* A rwLock per connection in m_ClientLockRequestsResponsesArray. This does not however get destroyed with disconnection as it gets reused by a new connection. */ + \
		1 /* A m_ArrayLock in ClientsPool */ + \
		1 /* A m_rwlAsyncHandleBarrierUseFlagLock lock */ + \
		1 /* A m_rwlProcessingThreadsSyncLock */ + \
		1 /* A m_rwlResponseLock */ + \
		1 /* A m_rwlMemoryAllocationErrorCounter */ + \
		NumberOfConnections /* A m_rwlLockForDisconnectionFlag for each connection. It gets destroyed with each disconnection. */ + \
		1 /* A m_rwlLogQueueLock for logger */ ;

	stServerStat.EstimatedHandleCount = InitialHandleCount + ConnectionsHandleCount + BarriersHandleCount + ThreadsHandleCount + LocksHandleCount ;

	/* Compute actual memory consumption and system free memory */
	stServerStat.ActualMemoryConsumption = ConnectionsManager::GetProcessPrivateBytes ();
	stServerStat.SystemFreeMemory = uv_get_free_memory();

	/* Compute approximate memory consumption */
	stServerStat.TotalMemoryConsumption = stServerStat.MemoryConsumptionByClients + stServerStat.MemoryConsumptionByRequestsInQueue + stServerStat.MemoryConsumptionByResponsesInQueue ;
}

// Called by log processing thread
void Logger::GetCopyAndProcessLog(ServerStat& stServerStat)
{
	try
	{
		/* Lock and get copies of each map. So that we won't hold any thread while logging when log is being processed */
		LoggerMap InfoMapCopy, NotesMapCopy, ErrorsMapCopy, ExceptionsMapCopy, DebugMapCopy;

		// Get copy of info map
		GetMapCopy (m_InfoMap, InfoMapCopy, m_rwlInfoMap, TRUE);

		// Get copy of notes map
		GetMapCopy (m_NotesMap, NotesMapCopy, m_rwlNotesMap);

		// Get copy of errors map
		GetMapCopy (m_ErrorsMap, ErrorsMapCopy, m_rwlErrorsMap);

		// Get copy of exceptions map
		GetMapCopy (m_ExceptionsMap, ExceptionsMapCopy, m_rwlExceptionsMap);

		// Get copy of debug map
		GetMapCopy (m_DebugMap, DebugMapCopy, m_rwlDebugMap, TRUE);

		ProcessLog (stServerStat, InfoMapCopy, NotesMapCopy, ErrorsMapCopy, ExceptionsMapCopy, DebugMapCopy);    
	}
	catch(std::bad_alloc&)
	{
		std::cout << "\nEXCEPTION bad_alloc in log processing thread." ;
		Sleep(333);
	}
}

// Called by log processing thread (through GetCopyAndProcessLog)
void Logger::GetMapCopy (LoggerMap& Map, LoggerMap& MapCopy, uv_rwlock_t& Lock, BOOL bEraseOriginalMap)
{
	// Get copy of Info map and erase all elements of the original map
	uv_rwlock_wrlock(&Lock);
	try
	{
		MapCopy = Map;
		if(bEraseOriginalMap)
			Map.clear(); 
	}
	catch(std::bad_alloc&)
	{
		std::cout << "\nEXCEPTION bad_alloc in log processing thread." ;
	}
	uv_rwlock_wrunlock(&Lock);
}


/*------------------------------------------------------------------------------------------------------------------------------------*/
// Called by LogStat
void Logger::LogStatistics (ServerStat& stServerStat)
{
	uv_rwlock_wrlock(&m_rwlStatQueue);

	try
	{
		m_StatQueue.push (stServerStat);
	}
	catch(std::bad_alloc&)
	{
		std::cout << "\nEXCEPTION bad_alloc. Cannot log server statistics." ;
	}

	uv_rwlock_wrunlock(&m_rwlStatQueue); 
}

void Logger::getClassName(const char* fullFuncName, std::string& csClassName)
{
	try
	{
		csClassName = fullFuncName;
		size_t pos = csClassName.find_last_of("::");

		if (pos == std::string::npos)
		{
			return;
		}

		csClassName = csClassName.substr(0, pos-1);
		return;
	}
	catch(std::bad_alloc&)
	{
		csClassName = ""; // If no space is available to allocate class name, let's return blank string. In case this throws, it will be handled by caller.
		return;
	}
}


/*------------------------------------------------------------------------------------------------------------------------------------*/
// Called by event loop as well as request processing threads
void Logger::LogMessage (int Type, char* FileName, int LineNumber,  char* Function, char* LogFormatMsg, ...)
{
	if (((Type == DEBUG) && (PROCESS_DEBUG_LOGS == FALSE)) || (Type == IGNORE) || (LogFormatMsg == NULL)) 
		return ;

	char* LogMessage = NULL;
	int LogMsgLen = DEFAULT_LOG_MSG_LENGTH;
	int characters_written = -1;

	while(characters_written < 0)
	{
		va_list args;
		va_start(args, LogFormatMsg);

		try
		{
			LogMessage = new char [LogMsgLen];
		}
		catch(std::bad_alloc&)
		{
			break;
		}

		// If the storage required to store the data and a terminating null exceeds sizeOfBuffer, and count is _TRUNCATE, 
		// as much of the string as will fit in buffer is written and -1 returned.
		characters_written = vsnprintf_s(LogMessage, LogMsgLen, _TRUNCATE, LogFormatMsg, args);

		va_end(args);

		if (characters_written < 0)
		{
			DEL_ARRAY (LogMessage);
			LogMsgLen += LOG_MSG_INCREMENT;
		}
	}

	if (LogMessage == NULL) // for any reason
	{
		// std::cout << "\n[" << Component << "]" << LogFormatMsg ;
		return ;
	}

	// Convert Component and LogMessage to std::string
	try
	{
		std::string Component="";
		if (Function)
		{
			// Only class name as component for info and notes
			if ((Type == INFO) || (Type == NOTE))
				getClassName(Function, Component);
			else
				Component = Function;
		}

		std::string logmessage;
		logmessage = (std::string)"[" + (std::string)Component + (std::string)"] ";
		logmessage += LogMessage; 

		if ((Type == ERROR) || (Type == EXCEPTION))
			logmessage += (std::string)" (In " + FileName + (std::string)", at line " + std::to_string(LineNumber)+ (std::string)")";

		LoggerMap* pLoggerMap;
		uv_rwlock_t* pLock;

		switch(Type)
		{
			case ASSERTION:
				std::cout << "\n\n\n" << logmessage;
				// No break here intentionally

			case ERROR:
				pLoggerMap = &m_ErrorsMap;
				pLock = &m_rwlErrorsMap;
				break;

			case EXCEPTION:
				pLoggerMap = &m_ExceptionsMap;
				pLock = &m_rwlExceptionsMap;
				break;

			case NOTE:
				pLoggerMap = &m_NotesMap;
				pLock = &m_rwlNotesMap;
				break;

			case DEBUG:
				pLoggerMap = &m_DebugMap;
				pLock = &m_rwlDebugMap;
				break;

			default:
				pLoggerMap = &m_InfoMap;
				pLock = &m_rwlInfoMap;
				break;
		}

		IncreaseMapCounter (*pLoggerMap, Type, logmessage, *pLock);
	}
	catch(std::bad_alloc&)
	{
	}

	// We are done with LogMessage
	DEL_ARRAY (LogMessage); 
}

// Called by event loop as well as request processing threads (through LogMessage)
void Logger::IncreaseMapCounter (LoggerMap& Map, int Type, std::string& Message, uv_rwlock_t& Lock)
{
	// Add logmessage to map
	// As per standards, map value gets initialized when we insert new key
	// So we can safely assume here that counter value (long) of LoggerMap will be zero when we insert new info/note/error

	uv_rwlock_wrlock(&Lock);
	try
	{
		if (Type == NOTE)
		{
			if (Map.find(Message) == Map.end())  // Note need not have to maintain counter. Insert only once in map.
				Map[Message]++ ;
		}
		else
		{
			Map[Message]++ ;
		}
	}
	catch(std::bad_alloc&)
	{
	}
	uv_rwlock_wrunlock(&Lock); 
}
