#include "ODNetwork.h"
#include "TCPClient.h"


using namespace std;

bool stopSending = false;
bool callNextVideo = false;
bool videoFinished = false;
bool end = false;
int clientID = -1;
int side = -1;
char* name = NULL;
int extra = -1;
////////////////////////////////// Timer ///////////////////////////////////
/* Basic timer
 */
double PCFreq = 0.0;
__int64 CounterStart;
double ret;

void startTimer()
{
    LARGE_INTEGER li;
    if(!QueryPerformanceFrequency(&li))
        printf("QueryPerformanceFrequency failed!\n");

    PCFreq = double(li.QuadPart)/1000.0;

    QueryPerformanceCounter(&li);
    CounterStart = li.QuadPart;
}
double getTimer()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
	//printf("Time diff: %f\n", (li.QuadPart-CounterStart)/PCFreq);
	return double(li.QuadPart-CounterStart)/PCFreq;
}

////////////////////////////////// UDP ///////////////////////////////////
/* Setup a new blocking UDP connection.
 */
ofxUDPManager *udpConnection = NULL;
int connectToUDPServer()
{
	udpConnection = new ofxUDPManager();
	char* udpServerIP = SERVERIP;
	int udpServerPort = UDP_PORT;
	

	udpConnection->Create();
	bool connected = udpConnection->Connect(udpServerIP, udpServerPort);
	if(connected)
		printf("Connected to UDP Server: %s Port: %d\n", udpServerIP, udpServerPort);
	else
		printf("ERROR: Could not bind to UDP Server: %s Port: %d\n", udpServerIP, udpServerPort);

	udpConnection->SetNonBlocking(true);
	udpConnection->SetTimeoutReceive(NO_TIMEOUT);
	//udpConnection->SetTimeoutReceive(0);

	return connected;
}
/* Check incoming UDP commands and respond by sending the audio clock times for the requested stream.
 */
void parseUDPCommands()
{
	char msg[8192];
	int bufsize = udpConnection->GetReceiveBufferSize();
	
	int received = udpConnection->Receive(msg, bufsize);
	//printf("checUDPData bufsize: %i recv %i  \n", bufsize, received);
	if(received > 0)
	{
		std::string videoInfo = std::string(msg);
		int found = 0, start = 0;
		found = videoInfo.find_first_of(" ,;");
		std::string command = videoInfo.substr(start,found-start).c_str();
		if(command.compare("DELAY") == 0)
		{
			start = found+1;
			found = videoInfo.find_first_of(" ,;", found+1);
			int _clientID = atoi((videoInfo.substr(start,found-start)).c_str());
			if(_clientID == clientID)
			{
				start = found+1;
				found = videoInfo.find_first_of(" ,;",found+1);
				int side = atoi(videoInfo.substr(start,found).c_str());
				start = found+1;
				found = videoInfo.find_first_of(" ,;",found+1);
				double delay = atof(videoInfo.substr(start,found).c_str());
				//printf("ClientID: %d - AUDIO_CLOCK[%d]: %f\n",_clientID, side, delay);
				fprintf(stdout, "\rAUDIO_CLOCK[%d]: %f", side, delay);
			}
		}
	}
}

////////////////////////////////// TCP ///////////////////////////////////
ofxTCPClient* commandReceiver = NULL;
std::string serverIP = SERVERIP; // default TCP server IP
int serverPort = TCP_PORT; // default port

//#define LOG
#ifdef LOG
FILE* incomingTCP;
#endif
bool setupTCPClient = false; // Default - TCP client disabled

/* Setup a TCP connection and wait for the "READY" signal to be sent.
 */
