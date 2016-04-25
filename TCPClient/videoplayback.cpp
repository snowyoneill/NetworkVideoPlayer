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

#include <Mmsystem.h>
#pragma comment(lib, "Winmm.lib" )

#ifdef NETWORKED_AUDIO
//#include "netClockSync.h"
#include "netClockSyncWin.h"
extern int _clientSenderID;
#endif
//#define DEBUG_PRINTF
#define MULTI_TIMER

#include "pbo.h"

HANDLE hTimer = NULL;
HANDLE hTimerQueue = NULL;
int videoCallbackTimer(int side, double delayms);
VOID CALLBACK updateFrame(PVOID lpParam, BOOLEAN TimerOrWaitFired);
//void updateFrame(void* arg);
void timerCB(int side, double ms);
double frame_timer[MAXSTREAMS];
double frame_last_delay[MAXSTREAMS];
double frame_last_pts[MAXSTREAMS];

#ifdef USE_ODBASE
ODBase::Lock	commandMutex;
#else
HANDLE			commandMutex;
#endif

enum {ffs = 1, ffmpeg}; // 1 = ffs, 2 = ffmpeg

double	g_AudioClock[MAXSTREAMS];
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
double frameTimeDiff[MAXSTREAMS];

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

	printf("\n//////////////// TIMER ///////////////////\n");
	#define TARGET_RESOLUTION 1 // 1-millisecond target resolution
	TIMECAPS tc;
	UINT     wTimerRes;
	if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) != TIMERR_NOERROR) 
	{
		// Error; application can't continue.
		printf ("Error - cannot get system time resolution\n");
	}
	printf ("Minimum supported resolution = %d(ms)\n", tc.wPeriodMin);
	printf ("Maximum supported resolution = %d(ms)\n", tc.wPeriodMax);
	wTimerRes = min(max(tc.wPeriodMin, TARGET_RESOLUTION), tc.wPeriodMax);
	timeBeginPeriod(wTimerRes);
	printf ("Resolution = %d(ms)\n", wTimerRes);

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
	initPBOs();

	// Init global timer.
	for(int i=0; i<MAXSTREAMS; i++)
		startGlobalVideoTimer(i);
#ifdef LOG
		videoDebugLogFile = fopen("log.txt", "w");
		if(videoDebugLogFile == NULL)
			printf("Could not log file for writing");
#endif

#ifdef MULTI_TIMER
	//if(multiTimer)
	//	for(int i=0; i<MAXSTREAMS; i++)
	//		_beginthread(updateVideoCallback, 0, /*NULL*/(void *)i);
#endif

//#ifdef MULTI_TIMER
//	if(multiTimer)
//		for(int i=0; i<MAXSTREAMS; i++)
//			_beginthread(updateFrame, 0, /*NULL*/(void *)i);
//#endif

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("InitVideo GL err.\n");
		printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
	}

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

typedef struct netMes {
	int type;
	int stream;
	//string videoName;
	char videoName[256];
	double seekDuration;
} commandMsg;

#include <queue>
queue<commandMsg> commandQueue;

enum {nstop, nload, npause, nseek, dPbo, iPbo, cPbo};

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

	if(enablePBOs)
	{
		{
				commandMsg newMsg;
				newMsg.type = iPbo;
				newMsg.stream = videounit;
				sprintf(newMsg.videoName, "%s", "");

			#ifdef USE_ODBASE
				commandMutex.grab();
			#else
				WaitForSingleObject(commandMutex, INFINITE);
			#endif
				commandQueue.push(newMsg);
			#ifdef USE_ODBASE
				commandMutex.release();
			#else
				ReleaseMutex(commandMutex);
			#endif
		}
	}
	else
	{
		if(rawRGBData[videounit] != NULL)
			free (rawRGBData[videounit]);
		rawRGBData[videounit] = (char*)malloc(sizeof(char) * inwidth[videounit] * inheight[videounit] * 4);
		//memset(rawRGBData[videounit], 0, sizeof(char) * inwidth[videounit] * inheight[videounit] * 4);
		//_beginthread(updateVideo, 0, 0  );
	}

	return true;
}
/* Seek the specified video stream by the seekDuration (seconds).
 */
//void seekVideo(int side, double netclock, double seekDuration)
void seekVideo(int side, double seekDuration, double seekBaseTime, bool sendSeekTCPCommand)
{
	if(status[side] == aplay || status[side] == apause)
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
		preTime[videounit] = 0.0;
		diff[videounit] = 0.0;
		preClock[videounit] = 0.0;
		totalPauseLength[videounit] = 0.0;
		frame_timer[videounit] = 0.0;
		frame_last_delay[videounit] = 0.0;
		frame_last_pts[videounit] = 0.0;

		status[videounit] = aplay;

		//HANDLE hstdout = GetStdHandle( STD_OUTPUT_HANDLE );
		//WORD   index   = 0;

		//// Remember how things were when we started
		//CONSOLE_SCREEN_BUFFER_INFO csbi;
		//GetConsoleScreenBufferInfo( hstdout, &csbi );

		//// Tell the user how to stop
		//SetConsoleTextAttribute( hstdout,  0x0000 | 0x0020 );

		//// Keep users happy
		//SetConsoleTextAttribute( hstdout, csbi.wAttributes );

		timerCB(videounit, 100);
		//videoCallbackTimer(videounit, 100);
		//clearPBOs(videounit);
		
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
	//GLenum err = glGetError();
	//if(err != GL_NO_ERROR)
	//{
	//	printf("PlayVideo GL err.\n");
	//}
}


