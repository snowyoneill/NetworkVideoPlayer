#if 1
#include "netClockSyncWin.h"
//#include "videoplayback.h"
#include <process.h>

extern bool broadcastAudio;
#if 0
static bool broadcastPTS = false;
#endif

#ifdef NETWORKED_AUDIO
int _clientSenderID = -1;
int _udpAudioServerPort = 2008;
char* _udpMCastIP = "224.0.0.0";
char* _udpAudioServerIP = "127.0.0.1";
char* _tcpAudioServerIP = "127.0.0.1"; // default TCP server IP
int _tcpAudioServerPort = 2007; // default port

double netAudioClock[MAXSTREAMS];// = 0;
double avgNetLatency = 0;
double minLatency = LONG_MAX;
double g_UDPDelay = 0.0;

enum {astop, aplay, invalid};
enum {ffs = 1, ffmpeg}; // 1 = ffs, 2 = ffmpeg

#define UDP_BUFFER_SIZE 128
char msg[UDP_BUFFER_SIZE];

extern void seekVideo(int side, double seekDuration, double seekBaseTime = -1, bool sendSeekTCPCommand = true);
#endif

#endif

bool verbose = false;
bool init = false;
bool tcpConnected = true;

WSADATA wsaData;
SOCKET ConnectSocket = INVALID_SOCKET;
SOCKET ConnectSocketUDP = INVALID_SOCKET;

static void SetNonBlocking(bool useNonBlocking)
{
    u_long sock_flags;
    sock_flags = useNonBlocking;
    ioctlsocket(ConnectSocketUDP, FIONBIO, &sock_flags);
}

bool closeWinSock()
{
	if(init)
	{
		if (ConnectSocket != INVALID_SOCKET)
		{
			#ifdef TARGET_WIN32
				if(closesocket(ConnectSocket) == SOCKET_ERROR)
			#else
				if(close(ConnectSocket) == SOCKET_ERROR)
			#endif
			{
				return(false);
			}
		}
		if(WSACleanup() != SOCKET_ERROR)
		{
			init = false;
			return true;
		}
	}

	return false;
}
bool initWinSock()
{
    int iResult;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return false;
    }
	init = true;

	return true;
}

int initTCPClient()
{
    struct addrinfo *result = NULL,
                    *ptr = NULL,
                    hints;

    int iResult;

    ZeroMemory( &hints, sizeof(hints) );
    //hints.ai_family = AF_UNSPEC;
	hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

	char portStr[5];
	sprintf(portStr, "%d", _tcpAudioServerPort);

    // Resolve the server address and port
    iResult = getaddrinfo(_tcpAudioServerIP, portStr, &hints, &result);
    if ( iResult != 0 ) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Attempt to connect to an address until one succeeds
    for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) {

        // Create a SOCKET for connecting to server
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            //WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect( ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            //closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            //continue;
			return 2;
        }
        break;
    }

    freeaddrinfo(result);

    if (ConnectSocket == INVALID_SOCKET) {
        printf("Unable to connect to server!\n");
        //WSACleanup();
        return 1;
    }

	//// Set non-blocking
	//unsigned long l = 1;
	//DWORD Returned = 0;
	//if ( SOCKET_ERROR == WSAIoctl ( ConnectSocket, FIONBIO, (LPVOID)&l, sizeof(l), NULL, 0, &Returned, NULL, NULL ) )
	//{
	//	printf("Failed to set non-blocking mode, error %d\n", WSAGetLastError() );
	//}

	return 0;
}

