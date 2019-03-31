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

// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>


// TODO: reference additional headers your program requires here
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <iostream>       // std::cout
#include <thread>         // std::thread
#include <string>

// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

// RELATED TO MASTER PROTOCOL
#define MSG_PREAMBLE "MAI" // Message And Information
#define PREAMBLE_BYTES_SIZE (sizeof(MSG_PREAMBLE)-1) // -1 because sizeof returns length including last null character
#define VERSION_BYTES_SIZE sizeof(USHORT)
#define SIZE_BYTES_SIZE sizeof(unsigned int)
#define UNINITIALIZED_VERSION 0
#define HEADER_SIZE (PREAMBLE_BYTES_SIZE + VERSION_BYTES_SIZE + SIZE_BYTES_SIZE) 
#define SPECIAL_COMMUNICATION		(0xFFFF) // Reserved version value
#define RESPONSE_KEEP_ALIVE 0 // 00: Keep Alive (Communicated by Server to Client when version is SPECIAL_COMMUNICATION)
#define RESPONSE_ERROR		1 // 01: Error (Communicated by Server to Client when version is SPECIAL_COMMUNICATION)

// REQUESTS AND RESPONSES FOR DEMO PROTOCOL

// Demo requests
#define PROTOCOL_VERSION 1 // Current version of protocol

#define REGISTER	1
#define ECHO		2

// Demo responses
#undef REGISTERED
#define REGISTERED	3
#define ECHOED		4

#define MAX_BUFFER_SIZE (64*1024) // This should be equal to MAX_REQUEST_SIZE in server code

