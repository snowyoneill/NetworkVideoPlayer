//#ifndef VIDEO_PLAYBACK
//#define VIDEO_PLAYBACK

//#define LOG
#include "constants.h"

#include <string>
using namespace std;

extern bool		loopvideo;
extern bool		enableDebugOutput;
extern void		setWindowTitle(int clientID, string text);
extern bool		intelliSleep;
extern bool		enablePBOs;
extern bool		multiTimer;
extern bool		g_preloadAudio;
extern int		g_bufferSize;


extern double g_netAudioClock[MAXSTREAMS];
extern double preClock[MAXSTREAMS];
extern double nextFrameDelay[MAXSTREAMS];
extern double g_Delay[MAXSTREAMS];
extern double currGlobalTimer[MAXSTREAMS];
extern double diff[MAXSTREAMS];
extern double preTime[MAXSTREAMS];
extern double pts[MAXSTREAMS];
extern double frameTimeDiff[MAXSTREAMS];
extern double g_newClock[MAXSTREAMS];

extern enum	 {astop, aplay, apause, invalid};
extern int	 status[MAXSTREAMS];
extern double fps[MAXSTREAMS], duration[MAXSTREAMS];
extern int	 inwidth[MAXSTREAMS], inheight[MAXSTREAMS];


//extern void startGlobalVideoTimer(int videounit);
extern double getGlobalVideoTimer(int videounit);
extern void notifyStopOrRestartVideo(int videounit);
extern void notifyScreenSyncLoadVideo(string videoName, int videounit);

//////////////////////////////////////////// INITIALISE AUDIO /////////////////////////////////////////////////
void initVideoAudioPlayback(char *serverIP, int audioServerTCPPort, int audioServerUDPPort, int ClientID, int side, char* fileName, bool autostart, bool testLatency);
void closeAudioEnv();
///////////////////////////////////////////// LOAD FFS/VIDEO //////////////////////////////////////////////////
void initvideo();

////////////////////////////////////////////// FFS PLAYBACK //////////////////////////////////////////////////
bool loadffs(const char* path, int videounit);
void playffs(int videounit);
void stopffs(int videounit);
void updateffs();

////////////////////////////////////////////// VIDEO PLAYBACK ////////////////////////////////////////////////
//void updateVideo(void* dummy);
void updateVideo();
bool loadVideo(const char* path, int videounit);
//void playVideo(int videounit);
void playVideo(int videounit, bool sendPlayTCPCommand = true);
//void stopVideo(int videounit);
void stopVideo(int videounit, bool sendStopTCPCommand = true);

void screenSyncLoadVideo(string videoName, int videounit);
void stopOrRestartVideo(int videounit);

void pauseVideo(int videounit, bool sendPauseTCPCommand = true);
void changeVideoVolume(int videounit, float vol);
void seekVideo(int side, double seekDuration, double seekBaseTime = -1, bool sendSeekTCPCommand = true);

#ifdef LOCAL_AUDIO
double getOpenALAudioClock(int side);
double getFFmpegClock(int side);
#endif

void updateVideoDraw();
void updateVideoCallback(void* arg);
//void updateVideoCallback();

////////////////////////////////// MAIN ///////////////////////////////////
bool audioClientSetup();

//#endif