SOCKADDR_IN local_sin;
int initUDPClient()
{
#if 1
    struct addrinfo *result = NULL,
                    *ptr = NULL,
                    hints;
    
    int iResult;

    ZeroMemory( &hints, sizeof(hints) );
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;

	char portStr[5];
	sprintf(portStr, "%d", _udpAudioServerPort);

    // Resolve the server address and port
	iResult = getaddrinfo(_udpAudioServerIP, portStr, &hints, &result);
    if ( iResult != 0 ) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }


    // Attempt to connect to an address until one succeeds
    for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) {

        // Create a SOCKET for connecting to server
        ConnectSocketUDP = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if (ConnectSocketUDP == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }

		if(!broadcastAudio)
		{
			// Connect to server.
			iResult = connect( ConnectSocketUDP, ptr->ai_addr, (int)ptr->ai_addrlen);
			if (iResult == SOCKET_ERROR) {
				closesocket(ConnectSocketUDP);
				ConnectSocket = INVALID_SOCKET;
				continue;
			}
			printf("Connected!\n");
		}
		else
		{
			int one = 1;
			setsockopt(ConnectSocketUDP, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));

			//SOCKADDR_IN local_sin;
			// Fill out the local socket's address information.
			local_sin.sin_family = AF_INET;
			local_sin.sin_port = htons (_udpAudioServerPort);  
			local_sin.sin_addr.s_addr = htonl (INADDR_ANY);
			iResult = bind( ConnectSocketUDP, (struct sockaddr FAR *) &local_sin, sizeof (local_sin));

			// Connect to server.
			//iResult = bind( ConnectSocketUDP, ptr->ai_addr, (int)ptr->ai_addrlen);
			if (iResult == SOCKET_ERROR) {
				closesocket(ConnectSocketUDP);
				ConnectSocket = INVALID_SOCKET;
				continue;
			}

			struct ip_mreq mreq;
			mreq.imr_multiaddr.s_addr = inet_addr(_udpAudioServerIP);
			mreq.imr_interface.s_addr = INADDR_ANY;
			int err = setsockopt(ConnectSocketUDP, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&mreq, sizeof(mreq));
			if( err == SOCKET_ERROR )
			{
				printf("setsockopt failed! Error: %d", WSAGetLastError ());
				closesocket (ConnectSocketUDP);
				return FALSE;
			}
			printf("Bind to multicast!\n");
		}
        break;
    }

    freeaddrinfo(result);

    if (ConnectSocketUDP == INVALID_SOCKET) {
        printf("Unable to connect to server!\n");
        WSACleanup();
        return 1;
    }
#endif

	//// Set non-blocking
	//unsigned long l = 1;
	//DWORD Returned = 0;
	//if ( SOCKET_ERROR == WSAIoctl ( ConnectSocketUDP, FIONBIO, (LPVOID)&l, sizeof(l), NULL, 0, &Returned, NULL, NULL ) )
	//{
	//	printf("Failed to set non-blocking mode, error %d\n", WSAGetLastError() );
	//}
	//WSAEWOULDBLOCK

	return 0;
}

/************************************* TCP *************************************/

int sendTCPData(const char* msg)
{
	int iResult;
	//char sendbuf[DEFAULT_BUFLEN];
	//sendbuf = "CLIENTID;4;";
	//memset(sendbuf, 0, DEFAULT_BUFLEN);
	//sendbuf[0] = STR_END_MSG;
	//strcpy(sendbuf, "CLIENTID;4;");
	char *sendbuf = (char*)msg;//"this is a test";
	//sendbuf[12] = (char)"";//(char)0; //for flash
	    // Send an initial buffer
    iResult = send( ConnectSocket, sendbuf, DEFAULT_BUFLEN, 0 );
    if (iResult == SOCKET_ERROR) {
        printf("send failed with error: %d\n", WSAGetLastError());
        closesocket(ConnectSocket);
        WSACleanup();
        return 1;
    }

    //printf("Bytes Sent:%ld\n", iResult);
	//printf("Bytes Sent:%s\n", sendbuf);

    //// shutdown the connection since no more data will be sent
    //iResult = shutdown(ConnectSocket, SD_SEND);
    //if (iResult == SOCKET_ERROR) {
    //    printf("shutdown failed with error: %d\n", WSAGetLastError());
    //    closesocket(ConnectSocket);
    //    WSACleanup();
    //    return 1;
    //}

	return 0;
}

int getTCPData(char* msg)
{
#if 1
    char recvbuf[DEFAULT_BUFLEN];
    int iResult = -1;
    int recvbuflen = DEFAULT_BUFLEN;

    // Receive until the peer closes the connection
    //do {

	int total = 0;
	bool found = false;
	//while(!found)
	//{

        iResult = recv(ConnectSocket, recvbuf, recvbuflen, 0);
        if ( iResult > 0 )
		{
            //printf("Bytes received: %d - %s\n", iResult, recvbuf);
			//recvbuf[iResult] = '\0';
			//sprintf(msg, "%s", recvbuf);
			memcpy(msg, recvbuf, DEFAULT_BUFLEN);
			//msg = "Hello";//recvbuf;

			//total += iResult;
			//found = (strchr(recvbuf, '\n') != 0);
		}
        else if ( iResult == 0 )
		{
            printf("Connection closed\n");
		}
        else
		{
			// Check for disconnect.
			int err = WSAGetLastError();   // This gets err code from recv() call inside recvRawBytes.
			if ((iResult == 0) || ((iResult < 0) && (err != WSAEWOULDBLOCK) && (err != WSAETIMEDOUT))) 
			{
				//if (verbose)
					//printf("ofxTCPCient: Client on %s disconnected\n", ipAddr.c_str());
					printf("recv failed with error: %d\n", err);
			}
            //printf("recv failed with error: %d\n", WSAGetLastError());
		}

	//}
    //} while( iResult > 0 );
#endif

	return iResult;
}



