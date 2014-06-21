#include "constants.h"
#include "netClockSync.h"
//#include "videoplayback.h"

extern bool broadcastAudio;
#if 0
static bool broadcastPTS = false;
ofxUDPManager *udpPTSConnection = NULL;
#endif

#ifdef NETWORKED_AUDIO
int _clientSenderID = -1;
int _udpAudioServerPort = 2008;
char* _udpMCastIP = "224.0.0.0";
char* _udpAudioServerIP = "127.0.0.1";
char* _tcpAudioServerIP = "127.0.0.1"; // default TCP server IP
int _tcpAudioServerPort = 2007; // default port

ofxTCPClient* commandReceiver = NULL;
ofxUDPManager *udpConnection = NULL;
#if 0
ofxUDPManager *udpConnectionSide[MAXSTREAMS]; // used to create a single UDP per stream
#endif

double netAudioClock[MAXSTREAMS];// = 0;
double avgNetLatency = 0;
double minLatency = LONG_MAX;
double g_UDPDelay = 0.0;

enum {astop, aplay, invalid};
enum {ffs = 1, ffmpeg}; // 1 = ffs, 2 = ffmpeg

#define UDP_BUFFER_SIZE 128
char msg[UDP_BUFFER_SIZE];

extern void seekVideo(int side, double seekDuration, double seekBaseTime = -1, bool sendSeekTCPCommand = true);

////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////// Timer  ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Basic timer
 */
__int64 ReadyCounterStart;
void startNetTimer()
{
    LARGE_INTEGER li;
    if(!QueryPerformanceFrequency(&li))
        printf("QueryPerformanceFrequency failed!\n");

    PCFreq = double(li.QuadPart)/1000.0;

    QueryPerformanceCounter(&li);
    ReadyCounterStart = li.QuadPart;
}
double getNetTimer()
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
	//printf("Time diff: %f\n", (li.QuadPart-ReadyCounterStart)/PCFreq);
	return double(li.QuadPart-ReadyCounterStart)/PCFreq;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////// Setup  ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Test the network latency.
 */
double testNetworkLatency()
{
	printf("Testing network latency...please wait.\n");
	udpConnection->SetNonBlocking(false);
	udpConnection->SetTimeoutReceive(NO_TIMEOUT);
	#define TOTAL	20
	double latencyTimes[TOTAL];
	minLatency = LONG_MAX;
	for (int i = 0; i < TOTAL; i++)
	{
		double netClock;
		double sent_date = -1;
		startGlobalVideoTimer(currentVideo);
		//sendGetAudioClockCommand(0);
		sendGetAudioClockCommand(currentVideo, sent_date);
		do {
			netClock = parseUDPCommands(0, &sent_date);
		}
		while(netClock < 0);
		latencyTimes[i] = getGlobalVideoTimer(currentVideo);
	}
	double avg = 0;
	
	for (int i = 0; i < TOTAL; i++)
	{
		printf("RTT: %09.6f (ms)\n", latencyTimes[i]);
		if(latencyTimes[i] < minLatency)
			minLatency = latencyTimes[i];
		avg += latencyTimes[i];
	}

	//udpConnection->SetTimeoutReceive(0);
	//udpConnection->SetNonBlocking(true);
	printf("TOTAL: %09.6f - COUNT: %d\n", avg, TOTAL);

	return (avg)/TOTAL;
}

/* Initialise the network sync.
 */
void initNetSync(char *serverIP, int TCPPort, int UDPPort, int ClientID, int side, bool testLatency)
{
	char *audioServerIP = serverIP;
	int audioServerTCPPort = TCPPort;
	int audioServerUDPPort = UDPPort;
	_clientSenderID = ClientID;
	_udpAudioServerIP = audioServerIP;
	_udpAudioServerPort = audioServerUDPPort;
	_tcpAudioServerIP = audioServerIP;
	_tcpAudioServerPort = audioServerTCPPort;

	if(broadcastAudio)
		_udpAudioServerIP = _udpMCastIP;

	printf("Setting up ClientID: %d, TCP Server IP: %s, UDP Server IP: %s, TCP port: %d UDP Port: %d\n", _clientSenderID, _tcpAudioServerIP, _udpAudioServerIP, audioServerTCPPort, audioServerUDPPort);
	printf("\n");

	if(!audioClientSetup()) {
		exit(-1);
	}
	if(testLatency)
	{
		if(broadcastAudio)
			printf("Broadcast Audio enabled cannot determine network latency.\n");
		else
		{
			avgNetLatency = testNetworkLatency();
			printf("Average UDP network latency (milliseconds): %f\n", avgNetLatency);
			printf("Mininum UDP network latency (milliseconds): %f\n", minLatency);
		}
	}

	if(broadcastAudio)
		printf("Audio timer broadcast enabled.\n");
	else
		printf("Audio timer broadcast disabled.\n");

	_beginthread(syncThread, 0, 0);
	_beginthread(tcpThread, 0, 0);
}

