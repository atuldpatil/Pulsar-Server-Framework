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

// Common parameters for sample server
#define NUMBER_OF_REQUESTPROCESSING_THREADS 5 // At least 1 is needed
#define MAX_PENDING_RESPONSES_PER_CLIENT 16	  // At least equal to NUMBER_OF_REQUESTPROCESSING_THREADS
#define KEEP_ALIVE_IN_SECONDS 30
#define STATUS_INTERVAL_IN_SECONDS 5 // Interval in seconds to send server stat to screen/log

#define VERSION_1 1
#define VERSION_2 2
#define VERSION_3 3