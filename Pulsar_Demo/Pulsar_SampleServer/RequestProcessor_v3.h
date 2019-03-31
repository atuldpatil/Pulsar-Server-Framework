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

	class RequestProcessor_v3 to process VERSION_3 protocol requests.
	If we've modified VERSION_2 protocol it will be VERSION_3.
	Thus clients working on VERSION_1, VERSION_2 protocol will not be affected as there is processor RequestProcessor_v1 and RequestProcessor_v2 respectively to process their requests.
	when VERSION_3 will be for clients with modified version. Thus framework supports multiple protocols at a time.

	Unlike RequestProcessor_v2 (which is derived from RequestProcessor), RequestProcessor_v3 is derived from RequestProcessor_v2.
	Benifit is that, only added/modified requests in v3 can be handled here. To handle rest of requests (which are unchanged) we can call methods of RequestProcessor_v2.

	In this sample, we do not have defined anything for VERSION_3 protocol. But here is just a placehoder class definition to handle VERSION_3.
*/

#include "RequestProcessor_v2.h"

class RequestProcessor_v3 : public RequestProcessor_v2
{
	private:

	public:
		RequestProcessor_v3(USHORT version);
		virtual ~RequestProcessor_v3() {}
		virtual BOOL ProcessRequest ();
		virtual void ProcessDisconnection (ClientHandle& clienthandle, void* pSessionData);
};
