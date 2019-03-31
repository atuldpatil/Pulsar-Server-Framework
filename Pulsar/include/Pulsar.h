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

Main header file of the framework. It contains macro definitions and also includes other essential header files needed by framework.
*/

// Pulsar.h: Main include file of Pulsar Server Framework
//
#pragma once

/* 
	Note: Size of short and int are same (16-bit and 32-bits respectively) on both 32-bit and 64-bit systems. 
	More info:
	For 32-bit systems, the 'de facto' standard is ILP32 - that is, int, long and pointer are all 32-bit quantities.
	For 64-bit systems, the primary Unix 'de facto' standard is LP64 - long and pointer are 64-bit (but int is 32-bit). 
	The Windows 64-bit standard is LLP64 - long long and pointer are 64-bit (but long and int are both 32-bit).
*/
#define MSG_PREAMBLE "MAI" // Message And Information (Preamble)


// Master Protocol Header:
// Below is master protocol header which comprises of only three fields PREAMBLE_BYTES, VERSION_BYTES & SIZE_BYTES having sizes 3+2+4 respectively. 
// Server appliction don't need to worry about it as framework takes care of parsing it. 
// Only clients have to consider it when sending request and receiving response to/from Pulsar based server application.
#define PREAMBLE_BYTES (sizeof(MSG_PREAMBLE)-1) // These bytes contains MSG_PREAMBLE (-1 because sizeof returns length including last null character)
#define VERSION_BYTES sizeof(USHORT) // These bytes hold protocol version value.
#define SIZE_BYTES sizeof(unsigned int) // Actual message body size (Excluding header size)


// Master protocol related macros
#define HEADER_SIZE (PREAMBLE_BYTES+VERSION_BYTES+SIZE_BYTES) // Used by framework code
#define HANDLE_BYTES sizeof(unsigned int) // Only used by framework's internal communication.
										  // This appears in requests received as forwarded response by another server (with version as SPECIAL_COMMUNICATION)
										  // It stores size of number of handles response being forwarded to. Actual handles are part of message body.
/*
In client server communication there can be ordinary communication (application driven and with application defined version)
or special communication (framework driven such as keep alive, error, forward, acknowledgement of forward etc).
So SPECIAL_COMMUNICATION is reserved version indicating it is framwork predefined message (single byte after header).
Following are types of SPECIAL_COMMUNICATION used by framework:
	KEEP_ALIVE: Server To Client: RESPONSE
	FORWARDED_RESPONSE: Server(acting as Client) To Server: REQUEST
	ACKNOWLEDGEMENT_OF_FWD_RESP: Server To Server(acting as Client): RESPONSE
	ERROR: Server To Client: RESPONSE
	FATAL_ERROR: Server To Client: RESPONSE
Thus there is single REQUEST and four RESPONSES when it comes to SPECIAL_COMMUNICATION. 
Therefore, following are response codes (appear in single byte after header) allocated for response having SPECIAL_COMMUNICATION version.
	00: KEEP_ALIVE (To be received by client. Client no need to act upon. This is used by framework to identify and disconnect zombie connections.)
	01: ERROR (To be received by client. (Total message size is two bytes. ERROR and error code)
	02: ACKNOWLEDGEMENT_OF_FWD_RESP (To be received only by PeerServer reader)
	03: FATAL_ERROR (To be intrepretted and used internally by framework to disconnect client before sending the response. Thus client actually never receives it.)
*/
#define SPECIAL_COMMUNICATION		(0xFFFF) // Master protocol reserved version value (Version field in response indicating 0xFFFF indicates special communication protocol)

/* Code values (single byte) followed by version SPECIAL_COMMUNICATION */
// REQUEST Codes (Communicated by Client to Server):
// (nil): ForwardedResponse (No request code needed as there is only single SPECIAL_COMMUNICATION request as discussed above
//
// RESPONSE Codes (Communicated by Server to Client when version is SPECIAL_COMMUNICATION):
#define RESPONSE_KEEP_ALIVE 0 // 00: Keep Alive
#define RESPONSE_ERROR		1 // 01: Error (Next byte contains application defined eror code)
#define RESPONSE_ACKNOWLEDGEMENT_OF_FORWARDED_RESP 2 // 02: AckOfFwd
#define RESPONSE_FATAL_ERROR 3 // 03: FatalError

