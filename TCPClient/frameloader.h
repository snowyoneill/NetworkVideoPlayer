#ifndef __FRAMELOADER_H
#define __FRAMELOADER_H

#include "constants.h"

////////////////////////////////////////////// FFS //////////////////////////////////////////////////
#if FFS
#include "ffs.h"
#include <stdio.h>

struct ddsringbuffer {
	FFSFrameLoader ffs;
	bool free;
	bool badmemory;
	int frame;
};

ffsImage * getnextframe(int frame, __int64 side);
void donewithframe(int frame, __int64 side);
bool loadddsframe(int frame);
//bool loadframes(const char * mediapath,int & lastframe,int & fps, int & inputwidth, int & inputheight, int & blocks, int & compressed, __int64 side);
bool loadframes(const char * mediapath,int & lastframe,int & fps, int & inputwidth, int & inputheight, int & blocks, int & compressed, __int64 side, int & compresstype);
int getdecodetime();
int getmbpersec();
int getbufferlength(__int64 side);
void calculatedecodetime();
void frameproducer(void * parg);
void underrunframe(int frame, __int64 side);
void restartframes(__int64 side);
void changeRingBuffer(__int64 side);
void chechringbufferhealth(__int64 side, int frame);
int buffergoodframesahed(__int64 side, int frame);
#endif
////////////////////////////////////////////// AUDIO ////////////////////////////////////////////////
#ifdef LOCAL_AUDIO
#define OPENAL_ENV
extern bool stopAudioThread;
extern bool		g_preloadAudio;
extern int		g_bufferSize;
#if (defined(OPENAL_AUDIO) || defined (OPENAL_ENV))
int initAudioEnvir();
void decodeAndPlayAllAudioStreams(void* dummy);
int closeAudioStream(int sourceNum);
int pauseAudioStream(int sourceNum);

//extern void notifyStopOrRestartVideo(int videounit);
#endif
#endif

////////////////////////////////////////////// VIDEO ////////////////////////////////////////////////
void registerFFmpeg();
bool loadVideoFrames(const char * mediapath,double & last_frame, double & fps, int & inputwidth, int & inputheight, __int64 side);
void seekVideoThread(int side, double clock, double incr);
//void seekVideoThread(int side, double seekDuration);

//double getNextVideoFramePbo(double netClock, double pauseLength, int side, char *plainData);
double getNextVideoFramePbo(double netClock, double pauseLength, int side, char *plainData, double pboPTS);


double getNextVideoFrameNext(int side, char *plainData, int pboIndex, double& pboPTS);

double getVideoPts(int side);
void closeVideoThread(int side);
void frameReader(void * parg);
#if 1
void frameDecoder(void * parg);
#endif

#ifdef NETWORKED_AUDIO
	double getNextVideoFrame(double netClock, double pauseLength, int side, char *plainData);
#else
	double getNextVideoFrame(double openALAudioClock, double pauseLength, int side, char *plainData);
#endif
#ifdef LOCAL_AUDIO
	double getALAudioClock(int side);
	double getFFmpeg(int side);
	void setALVolume(int side, float level);
#endif
#endif