int connectToAudioServer()
{
	connectToUDPServer();
	commandReceiver = new ofxTCPClient();

	// Setup TCP client to listen to incoming commands from the touch server
	if(!setupTCPClient)
	{
		#ifdef LOG
		// Open a text file for logging all incoming commands.
		incomingTCP = fopen("incomingTCP.txt", "w");
		if(incomingTCP == NULL)
			printf("Could not open command log file");
		#endif
		while(!commandReceiver->setup(serverIP, serverPort))
		{
			printf("Waiting to connect to server...\n");
			Sleep(1000);
		}
		startTimer();
		bool readyRev = false;
		std::string remainder;
		std::vector<std::string> commands;
		while(!readyRev)
		{
			if (commandReceiver->receiveStrings(commands, remainder) > 0)
				for (int i = 0; i < commands.size(); i++)
				{
					if(!commands[i].empty() && (commands[i].compare(STR_END_MSG) != 0))
					//if(!commands[i].empty())
					{
						int found = 0, start = 0;
						found = commands[i].find_first_of(" ,;");
						string command = commands[i].substr (start,found-start);

						if(command.compare("READY") == 0)
						{
							readyRev = true;
							break;
						}
					}
				}
				if(getTimer() > READY_TIMEOUT)
				{
					printf("Timeout waiting for READY...\n");
					return false;
				}
		}
		//while(commandReceiver->receive().compare("READY") != 0)
		//{
		//	//printf("Waiting for READY...\n");
		//	//Sleep(100);
		//	if(getTimer() > 5000)
		//	{
		//		printf("Waiting for READY timeout...\n");
		//		return false;
		//	}
		//}
		printf("CONNECTED to server!\n");
		commandReceiver->setVerbose(false);

		return true;
	}

	return false;
}


/* Send TCP play video command to the audio server.
 */
int playVideoCommand(int clientID, int side, char* fileName)
{
	//for(int i=0; i<20; i++)
	//{
	char buf[256];
	sprintf(buf, "%d;%d;%s;", clientID, side, fileName);
	//sprintf(buf, "%d;%d;%s;", clientID, side, fileName);
	std::string videoInfo = "PLAY_VIDEO;";
	videoInfo.append(buf);
	//videoInfo += STR_END_MSG;
	commandReceiver->send(videoInfo);
	//}
	return 0;
}

/* Send TCP stop video command to the audio server.
 */
int stopVideoCommand(int clientID, int side)
{
	//for(int i=0; i<20; i++)
	//{
	char buf[256];
	sprintf(buf, "%d;%d;", clientID, side);
	//sprintf(buf, "%d;%d;", clientID, side);
	std::string videoInfo = "STOP_VIDEO;";
	videoInfo.append(buf);
	//videoInfo += STR_END_MSG;
	commandReceiver->send(videoInfo);
	//}
	return 0;
}
#if 0
void parseTCPCommands()
{
		std::string videoInfo = commandReceiver->receive();
	   // If the command string is not blank
	   if(!videoInfo.empty())
	   {
			int found = 0, start = 0;
			found = videoInfo.find_first_of(" ,;");
			string command = videoInfo.substr (start,found-start);
			//if(command.compare("DELAY") == 0)
			//{
			//	start = found+1;
			//	found = videoInfo.find_first_of(" ,;",found+1);
			//	int side = atoi(videoInfo.substr(start,found).c_str());
			//	start = found+1;
			//	found = videoInfo.find_first_of(" ,;",found+1);
			//	double delay = atof(videoInfo.substr(start,found).c_str());
			//	printf("AUDIO_CLOCK[%d]: %f\n", side, delay);
			//}
			if(command.compare("VIDEO_FINISHED") == 0)
			{
				start = found+1;
				found = videoInfo.find_first_of(" ,;",found+1);
				int side = atoi(videoInfo.substr(start,found).c_str());

				printf("Video finished SIDE = %d\n", side);
				videoFinished = true;
			}
			else if(command.compare("HEARTBEAT") == 0)
			{
				//printf("-------------------Heart-------------------\n");
			}
			#ifdef LOG
			// Log all incoming commands
			fputs((commands.at(i)).c_str(), incomingTCP);
			fputs("\n", incomingTCP);
			fflush(incomingTCP);
			#endif
	   }
}
#endif
/* Parse and process incoming TCP commands.
 * Commands:
 * VIDEO_FINISHED, int side
 */
