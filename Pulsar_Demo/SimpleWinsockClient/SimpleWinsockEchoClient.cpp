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


#include "stdafx.h"

void PrepareBufferToSend(std::string Msg, char* pSendBuffer /*output*/, int* sendbuflen /*output*/);
void ExtractAndProcessResponse(char* recvbuf, int nread);
void KeepSending(SOCKET * pConnectSocket);
void KeepReceiving(SOCKET * pConnectSocket); 

int __cdecl main(int argc, char **argv) 
{
	int iResult;
    WSADATA wsaData;
    SOCKET ConnectSocket = INVALID_SOCKET;
    struct addrinfo *result = NULL,
                    *ptr = NULL,
                    hints;
    
    // Validate the parameters
    if (argc != 3) 
	{
        std::cout << "\nUsage: " << argv[0] << " server-name port\n";
        return 1;
    }

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) 
	{
        std::cout << "\nWSAStartup failed with error: " << iResult;
        return 1;
    }

    ZeroMemory( &hints, sizeof(hints) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    iResult = getaddrinfo(argv[1], argv[2], &hints, &result);
    if ( iResult != 0 ) 
	{
        std::cout << "\ngetaddrinfo failed with error: " << iResult;
        WSACleanup();
        return 1;
    }

    // Attempt to connect to an address until one succeeds
    for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) 
	{
        // Create a SOCKET for connecting to server
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, 
            ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) 
		{
            std::cout << "\nSocket failed with error: " << WSAGetLastError();
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect( ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) 
		{
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }

        break;
    }

    freeaddrinfo(result);

    if (ConnectSocket == INVALID_SOCKET) 
	{
        std::cout << "\nUnable to connect to server!\n";
        WSACleanup();
        return 1;
    }

	// spawn new thread that calls Receive
	std::thread ReceivingThread (KeepReceiving, &ConnectSocket);

	KeepSending(&ConnectSocket);

    // cleanup
    closesocket(ConnectSocket);
    WSACleanup();

    return 0;
}

BOOL SendBuffer(SOCKET* pConnectSocket, char* buffer, int buflen)
{
	// Send buffer
	int iResult = send( *pConnectSocket, buffer, buflen, 0);
	if (iResult == SOCKET_ERROR) 
	{
		std::cout << "\nSend failed with error: " << WSAGetLastError();
		closesocket(*pConnectSocket);
		WSACleanup();
		return iResult;
	}

	return iResult;
}

void KeepSending(SOCKET * pConnectSocket)
{
	int iResult;
	char sendbuf[MAX_BUFFER_SIZE];
	int sendbuflen = MAX_BUFFER_SIZE;

	// First send registration request
	std::string register_request;
	register_request.push_back(REGISTER);

	PrepareBufferToSend(register_request, sendbuf, &sendbuflen);
	iResult = SendBuffer(pConnectSocket, sendbuf, sendbuflen);

	if (iResult == SOCKET_ERROR)
		return;

	while(1)
	{
		// Prepare buffer
		std::string msg_to_broadcast;
		msg_to_broadcast.push_back(ECHO);

		std::string Msg;
		// std::cout << "\nPlease enter string to send: ";
		// std::cin >> Msg;
		std::getline(std::cin, Msg); // This reads whitespaces

		msg_to_broadcast = msg_to_broadcast + Msg;

		PrepareBufferToSend(msg_to_broadcast, sendbuf, &sendbuflen);
		iResult = SendBuffer(pConnectSocket, sendbuf, sendbuflen);

		if (iResult == SOCKET_ERROR)
			break;

		std::cout << "\nMessage sent (" << iResult << ") bytes)";
	}

	// shutdown the connection since no more data will be sent
	iResult = shutdown(*pConnectSocket, SD_SEND);
	if (iResult == SOCKET_ERROR) 
	{
		std::cout << "\nShutdown failed with error: " << WSAGetLastError();
		closesocket(*pConnectSocket);
		WSACleanup();
		return;
	}
}

void PrepareBufferToSend(std::string msg_to_send, char* pSendBuffer /*output*/, int* sendbuflen /*output*/)
{
	USHORT Version_n = htons(PROTOCOL_VERSION);
	UINT RequestLen_n = htonl(msg_to_send.length());

	memcpy(pSendBuffer, MSG_PREAMBLE, PREAMBLE_BYTES_SIZE);
	memcpy(&pSendBuffer[PREAMBLE_BYTES_SIZE], (UCHAR*)&Version_n, VERSION_BYTES_SIZE);
	memcpy(&pSendBuffer[PREAMBLE_BYTES_SIZE+VERSION_BYTES_SIZE], (UCHAR*)&RequestLen_n, SIZE_BYTES_SIZE);
	memcpy(&pSendBuffer[PREAMBLE_BYTES_SIZE+VERSION_BYTES_SIZE+SIZE_BYTES_SIZE], msg_to_send.c_str(), msg_to_send.length()); 

	(*sendbuflen) = HEADER_SIZE + msg_to_send.length();
}

