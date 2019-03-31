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

#include "Pulsar_SampleServer.h"
#include "RequestProcessor_v3.h"

RequestProcessor_v3::RequestProcessor_v3(USHORT version) : RequestProcessor_v2(version)
{

}

BOOL RequestProcessor_v3::ProcessRequest ()
{
	// Requests those are not changed, call v2 processor for them.

	return RequestProcessor_v2::ProcessRequest ();
}

void RequestProcessor_v3::ProcessDisconnection (ClientHandle& clienthandle, void* pSessionData)
{
	return;
}

RequestProcessor_v3 theRequestProcessor_v3_GlobalInstance(VERSION_3);
