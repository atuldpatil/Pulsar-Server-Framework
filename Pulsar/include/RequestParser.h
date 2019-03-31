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
	RequestParser is used by LocalClientsManager to parse and validate incoming request by client.

	Default parser that validates and parses and extracts request out of default MAI protocol as under:
	Protocol:
		First three bytes: Premble: "MAI" (Messages And Information).
		Next two bytes: Protocol version: This must be greater than 0 and less than 0xFFFE (0 is treated as UNINITIALIZED_VERSION and 0xFFFE is version reserved by framework for SPECIAL_COMMUNICATION.
		Next four bytes: Size of actual request/response (Size excluding these 9 bytes of protocol header). Minimum value is 1.
*/

class DLL_API RequestParser
{
	static RequestParser* m_pRequestParserInstance;
	static ConnectionsManager* m_pConnectionsManager;

	public:
		RequestParser();
		~RequestParser();
		VersionParameters* GetVersionParameters(USHORT version);
		static RequestParser* GetInstance(ConnectionsManager* pConnectionsManager);

		// Validation of protocol. This module validates protocol.
		UCHAR ValidateProtocolAndExtractRequest (char* input_buffer/* In */, unsigned int buffer_length /* In */, USHORT& ExistingVersion /* In & Out */, Buffer& request_out /* Out */);
};