/************************************* UDP *************************************/

int sendUDPData(const char* msg)
{
	int iResult;
	//memset(sendbuf, 0, DEFAULT_BUFLEN);
	char *sendbuf = (char*)msg;//"this is a test";
	//sendbuf[12] = (char)"";//(char)0; //for flash
	// Send an initial buffer
		//(int)strlen(sendbuf)
    iResult = send( ConnectSocketUDP, sendbuf, DEFAULT_BUFLEN, 0 );
    if (iResult == SOCKET_ERROR) {
        printf("send failed with error: %d\n", WSAGetLastError());
        closesocket(ConnectSocketUDP);
        WSACleanup();
        return false;
    }

    //printf("Bytes Sent:%ld\n", iResult);
	//printf("Bytes Sent:%s\n", sendbuf);

    //// shutdown the connection since no more data will be sent
    //iResult = shutdown(ConnectSocketUDP, SD_SEND);
    //if (iResult == SOCKET_ERROR) {
    //    printf("shutdown failed with error: %d\n", WSAGetLastError());
    //    closesocket(ConnectSocketUDP);
    //    WSACleanup();
    //    return 1;
    //}

	return true;
}

int getUDPData(char* msg)
{
    char recvbuf[DEFAULT_BUFLEN];
    int iResult = -1;
    int recvbuflen = DEFAULT_BUFLEN;

    // Receive until the peer closes the connection
    //do {

        //iResult = recv(ConnectSocketUDP, recvbuf, recvbuflen, 0);
		memset(recvbuf, 0, DEFAULT_BUFLEN);
		socklen_t nLen= sizeof(sockaddr);
		iResult = recvfrom(ConnectSocketUDP, recvbuf, recvbuflen, 0, (sockaddr *)&local_sin, &nLen);

        if ( iResult > 0 )
		{
            //printf("Bytes received: %d - %s\n", iResult, recvbuf);
			recvbuf[iResult] = '\0';
			sprintf(msg, "%s", recvbuf);
		}
        else if ( iResult == 0 )
            printf("Connection closed\n");
        else
            ;//printf("recv failed with error: %d\n", WSAGetLastError());

    //} while( iResult > 0 );

	return iResult;
}


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
#if 1
////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////// Setup  ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Test the network latency.
 */
