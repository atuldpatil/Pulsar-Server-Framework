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

This class holds information about clients connected to the server. 
Framework uses this class to add/remove client whenever client connects/disconnects. 
Also when it needs to add or remove request/response counts to the client.
*/

class ClientsPool
{
	std::map<UINT64, stClient*> m_ClientsMap;
	uv_rwlock_t m_ClientsMapLock;
	BOOL m_bIsServerShuttingDown;

	public:
		ClientsPool(); 
		~ClientsPool(); 
		BOOL AddClient(stClient* pClient);
		BOOL RemoveClient(stClient* pClient);
		int IncreaseCountForClient (ClientHandle* clienthandle, stClient* &pClient, BOOL RequestORResponse); // pClient is out param (See definition for details)
		int DecreaseCountForClient (stClient* pClient, BOOL RequestORResponse);
		void GetClients(Clients& vClients); 
		void GetIdleClients(ClientHandles& vClientHandles, ClientType Type); 
		// BOOL IsClientIdle(int ClientIndex, ClientHandle& clienthandle, USHORT& version); // Returns TRUE if stClient at ClientIndex is NOT NULL and has no pending requests and responses. FALSE otherwise.
		unsigned int GetClientsCount();
		void SetServerShuttingDown(); // Called only thru event loop after it reads keystrokes to shutdown
		BOOL IsShutdownInitiated();
};
