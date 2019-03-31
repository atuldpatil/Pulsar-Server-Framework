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

int main(int argc, char** argv) 
{
	    std::cout << "\nPulsar Server Framework: Copyright (c) 2013-2019 Atul D. Patil (atuldpatil@gmail.com) \
                 \nThis program comes with ABSOLUTELY NO WARRANTY;\nThis is free software, and you are welcome to redistribute it under certain conditions;";

	if (argc < 3)
	{
		std::cout << "\n\nERROR: Invalid command line. \n\nSyntax: " << argv[0] << " <IP Address> <Port>\n\nExample: " << argv[0] << " 192.168.1.100 8000\n\n";
		return -1;
	}
	else
	{
		unsigned short int IPv4Port = atoi(argv[2]);
		static char* IPAddress = argv[1];
		std::cout << "\nINFO: Command line parameters: IPAddress " << IPAddress << " Port " << IPv4Port;

		// Instantiate ConnectionManager
		ConnectionsManager* pConnectionsManager = NULL;

		try
		{
			pConnectionsManager = new ConnectionsManager;
		}
		catch(std::bad_alloc&)
		{
			std::cout << "\nMemory exception in ConnectionsManager.";
		}

		// Start server
		int RetVal = pConnectionsManager->StartServer (IPAddress, IPv4Port, true);

		if (RetVal)
			std::cout << "\nERROR Starting server. Code " << RetVal << "(" << pConnectionsManager->GetErrorDescription(RetVal) << ")";

		delete pConnectionsManager;

		std::cout << "\nMAIN: Server shutdown completed. Press a key to exit...\n\n";

		_getch();
	}

	return 0;
}
