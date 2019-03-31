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
#include "RequestProcessor_v2.h"

RequestProcessor_v2::RequestProcessor_v2(USHORT version) : RequestProcessor(version, VersionParameters(MAX_REQUEST_SIZE, MAX_RESPONSE_SIZE))
{
	m_Version = version;
}

// Should return TRUE if request was processed successfully FALSE otherwise
BOOL RequestProcessor_v2::ProcessRequest ()
{
	return FALSE;	
}

void RequestProcessor_v2::ProcessDisconnection (ClientHandle& clienthandle, void* pSessionData)
{
	return;
}

RequestProcessor_v2 theRequestProcessor_v2_GlobalInstance(VERSION_2);