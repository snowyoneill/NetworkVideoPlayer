#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 4996)
#endif

#include "main.h"
#include "windowsglskeleton.h"
#include "constants.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <Commdlg.h>
#include <math.h>

#ifdef USE_CIMAGE
#include <atlimage.h> // CImage
#endif

#include <string>
using namespace std;
//#include "psapi.h"
//#pragma comment( lib, "Psapi.lib" )

#ifdef USE_ODBASE
#include "ODBase.h"
#else
#include "shlwapi.h" // PathFileExists
#endif

// ----------------------------Fonts------------------------------
#ifdef USE_GLEW
#define USE_FREETYPE_FONTS
#else
#define WGL_FONTS
#endif

#ifdef USE_FREETYPE_FONTS
	#include "freetype-gl.h"
	#include "vertex-buffer.h"
#endif

#ifdef WGL_FONTS
	#include "glfont.h"
	GLFont* Font;
#endif
// ---------------------------------------------------------------

extern int pboMode;
extern GLuint videotextures[MAXSTREAMS];
//extern void notifyScreenSyncLoadVideo(string videoName, int videounit);
void displayHelp();

#ifdef LOCAL_AUDIO
extern bool stopAudioThread;
#endif
#ifdef LOG
extern FILE *videoDebugLogFile;
#endif

int		core;
int		fbowidth, fboheight, debug, width, height;
GLuint	fbo,framebuffer, stencil, background;
char	videoName[MAX_PATH];
//char *videoName;
#ifdef USE_CIMAGE
CImage Image;
#endif
GLuint bgTexture;

//int stream				= 0;
bool backgroundEnabled	= false;
bool enableVSync		= false;
bool enableDebugOutput	= false;
bool help				= true;
bool loopvideo			= false;
bool screenSync			= false;
int currentVideo		= 0;
bool fullScreenVideo	= false;
bool enablePBOs			= true;
bool intelliSleep		= false;
bool multiTimer			= true;
bool g_preloadAudio		= false;
int g_bufferSize		= -1;
float volume			= 1.0f;
float seekDur			= 0.0f;
bool testPattern		= false;

int		currFPS			 = 0;
int		mouse_x			 = 0, mouse_y = 0;
int		rmouse_x		 = -1, rmouse_y = -1;
bool	muted[MAXSTREAMS];
double	g_Delay[MAXSTREAMS];

#ifdef USE_FREETYPE_FONTS
// --------------------------------------------------------------- add_text ---
texture_atlas_t *atlas;
vertex_buffer_t *text_buffer;
vertex_buffer_t *text_buffer2;
texture_font_t *font;

typedef struct {
    float x, y, z;    // position
    float s, t;       // texture
    float r, g, b, a; // color
} vertex_t;

/* Add freetype text to vertex buffer.
 */
void freetype_add_text( vertex_buffer_t * buffer, texture_font_t * font,
               wchar_t * text, vec4 * color, vec2 * pen )
{
    size_t i;
    float r = color->red, g = color->green, b = color->blue, a = color->alpha;
    for( i=0; i<wcslen(text); ++i )
    {
        texture_glyph_t *glyph = texture_font_get_glyph( font, text[i] );
        if( glyph != NULL )
        {
            int kerning = 0;
            if( i > 0)
            {
                kerning = texture_glyph_get_kerning( glyph, text[i-1] );
            }
            pen->x += kerning;
            int x0  = (int)( pen->x + glyph->offset_x );
            int y0  = (int)( pen->y - glyph->offset_y );
            int x1  = (int)( x0 + glyph->width );
            int y1  = (int)( y0 + glyph->height );
            float s0 = glyph->s0;
            float t0 = glyph->t0;
            float s1 = glyph->s1;
            float t1 = glyph->t1;
            //GLuint indices[6] = {0,1,2, 0,2,3};
            GLuint index = (GLuint)buffer->vertices->size;
            GLuint indices[] = {index, index+1, index+2, index, index+2, index+3};
            vertex_t vertices[4] = { { x0,y0,0,  s0,t0,  r,g,b,a },
                                     { x0,y1,0,  s0,t1,  r,g,b,a },
                                     { x1,y1,0,  s1,t1,  r,g,b,a },
                                     { x1,y0,0,  s1,t0,  r,g,b,a } };
            //vertex_buffer_push_back( buffer, vertices, 4, indices, 6 );

            vertex_buffer_push_back_indices( buffer, indices, 6 );
            vertex_buffer_push_back_vertices( buffer, vertices, 4 );
            pen->x += glyph->advance_x;
        }
    }
}
#endif
// ----------------------------------------------------------------------------
/* Create an FBO.
 */
bool setFBO()
{
	glGenFramebuffersEXT(1, &fbo);
	glGenTextures(1, &framebuffer);
	glGenRenderbuffersEXT(1, &stencil);

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);

	glBindTexture(GL_TEXTURE_2D, framebuffer);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, fbowidth, fboheight, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, framebuffer, 0);

	glBindRenderbufferEXT(GL_RENDERBUFFER_EXT, stencil);
	glRenderbufferStorageEXT(GL_RENDERBUFFER_EXT, GL_DEPTH_STENCIL_EXT, fbowidth, fboheight);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_DEPTH_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, stencil);
	glFramebufferRenderbufferEXT(GL_FRAMEBUFFER_EXT, GL_STENCIL_ATTACHMENT_EXT, GL_RENDERBUFFER_EXT, stencil);

	GLenum status = glCheckFramebufferStatusEXT(GL_FRAMEBUFFER_EXT);

	if (status != GL_FRAMEBUFFER_COMPLETE_EXT )
	  return false;

	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);    // Unbind the FBO for now

	return true;
}

/* Re-size the OpenGL window.
 */
void ReSizeGLScene(GLsizei width, GLsizei height)
{
	glViewport(0, 0, width, height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D(0, width, height, 0);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	fbowidth = width;
	fboheight = height;
}

#ifndef USE_GLEW
// wgl function pointer for vsync
typedef bool (APIENTRY * PFNWGLSWAPINTERVALEXTPROC)        (int interval);
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = NULL;

PFNGLACTIVETEXTUREARBPROC        glActiveTextureARB;
PFNGLUSEPROGRAMOBJECTARBPROC     glUseProgramObjectARB;
PFNGLCREATEPROGRAMOBJECTARBPROC  glCreateProgramObjectARB;
PFNGLCREATESHADEROBJECTARBPROC   glCreateShaderObjectARB;
PFNGLSHADERSOURCEARBPROC         glShaderSourceARB;
PFNGLCOMPILESHADERARBPROC        glCompileShaderARB;
PFNGLATTACHOBJECTARBPROC         glAttachObjectARB;
PFNGLLINKPROGRAMARBPROC          glLinkProgramARB;
PFNGLDELETEOBJECTARBPROC         glDeleteObjectARB;
PFNGLGETINFOLOGARBPROC           glGetInfoLogARB;
PFNGLGETOBJECTPARAMETERIVARBPROC glGetObjectParameterivARB;
PFNGLGETUNIFORMLOCATIONARBPROC   glGetUniformLocationARB;
PFNGLUNIFORM1IARBPROC            glUniform1iARB;
PFNGLUNIFORM1FARBPROC            glUniform1fARB;
PFNGLUNIFORM2FARBPROC            glUniform2fARB;
PFNGLUNIFORM3FARBPROC            glUniform3fARB;
PFNGLGENFRAMEBUFFERSEXTPROC      glGenFramebuffersEXT;
PFNGLBINDFRAMEBUFFEREXTPROC      glBindFramebufferEXT;
PFNGLDELETEFRAMEBUFFERSEXTPROC   glDeleteFramebuffersEXT;
PFNGLFRAMEBUFFERTEXTURE2DEXTPROC glFramebufferTexture2DEXT;

PFNGLGENRENDERBUFFERSEXTPROC		glGenRenderbuffersEXT;
PFNGLBINDRENDERBUFFEREXTPROC		glBindRenderbufferEXT;
PFNGLRENDERBUFFERSTORAGEEXTPROC	glRenderbufferStorageEXT;
PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC	glCheckFramebufferStatusEXT;

PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;

PFNGLGENBUFFERSPROC glGenBuffers = NULL;
PFNGLDELETEBUFFERSPROC glDeleteBuffers = NULL;
PFNGLBINDBUFFERPROC glBindBuffer = NULL;
PFNGLBUFFERDATAPROC glBufferData = NULL;
PFNGLMAPBUFFERPROC glMapBuffer = NULL;
PFNGLUNMAPBUFFERPROC glUnmapBuffer = NULL;
PFNGLBUFFERSUBDATAPROC glBufferSubData = NULL;

//extern HGLRC		hRC;		// Permanent Rendering Context

PROC glGetProcAddress(LPCSTR addr)
{
	PROC ret = wglGetProcAddress(addr);
	if (ret == NULL)
		printf("Unable to load OpenGL Extension %s()\n", addr);
	return ret;
}

/* Prints out the list of supported OpenGL extensions
 */
static void printSupportedEXTList(char *list)
{
	char * pch;
	pch = strtok (list, " ");
    if(!list || *list == '\0')
        printf("    !!! none !!!\n");
    else while (pch != NULL) {
		
		printf("%s\n", pch);
		pch = strtok (NULL, " ");
    }
}

void LoadGlExtensions(){
	printf("Supported extensions...\n");
	//printf("%s", glGetString(GL_EXTENSIONS));
	printSupportedEXTList((char*)glGetString(GL_EXTENSIONS));
	printf("\n");

//...other extensions...
	glGenFramebuffersEXT = (PFNGLGENFRAMEBUFFERSEXTPROC)glGetProcAddress("glGenFramebuffersEXT");
	glBindFramebufferEXT = (PFNGLBINDFRAMEBUFFEREXTPROC)glGetProcAddress("glBindFramebufferEXT");
	glFramebufferTexture2DEXT = (PFNGLFRAMEBUFFERTEXTURE2DEXTPROC)glGetProcAddress("glFramebufferTexture2DEXT");
	glFramebufferRenderbufferEXT = (PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC)glGetProcAddress("glFramebufferTexture2DEXT");
	glCheckFramebufferStatusEXT = (PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC)glGetProcAddress("glCheckFramebufferStatusEXT");
	glGenRenderbuffersEXT = (PFNGLGENRENDERBUFFERSEXTPROC)glGetProcAddress("glGenRenderbuffersEXT");
	glBindRenderbufferEXT = (PFNGLBINDRENDERBUFFEREXTPROC)glGetProcAddress("glBindRenderbufferEXT");
	glRenderbufferStorageEXT = (PFNGLRENDERBUFFERSTORAGEEXTPROC)glGetProcAddress("glRenderbufferStorageEXT");

	wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC) glGetProcAddress("wglCreateContextAttribsARB");

	glGenBuffers = (PFNGLGENBUFFERSPROC) glGetProcAddress("glGenBuffers");
	glDeleteBuffers = (PFNGLDELETEBUFFERSPROC) glGetProcAddress("glDeleteBuffers");
	glBindBuffer = (PFNGLBINDBUFFERPROC) glGetProcAddress("glBindBuffer");
	glBufferData = (PFNGLBUFFERDATAPROC) glGetProcAddress("glBufferData");
	glMapBuffer = (PFNGLMAPBUFFERPROC) glGetProcAddress("glMapBuffer");
	glUnmapBuffer = (PFNGLUNMAPBUFFERPROC)  glGetProcAddress("glUnmapBuffer");
	glBufferSubData = (PFNGLBUFFERSUBDATAPROC) glGetProcAddress("glBufferSubData");
	//bool success = true;
	//success = (glGenBuffers = (PFNGLGENBUFFERSPROC)glGetProcAddress("glGenBuffers")) != 0 && success;
 
#if 0
	//HGLRC tempContext = wglCreateContext(getHDC());
	//wglMakeCurrent(getHDC(), getHRC());
 
	int attribs[] =
	{
		WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
		WGL_CONTEXT_MINOR_VERSION_ARB, 1,
		WGL_CONTEXT_FLAGS_ARB, 0,
		0
	};

	// Test GL version
	int nMajorVersion = -1;
	int nMinorVersion = -1;
	glGetIntegerv(GL_MAJOR_VERSION, &nMajorVersion);
	glGetIntegerv(GL_MINOR_VERSION, &nMinorVersion);
	printf("Reported GL Version %d.%d [Supported]\n", nMajorVersion, nMinorVersion);
 
    if(nMajorVersion >=3)
    {
		HGLRC l_hRC = wglCreateContextAttribsARB(getHDC(),0, attribs);
		wglMakeCurrent(NULL,NULL);
		wglDeleteContext(getHRC());
		wglMakeCurrent(getHDC(), l_hRC);
		hRC = l_hRC;
	}
	else
	{	//It's not possible to make a GL 3.x context. Use the old style context (GL 2.1 and before)
	}
#endif
//..etc...
}
#endif
/* Output command line arguments.
 */
