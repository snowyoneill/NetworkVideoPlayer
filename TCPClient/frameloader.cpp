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
#include "constants.h"
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

#ifdef USE_ODBASE
ODBase::Lock openALStreamLock[MAXSTREAMS];//("audioStreamLock");
ODBase::Lock videoStreamLock[MAXSTREAMS];//("videoStreamLock");
#else
HANDLE openALStreamLock[MAXSTREAMS];// = CreateMutex(NULL, false, NULL);
HANDLE videoStreamLock[MAXSTREAMS];// = CreateMutex(NULL, false, NULL);
#endif

ODFfmpegSource* videoSources[MAXSTREAMS];
bool videoFinished[MAXSTREAMS] = {false};

//////////////////////////////////////////// OPENAL AUDIO //////////////////////////////////////////////
#if 0

bool audioSourceInit[MAXSTREAMS] = {false};

//#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio
//int getALBuffer(char* stream, int requested_buffer_size);

int initOpenAlDevice()
{
    /* Open and initialize a device with default settings */
	device = alcOpenDevice(NULL);
    if(!device)
    {
        fprintf(stderr, "Could not open a device!\n");
        return 1;
    }

    ctx = alcCreateContext(device, NULL);
    if(ctx == NULL || alcMakeContextCurrent(ctx) == ALC_FALSE)
    {
        if(ctx != NULL)
            alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not set a context!\n");
        return 1;
    }

	printf("\nAudio environment initialised\n");

	return 0;
}

int closeAudioDevice()
{
    alcMakeContextCurrent(NULL);
    alcDestroyContext(ctx);
    alcCloseDevice(device);

	printf("\nClosing audio context.\n");

	return 0;
}

int initAudioEnvir()
{
	initOpenAlDevice();

	_beginthread(decodeAndPlayAllAudioStreams, 0, 0 );

	return 1;
}

