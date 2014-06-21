#define _CRT_SECURE_NO_DEPRECATE 1

#ifdef FFS
#define RINGSIZE 30
#endif

#include "ODFfmpegSource.h"
#include "frameloader.h"
#ifdef USE_ODBASE
#include "ODBase.h"
#else
#include "shlwapi.h"
#endif
#include <windows.h>
#include <process.h>
#include <string.h>

#ifdef LOCAL_AUDIO
#if (defined (OPENAL_ENV))
	#include "audio.h"
#endif
#endif

#ifdef FFS
//extern ODBase::Log* logFile;

long long int lpFrequency, start[MAXSTREAMS], stop[MAXSTREAMS], totaltime = 0;
int decodetime = -1, totaldecodes = 0;
char filepath[MAXSTREAMS][1000];
ddsringbuffer ringbuffer[MAXSTREAMS][RINGSIZE];
int firstframe[MAXSTREAMS],lastframe[MAXSTREAMS], currentframe[MAXSTREAMS];
HANDLE producersemaphore[MAXSTREAMS] = {NULL,NULL};
bool changeBuffer[MAXSTREAMS] = {false, false};
//extern bool blockused[8*8];
//extern bool hibernate[2];
char ffsfilename[MAXSTREAMS][1064];
#endif

typedef struct {
	__int64 side;
	ODFfmpegSource* vidSource;
	HANDLE readerSemaphore;
}threadArgs;

#ifdef  USE_CRITICAL_SECTION
	#ifdef OPENAL_ENV
	CRITICAL_SECTION openALStreamLock[MAXSTREAMS];
	#endif
	CRITICAL_SECTION videoStreamLock[MAXSTREAMS];
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
ODBase::Lock openALStreamLock[MAXSTREAMS];//("audioStreamLock");
ODBase::Lock videoStreamLock[MAXSTREAMS];//("videoStreamLock");
#else
#ifdef OPENAL_ENV
HANDLE openALStreamLock[MAXSTREAMS];// = CreateMutex(NULL, false, NULL);
#endif
HANDLE videoStreamLock[MAXSTREAMS];// = CreateMutex(NULL, false, NULL);
#endif
#endif

ODFfmpegSource* videoSources[MAXSTREAMS];
bool videoFinished[MAXSTREAMS] = {false};

//////////////////////////////////////////// OPENAL AUDIO //////////////////////////////////////////////
#ifdef LOCAL_AUDIO
#ifdef OPENAL_ENV
bool audioSourceInit[MAXSTREAMS] = {false};
AudioEnvironment *openALEnv;
bool stopAudioThread = false;

void decodeAndPlayAllAudioStreams(void* dummy);

int getALAudioInfo(unsigned int *rate, int *channels, int *type)
{
    if(type)
    {
        if(*type == AV_SAMPLE_FMT_U8)
            *type = AL_UNSIGNED_BYTE;
        else if(*type == AV_SAMPLE_FMT_S16)
            *type = AL_SHORT;
        else if(*type == AV_SAMPLE_FMT_S32)
            *type = AL_INT;
        else if(*type == AV_SAMPLE_FMT_FLT)
            *type = AL_FLOAT;
        else if(*type == AV_SAMPLE_FMT_DBL)
            *type = AL_DOUBLE;
        else
            return 1;
    }
    if(channels)
    {
        if(*channels == AV_CH_LAYOUT_MONO)
            *channels = AL_MONO;
        else if(*channels == AV_CH_LAYOUT_STEREO)
            *channels = AL_STEREO;
        else if(*channels == AV_CH_LAYOUT_QUAD)
            *channels = AL_QUAD;
        else if(*channels == AV_CH_LAYOUT_5POINT1) /* AV_CH_LAYOUT_5POINT1 - Causes corrupted audio with some mkv's using avc. */
            *channels = AL_5POINT1;
		else if(*channels == AV_CH_LAYOUT_5POINT1_BACK)
            *channels = AL_5POINT1;
        else if(*channels == AV_CH_LAYOUT_7POINT1)
            *channels = AL_7POINT1;
        else
            return 1;
    }

    return 0;
}

int initAudioEnvir()
{
	_beginthread(decodeAndPlayAllAudioStreams, 0, 0 );

	return 1;
}

