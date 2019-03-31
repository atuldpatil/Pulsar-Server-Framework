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

#include "Pulsar.h"

/*
Please refer WriteToFile.h
*/
// Called from GetInstance (which is called through ConnectionsManager c'tor)
WriteToFile::WriteToFile(uv_loop_t* loop)
{
	uv_rwlock_init(&m_rwlFileQueue);

	m_work_t.data = this;

	m_bStopFileWritingThread = FALSE;
	m_bFileWritingThreadStopped = FALSE;

	int RetVal = uv_queue_work (loop, &m_work_t, file_writing_thread, after_file_writing_thread);
	ASSERT (RetVal == 0); // uv_queue_work returns non-zero only when disconnection_processing_thread is NULL
}

// Called from DoPeriodicActivities event
BOOL WriteToFile::Stop()
{
	m_bStopFileWritingThread = TRUE;

	if (m_bFileWritingThreadStopped) 
		return TRUE;
	else
		return FALSE;
}

WriteToFile::~WriteToFile()
{
	ASSERT (m_bFileWritingThreadStopped == TRUE);

	uv_rwlock_destroy(&m_rwlFileQueue);
}

// Called from ConnectionsManager c'tor
WriteToFile* WriteToFile::GetInstance(uv_loop_t* loop) 
{ 
	static WriteToFile* pInstance=NULL;

	if (pInstance) // Except first time, it will return quickly from here itself
		return pInstance;

	try
	{
		ASSERT(loop); // When calling first time make sure to call with valid loop value.
		pInstance = new WriteToFile(loop);
	}
	catch(std::bad_alloc&) // WriteToFile class contains STL containers. Hence we have to catch bad_alloc exception.
	{
		std::cout << "\nError allocating memory to WriteToFile.";
		// exit(-1); // No need to exit here. Caller has taken care of return value.
	}

	return pInstance ;
}

void WriteToFile::DeleteAllFiles(std::string folderPath)
{
	char fileFound[1024];

	WIN32_FIND_DATA info;
	HANDLE hp; 
	sprintf(fileFound, "%s\\*.*", folderPath.c_str());

	hp = FindFirstFile(fileFound, &info);
	do
	{
		sprintf(fileFound, "%s\\%s", folderPath.c_str(), info.cFileName);
		DeleteFile(fileFound);
	}while(FindNextFile(hp, &info)); 

	FindClose(hp);
}

/* ----------------------------------------------------------------------------------------------------------------------------------
WriteToFile thread keeps contineously running (It starts when WriteToFile gets instantiated in main function). 
*/
void WriteToFile::file_writing_thread (uv_work_t* work_t)
{
	WriteToFile * pWriteToFile = (WriteToFile*) work_t->data;
	BOOL bToQuitAfterQueueCheck = FALSE;

	while(1)
	{
		// Get size. If non zero, get next element in the queue and remove it from queue.
		// We MUST do all three in single lock to avoid writer to modify the queue causing inconsistancy
		uv_rwlock_wrlock(&pWriteToFile->m_rwlFileQueue);
		int QSize = static_cast<int> (pWriteToFile->m_FileQueue.size());

		if ((!QSize) && (bToQuitAfterQueueCheck == FALSE))
		{
			// If no elements in queue, shrink the queue (or it may keep large amount of memory occupied unnecessarily)
			std::queue<stFileNameAndData*>(pWriteToFile->m_FileQueue).swap(pWriteToFile->m_FileQueue);

			uv_rwlock_wrunlock(&pWriteToFile->m_rwlFileQueue);

			Sleep(333); // When all the elements in queue are done, take a little pause...

			if (pWriteToFile->m_bStopFileWritingThread == TRUE)
				bToQuitAfterQueueCheck = TRUE;

			continue;
		}

		stFileNameAndData* pFileNameAndData = NULL;

		if (QSize)
		{
			pFileNameAndData = pWriteToFile->m_FileQueue.front();  
			pWriteToFile->m_FileQueue.pop();
		}
		uv_rwlock_wrunlock(&pWriteToFile->m_rwlFileQueue);

		if (QSize)
		{
			// First delete all existing files in path
			// Deleting using findfirst findnext is better (performance wise) than using system call
			// std::string commandToDeleteAllFiles = "del /Q ";
			// commandToDeleteAllFiles += pFileNameAndData->csPath + "\\*.*";
			// system(commandToDeleteAllFiles.c_str());
			pWriteToFile->DeleteAllFiles(pFileNameAndData->csPath);

			// Write here to file
			// Here we got foldername and filename to store imagedata
			std::ofstream outfile (pFileNameAndData->csFilePathName);

			if (outfile.fail())
			{
				LOG (ERROR, "Icon/image file creation failed.");
			}

			if (pFileNameAndData->csFileData.size())
			{
				outfile << pFileNameAndData->csFileData ;

				if (outfile.bad())
				{
					outfile.close();
					LOG (ERROR, "Cannot store icon/image.");
				}
			}

			outfile.close();

			// And delete file name and data (allocated in QueueFile)
			DEL(pFileNameAndData);
		}
		else if (bToQuitAfterQueueCheck == TRUE) // No element in queue and its time to quit
		{
			break;
		}
	}
}

void WriteToFile::after_file_writing_thread (uv_work_t* work_t, int status)
{
	WriteToFile* pWriteToFile = (WriteToFile*) work_t->data;
	pWriteToFile->m_bFileWritingThreadStopped = TRUE;
}

/*------------------------------------------------------------------------------------------------------------------------------------*/
bool WriteToFile::QueueFile (std::string& csPath, std::string& csFilePathName, const std::string& csFileData)
{
	bool bSuccess = false;
	
	uv_rwlock_wrlock(&m_rwlFileQueue);

	stFileNameAndData* pFileNameAndData = NULL;

	try
	{
		pFileNameAndData = new stFileNameAndData; // Will be deleted in file_writing_thread

		pFileNameAndData->csPath = csPath;
		pFileNameAndData->csFilePathName = csFilePathName;
		pFileNameAndData->csFileData = csFileData;

		m_FileQueue.push (pFileNameAndData);
		bSuccess = true;
	}
	catch(std::bad_alloc&)
	{
		if (pFileNameAndData) // In case if exception has occurred for push
			DEL(pFileNameAndData);

		LOG (EXCEPTION, "\nEXCEPTION bad_alloc. Cannot queue file.") ;
	}

	uv_rwlock_wrunlock(&m_rwlFileQueue); 

	return bSuccess;
}
