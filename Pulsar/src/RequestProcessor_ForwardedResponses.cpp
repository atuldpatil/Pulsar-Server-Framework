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

RequestProcessor_ForwardedResponses::RequestProcessor_ForwardedResponses(USHORT version):RequestProcessor(version, CalculateVersionParameters())
{
	m_Version = version;
}

VersionParameters& RequestProcessor_ForwardedResponses::CalculateVersionParameters ()
{
	static VersionParameters VerParams;

	// For forward responses we have to choose maximum value of all versions, and then compute relevent values for forward

	// MaxRequestSize: For forwarded message came as a request, max request size would be in proportion with "max _response_ size" of all versions (Because response would be forwarded)

	// Forwarded response received by client connected to other server:
	//                      version (SenderClientVersion)   |   number of handles   |   handles | response
	//						version (SenderClientVersion)	|	number of handles	|	handles
	// Size:						short 2 bytes			+     int 4 bytes		+ (number of handles * ( 4 m_ClientIndex + 8 m_ClientRegistrationNumber))
	// Thus we have to find:
	// 1. maximum response length in all versions and 
	// 2. maximum handles in a response (based on max response per request)

	// Since in framework, application will create global objects of request processors, GetMaxResponseSizeOfAllVersions() will not work.
	// (GetMaxResponseSizeOfAllVersions() works only after application calls StartListening)
	// To avoid complixity we decided to define this constant and put checks on max request size set by application should not exceed this

	int MaxResponseSizeOfAllVersions = MAX_POSSIBLE_REQUEST_RESPONSE_SIZE; /* GetMaxResponseSizeOfAllVersions(); */

	// CommonParameters ComParams = GetCommonParameters();  
	// int MaxNumberOfHandles = ComParams.MaxClientsPerMulticast;  // Maximum handles in forwarded response would be maximum number of clients to be responded

	int MaxNumberOfHandles = MAX_HANDLES_IN_FORWARDED_RESPONSE;  // Maximum handles in forwarded response. If there are more handles, multiple responses would be created.

	VerParams.m_MaxRequestSize = 2 + 4 + (MaxNumberOfHandles*sizeof(((ClientHandle*)0)->m_ClientRegistrationNumber)/*We don't include IP address of server in forwarded message*/) + MaxResponseSizeOfAllVersions ;
	VerParams.m_MaxResponseSize = VerParams.m_MaxRequestSize; // Forwarded requests and responses are formed out of normal responses only (and not requests)

	return VerParams;
}

// This should return TRUE if it processes request successfully, FALSE otherwise 
BOOL RequestProcessor_ForwardedResponses::ProcessRequest ()
{
	// First, lets turn on streaming for forwarded responses
	SetStreamingMode(true);

	// Forwarded response received by client connected to other server:
	//                      version (SenderClientVersion)   |   number of handles   |   handles | response
	//						version (SenderClientVersion)	|	number of handles	|	handles
	// Size:						short 2 bytes			+     int 4 bytes		+ (number of handles * ( 4 m_ClientIndex + 8 m_ClientRegistrationNumber))
	
	uv_buf_t forwarded_response = GetRequest();

	// forwarded response must have minimum size to store Version and NumberOfHandles at least
	if (forwarded_response.len < (sizeof(USHORT)+sizeof(UINT)))
		return FALSE;

	USHORT Version_n = *((USHORT*) forwarded_response.base);
	USHORT Version = ntohs (Version_n);

	UINT NumberOfHandles_n = *((UINT*) &forwarded_response.base[sizeof(USHORT)]);
	UINT NumberOfHandles = ntohl (NumberOfHandles_n);

	ULONG MinimumLength = VERSION_BYTES /* version */ + HANDLE_BYTES /* number of handles */ + (NumberOfHandles * sizeof(UINT64)) /* Handles */ + 1 /* at least a byte of response */;

	// Response should have minimum size to store version, number of handles and actual handles (remaining size is actual response to be forwarded)
	// Else we might not have received complete response forwarded
	if (forwarded_response.len < MinimumLength)
	{
		LOG (ERROR, "Error: Forwarded response received has too short length");
		return FALSE;
	}

	try
	{
		// Collect handles
		ClientHandles clienthandles;

		ULONG index=sizeof(USHORT)+sizeof(UINT); // Initial index is after skipping bytes used to store 'version' and 'number of handles'
		for (UINT handle_count=0; handle_count<NumberOfHandles; handle_count++) 
		{
			// We should have enough room ahead to read first/next Registration number. Else we might have not received complete response forwarded.
			if ((index+sizeof (UINT64)) >= forwarded_response.len)
			{
				LOG (ERROR, "Error: Forwarded response received doesn't have all indices and registration numbers");
				return FALSE;
			}

			ClientHandle clienthandle;
			UINT64* ClientRegistrationNumber = (UINT64*) &forwarded_response.base[index];
			index += sizeof (UINT64);

			clienthandle.m_ClientRegistrationNumber = ntohll(*ClientRegistrationNumber);
			clienthandle.m_ServerIPv4Address = GetRequestSendingClientsHandle().m_ServerIPv4Address; // Assign local ip address and port

			clienthandles.insert(clienthandle); 
		}

		uv_buf_t response;

		response.base = &forwarded_response.base[index]; 
		response.len = forwarded_response.len - index;

		SendResponse(&clienthandles, &response, Version);

		char acknowledgement = RESPONSE_ACKNOWLEDGEMENT_OF_FORWARDED_RESP;
		ClientHandle clienthandle =	GetRequestSendingClientsHandle();
		response.base = &acknowledgement;
		response.len = 1;

		SendResponse (&clienthandle, &response, SPECIAL_COMMUNICATION);
	}
	catch(std::bad_alloc&)
	{
		LOG (EXCEPTION, "Exception bad_alloc while processing forwarded response"); 
	}

	return TRUE;
}

// Disconnection of client will be processed here. This is invoked by thread  
// Just before stClient object is deleted after disconnection (ProcessDisconnect)
void RequestProcessor_ForwardedResponses::ProcessDisconnection (ClientHandle& clienthandle, void* ptr)
{
	/*	
		Server just connects to and forwards/receives request from each other. 
		No DB or any other external entities involved to take care after disconnection.
		So there is nothing much to do here.
	*/

	return;
}

RequestProcessor_ForwardedResponses globalInstanceForForwardedResponsesClass(SPECIAL_COMMUNICATION); 
													// This version is reserved by Pulsar framework
													// When Client receives this as response, the command code could be KeepAlive, Error, AcknowledgeOfForwardedResponse
													// When Server receives this as request, currently it only means it has received forwarded response as request
