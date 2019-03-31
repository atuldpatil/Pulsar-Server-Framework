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

Main class used by server application to instantiate server instance and start listening.
Application need to use only some of public methods of this class.
Please refer demo application to see how to use this class to create your server.
*/

class DLL_API ConnectionsManager:public LocalClientsManager, PeerServersManager
{
	private:
		Logger* m_pLogger;
		WriteToFile* m_pWriteToFile;

		BOOL m_bIsServerShutDown;
		uv_rwlock_t m_rwlResponseDirectionFlagLock; // Used by threads to add remove responses to queue and clients to set
		uv_rwlock_t m_rwlMemoryAllocationErrorCounter;
		uv_rwlock_t m_rwlResponseCountersLock1, m_rwlResponseCountersLock2; // Used by threads as well as event loop to increase/decrease response related counters

		uv_tty_t tty;
		uv_timer_t tick;

		char keyboard_buffer[KEYBOARD_BUFFER_LEN];

		UCHAR ValidateProtocolAndExtractRequest (char* input_buffer/* In */, unsigned int buffer_index /* In */, USHORT& ExistingVersion /* In & Out */, Buffer& request_out /* Out */);
		void AddResponseDetailsToServerStat(Response* pResponse, int ResponseReferenceCount);
		static void read_stdin(uv_stream_t *stream, ssize_t nread, const uv_buf_t* buf); 
		static void on_timer(uv_timer_t *req);
		static void alloc_keyboard_buffer(uv_handle_t *handle, uv_buf_t* buf); 
		static void after_close_read_stdin (uv_handle_t* handle);
		static void after_timer_closed(uv_handle_t* handle);

		void GetCopyOfServerStat (ServerStat& stServerStatCopy);
		static BOOL CtrlHandler( DWORD fdwCtrlType );
		int GetResponsesInQueue();
		void DoPeriodicActivities();
		void SendResponses();
		int AddResponseToQueues(class Response* pResponse, ClientHandlesPtrs* pClientHandlePtrs, BOOL& bHasEncounteredMemoryAllocationException);
		void AfterSendingResponse(Response* pResponse, stNode* pNode, int status);
		void Shutdown();

	protected:
		void LogStat(BOOL bCheckForRedundancy);

	public:
		ConnectionsManager();
		~ConnectionsManager();

		const char* GetErrorDescription(int errorcode);
		int StartServer (char* IPAddress, unsigned short int IPv4Port, bool bDisableConsoleWindowCloseButton = true);  // Calls LocalClientsManagers StartListening
		INT64 GetMemoryConsumptionByResponsesInQueue();
		void IncreaseExceptionCount(BOOL bType, char* filename, int linenumber); 
		void StopServer (); 
		VersionParameters* GetVersionParameters(USHORT version);

		static void after_send_responses(uv_write_t* write_req, int status);
		static double GetHighPrecesionTime();
		static long long GetProcessPrivateBytes(); 
};
