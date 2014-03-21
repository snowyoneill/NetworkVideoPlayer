#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include "constants.h"

#ifndef AL_SOFT_buffer_samples
/* Sample types */
#define AL_BYTE                                  0x1400
#define AL_UNSIGNED_BYTE                         0x1401
#define AL_SHORT                                 0x1402
#define AL_UNSIGNED_SHORT                        0x1403
#define AL_INT                                   0x1404
#define AL_UNSIGNED_INT                          0x1405
#define AL_FLOAT                                 0x1406
#define AL_DOUBLE                                0x1407

/* Channel configurations */
#define AL_MONO                                  0x1500
#define AL_STEREO                                0x1501
#define AL_REAR                                  0x1502
#define AL_QUAD                                  0x1503
#define AL_5POINT1                               0x1504 /* (WFX order) */
#define AL_6POINT1                               0x1505 /* (WFX order) */
#define AL_7POINT1                               0x1506 /* (WFX order) */
#endif

typedef long long int int64_t;

/* Define the number of buffers and buffer size (in bytes) to use. 3 buffers is
 * a good amount (one playing, one ready to play, another being filled). 32256
 * is a good length per buffer, as it fits 1, 2, 4, 6, 7, 8, 12, 14, 16, 24,
 * 28, and 32 bytes-per-frame sizes. */
#define NUM_BUFFERS 4
//#define BUFFER_SIZE 32256/4 //4096

//---------------------------------------------------------------------------------------------------

ALsizei FramesToBytes(ALsizei size, ALenum channels, ALenum type);
ALsizei BytesToFrames(ALsizei size, ALenum channels, ALenum type);

class AudioEnvironment {
	int m_bufferSize;
	bool m_preload;

    /* Here are the buffers and source to play out through OpenAL with */
    ALuint buffers[MAXSTREAMS][NUM_BUFFERS];
    ALuint source[MAXSTREAMS];

	ALint state[MAXSTREAMS]; /* This will hold the state of the source */
    ALbyte *data[MAXSTREAMS]; /* A temp data buffer for getAVAudioData to write to and pass to OpenAL with */
    int count[MAXSTREAMS]; /* The number of bytes read from getAVAudioData */
    ALenum old_format;
    ALuint old_rate;
    /* The base time to use when determining the playback time from the
     * source. */
    int64_t basetime[MAXSTREAMS];
    int64_t filetime[MAXSTREAMS];
    ALenum format[MAXSTREAMS];
    ALenum channels[MAXSTREAMS];
    ALenum type[MAXSTREAMS];
    ALuint rate[MAXSTREAMS];

	bool streamPaused[MAXSTREAMS];

  public:
	/* The device and context handles to play with */
	static ALCdevice *device;
	static ALCcontext *ctx;

	AudioEnvironment(bool preload, int bufferSize);
	~AudioEnvironment();

	int initOpenAlDevice();
	int closeAudioDevice();
	/* Sets the volume for a specific source */
    int setVolume(int streamID, float level);

	int preLoadSources(int clientID);
	int pauseAudioStream(int streamID);
	int isPaused(int streamID);
//#ifdef PRELOAD
	int initOpenAlBuf(int sourceNum);
//#else
	int initOpenAlBuf(int sourceNum, int bufSize);
//#endif
	int closeAudioStream(int sourceNum);
//#ifdef PRELOAD
	int initAudioBuffers(int sourceNum, int countArray[NUM_BUFFERS], char* dataArray[NUM_BUFFERS]);
//#else
	int initAudioBuffers(int sourceNum, int countArray[NUM_BUFFERS], char* dataArray[NUM_BUFFERS], int bufSize);
//#endif
	int checkAudioBuffers(int sourceNum);
//#ifdef PRELOAD
	int refillAudioBuffers(int sourceNum, int count_, char* data_);
//#else
	int refillAudioBuffers(int sourceNum, int count_, char* data_, int bufSize);
//#endif
//#ifdef PRELOAD
	bool initAudioSource(int side, unsigned int *_rate, int *_channels, int *_type);
//#else
	bool initAudioSource(int side, unsigned int *_rate, int *_channels, int *_type, int bufSize);
//#endif

    double getElapsedALTime(int sourceNum);

	bool isSourceStillPlaying(int sourceNum);
};