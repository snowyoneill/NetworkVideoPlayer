// ODFfmpegSource.cpp
// For reading video fiels via the Ffmpeg software package,
// which supports many video codecs and container formats.
// Authors: Michael Harville, Apr 2009, Shaun O'Neill, Mar 2012

#include "ODFfmpegSource.h"


#if 1
#define AVCODEC_MAX_AUDIO_FRAME_SIZE 192000

#include <windows.h>
#include <time.h>
//#include <sys/time.h>
//#include <sys/time.h>

struct timeval {
	__int32    tv_sec;         /* seconds */
	__int32    tv_usec;        /* microseconds */
};

/* FILETIME of Jan 1 1970 00:00:00. */
static const unsigned __int64 epoch = UINT64(116444736000000000);

/*
* timezone information is stored outside the kernel so tzp isn't used anymore.
*
* Note: this function is not for Win32 high precision timing purpose. See
* elapsed_time().
*/
static int
gettimeofday(struct timeval * tp, struct timezone * tzp)
{
	FILETIME	file_time;
	SYSTEMTIME	system_time;
	ULARGE_INTEGER ularge;

	GetSystemTime(&system_time);
	SystemTimeToFileTime(&system_time, &file_time);
	ularge.LowPart = file_time.dwLowDateTime;
	ularge.HighPart = file_time.dwHighDateTime;

	tp->tv_sec = (long)((ularge.QuadPart - epoch) / 10000000L);
	tp->tv_usec = (long)(system_time.wMilliseconds * 1000);

	return 0;
}

int64_t av_gettime(void) {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}
#endif
////////////////////////////// GLOBAL VARIABLES /////////////////////////////

// Need to do some FFMPEG setup once per application.
static bool initializedFfmpeg = false;

/////////////////////////////// INTERNAL STATE //////////////////////////////
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)
#if 1
#define MAX_VIDEOQ_SIZE (5 * 16 * 1024)
#endif
/* no AV sync correction is done if below the AV sync threshold */
#define AV_SYNC_THRESHOLD 0.01
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 100.0
#define VIDEO_PICTURE_QUEUE_SIZE 5
		
typedef struct VideoPicture {
	AVFrame* pictFrame;
	int width, height; /* source height & width */
	double pts;
} VideoPicture;

typedef unsigned __int8   uint8_t;

////////////////////////////// GLOBAL VARIABLES /////////////////////////////

// Special value indicating that we do not know the timestamp for 
// current frame.
const double ODFfmpegSource::UnknownTime = -1.0;

// Special value indicating that we do not know the frame rate for 
// the video
const double ODFfmpegSource::UnknownFPS = -1.0;

////////////////////////////////////////////////////////////////////////////

struct ODFfmpegSource::State
{
    // Pointer to parent object, so state can access its members and those of
    // base classes (e.g. width, height, etc.)
    ODFfmpegSource* parent_;

	// File format info, including stream information as well as things like
	// title, author, year, etc. See avformat.h for info.
	AVFormatContext* formatCtx_;

	// Description of aspects of the video stream such as frame rate,
	// duration, etc.
	AVStream* videoStream_;

	// Info about the video decoder, including things like pixel format, gop
	// size, etc. See avcodec.h for info.
	AVCodecContext* codecCtx_;

	// The decoder/encoder itself. Has member functions (well, function ptrs)
	// for encode, decode, etc. See avcodec.h for info.
	AVCodec* codec_;

	// Context for software scaler that decompresses while optionally scaling
	// image.
	SwsContext* decodeCtx_;

	// Storage for current compressed frame in its "native" format.
	AVFrame* nativeFrame_;
  
	// Storage for current uncompressed frame.
	AVFrame* rawFrame_;
	UINT8* rawFrameBuf_;
	int rawFrameNumBytes_;

	// Index of video stream, within multimedia file, which we are concerned.
	int videoStreamIndex_;

	// Dimensions of video before decompression. These may differ from
    // output video dimensions if we are doing scaling at decompression time. 
	int nativeWidth_;
	int nativeHeight_;

	// Pixel format to use for raw frame. See avutil.h
	PixelFormat desiredRawFormat_;

	// The last valid presentation time we have seen. We use this to determine
	// frame and packet timestamps in case not every packet has this info.
    //
    // TODO: Have one of these per instance, and use a global map to access
    // the right one inside allocateFrame.
	//SINT64 lastPresentationTime_;  // Currently not used.
    //static SINT64 GlobalLastPresentationTime_;
	__declspec( thread ) static SINT64 GlobalLastPresentationTime_;

	//----------------------------------------------------------------------------
	//SINT64 GlobalLastPresentationTime_;
	//----------------------------------------------------------------------------

	State(ODFfmpegSource* parent);
	~State();

	// Memory allocation methods used internally. These help propagate timestamp
	// values.
	static int allocateFrame(AVCodecContext* codecCtx, AVFrame* frame);
    static void releaseFrame(AVCodecContext* codecCtx, AVFrame* frame);

	//----------------------------------------------------------------------------
	//int allocateFrame(AVCodecContext* codecCtx, AVFrame* frame);
	//void releaseFrame(AVCodecContext* codecCtx, AVFrame* frame);
	//----------------------------------------------------------------------------

	PacketQueue     videoq;
	VideoPicture    pictq[VIDEO_PICTURE_QUEUE_SIZE];
	int             pictq_size, pictq_rindex, pictq_windex;
	bool			quit;
	//volatile int pictq_size;

	double			video_clock;


	double          frame_timer;

	// new stuff
	double          frame_timer_start;
	double			pts_drift;
	double			video_current_pts;
	int64_t			video_current_pts_time;
	// end new stuff

	double          frame_last_pts;
	double          frame_last_delay;
	//----------------------------------------------------------------------------
	// Description of aspects of the audio stream.
	AVStream* audioStream_;
	
	// Info about the audio decoder.
	AVCodecContext* acodecCtx_;

	// The audio decoder/encoder itself..
	AVCodec* acodec_;

	char	audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
	int		audio_buf_size;
	int		audio_buf_index;

	// Index of audio stream, within multimedia file, which we are concerned.
	int		audioStreamIndex_;

	PacketQueue		audioq;
	double			audio_clock;
	uint8_t         *audio_pkt_data;
	int             audio_pkt_size;
	int             audio_hw_buf_size;  

	int             seek_req;
	int64_t			seek_rel;
	int             seek_flags;
	int64_t         seek_pos;

	/* Mutexs / semaphores initialization - resource access control to queues.
	 */
#ifdef USE_ODBASE
	ODBase::Lock audioqMutex;
	ODBase::Lock pictqMutex;
	ODBase::Semaphore pictqSemaphore;
	ODBase::Semaphore pictPacketqSemaphore;
	ODBase::Lock videoqMutex;
#else
	HANDLE audioqMutex;// = CreateMutex(NULL, false, NULL);
	HANDLE pictqMutex;// = CreateMutex(NULL, false, NULL);
	HANDLE pictqSemaphore;// = CreateSemaphore( NULL, 0 , 1, NULL);
	HANDLE pictPacketqSemaphore;// = CreateSemaphore( NULL, 0 , 1, NULL);
	HANDLE videoqMutex;// = CreateMutex(NULL, false, NULL);
#endif
};


/* get the current video clock value */
//static double get_video_clock()
//{
//    double delta;
//    //if (is->paused) {
//    //    delta = 0;
//    //} else {
//        delta = (av_gettime() - video_current_pts_time) / 1000000.0;
//    //}
//    return video_current_pts + delta;
//}


/////////////////////////// AUDIO/VIDEO QUEUES //////////////////////////////
AVPacket flush_pkt, end_pkt;

bool videoDone = false;

void packet_queue_init(PacketQueue *q) {
	memset(q, 0, sizeof(PacketQueue));
}

#ifdef AUDIO_DECODING
//void packet_queue_init(PacketQueue *q);
//int audio_packet_queue_put(PacketQueue *q, AVPacket *pkt);
//int audio_packet_queue_get(PacketQueue *q, AVPacket *pkt);

void ODFfmpegSource::audio_packet_queue_flush(PacketQueue *q)
{
  AVPacketList *pkt, *pkt1;

#ifdef USE_ODBASE
	state->audioqMutex.grab();
#else
	WaitForSingleObject(state->audioqMutex, INFINITE);
#endif

	for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1) {
		pkt1 = pkt->next;
		av_free_packet(&pkt->pkt);
		av_freep(&pkt);
	}
	q->last_pkt = NULL;
	q->first_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	
#ifdef USE_ODBASE
	state->audioqMutex.release();
#else
	ReleaseMutex(state->audioqMutex);
#endif
}

/* Places the next packet on the specified queue.
 */
int ODFfmpegSource::audio_packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
	AVPacketList *pkt1;
	if(pkt != &flush_pkt && av_dup_packet(pkt) < 0) {
	//if(av_dup_packet(pkt) < 0) {
		return -1;
	}
	pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

#ifdef USE_ODBASE
	state->audioqMutex.grab();
#else
	WaitForSingleObject(state->audioqMutex, INFINITE);
#endif
	//WaitForSingleObject(audioqMutex, INFINITE);
	//SDL_LockMutex(q->mutex);
	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	//SDL_CondSignal(q->cond);
	//SDL_UnlockMutex(q->mutex);
	//ReleaseMutex(audioqMutex);
#ifdef USE_ODBASE
	state->audioqMutex.release();
#else
	ReleaseMutex(state->audioqMutex);
#endif
	//printf("PACKET PUT: nb_packets %d\n", q->nb_packets);

	return 0;
}
/* Retrieves the next packet on the specified queue.
 * Returns 1 if it got the next packet, -1 if there are no packets.
 */
int ODFfmpegSource::audio_packet_queue_get(PacketQueue *q, AVPacket *pkt)
{
  AVPacketList *pkt1;
  int ret = -1;
  
  //SDL_LockMutex(q->mutex);

#ifdef USE_ODBASE
	state->audioqMutex.grab();
#else
	WaitForSingleObject(state->audioqMutex, INFINITE);
#endif
  //WaitForSingleObject(audioqMutex, INFINITE);
  //while(ret == -1)
  //{
	  pkt1 = q->first_pkt;
	  if (pkt1)
	  {
		  q->first_pkt = pkt1->next;
		  if (!q->first_pkt)
			  q->last_pkt = NULL;
		  q->nb_packets--;
		  q->size -= pkt1->pkt.size;
		  *pkt = pkt1->pkt;
		  av_free(pkt1);
		  ret = 1;

		  //printf("PACKET GET: nb_packets %d\n", q->nb_packets);
		  //break;
		  //return ret;
	  }
	  else
	  {
		  //return -1;
		  ret = -1;
		  //printf("WAIT");
	  }
  //}
  //SDL_UnlockMutex(q->mutex);
  //ReleaseMutex(audioqMutex);
#ifdef USE_ODBASE
	state->audioqMutex.release();
#else
	ReleaseMutex(state->audioqMutex);
#endif
  //return -1;
  return ret;
}
#endif

#if 1
void ODFfmpegSource::video_packet_queue_flush(PacketQueue *q)
{
	AVPacketList *pkt, *pkt1;

#ifdef USE_ODBASE
	state->videoqMutex.grab();
#else
	WaitForSingleObject(state->videoqMutex, INFINITE);
#endif
	for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1) {
		pkt1 = pkt->next;
		av_free_packet(&pkt->pkt);
		av_freep(&pkt);
	}
	q->last_pkt = NULL;
	q->first_pkt = NULL;
	q->nb_packets = 0;
	q->size = 0;
	//printf("FLUSH videoq.\n");
#ifdef USE_ODBASE
	state->videoqMutex.release();
#else
	ReleaseMutex(state->videoqMutex);
#endif
}

//HANDLE videoqMutex = CreateMutex(NULL, false, NULL);
//int vid_packet_queue_put(PacketQueue *q, AVPacket *pkt);
//int vid_packet_queue_get(PacketQueue *q, AVPacket *pkt);
int ODFfmpegSource::vid_packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
	AVPacketList *pkt1;
	if(pkt != &flush_pkt && pkt != &end_pkt && av_dup_packet(pkt) < 0) {
	//if(av_dup_packet(pkt) < 0) {
		return -1;
	}
	pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
	if (!pkt1)
		return -1;
	pkt1->pkt = *pkt;
	pkt1->next = NULL;

#ifdef USE_ODBASE
	state->videoqMutex.grab();
#else
	WaitForSingleObject(state->videoqMutex, INFINITE);
#endif
	//WaitForSingleObject(videoqMutex, INFINITE);
	//SDL_LockMutex(q->mutex);

	if (!q->last_pkt)
		q->first_pkt = pkt1;
	else
		q->last_pkt->next = pkt1;
	q->last_pkt = pkt1;
	q->nb_packets++;
	q->size += pkt1->pkt.size;
	//SDL_CondSignal(q->cond);
	//SDL_UnlockMutex(q->mutex);
#ifdef USE_ODBASE
	state->pictPacketqSemaphore.signal();
#else
	ReleaseSemaphore(state->pictPacketqSemaphore, 1, NULL );
#endif

#ifdef USE_ODBASE
	state->videoqMutex.release();
#else
	ReleaseMutex(state->videoqMutex);
#endif
	//ReleaseMutex(videoqMutex);
	//printf("put: nb_packets %d\n", q->nb_packets);

	return 0;
}

