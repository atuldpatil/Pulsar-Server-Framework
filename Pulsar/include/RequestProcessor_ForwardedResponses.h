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
	Frameworks own defined request processor for it's internal purpose. 
	It is essentialy to process requests by peer servers (which are essentially responses forwarded to 
	this server as recepient of those responses are clients connected to this server)
*/

#include "Pulsar.h"

// Pulsar Framewors's inbuilt request processor to handle forwarded responses.
class RequestProcessor_ForwardedResponses : public RequestProcessor
{
	USHORT m_Version;
	// Disconnection of client will be processed here. This is invoked by thread  
	// Just before stClient object is deleted after disconnection (ProcessDisconnect)
	void ProcessDisconnection (ClientHandle& clienthandle, void*);

	// We need this method because version parameters for this class are not constant (unlike other derived request processors)
	// but they are calculated based on other factor. This is why to base class constructor we cannot pass constants but have to make a call this function.
	VersionParameters& CalculateVersionParameters ();

	// This should return TRUE if it processes request successfully, FALSE otherwise 
	BOOL ProcessRequest ();

	RequestProcessor_ForwardedResponses* GetAnotherInstance() { return new RequestProcessor_ForwardedResponses(m_Version); }

public:
	RequestProcessor_ForwardedResponses(USHORT version);
};