static void printUsageAndDie(const char* progName, bool die = true)
{
	//printf("Usage: TCPClient (<ServerIP> <TCPPort> <UDPPort> <ClientID> <Stream> <VideoName>)\n");
	//printf("Usage: %s [-ServerIP xxx.xxx.xxx.xxx][-TCPPort n][-UDPPort n][-ClientID n][-StreamID n][-Res <w>x<h>]", progName);
	printf("Usage:\t[-ServerIP xxx.xxx.xxx.xxx][-TCPPort n][-UDPPort n][-ClientID n][-StreamID n][-Res <w>x<h>][-Position <w>x<h>]");
    printf("\n\t[-VideoPath path][-AutoStart 'true'|'false'][-Vsync 'true'|'false'][-TestLatency 'true'|'false']\n");
	printf("\t[-Debug 'true'|'false'][-Affinity n][-Loop 'true'|'false'][-Loop 'true'|'false'][-ScreenSync 'true'|'false']\n");
	printf("\t[-IntelliSleep 'true'|'false'][-EnablePBOs 'true'|'false'][-MutliTimer 'true'|'false'][-preloadAudio 'true'|'false']\n");
	printf("\t[-PreloadAudio 'true'|'false'][-BufferSize n(B)]\n");
	printf("\n");
	printf("  -ServerIP:\t Specify the server IP address.\n");
	printf("  -TCPPort:\t Specify the server TCP port.\n");
	printf("  -UDPPort:\t Specify the server UDP port.\n");
	printf("  -ClientID:\t Specify the client ID number.\n");
	printf("  -StreamID:\t Specify the client stream number.\n");
	printf("  -Res:\t\t Specify the window width and height.\n");
	printf("  -Position:\t Specify the window position.\n");
	printf("  -VideoPath:\t Path to video.\n");
	printf("  -AutoStart:\t Automatically start video. Either 'true' or 'false'.\n");
	printf("  -VSync:\t Enable vsync. Either 'true' or 'false'.\n");
	printf("  -TestLatency:\t Test the network latency. Either 'true' or 'false'.\n");
	printf("  -Debug:\t Enable debug output. Either 'true' or 'false'.\n");
	printf("  -Affinity:\t Set FFmpeg multi core count. Decimal representation of bit mask.\n");
	printf("  -Loop:\t Loop video. Either 'true' or 'false'.\n");
	printf("  -ScreenSync:\t Sync all video streams to a single source. Either 'true' or 'false'.\n");
	printf("  -EnablePBOs:\t Enable pbos if supported. Either 'true' or 'false'.\n");
	printf("  -IntelliSleep: Enable intelligent sleep to reduce cpu resources. Either 'true' or 'false'.\n");
	printf("  -MutliTimer:\t Enable multimedia timer callbacks instead of the alternative busy wait sleep to reduce cpu resources. Either 'true' or 'false'.\n");
	printf("  -PreloadAudio: Preload audio buffers (only works with local audio playback). Either 'true' or 'false'.\n");
	printf("  -BufferSize:\t Set OpenAL buffer size in bytes (only works with local audio playback).\n");

    printf("\n");

	if ( die )
	{
	    exit(1);
	}
}

/////////////////////////////// Signal /////////////////////////////////
BOOL CtrlHandler( DWORD fdwCtrlType ) 
{ 
  switch( fdwCtrlType ) 
  { 
    // Handle the CTRL-C signal. 
    case CTRL_C_EVENT: 
      printf( "Ctrl-C event\n\n" );
	  printf("Shutting down player - Posting quit message.\n");
      Beep( 750, 300 );
	  PostMessage(gethWnd(), WM_APP+1, 0, 0);
	  //PostMessage(gethWnd(), WM_KEYDOWN, VK_ESCAPE, 0);
	  //PostMessage(gethWnd(), WM_KEYUP, VK_ESCAPE, 0);
	  //PostQuitMessage(0);	// Send A Quit Message
	  //shutdownPlayer();
	  
	  //break;
      //exit(EXIT_SUCCESS);
	  return FALSE;
 
    // CTRL-CLOSE: confirm that the user wants to exit. 
    case CTRL_CLOSE_EVENT: 
      Beep( 600, 200 ); 
      printf( "Ctrl-Close event\n\n" );
	  //exit(EXIT_SUCCESS);
      return( TRUE ); 
 
    // Pass other signals to the next handler. 
    case CTRL_BREAK_EVENT: 
      Beep( 900, 200 ); 
      printf( "Ctrl-Break event\n\n" );
      return FALSE; 
 
    case CTRL_LOGOFF_EVENT: 
      Beep( 1000, 200 ); 
      printf( "Ctrl-Logoff event\n\n" );
      return FALSE; 
 
    case CTRL_SHUTDOWN_EVENT: 
      Beep( 750, 500 ); 
      printf( "Ctrl-Shutdown event\n\n" );
      return FALSE; 
 
    default: 
      return FALSE; 
  } 
}
/* Setup signal handler.
 */
bool setSignalHandler()
{
	if( SetConsoleCtrlHandler( (PHANDLER_ROUTINE) CtrlHandler, TRUE ) ) 
	{ 
		//printf( "The Control Handler is installed.\n" ); 
		//printf( "\n -- Now try pressing Ctrl+C or Ctrl+Break, or" ); 
		//printf( "\n    try logging off or closing the console...\n" ); 
		//printf( "(...waiting in a loop for events...)\n\n" ); 
		return true;
	}
	return false;
}

////////////////////////////////////////////////////////////////////////

/* Create a string of binary digits based on the input value.
   Input:
       val:  value to convert.
       buff: buffer to write to must be >= sz+1 chars.
       sz:   size of buffer.
   Returns address of string or NULL if not enough space provided.
*/
static char *binrep (unsigned int val, char *buff, int sz) {
    char *pbuff = buff;

    /* Must be able to store one character at least. */
    if (sz < 1) return NULL;

    /* Special case for zero to ensure some output. */
    if (val == 0) {
        *pbuff++ = '0';
        *pbuff = '\0';
        return buff;
    }

    /* Work from the end of the buffer back. */
    pbuff += sz;
    *pbuff-- = '\0';

    /* For each bit (going backwards) store character. */
    while (val != 0) {
        if (sz-- == 0) return NULL;
        *pbuff-- = ((val & 1) == 1) ? '1' : '0';

        /* Get next bit. */
        val >>= 1;
    }
    return pbuff+1;
}

/* Set the window title text
 */
void setWindowTitle(int clientID, string text)
{
	char titleText[256];
	if(sprintf(titleText, "%d : %s", clientID, text.c_str()) < 0)
	{
		printf("Could not set window text\n");
		return;
	}
	SetWindowText(gethWnd(), titleText);
}

/*
Initialise OpenGL and set the vertical sync.
*/
int InitGL()
{
#ifdef USE_GLEW
	glewInit();

	if (!glewIsSupported("GL_VERSION_2_0"))
	{
		MessageBox(NULL,"OpenGL support not sufficient","ERROR missing 2.0 support",MB_OK | MB_ICONINFORMATION);
		exit(-1);
	}
#endif
	glEnable(GL_TEXTURE_2D);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);

	printf("/////////////// GPU ///////////////\n");
	printf("VENDOR : %s\n", glGetString(GL_VENDOR));
	printf("RENDERER : %s\n", glGetString(GL_RENDERER));
	printf("VERSION : %s\n", glGetString(GL_VERSION));

#ifndef USE_GLEW
	//if(isExtensionSupported("GL_EXT_framebuffer_object"))
		LoadGlExtensions();
#endif
	if(enableVSync)
	{
		// V-Sync
		wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)      wglGetProcAddress ("wglSwapIntervalEXT");  
		if(wglSwapIntervalEXT)
		{	
			wglSwapIntervalEXT(1);
			OutputDebugString("V-sync enabled.\n");
			printf("V-sync enabled.\n");
		}
		else
		{
			OutputDebugString("V-Sync not supported.\n");
			printf("V-Sync not supported.\n");
		}
	}
	else
		printf("V-Sync off.\n");

	//if(!setFBO())
	//{
	//	MessageBox(NULL,"Unable to create FBO","ERROR",MB_OK | MB_ICONINFORMATION);
	//	exit(-1);
	//}

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("Init GL err.\n");
	}

	//plain.Load("Content/shaders/plain.vert","Content/shaders/plain.frag");
	//blend.Load("Content/shaders/blend.vert","Content/shaders/blend.frag");

	//drawallscreens = glGenLists(1);
	//int sectionIndex = 0;
	//glNewList(drawallscreens, GL_COMPILE);
	//for (int c = 0; c < numAcross; c++)
	//{
	//    if (useBezier)
	//    {
	//        int numSections = 1;      // Central portion.
	//        if (c > 0)                // Has a left neighbor.
	//            numSections++;
 	//        if (c < (numAcross - 1))  // Has a right neighbor.
 	//            numSections++;
 	//        for (int s = 0; s < numSections; s++, sectionIndex++)
 	//            drawcornerpinbezier(sectionIndex, calibPts+16*sectionIndex, false, c);
 	//    }
  	//    else
  	//        drawscreen(c);
  	//}
	//glEndList();

	return true;
}

