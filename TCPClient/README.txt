TCPClient:

-----------------------------------OVERVIEW-----------------------------------

Generic audio and video playback engine.

2 modes of operation:
 - Local audio playback.
 - Networked audio playback.

The audio clock is always the master time keeper - video framerate is constantly adjusted to keep in sync. (other media players tend to sync audio to video by adjusting the sample rate)

Local Playback-:
Both the audio and video data is decoded on the local machine and synchronized.

Networked Playback-:

Server:
 - Responsible for playback of specified audio track (either the embedded audio of accompanying video or a completely seperate file)
 - Timecodes are transmitted back to client upon request or broadcasted at regular intervals.
 
Networked sequence of events:
 - Client selects which video they would like to play.
 - The directory path to the file on disk or network location is sent to the server.
 - Server and client begin playback immmediately.
 - Timecodes are then transmitted (in plaintext) from the server to all clients/specific client depending on whether broadcast mode is enabled.
 - Client keeps track of its own clock and the difference between its local time and server time.
 - This new clock is then used to calculate the delays between each subsequent frame.
 

In networked mode the player will also listen for incoming video commands (start, stop, pause, seek) from the server.
Video player commands are routed to the server so that they can be distributed to all connected clients.  This feature is useful to for either tiling the current video or spliting rendering of a single video frame across all displays.


------------------------------VS2008 SP1 PROJECT------------------------------

The visual studio project contains 2 main build profiles.

LOCAL_AUDIO (Debug_LOCAL_AUDIO/Release_LOCAL_AUDIO): Local profile includes OpenAL for local audio playback.
NETWORKED_AUDIO (Debug/Release): Stripped down version removing unnecessary audio packages and headers. Video playback only.

Default library includes: OpenGL, glew, FFmpeg (ffmpeg-git-ceb0dd9-win64-dev - 09-Jan-2012)
Fonts: Freetype GL (if glew included) otherwise standard windows wglUseFontBitmaps (extremely slow)
Audio: OpenAL Soft 1.13
Networking: Openframeworks (via ODNetwork header)
Synchronization: ODBase (for locks/semaphores) OR native windows mutexs and semaphores.

Key mappings:

'F1':					Toggle debug information.
'L':					Load video (opens windows file dialog).
'R':					Reload video. (Stop then play)
'S':					Stop current video.
' '(Spacebar):			Pause video.
'M':					Mute audio.
'E':					Shutdown video player.
'Z':					Decrease volume (5%).
'X':					Increase volume (5%).
'T':					Toggle test pattern.
'VK_ADD':				Increment video unit.
'VK_UP':				Increase global delay (small).
'VK_DOWN':				Decrease global delay (small).
'VK_LEFT':				Increase global delay (large).
'VK_RIGHT':				Decrease global delay (large).
(SHIFT) & 'VK_UP':		Seek forward (small).
(SHIFT) & 'VK_DOWN':	Seek backward (small).
(SHIFT) & 'VK_LEFT':	Seek backward (large).
(SHIFT) & 'VK_RIGHT':	Seek forward (large).
'1':					Background color (blue).
'2':					Background color (red).
'3':					Background color (black).

Command line arguments:

