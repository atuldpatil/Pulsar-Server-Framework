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
	Application's request processor can call this function to write it's data into file.
	Application would want to call QueueFile method of this class. File writing thread then 
	picks up the file and will start processing.  If file exists it will overwrite else will create new file.
	Application's request processor can save its time by handing it over to WriteToFile
	as writing into file is time consuming operation.
*/

struct stFileNameAndData
{
	std::string csPath;
	std::string csFilePathName;
	std::string csFileData;
};

class DLL_API WriteToFile
{
		uv_work_t m_work_t; // One is enough as we having only one thread running for a WriteToFile instance

		uv_rwlock_t m_rwlFileQueue;
		std::queue <stFileNameAndData*> m_FileQueue;

		static void file_writing_thread (uv_work_t* work_t);
		static void after_file_writing_thread (uv_work_t* work_t, int status);

		BOOL m_bStopFileWritingThread, m_bFileWritingThreadStopped;

		void DeleteAllFiles(std::string folderPath);

		WriteToFile (uv_loop_t* loop); // WriteToFile is having single instance. (Because we cannot run uv_queue_work from threads)

	public:
		~WriteToFile (); // Application should call Stop before deleting WriteToFile
		static WriteToFile* GetInstance(uv_loop_t* loop=NULL);
		bool QueueFile (std::string& csPath, std::string& csFilePathName, const std::string& csFileData);
		BOOL Stop ();
};