//#ifdef PRELOAD
//bool initAudioSource(int side)
//#else
bool initAudioSource(int side, int* bufSize)
//#endif
{
	if(audioSourceInit[side] == false)
	{
		unsigned int rate;
		int channels;
		int type;
		
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif

		if(videoSources[side] != NULL)
		{
			if(videoSources[side]->getAVAudioInfo(&rate, &channels, &type) != 0)
			{
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
				printf("Error getting ffmpeg audio info - could not initialise audio source [%d]\n", side);
				return false;
			}
		}
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif

		ALenum err = alGetError();
		if(err != AL_NO_ERROR)
			printf("OpenAL Error: %s (0x%x), @\n", alGetString(err), err);

		if(getALAudioInfo(&rate, &channels, &type) != 0)
		{
			printf("Error setting OpenAL audio info - could not initialise audio source [%d]\n", side);
			return false;
		}

		err = alGetError();
		if(err != AL_NO_ERROR)
			printf("OpenAL Error: %s (0x%x), @\n", alGetString(err), err);

//#ifndef PRELOAD
		if(!g_preloadAudio)
		{
			/* Allocate enough space for the temp buffer, given the format */
			//*bufSize = FramesToBytes(BUFFER_SIZE, channels, type);
			*bufSize = FramesToBytes(g_bufferSize, channels, type);
			if(openALEnv->initAudioSource(side, &rate, &channels, &type, *bufSize) != 0)
			{
				printf("Error initialising OpenAL audio source [%d]\n", side);
				return false;
			}
		}
//#else
		else
		{
			if(openALEnv->initAudioSource(side, &rate, &channels, &type) != 0)
			{
				printf("Error initialising OpenAL audio source [%d]\n", side);
				return false;
			}
		}
//#endif

		err = alGetError();
		if(err != AL_NO_ERROR)
			printf("OpenAL Error: %s (0x%x), @\n", alGetString(err), err);

		printf("Initialising audio source [%d]\n", side);
		
		char* data[NUM_BUFFERS];
		for(int i=0; i<NUM_BUFFERS; i++)
//#ifdef PRELOAD
		if(g_preloadAudio)
			//data[i] = (char*) malloc(BUFFER_SIZE);
			data[i] = (char*) malloc(g_bufferSize);
//#else
		else
			data[i] = (char*) malloc(*bufSize);
//#endif

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif
		if(videoSources[side] == NULL)
		{
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
			return false;
		}

		int count[NUM_BUFFERS];
		//videoStreamLock[i].grab();
		if(videoSources[side] != NULL)
		{
			for(int i=0; i<NUM_BUFFERS; i++)
			{
				do
					{
//#ifdef PRELOAD
						if(g_preloadAudio)
						{
							count[i] = videoSources[side]->getAudioBuffer(data[i], g_bufferSize);//BUFFER_SIZE
							//count[i] = videoSources[side]->getAVAudioData(data[i], g_bufferSize);
						}
//#else
						else
						{
							//count[i] = videoSources[side]->getAudioBuffer(data[i], *bufSize);
							count[i] = videoSources[side]->getAVAudioData(data[i], *bufSize);
						}
//#endif
					} while(count[i] == 0);
			}
		}
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif

		audioSourceInit[side] = true;

//#ifdef PRELOAD
		if(g_preloadAudio)
		{
			if(openALEnv->initAudioBuffers(side, count, data) != 0)
			{
				printf("Error trying to initialise source[%d]\n", side);
				for(int i=0; i<NUM_BUFFERS; i++)
					free(data[i]);
				return false;
			}
		}
//#else
		else
		{
			if(openALEnv->initAudioBuffers(side, count, data, *bufSize) != 0)
//#endif
			{
				printf("Error trying to initialise source[%d]\n", side);
				for(int i=0; i<NUM_BUFFERS; i++)
					free(data[i]);
				return false;
			}
		}

		err = alGetError();
		if(err != AL_NO_ERROR)
			printf("OpenAL Error: %s (0x%x), @\n", alGetString(err), err);

		for(int i=0; i<NUM_BUFFERS; i++)
			free(data[i]);
	}

	return true;
}

int pauseAudioStream(int side)
{
	if(audioSourceInit[side] == true)
	{
		if(openALEnv->pauseAudioStream(side)==0)
			return true;
		else
			printf("\nCouldn't pause audio stream\n");
	}

	return false;
}

int closeAudioStream(int side)
{
	if(audioSourceInit[side] == true)
	{
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&openALStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		openALStreamLock[side].grab();
#else
		WaitForSingleObject(openALStreamLock[side], INFINITE);
#endif
#endif

		//while(openALEnv->isSourceStillPlaying(side)) { openALStreamLock[side].release(); Sleep(10); openALStreamLock[side].grab(); }
		//if(!openALEnv->isSourceStillPlaying(side))
		{
			if(openALEnv->closeAudioStream(side)==0)
			{
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&openALStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		openALStreamLock[side].release();
#else
		ReleaseMutex(openALStreamLock[side]);
#endif
#endif
				audioSourceInit[side] = false;
				return true;
			}
			else
				printf("\nCouldn't close audio stream\n");
		}
		//else
		//	printf("\nAudio buffer still playing.\n");
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&openALStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		openALStreamLock[side].release();
#else
		ReleaseMutex(openALStreamLock[side]);
#endif
#endif
	}

	return false;
}