/* Stops the specified video stream.
 * If the video is currently playing then set the status array to astop, call the video shut procedure, set the screen to black
 * and finally send a stop command to the audio server.
 */
void stopVideo(int videounit, bool sendStopTCPCommand)
{
	if(status[videounit] == aplay || status[videounit] == apause || status[videounit] == pboUplDone)
	{
		printf("@@@Stopping video %d.\n", videounit);
		status[videounit] = astop;
		closeVideoThread(videounit);

		{
				commandMsg newMsg;
				newMsg.type = dPbo;
				newMsg.stream = videounit;
				sprintf(newMsg.videoName, "%s", "");

			#ifdef USE_ODBASE
				commandMutex.grab();
			#else
				WaitForSingleObject(commandMutex, INFINITE);
			#endif
				commandQueue.push(newMsg);
			#ifdef USE_ODBASE
				commandMutex.release();
			#else
				ReleaseMutex(commandMutex);
			#endif
		}

				//if(hTimerQueue != NULL)
				//{
				//	if (!DeleteTimerQueue(hTimerQueue) && GetLastError() != ERROR_IO_PENDING)
				//		printf("DeleteTimerQueue failed (%d)\n", GetLastError());

				//	hTimerQueue = NULL;
				//	printf("Delete Timer Queue: %d\n", videounit);
				//}

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
	//GLenum err = glGetError();
	//if(err != GL_NO_ERROR)
	//{
	//	printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
	//	printf("StopVideo GL err.\n");
	//}
}

/* Pause the selected video stream.
 * If the video is currently paused then calling this a second time will resume playback.
 */
void pauseVideo(int videounit, bool sendPauseTCPCommand)
{
	if(status[videounit] == aplay || status[videounit] == apause || status[videounit] == pboUplDone)
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
	pauseFrameReader(videounit);
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

void closeAudioEnv()
{
	stopAudioDecoder();
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

#if 0
std::vector<commandMsg> netCommands;
#endif

void checkMessages()
{
	//printf("___Checking commands...\n");
	while (!commandQueue.empty())
	{
#ifdef USE_ODBASE
		commandMutex.grab();
#else
		WaitForSingleObject(commandMutex, INFINITE);
#endif
		commandMsg qMsg = commandQueue.front();
#ifdef USE_ODBASE
		commandMutex.release();
#else
		ReleaseMutex(commandMutex);
#endif
		printf("+++Queue commands - size %d - type %d : %s\n", commandQueue.size(), qMsg.type, qMsg.videoName);
		//if(qMsg.type == nload/* && qMsg.videoName != ""*/)
		//{
		//	screenSyncLoadVideo(qMsg.videoName, qMsg.stream);
		//	commandQueue.pop();
		//}
		//if(qMsg.type == nstop)
		//{
		//	stopOrRestartVideo(qMsg.stream);
		//	commandQueue.pop();
		//}
		//if(qMsg.type == nseek)
		//{
		//	seekVideo(qMsg.stream, qMsg.seekDuration, false);
		//	commandQueue.pop();
		//}
		//if(qMsg.type == dPbo)
		//{
		//	deletePBOs(qMsg.stream);
		//	commandQueue.pop();
		//}
		//if(qMsg.type == iPbo)
		//{
		//	intiPBOsSide(inwidth[qMsg.stream], inheight[qMsg.stream], qMsg.stream);
		//	commandQueue.pop();
		//}
		//if(qMsg.type == cPbo)
		//{
		//	cleanUpPbosAndTextures(qMsg.stream);
		//	commandQueue.pop();
		//}
		switch(qMsg.type)
		{
			case nload :
				screenSyncLoadVideo(qMsg.videoName, qMsg.stream);
				break;
			case nstop :
				stopOrRestartVideo(qMsg.stream);
				break;
			case nseek :
				seekVideo(qMsg.stream, qMsg.seekDuration, false);
				break;
			case dPbo :
				deletePBOs(qMsg.stream);
				break;
			case iPbo :
				intiPBOsSide(inwidth[qMsg.stream], inheight[qMsg.stream], qMsg.stream);
				break;
			case cPbo :
				cleanUpPbosAndTextures(qMsg.stream);
				break;
			default: printf("Un-known command\n");
					break;
		}
		commandQueue.pop();
	}
}

void deletePbos(int side)
{
	deletePBOs(side);
}

void closePbosAndTextures()
{
	printf("Deleting video textures.\n");
	glDeleteTextures(MAXSTREAMS, videotextures);

	printf("Deleting PBO synchronization locks.\n");
	closePBOs();
	printf("Deleting FRAME synchronization locks.\n");
	closeFrameLoader();
}

void cleanUpPbosAndTextures(int videounit)
{
	closePbosAndTextures();

	timeEndPeriod(TARGET_RESOLUTION);

	//PostQuitMessage(0);	// Send A Quit Message
	//exit(0);

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
		printf("Free  Res GL err.\n");
	}
}
#ifdef NETWORKED_AUDIO
void notifyScreenSyncLoadVideo(string videoName, int videounit)
{
	commandMsg newMsg;
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
	commandQueue.push(newMsg);
	
#ifdef USE_ODBASE
	commandMutex.release();
#else
	ReleaseMutex(commandMutex);
#endif
}

void notifyScreenSeekVideo(int videounit, double seekDur)
{
	commandMsg newMsg;
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
	commandQueue.push(newMsg);
#ifdef USE_ODBASE
	commandMutex.release();
#else
	ReleaseMutex(commandMutex);
#endif
}

void notifyStopOrRestartVideo(int videounit)
{
	commandMsg newMsg;
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
	commandQueue.push(newMsg);
#ifdef USE_ODBASE
	commandMutex.release();
#else
	ReleaseMutex(commandMutex);
#endif
	//printf("commandMutex.release()\n");
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
		commandMsg qMsg;
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
				g_AudioClock[i] = udpAudioClock;
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
				g_AudioClock[i] = udpAudioClock;
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
					g_AudioClock[i] = udpAudioClock;
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
					g_AudioClock[i] = openALAudioClock;

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

					frameTimeDiff[i] = timeDif-nextFrameDelay[i];
					//if(enableDebugOutput)
					if ( frameTimeDiff[i] > 5.00 ) 
						printf("-frameDelayDiff: %09.6f(ms)", frameTimeDiff[i]); //-Current frame diff in ms - //(frameTimeDiff[i] / (double)1000)
					printf("\n");
					#ifdef LOG
					//#ifdef DEBUG_PRINTF
						fprintf(videoDebugLogFile, "-frameTimeDif: %09.6f", frameTimeDiff[i]);
						//printf("-frameTimeDif: %09.6f", frameTimeDiff[i]);
					//#endif
					#endif
					//memset(rawRGBData[i], 128, sizeof(char) * inwidth[i] * inheight[i] * 4);
					pts[i] = getVideoPts(i);
					newClock += (frameTimeDiff[i] / (double)1000);
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


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// Synchronous Multimedia ////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef MULTI_TIMER
	#if 0
	//int drawQCount = 0;

	#ifdef USE_ODBASE
		ODBase::Lock drawMutex("drawMutex");
	#else
		HANDLE drawMutex;
	#endif
	#endif

void SupdateVideoCallback(void* arg)
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
						g_AudioClock[i] = udpAudioClock;
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
						g_AudioClock[i] = openALAudioClock;

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
								fprintf(stdout, "\rNET_AUDIO_CLOCK[%d]: %019.16f(s)-GlobalVideoTimer[%d]: %f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %019.16f(s)", i, g_AudioClock[i], i, 
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
						g_AudioClock[i] = udpAudioClock;
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
						g_AudioClock[i] = openALAudioClock;

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
////////////////////////////////////// Asynchronous Multimedia ////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool stopUploadThread[MAXSTREAMS];
void shutdownUploadThread(int side)
{
	stopUploadThread[side] = true;

	//while(stopUploadThread[side])
	//{ 
	//	printf("Waiting for pbo upload thread to shutdown...\n");
	//	Sleep(100);
	//}
}

void updateVideoCallback(void* arg)
{
	int i = (int)arg;

	printf("---ASUpdateVideo thread started: %d.\n", i);

	//double totTime = getGlobalVideoTimer(0);
	//while(true)
	while(!stopUploadThread[i])
	{
		int totalActiveVideos = 0;
		//for (int i = 0 ; i < MAXSTREAMS; i++)
		{
			//printf("\tStatus: %d\n", status[i]);
			if(videoTypeStreams[i] == ffmpeg)
				if (status[i] == aplay)
				{
					totalActiveVideos++;
	
#ifdef LOG
					fprintf(videoDebugLogFile, "-NEXT_FRAME_DELAY[%d]: %f\n", i, nextFrameDelay[i]);
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
							status[i] = pboUplDone;
						}
					}
					//Sleep(10);
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
				}
				else if (status[i] == pboUplDone)
				{
					//memset(rawRGBData[i], 120, sizeof(char) * inwidth[i] * inheight[i] * 4);
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
	stopUploadThread[i] = false;

	//printf("totTime:\t %f\n", getGlobalVideoTimer(0) - totTime);
	printf("---ASUpdateVideo thread ended: %d.\n", i);
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

				static double staticTime = getGlobalVideoTimer(i);
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

				//printf("profileTime: %09.6f -- timeDif: %09.6f -- nextFrameDelay: %09.6f\n", (profileTime-staticTime), timeDif, nextFrameDelay[i]);

				if(timeDif >= nextFrameDelay[i])
				//if(timeDif >= nextFrameDelay[i]-5.00)
				{
#if 1
						currGlobalTimer[i] = getGlobalVideoTimer(i) / (double)1000;
						currGlobalTimer[i] -= totalPauseLength[i];
#endif
					preTime[i] = getGlobalVideoTimer(i);

	#if 1
		#ifdef NETWORKED_AUDIO
							double udpAudioClock = netAudioClock[i];
							g_AudioClock[i] = udpAudioClock;
							if(preClock[i] == udpAudioClock)
							{
								udpAudioClock = -1;
							}
							else if(udpAudioClock >= 0)// if(preClock[i] < udpAudioClock)
							{
								preClock[i] = udpAudioClock;
								diff[i] = (udpAudioClock-currGlobalTimer[i]);// + g_Delay[i];
							}
		#elif LOCAL_AUDIO
							double openALAudioClock = getOpenALAudioClock(i);
							double ffmpegClock = getFFmpegClock(i);
							g_AudioClock[i] = openALAudioClock;

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
					double newClock = currGlobalTimer[i] + diff[i] + g_Delay[i];

#ifdef NETWORKED_AUDIO
					if(enableDebugOutput)
						//#ifdef DEBUG_PRINTF
							fprintf(stdout, "\rNET_AUDIO_CLOCK[%d]: %019.16f(s)-GlobalVideoTimer[%d]: %f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %019.16f(s)", i, g_AudioClock[i], i, 
								currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
						//#endif
#endif
#ifdef LOCAL_AUDIO
					if(enableDebugOutput)
						//#ifdef DEBUG_PRINTF
							fprintf(stdout, "-GlobalVideoTimer[%d]: %09.6f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %09.6f(s)", i, 
								currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
#endif

					double frameDiff = timeDif-nextFrameDelay[i];
					//if(enableDebugOutput)
					if ( frameDiff > 5.00 )
						printf("-frameDelayDiff: %09.6f(ms) nextFrameDelay: %09.6f(ms)\n", frameDiff, nextFrameDelay[i]); //-Current frame diff in ms - //(frameTimeDiff[i] / (double)1000)
					printf("\n");

					frameTimeDiff[i] = frameDiff / (double)1000;
					newClock += frameTimeDiff[i];
					//newClock = netAudioClock[i];

					//#define PRECISION 0.01
					//newClock -= fmod(newClock,PRECISION);

					pts[i] = pboPTS[getCurrPbo(i)];//getVideoPts(i);

					/* Important */
					double pboPts = pboPTS[getCurrPbo(i)];

					//if(abs(pts[i]-newClock) > 2.0)
					//{
					//	printf("Seeking video stream: %d\n", i);
					//	seekVideo(i, diff[i], pts[i]-newClock, false);
					//}

					//printf("qindex[%d]: %d - pbopts: %09.6f ++ \n", i, getCurrPbo(i), pboPTS[getCurrPbo(i)]);
					if(pboPts == -1)
					{
						//printf("qindex[%d]: %d - pbopts: %09.6f - pbo not uploaded\n", i, getCurrPbo(i), pboPts);
						nextFrameDelay[i] = 10;
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
					if (nextFrameDelay[i] >= 0)// || !pbosEmpty(i))// || size > 0)
					{
						//#define PRECISION 1.0
						//frameDiff -= fmod(frameDiff,PRECISION);
						//nextFrameDelay[i] -= frameDiff;//frameTimeDiff[i];;
						if(enablePBOs)
						{
							//printf("pbosSize[%d]: %d - pts[%d]: %f, qindex[%d]: %d - pbopts: %f -NEXT_FRAME_DELAY[%d]: %f\n", i, pbosSize(i), i, pts[i], i, qindex[i], pboPTS[qindex[i]], i, nextFrameDelay[i]);
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

					//preTime[i] = getGlobalVideoTimer(i);
				}
				else
				{
					//printf("~Underrun->diff: %09.6f(ms) nextFrameDelay: %09.6f(ms)\n", timeDif, nextFrameDelay[i]);
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
	if(enableDebugOutput)
	{
		GLenum err = glGetError();
		if(err != GL_NO_ERROR)
		{
			printf("UpdateVideoDraw GL err.\n");
			printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
		}
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
///////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if 0
int videoCallbackTimer(int side, double delayms)
{
    //int arg = side;
	int *arg = new int(side);

    // Delete all timers in the timer queue.
    //if (!DeleteTimerQueueTimer(hTimerQueue, hTimer, INVALID_HANDLE_VALUE) && GetLastError() != ERROR_IO_PENDING)
	if(hTimerQueue)
	{
		//printf("DeleteTimerQueue (%x)\n", hTimerQueue);
		if (!DeleteTimerQueueTimer( hTimerQueue, hTimer, NULL ) && GetLastError() != ERROR_IO_PENDING)
			if (!DeleteTimerQueue(hTimerQueue) && GetLastError() != ERROR_IO_PENDING)
				printf("DeleteTimerQueue failed (%d)\n", GetLastError());
		hTimerQueue = 0;
	}

    // Create the timer queue.
    hTimerQueue = CreateTimerQueue();
    if (NULL == hTimerQueue)
    {
        printf("CreateTimerQueue failed (%d)\n", GetLastError());
        return 2;
    }

    // Set a timer to call the timer routine in delay milliseconds.
    if (!CreateTimerQueueTimer( &hTimer, hTimerQueue,(WAITORTIMERCALLBACK)updateFrame, arg , delayms, 0, 0))
    {
        printf("CreateTimerQueueTimer failed (%d)\n", GetLastError());
        return 3;
    }
    //if (!CreateTimerQueueTimer( &hTimer, NULL,(WAITORTIMERCALLBACK)updateFrame, &arg , delayms, 0, 0))
    //{
    //    printf("CreateTimerQueueTimer failed (%d)\n", GetLastError());
    //    return 3;
    //}
    return 0;
}

VOID CALLBACK updateFrame(PVOID lpParam, BOOLEAN TimerOrWaitFired)
{
	if (lpParam == NULL)
	{
		printf("TimerRoutine lpParam is NULL\n");
	}
	else
	{
        // lpParam points to the argument; in this case it is an int
		int uSide = *(int*)lpParam;
		delete (int*)lpParam;
        //printf("Timer routine called. Side is %d.\n", side);
        if(TimerOrWaitFired)
        {
            //printf("The wait timed out.\n");
        }
        else
        {
            printf("The wait event was signaled.\n");
        }
#endif
#if 0
void highResSleep(int sleepDur)
{
		// note: BE SURE YOU CALL timeBeginPeriod(1) at program startup!!!
        // note: BE SURE YOU CALL timeEndPeriod(1) at program exit!!!
        // note: that will require linking to winmm.lib
        // note: never use static initializers (like this) with Winamp plug-ins!

	    LARGE_INTEGER m_high_perf_timer_freq;
		if(!QueryPerformanceFrequency(&m_high_perf_timer_freq))
			printf("QueryPerformanceFrequency failed!\n");


        int max_fps = 60;

        LARGE_INTEGER t;
        QueryPerformanceCounter(&t);

        static LARGE_INTEGER m_prev_end_of_frame = t;

		double sT = getGlobalVideoTimer(0);
        if (m_prev_end_of_frame.QuadPart != 0)
        {
            //int ticks_to_wait = (int)m_high_perf_timer_freq.QuadPart / max_fps;

			int ticks_to_wait = (int)m_high_perf_timer_freq.QuadPart /1000.0;
			ticks_to_wait *= sleepDur;
            int done = 0;
            do
            {
                QueryPerformanceCounter(&t);
                
                int ticks_passed = (int)((__int64)t.QuadPart - (__int64)m_prev_end_of_frame.QuadPart);
                int ticks_left = ticks_to_wait - ticks_passed;

                if (t.QuadPart < m_prev_end_of_frame.QuadPart)    // time wrap
                    done = 1;
                if (ticks_passed >= ticks_to_wait)
                    done = 1;
                
                if (!done)
                {
                    // if > 0.002s left, do Sleep(1), which will actually sleep some 
                    //   steady amount, probably 1-2 ms,
                    //   and do so in a nice way (cpu meter drops; laptop battery spared).
                    // otherwise, do a few Sleep(0)'s, which just give up the timeslice,
                    //   but don't really save cpu or battery, but do pass a tiny
                    //   amount of time.
                    if (ticks_left > (int)m_high_perf_timer_freq.QuadPart*2/1000)
                        Sleep(1);
                    else                        
                        for (int i=0; i<10; i++) 
                            Sleep(0);  // causes thread to give up its timeslice
                }
				//printf ("%d - %d = %d(ms)\n", ticks_passed, ticks_left, ticks_to_wait);
            }
            while (!done);            
        }
        m_prev_end_of_frame = t;

		double eT = getGlobalVideoTimer(0);
		//printf ("sT: %09.6f - eT: %09.6f = %09.6f(ms)\n", sT, eT, eT-sT);
}

void updateFrame(void* arg)
{
	int side = (int)arg;

	highResSleep(60);

	//int count = 0;
	//while(++count < 60)
	//	highResSleep(30);

	//count = 0;
	//while(++count < 60)
	//	highResSleep(10);

	printf("ASTimer Video thread started: %d.\n", side);

	while(true)

#endif

#if 1
typedef void ( CALLBACK *LPTIMECALLBACK)(UINT uTimerID, UINT uMsg, DWORD_PTR dwUser, DWORD_PTR dw1, DWORD_PTR dw2);
void CALLBACK TimerFunction(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dw1, DWORD dw2);

int m_nID[MAXSTREAMS];
void timerCB(int side, double ms)
{
	//printf("Setting timer[%d]: %f\n", side, ms);
	if(ms < 1.0)
		ms = 1;
	m_nID[side] = timeSetEvent( ms, TARGET_RESOLUTION, (LPTIMECALLBACK) TimerFunction, /*0*/  (DWORD)side, TIME_ONESHOT );
	if(m_nID[side] == 0)
		printf("Error setting timer: %f\n", ms);
}

static void CALLBACK TimerFunction(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dw1, DWORD dw2)
{
	int uSide = (int) dwUser;

	timeKillEvent(m_nID[uSide]);
	//while(true)
#endif
	{
	int i = uSide;
	int totalActiveVideos = 0;
	//for (int i = 0 ; i < MAXSTREAMS; i++)
	{
		if(videoTypeStreams[i] == ffmpeg)
			if (status[i] == aplay || status[i] == pboUplDone)
			{
				totalActiveVideos++;
				double timeDif = getGlobalVideoTimer(i) - preTime[i];

				static double fDiff;
				double now = getGlobalVideoTimer(i);
				double diffT = now - fDiff;
				//printf("now: %09.6f - last_time: %09.6f - delta: %09.6f(ms) - nextFrameDelay[i]:%09.6f(ms) - driff: %09.6f(ms)\n", now, fDiff, diffT, nextFrameDelay[i], driff);
				fDiff = now;
				{
					currGlobalTimer[i] = getGlobalVideoTimer(i) / (double)1000;
					currGlobalTimer[i] -= totalPauseLength[i];
					preTime[i] = getGlobalVideoTimer(i);

					
		#ifdef NETWORKED_AUDIO
					syncThreadCheckMessages(i);
					double udpAudioClock = netAudioClock[i];
					g_AudioClock[i] = udpAudioClock;

					static double alpha = 0.75, accumulator = 1.0;
				#if 1
					if(preClock[i] == udpAudioClock)
					{
						//printf("Repeated network clock...calculating new delta!!! - preClock: %f - udpAudioClock: %f\n", preClock[i], udpAudioClock);
						udpAudioClock = -1;
						//printf("diff[i]: %09.6f - accumulator: %09.6f\n", diff[i], accumulator);
						//diff[i] = accumulator;
					}
					else if(udpAudioClock >= 0)// if(preClock[i] < udpAudioClock)
					{
						preClock[i] = udpAudioClock;
						diff[i] = (udpAudioClock-currGlobalTimer[i]);// + g_Delay[i];						
					}
				#endif
		#elif LOCAL_AUDIO
					double openALAudioClock = getOpenALAudioClock(i);
					double ffmpegClock = getFFmpegClock(i);
					g_AudioClock[i] = openALAudioClock;

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
					accumulator = (alpha * diff[i]) + (1.0 - alpha) * accumulator;

					double newClock = currGlobalTimer[i] + diff[i] + g_Delay[i];
#ifdef NETWORKED_AUDIO
					//newClock = netAudioClock[i];
#endif
					//#define PRECISION 0.001
					//newClock -= fmod(newClock,PRECISION);
					//newClock -= fmod(newClock,1.0/fps[i]);

#ifdef NETWORKED_AUDIO
					if(enableDebugOutput)
						//#ifdef DEBUG_PRINTF
							fprintf(stdout, "NET_AUDIO_CLOCK[%d]: %019.16f(s)-GlobalVideoTimer[%d]: %f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %019.16f(s)\n", i, g_AudioClock[i], i, 
								currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
						//#endif
#endif
#ifdef LOCAL_AUDIO
					if(enableDebugOutput)
						//#ifdef DEBUG_PRINTF
							fprintf(stdout, "-GlobalVideoTimer[%d]: %09.6f(s)-Diff: %09.6f(s)-GEN_AUDIO_CLOCK[%d]: %09.6f(s)", i, 
								currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock);
#endif

					double frameDiff = timeDif-nextFrameDelay[i];
					if(enableDebugOutput)
						printf("-frameDelayDiff: %09.6f(ms) nextFrameDelay: %09.6f(ms)\n", frameDiff, nextFrameDelay[i]); //-Current frame diff in ms - //(frameTimeDiff[i] / (double)1000)
					//printf("\n");
					frameTimeDiff[i] = frameDiff / (double)1000;
					//newClock += frameTimeDiff[i];

					/* Important */
					int pboIdx = getCurrPbo(i);//pboPTS[qindex[i]];
					double pboPts = pboPTS[pboIdx];

					//printf("qindex[%d]: %d - pbopts: %09.6f\n", i, pboIdx, pboPts);
					//if(pboPts != -50 && pboPts == -1)
					//{
					//	printf("qindex[%d]: %d - pts[i]: %09.6f - newClock: %09.6f - nextFrameDelay[i]: %09.6f Frame WAIT\n", i, pboIdx, pboPts, newClock, nextFrameDelay[i]);
					//	//nextFrameDelay[i] = 1;
					//	//if(newClock - pts[i] > (ceil(fps[i]))/1000 * 10)
					//	//{
					//	//	printf("skipFrame - pts[i]: %09.6f - newClock: %09.6f\n", pts[i], newClock);
					//	//	skipFrame(i);
					//	//}
					//	//waitOnBufFill(i);
					//	//videoCallbackTimer(i, 1);
					//	//break;
					//	timerCB(i, nextFrameDelay[i]);
					//	return;
					//}

					pts[i] = pboPts;//getVideoPts(i);

					//// If more than 10 frames behind.
					//if(pboPts != -50 && newClock - pboPts > (ceil(fps[i]))/1000 * 10)
					//{
					//	printf("qindex[%d]: %d - pts[i]: %09.6f - newClock: %09.6f - diff: %09.6f - nextFrameDelay[i]: %09.6f Frame SKIP\n", i, pboIdx, pboPts, newClock, newClock-pboPts, nextFrameDelay[i]);
					//	skipFrame(i);
					//	//waitOnBufFill(i);
					//	pboPTS[pboIdx] = -1;
					//	nextFrameDelay[i] = 1;//ceil(fps[i])/2;
					//
					//	videoCallbackTimer(i, nextFrameDelay[i]);
					//	break;
					//}

					//if(abs(newClock - pts[i]) > 1.5)
					//{
					//	printf("Seeking video stream: %d - newClock: %09.6f - pts: %09.6f - diff: %09.6f\n", i, newClock, pts[i], pts[i] - newClock);
					//	seekVideo(i, pts[i] - newClock, -1, false);
					//}

					// If more than 5 frames ahead.
					//if(pboPts - newClock > (ceil(fps[i]))/1000 * 2 && pboPts - newClock < (ceil(fps[i]))/1000 * 3)
					//{
					//	printf("qindex[%d]: %d - pts[i]: %09.6f - newClock: %09.6f - Frame HOLD\n", i, pboIdx, pboPts, newClock);
					//
					//	nextFrameDelay[i] = ceil(1000/fps[i]);
					//	videoCallbackTimer(i, nextFrameDelay[i]);
					//	break;
					//}
#if 0
					if(enablePBOs)
						nextFrameDelay[i] = getNextVideoFramePbo(newClock, totalPauseLength[i], i, NULL, pboPts);
					else
						nextFrameDelay[i] = getNextVideoFrame(newClock, totalPauseLength[i], i, rawRGBData[i]);
#endif
					if(pboPts >= 0)
					{
#if 1
						double delay = pboPts - frame_last_pts[i]; /* the pts from last time */
						if(delay <= 0 || delay >= 1.0)
						{
							if(frame_last_delay[i] <= 0)
								frame_last_delay[i] = 1/fps[i];
							/* if incorrect delay, use previous one */
							delay = frame_last_delay[i];
							//printf("incorrect delay !!! pbo -> ptsClk: %09.6f...Delay: %f - state->frame_last_pts: %f\n", pboPts, delay, frame_last_pts[i]);
						}
						/* save for next time */
						frame_last_delay[i] = delay;
						frame_last_pts[i] = pboPts;

						if (PTSSlave)
						{
					get_udp:
							newClock = get_master_clock_udp(pboPts);				
							if(newClock < 0) {
								printf("\nmaster exited\n");
								exit(1);
							}
						}

						//newClock = newClock - fmod(newClock,delay);
						double cDiff =  pboPts - newClock;// + 0.02;
						//delay = (delay <= 0 ) ? 0.01 : delay;
						double cDiffPts = cDiff - fmod(cDiff,delay);
#if 1
						/* no AV sync correction is done if below the minimum AV sync threshold */
						#define AV_SYNC_THRESHOLD_MIN 0.01
						/* AV sync correction is done if above the maximum AV sync threshold */
						#define AV_SYNC_THRESHOLD_MAX 0.1
						/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
						#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1

						#define FFMIN(a,b) ((a) > (b) ? (b) : (a))
						#define FFMAX(a,b) ((a) > (b) ? (a) : (b))
						double max_frame_duration = 10.0;
						double sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));				
						//sync_threshold = delay;
						if (/*!isnan(cDiff) && */fabs(cDiff) < max_frame_duration) {
							if (cDiffPts <= -sync_threshold)
								delay = FFMAX(0, delay + cDiffPts);
							else if (cDiffPts >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
								delay = delay + cDiffPts;
							else if (cDiffPts >= sync_threshold)
							{
								//delay = 2 * delay;
								delay = delay + cDiffPts;
								//delay = cDiff;

								//Sleep(delay);
								//syncThreadCheckMessagesBlk(i);

								if(PTSSlave)
									goto get_udp;
							}
						}
#endif
						frame_timer[i] += delay;
						double actual_delay = frame_timer[i] - currGlobalTimer[i];//newClock;
						

						//delay = cDiff;
						//actual_delay = delay;
						//printf("pts[i]: %f - newClock: %f - cDiff: %09.6f - cDiffPts: %09.6f - sync_threshold: %f - delay: %f - frame_timer[i]: %f - actual_delay: %f\n", pboPts, newClock, cDiff, cDiffPts, sync_threshold, delay, frame_timer[i], actual_delay);
						if(actual_delay < 0.01) {
							/* Really it should skip the picture instead */
							actual_delay = 0.01;

							//increamentPBORingBuffer(i);
							////skipFrame(i);
							//nextFrameDelay[i] = 1;
							//timerCB(i, nextFrameDelay[i]);
							//return;
						}
						nextFrameDelay[i] = (actual_delay * 1000);

					
						//printf("NET_CLOCK[%d]: %09.6f(s)-GlobalTimer[%d]: %09.6f(s)-Diff: %09.6f(s)-GEN_CLOCK[%d]: %09.6f(s) qindex[%d]: %d pts[%d]: %09.6f(s) cDiff[%d]: %09.6f(s) frame_timer: %09.6f-nextFrameDelay[i]: %09.6f\n", \
							i, g_AudioClock[i], i, currGlobalTimer[i], diff[i] + g_Delay[i], i, newClock, i, pboIdx, i, pboPts, i, cDiff, frame_timer[i], nextFrameDelay[i]);

						//printf("qindex[%d]: %d - pbopts: %09.6f - newClock: %09.6f -cDiff: %09.6f frame_timer: %09.6f ++ diff: %09.6f - nextFrameDelay[i]: %f-----<\n", \
							i, pboIdx, pboPts, newClock, cDiff, frame_timer[i], diff[i], nextFrameDelay[i]);

						//if(nextFrameDelay[i] > 0) {
						//  const int nearestDivisor = 1;
						//	int mul = (int)(nextFrameDelay[i]) / nearestDivisor;
						//	//int ext = (int)(f_delay) % nearestDivisor;
						//	nextFrameDelay[i] = nearestDivisor * (mul);// + ext);
						//	if(nextFrameDelay[i] < 1)
						//		nextFrameDelay[i] = 1;
						//}
#endif
					}
					if(enableDebugOutput)
						fprintf(stdout, "-NEXT_FRAME_DELAY[%d]: %f(ms)\n", i, nextFrameDelay[i]);
			
					//printf("<<<newClock: %09.6f\n",newClock);
					//printf("qindex[%d]: %d - pbopts: %09.6f - newClock: %09.6f ++ diff: %09.6f - nextFrameDelay[i]: %f-----<\n", i, pboIdx, pboPts, newClock, diff[i], nextFrameDelay[i]);
					if (nextFrameDelay[i] >= 0)// || !pbosEmpty(i))// || size > 0)
					{
						static double sendPtsTime = pboPts;
						if (PTSMaster && pboPts > 0 && pboPts != sendPtsTime) {
							sendPtsTime = pboPts;
							char current_time[256];
							sprintf(current_time, "%f", pboPts);
							//printf("s %f\n", pboPts);
							send_udp(udp_ip, udp_port, current_time);
						}

						if (PTSSlave) {
							//printf("r %f\n", pboPts);
						}

						if(enablePBOs)
						{
							increamentPBORingBuffer(i);
							//nextFrameDelay[i] = getNextVideoFramePbo(newClock, totalPauseLength[i], i, NULL, pboPts);
							//printf("qindex[%d]: %d - pts[i]: %09.6f - newClock: %09.6f - nextFrameDelay: %d - Frame INCR\n", i, pboIdx, pboPts, newClock, (int)nextFrameDelay[i]);
						}
						else
						{
							//	nextFrameDelay[i] = getNextVideoFrame(newClock, totalPauseLength[i], i, rawRGBData[i]);
						}
						//printf("++Frame drawer sleeping - nextFrameDelay[%d]: %f\n", i, nextFrameDelay[i]);
						timerCB(i, nextFrameDelay[i]);
						//highResSleep(nextFrameDelay[i]);
						//Sleep(nextFrameDelay[i]);
						//videoCallbackTimer(i, 1);
						//videoCallbackTimer(i, nextFrameDelay[i]);
					}
					else if (nextFrameDelay[i] == -1)
					{
						printf("++nextFrameDelay[i] == -1\n");
						//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
						//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

						//nextFrameDelay[i] = 1;//1000.0f/fps[i];//30.0;

						//highResSleep(10);
						timerCB(i, 10);
						//Sleep(1);
						//videoCallbackTimer(i, 1);
						//videoCallbackTimer(i, 10);
					}
					else if (nextFrameDelay[i] == -100)
					{
						printf("++nextFrameDelay[i] == 100\n");
						//nextFrameDelay[i] = 100;

						//highResSleep(100);
						timerCB(i, 100);
						//Sleep(100);
						//videoCallbackTimer(i, 1);
						//videoCallbackTimer(i, 100);
					}
					else if (nextFrameDelay[i] == -50)
					{
						printf("++nextFrameDelay[i] == -50\n");
						//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
						//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

						//stopOrRestartVideo(i);
					}
					else if (nextFrameDelay[i] == -10)
					{
						printf("++nextFrameDelay[i] == -10\n");
						//nextFrameDelay[i] = 10;
						//videoCallbackTimer(i, 10);
						//nextFrameDelay[i] = 0;
						//printf("***Dropping frame[%d].\n", i);
					}
					//preTime[i] = getGlobalVideoTimer(i);
				}
			}
			else if (status[i] == apause)
			{
				//totalActiveVideos++;
			}
			else if (status[i] == astop)
			{
				//memset(rawRGBData[i], 120, sizeof(char) * inwidth[i] * inheight[i] * 4);
			}
	}

	if(totalActiveVideos == 0)
	{
		//highResSleep(100);
		timerCB(i, 100);
		//Sleep(100);
		//videoCallbackTimer(i, 100);
	}

	}
	//}
}

void checkCommandMessages()
{
	checkMessages();
}

void updateVideoDraw2()
{
	int totalActiveVideos = 0;
	for (int i = 0 ; i < MAXSTREAMS; i++)
	{
		if(videoTypeStreams[i] == ffmpeg)
			if (status[i] == aplay || status[i] == pboUplDone)
			{
				totalActiveVideos++;

				double dPboTime = -1, mapTime = -1, drawTime = -1;
				static double staticTime = getGlobalVideoTimer(i);

				drawTime = getGlobalVideoTimer(i);
				double timeDif = getGlobalVideoTimer(i) - preTime[i];
#if 1
				//printf("profileTime: %09.6f -- timeDif: %09.6f -- nextFrameDelay: %09.6f\n", (profileTime-staticTime), timeDif, nextFrameDelay[i]);
				{
					//printf("qindex[%d]: %d - pbopts: %09.6f ++ - nextFrameDelay[i]: %f-----<\n", i, getCurrPbo(i), pboPts, nextFrameDelay[i]);
					glBindTexture(GL_TEXTURE_2D, videotextures[i]);	
					//if (nextFrameDelay[i] >= 0)// || !pbosEmpty(i))// || size > 0)
					{
						if(enablePBOs)
						{
							//printf("pbosSize[%d]: %d - pts[%d]: %f, qindex[%d]: %d - pbopts: %f -NEXT_FRAME_DELAY[%d]: %f\n", i, pbosSize(i), i, pts[i], i, qindex[i], pboPTS[qindex[i]], i, nextFrameDelay[i]);
							double profileTime = getGlobalVideoTimer(i);
							drawPBOsRingBuffer2(i);
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
					}
					if (nextFrameDelay[i] == -1)
					{
						printf("++nextFrameDelay[i] == -1\n");
						//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
						//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
					}
					else if (nextFrameDelay[i] == -100)
					{
						printf("++nextFrameDelay[i] == 100\n");
					}
					else if (nextFrameDelay[i] == -50)
					{
						//printf("++nextFrameDelay[i] == -50\n");
						//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, inwidth[i], inheight[i], 0, GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);
						//glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, inwidth[i], inheight[i], GL_RGBA/*GL_RGB*/, GL_UNSIGNED_BYTE, rawRGBData[i]);

						//stopOrRestartVideo(i);
					}
				}
#endif
				double profileTime = getGlobalVideoTimer(i);
				if(enablePBOs)
				{
					//updatePBOs(i);
					mapPBOsRingBuffer(i);
				}
				mapTime = getGlobalVideoTimer(i) - profileTime;
				//printf("MAPTime : %09.6f -- \n", mapTime);

				{
				double profileTime = getGlobalVideoTimer(i);
				if(enablePBOs)
					unmapPBOsRingBuffer(i);
				mapTime = getGlobalVideoTimer(i) - profileTime;
				//printf("UMPTime : %09.6f -- \n", mapTime);
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
	if(enableDebugOutput)
	{
		GLenum err = glGetError();
		if(err != GL_NO_ERROR)
		{
			printf("UpdateVideoDraw GL err.\n");
			printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
		}
	}

	if(totalActiveVideos == 0)
		Sleep(100);
}