int initOpenAlBuf(int sourceNum)
{
    /* Generate the buffers and source */
    alGenBuffers(NUM_BUFFERS, buffers[sourceNum]);
    if(alGetError() != AL_NO_ERROR)
    {
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not create buffers...\n");
        return 1;
    }
    alGenSources(1, &source[sourceNum]);
    if(alGetError() != AL_NO_ERROR)
    {
        alDeleteBuffers(NUM_BUFFERS, buffers[sourceNum]);
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not create source...\n");
        return 1;
    }

    /* Set parameters so mono sources won't distance attenuate */
    alSourcei(source[sourceNum], AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcei(source[sourceNum], AL_ROLLOFF_FACTOR, 0);
    if(alGetError() != AL_NO_ERROR)
    {
        alDeleteSources(1, &source[sourceNum]);
        alDeleteBuffers(NUM_BUFFERS, buffers[sourceNum]);
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not set source parameters...\n");
        return 1;
    }

    data[sourceNum] = (ALbyte *)malloc(BUFFER_SIZE);
    if(data == NULL)
    {
        alDeleteSources(1, &source[sourceNum]);
        alDeleteBuffers(NUM_BUFFERS, buffers[sourceNum]);
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(device);
        fprintf(stderr, "Could not create temp buffer...\n");
        return 1;
    }

	printf("Audio buf initialised %d\n", sourceNum);

	return 0;
}

int closeAudioStream(int sourceNum)
{
    /* All files done. Delete the source and buffers, and close OpenAL */
	if(data[sourceNum] != NULL)
	{
		free(data[sourceNum]);
		data[sourceNum] = NULL;
		if(source[sourceNum] != NULL)
		{
			alDeleteSources(1, &source[sourceNum]);
			if(buffers[sourceNum] != NULL)
			{
				alDeleteBuffers(NUM_BUFFERS, buffers[sourceNum]);
				audioSourceInit[sourceNum] = false;
				printf("\nClosing audio stream: %d\n", sourceNum);
				return 0;
			}
		}
	}
	return 1;
}

int initOpenALSource(int sourceNum)
{
        if(type[sourceNum] == AL_UNSIGNED_BYTE)
        {
            if(channels[sourceNum] == AL_MONO) format[sourceNum] = AL_FORMAT_MONO8;
            else if(channels[sourceNum] == AL_STEREO) format[sourceNum] = AL_FORMAT_STEREO8;
            else if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
            {
                if(channels[sourceNum] == AL_QUAD) format[sourceNum] = alGetEnumValue("AL_FORMAT_QUAD8");
                else if(channels[sourceNum] == AL_5POINT1) format[sourceNum] = alGetEnumValue("AL_FORMAT_51CHN8");
                else if(channels[sourceNum] == AL_7POINT1) format[sourceNum] = alGetEnumValue("AL_FORMAT_71CHN8");
            }
        }
        else if(type[sourceNum] == AL_SHORT)
        {
            if(channels[sourceNum] == AL_MONO) format[sourceNum] = AL_FORMAT_MONO16;
            else if(channels[sourceNum] == AL_STEREO) format[sourceNum] = AL_FORMAT_STEREO16;
            else if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
            {
                if(channels[sourceNum] == AL_QUAD) format[sourceNum] = alGetEnumValue("AL_FORMAT_QUAD16");
                else if(channels[sourceNum] == AL_5POINT1) format[sourceNum] = alGetEnumValue("AL_FORMAT_51CHN16");
                else if(channels[sourceNum] == AL_7POINT1) format[sourceNum] = alGetEnumValue("AL_FORMAT_71CHN16");
            }
        }
        else if(type[sourceNum] == AL_FLOAT && alIsExtensionPresent("AL_EXT_FLOAT32"))
        {
            if(channels[sourceNum] == AL_MONO) format[sourceNum] = alGetEnumValue("AL_FORMAT_MONO_FLOAT32");
            else if(channels[sourceNum] == AL_STEREO) format[sourceNum] = alGetEnumValue("AL_FORMAT_STEREO_FLOAT32");
            else if(alIsExtensionPresent("AL_EXT_MCFORMATS"))
            {
                if(channels[sourceNum] == AL_QUAD) format[sourceNum] = alGetEnumValue("AL_FORMAT_QUAD32");
                else if(channels[sourceNum] == AL_5POINT1) format[sourceNum] = alGetEnumValue("AL_FORMAT_51CHN32");
                else if(channels[sourceNum] == AL_7POINT1) format[sourceNum] = alGetEnumValue("AL_FORMAT_71CHN32");
            }
        }
        else if(type[sourceNum] == AL_DOUBLE && alIsExtensionPresent("AL_EXT_DOUBLE"))
        {
            if(channels[sourceNum] == AL_MONO) format[sourceNum] = alGetEnumValue("AL_FORMAT_MONO_DOUBLE");
            else if(channels[sourceNum] == AL_STEREO) format[sourceNum] = alGetEnumValue("AL_FORMAT_STEREO_DOUBLE");
        }

        if(format[sourceNum] == 0 || format[sourceNum] == -1)
        {
            //closeAVFile(file);
            fprintf(stderr, "Unhandled format (%s, %s) for %s", ChannelsName(channels[sourceNum]), TypeName(type[sourceNum]), videoSources[sourceNum]->name_.c_str());
			return 1;
            //continue;
        }

        /* If the format of the last file matches the current one, we can skip
         * the initial load and let the processing loop take over (gap-less
         * playback!) */
        //count[sourceNum] = 1;
        //if(format[sourceNum] == old_format && rate[sourceNum] == old_rate)
        //{
        //    /* When skipping the initial load of a file (because the previous
        //     * one is using the same exact format), just remove the length of
        //     * the previous file from the base. This is so the timing will be
        //     * from the beginning of this file, which won't start playing until
        //     * the next buffer to get queued does */
        //    basetime[sourceNum] -= filetime[sourceNum];
        //    filetime[sourceNum] = 0;
        //}
        //else
        //{
            int j;

            /* Wait for the last song to finish playing */
            //do {
            //    Sleep(10);
            //    alGetSourcei(source, AL_SOURCE_STATE, &state);
            //} while(alGetError() == AL_NO_ERROR && state == AL_PLAYING);
            /* Rewind the source position and clear the buffer queue */
            alSourceRewind(source[sourceNum]);
            alSourcei(source[sourceNum], AL_BUFFER, 0);
            /* Reset old variables */
            basetime[sourceNum] = 0;
            filetime[sourceNum] = 0;
            old_format = format[sourceNum];
            old_rate = rate[sourceNum];

			//count = audio_buf_size;
			//data = (ALbyte*)audio_buf;
			//data = (ALbyte*)malloc(sizeof(char)*audio_buf_size);
			//memset(data, 500, audio_buf_size);
			//memcpy(data, audio_buf, audio_buf_size);

            /* Fill and queue the buffers */
            for(j = 0;j < NUM_BUFFERS;j++)
            {
                ALint size, numchans, numbits;

                /* Make sure we get some data to give to the buffer */
                //count = getAVAudioData(stream, data, BUFFER_SIZE);
				do
				{
					count[sourceNum] = videoSources[sourceNum]->getALBuffer((char*) data[sourceNum], BUFFER_SIZE);
					//count = getALBuffer((char*)data, BUFFER_SIZE);
					//count = videoSources[0]->getAVAudioData((char*) data, BUFFER_SIZE);
					//printf("Waiting to fill buffer[%d]\n", j);
					//Sleep(10);
				} while(count[sourceNum] == 0);
				

                if(count[sourceNum] <= 0)
					//break;
					continue;

                /* Buffer the data with OpenAL and queue the buffer onto the
                 * source */
                alBufferData(buffers[sourceNum][j], format[sourceNum], data[sourceNum], count[sourceNum], rate[sourceNum]);
                alSourceQueueBuffers(source[sourceNum], 1, &buffers[sourceNum][j]);

                /* For each successful buffer queued, increment the filetime */
                alGetBufferi(buffers[sourceNum][j], AL_SIZE, &size);
                alGetBufferi(buffers[sourceNum][j], AL_CHANNELS, &numchans);
                alGetBufferi(buffers[sourceNum][j], AL_BITS, &numbits);
                filetime[sourceNum] += size / numchans * 8 / numbits;
            }

            /* Now start playback! */
            alSourcePlay(source[sourceNum]);
            if(alGetError() != AL_NO_ERROR)
            {
                //closeAVFile(file);
                fprintf(stderr, "Error starting playback...\n");
                //continue;
            }
        //}
		
        fprintf(stderr, "Playing %s (%s, %s, %dhz)\n", videoSources[sourceNum]->name_.c_str(), TypeName(type[sourceNum]), ChannelsName(channels[sourceNum]), rate[sourceNum]);

    return 0;
}

int refillAudioBuffers(int sourceNum)
{
	//if(count > 0)
	//{
		/* Check if any buffers on the source are finished playing */
		ALint processed = 0;
		alGetSourcei(source[sourceNum], AL_BUFFERS_PROCESSED, &processed);
		if(processed == 0)
		{
			/* All buffers are full. Check if the source is still playing.
			* If not, restart it, otherwise, print the time and rest */
			alGetSourcei(source[sourceNum], AL_SOURCE_STATE, &state[sourceNum]);
			if(alGetError() != AL_NO_ERROR)
			{
				fprintf(stderr, "\nError checking source state...\n");
				//break;
				return 1;
			}
			if(state[sourceNum] == AL_INITIAL)
			{
				printf("Buffer not initialised...\n");
				return 1;
			}
			if(state[sourceNum] != AL_PLAYING)
			{
				alSourcePlay(source[sourceNum]);
				if(alGetError() != AL_NO_ERROR)
				{
					//closeAVFile(file);
					fprintf(stderr, "\nError restarting playback...\n");
					//break;
					return 1;
				}
			}

			else
			{
					//int64_t curtime[2];
					//for (int i=0; i<2; i++)
					//{
					//	curtime[i] = 0;
					//	if(basetime[i] >= 0)
					//	{
					//		ALint offset = 0;
					//		alGetSourcei(source[i], AL_SAMPLE_OFFSET, &offset);
					//		curtime[i] = basetime[i] + offset;
					//	}
					//}
					//fprintf(stderr, "\rTime[Source = %d]: %ld:%05.02f\tTime[Source = %d]: %ld:%05.02f", 0, (long)(curtime[0]/rate[0]/60),(float)(curtime[0]%(rate[0]*60))/(float)rate[0],
					//	1, (long)(curtime[1]/rate[1]/60),(float)(curtime[1]%(rate[1]*60))/(float)rate[1]);

					int64_t curtime = 0;
					if(basetime[sourceNum] >= 0)
					{
						ALint offset = 0;
						alGetSourcei(source[sourceNum], AL_SAMPLE_OFFSET, &offset);
						curtime = basetime[sourceNum] + offset;
					}
					//fprintf(stderr, "\rTime[Source = %d]: %ld:%05.02f", sourceNum, (long)(curtime/rate[sourceNum]/60),(float)(curtime%(rate[sourceNum]*60))/(float)rate[sourceNum]);
				Sleep(10);
			}
			//continue;
			return count[sourceNum];
		}

		//printf("- Refilling buffers\n");
		count[sourceNum] = videoSources[sourceNum]->getALBuffer((char*)data[sourceNum], BUFFER_SIZE);
		//count = videoSources[0]->getAVAudioData((char*)data, BUFFER_SIZE);
		//count = getALBuffer((char*)data, BUFFER_SIZE);
		/* Read the next chunk of data and refill the oldest buffer */
		//count = getAVAudioData(stream, data, BUFFER_SIZE);
		if(count[sourceNum] > 0)
		{
			ALuint buf = 0;
			alSourceUnqueueBuffers(source[sourceNum], 1, &buf);
			if(buf != 0)
			{
				ALint size, numchans, numbits;

				/* For each successfully unqueued buffer, increment the
				* base time. */
				alGetBufferi(buf, AL_SIZE, &size);
				alGetBufferi(buf, AL_CHANNELS, &numchans);
				alGetBufferi(buf, AL_BITS, &numbits);
				basetime[sourceNum] += size / numchans * 8 / numbits;

				alBufferData(buf, format[sourceNum], data[sourceNum], count[sourceNum], rate[sourceNum]);
				alSourceQueueBuffers(source[sourceNum], 1, &buf);

				alGetBufferi(buf, AL_SIZE, &size);
				alGetBufferi(buf, AL_CHANNELS, &numchans);
				alGetBufferi(buf, AL_BITS, &numbits);
				filetime[sourceNum] += size / numchans * 8 / numbits;
			}
			if(alGetError() != AL_NO_ERROR)
			{
				fprintf(stderr, " !!! Error buffering data !!!\n");
				//break;
				return 1;
			}
		}
  
    return count[sourceNum];
}

//int getALBuffer(char* stream, int requested_buffer_size)
//{
//	//memset(stream, 1500, AVCODEC_MAX_AUDIO_FRAME_SIZE);
//
//	//int len = AVCODEC_MAX_AUDIO_FRAME_SIZE;
//	int len = requested_buffer_size;
//	int len1, audio_size;
//
//	//char audio_buf[AVCODEC_MAX_AUDIO_FRAME_SIZE];
//	char *audio_buf = (char*)malloc(requested_buffer_size);
//	int audio_buf_size = 0;
//	int audio_buf_index = 0;
//
//	while(len > 0)
//	{
//		if(audio_buf_index >= audio_buf_size)
//		{
//			/* We have already sent all our data; get more */
//			audio_size = videoSources[0]->audio_decode_frame(audio_buf, requested_buffer_size);
//			if(audio_size < 0)
//			{
//				/* If error, output silence */
//				//audio_buf_size = 1024; // arbitrary?
//				//memset(audio_buf, 0, audio_buf_size);
//				//return len;
//				if(audio_size < 0)
//					printf(":No packets - len %d - audio_size %d\n", len, audio_size);
//				//return 0;
//				break;
//			}
//			else
//			{
//				audio_buf_size = audio_size;
//			}
//			audio_buf_index = 0;
//		}
//		len1 = audio_buf_size - audio_buf_index;
//		if(len1 > len)
//			len1 = len;
//		memcpy(stream, audio_buf + audio_buf_index, len1);
//		//if(len != 0)
//		stream += len1;
//		len -= len1;
//
//		if(len1 < audio_buf_size)
//			memmove(audio_buf, &audio_buf[len1], audio_buf_size - len1);
//
//		audio_buf_index += len1;
//	}
//
//	return requested_buffer_size - len;
//
//	//stream -= AVCODEC_MAX_AUDIO_FRAME_SIZE;
//	//return AVCODEC_MAX_AUDIO_FRAME_SIZE;
//}

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
        else if(*channels == AV_CH_LAYOUT_5POINT1)
            *channels = AL_5POINT1;
        else if(*channels == AV_CH_LAYOUT_7POINT1)
            *channels = AL_7POINT1;
        else
            return 1;
    }

    return 0;
}