double getALAudioClock(int side)
{
	if(audioSourceInit[side] == true && videoSources[side] != NULL)
		return openALEnv->getElapsedALTime(side);
		//return videoSources[side]->get_audio_clock();
	else
		return -1;
}
double getFFmpeg(int side)
{
	if(audioSourceInit[side] == true && videoSources[side] != NULL)
		return videoSources[side]->get_audio_clock();
	else
		return -1;
}
void setALVolume(int side, float level)
{
	openALEnv->setVolume(side, level);
}

void stopAudioDecoder()
{
	stopAudioThread = true;
}

void decodeAndPlayAllAudioStreams(void* dummy)
{
	openALEnv = new AudioEnvironment(g_preloadAudio, g_bufferSize);
	//openALEnv = new AudioEnvironment(true, 2048);
	int audioDeviceInit = openALEnv->initOpenAlDevice();
	if(audioDeviceInit)
		printf("AUDIO DEVICE NOT DETECTED - AUDIO CLOCK WILL NOT RETURN CORRECT FRAME DELAYS!\n");

//#ifdef PRELOAD
	if(g_preloadAudio)
	{
		printf("\n///////////////////////// Pre-load all sources and buffers /////////////////////\n");
		for (int i = 0; i < MAXSTREAMS; i++)
		{
			if(openALEnv->preLoadSources(i) != 0)  // clientID 0 to context 0
			{
				printf("Could not pre-load buffers.\n");
				exit(EXIT_FAILURE);
			}
			openALEnv->setVolume(i, 1.0f);
		}
	}
//#endif

	char* data;
//#ifdef PRELOAD
	if(g_preloadAudio)
		//data = (char*) malloc(BUFFER_SIZE);
		data = (char*) malloc(g_bufferSize);
//#endif
	int bufSize[MAXSTREAMS];

	int count;

	while ( !stopAudioThread )
	{
		int totalActiveAudioStreams = 0;
		for (int i=0; i<MAXSTREAMS; i++)
		{
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&openALStreamLock[i]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		openALStreamLock[i].grab();
#else
		WaitForSingleObject(openALStreamLock[i], INFINITE);
#endif
#endif

			//videoStreamLock[i].grab();
			if(videoSources[i] != NULL)
				if(videoFinished[i]==false)
				{
					count = -1;
					totalActiveAudioStreams++;

					if(!videoSources[i]->hasAudiostream())
					{
						//printf("No ffmpeg audio track detected: side[%d]\n", i);
						totalActiveAudioStreams--;
						
					}
					else
					{
//#ifdef PRELOAD
					//if(initAudioSource(i))
//#else
					//int bufSize[MAXSTREAMS];
					if(initAudioSource(i, &bufSize[i]))
//#endif
					{
						int totPaused = 0;
						for(int k=0; k<MAXSTREAMS; k++)
						{
							if(openALEnv->isPaused(k) == 1 || audioSourceInit[k] == false)
								totPaused++;
						}
						if(totPaused == MAXSTREAMS )
						//if(openALEnv->isPaused(i) == 1)
						{
							//printf("Audio stream paused: %d\n");
							Sleep(10);
						}
						else
						{
							//printf("Check buffers:  %d\n", i);
							int ret = openALEnv->checkAudioBuffers(i);
							if(ret == 0)
							{
//#ifdef PRELOAD
								if(g_preloadAudio)
								{
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[i]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[i].grab();
#else
	WaitForSingleObject(videoStreamLock[i], INFINITE);
#endif
#endif
									if(videoSources[i] != NULL)
									{
										count = videoSources[i]->getAudioBuffer(data, g_bufferSize);/*BUFFER_SIZE*/
										//count = videoSources[i]->getAVAudioData(data, g_bufferSize);
										
									}
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[i]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[i].release();
#else
	ReleaseMutex(videoStreamLock[i]);
#endif
#endif
									if(count >= 0)
										ret = openALEnv->refillAudioBuffers(i, count, data);
								}
//#else
								else
								{
									//printf("Refill buffers: %d\n", i);
									char* data = (char*) malloc(bufSize[i]);
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[i]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[i].grab();
#else
	WaitForSingleObject(videoStreamLock[i], INFINITE);
#endif
#endif

									if(videoSources[i] != NULL)
									{
										//count = videoSources[i]->getAudioBuffer(data, bufSize[i]);
										count = videoSources[i]->getAVAudioData(data, bufSize[i]);
									}
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[i]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[i].release();
#else
	ReleaseMutex(videoStreamLock[i]);
#endif
#endif
									if(count >= 0)
										ret = openALEnv->refillAudioBuffers(i, count, data, bufSize[i]);

									free(data);
								}
//#endif
								if(ret == 1) {
									MessageBox(NULL, "Audio Buffer Error", "Could not queue audio buffers", MB_OK | MB_ICONEXCLAMATION);
									stopAudioThread = true;
									break;
									//exit(-1);
								}
								else if(ret == 0)// && videoSources[i]->atVideoEnd_)
								{
									//videoStreamLock[i].grab();
									//if(videoSources[i] != NULL)
									//{
									//	if(videoSources[i]->atVideoEnd_)
									//	{
									//		//do
									//		//{
									//			//Sleep(10);
									//		//}  while( openALEnv->isSourceStillPlaying(i) );
									//		if(!openALEnv->isSourceStillPlaying(i))
									//		{
									//			closeVideoThread(i);
									//			closeAudioStream(i);
									//		}
									//	}
									//}
									//videoStreamLock[i].release();

									bool atEnd = false;
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[i]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[i].grab();
#else
	WaitForSingleObject(videoStreamLock[i], INFINITE);
#endif
#endif

									if(videoSources[i] != NULL)
										if(videoSources[i]->atVideoEnd_)
											atEnd = true;
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[i]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[i].release();
#else
	ReleaseMutex(videoStreamLock[i]);
#endif
#endif

									if(!openALEnv->isSourceStillPlaying(i))
									{
										if(atEnd)
										{
										printf("Audio thread is no longer playing--->\n");
										closeVideoThread(i);
										closeAudioStream(i);
										}

										//stopOrRestartVideo(i);
									}
								}
//#ifndef PRELOAD
								//free(data);
//#endif
							}
						}
					}
					else
					{
						printf("Error trying to initialise source[%d]\n", i);
						//closeVideoThread(i);
						closeAudioStream(i);
						//Sleep(10);
						//stopAudioThread = true;
					}
					}
				}
			//videoStreamLock[i].release();
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&openALStreamLock[i]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		openALStreamLock[i].release();
#else
		ReleaseMutex(openALStreamLock[i]);
#endif
#endif
		}
		if(totalActiveAudioStreams == 0)
			Sleep(100);

	}
//#ifdef PRELOAD
	if(g_preloadAudio)
		free(data);
//#endif
	delete openALEnv;

	stopAudioThread = false;
	printf("+++Closing audio decoder/player thread.\n");
	//return 1;
}
#endif
#endif
///////////////////////////////////////////// FFSLoader ///////////////////////////////////////////////
#ifdef FFS
ffsImage* getnextframe(int frame, __int64 side)
{
    // Search through ring buffer with valid frame having correct index.
	for(int i = 0 ; i < RINGSIZE; i++)
	{
		if(ringbuffer[side][i].free == false && ringbuffer[side][i].frame == frame)
			return ringbuffer[side][i].ffs.getffsimagepointer();
	}

	return NULL;
}

