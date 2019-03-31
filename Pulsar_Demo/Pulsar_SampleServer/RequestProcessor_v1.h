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
	Sample class declaration to demonstrate how request processing class(es) can be derived from class RequestProcessor of Pulsar Server Framework.

	Demo Protocol:
	class RequestProcessor_v1 to process VERSION_1 protocol requests.
	In this demo protocol we will have first byte as request/response code followed by message.

	For demo purpose we will have just two request codes: 
		REGISTER: To register request sending client with server
		ECHO: To send message to all connected clients

	And two response codes:
		REGISTERED: This will let client know that it is registered with server
		ECHOED: This will let client know that messahe echo has been sent to all connected clients
*/

#include <map>

// Demo requests
#define REGISTER		1
#define ECHO			2

// Demo responses
#undef REGISTERED
#define REGISTERED		3
#define ECHOED			4

// Version parameters
#define MAX_REQUEST_SIZE (128*1024) // Let's consider this as maximum request length that we can have for this version of protocol
#define MAX_RESPONSE_SIZE MAX_REQUEST_SIZE

class RequestProcessor_v1 : public RequestProcessor
{
	private:
		/* 
			Protocol version will be stored here. For this class it will be VERSION_1 and for derived classes it will be respective classes protocol version.
		*/
		USHORT m_Version;

		/* Place to store all client handles:
			We will store handles of all clients connected in the map below.
			As the Pulsar Framework can pick any instance of this class to process request, 
			this map should be accessible by all instances of this class hence static.
		*/
		static std::map<UINT64, IPv4Address> m_ClientsMap ;

		static Lock m_rwlClientsMapLock; // Request processor runs in threads so we need lock when trying to access the map
		int RequestsCount;

		/* Function to send message to all connected clients:
			In this sample processor as soon as we receive any message (through ECHO request) we'll send it to all other client
			This function will be called by ProcessEcho() 
		*/
		void SendToAllClients(uv_buf_t response);

		/* Function to get another instance:
			We must need to override this as Pulsar Framework need to call this to create multiple instances of this class.
			Since in constructor we've assigned version value to m_Version, we'll use it here as constructor parameter.
		*/
		virtual RequestProcessor_v1* GetAnotherInstance() { return new RequestProcessor_v1(m_Version); }

		/* Request processing functions:
			These methods will be called by ProcessRequest. 
			Since in this demo server we are having only two requests, we have only two functions processing those requests.
		*/
		int	ProcessRegister();
		void ProcessEcho();

	public:
		/* Constructor of derived processor
		*/
		RequestProcessor_v1(USHORT version);

		/* Destructor of derived processor
		*/
		virtual ~RequestProcessor_v1(); 

		/* Framework calls this when client disconnects
		*/
		virtual void ProcessDisconnection (ClientHandle& clienthandle, void* pSessionData);

		/* Framework calls this when client send server a request 
		*/
		virtual BOOL ProcessRequest ();
};