void drawInitialText();

/* Initialise video player environment.
 */
int Init(char* cmdline)
//int Init(char** argv, int argc)
{
	fbowidth			= 1280;
	fboheight			= 720;

	char* serverIP		= "127.0.0.1";
	int tcpPort			= 2007;
	int udpPort			= 2008;
	int clientID		= 1;
	int streamID		= 0;
	strcpy(videoName,   "sample1.mp4");
	//videoName			= "sample1.mp4";
	bool autostart		= false;
	bool testLatency	= false;
	//enableVSync			= false;
	core				= 1;
	int winPosX			= 0;
	int winPosY			= 0;
	//enablePBOs			= false;
	//intelliSleep		= false;
	//multiTimer			= false;

	/*
	CHAR szEXEPath[1024];
	char actualpath[1024];
	GetModuleFileName ( NULL, szEXEPath, 1024 );

	HANDLE hProcess = GetCurrentProcess();
	char buffer[1024];
	DWORD app_path=GetModuleBaseName(hProcess, NULL, buffer, 1024);
	*/

	////////////////////////////////////////////////////////////////////////////////////////////////////////
	/////////////////////// Replace spaces with end string delimiter for tokenising ////////////////////////
	/////////////////////// Checks for matching quote pairs for input video path ///////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////

	char* pch;
	int argc = 0;
	int i = 0;
	pch = cmdline;
	while(pch[i] != NULL)
	{
		if(pch[i] == '"')
		{
			char* quote = strchr(pch+i+1,'"');
			if(quote == NULL) {
				printf("Error - Missing quote.\n");
				printUsageAndDie("Obscura Player");
			}
			//printf ("found at %d\n",quote-pch+1);
			i = (int)(quote-pch+1);
		}
		if(pch[i] == ' ')
		{
			pch[i] = '\0'; // replace space with end string delimiter
			argc++;
		}

		i++;
	}
#if 0
	pch = strtok (cmdline, " ");
	while(pch != NULL)
	{
		pch = strtok (NULL, " ");
		argc++;
	}

#endif
	//if (argc < 1)   // All args are optional...
	//	printUsageAndDie("Obscura Player", true);

	////////////////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////// Tokenise command line args ///////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////
	argc += 1;
	char** argv = new char*[argc];
	//pch = strtok (cmdline, " ");
	pch = cmdline;
	for (int i = 0; i < argc; i++)
	{
        //printf( "token %d: %s\n", i+1, pch);
		argv[i] = pch;
        pch += strlen(pch) + 1;  // get the next token by skipping past the '\0'
        pch += strspn(pch, ","); //   then skipping any starting delimiters
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////// Process command line args ///////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////

	int currArgPos = 0;
	while (currArgPos < argc)
	{
		if ( stricmp(argv[currArgPos], "-h") == 0 || stricmp(argv[currArgPos], "-?") == 0 || stricmp(argv[currArgPos], "/?") == 0 )
		{
			printUsageAndDie(argv[0]);
			currArgPos += 2;
		}
		else if ( stricmp(argv[currArgPos], "-ServerIP") == 0 )
		{
			serverIP = argv[currArgPos + 1];
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-TCPPort") == 0 )
		{
			tcpPort = atoi( argv[currArgPos + 1] );
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-UDPPort") == 0 )
		{
			udpPort = atoi( argv[currArgPos + 1] );
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-ClientID") == 0 )
		{
			clientID = atoi( argv[currArgPos + 1] );
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-StreamID") == 0 )
		{
			streamID = atoi( argv[currArgPos + 1] );
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-VideoPath") == 0 )
		{
			//videoName = argv[currArgPos + 1];
			strcpy(videoName, argv[currArgPos + 1]);
			int len = (int)strlen(videoName);
			if(videoName[0] == '"' && videoName[len-1] == '"' )
			{
				strncpy(videoName, videoName+1, len-2);
				videoName[len-2] = '\0';
			}

			currArgPos += 2;
		}
		else if ( stricmp(argv[currArgPos], "-Res") == 0 )
		{
			int w, h;
			int numRead = sscanf(argv[currArgPos+1], "%dx%d", &w, &h);
			if ((numRead < 2) || (w <= 0) || (h <= 0)) {
				printf("Invalid size arg '%s'; should be <width>x<height>\n", argv[currArgPos]);
				printUsageAndDie(argv[0], true);
			}
			fbowidth = w;
			fboheight = h;

			currArgPos += 2;
		}
		else if ( stricmp(argv[currArgPos], "-Position") == 0 )
		{
			int x, y;
			int numRead = sscanf(argv[currArgPos+1], "%dx%d", &x, &y);
			if ((numRead < 2) || (x < 0) || (y < 0)) {
				printf("Invalid position arg '%s'; should be <X>x<Y>\n", argv[currArgPos]);
				printUsageAndDie(argv[0], true);
			}
			winPosX = x;
			winPosY = y;

			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-AutoStart") == 0 )
		{
			if(strcmp("true", argv[currArgPos + 1]) == 0)
				autostart = true;
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-TestLatency") == 0 )
		{
			if(strcmp("true", argv[currArgPos + 1]) == 0)
				testLatency = true;
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-VSync") == 0 )
		{
			if(strcmp("true", argv[currArgPos + 1]) == 0)
				enableVSync = true;
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-Debug") == 0 )
		{
			if(strcmp("true", argv[currArgPos + 1]) == 0)
				enableDebugOutput = true;
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-Affinity") == 0 )
		{
			core = atoi(argv[currArgPos + 1] );
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-Loop") == 0 )
		{
			if(strcmp("true", argv[currArgPos + 1]) == 0)
				loopvideo = true;
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-ScreenSync") == 0 )
		{
			if(strcmp("true", argv[currArgPos + 1]) == 0)
				screenSync = true;
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-EnablePBOs") == 0 )
		{
			if(strcmp("true", argv[currArgPos + 1]) == 0)
				enablePBOs = true;
			else
				enablePBOs = false;
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-IntelliSleep") == 0 )
		{
			if(strcmp("true", argv[currArgPos + 1]) == 0)
				intelliSleep = true;
			else
				intelliSleep = false;
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-MultiTimer") == 0 )
		{
			if(strcmp("true", argv[currArgPos + 1]) == 0)
				multiTimer = true;
			else
				multiTimer = false;
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-PreloadAudio") == 0 )
		{
			if(strcmp("true", argv[currArgPos + 1]) == 0)
				g_preloadAudio = true;
			currArgPos += 2;
		}
		else if( stricmp(argv[currArgPos], "-BufferSize") == 0 )
		{
			if(argv[currArgPos + 1] != NULL)
			{
				g_bufferSize = atoi( argv[currArgPos + 1] );
				currArgPos += 2;
			}
			else
				printUsageAndDie(argv[0], true);
		}
		else
			currArgPos++;
	}

	////////////////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////// Set process affinity /////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////
	
	printf("\n/////////// PLAYER SETUP ///////////////\n");
	//core = 1 << core;
	printf("Core affinity: %x\n", core);
    char buff[32+1];
    printf("%9d -> %s\n", core, binrep(core,buff,32));

#if 0
	HANDLE process = GetCurrentProcess();
	DWORD_PTR processAffinityMask;
	DWORD_PTR systemAffinityMask;
    if (GetProcessAffinityMask(process, &processAffinityMask, &systemAffinityMask) != 0)
	{
		//DWORD core = 0;//GetCurrentProcessorNumber();
		//processAffinityMask &= 1 << core;
		processAffinityMask = core;
		printf("%%%%Changing - Process Affinity: %d\n", processAffinityMask/*core*/);

		//core = processAffinityMask;

		if(!SetProcessAffinityMask(process, processAffinityMask))
			printf("Couldn't set Process Affinity Mask\n");
	}
	else
		printf("===Setting process AffMask failed!!!\n");

	//DWORD_PTR threadAffMask = SetThreadAffinityMask(GetCurrentThread(), core);
	//if(threadAffMask == 0)
	//	printf("===Setting threadAffMask failed!!!\n");
#endif

	////////////////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////////////// Setup video player ///////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////
	
	width = fbowidth;
	height = fboheight;

	if(!setSignalHandler())
		printf("Couldn't setup signal handler\n");

	//HGLRC hRC = wglCreateLayerContext(getHDC(), clientID);
	//if(hRC != NULL)
	//	setHRC(hRC);

#ifdef VIDEO_PLAYBACK
		if (!CreateGLWindow("Video Playback", fbowidth, fboheight, 32, 0, 0, false))
			return false;
#ifdef WGL_FONTS
		Font = new GLFont(getHDC(), "Arial", 18);
#endif
		ReSizeGLScene(width, height);
		setWindowTitle(clientID, videoName);
		
		printf("\n//////////////   BG  ///////////////\n");
		if(loadBG())
			backgroundEnabled = true;

		printf("\n/////////////// VIDEO //////////////\n");
			initVideoAudioPlayback(serverIP, tcpPort, udpPort, clientID, streamID, videoName, autostart, testLatency);
#else
		init(args);
#endif

#if 0
	// Reset affinity
	SetThreadAffinityMask(GetCurrentThread(), threadAffMask);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");
#endif

#ifdef USE_FREETYPE_FONTS
	//glutInit( 0, NULL);
	/* Text to be printed */
    //wchar_t *text = L"A Quick Brown Fox Jumps Over The Lazy Dog 0123456789";

    /* Texture atlas to store individual glyphs */
    atlas = texture_atlas_new( 512, 512, 1 );

    /* Build a new texture font from its description and size */
    font = texture_font_new( atlas, "./Vera.ttf", 16 );

   /* Build a new vertex buffer (position, texture & color) */
    text_buffer = vertex_buffer_new( "v3f:t2f:c4f" );
	text_buffer2 = vertex_buffer_new( "v3f:t2f:c4f" );

    /* Cache some glyphs to speed things up */
    texture_font_load_glyphs( font, L" !\"#$%&'()*+,-./0123456789:;<=>?"
                                     L"@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_"
                                     L"`abcdefghijklmnopqrstuvwxyz{|}~");
    //texture_font_load_glyphs( font, text);

	drawInitialText();

	//texture_font_delete( font );
#endif

	// create a rendering context for overlay plane 1  
	//HDC lhdc = getHDC();
	//HGLRC hRC = wglCreateLayerContext(lhdc, clientID);
	//if(hRC != NULL)
	//	setHRC(hRC);

	SetWindowPos(gethWnd(), NULL, winPosX, winPosY, width, height, SWP_NOSIZE | SWP_NOZORDER);

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("Init GL err.\n");
	}

	return 1;
}

void drawInitialText()
{
#ifdef USE_FREETYPE_FONTS
    // create an array of display tips
    const int cOptions = 8;
    std::string helpText[cOptions] = { 
                         "L = Load a video file.",
						 "R = Reset the current video.",
                         "S = Stop video.", 
                         "VK_UP = Add 0.001 to global delay.",
						 "VK_DOWN = Minus 0.001 from global delay.",
						 "VK_RIGHT = Add 0.1 to global delay.",
						 "VK_LEFT = Minus 0.1 from global delay.",
						 "E = Exit or VK_ESCAPE."
            };

	wchar_t dText[256];
    /* Where to start printing on screen */
    vec2 pen = {0,0};
    vec4 black = {1,1,1,1};

	float oPosition = 20.0;
	/* Add text tothe buffer (see demo-font.c for the add_text code) */
    //freetype_add_text( text_buffer, font,L"Hello World!"/*text*/, &black, &pen );
	for(int i=0; i<cOptions; i++, oPosition += 15)
	{
		pen.x = 5;
		pen.y = oPosition;

		swprintf (dText, 256, L"%hs", helpText[i].c_str());
		freetype_add_text( text_buffer, font, dText, &black, &pen );
	}

	int lx = 0;
	int ly = 0;

	pen.x = fbowidth-350.0f;
	pen.y = 20.0f;

	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> FPS:", currFPS);
	freetype_add_text( text_buffer, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 35.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> g_Delay:");
	freetype_add_text( text_buffer, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 50.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> currGlobalTimer:");
	freetype_add_text( text_buffer, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 65.0f;
	pen.x += lx;
	pen.y += ly;
#if NETWORKED_AUDIO
	swprintf (dText, 256, L"%s", L"System -> NET_AUDIO_CLOCK:");
#else
	swprintf (dText, 256, L"%s", L"System -> OAL_AUDIO_CLOCK:");
#endif
	freetype_add_text( text_buffer, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 80.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> preClock:");
	freetype_add_text( text_buffer, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 95.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> PTS:");
	freetype_add_text( text_buffer, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 110.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> LClock:");
	freetype_add_text( text_buffer, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 125.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> diff:");
	freetype_add_text( text_buffer, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 140.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> NEXT_FRAME_DELAY:");
	freetype_add_text( text_buffer, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 155.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> VIDEO_AUDIO_SYNC:");
	freetype_add_text( text_buffer, font, dText, &black, &pen );
#endif
}

#if 0
#define NINT(f)   ((f >= 0) ? (int)(f + .5) : (int)(f - .5))
// CImage is BGR so defs of red and blue are switched from GetXValue() defs
#define RED(rgb)	(LOBYTE((rgb) >> 16))
#define GRN(rgb)	(LOBYTE(((WORD)(rgb)) >> 8))
#define BLU(rgb)	(LOBYTE(rgb))
#define BGR(b,g,r)	RGB(b,g,r)

char* getPlainRGB()
{
    int *p, *buf;
	char* buf2;
	unsigned long i, j, k, nx, ny;

	nx = Image.GetWidth();
	ny = Image.GetHeight();
	unsigned long n = nx * ny;   // No. of pixels
	unsigned long n2 = nx * ny * 3;   // No. of pixels

	// Allocate n sized buffer for temp storage
	if (!(buf = (int *)malloc(n * sizeof(int)))) {
		return NULL;
	}
	memset(buf, 100, n * sizeof(int));
	buf2 = (char*)malloc(n2 * sizeof(char));
	memset(buf2, 100, n2 * sizeof(char));
	
	for (j = 0, k=0; j < ny; j++)
	{
		// Addr. next row (avoids bottom-up calc.)
		p = (int *) Image.GetPixelAddress(0, j);
		for (i = 0; i < nx; i++, p++, k+=3) {
			buf[j*ny+i] = *p;
			buf2[j*ny+k] = RED(*p);
			buf2[j*ny+k+1] = GRN(*p);
			buf2[j*ny+k+2] = BLU(*p);//(float)255;
		}
	}

	return buf2;
}
#endif

#ifdef USE_CIMAGE
void Convert32Bit()
{
    if (Image.GetBPP() < 8) return;

    byte *t, r, g, b;
    int *p, *q, *buf;
    unsigned long i, j, nx, ny;
    RGBQUAD *pRGB = new RGBQUAD[256];   // For GetDIBColorTable()

    nx = Image.GetWidth();
    ny = Image.GetHeight();
    unsigned long n = nx * ny;   // No. of pixels

    // Allocate n sized buffer for temp storage
    if (!(buf = (int *)malloc(n * sizeof(int)))) {
       return;
    }
    switch (Image.GetBPP()) {
       case 8:
           if (!(i = GetDIBColorTable(Image.GetDC(), 0, 256, pRGB))) {

               Image.ReleaseDC();
               goto End;
           }
           for (j = 0, q = buf; j < ny; j++) {
               t = (byte *) Image.GetPixelAddress(0, j);
               for (i = 0; i < nx; i++, t++, q++) {
                  r = pRGB[*t].rgbRed;
                  g = pRGB[*t].rgbGreen;
                  b = pRGB[*t].rgbBlue;
                  *q = RGB(b, g, r);    // CImage is BGR
               }
           }
           break;
       case 24:
           for (j = 0, q = buf; j < ny; j++) {
               // Addr. next row (avoids 24 bit offset bottom-up calc.)
               t = (byte *) Image.GetPixelAddress(0, j);
               for (i = 0; i < nx; i++, t++, q++) {
                  b = *t;        // CImage is BGR
                  g = *(++t);
                  r = *(++t);
                  *q = RGB(b, g, r);
               }
           }
           break;
       case 32:   // Just need to make top-down
           for (j = 0, q = buf; j < ny; j++) {
               // Addr. next row (avoids bottom-up calc.)
               p = (int *) Image.GetPixelAddress(0, j);
               for (i = 0; i < nx; i++, p++, q++) {
                  *q = *p;
               }
           }
           break;
    }

    // Start a new CImage
    Image.Destroy();
    if (!Image.Create(nx, -(int)ny, 32, 0)) {
       goto End;
    }
    p = (int *) Image.GetBits();   // Ptr to new bitmap (top-down DIB)
    memcpy_s(p, n * sizeof(int), buf, n * sizeof(int)); // Copy buf to bitmap
    //Image.ptype = cRGB;        // Update pixel type

End:
    free(buf);
}
#endif
/* Open the background image from disk and create a new texture.
 */
bool loadBG()
{
#ifdef USE_CIMAGE
	char path[MAX_PATH];
	sprintf(path, "welcome.png");

	CString sFullpath(path);     
    Image.Load(sFullpath);

#if 1
	if(Image.IsNull())
	{
		MessageBox(NULL, "Couldn't load background.", "Error", NULL);
		printf("Couldn't load background.\n");
		return false;
	}
	else
	{
		Convert32Bit();
		//plainBG = getPlainRGB();
		printf("Image width: %d, height: %d.\n", Image.GetWidth(), Image.GetHeight());
		glGenTextures(1, &bgTexture);
		glBindTexture(GL_TEXTURE_2D, bgTexture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // Linear Magnification
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, Image.GetWidth(), Image.GetHeight(), 0, GL_BGRA , GL_UNSIGNED_BYTE, Image.GetBits());
	}
#endif
#else
	printf("Image width: %d, height: %d.\n", 1024, 768);
	glGenTextures(1, &bgTexture);
	glBindTexture(GL_TEXTURE_2D, bgTexture);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); // Linear Magnification
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA/*GL_RGB*/, 1024, 768, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
#endif
	return true;
}


/* Calculate the fps.
 */
void CalculateFrameRate()
{
    static float framesPerSecond    = 0.0f;       // This will store our fps
    static float lastTime			= 0.0f;       // This will hold the time from the last frame
    float currentTime = GetTickCount() * 0.001f;    
    ++framesPerSecond;
    if( currentTime - lastTime > 1.0f )
    {
        lastTime = currentTime;
		#ifdef LOG
		fprintf(videoDebugLogFile, "\r\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tFPS: %d\n", (int)framesPerSecond);
		#endif
		//if(status[0] == aplay)
		{
			//#ifdef DEBUG_PRINTF
			//printf(stdout, "\r\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tFPS: %d", (int)framesPerSecond);
			//#endif
			if(enableDebugOutput)
					fprintf(stdout, "\r\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\tFPS: %d\n", (int)framesPerSecond);
		}
		currFPS = (int)framesPerSecond;
        framesPerSecond = 0;
    }
}

/* Check the current mouse position and set the current video accordingly.
 */
void checkMouse()
{
#if MAXSTREAMS  == 2
	if(mouse_x < fbowidth/2)
		currentVideo = 0;
	if((mouse_x >= fbowidth/2) && (mouse_x < fbowidth))
		currentVideo = 1;
#endif
#if MAXSTREAMS  == 4
	if(mouse_x < fbowidth/2 && (mouse_y >= 0) && (mouse_y < fboheight/2))
		currentVideo = 0;
	if((mouse_x >= fbowidth/2) && (mouse_x < fbowidth) && (mouse_y >= 0) && (mouse_y < fboheight/2))
		currentVideo = 1;

	if(mouse_x < fbowidth/2 && (mouse_y >= fboheight/2) && (mouse_y < fboheight))
		currentVideo = 2;
	if((mouse_x >= fbowidth/2) && (mouse_x < fbowidth) && (mouse_y >= fboheight/2) && (mouse_y < fboheight))
		currentVideo = 3;

	//-------------------------------------------------------------------------------------------------------
	//if(rmouse_x == -1 && rmouse_y == -1)
	//	fullScreenVideo = -1;

	//if(rmouse_x < fbowidth/2 && (rmouse_y >= 0) && (rmouse_y < fboheight/2))
	//	fullScreenVideo = 0;
	//if((rmouse_x >= fbowidth/2) && (rmouse_x < fbowidth) && (rmouse_y >= 0) && (rmouse_y < fboheight/2))
	//	fullScreenVideo = 1;

	//if(rmouse_x < fbowidth/2 && (rmouse_y >= fboheight/2) && (rmouse_y < fboheight))
	//	fullScreenVideo = 2;
	//if((rmouse_x >= fbowidth/2) && (rmouse_x < fbowidth) && (rmouse_y >= fboheight/2) && (rmouse_y < fboheight))
	//	fullScreenVideo = 3;
#endif
	//printf("mouse_x: %d - mouse_y: %d\n", mouse_x, mouse_y);
}

/* Print out the current video debug info.
 */
void printVideoDebugInfo(int side, int lx, int ly)
{
	double curTime = currGlobalTimer[side]+diff[side];

#ifdef WGL_FONTS
			glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
			char buffer[255];

			// Draw specified text to screen.
			if(status[0] == apause)
			{
				sprintf(buffer, "PAUSED");
				Font->setposition(fbowidth/2 - 10.0f, fboheight/2 + 15.0f);
				Font->displaytext(buffer);
			}

			//----------------------------------------------------------

			sprintf(buffer, "System -> FPS:");
				Font->setposition(fbowidth-350.0f, 20.0f);
				Font->displaytext(buffer);
#if 1
			sprintf(buffer, "System -> g_Delay:");
				Font->setposition(fbowidth-350.0f, 35.0f);
				Font->displaytext(buffer);
			#ifdef NETWORKED_AUDIO
			sprintf(buffer, "System -> NET_AUDIO_CLOCK:");
			#else
			sprintf(buffer, "System -> NET_AUDIO_CLOCK:", -1.0f);
			#endif
				Font->setposition(fbowidth-350.0f, 50.0f);
				Font->displaytext(buffer);
			sprintf(buffer, "System -> currGlobalTimer:");
				Font->setposition(fbowidth-350.0f, 65.0f);
				Font->displaytext(buffer);
			#ifdef NETWORKED_AUDIO
			sprintf(buffer, "System -> preClock:");
			#else
			sprintf(buffer, "System -> preClock:", -1.0f);
			#endif
				Font->setposition(fbowidth-350.0f, 80.0f);
				Font->displaytext(buffer);
			sprintf(buffer, "System -> LClock:");
				Font->setposition(fbowidth-350.0f, 95.0f);
				Font->displaytext(buffer);
			sprintf(buffer, "System -> diff:");
				Font->setposition(fbowidth-350.0f, 110.0f);
				Font->displaytext(buffer);
			sprintf(buffer, "System -> NEXT_FRAME_DELAY:");
				Font->setposition(fbowidth-350.0f, 125.0f);
				Font->displaytext(buffer);
#endif
			//----------------------------------------------------------

			sprintf(buffer, "%3d", currFPS);
				Font->setposition(fbowidth-95.0f, 20.0f);
				Font->displaytext(buffer);
#if 1
			sprintf(buffer, "%3f", g_Delay[side]);
				Font->setposition(fbowidth-90.0f, 35.0f);
				Font->displaytext(buffer);
			#ifdef NETWORKED_AUDIO
			sprintf(buffer, "%3f", g_netAudioClock[side]);
			#else
			sprintf(buffer, "%3f", -1.0f);
			#endif
				Font->setposition(fbowidth-90.0f, 50.0f);
				Font->displaytext(buffer);
			sprintf(buffer, "%3f", currGlobalTimer[side]);
				Font->setposition(fbowidth-90.0f, 65.0f);
				Font->displaytext(buffer);
			#ifdef NETWORKED_AUDIO
			sprintf(buffer, "%3f", preClock[side]);
			#else
			sprintf(buffer, "%3f", -1.0f);
			#endif
				Font->setposition(fbowidth-90.0f, 80.0f);
				Font->displaytext(buffer);
			sprintf(buffer, "%3f", curTime);
				Font->setposition(fbowidth-90.0f, 95.0f);
				Font->displaytext(buffer);
			sprintf(buffer, "%3f", diff[currentVideo] + g_Delay[side]);
				Font->setposition(fbowidth-90.0f, 110.0f);
				Font->displaytext(buffer);
			sprintf(buffer, "%3f", nextFrameDelay[side]);
				Font->setposition(fbowidth-90.0f, 125.0f);
				Font->displaytext(buffer);

			//----------------------------------------------------------

			#ifdef NETWORKED_AUDIO
			sprintf(buffer, "System -> currentVideo: %d", side);
			Font->setposition(20.0f, (float)fboheight-30);
			Font->displaytext(buffer);
			sprintf(buffer, "System -> inwidth: %d - inheight: %d - FPS: %f - Time: %f / %f", inwidth[currentVideo], inheight[currentVideo], fps[currentVideo], curTime, duration[currentVideo]);
			#else
			sprintf(buffer, "System -> inwidth: %d - inheight: %d - FPS: %f - Time: AL: %f G:%f / %f", inwidth[0], inheight[0], fps[0], getOpenALAudioClock(0), currGlobalTimer[0], duration[0]);
			#endif
			Font->setposition(20.0f, (float)fboheight-15);
			Font->displaytext(buffer);

			displayHelp();
#endif
#endif

#ifdef USE_FREETYPE_FONTS

	wchar_t dText[256];
	vec2 pen = {0,0};
	vec4 black = {1,1,1,1};

	pen.x = fbowidth-350.0f;
	pen.y = 20.0f;

#if 0

	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> FPS:", currFPS);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 35.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> g_Delay:", g_Delay[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 50.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> currGlobalTimer:", currGlobalTimer[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 65.0f;
	pen.x += lx;
	pen.y += ly;
#if NETWORKED_AUDIO
	swprintf (dText, 256, L"%s", L"System -> NET_AUDIO_CLOCK:", g_netAudioClock[side]);
#else
	swprintf (dText, 256, L"%s", L"System -> OAL_AUDIO_CLOCK:", g_netAudioClock[side]);
#endif
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 80.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> preClock:", preClock[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 95.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> PTS:", pts[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 110.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> LClock:", curTime);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 125.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> diff:", diff[0] + g_Delay[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 140.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> NEXT_FRAME_DELAY:", nextFrameDelay[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-350.0f;
	pen.y = 155.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%s", L"System -> VIDEO_AUDIO_SYNC:");
	freetype_add_text( text_buffer2, font, dText, &black, &pen );
#endif
	// -----------------------------------------------------------

	pen.x = fbowidth-90.0f;
	pen.y = 20.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%d", currFPS);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-90.0f;
	pen.y = 35.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%09.4f", g_Delay[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-90.0f;
	pen.y = 50.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%09.4f", currGlobalTimer[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-90.0f;
	pen.y = 65.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%09.4f", g_netAudioClock[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-90.0f;
	pen.y = 80.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%09.4f", preClock[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-90.0f;
	pen.y = 95.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%09.4f", pts[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-90.0f;
	pen.y = 110.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%09.4f", curTime);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-90.0f;
	pen.y = 125.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%09.4f", diff[side] + g_Delay[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = fbowidth-90.0f;
	pen.y = 140.0f;
	pen.x += lx;
	pen.y += ly;
	swprintf (dText, 256, L"%09.6f", nextFrameDelay[side]);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	// -----------------------------------------------------------

	pen.x = 20.0f;
	pen.y = fboheight-25;
	swprintf (dText, 256, L"System -> currentVideo: %d", currentVideo);
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

	pen.x = 20.0f;
	pen.y = fboheight-10;
#ifdef NETWORKED_AUDIO
	swprintf (dText, 256, L"System -> inwidth: %d - inheight: %d - FPS: %f - Time: %f / %f", inwidth[currentVideo], inheight[currentVideo], fps[currentVideo], curTime, duration[currentVideo]);
#else
	swprintf (dText, 256, L"System -> inwidth: %d - inheight: %d - FPS: %f - Time: AL: %09.4f G:%09.4f / %09.4f", inwidth[currentVideo], inheight[currentVideo], fps[currentVideo], getOpenALAudioClock(currentVideo), currGlobalTimer[currentVideo], duration[currentVideo]);
#endif
	freetype_add_text( text_buffer2, font, dText, &black, &pen );

#endif
}

/* Main draw loop.
 */
void DrawGLScene()
{
	double profileTime = getGlobalVideoTimer(currentVideo);

	checkMouse();
	// Set the current context.
	//if(!wglMakeCurrent(getHDC(), getHRC()))
	//	printf("Couldn't make current.\n");
/*
    // clear buffer
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
	ReSizeGLScene(fbowidth, fboheight);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, fbo);
	ReSizeGLScene(fbowidth, fboheight);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, 0);	// Unbind the FBO
	ReSizeGLScene(fbowidth, fboheight);
*/
	////////////////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////// Draw video player texture ////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////
#if 1
	glClear(GL_COLOR_BUFFER_BIT);
	int vOffset = 40;

	glColor4f(1, 1, 1, 1.0);
	glEnable(GL_TEXTURE_2D);
	//glBindTexture(GL_TEXTURE_2D, framebuffer);
	#ifdef VIDEO_PLAYBACK
	if(multiTimer)
		updateVideoDraw();
	else
		updateVideo();
	#endif

	// Draw video texture
#if MAXSTREAMS==1
	if(status[0] == astop)
		glColor4f(0, 0, 0, 1.0);
	glBindTexture(GL_TEXTURE_2D, videotextures[0]);
	glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f); glVertex2i( 0,  vOffset);
		glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth , vOffset);
		glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth, fboheight-vOffset);
		glTexCoord2f(0.0f, 1.0f); glVertex2i( 0, fboheight-vOffset);
	glEnd();
#endif		
#if MAXSTREAMS==2
	if(status[0] == astop)
		glColor4f(0, 0, 0, 1.0);
	glBindTexture(GL_TEXTURE_2D, videotextures[0]);
	glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f); glVertex2i( 0,  vOffset);
		glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth/2 , vOffset);
		glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth/2, fboheight-vOffset);
		glTexCoord2f(0.0f, 1.0f); glVertex2i( 0, fboheight-vOffset);
	glEnd();

	glColor4f(1, 1, 1, 1.0);
	if(status[1] == astop)
		glColor4f(0, 0, 0, 1.0);
	glBindTexture(GL_TEXTURE_2D, videotextures[1]);
	glBegin(GL_QUADS);
		glTexCoord2f(0.0f, 0.0f); glVertex2i( fbowidth/2,  vOffset);
		glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth , vOffset);
		glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth, fboheight-vOffset);
		glTexCoord2f(0.0f, 1.0f); glVertex2i( fbowidth/2, fboheight-vOffset);
	glEnd();
#endif
#if MAXSTREAMS==4
	//printf("fullScreenVideo: %d\n", fullScreenVideo );
	if(!fullScreenVideo)
	{
		if(status[0] == astop)
			glColor4f(0, 0, 0, 1.0);
		glBindTexture(GL_TEXTURE_2D, videotextures[0]);
		glBegin(GL_QUADS);
		{
			glTexCoord2f(0.0f, 0.0f); glVertex2i( 0,  vOffset);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth/2 , vOffset);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth/2, fboheight/2);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( 0, fboheight/2);
		}
		glEnd();

		glColor4f(1, 1, 1, 1.0);
		if(status[1] == astop)
			glColor4f(0, 0, 0, 1.0);
		glBindTexture(GL_TEXTURE_2D, videotextures[1]);
		glBegin(GL_QUADS);
		{
			glTexCoord2f(0.0f, 0.0f); glVertex2i( fbowidth/2,  vOffset);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth , vOffset);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth, fboheight/2);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( fbowidth/2, fboheight/2);
		}
		glEnd();

		glColor4f(1, 1, 1, 1.0);
		if(status[2] == astop)
			glColor4f(0, 0, 0, 1.0);
		glBindTexture(GL_TEXTURE_2D, videotextures[2]);
		glBegin(GL_QUADS);
		{
			glTexCoord2f(0.0f, 0.0f); glVertex2i( 0,  fboheight/2);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth/2, fboheight/2);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth/2, fboheight-vOffset);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( 0, fboheight-vOffset);
		}
		glEnd();

		glColor4f(1, 1, 1, 1.0);
		if(status[3] == astop)
			glColor4f(0, 0, 0, 1.0);
		glBindTexture(GL_TEXTURE_2D, videotextures[3]);
		glBegin(GL_QUADS);
		{
			glTexCoord2f(0.0f, 0.0f); glVertex2i( fbowidth/2,  fboheight/2);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth , fboheight/2);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth, fboheight-vOffset);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( fbowidth/2, fboheight-vOffset);
		}
		glEnd();
	}
	//glDisable(GL_TEXTURE_2D);
#endif
	////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////// Draw background logo texture ////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////
#if 1
	// Draw background texture
	if(!fullScreenVideo)
	{
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		//glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, bgTexture);
#if MAXSTREAMS==1
	if(backgroundEnabled && status[0] == astop)
	{
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2i( 0,  0);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth , 0);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth, fboheight);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( 0, fboheight);
		glEnd();
	}
#elif MAXSTREAMS==2
	if(backgroundEnabled)// && status[0] == astop && status[1] == astop)
	{
		if(status[0] == astop)
		{
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2i( 0,  0);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth/2 , 0);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth/2, fboheight);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( 0, fboheight);
		glEnd();
		}
		if(status[1] == astop)
		{
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2i( fbowidth/2,  0);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth , 0);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth, fboheight);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( fbowidth/2, fboheight);
		glEnd();
		}
	}
#elif MAXSTREAMS==4
	if(backgroundEnabled)// && status[0] == astop && status[1] == astop && status[2] == astop && status[3] == astop)
	{
		if(status[0] == astop)
		{
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2i( 0,  vOffset);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth/2 , vOffset);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth/2, fboheight/2);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( 0, fboheight/2);
		glEnd();
		}
		if(status[1] == astop)
		{
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2i( fbowidth/2,  vOffset);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth , vOffset);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth, fboheight/2);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( fbowidth/2, fboheight/2);
		glEnd();
		}

		if(status[2] == astop)
		{
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2i( 0,  fboheight/2);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth/2, fboheight/2);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth/2, fboheight-vOffset);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( 0, fboheight-vOffset);
		glEnd();
		}
		if(status[3] == astop)
		{
		glBegin(GL_QUADS);
			glTexCoord2f(0.0f, 0.0f); glVertex2i( fbowidth/2,  fboheight/2);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth , fboheight/2);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth, fboheight-vOffset);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( fbowidth/2, fboheight-vOffset);
		glEnd();
		}
	}
