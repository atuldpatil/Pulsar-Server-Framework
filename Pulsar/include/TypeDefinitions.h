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
	Various data types used throughout the framework's source code have been defined here.
*/

struct stClient;

typedef uint64_t UINT64;

typedef struct structLockRequestsResponses // It's better we typedef it than just struct tag
{
	structLockRequestsResponses();
	~structLockRequestsResponses();
	uv_rwlock_t rwLock;
	int	Requests;
	int	Responses;
	time_t LastActivityTime;
} LockRequestsResponses;

struct stProfilerData
{
	double MaxDuration;
	double MaxDurationInLastInterval;
	double TotalDuration;
	long long Frequency;
	time_t PreviousTime;
};

typedef std::map<std::string, stProfilerData> FunctionProfilerMap;
// typedef std::map<std::string, long long> FunctionFrequencyMap;

// We need to export/import IPv4Address as application might want to use it
typedef class DLL_API clIPv4Address
{
	UINT m_Integer; // In case of IPv6, replace with structure having two __int64 and define operators inside
	
	// WARNING: Do not add any other variable here which will cause sizeof operator
	// Remember sizeof should return value of bytes actually needed to store IP address (e.g. sizeof(UINT) for IPv4 and sizeof (__int64)*2 for IPv6)

	static unsigned short int Port; // This won't add to sizeof as it is static. We must need to define separate macros for export and import 
									// else we get linker error 'unresolved symbol' for static variables in such kind of exported class.

	public:
		void SetAddress (const char * strIPAddress)
		{
			unsigned char Bytes[4];
			unsigned short int Short[4];
			// %hu is unsigned short int specifier. In standard library %hhu is unsigned char but NOT suported by Microsoft compiler.
			// Hence we've used %hu then converted it to bytes.
			sscanf_s(strIPAddress, "%hu.%hu.%hu.%hu:%hu", &Short[0], &Short[1], &Short[2], &Short[3], &Port); 
			for (int i=0; i<4; i++)
				Bytes[i] = (unsigned char)Short[i];

			// Let's convert IP address in 32-bit unsigned integer:
			// For example, my local google.com is at 64.233.187.99. That's equivalent to: 64*2^24 + 233*2^16 + 187*2^8 + 99 = 1089059683
			m_Integer = (UINT)(Bytes[0]<<24) + (UINT)(Bytes[1]<<16) + (UINT)(Bytes[2]<<8) + (UINT)Bytes[3];
		}

		// In case of IPv4, define Set function here, which accepts high and low __int64 to form IPv6
		void operator = (UINT ipv4addressint)
		{
			// Let's store IP address int in 32-bit unsigned integer member
			m_Integer = ipv4addressint;
		}

		unsigned char operator[](int i)
		{
			unsigned char Byte;
			// Code below should work for all platforms regardless of endianness:
			Byte = ((m_Integer >> ((3-i)*8)) & 0xFF);
			return Byte;
		}
		
		/*
		void GetBytes(unsigned char (&Bytes)[4]) // Only accepts reference of array of 4 bytes
		{ 
			// Code below should work for all platforms regardless of endianness:

			Bytes[0] = ((m_Integer >> 24) & 0xFF);
			Bytes[1] = ((m_Integer >> 16) & 0xFF);
			Bytes[2] = ((m_Integer >> 8) & 0xFF);
			Bytes[3] = (m_Integer & 0xFF);

			return; 
		}
		*/

		unsigned short int GetPort() { return Port; }

		// In case of IPv6, replace this with size of two __int64
		int GetSize()
		{
			return sizeof (m_Integer);
		}

		// As long as we are running servers on IPv4, integer UINT is sufficient
		// In case of IPv6 we need to define m_Integer1 and m_Integer2 of type __int64 and
		// also define < operator as well as gethigh(), getlow() functions those will return m_Integer1 and m_Integer2
		operator UINT() const
		{ 
			return m_Integer; 
		}
} IPv4Address;

