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

Profiler::Profiler(char* strFunctionName)
{
#ifdef GENERATE_PROFILE_DATA
	m_dblStartTime = ConnectionsManager::GetHighPrecesionTime(); 
	m_strFunctionName = strFunctionName;
	uv_rwlock_wrlock(&CommonComponents::m_rwlFunctionProfiler);
	CommonComponents::stServerStat.FunctionProfiler[m_strFunctionName].Frequency++ ;
	uv_rwlock_wrunlock(&CommonComponents::m_rwlFunctionProfiler);
#endif
}

Profiler::~Profiler()
{
#ifdef GENERATE_PROFILE_DATA
	double Duration = ConnectionsManager::GetHighPrecesionTime() - m_dblStartTime;
	uv_rwlock_wrlock(&CommonComponents::m_rwlFunctionProfiler);
	CommonComponents::stServerStat.FunctionProfiler[m_strFunctionName].MaxDuration = max(Duration, CommonComponents::stServerStat.FunctionProfiler[m_strFunctionName].MaxDuration );
	CommonComponents::stServerStat.FunctionProfiler[m_strFunctionName].TotalDuration += Duration;
	CommonComponents::stServerStat.FunctionProfiler[m_strFunctionName].MaxDurationInLastInterval = max(Duration, CommonComponents::stServerStat.FunctionProfiler[m_strFunctionName].MaxDurationInLastInterval );

	if ((time(NULL) - CommonComponents::stServerStat.FunctionProfiler[m_strFunctionName].PreviousTime) > RequestProcessor::GetCommonParameters().StatusUpdateFrequencyInSeconds)
	{
		CommonComponents::stServerStat.FunctionProfiler[m_strFunctionName].MaxDurationInLastInterval = 0;
		CommonComponents::stServerStat.FunctionProfiler[m_strFunctionName].PreviousTime = time(NULL);
	}
	uv_rwlock_wrunlock(&CommonComponents::m_rwlFunctionProfiler);
#endif
}