void donewithframe(int frame, __int64 side)
{
	if( lastframe[side] < RINGSIZE)	// The frame can remain in memory.. We dont need to free the memory.
		return;

	for(int i = 0 ; i < RINGSIZE; i++)
	{
		if(ringbuffer[side][i].free == false && ringbuffer[side][i].frame == frame)
		{
			ringbuffer[side][i].free = true;
			ReleaseSemaphore(producersemaphore[side], 1, NULL );
		}
	}
}

int getfirstframeinbuffer(__int64 side)
{
	int lowestframe = SINT32_MAX;

	for(int i = 0 ; i < RINGSIZE; i++)
	{
		if(ringbuffer[side][i].free == false)
		{
			if( ringbuffer[side][i].frame < lowestframe)
				lowestframe = ringbuffer[side][i].frame;
		}
	}

	return lowestframe;
}

void underrunframe(int frame, __int64 side)
{
	const int bufferadvance = 4;

	int firstframeinbuffer = getfirstframeinbuffer(side);
	if ( currentframe[side] < frame || firstframeinbuffer > (frame + bufferadvance) )
	{
      //  if (logFile)
      //      logFile->write("under-run in video %s, %d, %d, %d\n", 
      //                     ODBase::SINT64ToString(side).c_str(), frame, 
      //                     currentframe[side], firstframeinbuffer);
      //
		currentframe[side] = frame + bufferadvance; // getdecodetime() and calculate exact
		if ( currentframe[side] >= lastframe[side] )
			currentframe[side] = firstframe[side];

		changeBuffer[side] = true;
		ReleaseSemaphore(producersemaphore[side], 1, NULL );
	}
}

