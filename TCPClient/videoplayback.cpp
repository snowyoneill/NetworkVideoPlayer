#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "videoplayback.h"
#include "frameloader.h"
#ifdef USE_ODBASE
#include "ODBase.h"
#endif
#include "GL/glew.h"
#include "GL/wglew.h"
#include <process.h>
#include <math.h>
#ifdef NETWORKED_AUDIO
#include "netClockSync.h"
extern int _clientSenderID;
#endif
//#define DEBUG_PRINTF
#define MULTI_TIMER

#include "pbo.h"

#ifdef NETWORKED_AUDIO
#ifdef USE_ODBASE
ODBase::Lock	commandMutex;
#else
HANDLE			commandMutex;
#endif
#endif

enum {ffs = 1, ffmpeg}; // 1 = ffs, 2 = ffmpeg

double	g_netAudioClock[MAXSTREAMS];
char	vidPath[MAX_PATH];
//char* vidPath;
char*	rawRGBData[MAXSTREAMS];
bool	ispaused[MAXSTREAMS];
double	pauseStart[MAXSTREAMS];
double	totalPauseLength[MAXSTREAMS];
GLuint	videotextures[MAXSTREAMS];
double	duration[MAXSTREAMS], fps[MAXSTREAMS];
int		inwidth[MAXSTREAMS], inheight[MAXSTREAMS];
int		status[MAXSTREAMS] = {astop};
double	currGlobalTimer[MAXSTREAMS];
double	preClock[MAXSTREAMS];
double	diff[MAXSTREAMS];
double	preTime[MAXSTREAMS];
int		videoTypeStreams[MAXSTREAMS];
double	pts[MAXSTREAMS];

#ifdef FFS
int compressionType[MAXSTREAMS], compressed[MAXSTREAMS], blocks[MAXSTREAMS];
int currentdisplayframe[MAXSTREAMS];
int lastdisplayed[MAXSTREAMS] = {-1};//,-1,-1,-1,-1,-1};
long long int startTime[MAXSTREAMS], interFrameTimeDiff[MAXSTREAMS];
#endif

#ifdef LOG
FILE *videoDebugLogFile = NULL;
#endif
//////////////////////////////////////////////// TIMERS ///////////////////////////////////////////////////////
__int64 GlobalVideoCounter[MAXSTREAMS];
__int64 CounterStart[MAXSTREAMS];
double  PCFreq = 0.0;
double  nextFrameDelay[MAXSTREAMS];

void startGlobalVideoTimer(int videounit)
{
/*
	DWORD core = GetCurrentProcessorNumber();
	printf("~~~Start Global timer - Thread Core Affinity: %d\n", core+1);

	//Make sure we are using a single core on a dual core machine, otherwise timings will be off.
	DWORD_PTR threadAffMask = SetThreadAffinityMask(GetCurrentThread(), 0x01);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");
*/
    LARGE_INTEGER li;
    if(!QueryPerformanceFrequency(&li))
        printf("QueryPerformanceFrequency failed!\n");

    PCFreq = double(li.QuadPart)/1000.0;

    QueryPerformanceCounter(&li);
    GlobalVideoCounter[videounit] = li.QuadPart;
/*
	threadAffMask = SetThreadAffinityMask(GetCurrentThread(),threadAffMask );
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");
*/
}
double getGlobalVideoTimer(int videounit)
{
/*
	DWORD core = GetCurrentProcessorNumber();
	printf("~~~Get Global timer - Thread Core Affinity: %d\n", core+1);

	//Make sure we are using a single core on a dual core machine, otherwise timings will be off.
	DWORD_PTR threadAffMask = SetThreadAffinityMask(GetCurrentThread(), 0x01);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");
*/

    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
	double globaltimer = double(li.QuadPart-GlobalVideoCounter[videounit])/PCFreq;
/*
	threadAffMask = SetThreadAffinityMask(GetCurrentThread(),threadAffMask );
	if(threadAffMask == 0)
	{
		printf("===Setting threadAffMask failed!!!\n");
		int e = GetLastError();
		printf("threadAffMask error: %d\n", e);
	}
*/
	return globaltimer;
}

typedef unsigned __int64     uint64_t;
uint64_t getGlobalVideoTimerMicro(int videounit)
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);

	LARGE_INTEGER qpfreq;
	QueryPerformanceFrequency(&qpfreq);

	//printf("Time diff: %f\n", (li.QuadPart-CounterStart)/PCFreq);
	return ((li.QuadPart-GlobalVideoCounter[videounit])*1000) / (qpfreq.QuadPart / 1000);
}

#ifdef MULTI_TIMER
HANDLE gDoneEvent[MAXSTREAMS];
VOID CALLBACK TimerRoutine(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
    if (lpParam == NULL)
    {
        printf("TimerRoutine lpParam is NULL\n");
    }
    else
    {
        // lpParam points to the argument; in this case it is an int
		int side = *(int*)lpParam;
        //printf("Timer routine called. Side is %d.\n", side);
        if(TimerOrWaitFired)
        {
            //printf("The wait timed out.\n");
        }
        else
        {
            printf("The wait event was signaled.\n");
        }

		SetEvent(gDoneEvent[side]);
    }
}

int videoCallback(int side, int delayms)
{
    HANDLE hTimer = NULL;
    HANDLE hTimerQueue = NULL;
    int arg = side;

    // Use an event object to track the TimerRoutine execution
    gDoneEvent[side] = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (NULL == gDoneEvent[side])
    {
        printf("CreateEvent failed (%d)\n", GetLastError());
        return 1;
    }

    // Create the timer queue.
    hTimerQueue = CreateTimerQueue();
    if (NULL == hTimerQueue)
    {
        printf("CreateTimerQueue failed (%d)\n", GetLastError());
        return 2;
    }

    // Set a timer to call the timer routine in 10 seconds.
    if (!CreateTimerQueueTimer( &hTimer, hTimerQueue,(WAITORTIMERCALLBACK)TimerRoutine, &arg , delayms, 0, 0))
    {
        printf("CreateTimerQueueTimer failed (%d)\n", GetLastError());
        return 3;
    }

    // TODO: Do other useful work here 

    //printf("Call timer routine in %d(ms)...\n", delayms);

    // Wait for the timer-queue thread to complete using an event 
    // object. The thread will signal the event at that time.

	if (WaitForSingleObject(gDoneEvent[side], INFINITE) != WAIT_OBJECT_0)
        printf("WaitForSingleObject failed (%d)\n", GetLastError());

    CloseHandle(gDoneEvent[side]);

    // Delete all timers in the timer queue.
    if (!DeleteTimerQueue(hTimerQueue) && GetLastError() != ERROR_IO_PENDING)
        printf("DeleteTimerQueue failed (%d)\n", GetLastError());

    return 0;
}
#endif
///////////////////////////////////////// INITIALISE VIDEO/AUDIO //////////////////////////////////////////////

void initVideoAudioPlayback(char *serverIP, int TCPPort, int UDPPort, int ClientID, int side, char* fileName, bool autostart, bool testLatency)
{
#ifdef NETWORKED_AUDIO
	_clientSenderID = ClientID;
	initNetSync(serverIP, TCPPort, UDPPort, ClientID, side, testLatency);
	printf("\n////////////////////////////////////////\n");

	#ifndef USE_ODBASE
		commandMutex = CreateMutex(NULL, false, NULL);
	#endif
#endif

	if(enablePBOs)
	{
	#ifdef USE_GLEW
		if (glewIsSupported("GL_ARB_pixel_buffer_object")) 
	#else
		if (true) 
	#endif
			printf("PBOs supported and enabled.\n");
		else
		{
			enablePBOs = false;
			printf("PBOs not supported.\n");
		}
	}
	else
		printf("PBOs turned off.\n");

	if(intelliSleep && !multiTimer)
		printf("Intelli sleep turned on.\n");
	else
		printf("Intelli sleep turned off.\n");

	if(multiTimer)
		printf("Multimedia timers enabled.\n");
	else
		printf("Multimedia timers disabled.\n");

	registerFFmpeg();
#ifdef LOCAL_AUDIO
	printf("\n//////////////// AUDIO ///////////////////\n");

	if(g_bufferSize == -1)
	{
		if(g_preloadAudio)
			g_bufferSize = 32256/4;
		else
			g_bufferSize = 2048-512;
	}
	printf("Audio buffer size: %d.\n", g_bufferSize);
	if(g_preloadAudio)
		printf("Preload audio buffers enabled.\n");
	else
		printf("Dynamic audio buffers enabled.\n");

	initAudioEnvir();
#endif
	printf("\n///////////////////////////////////////////\n");
	initvideo();

#ifdef LOG
		videoDebugLogFile = fopen("log.txt", "w");
		if(videoDebugLogFile == NULL)
			printf("Could not log file for writing");
#endif

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("InitVideo GL err.\n");
		printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
	}
	
#ifdef MULTI_TIMER
	if(multiTimer)
		for(int i=0; i<MAXSTREAMS; i++)
			_beginthread(updateVideoCallback, 0, /*NULL*/(void *)i);
#endif

	err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("InitVideo GL err.\n");
		printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
	}

	// Init global timer.
	for(int i=0; i<MAXSTREAMS; i++)
		startGlobalVideoTimer(i);

	if(autostart)
		if(loadVideo(fileName, side))
			playVideo(side);
}

