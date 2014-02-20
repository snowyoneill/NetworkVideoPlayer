#ifndef _NETCLOCKSYNC_H
#define _NETCLOCKSYNC_H

#ifdef NETWORKED_AUDIO
#include "ODNetwork.h"

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



int sendPTSTime(int stream, double pts);

///////////////////////////////////////////// NETWORK //////////////////////////////////////////////////
#define READY_TIMEOUT 2500
#define VIDEO_UDP_TIMEOUT 1

extern ofxTCPClient* commandReceiver;
extern ofxUDPManager *udpConnection;
extern bool setupTCPClient;
void initNetSync(char *serverIP, int TCPPort, int UDPPort, int ClientID, int side, bool testLatency);

////////////////////////////////// UDP ///////////////////////////////////
int connectToUDPServer();
//double parseUDPCommands();
double parseUDPCommands(int lside, double *snt_date);
typedef unsigned __int64     uint64_t;
double parseUDPCommands(int lside, double *snt_date, uint64_t *master_system);

////////////////////////////////// TCP ///////////////////////////////////
int connectToAudioServer();
int playVideoCommand(int clientID, int side, char* fileName);
int stopVideoCommand(int clientID, int side);
int pauseVideoCommand(int clientID, int side);
int changeVolumeCommand(int clientID, int side, float vol);
int seekVideoCommand(int clientID, int side, double seekDur);
void parseTCPCommands();

////////////////////////////////// MAIN ///////////////////////////////////
void sendGetAudioClockCommand(int stream);
void sendGetAudioClockCommand(int stream, double sent_date);
bool audioClientSetup();

void startGlobalVideoTimer(int videounit);
double getGlobalVideoTimer(int videounit);
void syncThread(void* dummy);

#endif

#endif