void KeepReceiving(SOCKET * pConnectSocket) 
{
	int nread;

    // Receive until the peer closes the connection
    do 
	{
		char recvbuf[MAX_BUFFER_SIZE];
		unsigned int recvbuflen = MAX_BUFFER_SIZE;

        nread = recv(*pConnectSocket, recvbuf, recvbuflen, 0);

        if ( nread == 0 )
		{
            std::cout << "\nConnection closed\n";
			return ;
		}
        else if (nread < 0)
		{
            std::cout << "\nrecv failed with error: " << WSAGetLastError();
			return ;
		}

		// Receive response
		ExtractAndProcessResponse(recvbuf, nread);

		// After we process received response, keep our prompt displayed for user input
		std::cout << "\n\nPlease enter string to send: ";
    } while( nread > 0 );
}

void ExtractAndProcessResponse(char* recvbuf, int nread)
{
	char recvdresponse[MAX_BUFFER_SIZE];
	unsigned int recvdresponse_index = 0;

	// First get all the incoming bytes to response buffer
	int SizeAvailable = MAX_BUFFER_SIZE - recvdresponse_index;
	if (SizeAvailable < nread)
	{
		std::cout << "\nFATAL: No size available to read response";
		return;
	}

	memcpy (&recvdresponse[recvdresponse_index], recvbuf, nread);
	recvdresponse_index += nread;

	while(1)
	{
		// See if we got at least header size bytes else return
		if (recvdresponse_index < (HEADER_SIZE+1))
			return;

		/* Validate header */

		// Validate preamble
		for (int i=0; i<PREAMBLE_BYTES_SIZE; i++)
		{
			if (recvdresponse[i] != MSG_PREAMBLE[i])
			{
				recvdresponse_index=0;
				std::cout << "\nERROR: Invalid preamble in received response.";
				return;
			}
		}

		// Validate version
		USHORT Version_n = *(USHORT*) &recvdresponse[PREAMBLE_BYTES_SIZE];
		USHORT Version = ntohs(Version_n);

		// Protocol version should be greater than or equal to zero and less than or equal to current supported version 
		bool bErrorOccured = false ;
		if (Version == SPECIAL_COMMUNICATION)
		{
			unsigned char ResponseCode = recvdresponse[HEADER_SIZE]; // == RESPONSE_ERROR);
			switch(ResponseCode)
			{
				case RESPONSE_KEEP_ALIVE:
					std::cout << "\nKeep alive received";
					break;

				case RESPONSE_ERROR:
					bErrorOccured = true ;
					break;

				default:
					recvdresponse_index=0;
					std::cout << "\nERROR: Invalid response code " << ResponseCode << " received via SPECIAL_COMMUNICATION";
					return;
			}
		}
		else if ((Version != PROTOCOL_VERSION) || (Version == UNINITIALIZED_VERSION)) 
		{
			recvdresponse_index=0;
			std::cout << "\nERROR: Invalid version received in response";
			return;
		}

		// Verify response size with max response size allowed by response processr of related version
		unsigned int* pReqSize ;
		pReqSize = (unsigned int*) &recvdresponse[PREAMBLE_BYTES_SIZE+VERSION_BYTES_SIZE];

		unsigned int ReqSize_n = *pReqSize;
		unsigned int ReqSize = ntohl (ReqSize_n);

		if ((ReqSize) > MAX_BUFFER_SIZE) 
		{
			recvdresponse_index=0;
			std::cout << "\nERROR: Invalid size received in response";
			return;
		}

		// If header is valid but we don't have yet enough response bytes as mentioned in header (msg_size), 
		//      return (we gotto wait for more bytes)
		if (recvdresponse_index < (ReqSize + HEADER_SIZE))
		{
			return;
		}

		if (bErrorOccured) // If version field has indicated error, error code resides in first byte of message body 
		{
			std::cout << "\nError message received from server";
		}
		else
		{
			//  Now that everything is verified, we just need to extract response off and process
			char* message_received = (char*)&recvdresponse[HEADER_SIZE]; // We are giving processor response after stripping off header
			message_received[ReqSize] = NULL;

			if (Version != SPECIAL_COMMUNICATION)
			{
				if (message_received[0] == REGISTERED)
				{
					std::cout << "\nRegistered to server";
				}
				else if (message_received[0] == ECHOED)
				{
					std::cout << "\nMessage received: " << &message_received[1];
				}
				else
				{
					std::cout << "\nERROR: Unknown response received";
				}
			}
		}

		// Remove first (*msg_size + HEADER_SIZE) bytes from the response buffer
		for (unsigned int i=(ReqSize + HEADER_SIZE); i<recvdresponse_index; i++)
		{
			recvdresponse[i-(ReqSize + HEADER_SIZE)] = recvdresponse[i];
		}

		// Adjust response index acordingly
		recvdresponse_index -= (ReqSize + HEADER_SIZE);
	}
}
