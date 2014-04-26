#ifndef OD_FFMPEG_SOURCE
#define OD_FFMPEG_SOURCE

// ODFfmpegSource.h
// For extracting video from files using Ffmpeg interface.
// Works for a wide variety of video codecs and container
// fomats, including MPEG, WMV, h.264, Xvid, Quicktime, etc.
// Authors: Michael Harville, Apr 2009, Shaun O'Neill, Mar 2012

//#define AUDIO_DECODING
//#define VIDEO_DECODING
//#define NETWORKED_AUDIO

#ifndef __STDC_CONSTANT_MACROS
// For the Ffpmeg package.
#define __STDC_CONSTANT_MACROS   // Prevent INT64_C compile errors in VS2005
#endif
#pragma warning(disable : 4244)
extern "C" 
{
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
    #include "libavutil/avutil.h"
    #include "libswscale/swscale.h"
};

#pragma comment( lib, "avcodec.lib" )
#pragma comment( lib, "avdevice.lib" )
#pragma comment( lib, "avformat.lib" )
#pragma comment( lib, "avutil.lib" )
#pragma comment( lib, "swscale.lib" )

#ifdef USE_ODBASE
// Basic types and classes.
#include "ODBase.h"
#endif

#include <string>
using namespace std;

typedef unsigned int        UINT32, *PUINT32;
typedef double              FLOAT64;
typedef unsigned char       UINT8, *PUINT8;
typedef __int64             SINT64;

namespace ODImage
{
//////////////////////////////// IMAGE FORMATS ///////////////////////////////

// Flags describing the "meaning" of the image data channels. IplImage and
// OpenCV do not appear to have such standard definitions, so we create them.
// We currently store these flags in the "alphaChannel" member of IplImage,
// which is not used by OpenCV. Still, this is a little ugly, so try to
// use the functions and macros below, to keep "alphaChannel" from
// appearing in your code.
//
// Note: The flags below do NOT specify the bit depth or type of image
// data. These are described by the "depth" member of IplImage.
const UINT32 odi_Unknown= 0x00000001;   // Unknown.
const UINT32 odi_MONO   = 0x00000002;   // Single channel.
const UINT32 odi_RGB    = 0x00000004;   // 3 color channels, in this order.
const UINT32 odi_BGR    = 0x00000008;
const UINT32 odi_RGBA   = 0x00000010;   // 3 color channels plus alpha, in this order.
const UINT32 odi_BGRA   = 0x00000020;
const UINT32 odi_ARGB   = 0x00000040;
const UINT32 odi_ABGR   = 0x00000080;
const UINT32 odi_HSV    = 0x00000100;   // Hue-Saturation-Value.
const UINT32 odi_HLS    = 0x00000200;   // Hue-Lightness-Saturation.
const UINT32 odi_YUV444 = 0x00000400;   // aka YUV444: luminance and two chroma channels.
const UINT32 odi_YUV422 = 0x00000800;   // aka YUV422: chroma channels are at half-res.
const UINT32 odi_YUV420 = 0x00001000;
const UINT32 odi_LAB    = 0x00002000;   // Perceptual color spaces.
const UINT32 odi_XYZ    = 0x00004000;
const UINT32 odi_RGGB   = 0x00008000;   // Bayer patterns, with all four options for which
const UINT32 odi_BGGR   = 0x00010000;   // type of pixel is in upper-left image corner.
const UINT32 odi_GRBG   = 0x00020000;
const UINT32 odi_GBRG   = 0x00040000;
}

typedef struct PacketQueue {
	  AVPacketList *first_pkt, *last_pkt;
	  int nb_packets;
	  int size;
} PacketQueue;

struct ODFfmpegSource
{
	//////////////////// Data Members //////////////////////

    // See ODVideoSource for inherited members.

    // Name of video file being read.
    std::string videoFileName_;

    // Total length of video, in seconds.
    FLOAT64 duration_;

    // Index of current frame, starting at zero at beginning of file. 
    // Not reliable if "gotFirstFrame_" is false.
    UINT32 currFrameIndex_;

    // Flag indicating that we have reached end of video, and advanceFrame()
    // will not work any more.
    bool atVideoEnd_;

	//////////////////// Data Members //////////////////////