#endif
	}
#endif

#if 1
	////////////////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////// Draw fullscreen video texture ////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////
	if(status[0] == astop && status[1] == astop && status[2] == astop && status[3] == astop)
		fullScreenVideo = false;

	// Draw current video fullscreen
	if(fullScreenVideo && status[currentVideo] != astop)
	{
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		glBindTexture(GL_TEXTURE_2D, videotextures[currentVideo]);
		glBegin(GL_QUADS);
		{
			glTexCoord2f(0.0f, 0.0f); glVertex2i( 0,  vOffset);
			glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth , vOffset);
			glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth, fboheight-vOffset);
			glTexCoord2f(0.0f, 1.0f); glVertex2i( 0, fboheight-vOffset);
		}
		glEnd();
	}
	glDisable(GL_TEXTURE_2D);

	////////////////////////////////////////////////////////////////////////////////////////////////////////
	//////////////////////////////////// Draw time test pattern ////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////
#if 1
	// Draw test time test pattern.
	if(/*fullScreenVideo && */testPattern && status[currentVideo] != astop)
	{
		glColor4f(0.0f, 0.0f, 0.0f, 0.0f);
		glBegin(GL_QUADS);
			glVertex2i(fbowidth, 0);
			glVertex2i(fbowidth, fboheight);
			glVertex2i(0, fboheight);
			glVertex2i(0, 0);
		glEnd();

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glColor4f(0.0,1.0,1.0,1.0); // half transparency

		double curTime = currGlobalTimer[currentVideo]+diff[currentVideo];
		float percent = curTime / duration[currentVideo];

		int barWidth = fbowidth / 10;
		double mod = 1/3.0f;//1/fps[currentVideo];
		double offset = fmod(curTime, mod) / mod;

		//offset = fmod(pts[currentVideo]*fps[currentVideo],1);
		//offset = fmod((offset * 30), 1);

		//printf("curTime: %09.6f -> Offset: %09.6f + mod: %09.6f -- (fbowidth*offset): %09.6f\n", curTime, offset, mod, (fbowidth*offset));
		glBegin(GL_QUADS);
			//// Sliding pattern
			//glVertex2i(ceil(fbowidth*offset) +  barWidth, 0);
			//glVertex2i(ceil(fbowidth*offset) +  barWidth, fboheight);
			//glVertex2i(ceil(fbowidth*offset), fboheight);
			//glVertex2i(ceil(fbowidth*offset), 0);

			// Bar pattern
			glVertex2i(barWidth * int(ceil(fbowidth*offset)/barWidth) +  barWidth, 0);
			glVertex2i(barWidth * int(ceil(fbowidth*offset)/barWidth) +  barWidth, fboheight);
			glVertex2i(barWidth * int(ceil(fbowidth*offset)/barWidth), fboheight);
			glVertex2i(barWidth * int(ceil(fbowidth*offset)/barWidth), 0);
		glEnd();

		glColor4f(1.0,1.0,1.0,1.0);

		for(int i=0; i<10; i++)
		{
			glBegin(GL_QUADS);
				glVertex2i(barWidth*i +  1, 0);
				glVertex2i(barWidth*i +  1, fboheight);
				glVertex2i(barWidth*i, fboheight);
				glVertex2i(barWidth*i, 0);
			glEnd();
		}

		//glBegin(GL_QUADS);
		//	glVertex2i(ceil(fbowidth*percent), vOffset);
		//	glVertex2i(ceil(fbowidth*percent), fboheight - 40);
		//	glVertex2i(0, fboheight - 40);
		//	glVertex2i(0, vOffset);
		//glEnd();

		glDisable(GL_BLEND);
	}