typedef struct structServerStat
{
	/* These values will be generated as the server goes along processing requests */

	// Clients connected/disconnected
	INT64 ClientsConnectedCount, ClientsDisconnectedCount;
	INT64 DisconnectionsByServer, DisconnectionsByClients;
	INT64 MemoryConsumptionByClients; // Gets changed only through event loop
	INT64 ActiveClientRequestBuffers;

	// Requests and Responses related
	int ResponsesBeingSent, ResponsesInPeerServersQueues, ResponsesInLocalClientsQueues;

	INT64 RequestsArrived, RequestsProcesed, RequestsNotAdvicedToProcess, RequestsRejectedByServer, RequestsFailedToProcess, RequestBytesIgnored, TotalRequestBytesProcessed;
	INT64 RequestsProcessedPerThread[MAX_WORK_THREADS];
	INT64 ResponsesAcknowledgementsOfForwardedResponses, ResponsesErrors, ResponsesKeepAlives, ResponsesFatalErrors, ResponsesOrdinary;
	INT64 ResponsesForwarded, ResponsesMulticasts, ResponsesUpdates;
	INT64 ResponsesSent, ResponsesFailedToQueue, ResponsesFailedToSend, ResponsesFailedToForward, TotalResponseBytesSent;
	INT64 MemoryConsumptionByRequestsInQueue; // Gets changed in event loop
	INT64 MemoryConsumptionByResponsesInQueue; // Gets changed in request_processing_thread and event loop. Protected by rwlResponseLock.
	INT64 RequestProcessingThreadsStarted, RequestProcessingThreadsFinished;
	double TotalRequestProcessingTime, AverageRequestProcessingTime, ResponseQueuedDurationMinimum, ResponseQueuedDurationMaximum; 

	// Errors and exceptions
	UINT64 HeaderErrorInPreamble, HeaderErrorInVersion, HeaderErrorInSize;
	UINT64 ForwardErrorWritingServer, ForwardErrorConnectingTimedout, ForwardErrorOverflowed, ForwardErrorDisconnecting, ForwardErrorDisconnected;
	UINT64 MemoryAllocationExceptionCount, RequestCreationExceptionCount, ResponseCreationExceptionCount, ClientCreationExceptionCount, ConnectionCreationExceptionCount;
	

	/* These values will be computed inside LogStat */
	int Interval;
	long long TotalTimeElapsed;
	unsigned int ClientsConnectionsActive, ServersConnected;
	long long TotalMemoryConsumption;


	/* These value will be computed in logger thread */
	int RequestsArrivedPerSecond, RequestsProcessedPerSecond, AverageRequestsSize;
	DWORD EstimatedHandleCount, ActualHandleCount;
	long long ActualMemoryConsumption;
	long long SystemFreeMemory;
	INT64 MaxPossibleClients;

#ifdef GENERATE_PROFILE_DATA
	/* These fields are related to profiling */
	static FunctionProfilerMap FunctionProfiler;
#endif

	/* Time stamp will be put at the very moment just before adding stat to queue */
	time_t Time;
} ServerStat;

typedef struct stClientHandle 
{ 
	// Huge number of clients can connect to the Server at a time. There is huge theoretical limit to this. 

	// stClient registration number is a number which keep increamenting by one each time client connects (but never decreases even client disconnects)
	UINT64 m_ClientRegistrationNumber; 

	// IPv4 address of the server to which this client is connected
	IPv4Address m_ServerIPv4Address;

	stClientHandle()
	{
		m_ClientRegistrationNumber = 0;
		m_ServerIPv4Address = 0;
	}

	bool operator < (const stClientHandle& ch) const
	{
		if ((IPv4Address)m_ServerIPv4Address < (IPv4Address)ch.m_ServerIPv4Address)
		{
			return true;
		}
		else if ((IPv4Address)m_ServerIPv4Address > (IPv4Address)ch.m_ServerIPv4Address) 
		{
			return false;
		}
		else
		{
			return (m_ClientRegistrationNumber < ch.m_ClientRegistrationNumber);
		}
	}

	bool operator == (const stClientHandle& ch) const
	{
		return ((m_ClientRegistrationNumber == ch.m_ClientRegistrationNumber) && (m_ServerIPv4Address == ch.m_ServerIPv4Address));
	}

} ClientHandle;

// Structure to store version specific server paremeters
typedef struct stVersionParameters
{
	// Server should have idea how much max size it should wait for request, for particular client version.
	// Maximum request or response size needed for a communication happening over given version.
	int m_MaxRequestSize;
	int m_MaxResponseSize;

	stVersionParameters()
	{
		m_MaxRequestSize = (64*1024);
		m_MaxResponseSize = (64*1024);
	}

	stVersionParameters(int maxrequestsize, int maxresponsesize)
	{
		m_MaxRequestSize = maxrequestsize;
		m_MaxResponseSize = maxresponsesize;
	}
} VersionParameters;

// Structure to store common server paremeters (common to all versions)
typedef struct stCommonParameters
{
	int MaxPendingResponses;
	// int MaxPendingRequests; // This was per client number. However we cannot have more than 1 pending requests per client because parallel processing of requests of same client is not desired under any circumstance.
	int MaxRequestProcessingThreads;
	// int MaxClientsPerMulticast;
	// int MaxVersionNumber;
	// long MaxResponsesCreatedPerThread;
	int KeepAliveFrequencyInSeconds;
	int StatusUpdateFrequencyInSeconds;

	stCommonParameters()
	{
		// Set some default values of common parameters 
		KeepAliveFrequencyInSeconds = 30;
		StatusUpdateFrequencyInSeconds = 5;
		MaxPendingResponses = 16;
		MaxRequestProcessingThreads = 5;
	}
} CommonParameters;

typedef std::vector<stClient*> Clients;
typedef std::set<ClientHandle> ClientHandles;
typedef std::set<ClientHandle*> ClientHandlesPtrs;
typedef std::set<ClientHandle*>::iterator ClientHandlesPtrsIterator;
typedef std::map<IPv4Address, ClientHandlesPtrs> mapServersAndHandles;
typedef std::vector<class Response*> Responses;
typedef enum ClientType { VersionedClient, VersionlessClient };

typedef std::map<std::string, long long> LoggerMap;
typedef LoggerMap::iterator LoggerIterator;

typedef uv_rwlock_t Lock;
typedef uv_buf_t Buffer;

struct RequestCreationException{};
struct ResponseCreationException{};
struct ClientCreationException{};
struct ConnectionCreationException{};