Usage:  [-ServerIP xxx.xxx.xxx.xxx][-TCPPort n][-UDPPort n][-ClientID n][-StreamID n][-Res <w>x<h>][-Position <w>x<h>]
        [-VideoPath path][-AutoStart 'true'|'false'][-Vsync 'true'|'false'][-TestLatency 'true'|'false']
        [-Debug 'true'|'false'][-Affinity n][-Loop 'true'|'false'][-Loop 'true'|'false'][-ScreenSync 'true'|'false']
        [-IntelliSleep 'true'|'false'][-EnablePBOs 'true'|'false'][-MutliTimer 'true'|'false'][-preloadAudio 'true'|'false']
        [-PreloadAudio 'true'|'false'][-BufferSize n(B)]

  -ServerIP:     Specify the server IP address.
  -TCPPort:      Specify the server TCP port.
  -UDPPort:      Specify the server UDP port.
  -ClientID:     Specify the client ID number.
  -StreamID:     Specify the client stream number.
  -Res:          Specify the window width and height.
  -Position:     Specify the window position.
  -VideoPath:    Path to video.
  -AutoStart:    Automatically start video. Either 'true' or 'false'.
  -VSync:        Enable vsync. Either 'true' or 'false'.
  -TestLatency:  Test the network latency. Either 'true' or 'false'.
  -Debug:        Enable debug output. Either 'true' or 'false'.
  -Affinity:     Set FFmpeg multi core count. Decimal representation of bit mask.
  -Loop:         Loop video. Either 'true' or 'false'.
  -ScreenSync:   Sync all video streams to a single source. Either 'true' or 'false'.
  -EnablePBOs:   Enable pbos if supported. Either 'true' or 'false'.
  -IntelliSleep: Enable intelligent sleep to reduce cpu resources. Either 'true' or 'false'.
  -MutliTimer:   Enable multimedia timer callbacks instead of the alternative busy wait sleep to reduce cpu resources. Either 'true' or 'false'.
  -PreloadAudio: Preload audio buffers (only works with local audio playback). Either 'true' or 'false'.
  -BufferSize:   Set OpenAL buffer size in bytes (only works with local audio playback).

-----------------------------------SOURCE-------------------------------------

Constants.h
MAXSTREAMS: Number of video streams enabled (supports up to 4).  The media player can play mutliple video files simultaneously within the current window.

 _________ 		 _________ 		 _________ 
|         |		|    0    |  	| 0  |  1 |
|    0    |		|_________|		|____|____|
|		  |		|	 1	  |		| 2	 |  3 |
|_________| 	|_________| 	|____|____| 

Main.cpp:

Main OpenGL context.
Single context is used for all GL operations.

Read thread:
advanceFrame

Video decoding thread:
decodeVideoFrame

Audio decoding thread:
getAudioBuffer or getAVAudioData


 ________ audio  _______      ________
|        | pkts |       |    |        | to spkr
|  READ  |----->| AUDIO |--->| OpenAL |-->
|________|      |_______|    |________|
    |  video     _______		 _______
    |   pkts    |       |		|       |
    +---------->| VIDEO |------>|  PBO	|
				|_______|		|_______|
________           |				|		 __________
|       |          |       			|		|          |
| MAIN  |          |				+------>| VIDEO    | to mon.
| LOOP  |		   +----------------------->| PLAYBACK |-->wglSwapBuffers
|_______|-------------BUSY-WAIT------------>|__________|



NetClockSync:
  
Either 1 of 2 options in regards to receiving clock times. Either the client requests a new clock time or the server simply broadcasts the new time at set intervals and the client listens and up dates his clock accordingly.
All clock data is sent via UDP.
Player commands via TCP to ensure delivery.
Clock information is packaged as a custom string representing the Client ID, Stream ID and current clock time.

VideoPlayback:

Main video utility class. Deals with timing.

Uploads frames to video texture memory.

Either 1 of 2 options in regards to playback.  Busy-wait loop or using windows multimedia timer to enable blocking callbacks (helps reduce cpu resources).

Checkmessages: uses a standard queue with locks to record commands - the reason why this is checked in the main loop is because we don't want to modify any OpenGL without being on the main OpenGL context thread.

Pbo:

----------------------------------LIMITATIONS---------------------------------
1. Lots!
2. This whole thing needs to be re-written in a OOP fashion. Like completely - its currently written a very crappy procedural notation to adhear to a api interface from a different project.
3. Bugs - as far as the eye can see!
		- in the process of adding a pbo array I introduced many temporary hacks to figure out issues i was noticing with the frame rendering pipeline.
4. The project needs to be rewritten to make it more asynchronous - decode frames directly in the pbo arrary for example.

Bugs-:
Multimedia timers with pbos currently doesn't work.		
		  

----------------------------------REFERENCES----------------------------------
http://dranger.com/ffmpeg/ffmpegtutorial_all.html