#endif
//#endif
#ifdef VIDEO_PLAYBACK
	if(help)
	{
	////////////////////////////////////////////////////////////////////////////////////////////////////////
	///////////////////////////////// Display debug information ////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////
#if 1
		glEnable(GL_BLEND);
		//glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
		//glDisable(GL_TEXTURE_2D);
		//glColor3f(0.5,0.5,0.5); // half transparency
		//glBegin(GL_QUADS);
		//	glVertex2i(fbowidth,0);
		//	glVertex2i(fbowidth, 140);
		//	glVertex2i(fbowidth-360,140);
		//	glVertex2i(fbowidth-360,0);
		//glEnd();
		//glBegin(GL_QUADS);
		//	glVertex2i(0,0);
		//	glVertex2i(0, 140);
		//	glVertex2i(340,140);
		//	glVertex2i(340,0);
		//glEnd();
		//glBegin(GL_QUADS);
		//	glVertex2i(0,fboheight);
		//	glVertex2i(0, fboheight-40);
		//	glVertex2i(fbowidth,fboheight-40);
		//	glVertex2i(fbowidth,fboheight);
		//glEnd();

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		// Draw debug info background quad
		glColor4f(0.0,0.0,0.0,0.5); // half transparency
		glBegin(GL_QUADS);
			glVertex2i(fbowidth,0);
			glVertex2i(fbowidth, 160);
			glVertex2i(fbowidth-360,160);
			glVertex2i(fbowidth-360,0);
		glEnd();
		// Draw help background quad
		glBegin(GL_QUADS);
			glVertex2i(0,0);
			glVertex2i(0, 160);
			glVertex2i(360,160);
			glVertex2i(360,0);
		glEnd();
		// Draw lower rectangular quad
		glBegin(GL_QUADS);
			glVertex2i(0,fboheight);
			glVertex2i(0, fboheight-40);
			glVertex2i(fbowidth,fboheight-40);
			glVertex2i(fbowidth,fboheight);
		glEnd();

		//glEnable(GL_BLEND);
		//glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		//glColor4f(1.0,1.0,1.0,0.25); // half transparency
		//glBegin(GL_QUADS);
		//	glVertex2i(fbowidth, fboheight);
		//	glVertex2i(fbowidth, fboheight - 40);
		//	glVertex2i(0, fboheight - 40);
		//	glVertex2i(0, fboheight);
		//glEnd();


		/* Draw a horizontal bar representing the current position of the specified video source.
		 */
		double curTime = currGlobalTimer[currentVideo]+diff[currentVideo];
		float percent = curTime / duration[currentVideo];

		// Draw AUDIO_VIDEO_SYNC status
		//glColor4f(1.0,0.0,0.0,0.75);
		glColor4f(0.0,0.0,1.0,0.75);
		//if(curTime - pts[currentVideo] < 0.01)
		//if(nextFrameDelay[currentVideo] > 1.0 && status[currentVideo] == aplay)
		if(((nextFrameDelay[currentVideo] < (1 / fps[currentVideo] * 1000 * 1.5)) && (nextFrameDelay[currentVideo] > (1 / fps[currentVideo] * 1000 * 0.5))) && status[currentVideo] == aplay)
			glColor4f(0.0,1.0,0.0,0.75);
			//glColor4f(0.0,0.0,1.0,0.75);
		//printf("%f - %f\n", (1 / fps[currentVideo] * 1000 * 1.25), (1 / fps[currentVideo] * 1000 * 0.75));

		glBegin(GL_QUADS);
			glVertex2i(fbowidth-10,145);
			glVertex2i(fbowidth-10, 155);
			glVertex2i(fbowidth-20,155);
			glVertex2i(fbowidth-20,145);
		glEnd();

		// Draw horizontal position bar
		//glColor4f(1.0,1.0,1.0,0.20);
		glBegin(GL_QUADS);
			glVertex2i(ceil(fbowidth*percent), fboheight);
			glVertex2i(ceil(fbowidth*percent), fboheight - 40);
			glVertex2i(0, fboheight - 40);
			glVertex2i(0, fboheight);
		glEnd();

		glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
		//glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		//glDisable(GL_BLEND);

		// Draw pause semi transparent textures
		if(!fullScreenVideo)
		{
#if MAXSTREAMS==1
			if(status[0] == apause)
			{
				glBegin(GL_QUADS);
					glVertex2i(fbowidth/2 - 30, fboheight/2 - 10);
					glVertex2i(fbowidth/2 - 30, fboheight/2 + 30);
					glVertex2i(fbowidth/2 + 75,fboheight/2 + 30);
					glVertex2i(fbowidth/2 + 75,fboheight/2 - 10);
				glEnd();
			}
#endif
#if MAXSTREAMS==2
			if(status[0] == apause)
			{
				glBegin(GL_QUADS);
					glVertex2i(fbowidth/2 - fbowidth/4 - 30, fboheight/2 - 10);
					glVertex2i(fbowidth/2 - fbowidth/4 - 30, fboheight/2 + 30);
					glVertex2i(fbowidth/2 - fbowidth/4 + 75,fboheight/2 + 30);
					glVertex2i(fbowidth/2 - fbowidth/4 + 75,fboheight/2 - 10);
				glEnd();
			}
			if(status[1] == apause)
			{
				glBegin(GL_QUADS);
					glVertex2i(fbowidth/2 + fbowidth/4 - 30, fboheight/2 - 10);
					glVertex2i(fbowidth/2 + fbowidth/4 - 30, fboheight/2 + 30);
					glVertex2i(fbowidth/2 + fbowidth/4 + 75,fboheight/2 + 30);
					glVertex2i(fbowidth/2 + fbowidth/4 + 75,fboheight/2 - 10);
				glEnd();
			}
#endif
#if MAXSTREAMS==4
			if(status[0] == apause)
			{
				glBegin(GL_QUADS);
					glVertex2i(fbowidth/2 - fbowidth/4 - 30, fboheight/2 - fboheight/4 - 10);
					glVertex2i(fbowidth/2 - fbowidth/4 - 30, fboheight/2 - fboheight/4 + 30);
					glVertex2i(fbowidth/2 - fbowidth/4 + 75,fboheight/2 - fboheight/4 + 30);
					glVertex2i(fbowidth/2 - fbowidth/4 + 75,fboheight/2 - fboheight/4 - 10);
				glEnd();
			}
			if(status[1] == apause)
			{
				glBegin(GL_QUADS);
					glVertex2i(fbowidth/2 + fbowidth/4 - 30, fboheight/2 - fboheight/4 - 10);
					glVertex2i(fbowidth/2 + fbowidth/4 - 30, fboheight/2 - fboheight/4 + 30);
					glVertex2i(fbowidth/2 + fbowidth/4 + 75,fboheight/2 - fboheight/4 + 30);
					glVertex2i(fbowidth/2 + fbowidth/4 + 75,fboheight/2 - fboheight/4 - 10);
				glEnd();
			}
			if(status[2] == apause)
			{
				glBegin(GL_QUADS);
					glVertex2i(fbowidth/2 - fbowidth/4 - 30, fboheight/2 + fboheight/4 - 10);
					glVertex2i(fbowidth/2 - fbowidth/4 - 30, fboheight/2 + fboheight/4 + 30);
					glVertex2i(fbowidth/2 - fbowidth/4 + 75,fboheight/2 + fboheight/4 + 30);
					glVertex2i(fbowidth/2 - fbowidth/4 + 75,fboheight/2 + fboheight/4 - 10);
				glEnd();
			}
			if(status[3] == apause)
			{
				glBegin(GL_QUADS);
					glVertex2i(fbowidth/2 + fbowidth/4 - 30, fboheight/2 + fboheight/4 - 10);
					glVertex2i(fbowidth/2 + fbowidth/4 - 30, fboheight/2 + fboheight/4 + 30);
					glVertex2i(fbowidth/2 + fbowidth/4 + 75,fboheight/2 + fboheight/4 + 30);
					glVertex2i(fbowidth/2 + fbowidth/4 + 75,fboheight/2 + fboheight/4 - 10);
				glEnd();
			}
#endif
		}
		else
		{
			if(status[currentVideo] == apause)
			{
				glBegin(GL_QUADS);
					glVertex2i(fbowidth/2 - 30, fboheight/2  - 10);
					glVertex2i(fbowidth/2 - 30, fboheight/2 + 30);
					glVertex2i(fbowidth/2 + 75,fboheight/2 + 30);
					glVertex2i(fbowidth/2 + 75,fboheight/2 - 10);
				glEnd();
			}
		}

		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
		glEnable(GL_TEXTURE_2D);
		glDisable(GL_BLEND);
#endif

	////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////// Draw fonts  /////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////

#if 1
#ifdef USE_FREETYPE_FONTS
		//glClearColor( 1, 0, 0, 1 );
		//glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

		//vertex_buffer_delete(text_buffer2);
		//text_buffer2 = vertex_buffer_new( "v3f:t2f:c4f" );

		vertex_buffer_clear(text_buffer2);

		vec2 pen = {0,0};
		vec4 black = {1,1,1,1};
		wchar_t hText[256];

		if(!fullScreenVideo)
		{
#if MAXSTREAMS==1
			if(status[0] == apause)
			{
				pen.x = fbowidth/2 - 10.0f;
				pen.y = fboheight/2 + 15.0f;
				swprintf (hText, 256, L"%s", L"PAUSED");
				freetype_add_text( text_buffer2, font, hText, &black, &pen );
			}
#endif
#if MAXSTREAMS==2
			// Draw specified text to screen.
			if(status[0] == apause)
			{
				pen.x = fbowidth/2 - fbowidth/4 - 10.0f;
				pen.y = fboheight/2 + 15.0f;
				swprintf (hText, 256, L"%s", L"PAUSED");
				freetype_add_text( text_buffer2, font, hText, &black, &pen );
			}
			if(status[1] == apause)
			{
				pen.x = fbowidth/2 + fbowidth/4 - 10.0f;
				pen.y = fboheight/2 + 15.0f;
				swprintf (hText, 256, L"%s", L"PAUSED");
				freetype_add_text( text_buffer2, font, hText, &black, &pen );
			}
#endif
#if MAXSTREAMS==4
			// Draw specified text to screen.
			if(status[0] == apause)
			{
				pen.x = fbowidth/2 - fbowidth/4 - 10.0f;
				pen.y = fboheight/2 - fboheight/4 + 15.0f;
				swprintf (hText, 256, L"%s", L"PAUSED");
				freetype_add_text( text_buffer2, font, hText, &black, &pen );
			}
			if(status[1] == apause)
			{
				pen.x = fbowidth/2 + fbowidth/4 - 10.0f;
				pen.y = fboheight/2 - fboheight/4 + 15.0f;
				swprintf (hText, 256, L"%s", L"PAUSED");
				freetype_add_text( text_buffer2, font, hText, &black, &pen );
			}
			// Draw specified text to screen.
			if(status[2] == apause)
			{
				pen.x = fbowidth/2 - fbowidth/4 - 10.0f;
				pen.y = fboheight/2 + fboheight/4 + 15.0f;
				swprintf (hText, 256, L"%s", L"PAUSED");
				freetype_add_text( text_buffer2, font, hText, &black, &pen );
			}
			if(status[3] == apause)
			{
				pen.x = fbowidth/2 + fbowidth/4 - 10.0f;
				pen.y = fboheight/2 + fboheight/4 + 15.0f;
				swprintf (hText, 256, L"%s", L"PAUSED");
				freetype_add_text( text_buffer2, font, hText, &black, &pen );
			}
#endif
		}
		else
		{
			if(status[currentVideo] == apause)
			{
				pen.x = fbowidth/2 - 10.0f;
				pen.y = fboheight/2 + 15.0f;
				swprintf (hText, 256, L"%s", L"PAUSED");
				freetype_add_text( text_buffer2, font, hText, &black, &pen );
			}
		}
#endif
		// Print the currently selected video debug info.
		printVideoDebugInfo(currentVideo, 0, 0);

#ifdef USE_FREETYPE_FONTS
		glEnable( GL_BLEND );
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
		glEnable( GL_TEXTURE_2D );
		glBindTexture( GL_TEXTURE_2D, atlas->id );
		//glBegin(GL_QUADS);
		//	glTexCoord2f(0.0f, 0.0f); glVertex2i( 0,  0);
		//	glTexCoord2f(1.0f, 0.0f); glVertex2i( fbowidth , 0);
		//	glTexCoord2f(1.0f, 1.0f); glVertex2i( fbowidth, fboheight);
		//	glTexCoord2f(0.0f, 1.0f); glVertex2i( 0, fboheight);
		//glEnd();
		vertex_buffer_render( text_buffer, GL_TRIANGLES, "vt" );
		vertex_buffer_render( text_buffer2, GL_TRIANGLES, "vtc" );

		glBindTexture( GL_TEXTURE_2D, 0);
		glDisable( GL_TEXTURE_2D );
		glDisable( GL_BLEND );
#endif
#endif
	}
	////////////////////////////////////////////////////////////////////////////////////////////////////////
	////////////////////////////////////////// End draw ////////////////////////////////////////////////////
	////////////////////////////////////////////////////////////////////////////////////////////////////////
