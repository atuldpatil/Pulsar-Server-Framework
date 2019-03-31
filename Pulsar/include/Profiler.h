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

class created for performance testing and debugging purpose. It helps finding time taken by method under evaluation.
If GENERATE_PROFILE_DATA is defined, then all its needs for server application is to construct local object of this class 
and logs will be generated depcting how much time the function has taken to execute. Alternatively application may use 
ADD2PROFILER macro defined for the same purpose.
*/

// NOTE: Since this profiler copies timer data to std::map, this works correct only in event loop in its current state
class DLL_API Profiler
{
	double m_dblStartTime, m_dblEndTime;
	char* m_strFunctionName;

public:
	Profiler(char* strFunctionName);
	~Profiler();
};