bool audioClientSetup()
{
	if(!connectToAudioServer())
	{
		printf("ERROR: Couldn't connected to audio server\n");
		if(commandReceiver != NULL)
		{
			commandReceiver->close();
			delete commandReceiver;
		}
		return false;
	}

	return true;
}

///////////////////////////////// Network //////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////// UDP ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
#if 0
int setupPTSUDPServer()
{
	if(broadcastPTS)
	{
		if(udpPTSConnection != NULL)
		{
			delete udpPTSConnection;
		}
		udpPTSConnection = new ofxUDPManager();
		udpPTSConnection->Create();

		bool PTSConnected = udpPTSConnection->Bind(_udpAudioServerPort);

		if(PTSConnected)
			printf("Setup pts UDP Server: %s Port: %d\n", _udpAudioServerIP, _udpAudioServerPort);
		else
			printf("ERROR: Could not bind to UDP Server: %s Port: %d\n", _udpAudioServerIP, _udpAudioServerPort);

		//PTSConnected->SetNonBlocking(true);
		//PTSConnected->SetNonBlocking(false);
		udpPTSConnection->SetReceiveBufferSize(UDP_BUFFER_SIZE);

		return PTSConnected;
	}

	return false;
}

int sendPTSTime(int stream, double pts)
{
	char buf[256];
    sprintf(buf, "%d;%d;%.20f;", _clientSenderID, stream, pts);
	std::string command = "PTS_CLOCK;";
	command.append(buf);
	//command += STR_END_MSG;
	//commandReceiver->send(command);

	//printf("COMMAND TO SERVER:  %s", command.c_str());
	bool sent = udpPTSConnection->Send(command.c_str(),command.length());
	if(!sent)
		printf("**********Couldn't send UDP data**********\n");

	return true;
}
#endif

/* Setup a new blocking UDP connection.
 */
