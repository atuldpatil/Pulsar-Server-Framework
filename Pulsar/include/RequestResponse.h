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
	Request objects are used by LocalClientsManager.
	Response objects are created by application's request processors derived from class RequestProcessor.
*/

class Request
{
	uv_buf_t m_Request ; 
	stClient* m_Client ;
	double m_ArrivalTime ;
	BOOL m_bHasEncounteredMemoryAllocationException;
	BOOL m_bIsDeferred;

	public:

		/* USED BY CLIENT */
		uv_work_t m_work_t;
		Request* m_NextRequest;

		Request(uv_buf_t* request, double ArrivalTime, ClientsPool* pClientsPool, ClientHandle* pClientHandle);
		~Request();
		
		stClient* GetClient();
		void SetMemoryAllocationExceptionFlag();
		BOOL GetMemoryAllocationExceptionFlag();

		const uv_buf_t GetRequest();

		double GetArrivalTime();

		void DeferProcessing(BOOL bFlag);
		BOOL IsDeferred();
};

class Response
{
	std::unique_ptr<char[]> base; // This stores m_Response.base pointer. It will be automatically destructed when constructor throws exception.
	uv_buf_t m_Response ;

	/*
		Response is communication by server to client. Any type of response can have three distinct attributes: 
		multicast (to be sent to multiple clients), forward (to be sent to peer server) and update (return only after sending the response)
	*/
	BOOL m_bIsForward, m_bIsUpdate, m_bIsMulticast;

	// ClientHandlesPtrs* m_pClientHandles ;
	ClientHandlesPtrsIterator m_StartIt, m_EndIt;
	unsigned int m_NumberOfHandles;

	ConnectionsManager* m_pConnectionsManager;

	// Each response is associated with a server (local or remote). We store this value in construction.
	IPv4Address m_ServerIPv4Address;

	int m_ResponseType;
	// uv_write_t write_t;
	double m_RequestArrivalTime ;
	int m_ReferenceCount ;
	RequestProcessor* m_pRequestProcessor;

	void Initialize();

	// While constructing response we need to specify which version processor has stored the response so that client can decide how to process the response.
	// This is helpful in scenarios when for e.g. v1 processor sends response to all other clients and one of them is v2
	// Now since v1 woudn't know response format of v2 (and we are not supposed to modify older version processors while implementing next version)
	// v1 processor has to mention in response what version of protocol is the response, so that v2 client can decide 
	// (either process response if it is equipped with older processors, or reject response if it is from newer processors)

	/* C'tor for response being sent to client(s) to this server */
	void ConstructResponseForLocalClients(const uv_buf_t* response, int NumberOfClients, USHORT version /* Version of client who is creating/storing the Response */); 

	/* C'tor for response being forwarded to another server */
	void ConstructResponseForRemoteClients(const uv_buf_t* response, USHORT version /* Version of client who is creating/storing the Response */);

	public:
		/* USED BY CLIENT */
		int ForwardError ;
		BOOL bAddedToStat ;
		int ResponseSentCount;
		double QueuedTime;

		Response(const uv_buf_t* response, ClientHandlesPtrsIterator& StartIt, ClientHandlesPtrsIterator& EndIt, USHORT version /* Version of client who is creating/storing the Response */, BOOL bIsUpdate, RequestProcessor* pRequestProcessor, double RequestArrivalTime, ConnectionsManager* pConnectionsManager);
		~Response();

		double GetRequestArrivalTime();

		int GetResponseType();

		const uv_buf_t GetResponse();

		RequestProcessor* GetRequestProcessor();

		ConnectionsManager* GetConnectionsManager();
		IPv4Address GetServersIPv4Address(); 
		BOOL IsFatalErrorForLocallyConnectedClient();
		void SetReferenceCount(int ReferenceCount);
		int GetReferenceCount();

		BOOL IsMulticast();
		BOOL IsForward();
		BOOL IsUpdate();
};
