/*
 * This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details.
 */

/* ChangeLog:
 * 1 - Initial program
 * 2 - Changed getAVAudioData to not always grab another packet before decoding
 *     to prevent buffering more compressed data than needed
 * 3 - Update to use avcodec_decode_audio3 and fix for decoders that need
 *     aligned output pointers
 * 4 - Fixed bits/channels format assumption
 * 5 - Improve time handling
 * 6 - Remove use of ALUT
 */
#include "audio.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef _WIN32
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
#else
#include <windows.h>
#endif

//#include <AL/al.h>
//#include <AL/alc.h>
//#include <AL/alext.h>

/* Some helper functions to get the name from the channel and type enums. */
static const char *ChannelsName(ALenum chans)
{
	switch(chans)
	{
	case AL_MONO: return "Mono";
	case AL_STEREO: return "Stereo";
	case AL_REAR: return "Rear";
	case AL_QUAD: return "Quadraphonic";
	case AL_5POINT1: return "5.1 Surround";
	case AL_6POINT1: return "6.1 Surround";
	case AL_7POINT1: return "7.1 Surround";
	}
	return "Unknown";
}

static const char *TypeName(ALenum type)
{
    switch(type)
    {
    case AL_BYTE: return "S8";
    case AL_UNSIGNED_BYTE: return "U8";
    case AL_SHORT: return "S16";
    case AL_UNSIGNED_SHORT: return "U16";
    case AL_INT: return "S32";
    case AL_UNSIGNED_INT: return "U32";
    case AL_FLOAT: return "Float32";
    case AL_DOUBLE: return "Float64";
    }
    return "Unknown";
}

////////////////////////////////////////////// OpenAL ////////////////////////////////////////////////

ALCdevice * AudioEnvironment::device = NULL;
ALCcontext * AudioEnvironment::ctx = NULL;

AudioEnvironment::AudioEnvironment(bool preload, int bufferSize)
{
	//if(initOpenAlDevice())
	//	printf("AUDIO DEVICE NOT DETECTED - AUDIO CLOCK WILL NOT RETURN CORRECT FRAME DELAYS!\n");

	m_preload = preload;
	m_bufferSize = bufferSize;

	for(int i=0; i<MAXSTREAMS; i++)
	{
		state[i] = -1;
		streamPaused[i] = false;
	}
}
AudioEnvironment::~AudioEnvironment() { closeAudioDevice(); }