void restartframes(__int64 side)
{
	currentframe[side] = 0;
	changeBuffer[side] = true;
	ReleaseSemaphore(producersemaphore[side], 1, NULL );
}


void changeRingBuffer(__int64 side)
{
	if( changeBuffer[side] == true )
	{
		for(int i = 0 ; i < RINGSIZE; i++)
		{
			ringbuffer[side][i].free = true;
			ReleaseSemaphore(producersemaphore[side], 1, NULL );
		}
		changeBuffer[side] = false;
	}
}

//bool loadframes(const char * mediapath,int & last_frame, int & fps, int & inputwidth, int & inputheight, int & blocks, int & compressed, __int64 side)
bool loadframes(const char * mediapath,int & last_frame, int & fps, int & inputwidth, int & inputheight, int & blocks, int & compressed, __int64 side, int &format)
{
	/*if(loadclipdata(mediapath,last_frame,fps,inputwidth,inputheight,blocks,compressed,ffsfilename[side]) == false)
		return false;*/
	if(loadclipdata(mediapath,last_frame,fps,inputwidth,inputheight,blocks,compressed,ffsfilename[side], format) == false)
		return false;

	QueryPerformanceFrequency( (LARGE_INTEGER *)&lpFrequency);
	lpFrequency /= 1000;
	strcpy(filepath[side],mediapath);

	firstframe[side] = 0;
	lastframe[side] = last_frame;
	currentframe[side] = firstframe[side];
	//currentframe[side]  = (last_frame - (int)(delay * fps) ) % last_frame; //delay

	threadArgs *pS = new threadArgs;
	pS->side = side;
	//pS->vidSource = NULL;

	//threadArgs *pS =(threadArgs *)malloc(sizeof(threadArgs));
	//threadArgs pS;
	//pS.side = side;
	//pS.vidSource = videoSources;

	if( producersemaphore[side] == NULL)
	{
		for(int i = 0 ; i < RINGSIZE; i++)
		{
			ringbuffer[side][i].free = true;
			ringbuffer[side][i].badmemory = false;
		}
		producersemaphore[side] = CreateSemaphore( NULL, 0 , RINGSIZE, NULL);
		//_beginthread(frameproducer, 4096, (void *) side  );
		_beginthread(frameproducer, 4096, (void *) pS  );
		//_beginthread((void(*)(void*))frameproducer, 4096, (void *) &pS  );
		ReleaseSemaphore(producersemaphore[side], RINGSIZE, NULL );
	}
	else
	{
		changeBuffer[side] = true;
		ReleaseSemaphore(producersemaphore[side], 1, NULL );
	}

	return true;
}