int connectToUDPServer()
{
	if(udpConnection != NULL)
	{
		delete udpConnection;
	}
	udpConnection = new ofxUDPManager();
	udpConnection->Create();

	bool connected;
	if(!broadcastAudio)
	{
		connected = udpConnection->Connect(_udpAudioServerIP, _udpAudioServerPort);
	}
	else
	{
		//connected = udpConnection->Bind(_udpAudioServerPort);
		connected = udpConnection->BindMcast(_udpAudioServerIP, _udpAudioServerPort);
	}		

	//bool connected = udpConnection->Connect(_udpAudioServerIP, _udpAudioServerPort);
	//bool connected = udpConnection->Bind(_udpAudioServerPort);
	if(connected)
		printf("Connected to UDP Server: %s Port: %d\n", _udpAudioServerIP, _udpAudioServerPort);
	else
		printf("ERROR: Could not bind to UDP Server: %s Port: %d\n", _udpAudioServerIP, _udpAudioServerPort);

	//udpConnection->SetNonBlocking(true);
	//udpConnection->SetNonBlocking(false);
	udpConnection->SetReceiveBufferSize(UDP_BUFFER_SIZE);

#if 0
	udpConnection->SetNonBlocking(false);
	
	char buf[256];
	for(int stream=0; stream<MAXSTREAMS; stream++)
	{
		sprintf(buf, "%d;%d;", _clientSenderID, stream);
		std::string command = "GET_UDP_STREAM_PORT;";
		command.append(buf);
		//command += STR_END_MSG;
		//printf("COMMAND TO SERVER:  %s", command.c_str());
		bool sent = udpConnection->Send(command.c_str(),command.length());
		if(!sent)
			printf("**********Couldn't send UDP data**********\n");

		int bufsize = udpConnection->GetReceiveBufferSize();
		int received = udpConnection->Receive(msg, bufsize);

		string message = msg;
		if(message != "")
		{
			std::string udpInfo = std::string(message);

			int found = 0, start = 0;
			found = udpInfo.find_first_of(" ,;");
			std::string command = udpInfo.substr(start,found-start).c_str();
			if(command.compare("UDP_STREAM_PORT") == 0)
			{
				start = found+1;
				found = udpInfo.find_first_of(" ,;", found+1);
				int clientID = atoi((udpInfo.substr(start,found-start)).c_str());
				
				start = found+1;
				found = udpInfo.find_first_of(" ,;",found+1);
				int side = atoi(udpInfo.substr(start,found).c_str());

				if(clientID == _clientSenderID && side == stream)
				{
					start = found+1;
					found = udpInfo.find_first_of(" ,;",found+1);
					int port = atoi(udpInfo.substr(start,found).c_str());

					udpConnectionSide[stream] = new ofxUDPManager();

					udpConnectionSide[stream]->Create();
					bool connected = udpConnectionSide[stream]->Connect(_udpAudioServerIP, port);
					if(connected)
						printf("Connected to UDP Server: %s Port: %d\n", _udpAudioServerIP, port);
					else
						printf("ERROR: Could not bind to UDP Server: %s Port: %d\n", _udpAudioServerIP, port);

					udpConnection->SetNonBlocking(true);
					//udpConnection->SetNonBlocking(false);
					udpConnection->SetReceiveBufferSize(UDP_BUFFER_SIZE);
				}
			}
		}
	}

#endif
	return connected;
}
/* Check incoming UDP commands and return audio clock time for the requested stream (blocking).
 */
double parseUDPCommands(int lside, double *snt_date)
{
	return parseUDPCommands(lside, snt_date, 0x0);
}

double parseUDPCommands(int lside, double *snt_date, uint64_t *master_time)
{
	sprintf(msg, "");
	int bufsize = udpConnection->GetReceiveBufferSize();
	int received = udpConnection->Receive(msg, bufsize);
	string message=msg;

	//if(broadcastAudio)
	//{
	//	string tempMessage;
	//	bool getNext = true;
	//	while (getNext)
	//	{
	//	   tempMessage=msg;
	//		if (tempMessage == "")
	//		{
	//		  getNext = false;
	//			//udpConnection->SetNonBlocking(false);udpConnection->SetTimeoutReceive(2);
	//			//printf("UDP buffer finished:-------------\n");
	//		}
	//		else
	//		{
	//			message = tempMessage;
	//			//printf("UDP buffer: %s\n", message.c_str());
	//		}
	//		sprintf(msg, "");
	//		//udpConnection->SetNonBlocking(false);udpConnection->SetTimeoutReceive(2);
	//		int bufsize = udpConnection->GetReceiveBufferSize();
	//		int received = udpConnection->Receive(msg, bufsize);
	//	}
	//}
	//udpConnection->SetNonBlocking(true);

	if(message != "")
	{
		std::string videoInfo = std::string(message);
		//printf("videoInfo: %s\n", videoInfo.c_str());

		int found = 0, start = 0;
		found = videoInfo.find_first_of(" ,;");
		std::string command = videoInfo.substr(start,found-start).c_str();
		if(command.compare("DELAY") == 0)
		{
			start = found+1;
			found = videoInfo.find_first_of(" ,;", found+1);
			int clientID = atoi((videoInfo.substr(start,found-start)).c_str());
			
			start = found+1;
			found = videoInfo.find_first_of(" ,;",found+1);
			int side = atoi(videoInfo.substr(start,found).c_str());

			if(clientID == _clientSenderID && lside == side)
			{
				//printf("videoInfo: %s\n", videoInfo.c_str());

				start = found+1;
				found = videoInfo.find_first_of(" ,;",found+1);
				double delay = atof(videoInfo.substr(start,found).c_str());
				start = found+1;
				found = videoInfo.find_first_of(" ,;",found+1);
				double sent_date = atof(videoInfo.substr(start,found).c_str());

				start = found+1;
				found = videoInfo.find_first_of(" ,;",found+1);
				//double master_time = atoi(videoInfo.substr(start,found).c_str());
				//uint64_t master_time = 0x0;// = _atoi64(videoInfo.substr(start,found).c_str());
				//sscanf_s(videoInfo.substr(start,found).c_str(), "%016I64x", &master_time1);
				sscanf_s(videoInfo.substr(start,found).c_str(), "%016I64x", master_time);
				//printf("master_time  : %f - %I64u\n", master_time, ntoh64(master_time));

				start = found+1;
				found = videoInfo.find_first_of(" ,;",found+1);
				//double master_system = atoi(videoInfo.substr(start,found).c_str());
				uint64_t master_system = 0x0;// = _atoi64(videoInfo.substr(start,found).c_str());
				sscanf_s(videoInfo.substr(start,found).c_str(), "%016I64x", &master_system);
				//printf("master_system: %f - %I64u\n", master_system, ntoh64(master_system));

				//*master_time = (uint64_t)0x1234567898765432;
				//master_system = 0x9876543212345678;
				//printf("master_time  %016I64x : %I64u >>> master_system: %016I64x - %I64u\n", master_time, ntoh64(master_time), master_system, ntoh64(master_system));

				if(delay < 0.0)
				{
					printf("UDP stream stop. Video not init.\n");
					notifyStopOrRestartVideo(side);
				}

				*snt_date = sent_date;
				//g_UDPDelay = delay;
				return delay;
			}
			else
			{
				//printf("Wrong client id--->.\n");
			}
		}
	}
	//else if(received == SOCKET_TIMEOUT)
		//printf("\nVIDEO SOCKET TIMED OUT.\n");
	else
		printf("No data received = %d\n", received);

	return -1;
}
/* Send UDP get clock command to the audio server.
 */
