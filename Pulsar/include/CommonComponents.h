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

This is served as base class of classes LocalClientsManager and PeerServersManager
which are derived finally in single class ConnectionsManager which serves as main
class used by server application to create server instance and start listening.
*/


struct stNode
{
	protected:
		bool m_bIsServer;

	public:
		Responses* m_pResponsesBeingSent ;
		bool IsServer() 
		{
			return m_bIsServer;
		}
};

class DLL_API CommonComponents
{
	protected:
		uv_loop_t* loop;

		// Common data declaration: Common data needed by both ClientsManager and ServersManager
#ifdef GENERATE_PROFILE_DATA
		friend class Profiler;
		static uv_rwlock_t m_rwlFunctionProfiler;
		static ServerStat stServerStat; // Common structure for all clients to store statistical info
#else
		ServerStat m_stServerStat; // Common structure for all clients to store statistical info
#endif

		BOOL m_bResponseDirectionFlag;
		BOOL after_send_response_called_by_send_response;

		void ValidateCommonParamaters(CommonParameters& ComParam);

		// Common functions declaration: Common functions called by both ClientsManager and ServersManager. They will be defined in ConnectionsManager.
		virtual void DoPeriodicActivities()=0;
		virtual void SendResponses()=0;
		virtual void IncreaseExceptionCount(BOOL bType, char* filename, int linenumber)=0; 
		virtual int AddResponseToQueues(class Response* pResponse, ClientHandlesPtrs* pClientHandlePtrs, BOOL& bHasEncounteredMemoryAllocationException)=0;

	public:
		CommonComponents();
		~CommonComponents();
};