//void frameproducer(threadArgs * parg)
void frameproducer(void * parg)
{
	//__int64 side = (__int64)parg;

	threadArgs* args = ((threadArgs*)(parg));
	__int64 side = args->side;
	//ODFfmpegSource* videoSources = args->vidSource;
	
	//IplImage* tempFrame;
	//tempFrame = cvCreateImage(cvSize(videoSources->width_, videoSources->height_), IPL_DEPTH_8U, 3);
	//cvSet(tempFrame, cvScalar(128, 128, 128));

	for ( ; ; )
	//while(!endOfVideo)
	{
/*		while(hibernate[side] == true)
		{
			Sleep(10);
		}
*/		WaitForSingleObject(producersemaphore[side],INFINITE);
		changeRingBuffer(side);
		for(int i = 0 ; i < RINGSIZE; i++)
		{
			if(ringbuffer[side][i].free == true && ringbuffer[side][i].badmemory == false)
			{
				char filename[1100];
				int goodload;
				ringbuffer[side][i].frame = currentframe[side]++;
				if ( currentframe[side] > lastframe[side] )
					currentframe[side] = firstframe[side];
				//sprintf(filename,ffsfilename[side], filepath[side], ringbuffer[side][i].frame );
				sprintf(filename,"%s/flat-%06d.ffs", filepath[side], ringbuffer[side][i].frame );

				QueryPerformanceCounter( (LARGE_INTEGER *)&start[side] );


					//if (videoSources->advanceFrame() == false)
					//{
					//	endOfVideo = true;
					//	printf("\nEnd of video.");
					//	break;

					//}
					//else
					//{
					//	videoSources->getCurrFrame(&tempFrame);
						//cvSaveImage("currentFrame.png", currentFrame);
						//goodload = ringbuffer[side][i].ffs.loadVideoframe(tempFrame->imageData, tempFrame->width, tempFrame->height);
						//cvReleaseImage(&tempFrame);
					//}

				goodload = ringbuffer[side][i].ffs.loadframe(filename, NULL);
				QueryPerformanceCounter( (LARGE_INTEGER *)&stop[side] );
				totaltime += stop[side] - start[side];
				totaldecodes++;

				if( goodload == 1)
					ringbuffer[side][i].free = false;
				else if( goodload == -4)
					ringbuffer[side][i].badmemory = true;

				break;
			}
		}

		changeRingBuffer(side);
	}

	delete args;
}
#endif
//////////////////////////////////////////// VideoLoader //////////////////////////////////////////////
void closeFrameLoader()
{

	for(int i=0; i<MAXSTREAMS; i++)
	{
		while(videoFinished[i] == true) { Sleep(10); }
#ifdef  USE_CRITICAL_SECTION
		DeleteCriticalSection(&videoStreamLock[i]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	;//
#else
	CloseHandle(videoStreamLock[side]);
#endif
#endif

	}

}

/* Register FFmpeg
 */
void registerFFmpeg()
{
	av_register_all();
	for(int i=0; i<MAXSTREAMS; i++)
	{

	#ifdef  USE_CRITICAL_SECTION
			#ifdef OPENAL_ENV
				InitializeCriticalSection(&openALStreamLock[i]);
			#endif
			InitializeCriticalSection(&videoStreamLock[i]);
	#endif

	#ifdef USE_MUTEX
	#ifndef USE_ODBASE
		#ifdef OPENAL_ENV
			openALStreamLock[i] = CreateMutex(NULL, false, NULL);
		#endif
			videoStreamLock[i] = CreateMutex(NULL, false, NULL);
		#endif
	#endif
	}
}

/* Try to load the requested video name in the supplied client ID array slot.
 * If the requested video is in use return false
 * Else load the specified video and start a new thread that will read in frames.
 */
bool loadVideoFrames(const char * mediapath,double & last_frame, double & fps, int & inputwidth, int & inputheight, __int64 side)
{
	bool ret = false;
#if 0 // This was used to force the program to a single core to prevent incorrect pts timestamps
	// for being generated when the decoder used to use the packets pts (depreciated) instead of
	// the frames pts.
	DWORD_PTR threadAffMask = SetThreadAffinityMask(GetCurrentThread()/*timer->timerThread*/, core);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");

	// You need to set the process affinity on multi-core systems when loading an ffmpeg videos otherwise the pts timings will be off.

	DWORD_PTR p_processAffinityMask;
	DWORD_PTR systemAffinityMask;
	HANDLE process = GetCurrentProcess();

	if (GetProcessAffinityMask(process, &p_processAffinityMask, &systemAffinityMask) != 0)
	{
		//processAffinityMask = 1<<0;//(GetCurrentProcessorNumber() + 1);
		if(!SetProcessAffinityMask(process, core))
			printf("Couldn't set Process Affinity Mask\n\n");
	}
#endif

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif
	if(videoSources[side] == NULL)
	{
		if(videoFinished[side] == false)
		{
			videoSources[side] = NULL;
			char videoFileName[256];
			int g_DebugLevel;
			//ODVideoFileSource* videoSources;
			//videoFileName = mediapath;
			sprintf(videoFileName, mediapath);
			g_DebugLevel = 1;

			// Try to open a video file, using Ffmpeg.
			if (videoSources[side] == NULL)
			{
#ifdef USE_ODBASE
				// Check that the input video exists.
				if (!(ODBase::fileExists( videoFileName )))
#else

				int retval = PathFileExists(videoFileName);// Search for the presence of a file with a true result.
				if(retval == 0)
#endif
				{
					//throw ODBase::ErrorEvent("Could not find video file '%s'",videoFileName);
					MessageBox(NULL, "Could not find video file", "Error", NULL);	
					//printf(NULL, "Could not find video file", "Error", NULL);
				}
				else
				{

					// Try to open the input file.
					// Getting the video directly in RGBA format seems to result
					// in image artifacts.				
					videoSources[side] = new ODFfmpegSource(videoFileName, "", g_DebugLevel, videoFileName, ODImage::odi_BGRA/*ODImage::odi_YUV420*/);//odi_BGR//odi_YUV420
					if ((videoSources[side]->isSetup_))
					{
						inputwidth = videoSources[side]->width_;
						inputheight = videoSources[side]->height_;
						fps = videoSources[side]->fps_;
						last_frame = videoSources[side]->duration_;

						inputwidth = (inputwidth > 0) ? inputwidth : 0;
						inputheight = (inputheight > 0) ? inputheight : 0;
						fps = (fps > 0) ? fps : 0;
						last_frame = (last_frame > 0) ? last_frame : 0;
					/*
						QueryPerformanceFrequency( (LARGE_INTEGER *)&lpFrequency);
						lpFrequency /= 1000;
						strcpy(filepath[side],mediapath);

						firstframe[side] = 0;
						lastframe[side] = last_frame;
						currentframe[side] = firstframe[side];
						//currentframe[side]  = (last_frame - (int)(delay * fps) ) % last_frame; //delay
					*/
						threadArgs *pS = new threadArgs;
						pS->side = side;
						pS->vidSource = videoSources[side];
						pS->readerSemaphore = CreateSemaphore( NULL, 0 , 1, NULL);

						_beginthread(frameReader, 0, (void *) pS  );
						_beginthread(frameDecoder, 0, (void *) pS  );
						//printf("Waiting for frame reader thread to start...\n");
						//WaitForSingleObject(pS->readerSemaphore,INFINITE);

						// Output the video duration.
						// printf("%f\n", videoFileSource->duration_);

						ret = true;
					}
					else
					{
						char buf[256];
						sprintf(buf, "Could not read video file %s.", mediapath);
						MessageBox(NULL, buf, "Error", NULL);

						videoSources[side]->close();
						delete videoSources[side];
						videoSources[side] = NULL;
						videoFinished[side] = false;

						//throw ODBase::ErrorEvent("Could not read video file '%s'", videoFileName);
					}
				}
			}
		}
		else {
			printf("\n---Previous video[%d] not shutdown yet---\n", side);	
		}		
	}
	else
	{
		printf("\n---STREAM %d IS IN USE---\n", side);
		//closeVideoThread(side);
	}
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
#if 0
	if(!SetProcessAffinityMask(process, p_processAffinityMask))
			printf("Couldn't set Process Affinity Mask\n\n");

	SetThreadAffinityMask(GetCurrentThread()/*timer->timerThread*/, threadAffMask);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");
#endif

	return ret;
}
/* Notify the requested video thread to seek the specificed stream 'seekDuration' seconds.
 */
//void seekVideoThread(int side, double netClock, double incr)
void seekVideoThread(int side, double clock, double seekDuration)
//void seekVideoThread(int side, double seekDuration)
{
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif
	if(videoSources[side] != NULL)
	{	
		videoSources[side]->seek(clock, seekDuration);
		//videoSources[side]->seek(seekDuration);
	}
	else
	{
		printf("videoSources[%d] == NULL\n", side);
	}
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
}
/* Shutdown the specified video stream.
 */

void pauseFrameReader(int side)
{
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif
	if(videoSources[side] != NULL)
	{
		videoSources[side]->togglePause();
	}
	else
		printf("videoSources[%d] == NULL\n", side);
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
}

void closeVideoThread(int side)
{
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif
	if(videoSources[side] != NULL)
	{	
		printf("\n-Closing video stream: %d\n", side);
		#ifdef VIDEO_DECODING
			// Release the picture queue semaphore to allow the video thread to shut down
			videoSources[side]->stopQueuingFrames();
		#endif
		// Set the global boolean array slot to true
		videoFinished[side] = true;

		//ODFfmpegSource* localSource = videoSources[side];
		////videoSources[side]->close();
		//videoSources[side] = NULL;
		//localSource->close();
	}
	//else
		//printf("\n-Closing video stream failure (source not initialised): %d\n", side);

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
}
/* Start a new thread and begin reading/decoding the FFmpeg stream.
 */
void frameReader(void * parg)
{
	threadArgs* args = ((threadArgs*)(parg));
	int side = args->side;
	ODFfmpegSource* localVideoSource = args->vidSource;

	//ReleaseSemaphore(args->readerSemaphore, 1, NULL );
	printf("--Start FP thread %d.\n", side);

	//bool finished = false;
	//do
	//while(!videoFinished[side])
	while(true)
	{
		//videoStreamLock[side].grab();
		//if(videoFinished[side])
		//	finished = true;
		//videoStreamLock[side].release();
#if 0
			if(localVideoSource->quit())
				break;

		//printf("FrameReader Thread - side: %d\n", side);
			if(localVideoSource->queuesFull())
			{
				//printf("Frame reader thread sleeping...\n");
				Sleep(10);
				continue;
			}
#endif
			if (!(localVideoSource->advanceFrame()))
			{
				printf("Frame reader thread quitting...\n");
				//Sleep(100);
				break;
				//finished = true;
			}

		//Sleep(10);
		//DWORD core = GetCurrentProcessorNumber();
		//printf("$$$FrameReader Thread - Process Affinity: %d\n", core);
	} // while(!finished);

	
	WaitForSingleObject(args->readerSemaphore,INFINITE);
	CloseHandle(args->readerSemaphore);

#if 1
	//printf("-Getting ready to shutdown FP thread %d.\n", side);
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif

	//videoSources[side]->clearAudioPackets();
	//videoSources[side]->clearQueuedPackets();
	videoSources[side]->close();
	delete videoSources[side];
	videoSources[side] = NULL;
	videoFinished[side] = false;
	//videoFinished[side] = true;
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
	delete args;
#endif

	printf("-Stopped FP thread %d.\n", side);
}
#if 1
void frameDecoder(void * parg)
{
	threadArgs* args = ((threadArgs*)(parg));
	int side = args->side;
	ODFfmpegSource* localVideoSource = args->vidSource;

	//ReleaseSemaphore(args->readerSemaphore, 1, NULL );
	printf("--Start FD thread %d.\n", side);

	//bool finished = false;
	//do
	//while(!videoFinished[side])
	while(true)
	{
		//videoStreamLock[side].grab();
		//if(videoFinished[side])
		//	finished = true;
		//videoStreamLock[side].release();
		//printf("-FrameDecode Thread - side: %d\n", side);
			if(!(localVideoSource->decodeVideoFrame()))
			{
				printf("Frame decoder thread quitting...\n");
				//Sleep(100);
				break;
				//finished = true;
			}
			//printf("Frame decoder thread...\n");
			//Sleep(100);


			//else if(ret == -1) // Sleep thread if packet queue is empty.
				//Sleep(10);
		//DWORD core = GetCurrentProcessorNumber();
		//printf("$$$FrameReader Thread - Process Affinity: %d\n", core);
	} // while(!finished);


	//videoStreamLock[side].grab();
	//localVideoSource->stopQueuingFrames();
	//videoStreamLock[side].release();

	ReleaseSemaphore(args->readerSemaphore, 1, NULL );

#if 0
	//printf("-Getting ready to shutdown FP thread %d.\n", side);
	videoStreamLock[side].grab();
	//videoSources[side]->clearAudioPackets();
	//videoSources[side]->clearQueuedPackets();
	videoSources[side]->close();
	delete videoSources[side];
	videoSources[side] = NULL;
	videoFinished[side] = false;
	//videoFinished[side] = true;
	videoStreamLock[side].release();
	delete args;
#endif

	printf("-Stopped FD thread %d.\n", side);
}
#endif
#ifdef VIDEO_DECODING
#ifdef NETWORKED_AUDIO
/* Called by updateVideo().
 * Retrieves the next video frame for display and the calculates the appropriate delay (utilising the incoming UDP audio clock value) before making the 
 * callback again.
 */
double getNextVideoFrame(double netClock, double pauseLength, int side, char *plainData)
#else
double getNextVideoFrame(double openALAudioClock, double pauseLength, int side, char *plainData)
#endif
{
	double ret = -5;

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif

	if(videoSources[side] != NULL)
#ifdef NETWORKED_AUDIO
		ret = videoSources[side]->video_refresh_timer(netClock, pauseLength, plainData);
#else
		ret = videoSources[side]->video_refresh_timer(openALAudioClock, pauseLength, plainData);
#endif

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
	return ret;
}


//double getNextVideoFramePbo(double netClock, double pauseLength, int side, char *plainData)
double getNextVideoFramePbo(double netClock, double pauseLength, int side, char *plainData, double pboPTS)
{
	double ret = -5;
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif
	if(videoSources[side] != NULL)
		ret = videoSources[side]->video_refresh_timer_pbo(netClock, pauseLength, plainData, pboPTS);
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
	return ret;
}

double getVideoPts(int side)
{
	double ret = -5;
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif
	if(videoSources[side] != NULL)
		ret = videoSources[side]->video_pts();
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
	return ret;
}

double getNextVideoFrameNext(int side, char *plainData, int pboIndex, double& pboPTS)
{
	double ret = -5;
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
#endif
	if(videoSources[side] != NULL)
		ret = videoSources[side]->video_refresh_timer_next(plainData, pboIndex, pboPTS);
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&videoStreamLock[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#endif
	return ret;
}
#endif