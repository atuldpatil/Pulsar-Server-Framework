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

#include "Pulsar_SampleServer.h"
#include "Pulsar_SampleServerLogger.h"

void PulsarSampleServerLogger::ProcessLog (ServerStat& stServerStat, LoggerMap& InfoMap, LoggerMap& NotesMap, LoggerMap& ErrorsMap, LoggerMap& ExceptionsMap, LoggerMap& DebugMap)
{
	// Print server stat
	std::cout << "\n------------------------------------------------------------------------------\n\n";
	std::cout << "\n\n\nStatistics:\n";
	std::cout << "\nUpdated after: " << stServerStat.Interval << " seconds (Total time elapsed: " \
		<< (stServerStat.TotalTimeElapsed / 60 / 60 / 24) << " days, " \
		<< ((stServerStat.TotalTimeElapsed / 60 / 60) % 24) << " hours, " \
		<<  ((stServerStat.TotalTimeElapsed / 60) % 60) << " minutes, " \
		<< (stServerStat.TotalTimeElapsed % 60) << " seconds)";

	tm Tm;
	localtime_s(&Tm, &stServerStat.Time);
	char strTime[256];
	asctime_s(strTime, 256, &Tm);

	std::cout << "\nTime stamp: " << strTime;
	std::cout << "\n#ClientsConnectedTillNow " << stServerStat.ClientsConnectedCount << " #ClientsDisconnectedTillNow " << stServerStat.ClientsDisconnectedCount << " #ClientsConnectionsActive " << stServerStat.ClientsConnectionsActive << " #ServersConnected " << stServerStat.ServersConnected;
	std::cout << "\n#DisconnectionsByServer " << stServerStat.DisconnectionsByServer << " #DisconnectionsByClients " << stServerStat.DisconnectionsByClients;
	std::cout << "\n#MaxPossibleClients " << stServerStat.MaxPossibleClients;
	std::cout << "\n#ActiveClientRequestBuffers " << stServerStat.ActiveClientRequestBuffers;
	std::cout << "\n" ;
	std::cout << "\n#RequestsArrived " << stServerStat.RequestsArrived << " #RequestsProcesed " << stServerStat.RequestsProcesed << " (Difference: " << (stServerStat.RequestsArrived - stServerStat.RequestsProcesed) << ") #TotalRequestBytesProcessed " << stServerStat.TotalRequestBytesProcessed/1024 << " KB";
	std::cout << "\n#RequestsArrivedPerSecond " << stServerStat.RequestsArrivedPerSecond << " #RequestsProcessedPerSecond " << stServerStat.RequestsProcessedPerSecond << " (For average request size " << stServerStat.AverageRequestsSize << " in last " << (int) stServerStat.Interval << " seconds) AverageRequestProcessingTime " << stServerStat.AverageRequestProcessingTime << " seconds";
	std::cout << "\n#HeaderErrorInPreamble " << stServerStat.HeaderErrorInPreamble << " #HeaderErrorInVersion " << stServerStat.HeaderErrorInVersion << " #HeaderErrorInSize " << stServerStat.HeaderErrorInSize ;
	std::cout << "\n#RequestsNotAdvicedToProcess " << stServerStat.RequestsNotAdvicedToProcess << " #RequestsFailedToProcess " << stServerStat.RequestsFailedToProcess << " #RequestsRejectedByServer " << stServerStat.RequestsRejectedByServer << " #RequestBytesIgnored " << stServerStat.RequestBytesIgnored/1024 << " KB";
	std::cout << "\n#RequestProcessingThreadsStarted " << stServerStat.RequestProcessingThreadsStarted << " #RequestProcessingThreadsFinished " << stServerStat.RequestProcessingThreadsFinished ;
	std::cout << "\n" ;
	std::cout << "\n#TotalResponsesSent " << stServerStat.ResponsesSent ;
	std::cout << "\n(#ResponsesOrdinary " << stServerStat.ResponsesOrdinary << " #ResponsesMulticasts  " << stServerStat.ResponsesMulticasts << " #ResponsesUpdates " << stServerStat.ResponsesUpdates << " #ResponsesForwarded " << stServerStat.ResponsesForwarded << " #ResponsesErrors " << stServerStat.ResponsesErrors << " #ResponsesKeepAlives " << stServerStat.ResponsesKeepAlives << ") ";
	std::cout << "\nResponseQueuedDurationMinimum " << stServerStat.ResponseQueuedDurationMinimum << " ResponseQueuedDurationMaximum " << stServerStat.ResponseQueuedDurationMaximum << " #ResponsesInClientsQueues "  << stServerStat.ResponsesInLocalClientsQueues << " #ResponsesInServersQueues " << stServerStat.ResponsesInPeerServersQueues << " TotalResponseBytesSent " << stServerStat.TotalResponseBytesSent/1024 << " KB #ResponsesBeingSent " << stServerStat.ResponsesBeingSent;

	std::cout << "\n#ResponsesFailedToSend " << stServerStat.ResponsesFailedToSend << " #ResponsesFailedToForward " <<  stServerStat.ResponsesFailedToForward << " (#ForwardErrorWritingServer " << stServerStat.ForwardErrorWritingServer << ", #ForwardErrorConnectingTimedout " << stServerStat.ForwardErrorConnectingTimedout << ", #ForwardErrorOverflowed " << stServerStat.ForwardErrorOverflowed << ", #ForwardErrorDisconnecting " << stServerStat.ForwardErrorDisconnecting << ", #ForwardErrorDisconnected " << stServerStat.ForwardErrorDisconnected << ")";
	std::cout << "\n" ;
	std::cout << "\nErrors & Exceptions stat:";
	std::cout << "\n#MemoryAllocationExceptionCount " << stServerStat.MemoryAllocationExceptionCount << " #RequestCreationExceptionCount " << stServerStat.RequestCreationExceptionCount << " #ResponseCreationExceptionCount " << stServerStat.ResponseCreationExceptionCount ;
	std::cout << "\n" ;
	std::cout << "\nHandles stat:";
	std::cout << "\n#Estimated max handle count " << stServerStat.EstimatedHandleCount << " #Current handle count " << stServerStat.ActualHandleCount ;
	std::cout << "\n" ;
	std::cout << "\nMemory stat:";
	std::cout << "\nMemory consumed by Clients " << stServerStat.MemoryConsumptionByClients/1024 << " KB" ; // << " (" << (stServerStat.MemoryConsumptionByClients*100)/RequestProcessor::GetCommonParameters().MaxMemoryConsumptionByClients << "% of allowed MaxMemoryConsumptionByClients)";
	std::cout << "\nMemory consumed by requests in queue " << stServerStat.MemoryConsumptionByRequestsInQueue/1024 << " KB" ; // << " (" << (stServerStat.MemoryConsumptionByRequestsInQueue*100)/RequestProcessor::GetCommonParameters().MaxMemoryConsumptionByRequests << "% of allowed MaxMemoryConsumptionByRequests)";
	std::cout << "\nMemory consumed by responses in queue " << stServerStat.MemoryConsumptionByResponsesInQueue/1024 << " KB" ; // << " (" << (stServerStat.MemoryConsumptionByResponsesInQueue*100)/RequestProcessor::GetCommonParameters().MaxMemoryConsumptionByResponses << "% of allowed MaxMemoryConsumptionByResponses)";
	std::cout << "\nTotal memory consumption " << stServerStat.TotalMemoryConsumption/1024 << " KB";
	std::cout << "\nActual memory consumption " << stServerStat.ActualMemoryConsumption/1024 << " KB";

	// Print all elements of WarningsMap, ErrorsMap and DebugMap
	// First print all elements of InfoMap

	PrintMap (InfoMap, "\n\nInfo:");
	PrintMap (NotesMap, "\n\nNotes:");
	PrintMap (ErrorsMap, "\n\nErrors:");
	PrintMap (ExceptionsMap, "\n\nExceptions:");
	PrintMap (DebugMap, "\n\nDebug Info:");
}

// Called by ProcessLog (through log processing thread)
void PulsarSampleServerLogger::PrintMap (LoggerMap& Map, char* szMessage)
{
	if ((Map.size() != 0) && (szMessage))
		std::cout << szMessage;
	LoggerIterator iterator_end = Map.end();
	LoggerIterator iterator_begin = Map.begin();
	for(LoggerIterator iterator = iterator_begin; iterator != iterator_end; iterator++)
	{
		long long count = iterator->second ;
		std::cout << std::endl << (iterator->first).c_str();
		if (count > 1)
			std::cout << "   [Logged " << count << " times]";
	}
}

PulsarSampleServerLogger globalLoggerInstance;