bool initAudioSource(int side)
{
	if(audioSourceInit[side] == false)
	{
		videoSources[side]->getAVAudioInfo(&rate[side], &channels[side], &type[side]);
		getALAudioInfo(&rate[side], &channels[side], &type[side]);
		initOpenAlBuf(side);
		printf("Initialising audio source [%d]\n", side);
		if(initOpenALSource(side) != 0){
			printf("Error trying to initialise source[%d]\n", side);
			return false;
		}
		else
			audioSourceInit[side] = true;
	}

	return true;
}

void decodeAndPlayAllAudioStreams(void* dummy)
{
	//alSourcef(source[0], AL_GAIN, 0.1f);
	//alSourcef(source[1], AL_GAIN, 0.9f);

	for ( ; ; )
	{
		for (int i=0; i<MAXSTREAMS; i++)
		{
			videoStreamLock.grab();
			if(videoSources[i] != NULL)
				if(videoFinished[i]==false)
				{
					if(initAudioSource(i))
					{
						int ret = refillAudioBuffers(i);
						if(ret == 1) {
							MessageBox(NULL, "Audio Buffer Error", "Could not queue audio buffers", MB_OK | MB_ICONEXCLAMATION);
							exit(-1);
						}
						else if(ret == 0 && videoSources[i]->atVideoEnd_)
						{
							closeVideoThread(i);
							closeAudioStream(i);
						}
					}
				}
			videoStreamLock.release();
		}
	}

	printf("\nClosing audio decoder/player thread.\n");
	closeAudioDevice();
	//return 1;
}