#endif
	//glBindTexture( GL_TEXTURE_2D, 0);
	//glDisable( GL_TEXTURE_2D );
	//glDisable( GL_BLEND );

#endif	

	//glFlush();
	//glFinish();
	CalculateFrameRate();
	//wglMakeCurrent(NULL, NULL);

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("DrawScene GL err - %s.\n", gluErrorString(err));
	}
	//Sleep(5);
#endif

	//glClear(GL_COLOR_BUFFER_BIT);
#if 0
	//glDisable(GL_TEXTURE_2D);
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	//glEnable(GL_TEXTURE_2D);
	//glDisable(GL_BLEND);

	glBegin(GL_TRIANGLES);
        glVertex2f(0, 0);
        glVertex2f(fbowidth/2, fboheight/2);
        glVertex2f(fbowidth, 0);
    glEnd();

	//	CalculateFrameRate();
	//printf("\tFPS: %d\n", currFPS);

	GLenum err = glGetError();
	if(err != GL_NO_ERROR)
	{
		printf("DrawScene GL err.\n");
	}
#endif


	double timeBeforeSwapTime = getGlobalVideoTimer(currentVideo) - profileTime;

	SwapBuffers(getHDC());
	//glFinish();

	double timeAfterSwapTime = getGlobalVideoTimer(currentVideo) - profileTime;
	//if(status[currentVideo] == aplay)
	//	printf("TOTAL Time: %09.6f - WORK TIME: %09.6f - SWAP Time: %09.6f\n", timeAfterSwapTime, timeBeforeSwapTime, timeAfterSwapTime - timeBeforeSwapTime);

	//if(timeAfterSwapTime > (1000 / 60.0)+1 )
		//printf("=======================Missed V-SYNC\n");
}

