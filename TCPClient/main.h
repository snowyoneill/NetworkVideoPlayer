#pragma comment(linker, "/subsystem:console /entry:WinMainCRTStartup")
//#pragma comment(linker, "/subsystem:windows /entry:WinMainCRTStartup ")

//#define VIDEO_PLAYBACK

#ifdef VIDEO_PLAYBACK
#include "videoplayback.h"
#else
#include "TCPClient.h"
#endif


#ifdef _WIN32
	#ifdef USE_GLEW
		#define GLEW_STATIC
		//#define GLUT_STATIC_LIB
		//#define CEGUI_STATIC
		#include "GL/glew.h"
		#include "GL/wglew.h"
	#else
		#undef GL_GLEXT_PROTOTYPES
		#include <windows.h>
		#include "GL/gl.h"
		#include "GL/glu.h"
		#include "glext.h"


		//#define glGetProcAddress(n) wglGetProcAddress(n)
		#ifdef MANUALLY_DECLARE_ENTRY_POINTS

		#ifndef APIENTRY
		#define APIENTRY
		#endif
		#ifndef APIENTRYP
		#define APIENTRYP APIENTRY *
		#endif
		#ifndef GLAPI
		#define GLAPI extern
		#endif

		//typedef void (APIENTRYP PFNGLBINDFRAMEBUFFEREXTPROC) (GLenum target, GLuint framebuffer);
		//typedef void (APIENTRY * PFNGLGENFRAMEBUFFERSEXTPROC)(GLsizei n, GLuint *framebuffers);

		typedef GLboolean (APIENTRYP PFNGLISRENDERBUFFEREXTPROC) (GLuint renderbuffer);
		typedef void (APIENTRYP PFNGLBINDRENDERBUFFEREXTPROC) (GLenum target, GLuint renderbuffer);
		typedef void (APIENTRYP PFNGLDELETERENDERBUFFERSEXTPROC) (GLsizei n, const GLuint *renderbuffers);
		typedef void (APIENTRYP PFNGLGENRENDERBUFFERSEXTPROC) (GLsizei n, GLuint *renderbuffers);
		typedef void (APIENTRYP PFNGLRENDERBUFFERSTORAGEEXTPROC) (GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
		typedef void (APIENTRYP PFNGLGETRENDERBUFFERPARAMETERIVEXTPROC) (GLenum target, GLenum pname, GLint *params);
		typedef GLboolean (APIENTRYP PFNGLISFRAMEBUFFEREXTPROC) (GLuint framebuffer);
		typedef void (APIENTRYP PFNGLBINDFRAMEBUFFEREXTPROC) (GLenum target, GLuint framebuffer);
		typedef void (APIENTRYP PFNGLDELETEFRAMEBUFFERSEXTPROC) (GLsizei n, const GLuint *framebuffers);
		typedef void (APIENTRYP PFNGLGENFRAMEBUFFERSEXTPROC) (GLsizei n, GLuint *framebuffers);
		typedef GLenum (APIENTRYP PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC) (GLenum target);
		typedef void (APIENTRYP PFNGLFRAMEBUFFERTEXTURE1DEXTPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
		typedef void (APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DEXTPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
		typedef void (APIENTRYP PFNGLFRAMEBUFFERTEXTURE3DEXTPROC) (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset);
		typedef void (APIENTRYP PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC) (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
		typedef void (APIENTRYP PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVEXTPROC) (GLenum target, GLenum attachment, GLenum pname, GLint *params);
		typedef void (APIENTRYP PFNGLGENERATEMIPMAPEXTPROC) (GLenum target);

		typedef void (APIENTRYP PFNGLCOLORMASKIPROC) (GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a);
		typedef void (APIENTRYP PFNGLGETBOOLEANI_VPROC) (GLenum target, GLuint index, GLboolean *data);
		typedef void (APIENTRYP PFNGLGETINTEGERI_VPROC) (GLenum target, GLuint index, GLint *data);
		typedef void (APIENTRYP PFNGLENABLEIPROC) (GLenum target, GLuint index);
		typedef void (APIENTRYP PFNGLDISABLEIPROC) (GLenum target, GLuint index);
		typedef GLboolean (APIENTRYP PFNGLISENABLEDIPROC) (GLenum target, GLuint index);
		typedef void (APIENTRYP PFNGLBEGINTRANSFORMFEEDBACKPROC) (GLenum primitiveMode);
		typedef void (APIENTRYP PFNGLENDTRANSFORMFEEDBACKPROC) (void);
		typedef void (APIENTRYP PFNGLBINDBUFFERRANGEPROC) (GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
		typedef void (APIENTRYP PFNGLBINDBUFFERBASEPROC) (GLenum target, GLuint index, GLuint buffer);
		typedef void (APIENTRYP PFNGLTRANSFORMFEEDBACKVARYINGSPROC) (GLuint program, GLsizei count, const GLchar* *varyings, GLenum bufferMode);
		typedef void (APIENTRYP PFNGLGETTRANSFORMFEEDBACKVARYINGPROC) (GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLsizei *size, GLenum *type, GLchar *name);
		typedef void (APIENTRYP PFNGLCLAMPCOLORPROC) (GLenum target, GLenum clamp);
		typedef void (APIENTRYP PFNGLBEGINCONDITIONALRENDERPROC) (GLuint id, GLenum mode);
		typedef void (APIENTRYP PFNGLENDCONDITIONALRENDERPROC) (void);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBIPOINTERPROC) (GLuint index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer);
		typedef void (APIENTRYP PFNGLGETVERTEXATTRIBIIVPROC) (GLuint index, GLenum pname, GLint *params);
		typedef void (APIENTRYP PFNGLGETVERTEXATTRIBIUIVPROC) (GLuint index, GLenum pname, GLuint *params);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI1IPROC) (GLuint index, GLint x);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI2IPROC) (GLuint index, GLint x, GLint y);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI3IPROC) (GLuint index, GLint x, GLint y, GLint z);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI4IPROC) (GLuint index, GLint x, GLint y, GLint z, GLint w);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI1UIPROC) (GLuint index, GLuint x);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI2UIPROC) (GLuint index, GLuint x, GLuint y);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI3UIPROC) (GLuint index, GLuint x, GLuint y, GLuint z);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI4UIPROC) (GLuint index, GLuint x, GLuint y, GLuint z, GLuint w);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI1IVPROC) (GLuint index, const GLint *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI2IVPROC) (GLuint index, const GLint *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI3IVPROC) (GLuint index, const GLint *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI4IVPROC) (GLuint index, const GLint *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI1UIVPROC) (GLuint index, const GLuint *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI2UIVPROC) (GLuint index, const GLuint *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI3UIVPROC) (GLuint index, const GLuint *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI4UIVPROC) (GLuint index, const GLuint *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI4BVPROC) (GLuint index, const GLbyte *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI4SVPROC) (GLuint index, const GLshort *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI4UBVPROC) (GLuint index, const GLubyte *v);
		typedef void (APIENTRYP PFNGLVERTEXATTRIBI4USVPROC) (GLuint index, const GLushort *v);
		typedef void (APIENTRYP PFNGLGETUNIFORMUIVPROC) (GLuint program, GLint location, GLuint *params);
		typedef void (APIENTRYP PFNGLBINDFRAGDATALOCATIONPROC) (GLuint program, GLuint color, const GLchar *name);
		typedef GLint (APIENTRYP PFNGLGETFRAGDATALOCATIONPROC) (GLuint program, const GLchar *name);
		typedef void (APIENTRYP PFNGLUNIFORM1UIPROC) (GLint location, GLuint v0);
		typedef void (APIENTRYP PFNGLUNIFORM2UIPROC) (GLint location, GLuint v0, GLuint v1);
		typedef void (APIENTRYP PFNGLUNIFORM3UIPROC) (GLint location, GLuint v0, GLuint v1, GLuint v2);
		typedef void (APIENTRYP PFNGLUNIFORM4UIPROC) (GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3);
		typedef void (APIENTRYP PFNGLUNIFORM1UIVPROC) (GLint location, GLsizei count, const GLuint *value);
		typedef void (APIENTRYP PFNGLUNIFORM2UIVPROC) (GLint location, GLsizei count, const GLuint *value);
		typedef void (APIENTRYP PFNGLUNIFORM3UIVPROC) (GLint location, GLsizei count, const GLuint *value);
		typedef void (APIENTRYP PFNGLUNIFORM4UIVPROC) (GLint location, GLsizei count, const GLuint *value);
		typedef void (APIENTRYP PFNGLTEXPARAMETERIIVPROC) (GLenum target, GLenum pname, const GLint *params);
		typedef void (APIENTRYP PFNGLTEXPARAMETERIUIVPROC) (GLenum target, GLenum pname, const GLuint *params);
		typedef void (APIENTRYP PFNGLGETTEXPARAMETERIIVPROC) (GLenum target, GLenum pname, GLint *params);
		typedef void (APIENTRYP PFNGLGETTEXPARAMETERIUIVPROC) (GLenum target, GLenum pname, GLuint *params);
		typedef void (APIENTRYP PFNGLCLEARBUFFERIVPROC) (GLenum buffer, GLint drawbuffer, const GLint *value);
		typedef void (APIENTRYP PFNGLCLEARBUFFERUIVPROC) (GLenum buffer, GLint drawbuffer, const GLuint *value);
		typedef void (APIENTRYP PFNGLCLEARBUFFERFVPROC) (GLenum buffer, GLint drawbuffer, const GLfloat *value);
		typedef void (APIENTRYP PFNGLCLEARBUFFERFIPROC) (GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);
		typedef const GLubyte * (APIENTRYP PFNGLGETSTRINGIPROC) (GLenum name, GLuint index);
	#endif

	//#ifdef WGL_WGLEXT_PROTOTYPES 
	//extern HGLRC WINAPI wglCreateContextAttribsARB (HDC, HGLRC, const int *);
	//HGLRC wglCreateContextAttribsARB(HDC hDC, HGLRC hshareContext, const int *attribList);
	//#endif /* WGL_WGLEXT_PROTOTYPES */ 


	typedef HGLRC (APIENTRYP PFNWGLCREATECONTEXTATTRIBSARBPROC) (HDC hDC, HGLRC hShareContext, const int* attribList);

	extern PFNGLACTIVETEXTUREARBPROC        glActiveTextureARB;
	extern PFNGLUSEPROGRAMOBJECTARBPROC     glUseProgramObjectARB;
	extern PFNGLCREATEPROGRAMOBJECTARBPROC  glCreateProgramObjectARB;
	extern PFNGLCREATESHADEROBJECTARBPROC   glCreateShaderObjectARB;
	extern PFNGLSHADERSOURCEARBPROC         glShaderSourceARB;
	extern PFNGLCOMPILESHADERARBPROC        glCompileShaderARB;
	extern PFNGLATTACHOBJECTARBPROC         glAttachObjectARB;
	extern PFNGLLINKPROGRAMARBPROC          glLinkProgramARB;
	extern PFNGLDELETEOBJECTARBPROC         glDeleteObjectARB;
	extern PFNGLGETINFOLOGARBPROC           glGetInfoLogARB;
	extern PFNGLGETOBJECTPARAMETERIVARBPROC glGetObjectParameterivARB;
	extern PFNGLGETUNIFORMLOCATIONARBPROC   glGetUniformLocationARB;
	extern PFNGLUNIFORM1IARBPROC            glUniform1iARB;
	extern PFNGLUNIFORM1FARBPROC            glUniform1fARB;
	extern PFNGLUNIFORM2FARBPROC            glUniform2fARB;
	extern PFNGLUNIFORM3FARBPROC            glUniform3fARB;
	extern PFNGLGENFRAMEBUFFERSEXTPROC      glGenFramebuffersEXT;
	extern PFNGLBINDFRAMEBUFFEREXTPROC      glBindFramebufferEXT;
	extern PFNGLDELETEFRAMEBUFFERSEXTPROC   glDeleteFramebuffersEXT;
	extern PFNGLFRAMEBUFFERTEXTURE2DEXTPROC glFramebufferTexture2DEXT;

	extern PFNGLGENRENDERBUFFERSEXTPROC		glGenRenderbuffersEXT;
	extern PFNGLBINDRENDERBUFFEREXTPROC		glBindRenderbufferEXT;
	extern PFNGLRENDERBUFFERSTORAGEEXTPROC	glRenderbufferStorageEXT;
	extern PFNGLFRAMEBUFFERRENDERBUFFEREXTPROC glFramebufferRenderbufferEXT;
	extern PFNGLCHECKFRAMEBUFFERSTATUSEXTPROC	glCheckFramebufferStatusEXT;

	extern PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB;

	extern PFNGLGENBUFFERSPROC glGenBuffers;
	extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;
	extern PFNGLBINDBUFFERPROC glBindBuffer;
	extern PFNGLBUFFERDATAPROC glBufferData;
	extern PFNGLMAPBUFFERPROC glMapBuffer;
	extern PFNGLUNMAPBUFFERPROC glUnmapBuffer;
	extern PFNGLBUFFERSUBDATAPROC glBufferSubData;

	#endif
#endif

#define WGL_CONTEXT_MAJOR_VERSION_ARB		0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB		0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB			0x2093
#define WGL_CONTEXT_FLAGS_ARB				0x2094

//#ifdef VIDEO_PLAYBACK
#pragma comment( lib, "opengl32.lib" )
//#pragma comment( lib, "glu32.lib" )
//#pragma comment( lib, "glut32.lib" )
#pragma comment( lib, "./lib/x64/glu32.lib" )
#ifdef USE_GLEW
	#ifdef GLEW_STATIC 
		#pragma comment( lib, "glew32s.lib" )
	#else
		#pragma comment( lib, "glew32.lib" )
	#endif
#endif
//#endif

int Init(char* cmdline);
//int Init(char** cmdline, int argCount);
int InitGL();
void ReSizeGLScene(GLsizei width, GLsizei height);
void DrawGLScene();
void keyHandle(char key);
void upKeyHandle(char key);
bool loadBG();
void shutdownPlayer();
void setWindowTitle(int clientID, string text);

extern void notifyStopOrRestartVideo(int videounit);
extern void notifyScreenSyncLoadVideo(string videoName, int videounit);