double testNetworkLatency()
{
	printf("Testing network latency...please wait.\n");
	SetNonBlocking(false);
	//udpConnection->SetTimeoutReceive(NO_TIMEOUT);
	#define TOTAL	20
	double latencyTimes[TOTAL];
	for (int i = 0; i < TOTAL; i++)
	{
		double netClock;
		double sent_date = -1;
		startGlobalVideoTimer(currentVideo);
		//sendGetAudioClockCommand(0);
		sendGetAudioClockCommand(currentVideo, sent_date);
		do {
			//printf("here\n");
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

	//_beginthread(syncThread, 0, 0);
	_beginthread(tcpThread, 0, 0);
}

bool audioClientSetup()
{
	if(!connectToAudioServer())
	{
		printf("ERROR: Couldn't connected to audio server\n");
		return false;
	}

	tcpConnected = true;

	return true;
}



#endif
/*********************************** SETUP *************************************/
int connectToAudioServer()
{
	initWinSock();
	printf("Setting up TCP server!\n");
	//if(initTCPClient())
	//{
	//	printf("Could not connected to TCP Server!\n");
	//	return false;
	//}

	// Setup TCP client to listen to incoming commands from the touch server
	while(initTCPClient() == 2)
	{
		printf("Waiting to connect to audio server...\n");
		Sleep(100);
	}

	printf("Initial TCP connection made.\nWaiting for a response from the server.\n");
	bool readyRev = false;
	double timeout = 0.0;
	startNetTimer();

	int recieved;
	char message[DEFAULT_BUFLEN];
	while(!readyRev)
	{
		recieved = getTCPData(message);
		timeout = getNetTimer();

		std::string commands;
		commands.assign(message, DEFAULT_BUFLEN);

		//printf("message: %s\n", message);

		if(recieved > 1)
		{
			int found = 0, start = 0;

			//while(start < DEFAULT_BUFLEN)
			{
				found = commands.find_first_of(" ,;", start);
				std::string command = commands.substr (start,found-start);
				start = found+1;

				//if(stricmp(message, "READY") == 0)
				if(command.compare("READY") == 0)
				{
					readyRev = true;
					break;
				}	
			}

		}
		if(recieved < 1)
			Sleep(10);

		//if(timeout > READY_TIMEOUT)
		//{
		//	printf("Timeout waiting for READY...\n");
		//	return false;
		//}

		printf("recieved %d - TCPMSG:%s:END - compare: %s\n", recieved, message, stricmp(message, "READY;") == 0 ? "true" : "false");
	}
	printf("READY recieved.\n");

	{
		char buf[512];
		sprintf(buf, "%d;", _clientSenderID);
		std::string videoInfo = "CLIENTID;";
		videoInfo.append(buf);
		//videoInfo += STR_END_MSG;
		//commandReceiver->send(videoInfo);
		sendTCPData(videoInfo.c_str());
	}

	printf("Connected to TCP server!\n");

	connectToUDPServer();

	return true;
}

///////////////////////////////// Network //////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////// UDP ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
#if 1
/* Setup a new blocking UDP connection.
 */
int connectToUDPServer()
{
	if(initUDPClient())
	{
		printf("Could not connected to UDP Server!\n");
		return false;
	}

	return true;
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
	int received = getUDPData(msg);
	string message=msg;

	if(broadcastAudio)
	{
		SetNonBlocking(true);
		string tempMessage = "start";
		bool getNext = true;
		while (getNext)
		{
			sprintf(msg, "");
			int received = getUDPData(msg);
			tempMessage=msg;
			
			if (tempMessage == "")
			{
				getNext = false;
				//break;
				//printf("UDP buffer finished:--- %s - %s |\n", tempMessage.c_str(), message.c_str());
			}
			else
			{
				message = tempMessage;
				//printf("UDP buffer: %s\n", message.c_str());
				//printf("UDP buffer: %s | - received: %d\n", tempMessage.c_str(), received);
			}
		}
		SetNonBlocking(false);
	}

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

			if(/*clientID == _clientSenderID &&*/ lside == side)
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
	//else
	//	printf("No data received = %d\n", received);

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
	//bool sent = udpConnection->Send(command.c_str(),command.length());
	bool sent = sendUDPData(command.c_str());
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
	//bool sent = udpConnection->Send(command.c_str(),command.length());
	bool sent = sendUDPData(command.c_str());
	if(!sent)
		printf("**********Couldn't send UDP data**********\n");
}
////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////// TCP ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Setup a TCP connection and wait for the "READY" signal to be sent.
 */

/* Send TCP play video command to the audio server.
 */
int playVideoCommand(int clientID, int side, char* fileName)
{
	//udpConnection->SetNonBlocking(true);

	char buf[256];
	sprintf(buf, "%d;%d;%s;", clientID, side, fileName);
	std::string videoInfo = "PLAY_VIDEO;";
	videoInfo.append(buf);

	sendTCPData(videoInfo.c_str());
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

	sendTCPData(videoInfo.c_str());
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

	sendTCPData(videoInfo.c_str());
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

	sendTCPData(videoInfo.c_str());
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

	sendTCPData(videoInfo.c_str());
	printf("Send change seek command:  %s\n", videoInfo.c_str());
	return 0;
}
/* Parse and process incoming TCP commands.
 * Commands:
 * VIDEO_FINISHED, int side
 */
//#define STR_END_MSG "[/TCP]"
#define STR_END_MSG ""

void parseTCPCommands()
{
#if 1
	//std::string remainder;
	//std::vector<std::string> commands;
	//// Listen for incoming commands
	
	char message[DEFAULT_BUFLEN];
	int recieved = getTCPData(message);

	int i=0;
	std::string commands[1];
	if (recieved > 1)
	{
		commands[i].assign(message, DEFAULT_BUFLEN);
		//commands.push_back(message);
	}
	else if(recieved == 0)
	{
		tcpConnected = false;
	}

		//for (int i = commands.size()-1; i >= 0; i--)
		//for (int i = 0; i < commands.size(); i++)
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
				//commands.pop_back();
		   }
	   }
#endif
}
////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////// Net sync thread ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////