int AudioEnvironment::initOpenAlDevice()
{
	const ALCchar *devices; 
    if(alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT") != AL_FALSE)
        devices = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
    else
        devices = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
	//devices = alcGetString(NULL, ALC_DEVICE_SPECIFIER);

	printf("Devices: %s\n", devices);
#if 1
	int numDevices = 0;
	int lastDeviceIndex = 0;
	while(true)
	{
		if(devices[lastDeviceIndex] == '\0')
		{
			numDevices++;
			if(devices[lastDeviceIndex+1] == '\0')
				break;
		}
		lastDeviceIndex++;
	}
	printf("NUMBER OF SOUND DEVICES: %d\n", numDevices );
	int found = 0, start = 0;
	for(int i=0; i<numDevices; i++)
	{
		if((start + found) != lastDeviceIndex-1)
		{
			found = (int)strlen(devices+((sizeof(char))*start));
			printf("DEVICE %d: %s\n", i, devices+((sizeof(char))*start));
			start += found + 1;
		}
	}
#endif	
    /* Open and initialize a device with default settings */
	AudioEnvironment::device = alcOpenDevice(NULL);
	//AudioEnvironment::device = alcOpenDevice("Speakers (High Definition Audio via WaveOut");
	//AudioEnvironment::device = alcOpenDevice("Speakers (High Definition Audio Device) via DirectSound");

    if(!device)
    {
        fprintf(stderr, "Could not open a device!\n");
        return 1;
    }

	AudioEnvironment::ctx = alcCreateContext(device, NULL);
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

int AudioEnvironment::closeAudioDevice()
{
    alcMakeContextCurrent(NULL);
    alcDestroyContext(AudioEnvironment::ctx);
    alcCloseDevice(AudioEnvironment::device);

	printf("\nClosing audio context.\n");

	return 0;
}

ALsizei FramesToBytes(ALsizei size, ALenum channels, ALenum type)
{
    switch(channels)
    {
		case AL_MONO:    size *= 1; break;
		case AL_STEREO:  size *= 2; break;
		case AL_REAR:    size *= 2; break;
		case AL_QUAD:    size *= 4; break;
		case AL_5POINT1: size *= 6; break;
		case AL_6POINT1: size *= 7; break;
		case AL_7POINT1: size *= 8; break;
    }

    switch(type)
    {
		case AL_BYTE:           size *= sizeof(ALbyte); break;
		case AL_UNSIGNED_BYTE:  size *= sizeof(ALubyte); break;
		case AL_SHORT:          size *= sizeof(ALshort); break;
		case AL_UNSIGNED_SHORT: size *= sizeof(ALushort); break;
		case AL_INT:            size *= sizeof(ALint); break;
		case AL_UNSIGNED_INT:   size *= sizeof(ALuint); break;
		case AL_FLOAT:          size *= sizeof(ALfloat); break;
		case AL_DOUBLE:         size *= sizeof(ALdouble); break;
    }

    return size;
}

ALsizei BytesToFrames(ALsizei size, ALenum channels, ALenum type)
{
    return size / FramesToBytes(1, channels, type);
}

/* Retrieves the elapsed time for the specific source.*/
double AudioEnvironment::getElapsedALTime(int sourceNum)
{
	ALint offset = 0;
	alGetSourcei(source[sourceNum], AL_SAMPLE_OFFSET, &offset);
	int64_t curtime = basetime[sourceNum] + offset;
	double output = curtime/(double)rate[sourceNum];
	return output;
}

//#ifdef PRELOAD
/* Preloads all buffers and sources. This helps prevent allocation bugs occurring when alGenSources is constanstly called on the fly when a new source is requested.
 * (if alGenSources is called more times than the maximum number of buffers then an AL_INVALID_NAME error is generated) */
int AudioEnvironment::preLoadSources(int sourceNum)
{
		//for(int j=0; j<MAXSTREAMS; j++)
		{
			if(state[sourceNum] == -1)
			{
				if(initOpenAlBuf(sourceNum) != 0)
					return 1;
			}
			else
				printf("Source already loaded %d\n", sourceNum);
		}
		return 0;
}
//#endif
/* Sets the volume for a specific source. If level is greater than 1 or less than 0 then the value is capped. */
int AudioEnvironment::setVolume(int streamID, float level)
{
	level = (level < 0) ? 0 : level;
	level = (level > 1) ? 1 : level;
	//for(int j=0; j<MAXSTREAMS; j++)
		//alSourcef(source[j], AL_GAIN, level);
	alSourcef(source[streamID], AL_GAIN, level);
	return 0;
}
/* Pauses the specified audio source. */
int AudioEnvironment::pauseAudioStream(int streamID)
{
	if(streamPaused[streamID])
	{
		alSourcePlay(source[streamID]);
		streamPaused[streamID] = false;
	}
	else
	{
		alSourcePause(source[streamID]);
		streamPaused[streamID] = true;
		printf("Pausing OpenAL source - ClientID: %d, Stream: %d.\n", 0, streamID);
	}

	ALenum err = alGetError();
	if(err != AL_NO_ERROR)
	{
		printf("---OpenAL Error - Setting volume: %s (0x%x).\n", alGetString(err), err);
		return 1;
	}

	return 0;
}
/* Check if the specified audio source is paused. */
int AudioEnvironment::isPaused(int streamID)
{
	if(streamPaused[streamID])
		return 1;
	else
		return 0;
}
//#ifdef PRELOAD
int AudioEnvironment::initOpenAlBuf(int sourceNum)
{
	return initOpenAlBuf(sourceNum, -1);
}
//#else
int AudioEnvironment::initOpenAlBuf(int sourceNum, int bufSize)
//#endif
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
//#ifdef PRELOAD
	if(m_preload)
		//data[sourceNum] = (ALbyte *)malloc(BUFFER_SIZE);
		data[sourceNum] = (ALbyte *)malloc(m_bufferSize);
//#else
	else
		data[sourceNum] = (ALbyte *)malloc(bufSize);
//#endif
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

int AudioEnvironment::closeAudioStream(int sourceNum)
{
	alSourceStop(source[sourceNum]);
    alSourcei(source[sourceNum], AL_BUFFER, 0);
    /* Reset old variables */
    basetime[sourceNum] = 0;
    filetime[sourceNum] = 0;
	if(m_preload)
	{
		return 0;
	}

//#ifndef PRELOAD
	if(!m_preload)
	{
		/* All files done. Delete the source and buffers, and close OpenAL */
		if(data[sourceNum] != NULL)
		{
			free(data[sourceNum]);
			data[sourceNum] = NULL;
			if(source[sourceNum] != NULL)
			{
				alDeleteSources(1, &source[sourceNum]);
				if(alGetError() != AL_NO_ERROR)
				{
					printf("Couldn't delete source.\n");
				}
				if(buffers[sourceNum] != NULL)
				{
					alDeleteBuffers(NUM_BUFFERS, buffers[sourceNum]);
					printf("\nClosing audio stream: %d - Deleting buffers.\n", sourceNum);
					return 0;
				}
			}
		}
	}
//#endif
	return 1;
}
//#ifdef PRELOAD
int AudioEnvironment::initAudioBuffers(int sourceNum, int countArray[NUM_BUFFERS], char* dataArray[NUM_BUFFERS])
{
	return initAudioBuffers(sourceNum, countArray, dataArray, -1);
}
//#else
int AudioEnvironment::initAudioBuffers(int sourceNum, int countArray[NUM_BUFFERS], char* dataArray[NUM_BUFFERS], int bufSize)
//#endif
{
		//count = audio_buf_size;
		//data = (ALbyte*)audio_buf;
		//data = (ALbyte*)malloc(sizeof(char)*audio_buf_size);
		//memset(data, 500, audio_buf_size);
		//memcpy(data, audio_buf, audio_buf_size);

		/* Fill and queue the buffers */
		for(int j = 0;j < NUM_BUFFERS;j++)
		{
			ALint size, numchans, numbits;

			count[sourceNum] = countArray[j];
			//data[sourceNum] = (ALbyte*)dataArray[j];
			//memcpy(count[sourceNum], countArray[j], BUFFER_SIZE);
//#ifdef PRELOAD
			if(m_preload)
				//memcpy(data[sourceNum], (char*)dataArray[j], BUFFER_SIZE);
				memcpy(data[sourceNum], (char*)dataArray[j], m_bufferSize);
//#else
			else
				memcpy(data[sourceNum], (char*)dataArray[j], bufSize);
//#endif
			/* Make sure we get some data to give to the buffer */
			//count = getAVAudioData(stream, data, BUFFER_SIZE);
			//do
			//{
				//count[sourceNum] = videoSources[sourceNum]->getALBuffer((char*) data[sourceNum], BUFFER_SIZE);
				//count = getALBuffer((char*)data, BUFFER_SIZE);
				//count = videoSources[0]->getAVAudioData((char*) data, BUFFER_SIZE);
				//printf("Waiting to fill buffer[%d]\n", j);
				//Sleep(10);
			//} while(count[sourceNum] == 0);
			
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

			if(size == 0 || numchans == 0 || numbits == 0)
			{
				printf("OpenAL couldn't read source [%d] - Size: %d - NumChannels: %d - NumBits: %d \n", sourceNum, size, numchans, numbits);
				return 1;
			}
			printf("Source [%d] - Size: %d - NumChannels: %d - NumBits: %d \n", sourceNum, size, numchans, numbits);
			filetime[sourceNum] += size / numchans * 8 / numbits;
		}

		/* Now start playback! */
		alSourcePlay(source[sourceNum]);
		if(alGetError() != AL_NO_ERROR)
		{
			//closeAVFile(file);
			fprintf(stderr, "Error starting playback...\n");
			return 1;
			//continue;
		}
	//fprintf(stderr, "Playing %s (%s, %s, %dhz)\n", videoSources[sourceNum]->name_.c_str(), TypeName(type[sourceNum]), ChannelsName(channels[sourceNum]), rate[sourceNum]);

    return 0;
}

int AudioEnvironment::checkAudioBuffers(int sourceNum)
{
	if(count[sourceNum] > 0)
	{
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
			if(state[sourceNum] == -1)
			{
				printf("Audio playback state not initialised...\n");
				return 0;
			}				
			if(state[sourceNum] == AL_INITIAL)
			{
				printf("Buffer not initialised...\n");
				return 1;
			}
			if(state[sourceNum] != AL_PLAYING)
			//if(state[sourceNum] == AL_STOPPED)
			{

				alSourcePlay(source[sourceNum]);
				if(alGetError() != AL_NO_ERROR)
				{
					//closeAVFile(file);
					fprintf(stderr, "\nError restarting playback...\n");
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
			return 1;
		}
	}

	return 0;
}

//#ifdef PRELOAD
int AudioEnvironment::refillAudioBuffers(int sourceNum, int count_, char* data_)
{
	return refillAudioBuffers(sourceNum, count_, data_, -1);
}
//#else
int AudioEnvironment::refillAudioBuffers(int sourceNum, int count_, char* data_, int bufSize)
//#endif
{
	count[sourceNum] = count_;
//#ifdef PRELOAD
	if(m_preload)
	//memcpy(data[sourceNum], (char*)data_, BUFFER_SIZE);
		memcpy(data[sourceNum], (char*)data_, m_bufferSize);
//#else
	else
		memcpy(data[sourceNum], (char*)data_, bufSize);
//#endif

	//printf("- Refilling buffers\n");
	//count = videoSources[0]->getAVAudioData((char*)data, BUFFER_SIZE);
	//count[sourceNum] = videoSources[sourceNum]->getALBuffer((char*)data[sourceNum], BUFFER_SIZE);
	//count = getALBuffer((char*)data, BUFFER_SIZE);
	/* Read the next chunk of data and refill the oldest buffer */
	//count = getAVAudioData(stream, data, BUFFER_SIZE);
	if(count[sourceNum] > 0)
	{
		ALuint buf = 0;
		alSourceUnqueueBuffers(source[sourceNum], 1, &buf);

		if(alGetError() != AL_NO_ERROR)
		{
			fprintf(stderr, " !!! Error buffering data !!!\n");
			//break;
			return 1;
		}


		if(buf != 0)
		{
			ALint size, numchans, numbits;

			/* For each successfully unqueued buffer, increment the
			* base time. */
			alGetBufferi(buf, AL_SIZE, &size);
			alGetBufferi(buf, AL_CHANNELS, &numchans);
			alGetBufferi(buf, AL_BITS, &numbits);
			basetime[sourceNum] += size / numchans * 8 / numbits;
//#ifdef PRELOAD
			if(m_preload)
				alBufferData(buf, format[sourceNum], data[sourceNum], count[sourceNum], rate[sourceNum]);
//#else
			else
				alBufferData(buf, format[sourceNum], data[sourceNum], bufSize, rate[sourceNum]);
//#endif
			alSourceQueueBuffers(source[sourceNum], 1, &buf);

			alGetBufferi(buf, AL_SIZE, &size);
			alGetBufferi(buf, AL_CHANNELS, &numchans);
			alGetBufferi(buf, AL_BITS, &numbits);
			filetime[sourceNum] += size / numchans * 8 / numbits;

			if(size == 0 || numchans == 0 || numbits == 0)
			{
				printf("OpenAL couldn't read source [%d] - Size: %d - NumChannels: %d - NumBits: %d \n", sourceNum, size, numchans, numbits);
				return 1;
			}
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
//#ifdef PRELOAD
bool AudioEnvironment::initAudioSource(int sourceNum, unsigned int *_rate, int *_channels, int *_type)
{
	return initAudioSource(sourceNum, _rate, _channels, _type, -1);
}
//#else
bool AudioEnvironment::initAudioSource(int sourceNum, unsigned int *_rate, int *_channels, int *_type, int bufSize)
//#endif
{
		rate[sourceNum] = *_rate;
		channels[sourceNum] = *_channels;
		type[sourceNum] = *_type;
//#ifdef PRELOAD
		//if(initOpenAlBuf(sourceNum) != 0)
		//	;//return 1;
		//initOpenAlBuf(sourceNum); // buffers are actually been created and destroyed - unlike the Audio Server where the buffers once setup there attributes never change.
//#else
		if(!m_preload)
			if(initOpenAlBuf(sourceNum, bufSize) != 0)
				return 1;
//#endif

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
            //fprintf(stderr, "Unhandled format (%s, %s) for %s", ChannelsName(channels[sourceNum]), TypeName(type[sourceNum]), videoSources[sourceNum]->name_.c_str());
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

        /* Wait for the last song to finish playing */
        //do {
        //    Sleep(10);
        //    alGetSourcei(source, AL_SOURCE_STATE, &state);
        //} while(alGetError() == AL_NO_ERROR && state == AL_PLAYING);
        /* Rewind the source position and clear the buffer queue */
		alSourceStop(source[sourceNum]);
        //alSourceRewind(source[clientID][sourceNum]);
        alSourcei(source[sourceNum], AL_BUFFER, 0);
        /* Reset old variables */
        basetime[sourceNum] = 0;
        filetime[sourceNum] = 0;
        old_format = format[sourceNum];
        old_rate = rate[sourceNum];

	return 0;
}

bool AudioEnvironment::isSourceStillPlaying(int sourceNum)
{
    //do {
    //    Sleep(10);
       alGetSourcei(source[sourceNum], AL_SOURCE_STATE, &state[sourceNum]);
    //} while(alGetError() == AL_NO_ERROR && state == AL_PLAYING);

	   return state[sourceNum] == AL_PLAYING;
}