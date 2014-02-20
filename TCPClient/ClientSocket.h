//#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

// Need to link with Ws2_32.lib, Mswsock.lib, and Advapi32.lib
#pragma comment (lib, "Ws2_32.lib")
#pragma comment (lib, "Mswsock.lib")
#pragma comment (lib, "AdvApi32.lib")

extern int _clientSenderID;
extern int _udpAudioServerPort;
extern char* _udpAudioServerIP;
extern char* _tcpAudioServerIP; // default TCP server IP
extern int _tcpAudioServerPort; // default port

#define DEFAULT_BUFLEN 256

class ClientSocket
{
	bool verbose;
	bool init;

    WSADATA wsaData;
	SOCKET ConnectSocket;
	SOCKET ConnectSocketUDP;

	private:
		int initWinSock();
		int initTCPClient();
		int initUDPClient();

	public:
		ClientSocket();
		~ClientSocket();

		int sendTCPData(const char* msg);
		int getTCPData(char* msg);

		int sendUDPData(const char* msg);
		int getUDPData(char* msg);
};