void sendGetAudioClockCommand(int stream)
{
	double sent_date = getGlobalVideoTimer(stream) / (double)1000;

	char buf[256];
    sprintf(buf, "%d;%d;%.20f;", _clientSenderID, stream, sent_date);
	std::string command = "GET_AUDIO_CLOCK;";
	command.append(buf);
	//command += STR_END_MSG;
	//commandReceiver->send(command);

	//printf("COMMAND TO SERVER:  %s", command.c_str());
	bool sent = udpConnection->Send(command.c_str(),command.length());
	if(!sent)
		printf("**********Couldn't send UDP data**********\n");
}

void sendGetAudioClockCommand(int stream, double sent_date)
{
	char buf[256];
    sprintf(buf, "%d;%d;%.20f;", _clientSenderID, stream, sent_date);
	std::string command = "GET_AUDIO_CLOCK;";
	command.append(buf);
	//command += STR_END_MSG;
	//commandReceiver->send(command);

	//printf("COMMAND TO SERVER:  %s", command.c_str());
	bool sent = udpConnection->Send(command.c_str(),command.length());
	if(!sent)
		printf("**********Couldn't send UDP data**********\n");
}
////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////// TCP ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Setup a TCP connection and wait for the "READY" signal to be sent.
 */
int connectToAudioServer()
{
	printf("Setting up TCP server!\n");
	if(commandReceiver != NULL)
	{
		delete commandReceiver;
	}
	commandReceiver = new ofxTCPClient();

	// Setup TCP client to listen to incoming commands from the touch server
	while(!commandReceiver->setup(_tcpAudioServerIP, _tcpAudioServerPort))
	{
		printf("Waiting to connect to audio server...\n");
		Sleep(100);
	}
	
	printf("Initial TCP connection made.\nWaiting for a response from the server.\n");
	startNetTimer();
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
			double timeout = getNetTimer();
			if(timeout > READY_TIMEOUT)
			{
				printf("Timeout waiting for READY...\n");
				return false;
			}
			if(commands.size() == 0)
				Sleep(100);
	}
	printf("Connected to TCP server!\n");
	commandReceiver->setVerbose(false);

	char buf[256];
	sprintf(buf, "%d;", _clientSenderID);
	std::string videoInfo = "CLIENTID;";
	videoInfo.append(buf);
	//videoInfo += STR_END_MSG;
	commandReceiver->send(videoInfo);

	connectToUDPServer();