void initvideo()
{
	glGenTextures(MAXSTREAMS, videotextures);
	for(int i = 0 ; i < MAXSTREAMS ; i++)
	{
		glBindTexture(GL_TEXTURE_2D, videotextures[i]);
		//glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		//glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_FALSE );

		// JEBNOTE: As documented, GL_LINEAR should not be passed with GL_TEXTURE_MIN_FILTER, it is expecting a numeric value
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
		// JEBNOTE: Constants (such as 1200 and 800 below) should be #defined in a header and described
		//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/,  1200, 800, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	}
}

////////////////////////////////////////////////// FFS ////////////////////////////////////////////////////////

#if FFS
bool loadffs(const char* path, int videounit, Movie* pMovie)
{
    // Try to load info from the video config file. Bail out if it's missing.
	if (loadframes(path, lastdisplayframe[videounit], fps[videounit], inwidth[videounit], inheight[videounit], blocks[videounit], compressed[videounit], videounit,
		compressionType[videounit]) == false) 
	//if (loadframes(path, lastdisplayframe[videounit], fps[videounit], inwidth[videounit], inheight[videounit], blocks[videounit], compressed[videounit], videounit) == false) 
	{
        status[videounit] = invalid;
		return false;
	}

	if( compressionType[videounit] == 1)
		compressionType[videounit] = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
	else if( compressionType[videounit] == 3)
		compressionType[videounit] = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
	else if( compressionType[videounit] == 5)
		compressionType[videounit] = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;

	char target[20];
	sprintf_s(target, "RENDERTARGET%d", videounit);
	//setTexture(pMovie,target,videotextures[videounit], inwidth[videounit], inheight[videounit]);

	videoTypeStreams[videounit] = ffs;

	QueryPerformanceFrequency( (LARGE_INTEGER *)&interFrameTimeDiff[videounit]);
	interFrameTimeDiff[videounit] /= fps[videounit];
	return true;
}

////////////////////////////////////////////// FFS PLAYBACK //////////////////////////////////////////////////
void playffs(int videounit)
{
    // If the video does not seem to exist, do not try to play it.
    if (status[videounit] == invalid)
		return;

	status[videounit] = aplay;
	QueryPerformanceCounter( (LARGE_INTEGER *)&startTime[videounit] );
}

void stopffs(int videounit)
{
	status[videounit] = astop;
	lastdisplayed[videounit] = -1;
	restartframes(videounit);
}

void updateffs()
{
	
	long long int currenttime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currenttime);

	ffsImage* ffsimage;

	for (int i = 0 ; i < MAXSTREAMS ; i++)
	{
		if(videoTypeStreams[i] == ffs)
			if (status[i] == aplay)
			{
				// Determine which frame should be shown, based on current time.
				currentdisplayframe[i] = ((int)((currenttime - startTime[i]) / interFrameTimeDiff[i]) % (lastdisplayframe[i] + 1));

	            // Check for wrap around, which means we are at video end.
	            // In that case, stop the video playback and notify Flash.
				if ((currentdisplayframe[i] == 0) && ((currenttime - startTime[i]) > interFrameTimeDiff[i])) {
					// JEBNOTE: Should never happen, but if i got large or small enough it could exceed 9 characters, argBuf should be at least 12.
	                char argBuf[10];
	                sprintf_s(argBuf, "%d", i);
					//pMovie->Invoke("notifyVideoEnded", argBuf);
	                //status[i] = astop;
					
	                continue;
				}
				
				// Check if it time to display a new frame.
				if (currentdisplayframe[i] != lastdisplayed[i])
				{
					lastdisplayed[i] = currentdisplayframe[i];
					ffsimage = getnextframe(currentdisplayframe[i],i);
					if( ffsimage != NULL)
					{
						glBindTexture(GL_TEXTURE_2D, videotextures[i]);
						//PFNGLCOMPRESSEDTEXIMAGE2DPROC p_glCompressedTexImage2D = (PFNGLCOMPRESSEDTEXIMAGE2DPROC) wglGetProcAddress("glCompressedTexImage2DARB");
						
						//PFNGLCOMPRESSEDTEXIMAGE2DARBPROC glCompressedTexImage2DARB = ( PFNGLCOMPRESSEDTEXIMAGE2DARBPROC ) wglGetProcAddress ( "glCompressedTexImage2DARB" );
						//if ( glCompressedTexImage2DARB == NULL )
						//	bool tgaCompressedTexSupport = false;
						
						//pRenderHAL->glCompressedTexImage2D(GL_TEXTURE_2D, 0, compressionType[i], ffsimage->width, ffsimage->height, 0, ffsimage->size, ffsimage->pixels[0]);
						
						donewithframe(currentdisplayframe[i],i);
					} else
					{
						underrunframe(currentdisplayframe[i],i);
						//if (logFile)
						//	logFile->write("under-run in video %d\n", i);
					}
				}
			}
	}
}


#endif

////////////////////////////////////////////// VIDEO PLAYBACK ////////////////////////////////////////////////

#ifdef VIDEO_PLAYBACK
bool loadVideo(const char* path, int videounit)
{
	if (loadVideoFrames(path, duration[videounit], fps[videounit], inwidth[videounit], inheight[videounit], videounit) == false) 
	{
        //status[videounit] = invalid;
		return false;
	}

	//char target[20];
	//sprintf_s(target, "RENDERTARGET%d", videounit);
	//setTexture(pMovie,target,videotextures[videounit], inwidth[videounit], inheight[videounit]);

	videoTypeStreams[videounit] = ffmpeg;
	//vidPath = (char*)path;
	strcpy(vidPath, path);

	if(rawRGBData[videounit] != NULL)
		free (rawRGBData[videounit]);
	rawRGBData[videounit] = (char*)malloc(sizeof(char) * inwidth[videounit] * inheight[videounit] * 4);
	//memset(rawRGBData[videounit], 0, sizeof(char) * inwidth[videounit] * inheight[videounit] * 4);
	//_beginthread(updateVideo, 0, 0  );

	if(enablePBOs)
		intiPBOs(inwidth[videounit], inheight[videounit], videounit);

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("LoadVideo GL err.\n");
	}

	return true;
}
/* Seek the specified video stream by the seekDuration (seconds).
 */
//void seekVideo(int side, double netclock, double seekDuration)
void seekVideo(int side, double seekDuration, double seekBaseTime, bool sendSeekTCPCommand)
{
	double clock = 0.0;
	if(seekBaseTime > 0)
		clock = seekBaseTime;
	else
	{
#ifdef LOCAL_AUDIO
	double openALAudioClock = getOpenALAudioClock(side);
	double ffmpegClock = getFFmpegClock(side);
	if(openALAudioClock >= 0 || ffmpegClock >= 0)
		clock = ffmpegClock;
#endif
#ifdef NETWORKED_AUDIO
	clock = currGlobalTimer[side] + diff[side];
	printf("currGlobalTimer[%d]: %f + diff[%d]: %f - netAudioClock[%d] %f = %f\n", side, currGlobalTimer[side], side, diff[side], side, netAudioClock[side], currGlobalTimer[side] + diff[side] - netAudioClock[side]);
	//clock = netAudioClock[side];
	//clock = currGlobalTimer[side];
#endif
	}
	printf("clock: %f - seekDuration: %f - seekBaseTime: %f \n", clock, seekDuration, seekBaseTime);

	seekVideoThread(side, clock, seekDuration);
	//seekVideoThread(side, seekDuration);

#ifdef NETWORKED_AUDIO
		if(sendSeekTCPCommand)
			seekVideoCommand(_clientSenderID, side, seekDuration);
#endif

	printf("\n~~~Video seek %f(s).\n", seekDuration);
}

/* Starts the specified video stream.
 */
void playVideo(int videounit, bool sendPlayTCPCommand)
{
	if(status[videounit] == astop)
	{
		startGlobalVideoTimer(videounit);

		currGlobalTimer[videounit] = 0.0;
	#ifdef NETWORKED_AUDIO
		if(sendPlayTCPCommand)
			playVideoCommand(_clientSenderID, videounit, vidPath);

		netAudioClock[videounit] = 0.0;
	#endif
		status[videounit] = aplay;
		preTime[videounit] = 0.0;
		diff[videounit] = 0.0;
		preClock[videounit] = 0.0;
		totalPauseLength[videounit] = 0.0;
#if 0
		//// This wont work here because playVideo is not called from the draw loop.
		if(!enablePBOs)
		{
			glBindTexture(GL_TEXTURE_2D, videotextures[videounit]);
			memset(rawRGBData[videounit], 0, sizeof(char) * inwidth[videounit] * inheight[videounit] * 4);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[videounit], inheight[videounit], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[videounit]);
			
			//char *zeroBuf = (char*)malloc(sizeof(char) * inwidth[videounit] * inheight[videounit] * 3);
			//memset(zeroBuf, 0, sizeof(char) * inwidth[videounit] * inheight[videounit] * 3);
			//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, inwidth[videounit], inheight[videounit], 0, GL_RGB, GL_UNSIGNED_BYTE, zeroBuf);
			//free(zeroBuf);
		}
#endif
	}
	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("PlayVideo GL err.\n");
	}
}

/* Stops the specified video stream.
 * If the video is currently playing then set the status array to astop, call the video shut procedure, set the screen to black
 * and finally send a stop command to the audio server.
 */
void stopVideo(int videounit, bool sendStopTCPCommand)
{
	if(status[videounit] == aplay || status[videounit] == apause)
	{
		printf("@@@Stopping video %d.\n", videounit);
		status[videounit] = astop;
		closeVideoThread(videounit);
#ifdef LOCAL_AUDIO
		closeAudioStream(videounit);
#endif
#if 0
		if(!enablePBOs)
		{
			glBindTexture(GL_TEXTURE_2D, videotextures[videounit]);
			memset(rawRGBData[videounit], 0, sizeof(char) * inwidth[videounit] * inheight[videounit] * 4);
			//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
			//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
			//glTexSubImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[videounit], inheight[videounit], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[videounit]);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[videounit], inheight[videounit], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[videounit]);

			//// This wont work here because playVideo is not called from the draw loop.
			//char *zeroBuf = (char*)malloc(sizeof(char) * inwidth[videounit] * inheight[videounit] * 4);
			//memset(zeroBuf, 0, sizeof(char) * inwidth[videounit] * inheight[videounit] * 4);
			//glBindTexture(GL_TEXTURE_2D, videotextures[videounit]);
			//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[videounit], inheight[videounit], 0, GL_RGBA, GL_UNSIGNED_BYTE, zeroBuf);
			//free(zeroBuf);
			//free (rawRGBData[videounit]);
		}
#endif
#ifdef NETWORKED_AUDIO
		if(sendStopTCPCommand)
			stopVideoCommand(_clientSenderID, videounit);
#endif
	}
	else
		printf("###Video %d already stopped.\n", videounit);
	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
		printf("StopVideo GL err.\n");
	}
}

/* Pause the selected video stream.
 * If the video is currently paused then calling this a second time will resume playback.
 */
