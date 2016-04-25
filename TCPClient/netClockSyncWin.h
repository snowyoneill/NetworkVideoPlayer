#if defined( __WIN32__ ) || defined( _WIN32 )
	#define TARGET_WIN32
#elif defined( __APPLE_CC__)
	#include <TargetConditionals.h>

	#if (TARGET_OF_IPHONE_SIMULATOR) || (TARGET_OS_IPHONE) || (TARGET_IPHONE)
		#define TARGET_OF_IPHONEz
		#define TARGET_OPENGLES
	#else
		#define TARGET_OSX
	#endif
#else
	#define TARGET_LINUX
#endif

#include "constants.h"
#include <string>

//#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment (lib, "Ws2_32.lib")

#define DEFAULT_BUFLEN 128

/************************************ INIT *************************************/
#define READY_TIMEOUT 2500

using namespace std;
void startGlobalVideoTimer(int videounit);
double getGlobalVideoTimer(int videounit);

typedef unsigned __int32  uint32_t;
typedef unsigned __int64     uint64_t;
#define ntoh64(x) (((uint64_t)(ntohl((uint32_t)(((uint64_t)x << 32) >> 32))) << 32) | \
                       (uint32_t)ntohl(((uint32_t)((uint64_t)x >> 32))))

#define hton64(x) ntohl(x)

extern double netAudioClock[MAXSTREAMS];
extern int status[MAXSTREAMS];
extern int videoTypeStreams[MAXSTREAMS];

//extern void stopVideo(int videounit);
extern void notifyStopOrRestartVideo(int videounit);
//extern void stopOrRestartVideo(int videounit);
extern void notifyScreenSyncLoadVideo(string videoName, int videounit);
//extern void screenSyncLoadVideo(string videoName, int videounit);
extern void pauseVideo(int videounit, bool sendPauseTCPCommand);

extern double PCFreq;

bool closeWinSock();
bool initWinSock();

bool Create();
bool Connect(std::string ip, int _port, bool blocking);

bool Setup(std::string ip, int _port, bool blocking);

int initTCPClient();
int initUDPClient();
/************************************* TCP *************************************/

int sendTCPData(const char* msg);
int getTCPData(char* msg);
/************************************* UDP *************************************/

int sendUDPData(const char* msg);
int getUDPData(char* msg);


///////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////// Setup  ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Test the network latency.
 */
double testNetworkLatency();

/* Initialise the network sync.
 */
void initNetSync(char *serverIP, int TCPPort, int UDPPort, int ClientID, int side, bool testLatency);

bool audioClientSetup();

/*********************************** SETUP *************************************/
int connectToAudioServer();

///////////////////////////////// Network //////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////// UDP ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Setup a new blocking UDP connection.
 */
int connectToUDPServer();
/* Check incoming UDP commands and return audio clock time for the requested stream (blocking).
 */
double parseUDPCommands(int lside, double *snt_date);

double parseUDPCommands(int lside, double *snt_date, uint64_t *master_time);
/* Send UDP get clock command to the audio server.
 */
void sendGetAudioClockCommand(int stream);

void sendGetAudioClockCommand(int stream, double sent_date);
////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////// TCP ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
/* Setup a TCP connection and wait for the "READY" signal to be sent.
 */

/* Send TCP play video command to the audio server.
 */
int playVideoCommand(int clientID, int side, char* fileName);
/* Send TCP stop video command to the audio server.
 */
int stopVideoCommand(int clientID, int side);
/* Send TCP pause video command to the audio server.
 */
int pauseVideoCommand(int clientID, int side);
/* Send TCP change volume command to the audio server.
 */
int changeVolumeCommand(int clientID, int side, float vol);
/* Send TCP seek stream command to the audio server.
 */
int seekVideoCommand(int clientID, int side, double seekDur);
/* Parse and process incoming TCP commands.
 * Commands:
 * VIDEO_FINISHED, int side
 */
void parseTCPCommands();
////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////// Net sync thread ///////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
void syncThreadCheckMessagesBlk(int side);
void syncThreadCheckMessages(int side);

void syncThread(void* dummy);

void tcpThread(void* dummy);