int ODFfmpegSource::vid_packet_queue_get(PacketQueue *q, AVPacket *pkt)
{
	AVPacketList *pkt1;
	int ret = 0;

	//SDL_LockMutex(q->mutex);

	//if(state->quit)
	//	return -1;

	//state->videoqMutex.grab();
	//WaitForSingleObject(videoqMutex, INFINITE);
	while(ret == 0)
	{
		if(state->quit)
			return -1;

		
#ifdef USE_ODBASE
		state->videoqMutex.grab();
#else
		WaitForSingleObject(state->videoqMutex, INFINITE);
#endif

		pkt1 = q->first_pkt;
		if (pkt1)
		{
			q->first_pkt = pkt1->next;
			if (!q->first_pkt)
			  q->last_pkt = NULL;
			q->nb_packets--;
			q->size -= pkt1->pkt.size;
			*pkt = pkt1->pkt;
			av_free(pkt1);
			ret = 1;

#ifdef USE_ODBASE
			state->videoqMutex.release();
#else
			ReleaseMutex(state->videoqMutex);
#endif

			//printf("get: nb_packets %d\n", q->nb_packets);
			//break;
			//return ret;
		}
		else
		{
#ifdef USE_ODBASE
			state->videoqMutex.release();
#else
			ReleaseMutex(state->videoqMutex);
#endif
			
#ifdef USE_ODBASE
			state->pictPacketqSemaphore.wait();
#else
			WaitForSingleObject(state->pictPacketqSemaphore,INFINITE);
#endif
			//WaitForSingleObject(args->pictqCond,INFINITE);
			//return -1;
			//ret = 0;
			//ret = -1;
			//printf("WAIT");
		}
		//state->videoqMutex.release();
	}
	//SDL_UnlockMutex(q->mutex);

	//ReleaseMutex(videoqMutex);
	//return -1;
	return ret;
}
#endif
#ifdef VIDEO_DECODING
/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.01
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/////////////////////////// FRAME DISPLAY/DELAY //////////////////////////////
#ifdef NETWORKED_AUDIO
/* Uses the incoming audio clock from the server to calculate the next frame delay and copies the current index in the ring buffer to the dataBuff array.
 */
double ODFfmpegSource::video_refresh_timer(double netClock, double pauseLength, char *dataBuff)
#else
/* Uses the local audio clock time to calculate the next frame delay and copies the current index in the ring buffer to the dataBuff array.
 */