#endif

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
		
#ifdef USE_ODBASE
		videoStreamLock[side].grab();
#else
		WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
		if(videoSources[side] != NULL)
		{
			if(videoSources[side]->getAVAudioInfo(&rate, &channels, &type) != 0)
			{
#ifdef USE_ODBASE
				videoStreamLock[side].release();
#else
				ReleaseMutex(videoStreamLock[side]);
#endif
				printf("Error getting ffmpeg audio info - could not initialise audio source [%d]\n", side);
				return false;
			}
		}
#ifdef USE_ODBASE
		videoStreamLock[side].release();
#else
		ReleaseMutex(videoStreamLock[side]);
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

#ifdef USE_ODBASE
		videoStreamLock[side].grab();
#else
		WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
		if(videoSources[side] == NULL)
		{
#ifdef USE_ODBASE
			videoStreamLock[side].release();
#else
			ReleaseMutex(videoStreamLock[side]);
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
#ifdef USE_ODBASE
		videoStreamLock[side].release();
#else
		ReleaseMutex(videoStreamLock[side]);
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
#ifdef USE_ODBASE
		openALStreamLock[side].grab();
#else
		WaitForSingleObject(openALStreamLock[side], INFINITE);
#endif
		//while(openALEnv->isSourceStillPlaying(side)) { openALStreamLock[side].release(); Sleep(10); openALStreamLock[side].grab(); }
		//if(!openALEnv->isSourceStillPlaying(side))
		{
			if(openALEnv->closeAudioStream(side)==0)
			{
#ifdef USE_ODBASE
				openALStreamLock[side].release();
#else
				ReleaseMutex(openALStreamLock[side]);
#endif
				audioSourceInit[side] = false;
				return true;
			}
			else
				printf("\nCouldn't close audio stream\n");
		}
		//else
		//	printf("\nAudio buffer still playing.\n");
#ifdef USE_ODBASE
		openALStreamLock[side].release();
#else
		ReleaseMutex(openALStreamLock[side]);
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
#ifdef USE_ODBASE
			openALStreamLock[i].grab();
#else
			WaitForSingleObject(openALStreamLock[i], INFINITE);
#endif

			//videoStreamLock[i].grab();
			if(videoSources[i] != NULL)
				if(videoFinished[i]==false)
				{
					count = -1;
					totalActiveAudioStreams++;
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
#ifdef USE_ODBASE
									videoStreamLock[i].grab();
#else
									WaitForSingleObject(videoStreamLock[i], INFINITE);
#endif
									if(videoSources[i] != NULL)
									{
										count = videoSources[i]->getAudioBuffer(data, g_bufferSize);/*BUFFER_SIZE*/
										//count = videoSources[i]->getAVAudioData(data, g_bufferSize);
										
									}
#ifdef USE_ODBASE
									videoStreamLock[i].release();
#else
									ReleaseMutex(videoStreamLock[i]);
#endif
									if(count >= 0)
										ret = openALEnv->refillAudioBuffers(i, count, data);
								}
//#else
								else
								{
									//printf("Refill buffers: %d\n", i);
									char* data = (char*) malloc(bufSize[i]);
#ifdef USE_ODBASE
									videoStreamLock[i].grab();
#else
									WaitForSingleObject(videoStreamLock[i], INFINITE);
#endif
									if(videoSources[i] != NULL)
									{
										//count = videoSources[i]->getAudioBuffer(data, bufSize[i]);
										count = videoSources[i]->getAVAudioData(data, bufSize[i]);
									}
#ifdef USE_ODBASE
									videoStreamLock[i].release();
#else
									ReleaseMutex(videoStreamLock[i]);
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
#ifdef USE_ODBASE
									videoStreamLock[i].grab();
#else
									WaitForSingleObject(videoStreamLock[i], INFINITE);
#endif
									if(videoSources[i] != NULL)
										if(videoSources[i]->atVideoEnd_)
											atEnd = true;
#ifdef USE_ODBASE
									videoStreamLock[i].release();
#else
									ReleaseMutex(videoStreamLock[i]);
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
						stopAudioThread = true;
					}
				}
			//videoStreamLock[i].release();
