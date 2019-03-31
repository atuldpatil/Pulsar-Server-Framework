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
	Logger is class that handles logs. It essentially creates a thread that processes logs logged by framework as well as by server application
	Server application:
		1. Needs to derive this class and create static global instance of the derived class.
		2. Can call Logger::GetInstance()->LogMessage OR simply can use LOG macro defined in Pulsar.h to log errors/warnings/info etc.
		3. Needs to derive and write its own defination of ProcessLog which is virtual here. ProcessLog is called by log processing thread.
*/


class DLL_API Logger
{
		static Logger* m_LoggerInstance;

		uv_work_t m_log_work_t; // For thread log_processing_thread

		// To log structures that has server statistics 
		uv_rwlock_t m_rwlStatQueue;
		std::queue <ServerStat> m_StatQueue;

		// To log error messages
		uv_rwlock_t m_rwlErrorsMap;
		LoggerMap m_ErrorsMap;

		// To log exception messages
		uv_rwlock_t m_rwlExceptionsMap;
		LoggerMap m_ExceptionsMap;

		// To log notes
		uv_rwlock_t m_rwlNotesMap;
		LoggerMap m_NotesMap;

		// To log info messages (messages except Errors/Warnings/Debug)
		uv_rwlock_t m_rwlInfoMap;
		LoggerMap m_InfoMap;

		// To log debug messages
		uv_rwlock_t m_rwlDebugMap;
		LoggerMap m_DebugMap;
		
		void GetCopyAndProcessLog(ServerStat& stServerStat);
		void GetMapCopy (LoggerMap& Map, LoggerMap& MapCopy, uv_rwlock_t& Lock, BOOL bEraseOriginalMap=FALSE);
		void IncreaseMapCounter (LoggerMap& Map, int Type, std::string& Message, uv_rwlock_t& Lock);
		BOOL m_bStopLoggerThread, m_bLoggerThreadStopped;
		void ComputeAdditionalStat (ServerStat& stServerStat);
		void getClassName(const char* fullFuncName, std::string& csClassName);
		static void log_processing_thread (uv_work_t* log_work_t);
		static void after_log_processing_thread (uv_work_t* log_work_t, int status);

	protected:
		Logger();
		virtual void ProcessLog (ServerStat& stServerStat, LoggerMap& InfoMap, LoggerMap& NotesMap, LoggerMap& ErrorsMap, LoggerMap& ExceptionsMap, LoggerMap& DebugMap)=0;

	public:
		~Logger();
		static Logger* GetInstance();
		void LogMessage (int Type=NULL, char* FileName=NULL, int LineNumber=0, char* FunctionName=NULL, char* LogFormatMsg=NULL, ...);
		void LogStatistics (ServerStat& stServerStat);
		int Start(uv_loop_t* loop);
		BOOL Stop();
};