#define RESPONSE_ORDINARY 0xFF // Above codes will be treated as response types when version is SPECIAL_COMMUNICATION else type would be considered as ordinary

// #define MEMORY_FOOTPRINT_DEBUG // Turn on for memory footprint debugging


// Other version value related macros
#define UNINITIALIZED_VERSION 0 // Default version after client connects
#define DEFAULT_VERSION UNINITIALIZED_VERSION
#define MAX_VERSION_VALUE (SPECIAL_COMMUNICATION-1)


// Message buffering related
#define KEYBOARD_BUFFER_LEN 64		// Max keystrokes buffer can have. Normally we'll get callback every keystrokes unles event loops isn't too busy


// Request processing related
#define REQUESTCOUNT 1
#define RESPONSECOUNT 2
#define MAX_WORK_THREADS MAX_THREADPOOL_SIZE


// Memory related
#define DEL(ptr) {if(ptr){delete ptr;ptr=NULL;}}
#define DEL_ARRAY(ptr) {if(ptr){delete[] ptr; ptr=NULL;}}


// Return codes by ValidateProtocolAndExtractRequest after parsing request
#define REQUEST_FOUND		0
#define INVALID_HEADER		1
#define INVALID_VERSION		2
#define INVALID_SIZE		3
#define WAIT_FOR_MORE_BYTES	7


// Exception handling
#define MEMORY_ALLOCATION_EXCEPTION		1
#define REQUST_CREATION_EXCEPTION		2
#define RESPONSE_CREATION_EXCEPTION		3
#define CLIENT_CREATION_EXCEPTION		4
#define CONNECTION_CREATION_EXCEPTION	5


// Return code by server pool (when forwarding response)
#define CONNECTION_CONNECTING			1
#define CONNECTION_CONNECTED			2
#define CONNECTION_DISCONNECTING		3
#define CONNECTION_UNINITIATED			4 // Only initially (when connection attempt was never made)
#define CONNECTION_DISCONNECTED			5 // For any reason when we disconnect (coz of overflow, write failure, connection attempt failed) we must set status to CONNECTION_FAILED
#define CONNECTION_CONNECTING_TIMED_OUT 6
#define CONNECTION_OVERFLOWED			7


// Wait durations
#define RETRY_CONNECTION_AFTER		30  // Time in seconds. If connection attempt to other server was failed, this is minimum time duration after which it can be tried to reconnect.
#define MAX_OVERFLOWED_TIME			90  // Time in seconds. If no acknowledgements received from other server (server was overflowed), this is time limit after which peer server will be disconnected.
#define WAIT_FOR_CONNECTION		   150  // If connection (to other server) was in CONNECTION_CONNECTING state, this is max time for which response could be hold waiting for connection.


#include "targetver.h"
#include <stdio.h>
#include <conio.h>
#include <tchar.h>
#include <stdlib.h>
#include <ASSERT.h>
#include <typeinfo.h>
#include <time.h>

/*
Notes for future development for Pulsar Server Framework:

Amongst std::map, std::unordered_map and boost::unordered_map we found boost version highest performer 
(especially for value increment which happens in event loop for each request)
Below are #Requests per second (from ELTT counter) handled by event loop for these libraries
boost::unordered_map : 142000 to 162000
std::unordered_map : 140000 to 153000 (After having _SECURE_SCL=0;_HAS_ITERATOR_DEBUGGING=0 predefined directives. Without it performance is heavily degraded)
std::map : 120000 to 143000 (After having _SECURE_SCL=0;_HAS_ITERATOR_DEBUGGING=0 predefined directives. Without it performance is heavily degraded)

#include <boost/unordered_map.hpp> 
*/
#include <iostream>
#include <memory>
#include <string>
#include <queue>
#include <map>
#include <set>
#include <unordered_map>
#include <fstream> 

#include "..\LIBUV\libuv-v1.7.5\include\uv.h"
#include <psapi.h>

// #define NO_WRITE
// #define GENERATE_PROFILE_DATA // Defining this would generate function call profile data but it slows down the server
#define ADD2PROFILER Profiler TEMPPROFILEROBJECT(__FUNCTION__);

// We must define separate macros for import and export. If we do not we end up in linker error undefined symbol, viz. for static variable clIPv4Address::Port, 
// even after we've define it in LocalClientsmanager.cpp.