void parseTCPCommands()
{
	//if(commandReceiver->receive().compare("VIDEO_FINISHED") == 0)
	//	videoFinished = true;
	//if(commandReceiver->receive().compare("HEARTBEAT") == 0)
	//	printf("HEARTBEAT");
	//else if(commandReceiver->receive().compare("HEARTBEAT") == 0)
	//{ }//	printf("HEARTBEAT");

	std::string remainder;
	std::vector<std::string> commands;
	// Listen for incoming commands
	if (commandReceiver->receiveStrings(commands, remainder) > 0)
	//if (commandReceiver->receiveStrings(commands, remainder) > 0)
	{
		//for (int i = commands.size()-2; i < commands.size(); i++)
		for (int i = 0; i < commands.size(); i++)
		{
		   // If the command string is not blank
			if(!commands[i].empty() && (commands[i].compare(STR_END_MSG) != 0))
		   {
				//printf("commands[i]:%s\n", commands[i].c_str());
				//std::string meh1 = string("]");
				//bool meh = (commands[i].compare(meh1) == 0);
				//printf("commands[i].compare("") == %d\n", commands[i].compare(STR_END_MSG));
				int found = 0, start = 0;
				found = commands[i].find_first_of(" ,;");
				string command = commands[i].substr (start,found-start);
				
				//if(command.compare("DELAY") == 0)
				//{
				//	start = found+1;
				//	found = commands[i].find_first_of(" ,;",found+1);
				//	int side = atoi(commands[i].substr(start,found).c_str());
				//	start = found+1;
				//	found = commands[i].find_first_of(" ,;",found+1);
				//	double delay = atof(commands[i].substr(start,found).c_str());
				//	printf("AUDIO_CLOCK[%d]: %f\n", side, delay);
				//}
				if(command.compare("VIDEO_FINISHED") == 0)
				{
					start = found+1;
					found = commands[i].find_first_of(" ,;",found+1);
					int side = atoi(commands[i].substr(start,found).c_str());

					printf("Video finished SIDE = %d\n", side);
					//videoFinished = true;
				}
				else if(command.compare("HEARTBEAT") == 0)
				{
					//printf("-------------------Heart-------------------\n");
				}
				#ifdef LOG
				 Log all incoming commands
				fputs((commands.at(i)).c_str(), incomingTCP);
				fputs("\n", incomingTCP);
				fflush(incomingTCP);
				#endif
		   }
		}
	}
}
////////////////////////////////// SIGNAL ///////////////////////////////////
BOOL CtrlHandler( DWORD fdwCtrlType ) 
{ 
  switch( fdwCtrlType ) 
  { 
    // Handle the CTRL-C signal. 
    case CTRL_C_EVENT: 
      printf( "Ctrl-C event\n\n" );
      Beep( 750, 300 );
	  //udpConnection->SetTimeoutReceive(0);
	  //stopVideoCommand(clientID, side);// Test stopping a stream/side
	  videoFinished = true;
	  //end = true;
	  return TRUE;
	  //break;
      //exit(EXIT_SUCCESS);
 
    // CTRL-CLOSE: confirm that the user wants to exit. 
    case CTRL_CLOSE_EVENT: 
      Beep( 600, 200 );
	  exit(EXIT_FAILURE);
      printf( "Ctrl-Close event\n\n" );
      return( TRUE ); 
 
    // Pass other signals to the next handler. 
    case CTRL_BREAK_EVENT: 
      Beep( 900, 200 ); 
	  printf("Shutting down audio server\n");
      printf( "Ctrl-Break event\n\n" );

	  callNextVideo = true; // Test a different stream/side for same client
	  playVideoCommand(clientID, extra, name);

	  return TRUE;
      return FALSE; 
 
    case CTRL_LOGOFF_EVENT: 
      Beep( 1000, 200 ); 
      printf( "Ctrl-Logoff event\n\n" );
      return FALSE; 
 
    case CTRL_SHUTDOWN_EVENT: 
      Beep( 750, 500 ); 
      printf( "Ctrl-Shutdown event\n\n" );
      return FALSE; 
 
    default: 
      return FALSE; 
  } 
}
/* Setup signal handler.
 */