double ODFfmpegSource::video_refresh_timer(double openALAudioClock, double pauseLength, char *dataBuff)
#endif
{
	VideoPicture *vp;
	double actual_delay, delay, sync_threshold, ref_clock, diff;

	if(state->codecCtx_)
	{
#ifdef USE_ODBASE
		state->pictqMutex.grab();
#else
		WaitForSingleObject(state->pictqMutex, INFINITE);
#endif
		if(state->pictq_size == 0)
		{
#ifdef USE_ODBASE
			state->pictqMutex.release();
#else
			ReleaseMutex(state->pictqMutex);
#endif
			return -1;
		}
		//if(state->pictq_size == -100)
		//{
		//	return -100;
		//}
		else
		{
			
#ifdef USE_ODBASE
			state->pictqMutex.release();
#else
			ReleaseMutex(state->pictqMutex);
#endif

			vp = &state->pictq[state->pictq_rindex];
			//printf("Video Index: %d\n", state->pictq_rindex);
			delay = vp->pts - state->frame_last_pts; /* the pts from last time */
			//printf("vp->pts: %f - state->frame_last_pts: %f - delay: %f\n", vp->pts, state->frame_last_pts, delay);
			if(delay <= 0 || delay >= 1.0)
			{
				/* if incorrect delay, use previous one */
				delay = state->frame_last_delay;
			}

			//double max_frame_duration1 = (state->formatCtx_->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
			//if (/*!isnan(last_duration) && */delay > 0 && delay < max_frame_duration1) {
			//	delay = state->frame_last_delay;
			//}

			/* save for next time */
			state->frame_last_delay = delay;
			state->frame_last_pts = vp->pts;

			state->video_current_pts = vp->pts;
			state->video_current_pts_time = av_gettime();

#ifdef NETWORKED_AUDIO
			/* update delay to sync to audio */
			ref_clock = netClock;
#else
			ref_clock = openALAudioClock;
			//ref_clock = get_audio_clock();
#endif

#if 1
			//printf("\tref_clock: %f", ref_clock);
			diff = vp->pts - ref_clock;
			//diff = state->frame_timer /*- state->frame_timer_start*/ - ref_clock;
			//diff = vp->pts - ((av_gettime() / 1000000.0) - state->frame_timer_start);
			//diff = vp->pts - ref_clock + (((av_gettime() / 1000000.0) - state->frame_timer_start) - ref_clock);
			//diff = state->frame_timer - ref_clock;
			//diff = vp->pts - state->frame_timer - state->frame_timer_start;//(state->frame_timer - (av_gettime() / 1000000.0));// + ref_clock;

			//printf("diff: %09.6f ----> \n", diff);

			/* Skip or repeat the frame. Take delay into account
			//FFPlay still doesn't "know if this is the best guess." */
			//sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
			////if(fabs(diff) < AV_NOSYNC_THRESHOLD)
			//{
			//	//printf("diff: %09.6f - delay: %09.6f - sync_threshold: %09.6f\n", diff, delay, sync_threshold);
			//	if(diff <= -sync_threshold)
			//		delay = 0;
			//	//else if(diff >= sync_threshold * 1.5)
			//		//return -10; // before the delay is add to the frame timer.
			//	else if(diff >= sync_threshold)
			//		delay = 2 * delay;
			//		//delay = diff;
			//}

			//double max_frame_duration = (state->formatCtx_->flags & AVFMT_TS_DISCONT) ? 10.0 : 3600.0;
			//sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
			//if (/*!isnan(diff) && f*/abs(diff) < max_frame_duration) {
			//	if (diff <= -sync_threshold)
			//		delay = FFMAX(0, delay + diff);
			//	else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
			//		delay = delay + diff;
			//	else if (diff >= sync_threshold)
			//		delay = 2 * delay;
			//}

                /* skip or repeat frame. We take into account the
                   delay to compute the threshold. I still don't know
                   if it is the best guess */
               sync_threshold = AV_SYNC_THRESHOLD;
                if (delay > sync_threshold)
                    sync_threshold = delay;
                if (fabs(diff) < AV_NOSYNC_THRESHOLD) {
                    if (diff <= -sync_threshold)
                        delay = 0;
                    else if (diff >= sync_threshold)
                        delay = 2 * delay;
					//printf("delay updated - diff: %09.6f - sync_threshold: %09.6f\n", diff, sync_threshold);
                }
#endif

			//// Copy the data.
			if(vp->pictFrame)
				memcpy(dataBuff, vp->pictFrame->data[0], vp->pictFrame->linesize[0] * vp->height);

			state->frame_timer += delay;
			//state->frame_timer = state->frame_timer_start + ref_clock + delay;
			/* computer the REAL delay */

			//actual_delay = state->frame_timer - (av_gettime() / 1000000.0) + pauseLength;
//			actual_delay = (state->frame_timer - (av_gettime() / 1000000.0))/* - ref_clock*/ + pauseLength;
			//actual_delay = state->frame_timer - ref_clock + pauseLength;
			//actual_delay = (state->video_clock);// - ref_clock + (av_gettime() / 1000000.0)) + pauseLength;

			double now = av_gettime() / 1000000.0;
			double frameDiff = state->frame_timer - (now);
			//frameDiff = state->frame_timer - state->frame_timer_start - ref_clock;
//			frameDiff = state->frame_timer - ref_clock;
			actual_delay = frameDiff + pauseLength;
			//actual_delay = vp->pts + delay - ref_clock + pauseLength;
			//actual_delay = vp->pts + delay - ref_clock + pauseLength;
			
			//actual_delay = (state->frame_timer - state->frame_timer_start) - ref_clock + pauseLength;
			
			//printf("frame_timer: %09.6f - av_gettime: %09.6f - pauseLength: %09.6f - actual_delay: %09.6f\n", state->frame_timer, ref_clock, pauseLength, actual_delay);
			//printf("frame_timer: %09.6f - av_gettime: %09.6f - pauseLength: %09.6f - actual_delay: %09.6f - diff: %09.6f\n", state->frame_timer, av_gettime() / 1000000.0, pauseLength, actual_delay, diff);
			//printf("frame_timer: %09.6f - av_gettime: %I64u - pauseLength: %09.6f - actual_delay: %09.6f\n", state->frame_timer, av_gettime(), pauseLength, actual_delay);

			//printf("frame_timer_start: %09.6f - frame_timer: %09.6f - av_gettime: %09.6f - frameDiff: %09.6f - ref_clock: %09.6f - < 0.01: %s\n", state->frame_timer_start, state->frame_timer, now, frameDiff, ref_clock, (actual_delay < 0.01) ? "true" : "false");
#if 1
			if(actual_delay < 0.01) {
				
						
				/* Really it should skip the picture instead */
				//actual_delay = 0.01;
				//actual_delay *= -1;
				actual_delay = 0.0001;
				//{
				//	if(++state->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
				//		state->pictq_rindex = 0;


				//	state->pictqMutex.grab();
				//	state->pictq_size--;
				//	state->pictqMutex.release();
				//	state->pictqSemaphore.signal();
				//	return -10;
				//}
			}
#endif
			//actual_delay = (actual_delay < 0.01) ? 0.0001 : actual_delay;
			//else
				//actual_delay = 0.03;
			double f_delay = (actual_delay * 1000 /*+ 0.5*/);
#if 1

			// Copy the data.
			//if(vp->pictFrame)
			//	memcpy(dataBuff, vp->pictFrame->data[0], vp->pictFrame->linesize[0] * vp->height);

#endif
			/* show the picture! */
			//video_display();
			//if(diff >= sync_threshold)
				//return -10;
				//return f_delay;

			//if(diff > 2.0)
			//{
			//	printf("Seek stream - vp->pts: %6.2f - ref_clock: %6.2f - diff: %6.2f", vp->pts, ref_clock, diff);
			//	//seek(netClock, netClock - vp->pts);
			//	seek(netClock, -diff);
			//}

			//printf("vp->pts: %6.2f - ref_clock: %6.2f - diff: %6.2f - sync_threshold: %6.2f - f_delay: %6.2f.\n", vp->pts, ref_clock, diff, sync_threshold, f_delay);
	
			//if(diff <= sync_threshold || diff > sync_threshold * VIDEO_PICTURE_QUEUE_SIZE)
			//if(diff <= sync_threshold)
			//if(diff < sync_threshold)
			{
				/* update queue for next picture! */
				if(++state->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
					state->pictq_rindex = 0;

				//printf("\tGrabbing video mutex\n");
#ifdef USE_ODBASE
				state->pictqMutex.grab();
#else
				WaitForSingleObject(state->pictqMutex, INFINITE);
#endif

				//WaitForSingleObject(pictqMutex, INFINITE);
				//SDL_LockMutex(is->pictq_mutex);
				state->pictq_size--;
				//printf("ReleaseSemaphore\n");
				//printf("out - pictq_size: %d.\n", state->pictq_size);
				//SDL_CondSignal(is->pictq_cond);
				//SDL_UnlockMutex(is->pictq_mutex);
				//ReleaseMutex(pictqMutex);
#ifdef USE_ODBASE
				state->pictqMutex.release();
#else
				ReleaseMutex(state->pictqMutex);
#endif

				//ReleaseSemaphore(state->pictqSemaphore, 1, NULL );
#ifdef USE_ODBASE
				state->pictqSemaphore.signal();
#else
				ReleaseSemaphore(state->pictqSemaphore, 1, NULL );
#endif

				videoDone = true;
			}

			return f_delay;	
		}
	}
	else
	{
		// If the video codec has not yet been initialised.
		return -100;
	}
}
#endif

double ODFfmpegSource::video_pts()
{
	VideoPicture *vp;
	vp = &state->pictq[state->pictq_rindex];
	return vp->pts;
}

#ifdef NETWORKED_AUDIO
/* Uses the incoming audio clock from the server to calculate the next frame delay and copies the current index in the ring buffer to the dataBuff array.
 */
//double ODFfmpegSource::video_refresh_timer_pbo(double netClock, double pauseLength, char *dataBuff)
double ODFfmpegSource::video_refresh_timer_pbo(double netClock, double pauseLength, char *dataBuff, double ptsClk)
#else
/* Uses the local audio clock time to calculate the next frame delay and copies the current index in the ring buffer to the dataBuff array.
 */
//double ODFfmpegSource::video_refresh_timer_pbo(double openALAudioClock, double pauseLength, char *dataBuff)
double ODFfmpegSource::video_refresh_timer_pbo(double openALAudioClock, double pauseLength, char *dataBuff, double ptsClk)
#endif
{
	//VideoPicture *vp;
	double actual_delay, delay, sync_threshold, ref_clock, diff;

	if(state->codecCtx_)
	{
		//state->pictqMutex.grab();
		if(state->pictq_size == 0 && ptsClk == 0)
		{
			//state->pictqMutex.release();
			//if(atVideoEnd_)
			if(state->quit)
			{
				printf(")))video_refresh_timer_pbo - state quit is -50\n\n");
				return -50;
			}
			//state->pictqSemaphore.signal();
			//printf(")))video_refresh_timer_pbo - size: %d\n", state->pictq_size);
			return -1;
		}
		//if(state->pictq_size == -100)
		//{
		//	return -100;
		//}
		else
		{
			//state->pictqMutex.release();


			//vp = &state->pictq[state->pictq_rindex];
			////printf("Video Index: %d\n", state->pictq_rindex);
			delay = ptsClk - state->frame_last_pts; /* the pts from last time */
			//delay = vp->pts - state->frame_last_pts; /* the pts from last time */
			if(delay <= 0 || delay >= 1.0)
			{
				/* if incorrect delay, use previous one */
				delay = state->frame_last_delay;
			}
			/* save for next time */
			state->frame_last_delay = delay;
			//state->frame_last_pts = vp->pts;
			state->frame_last_pts = ptsClk;

#ifdef NETWORKED_AUDIO
			/* update delay to sync to audio */
			ref_clock = netClock;
#else
			ref_clock = openALAudioClock;
			//ref_clock = get_audio_clock();
#endif


			//diff = vp->pts - ref_clock;
			diff = ptsClk - ref_clock;

			//printf("+++pts: %07.2f - ref_clock: %07.2f\n", ptsClk, ref_clock);



			//printf(" - pts: %07.2f - ref_clock: %07.2f\n", vp->pts, ref_clock);

			/* Skip or repeat the frame. Take delay into account
			FFPlay still doesn't "know if this is the best guess." */
			sync_threshold = (delay > AV_SYNC_THRESHOLD) ? delay : AV_SYNC_THRESHOLD;
			//if(fabs(diff) < AV_NOSYNC_THRESHOLD)
			{
				if(diff <= -sync_threshold)
					delay = 0;
				//else if(diff > sync_threshold)
					//return -10; // before the delay is add to the frame timer.
				else if(diff >= sync_threshold)
					delay = 2 * delay;
			}
			//printf(" - diff: %07.2f - delay: %07.2f - sync_threshold: %07.2f", diff, delay, sync_threshold);
			state->frame_timer += delay;
			/* computer the REAL delay */

			actual_delay = state->frame_timer - (av_gettime() / 1000000.0) + pauseLength;
			//actual_delay = state->frame_timer - ref_clock + pauseLength;
			
			//printf("pbo -> frame_timer: %09.6f - av_gettime: %09.6f - pauseLength: %09.6f - actual_delay: %09.6f\n", state->frame_timer, ref_clock, pauseLength, actual_delay);
			//printf(" - pauseLength %f - actual_delay: %07.2f", pauseLength, actual_delay);
			if(actual_delay < 0.01) {
				/* Really it should skip the picture instead */
				//actual_delay = 0.01;
				actual_delay = 0.001;
				//actual_delay *= -1;
			}
			//else
			//	actual_delay = 0.03;
			double f_delay = (actual_delay * 1000/* + 0.5*/);
			//printf(" - f_delay: %07.2f\n", f_delay);

			//if(diff >= sync_threshold)
				//return -10;
				//return f_delay;

			//if(diff > 2.0)
			//{
			//	printf("Seek stream - vp->pts: %6.2f - ref_clock: %6.2f - diff: %6.2f", vp->pts, ref_clock, diff);
			//	//seek(netClock, netClock - vp->pts);
			//	seek(netClock, -diff);
			//}

			//printf("vp->pts: %6.2f - ref_clock: %6.2f - diff: %6.2f - sync_threshold: %6.2f - f_delay: %6.2f.\n", vp->pts, ref_clock, diff, sync_threshold, f_delay);
			//if(diff <= sync_threshold || diff > sync_threshold * VIDEO_PICTURE_QUEUE_SIZE)
			//if(diff <= -sync_threshold || diff > sync_threshold)
			//if(diff <= sync_threshold)





			//{
			//	/* update queue for next picture! */
			//	if(++state->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
			//		state->pictq_rindex = 0;

			//	//printf("\tGrabbing video mutex\n");
			//	state->pictqMutex.grab();
			//	//WaitForSingleObject(pictqMutex, INFINITE);
			//	state->pictq_size--;
			//	//printf("out pictq_size: %d.\n", state->pictq_size);
			//	//ReleaseMutex(pictqMutex);
			//	state->pictqMutex.release();
			//	//ReleaseSemaphore(state->pictqSemaphore, 1, NULL );
			//	state->pictqSemaphore.signal();	

			//	videoDone = true;
			//}

			return f_delay;
		}
	}
	else
	{
		// If the video codec has not yet been initialised.
		return -100;
	}
}
//int lastRingIdx = 0;
int pboCpyIdx = 0;
int ODFfmpegSource::video_refresh_timer_next(char *dataBuff, int pboIndex, double& pboPTS)
{
	VideoPicture *vp;
	//double actual_delay, delay, sync_threshold, ref_clock, diff;

	if(state->codecCtx_)
	{
#ifdef USE_ODBASE
		state->pictqMutex.grab();
#else
		WaitForSingleObject(state->pictqMutex, INFINITE);
#endif
		if(currFrameIndex_ == int(duration_ * fps_))
		{
#ifdef USE_ODBASE
			state->pictqMutex.release();
#else
			ReleaseMutex(state->pictqMutex);
#endif
			return -1*50;
		}

		if(state->pictq_size == 0)
		{
			//printf("state->pictq_size: %d\n", state->pictq_size);
#ifdef USE_ODBASE
			state->pictqMutex.release();
#else
			ReleaseMutex(state->pictqMutex);
#endif
			//if(atVideoEnd_)
			if(state->quit)
			{
				printf(")))video_refresh_timer_next - state quit is -50\n\n");
				return -50;
			}
			//state->pictqSemaphore.signal();	
			return  -1;
		}
		//if(state->pictq_size == -100)
		//{
		//	return -100;
		//}
		else
		{
#ifdef USE_ODBASE
			state->pictqMutex.release();
#else
			ReleaseMutex(state->pictqMutex);
#endif
			
#if 1
			//vp = &state->pictq[state->pictq_rindex];

			//if(lastRingIdx != state->pictq_rindex)
			//{
			//	pboIndex = 0;
			//	lastRingIdx = state->pictq_rindex;
			//}
			//else
			{
				/*int newPboIndex = state->pictq_rindex + pboIndex;
				if(newPboIndex >= VIDEO_PICTURE_QUEUE_SIZE)
					newPboIndex = newPboIndex-VIDEO_PICTURE_QUEUE_SIZE;*/

			/*	if(newPboIndex >= VIDEO_PICTURE_QUEUE_SIZE)
					newPboIndex = 0;*/

				int wIdx = (state->pictq_rindex + state->pictq_size) % VIDEO_PICTURE_QUEUE_SIZE;
				//if(pboCpyIdx >= wIdx)
				
				if(pboCpyIdx == wIdx && state->pictq_size < VIDEO_PICTURE_QUEUE_SIZE)
				//if(pboIndex == wIdx && state->pictq_size < VIDEO_PICTURE_QUEUE_SIZE)


				//if(pboIndex >= state->pictq_size)
				{
					//printf("---wIdx: %d - pboCpyIdx: %d\n", wIdx, pboCpyIdx);
					//state->pictqMutex.release();
					//Sleep(5);
					return -1;
				}
			}
			//state->pictqMutex.release();
			//int newPboIndex = (state->pictq_rindex + pboIndex) % VIDEO_PICTURE_QUEUE_SIZE;

			//int newPboIndex = (state->pictq_rindex + pboCpyIdx) % VIDEO_PICTURE_QUEUE_SIZE;

			int newPboIndex = pboCpyIdx;
			//int newPboIndex = pboIndex;
			

			vp = &state->pictq[newPboIndex];
			//printf("state->pictq_size: %d - pictq_rindex: %d - pboIndex: %d - newPboIndex: %d\n", state->pictq_size, state->pictq_rindex, pboIndex, newPboIndex);

			//printf("state->pictq_size: %d - pictq_rindex: %d - pboCpyIdx: %d - newPboIndex: %d\n", state->pictq_size, state->pictq_rindex, pboCpyIdx, newPboIndex);
			pboCpyIdx = (pboCpyIdx + 1) % VIDEO_PICTURE_QUEUE_SIZE;
			/*
			printf("pictq_rindex: %d - pboIndex:     %d - newPboIndex:  %d\n", state->pictq_rindex, pboIndex, newPboIndex);
			if(pboIndex == 1)
				printf("--------------\n");
			*/

			// Copy the data.
			if(vp->pictFrame)// && vp->pictFrame > 0 && vp->width > 0)
				memcpy((unsigned char *) dataBuff, (unsigned char *) vp->pictFrame->data[0], sizeof(unsigned char) * vp->pictFrame->linesize[0] * vp->height);

			pboPTS = vp->pts;

			//printf("\tGrabbing video mutex\n");
			
			//pictqMutex.grab();
#endif
			//WaitForSingleObject(pictqMutex, INFINITE);
			//SDL_LockMutex(is->pictq_mutex);
			//state->pictq_size--;
			//printf("out pictq_size: %d.\n", state->pictq_size);
			//SDL_CondSignal(is->pictq_cond);
			//SDL_UnlockMutex(is->pictq_mutex);
			//ReleaseSemaphore(pictqSemaphore, 1, NULL );
#if 1
			//pictqSemaphore.signal();
			//ReleaseMutex(pictqMutex);
			//pictqMutex.release();
#endif
			//state->pictqMutex.release();


			{
				/* update queue for next picture! */
				if(++state->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
					state->pictq_rindex = 0;

				//printf("\tGrabbing video mutex\n");
#ifdef USE_ODBASE
				state->pictqMutex.grab();
#else
				WaitForSingleObject(state->pictqMutex, INFINITE);
#endif
				//WaitForSingleObject(pictqMutex, INFINITE);
				state->pictq_size--;
				//printf("out pictq_size: %d.\n", state->pictq_size);
				//ReleaseMutex(pictqMutex);
#ifdef USE_ODBASE
				state->pictqMutex.release();
#else
				ReleaseMutex(state->pictqMutex);
#endif

#ifdef USE_ODBASE
				state->pictqSemaphore.signal();
#else
				ReleaseSemaphore(state->pictqSemaphore, 1, NULL );
#endif

				videoDone = true;
			}


			return 1;			
		}
		
	}

	// If the video codec has not yet been initialised.
	return -100;
}

#if 0
void scale_video()
{
	VideoPicture *vp;
	AVPicture pict;
	float aspect_ratio;
	int w, h, x, y;
	int i;

	vp = &state->pictq[state->pictq_rindex];
	if(vp->pictFrame)
	{
		if(state->codecCtx_->sample_aspect_ratio.num == 0)
		{
			aspect_ratio = 0;
		}
		else
		{
			aspect_ratio = av_q2d(state->codecCtx_->sample_aspect_ratio) *
				state->codecCtx_->width / state->codecCtx_->height;
		}
		if(aspect_ratio <= 0.0)
		{
			aspect_ratio = (float)state->codecCtx_->width / (float)state->codecCtx_->height;
		}
		h = state->codecCtx_->height;
		w = ((int)rint(h * aspect_ratio)) & -3;
		if(w > state->codecCtx_->width)
		{
			w = state->codecCtx_->width;
			h = ((int)rint(w / aspect_ratio)) & -3;
		}
		x = (state->codecCtx_->width - w) / 2;
		y = (state->codecCtx_->height - h) / 2;

		//SDL_DisplayYUVOverlay(vp->bmp, &rect);
	}
}
#endif

////////////////////////////////// AUDIO DECODING /////////////////////////////////

/* Returns information about the given audio stream. Returns 0 on success. */
int ODFfmpegSource::getAVAudioInfo(unsigned int *rate, int *channels, int *type)
{
	if (state->audioStreamIndex_ == -1)
		return 1;

    if(state->acodecCtx_->codec_type != AVMEDIA_TYPE_AUDIO)
        return 1;

	if(type)
    {
        if(state->acodecCtx_->sample_fmt == AV_SAMPLE_FMT_U8)
            *type = AV_SAMPLE_FMT_U8;
        else if(state->acodecCtx_->sample_fmt == AV_SAMPLE_FMT_S16)
            *type = AV_SAMPLE_FMT_S16;
        else if(state->acodecCtx_->sample_fmt == AV_SAMPLE_FMT_S32)
            *type = AV_SAMPLE_FMT_S32;
        else if(state->acodecCtx_->sample_fmt == AV_SAMPLE_FMT_FLT)
            *type = AV_SAMPLE_FMT_FLT;
        else if(state->acodecCtx_->sample_fmt == AV_SAMPLE_FMT_DBL)
            *type = AV_SAMPLE_FMT_DBL;
        else
            return 1;
    }
    if(channels)
    {
        if(state->acodecCtx_->channel_layout == AV_CH_LAYOUT_MONO)
            *channels = AV_CH_LAYOUT_MONO;
        else if(state->acodecCtx_->channel_layout == AV_CH_LAYOUT_STEREO)
            *channels = AV_CH_LAYOUT_STEREO;
        else if(state->acodecCtx_->channel_layout == AV_CH_LAYOUT_QUAD)
            *channels = AV_CH_LAYOUT_QUAD;
        else if(state->acodecCtx_->channel_layout == AV_CH_LAYOUT_5POINT1) /* AV_CH_LAYOUT_5POINT1 - Causes corrupted audio with some mkv's */
            *channels = AV_CH_LAYOUT_5POINT1;
        else if(state->acodecCtx_->channel_layout == AV_CH_LAYOUT_5POINT1_BACK)
            *channels = AV_CH_LAYOUT_5POINT1_BACK;
        else if(state->acodecCtx_->channel_layout == AV_CH_LAYOUT_7POINT1)
            *channels = AV_CH_LAYOUT_7POINT1;
		else if(state->acodecCtx_->channel_layout == 0)
		{
			/* Unknown channel layout. Try to guess. */
			if(state->acodecCtx_->channels == 1)
				*channels = AV_CH_LAYOUT_MONO;
			else if(state->acodecCtx_->channels == 2)
				*channels = AV_CH_LAYOUT_STEREO;
			else
			{
				fprintf(stderr, "Unsupported ffmpeg raw channel count: %d\n",
						state->acodecCtx_->channels);
				return 1;
			}
		}
		else
		{
			char str[1024];
			av_get_channel_layout_string(str, sizeof(str), state->acodecCtx_->channels,
										 state->acodecCtx_->channel_layout);
			fprintf(stderr, "Unsupported ffmpeg channel layout: %s\n", str);
			return 1;
		}
	}
    if(rate) *rate = state->acodecCtx_->sample_rate;

    return 0;
}

#ifdef AUDIO_DECODING
/* Returns the current audio clock */
double ODFfmpegSource::get_audio_clock()
{
	double pts = 0;
	pts = state->audio_clock; /* maintained in the audio thread */

#if 1
	int hw_buf_size, bytes_per_sec, n;
	hw_buf_size = state->audio_buf_size - state->audio_buf_index;
	if(hw_buf_size < 0)
		printf("bad.\n");
	bytes_per_sec = 0;
	if(state->acodecCtx_)
	{
		n = state->acodecCtx_->channels * 2;
	//if(state->acodecCtx_) {
		bytes_per_sec = state->acodecCtx_->sample_rate * n;
	}
	if(bytes_per_sec) {
		pts -= (double)hw_buf_size / bytes_per_sec;
	}
#endif
	//printf("\tPTS: %f, %f", pts, state->audio_clock);
	
	return pts;
}
#endif
#ifdef AUDIO_DECODING
//unsigned int audio_buf_size = 0;
//char audio_buf[AVCODEC_MAX_AUDIO_FRAME_SIZE];
int ODFfmpegSource::getAVAudioData(char *data, int requested_buffer_size)
{
	AVPacket pkt;
    int dec = 0;
	//char *audio_buf = (char*)malloc(requested_buffer_size);
	//char audio_buf[AVCODEC_MAX_AUDIO_FRAME_SIZE];
	//unsigned int audio_buf_size = 0;

    while(dec < requested_buffer_size)
    {
        /* If there's no decoded data, find some */
        if(state->audio_buf_size == 0)
        {
            int size;
            int len;

            /* If there's no more input data, break and return what we have */
            if(audio_packet_queue_get(&(state->audioq), &pkt) < 0)
			//if(audio_packet_queue_get(&audioq, &pkt) < 0)
			{
				//printf("No packets\n");
                break;
			}
			if(pkt.data == flush_pkt.data) {
				avcodec_flush_buffers(state->acodecCtx_);
				continue;
			}
			state->audio_pkt_data = pkt.data;
			state->audio_pkt_size = pkt.size;
			/* if update, update the audio clock w/pts */
			if(pkt.pts != AV_NOPTS_VALUE)
			  state->audio_clock = av_q2d(state->audioStream_->time_base) * pkt.pts;
			  
            /* Decode some data, and check for errors */
            size = (AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2;
			//size = requested_buffer_size;
			//avcodec_decode_audio3(state->acodecCtx_, (int16_t*)audio_buf, &data_size, &pkt);
            while((len=avcodec_decode_audio3(state->acodecCtx_, (int16_t*)state->audio_buf, &size, &pkt)) == 0)
            {
                //PacketList *self;

                //if(size > 0)
                //    break;

                //self = stream->Packets;
                //stream->Packets = self->next;

                //av_free_packet(&self->pkt);
                //av_free(self);

                //if(!stream->Packets)
                //    break;
            }
            //if(!stream->Packets)
            //    continue;

            if(len < 0)
                break;

            if(len > 0)
            {
                if(len < pkt.size)
                {
                    /* Move the remaining data to the front and clear the end
                     * bits */
                    int remaining = pkt.size - len;
                    memmove(pkt.data, &pkt.data[len], remaining);
                    memset(&pkt.data[remaining], 0, pkt.size - remaining);
                    pkt.size -= len;
                }
                else
                {
                    //PacketList *self = stream->Packets;
                    //stream->Packets = self->next;

                    //av_free_packet(&self->pkt);
                    //av_free(self);
					if(pkt.data)
						av_free_packet(&pkt);
                }
            }

            /* Set the output buffer size */
            state->audio_buf_size = size;

			int n = 2 * state->acodecCtx_->channels;
			state->audio_clock += (double)state->audio_buf_size / (double)(n * state->acodecCtx_->sample_rate);
        }
		if(state->audio_buf_size > 0)
        {
            /* Get the amount of bytes remaining to be written, and clamp to
             * the amount of decoded data we have */
            size_t rem = requested_buffer_size-dec;
            if(rem > state->audio_buf_size)
                rem = state->audio_buf_size;

            /* Copy the data to the app's buffer and increment */
            memcpy(data, state->audio_buf, rem);
            data = (char*)data + rem;
            dec += (int)rem;

            /* If there's any decoded data left, move it to the front of the
             * buffer for next time */
            if(rem < state->audio_buf_size)
                memmove(state->audio_buf, &state->audio_buf[rem], state->audio_buf_size - rem);
            state->audio_buf_size -= (int)rem;
        }
    }

	//free(audio_buf);

    /* Return the number of bytes we were able to get */
    return dec;
}
#endif

#ifdef AUDIO_DECODING
/* Gets the next audio packet from the audio queue, decodes it and copies the result to the audio_buf array.
 * The current audio clock time is maintained by this method.
 * Should only be called by getAudioBuffer().
 */
int ODFfmpegSource::audio_decode_frame(char *audio_buf, int buf_size)
{
	AVPacket pkt;
	//uint8_t *audio_pkt_data = NULL;
	//int audio_pkt_size = 0;

	int len1, data_size, n;

	if(audio_packet_queue_get(&(state->audioq), &pkt) < 0) {
	//if(audio_packet_queue_get(&audioq, &pkt) < 0) {
		return -1;
	}
	if(pkt.data == flush_pkt.data) {
		avcodec_flush_buffers(state->acodecCtx_);
		return -1;
	}
	state->audio_pkt_data = pkt.data;
	state->audio_pkt_size = pkt.size;
	/* if update, update the audio clock w/pts */
	if(pkt.pts != AV_NOPTS_VALUE)
	  state->audio_clock = av_q2d(state->audioStream_->time_base) * pkt.pts;

	while(state->audio_pkt_size > 0)
	{
		data_size = buf_size;

		len1 = avcodec_decode_audio3(state->acodecCtx_, (int16_t*)audio_buf, &data_size, &pkt);
		//len1 = avcodec_decode_audio2(state->acodecCtx_, (int16_t *)audio_buf, &data_size, audio_pkt_data, audio_pkt_size);
		//len1 = avcodec_decode_audio2(aCodecCtx, (int16_t *)audio_buf, &data_size, audio_pkt_data, audio_pkt_size);
		if(len1 < 0) {
			/* if error, skip frame */
			state->audio_pkt_size = 0;
			break;
		}
		state->audio_pkt_data += len1;
		state->audio_pkt_size -= len1;
		if(data_size <= 0) {
			/* No data yet, get more frames */
			continue;
		}
		n = 2 * state->acodecCtx_->channels;
		state->audio_clock += (double)data_size / (double)(n * state->acodecCtx_->sample_rate);
		//state->audio_clock += (double)state->audio_buf_size / (double)(n * state->acodecCtx_->sample_rate);

		if(pkt.data)
			av_free_packet(&pkt);
		/* We have data, return it and come back for more later */
		return data_size;
	}

	return 0;
}
/* Fills the stream pointer with requested size in bytes of decoded audio samples.
 */
int ODFfmpegSource::getAudioBuffer(char* stream, int requested_buffer_size)
{
	int len = requested_buffer_size;
	int len1;

	//char *audio_buf = (char*)malloc(requested_buffer_size);
	//char audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
	//state->audio_buf_index = 0;
	//state->audio_buf_size = 0;

	while(len > 0)
	{
		//if(state->audio_buf_size == 0)
		{
			if(state->audio_buf_index >= state->audio_buf_size)
			{
				/* We have already sent all our data; get more */
				int audio_size = audio_decode_frame(state->audio_buf, /*AVCODEC_MAX_AUDIO_FRAME_SIZE*/(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2);
				if(audio_size < 0)
				{
					if(atVideoEnd_)
						return 0;

					/* If error, output silence */
					state->audio_buf_size = 1024; // arbitrary?
					memset(state->audio_buf, 0, state->audio_buf_size);
					//return len;
					//if(audio_size < 0)
					//	printf(":No packets - len %d - audio_size %d\n", len, audio_size);
					//return 0;
					//break;
				}
				else
				{
					state->audio_buf_size = audio_size;
				}
				state->audio_buf_index = 0;
			}
		}
		if(state->audio_buf_size - state->audio_buf_index < 0 )
			printf("bad.\n");
		//if(state->audio_buf_size > 0)
        {
			//state->audio_buf_index = 0;
			len1 = state->audio_buf_size - state->audio_buf_index;

			if(len1 > len)
				len1 = len;
			memcpy(stream, state->audio_buf + state->audio_buf_index, len1);
			stream += len1;
			len -= len1;
			state->audio_buf_index += len1;

			//if(len1 < state->audio_buf_size)
			//	memmove(state->audio_buf, &state->audio_buf[len1], state->audio_buf_size - len1);
			//state->audio_buf_size -= len1;
		}
	}
	//free(audio_buf);
	return requested_buffer_size;
}
#endif

////////////////////// CONSTRUCTION AND DESTRUCTION ////////////////////////

//SINT64 GlobalLastPresentationTime_;
SINT64 ODFfmpegSource::State::GlobalLastPresentationTime_ = AV_NOPTS_VALUE;

ODFfmpegSource::State::State(ODFfmpegSource* parent) :
    parent_(parent), formatCtx_(NULL), videoStream_(NULL), codecCtx_(NULL), 
	codec_(NULL), acodecCtx_(NULL), acodec_(NULL), decodeCtx_(NULL), 
	nativeFrame_(NULL), rawFrame_(NULL), rawFrameBuf_(NULL), 
	rawFrameNumBytes_(-1), videoStreamIndex_(-1), audioStreamIndex_(-1),
	nativeWidth_(-1), nativeHeight_(-1), desiredRawFormat_(PIX_FMT_RGB24)//, 
    //lastPresentationTime_(AV_NOPTS_VALUE)//, GlobalLastPresentationTime_(AV_NOPTS_VALUE)
{  
#ifdef LOCAL_AUDIO
	packet_queue_init(&audioq);
#endif
	packet_queue_init(&videoq);

	pictq_windex = pictq_size = pictq_rindex = audio_pkt_size = 0;
	frame_timer = frame_last_delay = frame_last_pts = 0.0;

	frame_timer_start = 0.0;

	quit = false;

	//GlobalLastPresentationTime_ = (AV_NOPTS_VALUE);

	audio_hw_buf_size = 0;
	audio_buf_size = 0;
	audio_buf_index = 0;
	audio_clock = 0;

	seek_req = 0;
	seek_flags = 0;
	seek_pos = 0;

	//pictqSemaphore = CreateSemaphore( NULL, 0 , 1, NULL);
	//pictqSemaphore = Semaphore( "pictqSemaphore" );

	audioqMutex = CreateMutex(NULL, false, NULL);
	pictqMutex = CreateMutex(NULL, false, NULL);
	pictqSemaphore = CreateSemaphore( NULL, 0 , 1, NULL);
	pictPacketqSemaphore = CreateSemaphore( NULL, 0 , 1, NULL);
	videoqMutex = CreateMutex(NULL, false, NULL);

}

ODFfmpegSource::State::~State()
{
    parent_->close();
}

ODFfmpegSource::ODFfmpegSource(const std::string& name, 
                               const std::string& settingsFileName,
                               int verbosity, const std::string& videoFile,
							   UINT32 desiredOutputFormat)
{
	this->name_ = name;
	settingsFileName;
	this->verbosity_ = verbosity;
	this->videoFileName_ = videoFile;

	av_log_set_level(AV_LOG_DEBUG);
    // Allocate internal state.
    state = new State(this);

    // Translate desired output format into something FFMPEG understands.
    meaning_ = desiredOutputFormat;
	//numChannels_ = ODImage::numChannelsForImageMeaning(meaning_);
	switch (meaning_) {
	case ODImage::odi_MONO:
	    state->desiredRawFormat_ = PIX_FMT_GRAY8;
	    break;
	case ODImage::odi_RGB:
		state->desiredRawFormat_ = PIX_FMT_RGB24;
		break;
	case ODImage::odi_BGR:
		state->desiredRawFormat_ = PIX_FMT_BGR24;
	    break;
	case ODImage::odi_RGBA:
	    state->desiredRawFormat_ = PIX_FMT_RGBA;
	    break;
	case ODImage::odi_BGRA:
	    state->desiredRawFormat_ = PIX_FMT_BGRA;
	    break;
	case ODImage::odi_ARGB:
	    state->desiredRawFormat_ = PIX_FMT_ARGB;
	    break;
	case ODImage::odi_ABGR:
	    state->desiredRawFormat_ = PIX_FMT_ABGR;
	    break;
	case ODImage::odi_YUV420:
	    state->desiredRawFormat_ = PIX_FMT_YUV420P;
	    break;
	default:
		break;
	}

	//state->desiredRawFormat_ = PIX_FMT_YUV420P;


	numChannels_ = 2;
    // For now, assume all video data types are 8-bit unsigned.
    //dataType_ = IPL_DEPTH_8U;

    // If a file name was provided, open it up the file and 
	// prepare to extract frames.
    isSetup_ = false;
    if (!(videoFileName_.empty()))
		isSetup_ = open(videoFileName_);

    return;
}

ODFfmpegSource::~ODFfmpegSource()
{
	delete state;
}

AVDictionary *opts;
bool ODFfmpegSource::open(const std::string& videoFileName)
{
#ifdef VIDEO_DECODING
	printf("VIDEO_DECODING\n");
#endif
#ifdef AUDIO_DECODING
	printf("AUDIO_DECODING\n");
#endif

    // Close old file if one is already open.
    close();

    // Set the new video file name.
    videoFileName_ = videoFileName;

	// Initialize FFMPEG if we have not already.
	if (initializedFfmpeg == false) {
		//av_register_all();
		initializedFfmpeg = true;
	}

	// Open video file and get file format info.
#if 0
	if (av_open_input_file(&(state->formatCtx_), videoFileName_.c_str(), 
		                   NULL, 0, NULL) != 0)
#else
	if ( avformat_open_input(&(state->formatCtx_), videoFileName_.c_str(), 
		                   NULL, NULL) != 0)

#endif
	{
	    printf("Could not open video file '%s'", videoFileName_.c_str());
	}

	// Get info on the various streams (e.g. audio and video) inside the file.
	// Output the info in a readable format, for debugging purposes.
	//if (av_find_stream_info(state->formatCtx_) < 0)
	if ( avformat_find_stream_info( state->formatCtx_, NULL ) < 0 )
	{
		printf("No stream information for file %s\n", videoFileName_.c_str());
#ifdef USE_ODBASE
		OD_HANDLE_ERROR("No stream information for file %s", videoFileName_.c_str());
#endif
	}

	// Show stream info.
	if (verbosity_ > 0)
#if 0
		dump_format(state->formatCtx_, 0, videoFileName_.c_str(), 0);
#else
		av_dump_format(state->formatCtx_, 0, videoFileName_.c_str(), 0);
#endif
	// Find the first video stream, and get info about its codec.
	for (UINT32 i = 0; i < state->formatCtx_->nb_streams; i++) 
	{
		//if (state->formatCtx_->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO) 
		if (state->formatCtx_->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) 
		{
			state->videoStreamIndex_ = i;
			//break;
		}
		//----------------------------------------------------------------------------
		if (state->formatCtx_->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) 
		{
			state->audioStreamIndex_ = i;
		}
		//----------------------------------------------------------------------------
	}
	if (state->videoStreamIndex_ == -1) 
	{
		// Didn't find a video stream
		printf("No video stream found in '%s'\n", 
                        videoFileName_.c_str());
	}

	//----------------------------------------------------------------------------
	if (state->audioStreamIndex_ == -1) 
	{// Didn't find a audio stream
		printf("No audio stream found in '%s'\n", 
                        videoFileName_.c_str());
	}
	//----------------------------------------------------------------------------

#ifdef VIDEO_DECODING
	if (state->videoStreamIndex_ != -1) 
	{
		state->videoStream_ = state->formatCtx_->streams[state->videoStreamIndex_];
		state->codecCtx_ = state->videoStream_->codec; //Get context for video stream
	}
#endif

#ifdef AUDIO_DECODING
	//----------------------------------------------------------------------------
	if (state->audioStreamIndex_ != -1)
	{
		state->audioStream_ = state->formatCtx_->streams[state->audioStreamIndex_];
		state->acodecCtx_ = state->audioStream_->codec;
		//state->acodecCtx_ = state->formatCtx_->streams[state->audioStreamIndex_]->codec;
		//aCodecCtx=pFormatCtx->streams[audioStream]->codec;
	}
	//----------------------------------------------------------------------------
#endif

#ifdef VIDEO_DECODING
    // Get video duration.
    duration_ = (double)(state->formatCtx_->duration) / AV_TIME_BASE;

	if (state->videoStreamIndex_ != -1) 
	{
		// Compute video frame rate.
		fps_ = (double)(state->videoStream_->r_frame_rate.num) / (double)(state->videoStream_->r_frame_rate.den);

		// Use custom memory allocation functions that propagate timestamp info.
		state->codecCtx_->get_buffer = state->allocateFrame;
		state->codecCtx_->release_buffer = state->releaseFrame;

		// Get image frame dimensions.
		state->nativeWidth_ = state->codecCtx_->width;
		state->nativeHeight_ = state->codecCtx_->height;
		width_ = state->nativeWidth_;   // Optionally set downsample factor
		height_ = state->nativeHeight_; // right here.

		// Look for an appropriate video decoder, and initialize it.
		state->codec_ = avcodec_find_decoder(state->codecCtx_->codec_id);
		if (state->codec_ == NULL)
		{
			printf("Unsupported codec %d!\n", state->codecCtx_->codec_id);
#ifdef USE_ODBASE
			OD_HANDLE_ERROR("Unsupported codec %d!", state->codecCtx_->codec_id);
#endif
		}

		//int l_thread_count = state->codecCtx_->thread_count;
		//printf("l_thread_count: %d\n", l_thread_count);
		//if( avcodec_get_context_defaults3( state->codecCtx_, state->codec_ ) < 0 )
		//{
		//	printf("Failed to get CODEC default.\n");
		//	OD_HANDLE_ERROR("Failed to get CODEC default!", state->codecCtx_->codec_id);
		//}

		//state->codecCtx_->thread_count       = 8/*MAX_THREADS*/;
		//state->codecCtx_->active_thread_type = FF_THREAD_SLICE;//FF_THREAD_FRAME;
		//if (state->codec_->capabilities & CODEC_CAP_SLICE_THREADS && state->codecCtx_->thread_type & FF_THREAD_SLICE)
		//{
		//	state->codecCtx_->active_thread_type = FF_THREAD_SLICE;
		//}
		//else if(/*!HAVE_THREADS && */!(state->codec_->capabilities & CODEC_CAP_AUTO_THREADS))
		//{
		//	state->codecCtx_->thread_count       = 1;
		//	state->codecCtx_->active_thread_type = 0;
		//	printf("Capping decoding threads.\n");
		//}

		//int frame_threading_supported = (state->codec_->capabilities & CODEC_CAP_FRAME_THREADS)
		//							&& !(state->codecCtx_->flags & CODEC_FLAG_TRUNCATED)
		//							&& !(state->codecCtx_->flags & CODEC_FLAG_LOW_DELAY)
		//							&& !(state->codecCtx_->flags2 & CODEC_FLAG2_CHUNKS);
		//if (state->codecCtx_->thread_count <= 1) // this will only be valid after call to avcodec_open2.
		//	state->codecCtx_->active_thread_type = 0;
		//else if (frame_threading_supported && (state->codecCtx_->thread_type & FF_THREAD_FRAME))
			//state->codecCtx_->active_thread_type = FF_THREAD_FRAME;
		//else
		//	state->codecCtx_->active_thread_type = FF_THREAD_SLICE;
		//state->codecCtx_->thread_count       = 0; // 0 = auto

		printf("Codec Capabilities:\n");
		switch (state->codec_->capabilities & (CODEC_CAP_FRAME_THREADS | CODEC_CAP_SLICE_THREADS | CODEC_CAP_DELAY)) {
			//case CODEC_CAP_FRAME_THREADS | CODEC_CAP_SLICE_THREADS: printf("Frame and Slice"); break;
			case CODEC_CAP_FRAME_THREADS: printf("Frame"); break;
			case CODEC_CAP_SLICE_THREADS: printf("Slice"); break;
			case CODEC_CAP_DELAY:		  printf("Cap delay"); break;
			default:                      printf("No multithreading methods supported"); break;
		}


		if(state->codec_->capabilities & CODEC_CAP_FRAME_THREADS)
			printf("Frame\n");
		if(state->codec_->capabilities & CODEC_CAP_SLICE_THREADS)
			printf("Slice\n");
		if(state->codec_->capabilities & CODEC_CAP_DELAY)
			printf("Cap delay\n");

		printf("\n");

		int thread_count = state->codecCtx_->thread_count;
		printf("thread-count before avcodec_open2: %d\n", thread_count);
		//if (!(state->codec_->capabilities & CODEC_CAP_AUTO_THREADS))
		//{
			//printf("Limiting threads\n");
			//state->codecCtx_->thread_count = 1;
		//}
		//state->codecCtx_->thread_count       = 1;

		  //av_dict_set(&opts, "threads", "auto", 0);
		  //av_dict_set(&opts, "threads", "1", 0);

		//if (avcodec_open(state->codecCtx_, state->codec_, ) < 0)
		//if ( avcodec_open2(state->codecCtx_, state->codec_, &opts) < 0)
		if ( avcodec_open2(state->codecCtx_, state->codec_, NULL) < 0)
		{
			printf("Could not initialize codec %d", state->codecCtx_->codec_id);
#ifdef USE_ODBASE
			OD_HANDLE_ERROR("Could not initialize codec %d", state->codecCtx_->codec_id);
#endif
		}

		thread_count = state->codecCtx_->thread_count;
		printf("thread-count after avcodec_open2: %d\n", thread_count);

		//state->codecCtx_->thread_count       = 1;
		//state->codecCtx_->thread_count       = 0;
		//state->codecCtx_->thread_count       = l_thread_count;
		//avcodec_flush_buffers(state->codecCtx_);


		//thread_count = state->codecCtx_->thread_count;
		//printf("thread-count: %d\n", thread_count);
	}
#endif

#ifdef AUDIO_DECODING
	//----------------------------------------------------------------------------
	if (state->audioStreamIndex_ != -1)
	{
		// Look for an appropriate audio decoder, and initialize it.
		state->acodec_ = avcodec_find_decoder(state->acodecCtx_->codec_id);
		if (state->acodec_ == NULL)
		{
			printf("Unsupported codec %d!", state->acodecCtx_->codec_id);
#ifdef USE_ODBASE
			OD_HANDLE_ERROR("Unsupported codec %d!", state->acodecCtx_->codec_id);
#endif
		}
		//if (avcodec_open(state->acodecCtx_, state->acodec_) < 0)
		//	OD_HANDLE_ERROR("Could not initialize code %d", state->acodecCtx_->codec_id);
		if ( avcodec_open2(state->acodecCtx_, state->acodec_, NULL) < 0)
		{
			printf("Could not initialize codec %d", state->acodecCtx_->codec_id);
#ifdef USE_ODBASE
			OD_HANDLE_ERROR("Could not initialize codec %d", state->acodecCtx_->codec_id);
#endif
		}
	}
	//----------------------------------------------------------------------------
#endif

#ifdef VIDEO_DECODING
	if (state->videoStreamIndex_ != -1) 
	{
		state->video_current_pts_time = av_gettime();
		state->frame_timer = av_gettime() / 1000000.0;
		state->frame_timer_start = state->frame_timer;
//		state->frame_timer = 0;
		state->frame_last_delay = 40e-3;

		// Allocate storage for video frame in its "native" (compressed) format.
		state->nativeFrame_ = avcodec_alloc_frame();
		if (state->nativeFrame_ == NULL)
		{
			printf("Failed to allocate native frame buffer.");
#ifdef USE_ODBASE
			OD_HANDLE_ERROR("Failed to allocate native frame buffer.");
#endif
		}

		// Allocate storage for uncompressed frame after conversion to RGB format.
		state->rawFrame_ = avcodec_alloc_frame();
		if (state->rawFrame_ == NULL)
		{
			printf("Failed to allocate uncompressed video frame buffer.");
#ifdef USE_ODBASE
			OD_HANDLE_ERROR("Failed to allocate uncompressed video frame buffer.");
#endif
		}
		state->rawFrameNumBytes_ = avpicture_get_size(state->desiredRawFormat_, width_, height_);
		state->rawFrameBuf_ = (uint8_t*)av_malloc(state->rawFrameNumBytes_*sizeof(uint8_t));
		if (state->rawFrameBuf_ == NULL)
		{
			printf("Failed to allocate uncompressed video frame buffer.");
#ifdef USE_ODBASE
			OD_HANDLE_ERROR("Failed to allocate uncompressed video frame buffer.");
#endif
		}
		avpicture_fill((AVPicture*)(state->rawFrame_), state->rawFrameBuf_, state->desiredRawFormat_, width_, height_);

		// Create software scaler context for converting to uncompressed imagery.
		state->decodeCtx_ =
			sws_getContext(state->nativeWidth_, state->nativeHeight_, 
						   state->codecCtx_->pix_fmt, width_, height_, 
						   state->desiredRawFormat_, SWS_BILINEAR, 
						   NULL, NULL, NULL);
		if (state->decodeCtx_ == NULL)
		{
			printf("Cannot initialize image decompression context for %s.",
							videoFileName_.c_str());
#ifdef USE_ODBASE
			OD_HANDLE_ERROR("Cannot initialize image decompression context for %s.",
							videoFileName_.c_str());
#endif
		}
	}
#endif

	av_init_packet(&flush_pkt);
	flush_pkt.data = (uint8_t*)"FLUSH";

	av_init_packet(&end_pkt);
	end_pkt.data = (uint8_t*)"END";

    // Read in the first frame, so it is available for access.
    //isSetup_ = advanceFrame();
    currFrameIndex_ = 0;  // Override incrementing that happened in advanceFrame().
	currentTime_ = 0;
	atVideoEnd_ = false;
	isSetup_ = true;
    return isSetup_;
}

void ODFfmpegSource::close()
{
    atVideoEnd_ = true;

	//if(state->codecCtx_ != NULL)
		//state->codecCtx_->thread_count = 0;

#ifdef AUDIO_DECODING
	if (state->audioStreamIndex_ >= 0)
		clearAudioPackets();
#endif
#ifdef VIDEO_DECODING
	if (state->videoStreamIndex_ >= 0)
		clearQueuedFrames();
#endif

	if (state->decodeCtx_ != NULL) {
		sws_freeContext(state->decodeCtx_);
		state->decodeCtx_ = NULL;
	}
	if (state->rawFrame_ != NULL) {
		av_free(state->rawFrameBuf_);
		av_free(state->rawFrame_);
		state->rawFrameBuf_ = NULL;
		state->rawFrame_ = NULL;
	}
	if (state->nativeFrame_ != NULL) {
		av_free(state->nativeFrame_);
		state->nativeFrame_ = NULL;
	}
	if (state->codecCtx_ != NULL) {
		avcodec_flush_buffers(state->codecCtx_);
		avcodec_close(state->codecCtx_);
		state->codecCtx_ = NULL;
	}
    // Do we need to explicitly close the AVStream videoStream_?
	if (state->acodecCtx_ != NULL) {
			avcodec_close(state->acodecCtx_);
			state->acodecCtx_ = NULL;
			//printf("CLOSING AUDIO CONTEXT\n");
	}
	if (state->formatCtx_ != NULL) {
		av_close_input_file(state->formatCtx_);
		state->formatCtx_ = NULL;
		//printf("CLOSING VIDEO CONTEXT\n");
	}
	//printf("Closing previous video source.\n");
}

//#include <windows.h>
#ifdef VIDEO_DECODING
////////////////////////////////// DECODING /////////////////////////////////
#pragma region MEMORY ALLOCATION
////////////////////////////// MEMORY ALLOCATION /////////////////////////////

// These custom memory management functions enable us to track current 
// frame time.

/*int ODFfmpegSource::allocateFrame(AVCodecContext* codecCtx, AVFrame* frame)*/
int ODFfmpegSource::State::allocateFrame(AVCodecContext* codecCtx, AVFrame* frame)
{
	// Use standard memory allocator.
	int retVal = avcodec_default_get_buffer(codecCtx, frame);
  
	// Store the last-received timestamp into this frame. Note that using
	// a global variable here prevents this code from being multi-threaded...
	SINT64* presentationTime = (SINT64*)av_malloc(sizeof(SINT64));
	*presentationTime = ODFfmpegSource::State::GlobalLastPresentationTime_;
	//*presentationTime = GlobalLastPresentationTime_;
	frame->opaque = presentationTime;
	//printf("opaque: %d - presentationTime: %d\n", *(SINT64*)frame->opaque, *presentationTime);

	return retVal;
}

/*void ODFfmpegSource::releaseFrame(AVCodecContext* codecCtx, AVFrame* frame)*/
void ODFfmpegSource::State::releaseFrame(AVCodecContext* codecCtx, AVFrame* frame)
{
	// Use the standard deallocator, but also delete the presentation time
	// data we allocated inside the opaque data member.
	if (frame && frame->opaque)
		av_freep(&(frame->opaque));
	avcodec_default_release_buffer(codecCtx, frame);
	return;
}
#pragma endregion
#pragma region SYNC
//////////////////////////////////// SYNC ///////////////////////////////////
/* Called by advanceFrame() used to correct PTS for the current frame.
 * If PTS is not 0 then set the video clock to PTS.
 * Else set PTS to the current video clock.
 */
double ODFfmpegSource::synchronize_video(double pts)
//double ODFfmpegSource::synchronize_video(AVFrame *src_frame, double pts)
{
  double frame_delay;

  if(pts != 0) {
    /* if we have pts, set video clock to it */
    state->video_clock = pts;
  } else {
    /* if we aren't given a pts, set it to the clock */
    pts = state->video_clock;
  }
  /* update the video clock */
  frame_delay = av_q2d(state->codecCtx_->time_base);
  /* if we are repeating a frame, adjust clock accordingly */
  frame_delay += state->nativeFrame_->repeat_pict * (frame_delay * 0.5);
  //frame_delay += src_frame->repeat_pict * (frame_delay * 0.5);
  state->video_clock += frame_delay;

  return pts;
}
#pragma endregion
/* Called by advanceFrame() to queue pictures on a ring buffer.
 * If the ring buffer is full then the thread calling advanceFrame() will block waiting for the ring buffer size to drop below VIDEO_PICTURE_QUEUE_SIZE.
 * Else add the next picture to the ring buffer.
 */
int ODFfmpegSource::queue_picture(AVFrame *pFrame, double pts)
{
	VideoPicture *vp;
	AVPicture pict;
#if 1
	/* wait until we have space for a new pic */
	// Accquire pictq_mutex mutex
	//SDL_LockMutex(is->pictq_mutex);
	//WaitForSingleObject(pictqMutex, INFINITE);
	//printf("Grabbing decoder mutex\n");
#ifdef USE_ODBASE
	state->pictqMutex.grab();
#else
	WaitForSingleObject(state->pictqMutex, INFINITE);
#endif
	//while(state->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE)
	while(state->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE && !state->quit)
	{
#ifdef USE_ODBASE
		state->pictqMutex.release();
#else
		ReleaseMutex(state->pictqMutex);
#endif
		//ReleaseMutex(pictqMutex);
//		printf("- Waiting for space in pictq: %d\n", state->pictq_size );
#ifdef USE_ODBASE
		state->pictqSemaphore.wait();
#else
		WaitForSingleObject(state->pictqSemaphore,INFINITE);
#endif


#ifdef USE_ODBASE
		state->pictqMutex.grab();
#else
		WaitForSingleObject(state->pictqMutex, INFINITE);
#endif
		//WaitForSingleObject(pictqSemaphore,INFINITE);
		//printf("* Filling pictq: %d\n", state->pictq_size );
		//return -1;
		// Wait
		//SDL_CondWait(is->pictq_cond, is->pictq_mutex);
	}
#ifdef USE_ODBASE
		state->pictqMutex.release();
#else
		ReleaseMutex(state->pictqMutex);
#endif
	//ReleaseMutex(pictqMutex);
	//SDL_UnlockMutex(is->pictq_mutex);
	// Release pictq_mutex mutex
#endif

#if 0
	videoDone = false;
	while(!state->quit)
	{
		//state->pictqMutex.grab();
		//int currPictq_size = state->pictq_size;
		//printf("- pictq: %d\n", currPictq_size );
		//state->pictqMutex.release();
		//if(currPictq_size >= VIDEO_PICTURE_QUEUE_SIZE)
		//{
		//	printf("- Waiting for space in pictq: %d\n", currPictq_size );
		//	state->pictqSemaphore.wait();
		//	state->pictqMutex.grab();
		//	int currPictq_size = state->pictq_size;
		//	printf("- pictq after: %d\n", currPictq_size );
		//	state->pictqMutex.release();
		//}
		//else
		//	break;

		state->pictqMutex.grab();
		int currPictq_size = state->pictq_size;
		//printf("- pictq: %d\n", currPictq_size );
		state->pictqMutex.release();
		if(currPictq_size >= VIDEO_PICTURE_QUEUE_SIZE)
		{
			if(!videoDone)
			{
				printf("- Waiting for space in pictq: %d\n", currPictq_size );
				Sleep(10);
			}
			else
			{
				//printf("- pictq after: %d\n", currPictq_size );
				break;
			}
		}
		else
			break;
	}
#endif


	//state->pictqMutex.grab();
	//int currPictq_size = state->pictq_size;
	//state->pictqMutex.release();
	//printf("- Space in pictq %s - l:  %d\n", videoFileName_.c_str(), currPictq_size );
	//if(currPictq_size < 0)// if(currPictq_size == -100) - this is bad because of race conditions - if the picture queue size 
							//								is set to -100 right before it is incremented by the code at the end
							//								of this function then the result is -99 it will not be detected and the thread
							//								will block.
	//printf("- Space in pictq %s - l:  %d\n", videoFileName_.c_str(), state->pictq_size );
	//printf("- Space in pictq:  %d\n", state->pictq_size );
	if(state->quit)
	{
		printf("---Shutdown frame stream %s.\n", videoFileName_.c_str());
		//state->pictq_size = 0;
		atVideoEnd_ = true;
		return -1;
	}
	else
	{
		// windex is set to 0 initially
		vp = &state->pictq[state->pictq_windex];
		//printf("Decoder Index: %d\n", state->pictq_windex);

		/* allocate or resize the buffer! */
		if(!vp->pictFrame || vp->width != state->codecCtx_->width || vp->height != state->codecCtx_->height)
		{
				/* wait until we have a picture allocated */
				vp->pictFrame = avcodec_alloc_frame();
				if (vp->pictFrame == NULL)
					printf("Failed to allocate uncompressed video frame buffer.");
				int rawFrameNumBytes_ = avpicture_get_size(state->desiredRawFormat_, state->codecCtx_->width, state->codecCtx_->height); //RGB!!!!
				uint8_t *rawFrameBuf_ = (uint8_t*)av_malloc(rawFrameNumBytes_*sizeof(uint8_t));
				if (rawFrameBuf_ == NULL)
					printf("Failed to allocate uncompressed video frame buffer.");
				avpicture_fill((AVPicture*)(vp->pictFrame), rawFrameBuf_, state->desiredRawFormat_, state->codecCtx_->width, state->codecCtx_->height);

				//av_free(rawFrameBuf_);

				//printf("Allocated uncompressed video frame buffer.\n");

				vp->width = state->codecCtx_->width;
				vp->height = state->codecCtx_->height;
		}
		/* We have a place to put our picture on the queue */
		/* If we are skipping a frame, do we set this to null 
		but still return vp->allocated = 1? */

		if(vp->pictFrame)
		//if(vp->bmp)
		{
			/* point pict at the queue */
			pict.data[0] = vp->pictFrame->data[0];
			pict.data[1] = vp->pictFrame->data[1];
			pict.data[2] = vp->pictFrame->data[2];

			pict.linesize[0] = vp->pictFrame->linesize[0];
			pict.linesize[1] = vp->pictFrame->linesize[1];
			pict.linesize[2] = vp->pictFrame->linesize[2];

			// Convert the image into YUV format that SDL uses
			//img_convert(&pict, dst_pix_fmt,
			//	(AVPicture *)pFrame, is->video_st->codec->pix_fmt, 
			//	is->video_st->codec->width, is->video_st->codec->height);


//printf("/// INIT TIMER ///\n");
//double sws_scaleStartTime = (av_gettime() /*/ 1000000.0 */ );


			//sws_scale(is->decodeCtx_, is->nativeFrame_->data, is->nativeFrame_->linesize, 0, is->nativeHeight_, is->rawFrame_->data, is->rawFrame_->linesize);
			sws_scale(state->decodeCtx_, ((AVPicture *)pFrame)->data, ((AVPicture *)pFrame)->linesize, 0, (pFrame)->height, 
				pict.data, pict.linesize);

//double sws_scaleTime = ( av_gettime() /*/ 1000000.0*/ );
//printf("sws_scale Time: %09.6f\n", sws_scaleTime  - sws_scaleStartTime);// - state->frame_timer_start;);

			//SDL_UnlockYUVOverlay(vp->bmp);
			vp->pts = pts;

			/* now we inform our display thread that we have a pic ready */
			if(++state->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE)
				state->pictq_windex = 0;
			// Accquire pictq_mutex mutex
			//SDL_LockMutex(is->pictq_mutex);
			
			//printf("\tGrabbing decoder2 mutex\n");
#ifdef USE_ODBASE
			state->pictqMutex.grab();
#else
			WaitForSingleObject(state->pictqMutex, INFINITE);
#endif

			//WaitForSingleObject(pictqMutex, INFINITE);
			state->pictq_size++;
			//fprintf(stderr, "in pictq_size: %d\n", state->pictq_size);
			//printf("in %s -  pictq_size: %d\n", videoFileName_.c_str(), state->pictq_size);
			//printf("in  - pictq_size: %d\n", state->pictq_size);
			//ReleaseMutex(pictqMutex);
#ifdef USE_ODBASE
			state->pictqMutex.release();
#else
			ReleaseMutex(state->pictqMutex);
#endif
			//SDL_UnlockMutex(is->pictq_mutex);
			// Release pictq_mutex mutex
		}
	}

	return 0;
}
#endif

#if 0
bool ODFfmpegSource::advanceFrame(int numFrames)
{
    // Do not even try if we already know we are at end of video.
    if (atVideoEnd_)
		return false;

#ifdef VIDEO_DECODING
	double pts = 0;
#endif
	// For as many frames as we need to advance...
	for (int f = 0; f < numFrames; f++)
	{
		// Read uncompressed video data one packet at a time, filling up
		// the frame buffer as we go, until we finish a frame.
#ifdef VIDEO_DECODING
		int frameFinished = 0;
#endif
		AVPacket packet;
		av_init_packet(&packet);
		packet.data = NULL;
		packet.size = 0;

#if 1
#ifdef AUDIO_DECODING
		if(state->audioq.size > MAX_AUDIOQ_SIZE)// || state->videoq.size > MAX_VIDEOQ_SIZE)
			return true; //return false;
#endif
#endif
#if 0
#ifdef VIDEO_DECODING
		if(state->videoq.size > MAX_VIDEOQ_SIZE)
			return true;
#endif
#endif
#ifdef AUDIO_DECODING
#ifndef VIDEO_DECODING
		if(state->audioq.size > MAX_AUDIOQ_SIZE)
			return false;

		// Read uncompressed video data one packet at a time, filling up
		// the frame buffer as we go, until we finish a frame.
		//while ((av_read_frame(state->formatCtx_, &packet) >= 0))
		//if ((av_read_frame(state->formatCtx_, &packet) >= 0))
		int advan = av_read_frame(state->formatCtx_, &packet);
		if(advan >= 0)
#endif
#endif
#ifdef VIDEO_DECODING
		//while (avpkt.size > 0 || (!pkt && ist->next_pts != ist->pts)) {
		while (!frameFinished && (av_read_frame(state->formatCtx_, &packet) >= 0))
#endif
		{
#ifdef VIDEO_DECODING
			// For many video formats, the presentation time stamp (pts) is the
			// same as the packet decoding timestamp (dts). We will try to use
			// pts = dts below, but when dts is missing (for some formats),
			// we fall back to the packet presentation timestamp, which we
			// cache in a global here. The value of this global is stuffed into
			// each decoded packet inside our custom memory allocation function.
			ODFfmpegSource::State::GlobalLastPresentationTime_ = packet.pts;
			//printf("packet.pts: %d\n", packet.pts);
			//GlobalLastPresentationTime_ = packet.pts;
#endif

			if(state->seek_req == 1)
			{
#if 1
				int64_t seek_target = state->seek_pos;
				int64_t seek_min= state->seek_rel > 0 ? seek_target - state->seek_rel + 2: _I64_MIN/*INT64_MIN*/;
				int64_t seek_max= state->seek_rel < 0 ? seek_target - state->seek_rel - 2: _I64_MAX/*INT64_MAX*/;
				//FIXME the +-2 is due to rounding being not done in the correct direction in generation
				//      of the seek_pos/seek_rel variables

				int ret = avformat_seek_file(state->formatCtx_, -1, seek_min, seek_target, seek_max, state->seek_flags);
				if(ret < 0)
				{
					fprintf(stderr, "%s: error while seeking\n", videoFileName_.c_str());
				}
				else
				{
#ifdef AUDIO_DECODING
					if(state->audioStreamIndex_ >= 0)
					{
						audio_packet_queue_flush(&(state->audioq));
						audio_packet_queue_put(&state->audioq, &flush_pkt);
						//clearAudioPackets();
					}
#endif
					if(state->videoStreamIndex_ >= 0)
					{
						packet.data = flush_pkt.data;
						clearQueuedFrames();

						//packet_queue_flush(&state->videoq);
						//packet_queue_put(&state->videoq, &flush_pkt);
					}
				}
				state->seek_req = 0;
#endif
#if 0
				int stream_index= -1;
				int64_t seek_target = state->seek_pos;

				if     (state->videoStreamIndex_ >= 0) stream_index = state->videoStreamIndex_;
				else if(state->audioStreamIndex_ >= 0) stream_index = state->audioStreamIndex_;
				//if(state->audioStreamIndex_ >= 0) stream_index = state->audioStreamIndex_;

				if(stream_index>=0)
				{
					//seek_target= av_rescale_q(seek_target, AV_TIME_BASE_Q, pFormatCtx->streams[stream_index]->time_base);
					AVRational time = {1, 1000000};
					seek_target= av_rescale_q(seek_target, time, state->videoStream_->time_base);
					//seek_target= av_rescale_q(seek_target, time, state->audioStream_->time_base);
				}
				//if(!avformat_seek_file(state->pFormatCtx, stream_index, seek_target, state->seek_flags))
				int res = av_seek_frame(state->formatCtx_, stream_index, seek_target, state->seek_flags);
				if(res < 0)
				{
					fprintf(stderr, "%s: error while seeking\n", videoFileName_.c_str());
				}
				else
				{
#ifdef AUDIO_DECODING
					if(state->audioStreamIndex_ >= 0)
					{
						//audio_packet_queue_flush(&(state->audioq));
						//audio_packet_queue_put(&state->audioq, &flush_pkt);
						clearAudioPackets();
					}
#endif
					if(state->videoStreamIndex_ >= 0)
					{
						packet.data = flush_pkt.data;
						clearQueuedFrames();
						//packet_queue_flush(&state->videoq);
						//packet_queue_put(&state->videoq, &flush_pkt);
					}
				}
				state->seek_req = 0;
#endif
			}
			if(packet.data == flush_pkt.data) {
			  avcodec_flush_buffers(state->codecCtx_);
			  continue;
			}

#ifdef AUDIO_DECODING
			if(packet.stream_index==state->audioStreamIndex_) {
				audio_packet_queue_put(&(state->audioq), &packet);
			}
#endif
#ifdef VIDEO_DECODING
	#ifdef AUDIO_DECODING
			else if (packet.stream_index == state->videoStreamIndex_)
	#else
			// If this is a video packet, decode it into "native" format.
			// (still compressed?)
			if (packet.stream_index == state->videoStreamIndex_)
	#endif
			{
				avcodec_get_frame_defaults(state->nativeFrame_);
				/*int retVal = avcodec_decode_video(state->codecCtx_, state->nativeFrame_, &frameFinished,packet.data, packet.size);*/
                int retVal = avcodec_decode_video2(state->codecCtx_, state->nativeFrame_, &frameFinished, &packet);

				/*
				SINT64 newTimestamp = (SINT64)ODFfmpegSource::UnknownTime;
				if ((packet.dts == (SINT64)AV_NOPTS_VALUE) && state->nativeFrame_->opaque && (*((SINT64*)(state->nativeFrame_->opaque)) != (SINT64)AV_NOPTS_VALUE))
					newTimestamp = *((SINT64*)(state->nativeFrame_->opaque));
				else if (packet.dts != (SINT64)AV_NOPTS_VALUE)
					newTimestamp = packet.dts;

				if (newTimestamp == ODFfmpegSource::UnknownTime)
					currentTime_ = ODFfmpegSource::UnknownTime;
				else
					currentTime_ = (double)newTimestamp * av_q2d(state->videoStream_->time_base);
				*/

				
				//static int decoder_reorder_pts = -1;
				//if (decoder_reorder_pts == -1) {
				if (state->nativeFrame_->best_effort_timestamp > 0 && state->nativeFrame_->best_effort_timestamp  != (SINT64)AV_NOPTS_VALUE) {
					currentTime_ = state->nativeFrame_->best_effort_timestamp;
					//currentTime_ = av_frame_get_best_effort_timestamp(state->nativeFrame_);
				//} else if (decoder_reorder_pts) {
				} else if (state->nativeFrame_->pkt_pts != 0 || state->nativeFrame_->pkt_pts != (SINT64)AV_NOPTS_VALUE) {
					currentTime_ = state->nativeFrame_->pkt_pts;
					//currentTime_ = state->nativeFrame_->pts;
				} else {
					currentTime_ = state->nativeFrame_->pkt_dts;
				}

				if (currentTime_ == AV_NOPTS_VALUE) {
					currentTime_ = 0;
				}
				currentTime_ *= av_q2d(state->videoStream_->time_base);
				//currentTime_ = currentTime_ - ((state->codecCtx_->thread_count-1) * 1.0 / fps_); // To compensate for 'FF_THREAD_FRAME'  - from avcodec.h - Use of FF_THREAD_FRAME will increase decoding delay by one frame per thread

				av_free_packet(&packet);
			}
#endif
			else
			{
				av_free_packet(&packet);
			}
		}
#ifdef AUDIO_DECODING
#ifndef VIDEO_DECODING
		else
		{
			if(advan == AVERROR_EOF)
			{
				atVideoEnd_ = true;
				return false;
			}
			char errbuf[128];
			av_strerror(advan, errbuf, 128);
			if(url_ferror(state->formatCtx_->pb) == 0)
				return true;
		}
#endif
#endif
#ifdef VIDEO_DECODING
		// If we somehow got out of loop without finishing a frame, we must
		// have reached end of video.
		if (!frameFinished) {
            atVideoEnd_ = true;
			return false;
		}

		// Update frame index.
		currFrameIndex_++;
	}

#if 1
	// Convert the last frame into the user's desired raw format.
	// What is the return value? Sometimes NULL is good, sometimes not. Example
	// code never checks return value.
/*	sws_scale(state->decodeCtx_, state->nativeFrame_->data, 
		      state->nativeFrame_->linesize, 0, 
			  state->nativeHeight_, state->rawFrame_->data, 
			  state->rawFrame_->linesize); */
#else
    // Deprecated, old way of doing this.
    // img_convert((AVPicture*)(state->rawFrame_), PIX_FMT_RGB24,
    //             (AVPicture*)(state->nativeFrame_), state->codecCtx_->pix_fmt,
	//	  		   state->codecCtx_->width, state->codecCtx_->height);
#endif
	// Did we get a video frame?
	//if(frameFinished)
	//{
	//	pts = synchronize_video(state->nativeFrame_, currentTime_);
		currentTime_ = synchronize_video(currentTime_);
		// Add the next frame to the queue with its updated PTS (currentTime_)
		if(queue_picture(state->nativeFrame_, currentTime_) < 0)
		{
			//printf("\nCould not queue PICTURE\n");
			//WaitForSingleObject(semaphore,INFINITE);
			//semaphore = CreateSemaphore( NULL, 0 , RINGSIZE, NULL);
			//ReleaseSemaphore(semaphore, RINGSIZE, NULL );
			return false;
			//break;

		}
	//}
#endif

	return true;
}
#endif
#ifdef VIDEO_DECODING
#pragma region SEEK
void ODFfmpegSource::stream_seek(int64_t pos, int rel)
{
    //if (!state->seek_req) {
    //    state->seek_pos = pos;
    //    state->seek_rel = rel;
    //    state->seek_flags &= ~AVSEEK_FLAG_BYTE;
    //    //if (seek_by_bytes)
    //        state->seek_flags |= AVSEEK_FLAG_BYTE;
    //    state->seek_req = 1;
    //}
	if(!state->seek_req) {
		state->seek_pos = pos;
		state->seek_flags = rel < 0 ? AVSEEK_FLAG_BACKWARD : 0;

		//state->seek_flags &= AVSEEK_FLAG_ANY;
		state->seek_req = 1;

		//state->pictqSemaphore.signal();
	}
}

void ODFfmpegSource::seek(double clock, double incr)
//void ODFfmpegSource::seek(double incr)
{
	double pos;
	pos = clock;
	//pos = get_audio_clock();//get_master_clock(global_video_state);
	printf("\nPos: %f - incr: %f\n", pos, incr);

	pos += incr;
	stream_seek((int64_t)(pos * AV_TIME_BASE), /*incr*/(int64_t)(incr * AV_TIME_BASE));
}
#pragma endregion 
/* Signal the thread that calls advanceFrame() to stop queuing frames and shutdown.
 * Release the picture queue semaphore to allow the video thread to stop blocking on the video display callback.
 */
void ODFfmpegSource::stopQueuingFrames()
{
	//state->pictqMutex.grab();
	//state->pictq_size = -100;
	state->quit = true;
	//printf("\npictq_size: %s - %d\n", videoFileName_.c_str(), state->pictq_size);
	//state->pictqMutex.release();
#ifdef USE_ODBASE
	state->pictPacketqSemaphore.signal();
#else
	ReleaseSemaphore(state->pictPacketqSemaphore, 1, NULL );
#endif

#ifdef USE_ODBASE
	state->pictqSemaphore.signal();
#else
	ReleaseSemaphore(state->pictqSemaphore, 1, NULL );
	//if(ReleaseSemaphore(pictqSemaphore, 1, NULL ))
	//	printf("Released sema...\n");
#endif
}
/* Clears the ring buffer of queued video frames.
 */
void ODFfmpegSource::clearQueuedFrames()
{
	// Only clear queued frames for a video that has been explicitly opened.
	if(state != NULL && state->formatCtx_ != NULL)
	{
		if(state->codecCtx_!= NULL)
		{
#ifdef USE_ODBASE
			state->pictqMutex.grab();
#else
			WaitForSingleObject(state->pictqMutex, INFINITE);
#endif
			printf("\nClearing video queue.\n");
			VideoPicture *vp;
			for(int i=0; i<VIDEO_PICTURE_QUEUE_SIZE; i++)
			{
				vp = &state->pictq[i];
				if(vp->pictFrame && vp->width == state->codecCtx_->width && vp->height == state->codecCtx_->height)
				// if(vp->pictFrame != NULL)
				{
					if(vp->pictFrame->data[0] != NULL)
						av_free(vp->pictFrame->data[0]);
					if(vp->pictFrame->data[1] != NULL)
						av_free(vp->pictFrame->data[1]);
					if(vp->pictFrame->data[2] != NULL)
						av_free(vp->pictFrame->data[2]);

					av_free(vp->pictFrame);
					vp->pictFrame = NULL;
				}
			}
			//state->pictq_windex = 0;
			//state->pictq_rindex = 0;
			
			state->pictq_size = 0;
			
#ifdef USE_ODBASE
			state->pictqMutex.release();
#else
			ReleaseMutex(state->pictqMutex);
#endif
		}
	}
}
#endif
#ifdef AUDIO_DECODING
/* Clears any remaining audio packets from the queue and flushes the codec buffers.
 */
void ODFfmpegSource::clearAudioPackets()
{
#ifdef USE_ODBASE
	state->audioqMutex.grab();
#else
	WaitForSingleObject(state->audioqMutex, INFINITE);
#endif
	if(state->formatCtx_ != NULL)
	{
		printf("Clearing remaining audio queue packets.\n");
		AVPacketList *pkt, *pkt1;
		for(pkt = (state->audioq).first_pkt; pkt != NULL; pkt = pkt1)
		{
			pkt1 = pkt->next;
			av_free_packet(&pkt->pkt);
			av_freep(&pkt);
		}
		(state->audioq).last_pkt = NULL;
		(state->audioq).first_pkt = NULL;
		(state->audioq).nb_packets = 0;
		(state->audioq).size = 0;

		avcodec_flush_buffers(state->acodecCtx_);
		//while(audio_packet_queue_get(&(state->audioq), &pkt) > 0)
		//{
		//    if(pkt.data)
		//		av_free_packet(&pkt);
		//}
	}
	
#ifdef USE_ODBASE
	state->audioqMutex.release();
#else
	ReleaseMutex(state->audioqMutex);
#endif
}
#endif
#if 1
//bool ODFfmpegSource::getCurrFrame(IplImage** imPtr)
//{
//    // (Re)allocate image data pointer if necessary.
//	prepareCurrFrameReturnBuf(imPtr);
//    
//    // Copy the data.
//    memcpy((*imPtr)->imageData, state->rawFrame_->data[0], 
//           state->rawFrame_->linesize[0] * height_);
//
//    return true;
//}
bool ODFfmpegSource::queuesFull()
{
	if(state->audioq.size > MAX_AUDIOQ_SIZE || state->videoq.size > MAX_VIDEOQ_SIZE || atVideoEnd_)
	{
		//Sleep(10);
		return true;
	}
	return false;
}

bool ODFfmpegSource::advanceFrame(int numFrames)
{
	AVPacket packet;
	av_init_packet(&packet);
	packet.data = NULL;
	packet.size = 0;

	if (state->quit)
		return false;
  //  if (atVideoEnd_)
		//Sleep(10);
		//return false;
		
		
		if(state->seek_req == 1)
		{
#if 1
			int64_t seek_target = state->seek_pos;
			int64_t seek_min= state->seek_rel > 0 ? seek_target - state->seek_rel + 2: _I64_MIN/*INT64_MIN*/;
			int64_t seek_max= state->seek_rel < 0 ? seek_target - state->seek_rel - 2: _I64_MAX/*INT64_MAX*/;
			//FIXME the +-2 is due to rounding being not done in the correct direction in generation
			//      of the seek_pos/seek_rel variables

			int ret = avformat_seek_file(state->formatCtx_, -1, seek_min, seek_target, seek_max, state->seek_flags);
			if(ret < 0)
			{
				fprintf(stderr, "%s: error while seeking\n", videoFileName_.c_str());
			}
			else
			{
				atVideoEnd_ = false;
#ifdef AUDIO_DECODING
				if(state->audioStreamIndex_ >= 0)
				{
					audio_packet_queue_flush(&(state->audioq));
					audio_packet_queue_put(&state->audioq, &flush_pkt);
					//clearAudioPackets();
				}
#endif
				if(state->videoStreamIndex_ >= 0)
				{
					//packet.data = flush_pkt.data;
					video_packet_queue_flush(&(state->videoq));
					vid_packet_queue_put(&state->videoq, &flush_pkt);
					//clearQueuedFrames();
					//packet_queue_flush(&state->videoq);
					//packet_queue_put(&state->videoq, &flush_pkt);
				}
			}
			state->seek_req = 0;
#endif
#if 0
				int stream_index= -1;
				int64_t seek_target = state->seek_pos;

				if     (state->videoStreamIndex_ >= 0) stream_index = state->videoStreamIndex_;
				else if(state->audioStreamIndex_ >= 0) stream_index = state->audioStreamIndex_;
				//if(state->audioStreamIndex_ >= 0) stream_index = state->audioStreamIndex_;

				if(stream_index>=0)
				{
					//seek_target= av_rescale_q(seek_target, AV_TIME_BASE_Q, pFormatCtx->streams[stream_index]->time_base);
					AVRational time = {1, 1000000};
					seek_target= av_rescale_q(seek_target, time, state->videoStream_->time_base);
					//seek_target= av_rescale_q(seek_target, time, state->audioStream_->time_base);
				}
				//if(!avformat_seek_file(state->pFormatCtx, stream_index, seek_target, state->seek_flags))
				int res = av_seek_frame(state->formatCtx_, stream_index, seek_target, state->seek_flags);
				if(res < 0)
				{
					fprintf(stderr, "%s: error while seeking\n", videoFileName_.c_str());
				}
				else
				{
#ifdef AUDIO_DECODING
					if(state->audioStreamIndex_ >= 0)
					{
						//audio_packet_queue_flush(&(state->audioq));
						//audio_packet_queue_put(&state->audioq, &flush_pkt);
						clearAudioPackets();
					}
#endif
					if(state->videoStreamIndex_ >= 0)
					{
						packet.data = flush_pkt.data;
						clearQueuedFrames();
						//packet_queue_flush(&state->videoq);
						//packet_queue_put(&state->videoq, &flush_pkt);
					}
				}
				state->seek_req = 0;
#endif
		}

	//if(state->audioq.size > MAX_AUDIOQ_SIZE || state->videoq.size > MAX_VIDEOQ_SIZE)
	//{
	//	//Sleep(10);
	//	return true;
	//}

	int advan = av_read_frame(state->formatCtx_, &packet);
	if (advan >= 0)
	{
#ifdef AUDIO_DECODING
		if(packet.stream_index==state->audioStreamIndex_)
		{
			audio_packet_queue_put(&(state->audioq), &packet);
		}
		else if (packet.stream_index == state->videoStreamIndex_)
#else
		if (packet.stream_index == state->videoStreamIndex_)
#endif
		{
			vid_packet_queue_put(&(state->videoq), &packet);
		}
		else
			av_free_packet(&packet);
	}
	else
	{
		if(advan == AVERROR_EOF)
		{
			if(!atVideoEnd_)
			{
				AVPacket empty_packet;
				av_init_packet(&empty_packet);
				for(int i=0; i<state->codecCtx_->thread_count; i++)
					vid_packet_queue_put(&state->videoq, &empty_packet);

				printf("At the end of the video.\n");
				atVideoEnd_ = true;
				vid_packet_queue_put(&state->videoq, &end_pkt);
			}
		}
		char errbuf[128];
		av_strerror(advan, errbuf, 128);
		//if(url_ferror(state->formatCtx_->pb) == 0)
		if (state->formatCtx_->pb && state->formatCtx_->pb->error)
		{
			return false;
			//Sleep(10); /* no error; wait for user input */
		}
		else
			return true;
	}

	return true;
}
bool reachedEnd = false;
int ODFfmpegSource::decodeVideoFrame()
{
    //if (atVideoEnd_)
	//	return false;

    if (atVideoEnd_ && reachedEnd)
	{
		Sleep(10);
		return true;
	}

	double pts = 0;
 
	// Read uncompressed video data one packet at a time, filling up
	// the frame buffer as we go, until we finish a frame.
	int frameFinished = 0;
	AVPacket packet;

	while (!frameFinished)
	{
		int qRet = vid_packet_queue_get(&(state->videoq), &packet);
		if(qRet < 0) //
		{
			//return -1;
			return false;
			//continue;
		}
		//else if (qRet == 0) //Return -1 to signal that the queue is currently empty and that the calling thread should sleep.
		//{
		//	//Sleep(10);
		//	return -1;
		//	//printf("videoq pkq NULL - queue empty\n");
		//	//continue;
		//}
		if(packet.data == flush_pkt.data) {
			printf("Videoq pkq = FLUSH\n");
			clearQueuedFrames();
			avcodec_flush_buffers(state->codecCtx_);
			//continue;
			//av_free_packet(&packet);
			return true;
		}

		if(packet.data == end_pkt.data) {
			printf("Videoq pkq = END\n");

			reachedEnd = true;
			return true;
			//clearQueuedFrames();
			//avcodec_flush_buffers(state->codecCtx_);

#ifdef USE_ODBASE
				state->pictqMutex.grab();
#else
				WaitForSingleObject(state->pictqMutex, INFINITE);
#endif
				while(/*state->pictq_size != 0 &&*/ !state->quit)
				{
					printf("Waiting for video queue to empty.\n");
#ifdef USE_ODBASE
					state->pictqMutex.release();
#else
					ReleaseMutex(state->pictqMutex);
#endif
					Sleep(10);
#ifdef USE_ODBASE
					state->pictqMutex.grab();
#else
					WaitForSingleObject(state->pictqMutex, INFINITE);
#endif				
				}
#ifdef USE_ODBASE
				state->pictqMutex.release();
#else
				ReleaseMutex(state->pictqMutex);
#endif
				state->quit = true;

			return false;
		}
		
		ODFfmpegSource::State::GlobalLastPresentationTime_ = packet.pts;
		
		avcodec_get_frame_defaults(state->nativeFrame_);
		/*int retVal = avcodec_decode_video(state->codecCtx_, state->nativeFrame_, &frameFinished,packet.data, packet.size);*/
		int retVal = avcodec_decode_video2(state->codecCtx_, state->nativeFrame_, &frameFinished, &packet);

#if 0 // Returns incorrect pts
		SINT64 newTimestamp = (SINT64)ODFfmpegSource::UnknownTime;
		if ((packet.dts == (SINT64)AV_NOPTS_VALUE) && state->nativeFrame_->opaque && 
			(*((SINT64*)(state->nativeFrame_->opaque)) != (SINT64)AV_NOPTS_VALUE))
			newTimestamp = *((SINT64*)(state->nativeFrame_->opaque));
		else if (packet.dts != (SINT64)AV_NOPTS_VALUE)
			newTimestamp = packet.dts;
		if (newTimestamp == ODFfmpegSource::UnknownTime)
			//currentTime_ = ODFfmpegSource::UnknownTime;
			pts = ODFfmpegSource::UnknownTime;
		else
			//currentTime_ = (double)newTimestamp * av_q2d(state->videoStream_->time_base);
			pts = (double)newTimestamp * av_q2d(state->videoStream_->time_base);
#endif
		//currentTime_ = currentTime_ - ((state->codecCtx_->thread_count-1) * 1.0 / fps_); // To compensate for 'FF_THREAD_FRAME'  - from avcodec.h - Use of FF_THREAD_FRAME will increase decoding delay by one frame per thread
		//if(currFrameIndex_ < int(fps_ * duration_)-(state->codecCtx_->thread_count-1))
		//	pts = pts - ((state->codecCtx_->thread_count-1) * 1.0 / fps_); // To compensate for 'FF_THREAD_FRAME'  - from avcodec.h - Use of FF_THREAD_FRAME will increase decoding delay by one frame per thread

#if 1
		//static int decoder_reorder_pts = -1;
		//if (decoder_reorder_pts == -1) {
		//if (state->nativeFrame_->best_effort_timestamp != 0 || state->nativeFrame_->best_effort_timestamp  != (SINT64)AV_NOPTS_VALUE) {
		if (state->nativeFrame_->best_effort_timestamp > 0 && state->nativeFrame_->best_effort_timestamp  != (SINT64)AV_NOPTS_VALUE) {
			pts = state->nativeFrame_->best_effort_timestamp;
			//currentTime_ = av_frame_get_best_effort_timestamp(state->nativeFrame_);
		//} else if (decoder_reorder_pts) {
		} else if (state->nativeFrame_->pkt_pts != 0 || state->nativeFrame_->pkt_pts != (SINT64)AV_NOPTS_VALUE) {
			pts = state->nativeFrame_->pkt_pts;
			//currentTime_ = state->nativeFrame_->pts;
		} else {
			pts = state->nativeFrame_->pkt_dts;
		}

		if (pts == AV_NOPTS_VALUE) {
			pts = 0;
		}
		pts *= av_q2d(state->videoStream_->time_base);
#endif
		//printf("pts: %f\n", pts);
		av_free_packet(&packet);
	}

	// Did we get a video frame?
	if(frameFinished)
	{
		// Update frame index.
		currFrameIndex_++;
		printf("pts: %f - currFrameIndex_: %d\n", pts, currFrameIndex_);
		//pts = synchronize_video(state->nativeFrame_, currentTime_);
		pts = synchronize_video(pts);
		//if(queue_picture(state->rawFrame_, pts) < 0)
		//if(queue_picture(state->nativeFrame_, pts) < 0)
		if(queue_picture(state->nativeFrame_, pts) < 0)
		{
			printf("\nCould not queue PICTURE\n");
			//WaitForSingleObject(semaphore,INFINITE);
			//semaphore = CreateSemaphore( NULL, 0 , RINGSIZE, NULL);
			//ReleaseSemaphore(semaphore, RINGSIZE, NULL );
			//Sleep(1000);
			return false;
			//break;

		}
	}
	//av_free(state->nativeFrame_);

	return true;
}
#endif