#ifdef DLL_EXPORTS
#define DLL_API __declspec(dllexport)
#else
#define DLL_API __declspec(dllimport)
#endif

#pragma warning(disable: 4251) // Disable warning: 'needs to have dll-interface to be used by clients'

#include "TypeDefinitions.h"
#include "Logger.h"
#include "WriteToFile.h"

#define htonll(x) ((1==htonl(1)) ? (x) : ((uint64_t)htonl((x) & 0xFFFFFFFF) << 32) | htonl((x) >> 32))
#define ntohll(x) ((1==ntohl(1)) ? (x) : ((uint64_t)ntohl((x) & 0xFFFFFFFF) << 32) | ntohl((x) >> 32))

// TO HANDLE WRITE ERRORS
#define WRITE_OK	0L

// LOGGING RELATED
// When NO_WRITE is defined and server is bombarded with requests, SendResponse stays most busy and in that case DoPeriodicActivities function takes as max as 0.2 seconds
// Hence to not to overkill timer let's set threshold to what it can take in worst case
#define TIMER_INTERVAL_IN_MILLISECONDS 201

//  Sample worst case message for length estimation (e.g. Status message) length could go upto 256 bytes
//	#Requests received 18446744073709551615 #Requests responded 18446744073709551615 #Clients connected 50000 #Memory consumption: 8388608 KB (8192 MB) #ELTAT 1000 milliseconds #RPT 1000 miliseconds #Requests pending in the queue 10240
#define DEFAULT_LOG_MSG_LENGTH 256 // Original value 256
// Most log message should fit in DEFAULT_LOG_MSG_LENGTH. But if any message exceeds, we should try increments of LOG_MSG_INCREMENT bytes
#define LOG_MSG_INCREMENT 64	// Original value 64
// #define LOG (Logger::GetInstance()->LogMessage) 

// Types
#undef ERROR
#undef IGNORE
#define INFO	1 // Info and Debug logs will be processed at each LogStat call and will be erased
#define NOTE	2 // Notes will be preserved forever
#define ERROR	3 // Errors will be preserved forever
#define EXCEPTION	4 // Warnings and Errors will be preserved forever
#define DEBUG	5 // Info and Debug logs will be processed at each LogStat call and will be erased
#define IGNORE  6 // To selectively ignore any log line (viz Ignoring unwanted Debug logs without commenting them out)
#define ASSERTION 7

#define PROCESS_DEBUG_LOGS FALSE // If we want to turn ON/OFF all debug logs

#define LOG(Type, Format, ...) Logger::GetInstance()->LogMessage(Type, __FILE__, __LINE__, __FUNCTION__, Format, ##__VA_ARGS__)

// Assertions are to be invoked ONLY when there is abnormal circumstances occurred under which it is not good to keep server running (It normally indicates flaw in logic etc.)
// In order that before exiting, server should log the failure through logger (so that assertions can be logged to database also), 
// we will wait for sometime (so that logger thread gets enough time to process the message) before exiting.
#define ASSERT(_expression) if((_expression)==FALSE){printf ("\n\nAssertion failed at line number %d in file %s.\n\nPress a key to exit...\n", __LINE__, __FILE__);_getch();_getch();exit(-1);}
#define ASSERT_MSG(_expression, _msg) if((_expression)==FALSE){printf ("\n\n%s\nAssertion failed at line number %d in file %s.\n\nPress a key to exit...\n", _msg, __LINE__, __FILE__);_getch();_getch();exit(-1);}
// To throw exceptions from constructors
#define ASSERT_THROW(_expression, _msg) if((_expression)==FALSE) {char _throw_msg[DEFAULT_LOG_MSG_LENGTH]; sprintf_s (_throw_msg, DEFAULT_LOG_MSG_LENGTH, "\n\nException: %s. At line number %d in file %s", _msg, __LINE__, __FILE__); throw _throw_msg;}
// To log errors after checking value
#define ASSERT_RETURN(_expression) {if(_expression) return _expression;}

#include "CommonComponents.h"
#include "ClientsPool.h"
#include "LocalClientsManager.h"
#include "PeerServersManager.h"
#include "ConnectionsManager.h"
#include "RequestResponse.h"
#include "RequestProcessor.h"
#include "RequestProcessor_ForwardedResponses.h"
#include "RequestParser.h"
#include "Profiler.h"