#ifdef WGL_FONTS
/* Draw a piece of text to the screen
 */
void displayTextString(string outputString, float xCoord, float yCoord, int fontSize = 12, string fontType = "Arial", float r = 1, float g = 1, float b = 1)
{
	//delete Font;
	//Font = new GLFont(getHDC(), (char *)fontType.c_str(), fontSize);//Segoe UI Semibold;//Tahoma Bold;//Georgia

    // set the text colour to white
    glColor3f(r,g,b);

    // Draw the display tips on screen
    //smallFont->setposition((float)xCoord, (float)yCoord);
    //smallFont->displaytext(outputString.c_str());
	Font->setposition((float)xCoord, (float)yCoord);
    Font->displaytext(outputString.c_str());
}

/* Display debug hud.
 */
void displayHelp()
{
	glDisable(GL_LIGHTING);
    // set the text colour to white
    glColor3f(1,1,1);

    // create an array of display tips
    const int cOptions = 8;
    std::string helpText[cOptions] = { 
                         "L = Load a video file.",
						 "R = Reset the current video.",
                         "S = Stop video.", 
                         "VK_UP = Add 0.001 to global delay.",
						 "VK_DOWN = Minus 0.001 from global delay.",
						 "VK_RIGHT = Add 0.1 to global delay.",
						 "VK_LEFT = Minus 0.1 from global delay.",
						 "E = Exit or VK_ESCAPE."
            };

    // Draw the display tips on screen
    float oPosition = 20.0;
    for(int i=0; i<(cOptions); i++, oPosition += 15)
		displayTextString(helpText[i], 10.0f, oPosition, 20, "Verdana");
}
#endif
/* Handle windows key down events.
*/
void keyHandle(char key)
{
#ifdef VIDEO_PLAYBACK
	if(key == 'Z') {
		volume -= 0.05f;
		changeVideoVolume(currentVideo, volume);
	}
	else if(key == 'X') {
		volume += 0.05f;
		changeVideoVolume(currentVideo, volume);
	}
	else if (key == '1') {
		glClearColor(0, 0, 1, 0);
	}
	else if (key == '2') {
		glClearColor(1, 0, 0, 0);
	}
	else if (key == '3') {
		glClearColor(0, 0, 0, 0);
	}
#endif

	if(key == VK_UP)
	{
		if(!(GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000))
			g_Delay[currentVideo] += 0.001;
	}
	else if(key == VK_DOWN)
	{
		if(!(GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000))
			g_Delay[currentVideo] -= 0.001;
	}
	else if(key == VK_LEFT)
	{
		if(!(GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000))
			g_Delay[currentVideo] -= 0.1;
	}
	else if(key == VK_RIGHT)
	{
		if(!(GetAsyncKeyState(VK_SHIFT) & 0x8000) && !(GetAsyncKeyState(VK_CONTROL) & 0x8000))
			g_Delay[currentVideo] += 0.1;
	}
}