#ifdef USE_ODBASE
			openALStreamLock[i].release();
#else
			ReleaseMutex(openALStreamLock[i]);
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
	printf("\n+++Closing audio decoder/player thread.\n");
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
/* Register FFmpeg
 */
void registerFFmpeg()
{
	av_register_all();

#ifndef USE_ODBASE
	for(int i=0; i<MAXSTREAMS; i++)
	{
		openALStreamLock[i] = CreateMutex(NULL, false, NULL);
		videoStreamLock[i] = CreateMutex(NULL, false, NULL);		
	}
#endif

}

/* Try to load the requested video name in the supplied client ID array slot.
 * If the requested video is in use return false
 * Else load the specified video and start a new thread that will read in frames.
 */
bool loadVideoFrames(const char * mediapath,double & last_frame, double & fps, int & inputwidth, int & inputheight, __int64 side)
{
	bool ret = false;
#if 1 // This was used to force the program to a single core to prevent incorrect pts timestamps
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

#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
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
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
#if 1
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
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
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
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
}
/* Shutdown the specified video stream.
 */
void closeVideoThread(int side)
{
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
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
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
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

	bool finished = false;
	//do
	//while(!videoFinished[side])
	while(!finished)
	{
		//videoStreamLock[side].grab();
		//if(videoFinished[side])
		//	finished = true;
		//videoStreamLock[side].release();

		//printf("FrameReader Thread - side: %d\n", side);
			if(localVideoSource->queuesFull())
			{
				//printf("Frame reader thread sleeping...\n");
				Sleep(10);
			}
			
			if (localVideoSource->advanceFrame() == false)
			{
				printf("Frame reader thread quitting...\n");
				//Sleep(100);
				//break;
				finished = true;
			}
			//Sleep(10);
		//DWORD core = GetCurrentProcessorNumber();
		//printf("$$$FrameReader Thread - Process Affinity: %d\n", core);
	} // while(!finished);

	WaitForSingleObject(args->readerSemaphore,INFINITE);

#if 1
	//printf("-Getting ready to shutdown FP thread %d.\n", side);
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif

	//videoSources[side]->clearAudioPackets();
	//videoSources[side]->clearQueuedPackets();
	videoSources[side]->close();
	delete videoSources[side];
	videoSources[side] = NULL;
	videoFinished[side] = false;
	//videoFinished[side] = true;
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
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

	bool finished = false;
	//do
	//while(!videoFinished[side])
	while(!finished)
	{
		//videoStreamLock[side].grab();
		//if(videoFinished[side])
		//	finished = true;
		//videoStreamLock[side].release();
		//printf("-FrameDecode Thread - side: %d\n", side);
			int ret = localVideoSource->decodeVideoFrame();
			if (ret == false)
			//if(localVideoSource->decodeVideoFrame() == false)
			{
				printf("Frame decoder thread quitting...\n");
				//Sleep(100);
				//break;
				finished = true;
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
{
	double ret = -5;
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
	if(videoSources[side] != NULL)
		ret = videoSources[side]->video_refresh_timer(netClock, pauseLength, plainData);
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
	return ret;
}
#else
double getNextVideoFrame(double openALAudioClock, double pauseLength, int side, char *plainData)
{
	double ret = -5;
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
	if(videoSources[side] != NULL)
		ret = videoSources[side]->video_refresh_timer(openALAudioClock, pauseLength, plainData);
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
	return ret;
}
#endif

//double getNextVideoFramePbo(double netClock, double pauseLength, int side, char *plainData)
double getNextVideoFramePbo(double netClock, double pauseLength, int side, char *plainData, double pboPTS)
{
	double ret = -5;
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
	if(videoSources[side] != NULL)
		ret = videoSources[side]->video_refresh_timer_pbo(netClock, pauseLength, plainData, pboPTS);
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
	return ret;
}

double getVideoPts(int side)
{
	double ret = -5;
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
	if(videoSources[side] != NULL)
		ret = videoSources[side]->video_pts();
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
	return ret;
}

double getNextVideoFrameNext(int side, char *plainData, int pboIndex, double& pboPTS)
{
	double ret = -5;
#ifdef USE_ODBASE
	videoStreamLock[side].grab();
#else
	WaitForSingleObject(videoStreamLock[side], INFINITE);
#endif
	if(videoSources[side] != NULL)
		ret = videoSources[side]->video_refresh_timer_next(plainData, pboIndex, pboPTS);
#ifdef USE_ODBASE
	videoStreamLock[side].release();
#else
	ReleaseMutex(videoStreamLock[side]);
#endif
	return ret;
}
#endif