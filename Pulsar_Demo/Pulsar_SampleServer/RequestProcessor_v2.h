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

	class RequestProcessor_v2 to process VERSION_2 protocol requests.
	If we've modified VERSION_1 protocol it will be VERSION_2.
	Thus clients working on VERSION_1 protocol will not be affected as there is processor RequestProcessor_v1 to process their requests
	when VERSION_2 will be for clients with modified version. Thus framework supports multiple protocols at a time.

	In this sample, we do not have defined anything for VERSION_2 protocol. But here is just a placehoder class definition to handle VERSION_2.
*/

// Version parameters
#define MAX_REQUEST_SIZE (256*1024) // Let's consider this as maximum request length that we can have for this version of protocol
#define MAX_RESPONSE_SIZE MAX_REQUEST_SIZE

class RequestProcessor_v2 : public RequestProcessor
{
	private:
		USHORT m_Version;

		virtual RequestProcessor_v2* GetAnotherInstance() { return new RequestProcessor_v2(m_Version); }

	public:
		RequestProcessor_v2(USHORT version);

		virtual ~RequestProcessor_v2() {}

		virtual void ProcessDisconnection (ClientHandle& clienthandle, void* pSessionData);

		virtual BOOL ProcessRequest ();
};