#if 0
	if(broadcastPTS)
		setupPTSUDPServer();
#endif

	return true;
}

/* Send TCP play video command to the audio server.
 */
int playVideoCommand(int clientID, int side, char* fileName)
{
	//udpConnection->SetNonBlocking(true);

	char buf[256];
	sprintf(buf, "%d;%d;%s;", clientID, side, fileName);
	std::string videoInfo = "PLAY_VIDEO;";
	videoInfo.append(buf);
	//videoInfo += STR_END_MSG;
	commandReceiver->send(videoInfo);
	printf("Send play command:  %s\n", videoInfo.c_str());
	return 0;
}
/* Send TCP stop video command to the audio server.
 */
int stopVideoCommand(int clientID, int side)
{
	//udpConnection->SetNonBlocking(false);

	char buf[256];
	sprintf(buf, "%d;%d;", clientID, side);
	std::string videoInfo = "STOP_VIDEO;";
	videoInfo.append(buf);
	//videoInfo += STR_END_MSG;
	commandReceiver->send(videoInfo);
	printf("Send stop command:  %s\n", videoInfo.c_str());
	return 0;
}
/* Send TCP pause video command to the audio server.
 */
int pauseVideoCommand(int clientID, int side)
{
	char buf[256];
	sprintf(buf, "%d;%d;", clientID, side);
	std::string videoInfo = "PAUSE_VIDEO;";
	videoInfo.append(buf);
	//videoInfo += STR_END_MSG;
	commandReceiver->send(videoInfo);
	printf("Send pause command:  %s\n", videoInfo.c_str());

	//udpConnection->SetNonBlocking(false);

	return 0;
}
/* Send TCP change volume command to the audio server.
 */
int changeVolumeCommand(int clientID, int side, float vol)
{
	vol = (vol < 0) ? 0 : vol;
	vol = (vol > 1) ? 1 : vol;
	char buf[256];
	sprintf(buf, "%d;%d;%f;", clientID, side, vol);
	std::string videoInfo = "CHANGE_VOLUME;";
	videoInfo.append(buf);
	//videoInfo += STR_END_MSG;
	commandReceiver->send(videoInfo);
	printf("Send change volume command:  %s\n", videoInfo.c_str());
	return 0;
}
/* Send TCP seek stream command to the audio server.
 */
int seekVideoCommand(int clientID, int side, double seekDur)
{
	char buf[256];
	sprintf(buf, "%d;%d;%f;", clientID, side, seekDur);
	std::string videoInfo = "SEEK_VIDEO;";
	videoInfo.append(buf);
	//videoInfo += STR_END_MSG;
	commandReceiver->send(videoInfo);
	printf("Send change seek command:  %s\n", videoInfo.c_str());
	return 0;
}
/* Parse and process incoming TCP commands.
 * Commands:
 * VIDEO_FINISHED, int side
 */
