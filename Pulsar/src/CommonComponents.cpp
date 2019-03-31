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

#ifdef GENERATE_PROFILE_DATA
// If ServerStat is static (when Profiler is turned on it has to access it so we make it static) we need to initialize it.
ServerStat CommonComponents::stServerStat={};
uv_rwlock_t CommonComponents::m_rwlFunctionProfiler;
FunctionProfilerMap structServerStat::FunctionProfiler;
#else
// We use globally initialized structure to initialize our member structure. 
// This is preferable way than having c'tro in structure and assigning zero to all member variables:
static ServerStat staticServerStat={};
#endif

CommonComponents::CommonComponents()
{
	after_send_response_called_by_send_response = FALSE;

#ifdef GENERATE_PROFILE_DATA
	int RetVal = uv_rwlock_init(&m_rwlFunctionProfiler);
	ASSERT(RetVal>=0);
#else
	m_stServerStat = staticServerStat;
#endif

    loop = uv_default_loop();

	ASSERT_THROW(loop, "Error initializing event loop");

	// Let's validate common parameters here
	CommonParameters ComParams = RequestProcessor::GetCommonParameters();

	ValidateCommonParamaters(ComParams);

	// MUST BE called before first call of uv_queue_work
	uv_set_threadpool_size (RequestProcessor::GetCommonParameters().MaxRequestProcessingThreads + 1 + 1); //  One thread for logger and one for file writer
}

void CommonComponents::ValidateCommonParamaters(CommonParameters& ComParams)
{
	int MaxAllowedRequestProcessingThread = (MAX_WORK_THREADS - 2); // At least two threads we need other than request processing threads. One for logger another for file writer.

	ASSERT_MSG ((ComParams.MaxRequestProcessingThreads >= 1), "Invalid value: MaxRequestProcessingThreads"); // At least one reqest processing thread is needed
	ASSERT_MSG ((ComParams.MaxRequestProcessingThreads < MaxAllowedRequestProcessingThread), "Invalid value: MaxRequestProcessingThreads");
	
    // ComParams.MaxPendingRequests = (ComParams.MaxPendingRequests < 1) ? 1 : ComParams.MaxPendingRequests;
		
	// In case when all (request processing) threads generate response(s) for one client, pending responses must be at least equal to (number of processing threads).
	ASSERT_MSG ((ComParams.MaxPendingResponses >= ComParams.MaxRequestProcessingThreads), "Invalid value: MaxPendingResponses");

	ASSERT_MSG ((ComParams.KeepAliveFrequencyInSeconds >= 1), "Invalid value: KeepAliveFrequencyInSeconds");
	ASSERT_MSG ((ComParams.StatusUpdateFrequencyInSeconds >= 1), "Invalid value: StatusUpdateFrequencyInSeconds");
}

CommonComponents::~CommonComponents()
{
#ifdef GENERATE_PROFILE_DATA
	uv_rwlock_destroy(&m_rwlFunctionProfiler);
#endif
	uv_loop_close(loop);
}
