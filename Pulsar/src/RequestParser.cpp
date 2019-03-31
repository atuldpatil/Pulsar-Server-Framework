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

/* Validates input 'request' (size request.len & valid upto request_index) and (if valid) gives back Request object (through last parameter)
	Returns:
	WAIT_FOR_MORE: If request buffer has not enough bytes
	REQUEST_FOUND: If a valid request was found and Request object was created with it
	INVALID_PREAMBLE || INVALID_VERSION || INVALID_SIZE: When errors in header found
*/

RequestParser* RequestParser::m_pRequestParserInstance = NULL;
ConnectionsManager* RequestParser::m_pConnectionsManager = NULL;

RequestParser::RequestParser()
{
	ASSERT(m_pRequestParserInstance == NULL); // Only one instance allowed
	m_pRequestParserInstance = this;

	m_pConnectionsManager = NULL;
}

RequestParser::~RequestParser()
{
}

RequestParser* RequestParser::GetInstance(ConnectionsManager* pConnectionsManager)
{
	ASSERT_MSG ((m_pRequestParserInstance != NULL), "ERROR: RequestParser instance not found"); // Must have been instantiated before calling this

	m_pConnectionsManager = pConnectionsManager;
	return m_pRequestParserInstance;
}

VersionParameters* RequestParser::GetVersionParameters(USHORT version)
{
	ASSERT(m_pConnectionsManager != NULL); // Must have invoked GetInstance before calling this
	return m_pConnectionsManager->GetVersionParameters(version);
}

/*
	When we want to allow applicaions to parse requests, we will make ValidateProtocolAndExtractRequest virtual and then application need to create instance of derived class
	(Just like logger does now). However, forwarded requests still use MAI protocol and that is challange as of now we need to address if we want aplications to define their own protocol.
*/
UCHAR RequestParser::ValidateProtocolAndExtractRequest (char* input_buffer/* In */, unsigned int buffer_length /* In */, USHORT& ExistingVersion /* In & Out */, Buffer& request_out /* Out */)
{
	ADD2PROFILER;
	
	// buffer_length is maximum size(in bytes) of input_buffer
	// See if we got at least (header size+1) bytes (other than header, at least one more byte (actual byte havinng request) is needed for a request to be considered complete) else return.
	if (buffer_length < (HEADER_SIZE+1))
		return WAIT_FOR_MORE_BYTES; // request bytes are yet less than minimum required (HEADER_SIZE+1)

	/* Validate header */

	// Validate preamble
	for (int i=0; i<PREAMBLE_BYTES; i++)
	{
		if (input_buffer[i] != MSG_PREAMBLE[i])
		{
			return INVALID_HEADER;
		}
	}

	// Validate version
	USHORT Version_n = *((USHORT*) &input_buffer[PREAMBLE_BYTES]);
	USHORT Version = ntohs (Version_n);

	int MaxVersionNumber = MAX_VERSION_VALUE;
	ASSERT (MaxVersionNumber > UNINITIALIZED_VERSION);

	// Protocol version should less than or equal to current supported version and should be other than UNINITIALIZED_VERSION value
	// Also, version should be equal to already initialized value (if it is) [Change in version on the fly (by stClient) could hamper already running processing threads]
	if ((Version > 0xFFFF) || (Version == UNINITIALIZED_VERSION) || ((ExistingVersion != UNINITIALIZED_VERSION) && (ExistingVersion != Version))) 
	{
		return INVALID_VERSION;
	}

	ExistingVersion = Version;

	// Verify request size with max request size allowed by request processr of related version
	unsigned int* pReqSize ;
	pReqSize = (unsigned int*) &input_buffer[PREAMBLE_BYTES+VERSION_BYTES];

	unsigned int ReqSize_n = *pReqSize;
	unsigned int ReqSize = (UINT) ntohl (ReqSize_n);

	// ReqSize should not be zero. There should be at least a byte other than header.
	if (ReqSize == 0)
	{
		return INVALID_SIZE;
	}

	VersionParameters* pVersionParameters = GetVersionParameters(ExistingVersion);

	// Version is NOT one of those invalid versions. Still if associated version parameters are not available, we shouln't treat it as valid version.
	if (!pVersionParameters)
	{
		LOG (ERROR, "Request processor is not available for version 0x%X. This is being treated as invalid version and can result in disconnection of relevent clients.", ExistingVersion);
		return INVALID_VERSION;
	}

	unsigned int MaxRequestSize = pVersionParameters->m_MaxRequestSize;

	if (ReqSize > MaxRequestSize) 
	{
		return INVALID_SIZE;
	}

	request_out.len = ReqSize;

	// If header is valid but we don't have yet enough request bytes as mentioned in header (msg_size), 
	//      return (we gotto wait for more bytes)
	if ((ReqSize + HEADER_SIZE) > buffer_length)
	{
		return WAIT_FOR_MORE_BYTES;
	}

	unsigned int TotalRequestSize = HEADER_SIZE + ReqSize ;
	ASSERT (TotalRequestSize <= buffer_length);

	//  Now that everything is verified, we just need to extract request off and process
	request_out.base = (char*)&input_buffer[HEADER_SIZE]; // We are giving processor request after stripping off header

	return REQUEST_FOUND;
}

RequestParser globalRequestParserInstance; // Remove this when we start allow application to define their own protocol. In that case app would create its own instance of derived class.