void syncThreadCheckMessagesBlk(int side)
{
	SetNonBlocking(false);
	syncThreadCheckMessages(side);
}

void syncThreadCheckMessages(int side)
{
	double now[MAXSTREAMS], start[MAXSTREAMS];
	//double now = 0.0, start = 0.0;
	int i = side;

	SetNonBlocking(true);

	//if(!broadcastAudio)
	//{
	//	SetNonBlocking(false);
	//	//udpConnection->SetTimeoutReceive(1);

	//	//udpConnection->SetNonBlocking(true);
	//}
	//else
	//{
	//	//udpConnection->SetTimeoutReceive(NO_TIMEOUT);
	//	//udpConnection->SetNonBlocking(true);

	//	SetNonBlocking(false);
	//	//udpConnection->SetTimeoutReceive(NO_TIMEOUT);
	//}

#if 1

		{
			if(videoTypeStreams[i] == ffmpeg)
				if (status[i] == aplay)
				{
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
							//printf(">>>>Recieved timestamp %d - now: %06.3f - netAudioClock: %06.3f - ServerClock: %06.3f - sent_date: %06.3f - receive_date: %06.3f - RTT: %06.3f.\n", i, now[i], netAudioClock[i], udpAudioClock, sent_date, receive_date, receive_date-sent_date);
						}
						//printf(">>>>Recieved timestamp %d - now: %06.3f - netAudioClock: %06.3f - ServerClock: %06.3f - sent_date: %06.3f - receive_date: %06.3f - RTT: %06.3f.\n", i, now[i], netAudioClock[i], udpAudioClock, sent_date, receive_date, receive_date-sent_date);
					}
				}
		}
#endif
}

void syncThread(void* dummy)
{
	double now[MAXSTREAMS], start[MAXSTREAMS];
	//double now = 0.0, start = 0.0;
	int totalActiveVideos = 0;

	if(!broadcastAudio)
	{
		SetNonBlocking(false);
		//udpConnection->SetTimeoutReceive(1);

		//udpConnection->SetNonBlocking(true);
	}
	else
	{
		//udpConnection->SetTimeoutReceive(NO_TIMEOUT);
		//udpConnection->SetNonBlocking(true);

		SetNonBlocking(false);
		//udpConnection->SetTimeoutReceive(NO_TIMEOUT);
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
							//printf(">>>>Recieved timestamp %d - now: %06.3f - netAudioClock: %06.3f - ServerClock: %06.3f - sent_date: %06.3f - receive_date: %06.3f - RTT: %06.3f.\n", i, now[i], netAudioClock[i], udpAudioClock, sent_date, receive_date, receive_date-sent_date);
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

			//udpConnection->SetNonBlocking(true);
			////udpConnection->SetTimeoutReceive(0);
			//string message;
			//bool getNext = true;
			//while (getNext)
			//{
			//	sprintf(msg, "");
			//	int bufsize = udpConnection->GetReceiveBufferSize();
			//	int received = udpConnection->Receive(msg, bufsize);
			//	message=msg;

			//	if (message == "")
			//	{
			//		getNext = false;
			//		//printf("UDP buffer finished:-------------\n");
			//	}
			//}
		}
	}
}

//bool isConnected()
//{
//	char recvbuf[DEFAULT_BUFLEN];
//    int iResult = -1;
//	iResult = recv(ConnectSocket, recvbuf, DEFAULT_BUFLEN, 0);
//	return ( iResult == 0 ) ? false : true;
//}

bool isConnected()
{
	return tcpConnected;
//	char recvbuf[DEFAULT_BUFLEN];
//    int iResult = -1;
//	iResult = recv(ConnectSocket, recvbuf, DEFAULT_BUFLEN, 0);
//	return ( iResult == 0 ) ? false : true;
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
			if(/*!commandReceiver->send("") && */!isConnected()) // Check if the client is still connected to the audio server
			{
				printf("###Re-connecting to the server...\n");
				audioClientSetup(); // If not setup TCP/UDP connection
			}
#endif
		}
		
		if(isConnected())
			parseTCPCommands();

		Sleep(10); // give up resources.
		//Sleep(PCFreq / 20);

#endif
	}
}
#endif