void pauseVideo(int videounit, bool sendPauseTCPCommand)
{
	if(status[videounit] == aplay || status[videounit] == apause)
	{
		ispaused[videounit] = !ispaused[videounit];
		if(ispaused[videounit])
		{
			#ifdef NETWORKED_AUDIO
			if(sendPauseTCPCommand)
					pauseVideoCommand(_clientSenderID, videounit);
			#endif
			status[videounit] = apause;
			pauseStart[videounit] = getGlobalVideoTimer(videounit) / (double)1000;
		}
		else
		{
			#ifdef NETWORKED_AUDIO
			if(sendPauseTCPCommand)
					pauseVideoCommand(_clientSenderID, videounit);
			#endif
			double pauseLen = (getGlobalVideoTimer(videounit) / (double)1000) - pauseStart[videounit];
			totalPauseLength[videounit] += pauseLen;
			printf("Video paused for %f(s) - total delay: %f(s)\n", pauseLen, totalPauseLength[videounit]);
			status[videounit] = aplay;
		}
#ifdef LOCAL_AUDIO
	pauseAudioStream(videounit);
#endif
	}
}
/* Loads the specified video name
 * Stops the current video then calls playVideo but without the sending the TCP command back to the server.
 */

/* Change the volume of the current video stream.
 */
void changeVideoVolume(int videounit, float vol)
{
	printf("Change volume: %f\n", vol);

#ifdef NETWORKED_AUDIO
	changeVolumeCommand(_clientSenderID, videounit, vol);
#elif LOCAL_AUDIO
	setALVolume(videounit, vol);
#endif

}

#ifdef LOCAL_AUDIO
/* Return the current OpenAL clock.
 */
double getOpenALAudioClock(int side)
{
	return getALAudioClock(side);
}
/* Return the current ffmpeg clock.
 */
double getFFmpegClock(int side)
{
	return getFFmpeg(side);
}
#endif
#endif
///////////////////////////////////////////// Command Queue  //////////////////////////////////////////////////
#if 0
bool b_screenSyncLoadVideo = false;
bool b_stopOrRestartVideo = false;

string netVideoName = "";
int netVideoUnit = -1;
void notifyScreenSyncLoadVideo(string videoName, int videounit)
{
	netVideoName = videoName;
	netVideoUnit = videounit;
	b_screenSyncLoadVideo = true;
}

void notifyStopOrRestartVideo(int videounit)
{
	netVideoUnit = videounit;
	b_stopOrRestartVideo = true;
}

void screenSyncLoadVideo(string videoName, int videounit)
{
	if(screenSync)
	{
		printf("---Loading video stream...: %d\n", videounit);
		stopVideo(videounit);
		while(!loadVideo(videoName.c_str()/*vidPath*/, videounit))
		{
			Sleep(100);
		}
		//playVideo(videounit);
		playVideo(videounit, false);
	}
	else
		printf("Screen Sync not enabled!!!\n");
}
/* Either loops or stops the current video stream.
 */
void stopOrRestartVideo(int videounit)
{
	if(loopvideo)
	{
		printf("---Looping video stream...: %d\n", videounit);
		stopVideo(videounit);
		while(!loadVideo(vidPath, videounit))
		{
			Sleep(100);
		}
		playVideo(videounit);
		//else
		//printf("Could not reload video stream: %d\n", videounit);
	}
	else
		stopVideo(videounit);
}

void checkMessages()
{
	if(b_screenSyncLoadVideo)
	{
		screenSyncLoadVideo(netVideoName, netVideoUnit);
		b_screenSyncLoadVideo = false;
	}
	if(b_stopOrRestartVideo)
	{
		b_stopOrRestartVideo = false;
		stopOrRestartVideo(netVideoUnit);
	}
}
#endif

#ifdef NETWORKED_AUDIO
enum {nstop, nload, npause, nseek};

typedef struct netMes {
	int type;
	int stream;
	//string videoName;
	char videoName[256];
	double seekDuration;
} networkMsg;

#if 0
std::vector<networkMsg> netCommands;
#endif

#include <queue>
queue<networkMsg> netQueue;

void notifyScreenSyncLoadVideo(string videoName, int videounit)
{
	networkMsg newMsg;
	newMsg.type = nload;
	//newMsg.videoName = videoName;
	sprintf(newMsg.videoName, "%s", videoName.c_str());
	newMsg.stream = videounit;

	//printf("++++++NET commands - type %d : %s\n", newMsg.type, newMsg.videoName.c_str());
	
#ifdef USE_ODBASE
	commandMutex.grab();
#else
	WaitForSingleObject(commandMutex, INFINITE);
#endif

	//netCommands.push_back(newMsg);
	netQueue.push(newMsg);
	
#ifdef USE_ODBASE
	commandMutex.release();
#else
	ReleaseMutex(commandMutex);
#endif
}

void notifyScreenSeekVideo(int videounit, double seekDur)
{
	networkMsg newMsg;
	newMsg.type = nseek;
	//newMsg.videoName = videoName;
	sprintf(newMsg.videoName, "%s", "");
	newMsg.stream = videounit;
	newMsg.seekDuration = seekDur;

	//printf("++++++NET commands - type %d : %s\n", newMsg.type, newMsg.videoName.c_str());
#ifdef USE_ODBASE
	commandMutex.grab();
#else
	WaitForSingleObject(commandMutex, INFINITE);
#endif
	//netCommands.push_back(newMsg);
	netQueue.push(newMsg);
#ifdef USE_ODBASE
	commandMutex.release();
#else
	ReleaseMutex(commandMutex);
#endif
}

void notifyStopOrRestartVideo(int videounit)
{
	networkMsg newMsg;
	newMsg.type = nstop;
	//newMsg.videoName = "";
	sprintf(newMsg.videoName, "%s", "");
	newMsg.stream = videounit;

	//printf("commandMutex.grab()\n");
#ifdef USE_ODBASE
	commandMutex.grab();
#else
	WaitForSingleObject(commandMutex, INFINITE);
#endif
	//netCommands.push_back(newMsg);
	netQueue.push(newMsg);
#ifdef USE_ODBASE
	commandMutex.release();
#else
	ReleaseMutex(commandMutex);
#endif
	//printf("commandMutex.release()\n");
}

void checkMessages()
{
	//printf("___Checking commands...\n");
	while (!netQueue.empty())
	{
#ifdef USE_ODBASE
		commandMutex.grab();
#else
		WaitForSingleObject(commandMutex, INFINITE);
#endif
		networkMsg qMsg = netQueue.front();
#ifdef USE_ODBASE
		commandMutex.release();
#else
		ReleaseMutex(commandMutex);
#endif
		printf("+++Queue commands - size %d - type %d : %s\n", netQueue.size(), qMsg.type, qMsg.videoName);
		if(qMsg.type == nload/* && qMsg.videoName != ""*/)
		{
			screenSyncLoadVideo(qMsg.videoName, qMsg.stream);
			netQueue.pop();
		}
		if(qMsg.type == nstop)
		{
			stopOrRestartVideo(qMsg.stream);
			netQueue.pop();
		}
		if(qMsg.type == nseek)
		{
			seekVideo(qMsg.stream, qMsg.seekDuration, false);
			netQueue.pop();
		}
	}
}

#if 0
void checkMessages()
{
#ifdef USE_ODBASE
	commandMutex.grab();
#else
	WaitForSingleObject(commandMutex, INFINITE);
#endif
	//while( !netCommands.empty() )
	//for(int i=netCommands.size()-1; i>=0; i--)
	for(int i=0; i<netCommands.size(); i++)
	{
		networkMsg qMsg;
		qMsg = netCommands.at(0);
		//qMsg = netCommands.at(i);
		
		printf("++++++Queue commands - size %d - type %d : %s\n", netCommands.size(), qMsg.type, qMsg.videoName);
		if(qMsg.type == nload/* && qMsg.videoName != ""*/)
		{
			printf("+++++++Load commands - type %d : %s\n", qMsg.type, qMsg.videoName);
			screenSyncLoadVideo(qMsg.videoName, qMsg.stream);
			//netCommands.pop_back();
		}
		if(qMsg.type == nstop)
		{
			stopOrRestartVideo(qMsg.stream);
			//netCommands.pop_back();
		}
		netCommands.pop_back();
	}
#ifdef USE_ODBASE
	commandMutex.release();
#else
	ReleaseMutex(commandMutex);
#endif
}
#endif
#endif

/* Stops the current video, loads specified video and begins playback.
 */
void screenSyncLoadVideo(string videoName, int videounit)
{
	if(screenSync)
	{
		printf("--Loading video stream...: %d\n", videounit);
		stopVideo(videounit, true);
		//Sleep(100);
		//loadVideo(videoName.c_str()/*vidPath*/, videounit);
		while(!loadVideo(videoName.c_str()/*vidPath*/, videounit))
		{
			Sleep(100);
		}
#ifdef LOCAL_AUDIO
		setWindowTitle(-1, videoName);
#else
		setWindowTitle(_clientSenderID, videoName);
#endif
		//playVideo(videounit);
		playVideo(videounit, false);
	}
	else
		printf("Screen Sync not enabled!!!\n");
}

/* Either loops or stops the current video stream.
 */