/* Handle windows key up events.
*/
void upKeyHandle(char key)
{
#ifdef VIDEO_PLAYBACK
	if(key == VK_UP)
	{
		if((GetAsyncKeyState(VK_SHIFT) & 0x8000))
		{
			seekDur = 50.0;
			seekVideo(currentVideo, seekDur);
		}
		else if((GetAsyncKeyState(VK_CONTROL) & 0x8000))
		{
			seekDur = 10.0;
			seekVideo(currentVideo, seekDur);
		}
	}
	else if(key == VK_DOWN)
	{
		if((GetAsyncKeyState(VK_SHIFT) & 0x8000))
		{
			seekDur = -50.0;
			seekVideo(currentVideo, seekDur);
		}
		else if((GetAsyncKeyState(VK_CONTROL) & 0x8000))
		{
			seekDur = -10.0;
			seekVideo(currentVideo, seekDur);
		}
	}
	else if(key == VK_LEFT)
	{
		if((GetAsyncKeyState(VK_SHIFT) & 0x8000))
		{
			seekDur = -100.0;
			seekVideo(currentVideo, seekDur);
		}
		else if((GetAsyncKeyState(VK_CONTROL) & 0x8000))
		{
			seekDur = -30.0;
			seekVideo(currentVideo, seekDur);
		}
	}
	else if(key == VK_RIGHT)
	{
		if((GetAsyncKeyState(VK_SHIFT) & 0x8000))
		{
			seekDur = 100.0;
			seekVideo(currentVideo, seekDur);
		}
		else if((GetAsyncKeyState(VK_CONTROL) & 0x8000))
		{
			seekDur = 30.0;
			seekVideo(currentVideo, seekDur);
		}
	}
	else if( key == 'S') {
		//notifyStopOrRestartVideo(currentVideo);
		stopVideo(currentVideo);
		seekDur = 0.0;
		volume = 1.0;
	}
    else if(key == VK_F1)
	{
		help = !help;
    }
    else if(key == ' ')
	{
		pauseVideo(currentVideo);
    }
    else if(key == 'I')
	{
		pboMode = 1;
		printf("pboMode: %d\n", pboMode);
    }
    else if(key == 'O')
	{
		pboMode = 2;
		printf("pboMode: %d\n", pboMode);
    }
    else if(key == 'P')
	{
		pboMode = 3;
		printf("pboMode: %d\n", pboMode);
    }
	else if(key == -37)//'[')
	{
		pboMode = 4;
		printf("pboMode: %d\n", pboMode);
    }
	else if(key == -35)
	{
		pboMode = 5;
		printf("pboMode: %d\n", pboMode);
    }
    else if(key == 'M')
	{
		if(muted[currentVideo])
		{
			changeVideoVolume(currentVideo, volume);
			muted[currentVideo] = false;
		}
		else
		{
			printf("Muting video stream: %d", currentVideo);
			changeVideoVolume(currentVideo, 0.0);
			muted[currentVideo] = true;
		}
    }
	else if( key == 'L')
	{
		OPENFILENAME ofn;
		char szFileName[MAX_PATH] = "";

		ZeroMemory(&ofn, sizeof(ofn));

		ofn.lStructSize = sizeof(ofn); // SEE NOTE BELOW
		ofn.hwndOwner = gethWnd();
		ofn.lpstrFilter = "All Files (*.*)\0*.*\0MP4 Files (*.mp4)\0*.mp4\0";
		ofn.lpstrFile = szFileName;
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		ofn.lpstrDefExt = "mp4";

		if(GetOpenFileName(&ofn))
		{
			// Do something useful with the filename stored in szFileName
			//videoName = szFileName;
			strcpy(videoName, szFileName);
			//notifyScreenSyncLoadVideo(videoName, currentVideo);
			stopVideo(currentVideo);

#ifdef USE_ODBASE
			if (!(ODBase::fileExists( videoName )))
#else
			/*
			DWORD fileType = GetFileAttributes(szFileName);
			bool isDir = fileType & FILE_ATTRIBUTE_DIRECTORY;
			if(INVALID_FILE_ATTRIBUTES == GetFileAttributes(szFileName) && GetLastError()==ERROR_FILE_NOT_FOUND)
			{
				//File not found
			}
			*/

			int retval = PathFileExists(szFileName);// Search for the presence of a file with a true result.
			if(retval == 0)
#endif
			{

				char buf[260];
				sprintf(buf, "Could not find video file: %s", videoName);
				//throw ODBase::ErrorEvent("Could not find video file '%s'",videoName);
				MessageBox(NULL, buf, "Error", NULL);
				//printf(NULL, "Could not find video file", "Error", NULL);
			}
			else
			{
				while (!(loadVideo(videoName, currentVideo))){ Sleep(100); /*stopVideo(currentVideo);*/  }
				//if(loadVideo(szFileName, 0))
				{
					playVideo(currentVideo);
				}
			}
		}
		else
			printf("Could not load file.\n");
	}
	else if(key == 'E') {
			shutdownPlayer();
			//PostQuitMessage(0);	// Send A Quit Message
    }
    else if(key == 'R')
	{
		//notifyScreenSyncLoadVideo(videoName, currentVideo);
		stopVideo(currentVideo);
		//Sleep(100);

#ifdef USE_ODBASE
		if (!(ODBase::fileExists( videoName )))
#else
		int retval = PathFileExists(videoName);// Search for the presence of a file with a true result.
		if(retval == 0)
#endif
		{
			char buf[260];
			sprintf(buf, "Could not find video file: %s", videoName);
			//throw ODBase::ErrorEvent("Could not find video file '%s'",videoName);
			MessageBox(NULL, buf, "Error", NULL);
			//printf(NULL, "Could not find video file", "Error", NULL);
		}
		else
		{
			while (!(loadVideo(videoName, currentVideo))){ Sleep(100); /*stopVideo(currentVideo);*/  }
			playVideo(currentVideo);
		}
    }
	else if (key == VK_ADD) {
		mouse_x = mouse_y = -1;
		currentVideo += 1;
		if(currentVideo == MAXSTREAMS)
			currentVideo = 0;
	}
	else if(key == 'T') {
		testPattern = !testPattern;
	}
#endif
}

/* Shutdown the video player.
*/
void shutdownPlayer()
{
#ifdef VIDEO_PLAYBACK
	for (int i=0; i<MAXSTREAMS; i++)
		stopVideo(i);

	printf("All streams stopped\n");

	#ifdef LOCAL_AUDIO
		//stopAudioThread = true;
		while(stopAudioThread == true) { 
			printf("Waiting for audio thread to shutdown...");
			Sleep(100);
		}
	#endif
	printf("All threads shutdown.\n");
	PostQuitMessage(0);	// Send A Quit Message
	//exit(0);
#endif
}