void parseTCPCommands()
{
	std::string remainder;
	std::vector<std::string> commands;
	// Listen for incoming commands
	if (commandReceiver->receiveStrings(commands, remainder) > 0)
	{
		//for (int i = commands.size()-1; i >= 0; i--)
		for (int i = 0; i < commands.size(); i++)
	   {
		   if(!commands[i].empty() && (commands[i].compare(STR_END_MSG) != 0))
		   // If the command string is not blank
		   //if(!commands[i].empty())
		   {
				//printf("commands:: %s\n", commands[i].c_str());
				int found = 0, start = 0;
				found = commands[i].find_first_of(" ,;");
				string command = commands[i].substr (start,found-start);

#if 0
				if(command.compare("DELAY") == 0)
				{
					start = found+1;
					found = commands[i].find_first_of(" ,;",found+1);
					int side = atoi(commands[i].substr(start,found).c_str());
					start = found+1;
					found = commands[i].find_first_of(" ,;",found+1);
					double delay = atof(commands[i].substr(start,found).c_str());
					printf("AUDIO_CLOCK[%d]: %f\n", side, delay);
				}
#endif
				if(command.compare("VIDEO_FINISHED") == 0)
				{
					start = found+1;
					found = commands[i].find_first_of(" ,;",found+1);
					int side = atoi(commands[i].substr(start,found).c_str());

					printf("\nNET::Recieved stop command ---- SIDE = %d\n", side);
					//stopVideo(side);
					notifyStopOrRestartVideo(side);
				}
				if(command.compare("LOAD_VIDEO") == 0)
				{
					//start = found+1;
					//found = commands[i].find_first_of(" ,;",found+1);
					//string videoName = commands[i].substr(start,found).c_str();
					start = found+1;
					found = commands[i].find_first_of(";", found+1);
					string videoName = commands[i].substr(start,found-start);

					start = found+1;
					found = commands[i].find_first_of(" ,;",found+1);
					int side = atoi(commands[i].substr(start,found).c_str());

					printf("\nNET::Recieved load command ---- VIDEONAME: %s ---- SIDE = %d\n", videoName.c_str(), side);
					//notifyScreenSyncLoadVideo("C:\\Users\\THoR\\Desktop\\VJ\\Vids\\out.mp4", side);
					notifyScreenSyncLoadVideo(videoName, side);
				}
				if(command.compare("SEEK_VIDEO") == 0)
				{
					start = found+1;
					found = commands[i].find_first_of(" ,;",found+1);
					int side = atoi(commands[i].substr(start,found).c_str());
					start = found+1;
					found = commands[i].find_first_of(" ,;",found+1);
					double seekBaseTime = atof(commands[i].substr(start,found).c_str());
					start = found+1;
					found = commands[i].find_first_of(" ,;",found+1);
					double seekDur = atof(commands[i].substr(start,found).c_str());

					printf("\nNET::Recieved seek command ---- SIDE: %d ---- SEEKDUR = %f ---- SEEKBASETIME = %f\n", side, seekDur, seekBaseTime);
					seekVideo(side, seekDur, seekBaseTime, false);
					//notifyScreenSyncLoadVideo(videoName, side);
				}
				if(command.compare("PAUSE_VIDEO") == 0)
				{
					start = found+1;
					found = commands[i].find_first_of(" ,;",found+1);
					int side = atoi(commands[i].substr(start,found).c_str());

					printf("\nNET::Recieved pause command ---- SIDE = %d\n", side);
					pauseVideo(side, false);
				}
#if 0
				else if(command.compare("HEARTBEAT") == 0)
				{
					#ifdef DEBUG_PRINTF
						//printf("-------------------Heart-------------------\n");
					#endif
				}
#endif
		   }
	   }
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////// Net sync thread ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////

void syncThread(void* dummy)
{
	//return;
	//DWORD_PTR threadAffMask = SetThreadAffinityMask(GetCurrentThread(), core);
	//if(threadAffMask == 0)
	//	printf("===Setting threadAffMask failed!!!\n");

	double now[MAXSTREAMS], start[MAXSTREAMS];
	//double now = 0.0, start = 0.0;
	int totalActiveVideos = 0;

	if(!broadcastAudio)
	{
		udpConnection->SetNonBlocking(false);
		udpConnection->SetTimeoutReceive(1);

		//udpConnection->SetNonBlocking(true);
	}
	else
	{
		//udpConnection->SetTimeoutReceive(0);
		//udpConnection->SetNonBlocking(true);

		udpConnection->SetNonBlocking(false);
		udpConnection->SetTimeoutReceive(1);
	}

	while(true)
	{
		totalActiveVideos = 0;
#if 1

#if 0
		if(getNetTimer() > 250)
		{
			startNetTimer();
			if(/*!commandReceiver->send("") && */!commandReceiver->isConnected()) // Check if the client is still connected to the audio server
			{
				printf("###Re-connecting to the server...\n");
				audioClientSetup(); // If not setup TCP/UDP connection

				if(!broadcastAudio)
				{
					udpConnection->SetNonBlocking(false);
					udpConnection->SetTimeoutReceive(2);

					//udpConnection->SetNonBlocking(true);
				}
				else
				{
					//udpConnection->SetTimeoutReceive(0);
					udpConnection->SetNonBlocking(true);
				}
			}

		}
		if(commandReceiver->isConnected())
			parseTCPCommands();
#endif

		for (int i = 0 ; i < MAXSTREAMS; i++)
		{
			if(videoTypeStreams[i] == ffmpeg)
				if (status[i] == aplay)
				{
					totalActiveVideos++;
#if 0
					netAudioClock = g_UDPDelay+(avgNetLatency/(double)1000);
#ifdef NETWORKED_AUDIO
					//if(netAudioClock == preClock[i] || netAudioClock < 0)
					if(preClock[i] > netAudioClock)
					{
						netAudioClock = -1;
						preClock[i] = 0.0;
					}
					else
					{
						preClock[i] = netAudioClock;
						diff[i] = (netAudioClock-currGlobalTimer[i]);
						//diff[i] = diff[i] > 0 ? diff[i] : (avgNetLatency/(double)1000);
					}
					double newClock = currGlobalTimer[i] + diff[i];
					//double newClock = 0.0;
#endif
#endif
					//now = getGlobalVideoTimer(i) / (double)1000;
					now[i] = getGlobalVideoTimer(i) / (double)1000;
					//printf("now[%d]: %f - start[%d]: %f - fabs[%d]: %f\n", i, now[i], i, start[i], i, fabs(now[i]-start[i]));
					//if(fabs(now[i]-start[i]) > 0.01)
					//if(fabs(now-start) > 0.01)
					{
						start[i] = now[i];
						//	start = now;
						if(!broadcastAudio)
						{
							sendGetAudioClockCommand(i);
							//printf(" - Sent clock command(%d)\n", i);
						}
					//}
					
						double sent_date = -1;

						//double udpAudioClock = parseUDPCommands(i, &sent_date);
						uint64_t master_time = 0x0;
						double udpAudioClock = parseUDPCommands(i, &sent_date, &master_time);
						//printf("master_time>%016I64x : %I64u : %f\n", master_time, ntoh64(master_time), master_time / 1000000.0);

						double receive_date = getGlobalVideoTimer(i) / (double)1000;
						//netAudioCloeck[i] = -1;
						if(sent_date >= 0 && udpAudioClock >= 0)
						{
							double rTT = ((receive_date - sent_date) / 2.0f);
							if(rTT > minLatency)
							{
								printf("late packet - RTT: %09.6f\n", rTT);
								rTT = minLatency;
							}
							if(!broadcastAudio)
								netAudioClock[i] = rTT + udpAudioClock;
							else
								netAudioClock[i] = (udpAudioClock);
							//netAudioClock[i] = master_time;
							//netAudioClock[i] =  /*((receive_date - sent_date) / 2.0f) +*/ (master_time / 1000000.0);
							//printf(">>>>Recieved timestamp: %I64u\n", netAudioClock[i]);
						}
						//printf(">>>>Recieved timestamp %d - now: %06.3f - netAudioClock: %06.3f - ServerClock: %06.3f - sent_date: %06.3f - receive_date: %06.3f - RTT: %06.3f.\n", i, now[i], netAudioClock[i], udpAudioClock, sent_date, receive_date, receive_date-sent_date);
					}
				}
		}

		if(!broadcastAudio)
		{
			Sleep(10); // give up resources.
			//Sleep(PCFreq / 20);
		}
#endif
		//DWORD core = GetCurrentProcessorNumber();
		//printf("~~~~~Network Thread - Process Affinity: %d\n", core);
		if(totalActiveVideos == 0)
		{
			//printf("~~~~~Network Thread - Sleeping\n");
			Sleep(100);
			//start = 0;
			//now = 0;
			for(int i=0; i<MAXSTREAMS; i++)
			{
				start[i] = 0;
				now[i] = 0;
			}
		}
	}
}

void tcpThread(void* dummy)
{
	while(true)
	{
#if 1
#ifdef NETWORKED_AUDIO
		if(getNetTimer() > 250)
		{
			startNetTimer();
			if(/*!commandReceiver->send("") && */!commandReceiver->isConnected()) // Check if the client is still connected to the audio server
			{
				printf("###Re-connecting to the server...\n");
				audioClientSetup(); // If not setup TCP/UDP connection
			}
#endif
		}
		
		if(commandReceiver->isConnected())
			parseTCPCommands();

		Sleep(10); // give up resources.
		//Sleep(PCFreq / 20);

#endif
	}
}
#endif