bool setSignalHandler()
{
	if( SetConsoleCtrlHandler( (PHANDLER_ROUTINE) CtrlHandler, TRUE ) ) 
	{ 
		printf( "\nThe Control Handler is installed.\n" ); 
		printf( "\n -- Now try pressing Ctrl+C or Ctrl+Break, or" ); 
		printf( "\n    try logging off or closing the console...\n" ); 
		printf( "\n(...waiting in a loop for events...)\n\n" ); 
		return true;
	}
	return false;
}
////////////////////////////////// MAIN ///////////////////////////////////
void sendGetAudioClockCommand(int stream)
{
	char buf[256];
	sprintf(buf, "%d;%d;", clientID, stream);
	std::string command = "GET_AUDIO_CLOCK;";
	command.append(buf);
	//command += STR_END_MSG;
	//commandReceiver->send(command);

	bool sent = udpConnection->Send(command.c_str(),command.length());
	if(!sent)
		printf("**********Couldn't send UDP data**********\n");
}


int init(char* argv[])
{
	if(!setSignalHandler())
		printf("Couldn't setup signal handler\n");

	if(!connectToAudioServer())
	{
		printf("ERROR: Couldn't connect to audio server\n");
		commandReceiver->close();
		delete commandReceiver;
		exit(EXIT_FAILURE);
	}
#if 0
	//commandReceiver->send("AUDIO_INIT");
	//Sleep(1000);
#endif
	
	clientID = atoi(argv[0]);
	side = atoi(argv[1]);
	name = argv[2];
	extra = atoi(argv[3]);

	videoFinished = false;
	stopVideoCommand(clientID, side);
	//Sleep(500);
	playVideoCommand(clientID, side, name);

	return 1;
}

void updateClient()
{
	if(!videoFinished)
	{
		parseTCPCommands();

		if(!videoFinished)
		{
			sendGetAudioClockCommand(side);
			parseUDPCommands();
			Sleep(33);
		}
		
		if(callNextVideo) // Test a different stream/side for same client
			sendGetAudioClockCommand(extra);
	}
	//if(!connectToAudioServer())
	//{
	//	printf("ERROR: Couldnt connected to audio server\n");
	//	commandReceiver->close();
	//	delete commandReceiver;
	//	exit(EXIT_FAILURE);
	//}
	//udpConnection->SetTimeoutReceive(NO_TIMEOUT);
}
#if 0
int main(int argc, char* argv[])
{
	if(!setSignalHandler())
		printf("Couldnt setup signal handler\n");

	if(!connectToAudioServer())
	{
		printf("ERROR: Couldnt connected to audio server\n");
		commandReceiver->close();
		delete commandReceiver;
		exit(EXIT_FAILURE);
	}
#if 0
	//commandReceiver->send("AUDIO_INIT");
	//Sleep(1000);
#endif
	
	clientID = atoi(argv[1]);
	side = atoi(argv[2]);
	name = argv[3];
	extra = atoi(argv[4]);

	//while(true)
	//{
	//	parseTCPCommands();
	//	Sleep(1000);
	//}
	while(!end)
	{
		videoFinished = false;
		stopVideoCommand(clientID, side);
		//Sleep(500);
		playVideoCommand(clientID, side, name);
		while(!videoFinished)
		{
			parseTCPCommands();

			if(!videoFinished)
			{
				sendGetAudioClockCommand(side);
				parseUDPCommands();
				Sleep(33);
			}
			
			if(callNextVideo) // Test a different stream/side for same client
				sendGetAudioClockCommand(extra);
		}
		//if(!connectToAudioServer())
		//{
		//	printf("ERROR: Couldnt connected to audio server\n");
		//	commandReceiver->close();
		//	delete commandReceiver;
		//	exit(EXIT_FAILURE);
		//}
		//udpConnection->SetTimeoutReceive(NO_TIMEOUT);
	}

	if(setupTCPClient)
	{
#ifdef LOG
		// Close connections
		fclose(incomingTCP);
#endif
		if (commandReceiver->isConnected())
		{
			commandReceiver->close();
			delete commandReceiver;
		}
	}

	printf("Close TCP Client");
	exit(EXIT_SUCCESS);
}
#endif