void stopOrRestartVideo(int videounit)
{
	if(loopvideo)
	{
		printf("--Looping video stream...: %d\n", videounit);
		stopVideo(videounit);
		//loadVideo(vidPath, videounit);
		while(!loadVideo(vidPath, videounit))
		{
			Sleep(100);
		}
		playVideo(videounit, false);
		//else
		//printf("Could not reload video stream: %d\n", videounit);
	}
	else
	{
		printf("--Stopping video stream...: %d\n", videounit);
		stopVideo(videounit, true);
	}
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////// VIDEO PLAYBACK /////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* Video display function.
 * Starts the callback timer for the correct stream, sends a UDP message to the audio server requesting the specificed audio clock and blocks (for 1 second)
 * awaiting a response. If it recieves a postive double value then getNextVideoFrame is called passing the received audio clock value, otherwise the video 
 * playback is halted. Finally the next frame is copied to plainData buffer, uploaded to the OpenGL texture the video timer is set for the next callback.
 */
#define MAX_PBOS 5
extern double pboPTS[MAX_PBOS];
extern int	qindex[MAXSTREAMS];
//void updateVideo(void* dummy)
void updateVideo()
{
#ifdef NETWORKED_AUDIO
	checkMessages();
#endif

#if 0
	//double totTime = getGlobalVideoTimer(0);
	int totalActiveVideos = 0;
	for (int i = 0 ; i < MAXSTREAMS; i++)
	{
		//printf("\tStatus: %d\n", status[i]);
		if(videoTypeStreams[i] == ffmpeg)
			if (status[i] == aplay)
			{
				totalActiveVideos++;
#if 1
				currGlobalTimer[i] = getGlobalVideoTimer(i) / (double)1000;
				currGlobalTimer[i] -= totalPauseLength[i];

				double udpAudioClock = netAudioClock[i];
				g_netAudioClock[i] = udpAudioClock;
				//if(preClock[i] == udpAudioClock)
				//{
				//	//printf("Gen new clock - preClock[i]: %09.6f - udpAudioClock: %09.6f\n", preClock[i], udpAudioClock);
				//	udpAudioClock = -1;
				//}
				//else if(udpAudioClock >= 0)// if(preClock[i] < udpAudioClock)
				//if(udpAudioClock >= 0)
				{
					//preClock[i] = udpAudioClock;
					//diff[i] = (udpAudioClock-currGlobalTimer[i]);// + g_Delay[i];
				}

				currGlobalTimer[i] = netAudioClock[i];
#endif
			}
	}
	//glFlush();
	//glFinish();
	//Sleep(30);
#endif

#if 1
	//double totTime = getGlobalVideoTimer(0);
	int totalActiveVideos = 0;
	for (int i = 0 ; i < MAXSTREAMS; i++)
	{
		//printf("\tStatus: %d\n", status[i]);
		if(videoTypeStreams[i] == ffmpeg)
			if (status[i] == aplay)
			{
				totalActiveVideos++;
				if(enablePBOs)
				{
					//double profileTime = getGlobalVideoTimer(i);
					updatePBOs(i);
					//double durTime = getGlobalVideoTimer(i) - profileTime;
					//printf("UPLOADTime:\t %f\n", durTime);
				}
#if 0
#ifdef NETWORKED_AUDIO

				currGlobalTimer[i] = getGlobalVideoTimer(i) / (double)1000;
				currGlobalTimer[i] -= totalPauseLength[i];

				double udpAudioClock = netAudioClock[i];
				g_netAudioClock[i] = udpAudioClock;
				if(preClock[i] == udpAudioClock)
				{
					//printf("Gen new clock - preClock[i]: %09.6f - udpAudioClock: %09.6f\n", preClock[i], udpAudioClock);
					udpAudioClock = -1;
				}
				else if(udpAudioClock >= 0)// if(preClock[i] < udpAudioClock)
				//if(udpAudioClock >= 0)
				{
					preClock[i] = udpAudioClock;
					diff[i] = (udpAudioClock-currGlobalTimer[i]);// + g_Delay[i];
				}
#endif
#endif
				int notfullPbos = pbosFull(i);
				double timeDif = getGlobalVideoTimer(i) - preTime[i];
				if(timeDif >= nextFrameDelay[i])// && !notfullPbos)
				{
					//Sleep(30);
					//printf("Here\n");
					//double frameTimeDiff1 = timeDif-nextFrameDelay[i];
					//printf("-frameDelayDiff: %09.6f(ms) - preTime[i]: %09.6f - gTimer: %09.6f(ms)\n", frameTimeDiff1, preTime[i], getGlobalVideoTimer(i));
					preTime[i] = getGlobalVideoTimer(i);
#if 1
					currGlobalTimer[i] = getGlobalVideoTimer(i) / (double)1000;
					currGlobalTimer[i] -= totalPauseLength[i];
					//updatePBOs(i);

#ifdef NETWORKED_AUDIO
					double udpAudioClock = netAudioClock[i];
					g_netAudioClock[i] = udpAudioClock;
					if(preClock[i] == udpAudioClock)
					{
						//printf("Gen new clock - preClock[i]: %09.6f - udpAudioClock: %09.6f\n", preClock[i], udpAudioClock);
						udpAudioClock = -1;
					}
					else if(udpAudioClock >= 0)// if(preClock[i] < udpAudioClock)
					//if(udpAudioClock >= 0)
					{
						preClock[i] = udpAudioClock;
						diff[i] = (udpAudioClock-currGlobalTimer[i]);// + g_Delay[i];
					}
#elif LOCAL_AUDIO
					double openALAudioClock = getOpenALAudioClock(i);
					double ffmpegClock = getFFmpegClock(i);
					g_netAudioClock[i] = openALAudioClock;

					static double lClockDiff[MAXSTREAMS] = {0};
					double lcDiff = openALAudioClock - ffmpegClock;
					if(fabs(lcDiff) < 1.0)
						lClockDiff[i] = lcDiff;
					else
					{
						//openALAudioClock = ffmpegClock + lClockDiff[i];
						openALAudioClock = openALAudioClock - (lcDiff) + lClockDiff[i];
					}
					//double openALAudioClock = (getOpenALAudioClock(i) > 0) ? getOpenALAudioClock(i) : currGlobalTimer[i];
					//printf("-lClockDiff[%d]: %f(s) - openALAudioClock[%d]: %f(s) - ffmpegClock[%d]: %f(s) - Diff[%d]: %f(s)\n", i, lClockDiff[i], i, openALAudioClock, i, ffmpegClock, i, lcDiff);
					if(enableDebugOutput)
						fprintf(stdout, "\rOAL_AUDIO_CLOCK[%d]: %09.4f(s)-ffmpegClock[%d]: %09.4f(s)", i, openALAudioClock, i, ffmpegClock);

					openALAudioClock = ffmpegClock;
					if(preClock[i] == openALAudioClock)
					{
						openALAudioClock = -1;
					}
					else if(openALAudioClock >= 0)
					//if(openALAudioClock >= 0)
					{
						preClock[i] = openALAudioClock;
						diff[i] = (openALAudioClock-currGlobalTimer[i]);
					}
#endif
#endif
					double newClock = currGlobalTimer[i] + diff[i];
					newClock = newClock + g_Delay[i];

					//diff[i] = diff[i] + g_Delay[i];
					//if(abs(diff[i]) > 0.01) //use the netAudioClock if the difference between the local clock and it is greater than 0.01s
					//	newClock = netAudioClock;
					//if(abs(diff[i]) > 2.0)
					//{
					//	printf("Seeking video stream: %d\n", i);
					//	seekVideo(i, newClock, diff[i]);
					//}
#ifdef LOG
					#ifdef NETWORKED_AUDIO
						fprintf(videoDebugLogFile, "\rNET_AUDIO_CLOCK[%d]: %019.16f(s)-GlobalVideoTimer[%d]: %f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %019.16f(s)", i, udpAudioClock, i, 
							currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
					#endif
					#ifdef LOCAL_AUDIO
						fprintf(videoDebugLogFile, "\rOAL_AUDIO_CLOCK[%d]: %019.16f(s)-GlobalVideoTimer[%d]: %f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %019.16f(s)", i, openALAudioClock, i, 
							currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
					#endif
#endif
#ifdef NETWORKED_AUDIO
					if(enableDebugOutput)
						//#ifdef DEBUG_PRINTF
							fprintf(stdout, "\rNET_AUDIO_CLOCK[%d]: %019.16f(s)-GlobalVideoTimer[%d]: %f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %019.16f(s)", i, udpAudioClock, i, 
								currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
						//#endif
#endif
#ifdef LOCAL_AUDIO
					if(enableDebugOutput)
						//#ifdef DEBUG_PRINTF
							fprintf(stdout, "-GlobalVideoTimer[%d]: %09.6f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %09.6f(s)", i, 
								currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
#endif

					double frameTimeDiff = timeDif-nextFrameDelay[i];
					if(enableDebugOutput)
						printf("-frameDelayDiff: %09.6f(ms)", frameTimeDiff); //-Current frame diff in ms - //(frameTimeDiff / (double)1000)
					//printf("\n");
					#ifdef LOG
					//#ifdef DEBUG_PRINTF
						fprintf(videoDebugLogFile, "-frameTimeDif: %09.6f", frameTimeDiff);
						//printf("-frameTimeDif: %09.6f", frameTimeDiff);
					//#endif
					#endif
					//memset(rawRGBData[i], 128, sizeof(char) * inwidth[i] * inheight[i] * 4);
					pts[i] = getVideoPts(i);
					newClock += (frameTimeDiff / (double)1000);
					//newClock = pts[i];//preClock[i];//getGlobalVideoTimer(i) / (double)1000;//pts[i]/* - (1 / fps[i])*/ + g_Delay[i];
					//newClock = netAudioClock[i];

					//uint64_t globalTimer = hton64(getGlobalVideoTimerMicro(i));// / 1000000.0;
					//currGlobalTimer[i] = getGlobalVideoTimerMicro(i);// / 1000000.0;
//					currGlobalTimer[i] = netAudioClock[i];
					//printf(">>> globalTimer %016I64x : %I64u \n", globalTimer, ntoh64(globalTimer));
					//printf("-currGlobalTimer[%d]: %f(ms)\n", i, currGlobalTimer[i]);
//					newClock = currGlobalTimer[i];

					//printf("-newClock: %09.6f(s)\n", netAudioClock[i]);
					//double profileCopyTime = getGlobalVideoTimer(i);
					//printf("-pts[%d]: %09.6f(s)\n", i, pts[i]);
					if(enablePBOs)
						//nextFrameDelay[i] = getNextVideoFramePbo(newClock, totalPauseLength[i], i, rawRGBData[i]);
						//nextFrameDelay[i] = getNextVideoFramePbo(newClock, totalPauseLength[i], i, rawRGBData[i], pboPTS[i]);
						nextFrameDelay[i] = getNextVideoFramePbo(newClock, totalPauseLength[i], i, rawRGBData[i], pbosSize(i) > -1.0 ? pboPTS[qindex[i]] : 0);
					else
						nextFrameDelay[i] = getNextVideoFrame(newClock, totalPauseLength[i], i, rawRGBData[i]);
					//printf("CpyTime:\t %f\n", getGlobalVideoTimer(i) - profileCopyTime);	

					//Sleep(5);

					//sendPTSTime(i, getVideoPts(i));


					//printf("qindex[%d]: %d - pbopts: %f\t", i, qindex[i], pboPTS[qindex[i]]);
					//if(nextFrameDelay[i] < 0 && !pbosEmpty(i))
					//	nextFrameDelay[i] = 30.1;

#ifdef LOG
					fprintf(videoDebugLogFile, "-NEXT_FRAME_DELAY[%d]: %f(ms)\n", i, nextFrameDelay[i]);
#endif
					if(enableDebugOutput)
						fprintf(stdout, "-NEXT_FRAME_DELAY[%d]: %f(ms)\n", i, nextFrameDelay[i]);
		
					double profileTime = getGlobalVideoTimer(i);
					glBindTexture(GL_TEXTURE_2D, videotextures[i]);
					if(nextFrameDelay[i] > 0 || !pbosEmpty(i))//10.0 && nextFrameDelay[i] < 40.0)
					//if(1)
					{
						//printf("***Display Video frame[%d] - timeDif: %f\n", i, timeDif);
#if 1
						//printf("\tDisplay Video frame: %d\n", i);
						//memset(rawRGBData[i], 128, sizeof(char) * inwidth[i] * inheight[i] * 4);
						if(enablePBOs)
							drawPBO(i);
							//drawPBO(i, -1);
						else
						{
							if( rawRGBData[i] != NULL)
							{
								static int lwidth[MAXSTREAMS] = {0};
								static int lheight[MAXSTREAMS] = {0};

								if(lwidth[i] != inwidth[i] || lheight[i] != inheight[i] )
								{
									glTexImage2D(GL_TEXTURE_2D, 0, /*GL_BGRA*/ GL_RGBA  /*GL_RGB*/, inwidth[i], inheight[i], 0, GL_BGRA /*GL_RGBA*/, GL_UNSIGNED_BYTE, 0);
								}
								else
									glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_BGRA /*GL_RGBA*/  /*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

								lwidth[i] = inwidth[i];
								lheight[i] = inheight[i];
							}
						}
#endif
						//nextFrameDelay[i] = 0;
						//Sleep(nextFrameDelay[i]);
						//Sleep(10);
					}
					else if (nextFrameDelay[i] == -1)
					{
						nextFrameDelay[i] = 0.0;
						printf("<<<Stop drawing video[%d]: pictq_size = 0\n", i);
						//Sleep(10);

						//memset(rawRGBData[i], 0, sizeof(char) * inwidth[i] * inheight[i] * 4);
						//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
						//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
					}
					else if (nextFrameDelay[i] == -100)
					{
						nextFrameDelay[i] = 100;
						printf("<<<Stop drawing video[%d]: codecctx_ not init\n", i);
					}
					else if (nextFrameDelay[i] == -5)
					{
						printf("<<<Stop drawing video[%d]: video not initialized\n", i);
						//status[i] = astop;

						//memset(rawRGBData[i], 0, sizeof(char) * inwidth[i] * inheight[i] * 4);
						//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
						//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

						//stopOrRestartVideo(i);
					}
					else if (nextFrameDelay[i] == -10)
					{
						//nextFrameDelay[i] = 10;
						//nextFrameDelay[i] = 1.0;
						nextFrameDelay[i] = 0.0;
						//printf("***Dropping frame[%d].\n", i);

						//memset(rawRGBData[i], 128, sizeof(char) * inwidth[i] * inheight[i] * 4);
						//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
					}
					double durTime = getGlobalVideoTimer(i) - profileTime;
					//printf("DISPLYTime:\t %f\n", durTime);

					//preTime[i] = getGlobalVideoTimer(i);
					//Sleep(10);
				}
				
				else
				{
#ifdef LOG
					fflush(videoDebugLogFile);
					//fclose(videoDebugLogFile);

					//int numflushed = _flushall();
					//printf( "There were %d streams flushed\n", numflushed );
#endif

					//fprintf(stdout, "...Underrun -> diff: %f ret: %f\n", timeDif, nextFrameDelay[i]);
					//printf("...Underrun -> diff: %f ret: %f\n", timeDif, nextFrameDelay[i]);
					//Sleep(5);
					//if(enablePBOs)
					//{
					//	//double profileTime = getGlobalVideoTimer(i);
					//	updatePBOs(i);
					//	//double durTime = getGlobalVideoTimer(i) - profileTime;
					//	//printf("UPLOADTime:\t %f\n", durTime);
					//}
				}
				
#if 0
				double now = getGlobalVideoTimer(i);
				double remainingFrameDelay = fabs(double(now - preTime[i] - nextFrameDelay[i]));
				//double now = getTimer(i);
				//double remainingFrameDelay = fabs(double(now - nextFrameDelay[i]));
				//printf("remainingFrameDelay: %f\n", remainingFrameDelay);
				double sleepTime = (remainingFrameDelay > 10.0) ? remainingFrameDelay / 2 : 0;
				Sleep(sleepTime);
				//printf("sleepTime: %f\n", sleepTime);
				//continue;
#endif
			}
			else if (status[i] == apause)
			{
				//totalActiveVideos++;
			}
			else if (status[i] == astop)
			{
				//memset(rawRGBData[i], 120, sizeof(char) * inwidth[i] * inheight[i] * 4);
				//glTexSubImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
			}
			//DWORD core = GetCurrentProcessorNumber();
			//printf("^^^^Main draw - Process Affinity: %d\n", core);
	}
	if(totalActiveVideos == 0)
		Sleep(100);
		//Sleep(0);
	else
	{
		//if(0)
		if(intelliSleep)
		{
			int minFrameDelayIndex = -1;
			double minFrameDelay = INT_MAX;
			for(int i=0; i<MAXSTREAMS; i++)
				if( status[i] == aplay && nextFrameDelay[i] < minFrameDelay && nextFrameDelay[i] > 0.0)
				{
					minFrameDelay = nextFrameDelay[i];
					minFrameDelayIndex = i;
				}

			if(minFrameDelayIndex != -1 && minFrameDelay != INT_MAX)
			{
				double now = getGlobalVideoTimer(minFrameDelayIndex);
				double remainingFrameDelay = fabs(double(now - preTime[minFrameDelayIndex] - nextFrameDelay[minFrameDelayIndex]));
				//double sleepTime = (remainingFrameDelay > 10.5) ? remainingFrameDelay / 2 : 0;
				//double sleepTime = (remainingFrameDelay > 5.5) ? remainingFrameDelay : 0;
				double sleepTime;
				if(remainingFrameDelay > 5.5)
					sleepTime = remainingFrameDelay;
				else
				{
					//printf("///here");
					sleepTime = 0;
				}
				printf("///now: %f - preTime[%d]: %f - nextFrameDelay[%d]: %f - remainingFrameDelay: %f - sleepTime: %f\n", now / 1000.0f, minFrameDelayIndex, preTime[minFrameDelayIndex] / 1000.0f, minFrameDelayIndex, nextFrameDelay[minFrameDelayIndex], remainingFrameDelay, sleepTime);
				Sleep((DWORD)sleepTime);
			}
		}
		//Sleep(5);
	}
	//glFlush();
	//glFinish();

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("UpdateVideo GL err - %s.\n", gluErrorString(err));
		printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
	}

#endif
	//printf("totTime:\t %f\n", getGlobalVideoTimer(0) - totTime);
}

#ifdef MULTI_TIMER
//int drawQCount = 0;

#if 0
#ifdef USE_ODBASE
	ODBase::Lock drawMutex("drawMutex");
#else
	HANDLE drawMutex;
#endif
#endif

void SupdateVideoCallback(void* arg)
//void updateVideoCallback()
{
#ifdef MULTI_TIMER
	int i = (int)arg;

	printf("UpdateVideo thread started: %d.\n", i);

	//GLenum err = glGetError();
	//if(err != GL_NO_ERROR)
	//{
	//	printf("UpdateVideo GL err.\n");
	//	printf("OpenAL Error: %s (0x%x), @\n", glGetString(err), err);
	//}
#endif
	//checkMessages();

	double last_callback_time = 0.0;

	//double totTime = getGlobalVideoTimer(0);
	while(true)
	{
		int totalActiveVideos = 0;
		//for (int i = 0 ; i < MAXSTREAMS; i++)
		{
			//printf("\tStatus: %d\n", status[i]);
			if(videoTypeStreams[i] == ffmpeg)
				if (status[i] == aplay)
				{
					totalActiveVideos++;
	#if 0
					if(enablePBOs)
						updatePBOs(i);
	#endif

					//double timeDif = getGlobalVideoTimer(i) - preTime[i];
					//if(timeDif >= nextFrameDelay[i])
					{
#if 0
						currGlobalTimer[i] = getGlobalVideoTimer(i) / (double)1000;
						currGlobalTimer[i] -= totalPauseLength[i];
						//updatePBOs(i);
	#ifdef NETWORKED_AUDIO
						double udpAudioClock = netAudioClock[i];
						g_netAudioClock[i] = udpAudioClock;
						if(preClock[i] == udpAudioClock)
						{
							udpAudioClock = -1;
						}
						else if(udpAudioClock >= 0)// if(preClock[i] < udpAudioClock)
						//if(udpAudioClock >= 0)
						{
							preClock[i] = udpAudioClock;
							diff[i] = (udpAudioClock-currGlobalTimer[i]);// + g_Delay[i];
						}
	#elif LOCAL_AUDIO
						double openALAudioClock = getOpenALAudioClock(i);
						double ffmpegClock = getFFmpegClock(i);
						g_netAudioClock[i] = openALAudioClock;

						static double lClockDiff[MAXSTREAMS] = {0};
						double lcDiff = openALAudioClock - ffmpegClock;
						if(fabs(lcDiff) < 1.0)
							lClockDiff[i] = lcDiff;
						else
						{
							//openALAudioClock = ffmpegClock + lClockDiff[i];
							openALAudioClock = openALAudioClock - (lcDiff) + lClockDiff[i];
						}
						//double openALAudioClock = (getOpenALAudioClock(i) > 0) ? getOpenALAudioClock(i) : currGlobalTimer[i];
						//printf("-lClockDiff[%d]: %f(s) - openALAudioClock[%d]: %f(s) - ffmpegClock[%d]: %f(s) - Diff[%d]: %f(s)\n", i, lClockDiff[i], i, openALAudioClock, i, ffmpegClock, i, lcDiff);
						openALAudioClock = ffmpegClock;
						if(enableDebugOutput)
							fprintf(stdout, "\rOAL_AUDIO_CLOCK[%d]: %09.4f(s)-ffmpegClock[%d]: %09.4f(s)", i, openALAudioClock, i, ffmpegClock);
						if(preClock[i] == openALAudioClock)
						{
							openALAudioClock = -1;
						}
						else if(openALAudioClock >= 0)
						//if(openALAudioClock >= 0)
						{
							preClock[i] = openALAudioClock;
							diff[i] = (openALAudioClock-currGlobalTimer[i]);
						}
	#endif			
#endif
						double newClock = currGlobalTimer[i] + diff[i];
						newClock = newClock + g_Delay[i];

						//diff[i] = diff[i] + g_Delay[i];
						//if(abs(diff[i]) > 0.01) //use the netAudioClock if the difference between the local clock and it is greater than 0.01s
						//	newClock = netAudioClock;
						//if(abs(diff[i]) > 2.0)
						//{
						//	printf("Seeking video stream: %d\n", i);
						//	seekVideo(i, newClock, diff[i]);
						//}
	//#ifdef LOG
	//					//if(netAudioClock > 0)
	//					//	fprintf(videoDebugLogFile, "\rNET_AUDIO_CLOCK[%d]: %f\t\tGlobalVideoTimer[%d] = %f\t\tDiff: %f", i, netAudioClock, i, currGlobalTimer[i], currGlobalTimer[i]);					
	//					//else
	//					//	fprintf(videoDebugLogFile, "\r_GEN_AUDIO_CLOCK[%d]: %f\t\tGlobalVideoTimer[%d] = %f", i, newClock, i, currGlobalTimer[i]);

	//	#ifdef NETWORKED_AUDIO
	//						fprintf(videoDebugLogFile, "\rNET_AUDIO_CLOCK[%d]: %019.16f(s)-GlobalVideoTimer[%d]: %f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %019.16f(s)", i, udpAudioClock, i, 
	//							currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
	//	#endif
	//	#ifdef LOCAL_AUDIO
	//						fprintf(videoDebugLogFile, "\rNET_AUDIO_CLOCK[%d]: %019.16f(s)-GlobalVideoTimer[%d]: %f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %019.16f(s)", i, openALAudioClock, i, 
	//							currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
	//	#endif
	//#endif
	
#ifdef NETWORKED_AUDIO
						if(enableDebugOutput)
							//#ifdef DEBUG_PRINTF
								fprintf(stdout, "\rNET_AUDIO_CLOCK[%d]: %019.16f(s)-GlobalVideoTimer[%d]: %f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %019.16f(s)", i, g_netAudioClock[i], i, 
									currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
							//#endif
	#endif
	#ifdef LOCAL_AUDIO
						if(enableDebugOutput)
							//#ifdef DEBUG_PRINTF
								fprintf(stdout, "-GlobalVideoTimer[%d]: %09.6f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %09.6f(s)", i, 
									currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
	#endif
						double frameTimeDiff = newClock - last_callback_time;
						//printf("-frameDelayDiff: %09.6f(ms)\n", frameTimeDiff * 1000); //-Current frame diff in ms
						last_callback_time = newClock;

						//double frameTimeDiff = timeDif-nextFrameDelay[i];
						//if(enableDebugOutput)
							//printf("-frameDelayDiff: %09.6f(ms)", frameTimeDiff); //-Current frame diff in ms - //(frameTimeDiff / (double)1000)
						//#ifdef DEBUG_PRINTF
						//		printf("-frameTimeDif: %09.6f", frameTimeDiff);
						//#endif
						//memset(rawRGBData[i], 128, sizeof(char) * inwidth[i] * inheight[i] * 4);
						
						//pts[i] = getVideoPts(i);

	//#ifdef NETWORKED_AUDIO
	//					newClock = netAudioClock[i];
	//#endif
						//newClock += (frameTimeDiff / (double)1000);
						//double profileCopyTime = getGlobalVideoTimer(i);

#if 0
#ifdef USE_ODBASE
		drawMutex.grab();
#else
		WaitForSingleObject(drawMutex, INFINITE);
#endif
							int size = drawQCount;
#ifdef USE_ODBASE
		drawMutex.release();
#else
		ReleaseMutex(drawMutex);
#endif
							if(size == 5)
							{
								//Sleep(10);
								nextFrameDelay[i] = 10.0;
								videoCallback(i, (int)nextFrameDelay[i]);
								continue;
							}
#endif


						if(enablePBOs)
						{
							//nextFrameDelay[i] = getNextVideoFramePbo(newClock, totalPauseLength[i], i, rawRGBData[i]);

							//int pboPts = pbosSize(i) > 0 ? pboPTS[qindex[i]] : -1;
							int pboPts = pboPTS[qindex[i]];
							nextFrameDelay[i] = getNextVideoFramePbo(newClock, totalPauseLength[i], i, rawRGBData[i], pboPts);
						}
						else
							nextFrameDelay[i] = getNextVideoFrame(newClock, totalPauseLength[i], i, rawRGBData[i]);

						//printf("pbosSize[%d]: %d - drawQCount: %d - pts[%d]: %f, qindex[%d]: %d - pbopts: %f\n", i, pbosSize(i), drawQCount, i, pts[i], i, qindex[i], pboPTS[qindex[i]]);

						//printf("CpyTime:\t %f\n", getGlobalVideoTimer(i) - profileCopyTime);	
	#ifdef LOG
						fprintf(videoDebugLogFile, "-NEXT_FRAME_DELAY[%d]: %f\n", i, nextFrameDelay[i]);
	#endif
						if(enableDebugOutput)
							fprintf(stdout, "-NEXT_FRAME_DELAY[%d]: %f(ms)\n", i, nextFrameDelay[i]);
			
						double profileTime = getGlobalVideoTimer(i);
						//glBindTexture(GL_TEXTURE_2D, videotextures[i]);
						if(nextFrameDelay[i] > 0)// && /*!pbosEmpty(i)*/ size < pbosSize(i))
						{
							//printf("\tDisplay Video frame: %d\n", i);
							//memset(plainData, 128, sizeof(char) * inwidth[i] * inheight[i] * 3);
	#if 0
							if(enablePBOs)
								drawPBO(i);
							else
							{
								if( rawRGBData[i] != NULL)
								{
									static int lwidth[MAXSTREAMS] = {0};
									static int lheight[MAXSTREAMS] = {0};

									if(lwidth[i] != inwidth[i] || lheight[i] != inheight[i] )
									{
										glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
									}
									else
										glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

									lwidth[i] = inwidth[i];
									lheight[i] = inheight[i];
								}
							}
	#endif
#if 0
							//nextFrameDelay[i] = -1;
							//drawQCount = (drawQCount + 1) % 5;

#ifdef USE_ODBASE
		drawMutex.grab();
#else
		WaitForSingleObject(drawMutex, INFINITE);
#endif
							//if(drawQCount == 5)
							//{
							//	//Sleep(10);
							//	//nextFrameDelay[i] = 10.0;
							//	continue;
							//}
							//else
								drawQCount++;
#ifdef USE_ODBASE
		drawMutex.release();
#else
		ReleaseMutex(drawMutex);
#endif
#endif
						//printf("***NOW drawing[%d]: drawQCount_size = 0\n", i, drawQCount);
							videoCallback(i, nextFrameDelay[i]);
						}
						else if (nextFrameDelay[i] == -1)
						{
							nextFrameDelay[i] = -1;
							printf("***Stop drawing[%d]: pictq_size = 0\n", i);

							//memset(rawRGBData[i], 0, sizeof(char) * inwidth[i] * inheight[i] * 4);
							//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
							//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

							videoCallback(i, (int)10);
							//Sleep(10);
						}
						else if (nextFrameDelay[i] == -100)
						{
							nextFrameDelay[i] = 100;
							printf("Stop drawing video[%d]: codecctx_ not init\n", i);
							//break;
						}
						else if (nextFrameDelay[i] == -50)
						{
							nextFrameDelay[i] = -50;
							printf("Stop drawing video[%d]: at video end.\n", i);

							videoCallback(i, (int)10);
							//totalActiveVideos--;
							//break;
						}
						else if (nextFrameDelay[i] == -5)
						{
							printf("***Stop drawing video[%d]: video not initialized\n", i);
							//status[i] = astop;

							memset(rawRGBData[i], 0, sizeof(char) * inwidth[i] * inheight[i] * 4);
							//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
							//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

							nextFrameDelay[i] = 0;
							//stopOrRestartVideo(i);
						}
						else if (nextFrameDelay[i] == -10)
						{
							nextFrameDelay[i] = 10;
							//nextFrameDelay[i] = 0;
							//printf("***Dropping frame[%d].\n", i);
						}
						//double durTime = getGlobalVideoTimer(i) - profileTime;
						//printf("uplTime:\t %f\n", durTime);

	#ifdef LOG
						fflush(videoDebugLogFile);
						//fclose(videoDebugLogFile);
	#endif
						//preTime[i] = getGlobalVideoTimer(i);
					}
					/*
					else
					{
						//fprintf(stdout, "...Underrun -> diff: %f ret: %f\n", timeDif, nextFrameDelay[i]);
					}
					*/
				}
				else if (status[i] == apause)
				{
					//totalActiveVideos++;
				}
				else if (status[i] == astop)
				{
					//memset(rawRGBData[i], 120, sizeof(char) * inwidth[i] * inheight[i] * 4);
					//glTexSubImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
				}
				//DWORD core = GetCurrentProcessorNumber();
				//printf("^^^^Main draw - Process Affinity: %d\n", core);
		}

		//GLenum err = glGetError();
		//if(err != GL_NO_ERROR)
		//{
		//	printf("UpdateVideo GL err.\n");
		//}

		if(totalActiveVideos == 0)
		{
			//printf("Sleeping video callback thread: %d\n", i);
			Sleep(100);
		}
	}
	//printf("totTime:\t %f\n", getGlobalVideoTimer(0) - totTime);
}

void SupdateVideoDraw()
{
#ifdef NETWORKED_AUDIO
	checkMessages();
#endif

	int totalActiveVideos = 0;
	for (int i = 0 ; i < MAXSTREAMS; i++)
	{
		if(videoTypeStreams[i] == ffmpeg)
			if (status[i] == aplay)
			{
				totalActiveVideos++;
				if(enablePBOs)
					updatePBOs(i);

#if 1
						currGlobalTimer[i] = getGlobalVideoTimer(i) / (double)1000;
						currGlobalTimer[i] -= totalPauseLength[i];
						//updatePBOs(i);
#endif

#if 1
	#ifdef NETWORKED_AUDIO
						double udpAudioClock = netAudioClock[i];
						g_netAudioClock[i] = udpAudioClock;
						if(preClock[i] == udpAudioClock)
						{
							udpAudioClock = -1;
						}
						else if(udpAudioClock >= 0)// if(preClock[i] < udpAudioClock)
						//if(udpAudioClock >= 0)
						{
							preClock[i] = udpAudioClock;
							diff[i] = (udpAudioClock-currGlobalTimer[i]);// + g_Delay[i];
						}
	#elif LOCAL_AUDIO
						double openALAudioClock = getOpenALAudioClock(i);
						double ffmpegClock = getFFmpegClock(i);
						g_netAudioClock[i] = openALAudioClock;

						static double lClockDiff[MAXSTREAMS] = {0};
						double lcDiff = openALAudioClock - ffmpegClock;
						if(fabs(lcDiff) < 1.0)
							lClockDiff[i] = lcDiff;
						else
						{
							//openALAudioClock = ffmpegClock + lClockDiff[i];
							openALAudioClock = openALAudioClock - (lcDiff) + lClockDiff[i];
						}
						//double openALAudioClock = (getOpenALAudioClock(i) > 0) ? getOpenALAudioClock(i) : currGlobalTimer[i];
						//printf("-lClockDiff[%d]: %f(s) - openALAudioClock[%d]: %f(s) - ffmpegClock[%d]: %f(s) - Diff[%d]: %f(s)\n", i, lClockDiff[i], i, openALAudioClock, i, ffmpegClock, i, lcDiff);
						openALAudioClock = ffmpegClock;
						if(enableDebugOutput)
							fprintf(stdout, "\rOAL_AUDIO_CLOCK[%d]: %09.4f(s)-ffmpegClock[%d]: %09.4f(s)", i, openALAudioClock, i, ffmpegClock);
						if(preClock[i] == openALAudioClock)
						{
							openALAudioClock = -1;
						}
						else if(openALAudioClock >= 0)
						//if(openALAudioClock >= 0)
						{
							preClock[i] = openALAudioClock;
							diff[i] = (openALAudioClock-currGlobalTimer[i]);
						}
	#endif
#endif
				pts[i] = getVideoPts(i);

#if 0
#ifdef USE_ODBASE
		drawMutex.grab();
#else
		WaitForSingleObject(drawMutex, INFINITE);
#endif
				int size = drawQCount;

#ifdef USE_ODBASE
		drawMutex.release();
#else
		ReleaseMutex(drawMutex);
#endif
#endif
				

				glBindTexture(GL_TEXTURE_2D, videotextures[i]);
				if (nextFrameDelay[i] > 0)// || !pbosEmpty(i))// || size > 0)
				{
					if(enablePBOs)// &&  size > 0)
					{
						//if(size <= 0)
						//{
						//	printf("STOP PBO - pbosSize[%d]: %d - drawQCount: %d - pts[%d]: %f, qindex[%d]: %d - pbopts: %f -NEXT_FRAME_DELAY[%d]: %f\n", i, pbosSize(i), size, i, pts[i], i, qindex[i], pboPTS[qindex[i]], i, nextFrameDelay[i]);
						//	continue;
						//}
						//printf("Draw PBO - size: %d - qindex[i]: %d - nextFrameDelay[i]: %f-----<\n", size, qindex[i], nextFrameDelay[i]);
//						printf("pbosSize[%d]: %d - pts[%d]: %f, qindex[%d]: %d - pbopts: %f -NEXT_FRAME_DELAY[%d]: %f\n", i, pbosSize(i), i, pts[i], i, qindex[i], pboPTS[qindex[i]], i, nextFrameDelay[i]);
						
						drawPBO(i);
						//drawPBO(i, size);

						//nextFrameDelay[i] = -1;

#if 0
#ifdef USE_ODBASE
		drawMutex.grab();
#else
		WaitForSingleObject(drawMutex, INFINITE);
#endif
				drawQCount--;

#ifdef USE_ODBASE
		drawMutex.release();
#else
		ReleaseMutex(drawMutex);
#endif
#endif
					}
					else
					{
						if( rawRGBData[i] != NULL)
						{
							static int lwidth[MAXSTREAMS] = {0};
							static int lheight[MAXSTREAMS] = {0};

							if(lwidth[i] != inwidth[i] || lheight[i] != inheight[i] )
							{
								glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_BGRA /*GL_RGBA*/ /*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
							}
							else
								glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_BGRA /*GL_RGBA*/ /*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

							lwidth[i] = inwidth[i];
							lheight[i] = inheight[i];

							//printf("++Frame ready\n");
						}
						//else
							//printf("++Frame not ready\n");
					}
				}
				else if (nextFrameDelay[i] == -1)
				{
					//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
					//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
				}
				else if (nextFrameDelay[i] == -100)
				{
					nextFrameDelay[i] = 100;
				}
				else if (nextFrameDelay[i] == -5)
				{
					//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
					//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

					stopOrRestartVideo(i);
				}
				else if (nextFrameDelay[i] == -10)
				{
					nextFrameDelay[i] = 10;
					//nextFrameDelay[i] = 0;
					//printf("***Dropping frame[%d].\n", i);
				}
			}
	}

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("UpdateVideoDraw GL err.\n");
	}

#if 0
	if(intelliSleep)
		{
			int minFrameDelayIndex = -1;
			double minFrameDelay = INT_MAX;
			for(int i=0; i<MAXSTREAMS; i++)
				if( status[i] == aplay && nextFrameDelay[i] < minFrameDelay && nextFrameDelay[i] > 0.0)
				{
					minFrameDelay = nextFrameDelay[i];
					minFrameDelayIndex = i;
				}

			if(minFrameDelayIndex != -1 && minFrameDelay != INT_MAX)
			{
				double now = getGlobalVideoTimer(minFrameDelayIndex);
				double remainingFrameDelay = fabs(double(now - preTime[minFrameDelayIndex] - nextFrameDelay[minFrameDelayIndex]));
				//double sleepTime = (remainingFrameDelay > 10.5) ? remainingFrameDelay / 2 : 0;
				//double sleepTime = (remainingFrameDelay > 5.5) ? remainingFrameDelay : 0;
				double sleepTime;
				if(remainingFrameDelay > 5.5)
					sleepTime = remainingFrameDelay;
				else
				{
					printf("///here");
					sleepTime = 0;
				}
				//printf("///now: %f - preTime[%d]: %f - nextFrameDelay[%d]: %f - remainingFrameDelay: %f - sleepTime: %f\n", now / 1000.0f, minFrameDelayIndex, preTime[minFrameDelayIndex] / 1000.0f, minFrameDelayIndex, nextFrameDelay[minFrameDelayIndex], remainingFrameDelay, sleepTime);
				Sleep((DWORD)sleepTime);
			}
		}
#endif

	if(totalActiveVideos == 0)
		Sleep(100);

	if(intelliSleep)
	{
		//int minFrameDelayIndex = -1;
		//double minFrameDelay = INT_MAX;
		//for(int i=0; i<MAXSTREAMS; i++)
		//	if( status[i] == aplay && nextFrameDelay[i] < minFrameDelay && nextFrameDelay[i] > 0.0)
		//	{
		//		minFrameDelay = nextFrameDelay[i];
		//		minFrameDelayIndex = i;
		//	}

		double minFPS = 0;
		for (int i = 0 ; i < MAXSTREAMS; i++)
		{
			if(status[i] == aplay && fps[i] > 0 && fps[i] > minFPS)
				minFPS = fps[i];
		}
		//printf("minFPS: %f\n", minFPS);
		if(minFPS != 0)
			Sleep((DWORD)1000/minFPS);
	}
	//Sleep(10);
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////// Asynchronize Multimedia ////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void updateVideoCallback(void* arg)
//void updateVideoCallback()
{
	int i = (int)arg;

	printf("ASUpdateVideo thread started: %d.\n", i);

	//double totTime = getGlobalVideoTimer(0);
	while(true)
	{
		int totalActiveVideos = 0;
		//for (int i = 0 ; i < MAXSTREAMS; i++)
		{
			//printf("\tStatus: %d\n", status[i]);
			if(videoTypeStreams[i] == ffmpeg)
				if (status[i] == aplay)
				{
					totalActiveVideos++;
	
#if 0
#ifdef NETWORKED_AUDIO
						if(enableDebugOutput)
							//#ifdef DEBUG_PRINTF
								fprintf(stdout, "\rNET_AUDIO_CLOCK[%d]: %019.16f(s)-GlobalVideoTimer[%d]: %f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %019.16f(s)", i, g_netAudioClock[i], i, 
									currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
							//#endif
#endif
#ifdef LOCAL_AUDIO
						if(enableDebugOutput)
							//#ifdef DEBUG_PRINTF
								fprintf(stdout, "-GlobalVideoTimer[%d]: %09.6f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %09.6f(s)", i, 
									currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
#endif
	#ifdef LOG
						fprintf(videoDebugLogFile, "-NEXT_FRAME_DELAY[%d]: %f\n", i, nextFrameDelay[i]);
	#endif
						if(enableDebugOutput)
							fprintf(stdout, "-NEXT_FRAME_DELAY[%d]: %f(ms)\n", i, nextFrameDelay[i]);

#endif						

					if(enablePBOs)
					{					
						double profileTime = getGlobalVideoTimer(i);
						int ret = updatePBOsRingBuffer(i);
						double durTime = getGlobalVideoTimer(i) - profileTime;
						
						//if(ret == 0)
						//	printf("UPLTime : %09.6f -- Pbo upload thread - ret: %d\n", durTime, ret);

						if(ret == -50)
						{
							printf("Finished uploading pbos---+\n");
							totalActiveVideos--;
						}
					}
					Sleep(10);
					//printf("pbosSize[%d]: %d - drawQCount: %d - pts[%d]: %f, qindex[%d]: %d - pbopts: %f\n", i, pbosSize(i), drawQCount, i, pts[i], i, qindex[i], pboPTS[qindex[i]]);
#ifdef LOG
					fflush(videoDebugLogFile);
					//fclose(videoDebugLogFile);
#endif
				}
				else if (status[i] == apause)
				{
					//totalActiveVideos++;
				}
				else if (status[i] == astop)
				{
					//memset(rawRGBData[i], 120, sizeof(char) * inwidth[i] * inheight[i] * 4);
					//glTexSubImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
				}
				//DWORD core = GetCurrentProcessorNumber();
				//printf("^^^^Main draw - Process Affinity: %d\n", core);
		}

		if(totalActiveVideos == 0)
		{
			//printf("Sleeping video callback thread: %d\n", i);
			Sleep(100);
		}
	}
	//printf("totTime:\t %f\n", getGlobalVideoTimer(0) - totTime);
	printf("ASUpdateVideo thread ended: %d.\n", i);
}

void updateVideoDraw()
{
#ifdef NETWORKED_AUDIO
	checkMessages();
#endif

	int totalActiveVideos = 0;
	for (int i = 0 ; i < MAXSTREAMS; i++)
	{
		if(videoTypeStreams[i] == ffmpeg)
			if (status[i] == aplay)
			{
				
				totalActiveVideos++;

				double dPboTime = -1, mapTime = -1, drawTime = -1;

				double profileTime = getGlobalVideoTimer(i);
				if(enablePBOs)
				{
					//updatePBOs(i);
					mapPBOsRingBuffer(i);
				}
				mapTime = getGlobalVideoTimer(i) - profileTime;
				//printf("MAPTime : %09.6f -- \n", mapTime);
				drawTime = getGlobalVideoTimer(i);

				double timeDif = getGlobalVideoTimer(i) - preTime[i];
				if(timeDif >= nextFrameDelay[i])
				{
#if 1
						currGlobalTimer[i] = getGlobalVideoTimer(i) / (double)1000;
						currGlobalTimer[i] -= totalPauseLength[i];
#endif
					preTime[i] = getGlobalVideoTimer(i);

	#if 1
		#ifdef NETWORKED_AUDIO
							double udpAudioClock = netAudioClock[i];
							g_netAudioClock[i] = udpAudioClock;
							if(preClock[i] == udpAudioClock)
							{
								udpAudioClock = -1;
							}
							else if(udpAudioClock >= 0)// if(preClock[i] < udpAudioClock)
							//if(udpAudioClock >= 0)
							{
								preClock[i] = udpAudioClock;
								diff[i] = (udpAudioClock-currGlobalTimer[i]);// + g_Delay[i];
							}
		#elif LOCAL_AUDIO
							double openALAudioClock = getOpenALAudioClock(i);
							double ffmpegClock = getFFmpegClock(i);
							g_netAudioClock[i] = openALAudioClock;

							static double lClockDiff[MAXSTREAMS] = {0};
							double lcDiff = openALAudioClock - ffmpegClock;
							if(fabs(lcDiff) < 1.0)
								lClockDiff[i] = lcDiff;
							else
							{
								//openALAudioClock = ffmpegClock + lClockDiff[i];
								openALAudioClock = openALAudioClock - (lcDiff) + lClockDiff[i];
							}
							//double openALAudioClock = (getOpenALAudioClock(i) > 0) ? getOpenALAudioClock(i) : currGlobalTimer[i];
							//printf("-lClockDiff[%d]: %f(s) - openALAudioClock[%d]: %f(s) - ffmpegClock[%d]: %f(s) - Diff[%d]: %f(s)\n", i, lClockDiff[i], i, openALAudioClock, i, ffmpegClock, i, lcDiff);
							openALAudioClock = ffmpegClock;
							//if(enableDebugOutput)
							//	fprintf(stdout, "\rOAL_AUDIO_CLOCK[%d]: %09.4f(s)-ffmpegClock[%d]: %09.4f(s)", i, openALAudioClock, i, ffmpegClock);
							if(preClock[i] == openALAudioClock)
							{
								openALAudioClock = -1;
							}
							else if(openALAudioClock >= 0)
							//if(openALAudioClock >= 0)
							{
								preClock[i] = openALAudioClock;
								diff[i] = (openALAudioClock-currGlobalTimer[i]);
							}
		#endif
	#endif
					double newClock = currGlobalTimer[i] + diff[i];
					newClock = newClock + g_Delay[i];

					pts[i] = getVideoPts(i);
					double frameTimeDiff = timeDif-nextFrameDelay[i];
					//if(enableDebugOutput)
					//	printf("-frameDelayDiff: %09.6f(ms)\n", frameTimeDiff); //-Current frame diff in ms - //(frameTimeDiff / (double)1000)
					newClock += (frameTimeDiff / (double)1000);
					//newClock = netAudioClock[i];

					/* Important */
					double pboPts = pboPTS[getCurrPbo(i)];

					//printf("qindex[%d]: %d - pbopts: %09.6f ++ \n", i, getCurrPbo(i), pboPTS[getCurrPbo(i)]);
					if(pboPts == -1)
					{
						//printf("qindex[%d]: %d - pbopts: %09.6f - pbo not uploaded\n", i, getCurrPbo(i), pboPts);
						////nextFrameDelay[i] = -1;
						continue;
					}

					if(enablePBOs)
						nextFrameDelay[i] = getNextVideoFramePbo(newClock, totalPauseLength[i], i, NULL, pboPts);
					else
						nextFrameDelay[i] = getNextVideoFrame(newClock, totalPauseLength[i], i, rawRGBData[i]);

		#ifdef LOCAL_AUDIO
					if(enableDebugOutput)
						fprintf(stdout, "-GlobalVideoTimer[%d]: %09.6f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %09.6f(s)-pboPts: %09.6f", i, 
										currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock, pboPts);
		#endif
					if(enableDebugOutput)
						fprintf(stdout, "-NEXT_FRAME_DELAY[%d]: %f(ms)\n", i, nextFrameDelay[i]);

					//printf("qindex[%d]: %d - pbopts: %09.6f ++ - nextFrameDelay[i]: %f-----<\n", i, getCurrPbo(i), pboPts, nextFrameDelay[i]);
					glBindTexture(GL_TEXTURE_2D, videotextures[i]);	
					if (nextFrameDelay[i] > 0)// || !pbosEmpty(i))// || size > 0)
					{
						if(enablePBOs)
						{
	//						printf("pbosSize[%d]: %d - pts[%d]: %f, qindex[%d]: %d - pbopts: %f -NEXT_FRAME_DELAY[%d]: %f\n", i, pbosSize(i), i, pts[i], i, qindex[i], pboPTS[qindex[i]], i, nextFrameDelay[i]);
							double profileTime = getGlobalVideoTimer(i);
							drawPBOsRingBuffer(i);
							dPboTime = getGlobalVideoTimer(i) - profileTime;
							//printf("\t\tPBO-Time: %09.6f ++ \n", dPboTime);
						}
						else
						{
							if(rawRGBData[i] != NULL)
							{
								static int lwidth[MAXSTREAMS] = {0};
								static int lheight[MAXSTREAMS] = {0};

								if(lwidth[i] != inwidth[i] || lheight[i] != inheight[i] )
								{
									glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_BGRA /*GL_RGBA*/ /*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
								}
								else
									glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_BGRA /*GL_RGBA*/ /*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

								lwidth[i] = inwidth[i];
								lheight[i] = inheight[i];
							}
						}

						//printf("++Frame drawer sleeping - nextFrameDelay[%d]: %f\n", i, nextFrameDelay[i]);
						//videoCallback(i, nextFrameDelay[i]);
					}
					if (nextFrameDelay[i] == -1)
					{
						printf("++nextFrameDelay[i] == -1\n");
						//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
						//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

						nextFrameDelay[i] = 1;//1000.0f/fps[i];//30.0;
					}
					else if (nextFrameDelay[i] == -100)
					{
						printf("++nextFrameDelay[i] == 100\n");
						nextFrameDelay[i] = 100;
					}
					else if (nextFrameDelay[i] == -50)
					{
						//printf("++nextFrameDelay[i] == -50\n");
						//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
						//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

						//stopOrRestartVideo(i);
					}
					else if (nextFrameDelay[i] == -10)
					{
						printf("++nextFrameDelay[i] == -10\n");
						nextFrameDelay[i] = 10;
						//nextFrameDelay[i] = 0;
						//printf("***Dropping frame[%d].\n", i);
					}
					//glBindTexture( GL_TEXTURE_2D, 0);
				}
				double durTime = getGlobalVideoTimer(i) - drawTime;
				//printf("DRAWTime: %09.6f MAPTime : %09.6f -- PBO-Time: %09.6f ++\n", durTime, mapTime, dPboTime);
			}
			else if (status[i] == apause)
			{
				//totalActiveVideos++;
			}
			else if (status[i] == astop)
			{
				//memset(rawRGBData[i], 120, sizeof(char) * inwidth[i] * inheight[i] * 4);
				//glTexSubImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
			}
	}

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("UpdateVideoDraw GL err.\n");
		printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
	}

#if 0
	if(intelliSleep)
		{
			int minFrameDelayIndex = -1;
			double minFrameDelay = INT_MAX;
			for(int i=0; i<MAXSTREAMS; i++)
				if( status[i] == aplay && nextFrameDelay[i] < minFrameDelay && nextFrameDelay[i] > 0.0)
				{
					minFrameDelay = nextFrameDelay[i];
					minFrameDelayIndex = i;
				}

			if(minFrameDelayIndex != -1 && minFrameDelay != INT_MAX)
			{
				double now = getGlobalVideoTimer(minFrameDelayIndex);
				double remainingFrameDelay = fabs(double(now - preTime[minFrameDelayIndex] - nextFrameDelay[minFrameDelayIndex]));
				//double sleepTime = (remainingFrameDelay > 10.5) ? remainingFrameDelay / 2 : 0;
				//double sleepTime = (remainingFrameDelay > 5.5) ? remainingFrameDelay : 0;
				double sleepTime;
				if(remainingFrameDelay > 5.5)
					sleepTime = remainingFrameDelay;
				else
				{
					printf("///here");
					sleepTime = 0;
				}
				//printf("///now: %f - preTime[%d]: %f - nextFrameDelay[%d]: %f - remainingFrameDelay: %f - sleepTime: %f\n", now / 1000.0f, minFrameDelayIndex, preTime[minFrameDelayIndex] / 1000.0f, minFrameDelayIndex, nextFrameDelay[minFrameDelayIndex], remainingFrameDelay, sleepTime);
				Sleep((DWORD)sleepTime);
			}
		}
#endif

	if(totalActiveVideos == 0)
		Sleep(100);

	if(intelliSleep)
	{
		//int minFrameDelayIndex = -1;
		//double minFrameDelay = INT_MAX;
		//for(int i=0; i<MAXSTREAMS; i++)
		//	if( status[i] == aplay && nextFrameDelay[i] < minFrameDelay && nextFrameDelay[i] > 0.0)
		//	{
		//		minFrameDelay = nextFrameDelay[i];
		//		minFrameDelayIndex = i;
		//	}

		double minFPS = 0;
		for (int i = 0 ; i < MAXSTREAMS; i++)
		{
			if(status[i] == aplay && fps[i] > 0 && fps[i] > minFPS)
				minFPS = fps[i];
		}
		//printf("minFPS: %f\n", minFPS);
		if(minFPS != 0)
			Sleep((DWORD)1000/minFPS);
	}
	//Sleep(10);
}