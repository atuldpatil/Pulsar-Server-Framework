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

class PeerServersManager is responsible to manage peer servers and routing responses to them (intended to clients connected to them)
Primary responsibilities are:
	1. Make connection with other peer server when it receives response for client connected to that server
	2. Creates stPeerServer object and add it to the m_ServersInfo map
	3. Validate incoming responses and finally send them to the clients connected to this server
	4. Validates requests (viz RESPONSE_KEEP_ALIVE, RESPONSE_ERROR or RESPONSE_ACKNOWLEDGEMENT_OF_FORWARDED_RESP) received by connected peer server
	5. Sends forwarded responses to appropriate peer server
*/


struct stPeerServer:public stNode
{
	void Construct(IPv4Address& oServerIPv4Address);
	stPeerServer(IPv4Address& oServerIPv4Address);
	stPeerServer(stPeerServer& oOriginalInfo); // We need to override copy constructor because the way stl map works (See definition for details)
	~stPeerServer();
	IPv4Address m_ServerIPv4Address;// Server has to have ipv4 address. Because many clients will be on ipv4. 
									// They won't be able to connect to server that has only ipv6.
									// Hence it is safe to use ipv4 for interserver communication.

	// Each server will send back a single byte of acknowledgement for each forwarded message.
	// Unlike requests received by clients, no need to have dynamically allocated buffer here (as the ack size here is fixed)
	char m_response_buffer[1024*(HEADER_SIZE+1)]; // Reading thousand acknowledgements at a time would serve reasonably good bandwidth for most common purpose servers. Change later if needed.
	uv_buf_t m_ResponseBuffer;
	unsigned int m_ResponseBufferIndex;

	USHORT m_Version;
	uv_tcp_t m_server;
	uv_stream_t* m_connection;
	uv_connect_t m_connect_req;
	int Status;
	int ResponsesForwarded;
	class PeerServersManager* m_pPeerServersManager;

	std::deque<class Response*>* m_ResponsesQueue1, * m_ResponsesQueue2;
	uv_rwlock_t m_rwlResponsesQueueLock;

	time_t OverflowedTime;
	time_t DisconnectedTime;
	time_t ConnectingTime;

	BOOL m_bResponseForwardingSucceededLastTime;

	uv_write_t m_write_req;
	std::vector<uv_buf_t> m_pResponsesBuffersBeingForwarded;
};

class DLL_API PeerServersManager:protected virtual CommonComponents
{
	int m_ServersConnected;

	// Performance: For small amounts of data, maybe 8 or 12 elements, sometimes it's faster to use vector than map
	// just to do a linear search over an array than to walk a binary search tree.
	// But as a longer term solution map is better (Considering tradeoff between vector and map for small vs large numbers)
	std::map<IPv4Address, stPeerServer*> m_ServersInfo;
	int m_ServersConnecting, m_ServersClosing ;
	uv_rwlock_t m_rwlServerSetLock, m_rwlServersInfoLock;
	std::set <stPeerServer*> m_RecevingServersSet1, m_RecevingServersSet2 ;
	void InitiateConnection(stPeerServer* pRemoteSvr);
	void ProcessResponse(uv_buf_t* response, stPeerServer* pPeerSvr);

	int GetServerConnection(stPeerServer* pPeerServer /* input */); // Called by event loop (SendPeerServersResponses)
	void DisconnectServer(stPeerServer* pPeerServerInfo, BOOL bReduceConnectedServersCount=TRUE); // Called by event loop
	void IncreaseForwardedResponsesCount (stPeerServer* pPeerServerInfo); // Called by event loop
	void GetRecevingServersSet (BOOL bResponseDirectionFlag, std::set<IPv4Address>* &pServersSet);
	BOOL AddToServerSet (std::set <stPeerServer*>* pServersSet, stPeerServer* pPeerServer, BOOL bTobeLocked);

	static void after_getaddrinfo(uv_getaddrinfo_t* gai_req, int status, struct addrinfo* ai);
	static void after_connect(uv_connect_t* connect_req, int status);
	static void on_server_closed(uv_handle_t* handle);
	static void alloc_buffer(uv_handle_t *handle, uv_buf_t* buffer);
	static void on_read(uv_stream_t* tcp_handle, ssize_t nread, const uv_buf_t* read_bytes);

	protected:
		BOOL AddResponseToQueue(class Response* pResponse, BOOL& bHasEncounteredMemoryAllocationException);
		void DisconnectAndCloseAllConnections(); // Called by event loop
		void SendPeerServersResponses();
		void AfterSendingPeerServersResponses(stPeerServer* pPeerServerInfo, Response* pResponse, int status);
		int GetServersConnectedCount();

	public:
		PeerServersManager ();
		~PeerServersManager ();
		
		int AreServersClosing();
		int AreServersConnecting();
};