    // A descriptive name for the video source (e.g. a camera name, a video
    // file name, etc.)
    std::string name_;

    // Indicates whether video source is functioning. If constructor or
    // initialization fails, this should be false.
    bool isSetup_;

    // Control verbosity of debug printfs. Convention is zero for no output,
    // and higher numbers for more output.
    int verbosity_;

    // Format of images frames provided. See ODImage.h.
    UINT32 width_;
    UINT32 height_;
    UINT8 numChannels_;
    UINT8 dataType_;   // Uses one of the IPL_DEPTH_* constants
    UINT32 meaning_;   // Uses one of the ODImage format flags.

	// Timestamp associated with current frame, relative to beginning
    // of stream.
    // 
    // For some video sources or files, it may not always be possible
    // to determine this timing information. In such cases, the
    // special value of "UnknownTime" is returned.
	double currentTime_;
	static const double UnknownTime;

    // Frame rate of video. If source does not set it, or frame rate
    // is unknown, the special vaue of "UnknownFPS" is returned.
    double fps_;
    static const double UnknownFPS;

	////////// Source construction / destruction ///////////

    // Source construction / destruction.
	ODFfmpegSource(const std::string& name = "unnamed video source",
                   const std::string& settingsFileName = "",
                   int verbosity = 0,
		           const std::string& videoFile = "",
				   UINT32 desiredOutputFormat = ODImage::odi_RGB);
	~ODFfmpegSource();

    //////////////////// Other methods //////////////////////

    // Open a particular file. Allows instance of object to be re-used
    // for multiple files if desired. Or, you can just pass a file name
    // into the constructor.
	bool open(const std::string& videoFile);

    // Free up all resources associated with current file being read. 
    void close();

    // Get current video frame. This will return the same frame repeatedly
    // until advanceFrame() is called.
    //
    // This function does not return a handle to the internal
    // image buffer. Instead, the behavior is:
    // -- If *imPtr is NULL, allocate a new image and copy data into it.
    // -- If *imPtr is not NULL and is correct size, copy data into it.
    // -- If *imPtr is not NULL but wrong size, dealloc it and return
    //    a new image with the correct dimensions.
    //
    // Returns true if successful, false if not.
	//bool getCurrFrame(IplImage** imPtr);

    // Move forward "numFrames" in the video.
    bool advanceFrame(int numFrames = 1);


	bool decodeVideoFrame();
	int vid_packet_queue_put(PacketQueue *q, AVPacket *pkt);
	int vid_packet_queue_get(PacketQueue *q, AVPacket *pkt);

	////////////////////////////////// AUDIO  /////////////////////////////////

	int getAVAudioInfo(unsigned int *rate, int *channels, int *type);
	int audio_decode_frame(char *audio_buf, int buf_size);
	int getAudioBuffer(char* stream, int requested_buffer_size);
	double get_audio_clock();
	void clearAudioPackets();
	int getAVAudioData(char *data, int requested_buffer_size);

	void video_packet_queue_flush(PacketQueue *q);
	void audio_packet_queue_flush(PacketQueue *q);
	int audio_packet_queue_put(PacketQueue *q, AVPacket *pkt);
	int audio_packet_queue_get(PacketQueue *q, AVPacket *pkt);

	////////////////////////////////// VIDEO /////////////////////////////////
	double synchronize_video(double pts);
	int queue_picture(AVFrame *pFrame, double pts);
#ifdef VIDEO_DECODING
	#ifdef NETWORKED_AUDIO
		double video_refresh_timer(double netClock, double pauseLength, char *dataBuff);
	#else
		double video_refresh_timer(double openALAudioClock, double pauseLength, char *dataBuff);
	#endif
#endif
	//double video_refresh_timer_pbo(double netClock, double pauseLength, char *dataBuff);
	double video_refresh_timer_pbo(double openALAudioClock, double pauseLength, char *dataBuff, double ptsClk);
	int video_refresh_timer_next(char *dataBuff, int pboIndex, double& pboPTS);

	void seek(double clock, double incr);
	//void seek(double pos);
	void stream_seek(int64_t pos, int rel);
	void stopQueuingFrames();
    void clearQueuedFrames();
	bool queuesFull();

	double video_pts();

	//bool quit();
	void togglePause();

    // Hidden internal state.
    struct State;
    State* state;
};

#endif
