#include "pbo.h"
#include "main.h"
//#define USE_GLEW
//#ifdef _WIN32
//	#ifdef USE_GLEW
//		#define GLEW_STATIC
//		#include "GL/glew.h"
//		#include "GL/wglew.h"
//	#endif
//#endif
#include "frameloader.h"
#include <stdio.h>
#ifdef USE_ODBASE
#include "ODBase.h"
#endif
#include <assert.h>

#define MAX_PBOS 5
int pboMode = 5;

#ifdef USE_ODBASE
	ODBase::Lock pboMutex("pboMutex");
#else
	HANDLE pboMutex;
#endif

GLuint	pboIds[MAXSTREAMS][MAX_PBOS]; // IDs of PBO
int		pbowidth[MAXSTREAMS];
int		pboheight[MAXSTREAMS];
bool	pboSupported[MAXSTREAMS];
int		size[MAXSTREAMS];
int		pbo_size[MAXSTREAMS];
int		qindex[MAXSTREAMS];
int		pindex[MAXSTREAMS];

double pboPTS[MAX_PBOS];

////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////// Timer //////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
__int64 pboCounter[MAXSTREAMS];
double PCFreqPBO = 0.0;
void startPBOTimer(int videounit)
{
    LARGE_INTEGER li;
    if(!QueryPerformanceFrequency(&li))
        printf("QueryPerformanceFrequency failed!\n");

    PCFreqPBO = double(li.QuadPart)/1000.0;

    QueryPerformanceCounter(&li);
    pboCounter[videounit] = li.QuadPart;
}
double getPBOTimer(int videounit)
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
	double globaltimer = double(li.QuadPart-pboCounter[videounit])/PCFreqPBO;

	return globaltimer;
}
*/
////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////// PBO //////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
//void createPBOs()
//{
//	glGenBuffers(2, pboIds[side]);
//}
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;

void intiPBOs(int w, int h, int side)
{
#ifndef USE_ODBASE
	pboMutex = CreateMutex(NULL, false, NULL);
#endif

	#ifdef USE_GLEW
		if (glewIsSupported("GL_ARB_pixel_buffer_object")) 
	#else
		if (true) 
	#endif
	{
		//glDeleteBuffersARB(2, pboIds[side]);
		glDeleteBuffers(MAX_PBOS, pboIds[side]);

		pbowidth[side] = w;
		pboheight[side] = h;

		pboSupported[side] = true;
		size[side] = (sizeof(unsigned char)) * pbowidth[side] * pboheight[side] * 4;
		if(pboSupported[side])
		{
			// create 2 pixel buffer objects, you need to delete them when program exits.
			// glBufferDataARB with NULL pointer reserves only memory space.
			glGenBuffers(MAX_PBOS, pboIds[side]);

			for(int i=0; i<MAX_PBOS; i++)
			{
				glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][i]);
				//glBufferDataARB(GL_PIXEL_UNPACK_BUFFER_ARB, size[side], 0, GL_STREAM_DRAW_ARB);
				glBufferData(GL_PIXEL_UNPACK_BUFFER, size[side], 0, GL_STREAM_DRAW);
			}
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}
		qindex[side] = 0;
		//pindex[side] = 0;
		//size[side] = 0;
	}
	//startPBOTimer(side);
	startGlobalVideoTimer(side);
	//_beginthread(updatePBOThreads, 0, (void *)side);
}

void updatePixels(int side, GLbyte* dst, int size)
{
   static int color = 0;

    if(!dst)
        return;

    int* ptr = (int*)dst;

    for(int i = 0; i < pboheight[side]; ++i)
    {
        for(int j = 0; j < pbowidth[side]; ++j)
        {
            *ptr = color;
            ++ptr;
        }
        color += 50;   // add an arbitary number (no meaning)
    }
    ++color;            // scroll down
}

//void updatePBOs(void* arg)

bool pbosFull(int side)
{
	return (pbo_size[side] < pboMode);
}

bool pbosEmpty(int side)
{
	return pbo_size[side] == 0;
}

void updatePBOs(int side)
{
	//int side = (int)arg;
	//static int qindex = 0;
	//qindex[side] = 0;
    int nextIndex = 0;                  // pbo index used for next frame

	if(!(pbo_size[side] < pboMode))
	{
		//printf("Pbo already uploaded - size = %d.\n", pbo_size[side]);
		return;
	}

    if(pboMode > 0)
    {
		// "index" is used to copy pixels from a PBO to a texture object
        // "nextIndex" is used to update pixels in a PBO
#if 0
        if(pboMode == 1)
        {
            // In single PBO mode, the index and nextIndex are set to 0
            qindex[side] = nextIndex = 0;
        }
        else if(pboMode == 2)
        {
            // In dual PBO mode, increment current index first then get the next index
            //qindex[side] = (qindex[side] + 1) % 2;
            //nextIndex = (qindex[side] + 1) % 2;

			nextIndex = (qindex[side] + pbo_size[side]) % 2;
        }
		else if(pboMode == 3)
        {
            // In dual PBO mode, increment current index first then get the next index
            //qindex[side] = (qindex[side] + 1) % 3;
            //nextIndex = (qindex[side] + 1) % 3;

			nextIndex = (qindex[side] + pbo_size[side]) % 3;
        }
		else if(pboMode == 4)
        {
            // In dual PBO mode, increment current index first then get the next index
            //qindex[side] = (qindex[side] + 1) % 3;
            //nextIndex = (qindex[side] + 1) % 4;

			nextIndex = (qindex[side] + pbo_size[side]) % 4;
        }
		else if(pboMode == 5)
        {
            // In dual PBO mode, increment current index first then get the next index
            //qindex[side] = (qindex[side] + 1) % 3;
            //nextIndex = (qindex[side] + 1) % 5;
			nextIndex = (qindex[side] + pbo_size[side]) % 5;
        }
#endif
		if(pboMode == 1)
        {
            // In single PBO mode, the index and nextIndex are set to 0
            qindex[side] = nextIndex = 0;
        }
        else if(pboMode > 1)
        {
			nextIndex = (qindex[side] + pbo_size[side]) % pboMode;
		}

#if 0
        //// start to copy from PBO to texture object ///////
        //t1.start();

		printf("nextindex: %d\n", nextIndex);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, pboIds[side][index]);

        // copy pixels from PBO to texture object
        // Use offset instead of ponter.

		static int lwidth[MAXSTREAMS] = {0};
		static int lheight[MAXSTREAMS] = {0};

		if(lwidth[side] != pbowidth[side] || lheight[side] != pboheight[side] )
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pbowidth[side], pboheight[side], 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
		}
		else
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pbowidth[side], pboheight[side], GL_RGBA, GL_UNSIGNED_BYTE, 0);

		lwidth[side] = pbowidth[side];
		lheight[side] = pboheight[side];
#endif

        // measure the time copying data from PBO to texture object
        //t1.stop();
        //copyTime = t1.getElapsedTimeInMilliSec();
        ///////////////////////////////////////////////////

		double startTime = getGlobalVideoTimer(side);//getPBOTimer(side);
        // start to modify pixel values ///////////////////
        //t1.start();

        // bind PBO to update pixel values
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][nextIndex]);

        // map the buffer object into client's memory
        // Note that glMapBufferARB() causes sync issue.
        // If GPU is working with this buffer, glMapBufferARB() will wait(stall)
        // for GPU to finish its job. To avoid waiting (stall), you can call
        // first glBufferDataARB() with NULL pointer before glMapBufferARB().
        // If you do that, the previous data in PBO will be discarded and
        // glMapBufferARB() returns a new allocated pointer immediately
        // even if GPU is still working with the previous data.
        glBufferData(GL_PIXEL_UNPACK_BUFFER_ARB, size[side], 0, GL_STREAM_DRAW_ARB);
		//glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, size[side], 0);
        GLbyte* ptr = (GLbyte*)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
		assert(ptr);
        if(ptr)
        {
            // update data directly on the mapped buffer
		    //updatePixels(side, ptr, size[side]);
			//if(qindex[side] % 2 == 0)
			//	memset(ptr, 0, size[side]);
			//else
			//	memset(ptr, 255, size[side]);
			
			//double ret = getNextVideoFrameNext(side, (char *)ptr, qindex[side]);
			//	memset(ptr, 0, size[side]);

			double pbopts = 0;
			double ret = 0;
			//do
			//{
				ret = getNextVideoFrameNext(side, (char *)ptr, nextIndex, pbopts);
			//} while(ret == -1);

				pboPTS[nextIndex] = pbopts;

				//printf("qindex[%d]: %d - pbopts: %f\n", side, nextIndex, pbopts);
			//printf("Pbo         uploaded...\n");

            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer

				if(ret == -1)
				{
					//printf("HERE+++\n");
					//nextIndex = qindex[side] % 2;
					return;
				}
				if(ret == -50)
				{
					return;
				}
        }

        // measure the time modifying the mapped buffer
        //t1.stop();
        //updateTime = t1.getElapsedTimeInMilliSec();
		double endTime = getGlobalVideoTimer(side);//getPBOTimer(side);
		//printf("mod mapped buffer(ms): %f\n", (endTime-startTime));
		//printf("\t\t\t\tUPLOADTime: %f\n", (endTime-startTime));
        ///////////////////////////////////////////////////

        // it is good idea to release PBOs with ID 0 after use.
        // Once bound with 0, all pixel operations behave normal ways.
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

//#ifdef USE_ODBASE
//		pboMutex.grab();
//#else
//		WaitForSingleObject(pboMutex, INFINITE);
//#endif
		pbo_size[side]++;
//#ifdef USE_ODBASE
//		pboMutex.release();
//#else
//		ReleaseMutex(pboMutex);
//#endif

		//if(pbo_size == PBO_QUEUE_SIZE)
		//	index;
		//if(++index == PBO_QUEUE_SIZE)
		//	index;

		GLenum err = glGetError();
		if(err != GL_NO_ERROR)
		{
			printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
			printf("UpdatePBO GL err.\n");
		}
    }
}
#if 0
void updatePBOThreads(void* arg)
{
	int side = (int)arg;
	//static int qindex = 0;
	//qindex[side] = 0;
    int nextIndex = 0;                  // pbo index used for next frame

	while(true)
	{
		if(pboMode > 0 && pbo_size[side] < pboMode)
		{
			// "index" is used to copy pixels from a PBO to a texture object
			// "nextIndex" is used to update pixels in a PBO
			if(pboMode == 1)
			{
				// In single PBO mode, the index and nextIndex are set to 0
				qindex[side] = nextIndex = 0;
			}
			else if(pboMode == 2)
			{
				// In dual PBO mode, increment current index first then get the next index
				//qindex[side] = (qindex[side] + 1) % 2;
				//nextIndex = (qindex[side] + 1) % 2;

				nextIndex = (qindex[side] + pbo_size[side]) % 2;
			}
			else if(pboMode == 3)
			{
				// In dual PBO mode, increment current index first then get the next index
				//qindex[side] = (qindex[side] + 1) % 3;
				//nextIndex = (qindex[side] + 1) % 3;

				nextIndex = (qindex[side] + pbo_size[side]) % 3;
			}

		else if(pboMode == 4)
        {
            // In dual PBO mode, increment current index first then get the next index
            //qindex[side] = (qindex[side] + 1) % 3;
            //nextIndex = (qindex[side] + 1) % 4;

			nextIndex = (qindex[side] + pbo_size[side]) % 4;
        }
		else if(pboMode == 5)
        {
            // In dual PBO mode, increment current index first then get the next index
            //qindex[side] = (qindex[side] + 1) % 3;
            //nextIndex = (qindex[side] + 1) % 5;
			nextIndex = (qindex[side] + pbo_size[side]) % 5;
        }
#if 0
			//// start to copy from PBO to texture object ///////
			//t1.start();

			printf("nextindex: %d\n", nextIndex);
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, pboIds[side][index]);

			// copy pixels from PBO to texture object
			// Use offset instead of ponter.

			static int lwidth[MAXSTREAMS] = {0};
			static int lheight[MAXSTREAMS] = {0};

			if(lwidth[side] != pbowidth[side] || lheight[side] != pboheight[side] )
			{
				glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pbowidth[side], pboheight[side], 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
			}
			else
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pbowidth[side], pboheight[side], GL_RGBA, GL_UNSIGNED_BYTE, 0);

			lwidth[side] = pbowidth[side];
			lheight[side] = pboheight[side];
#endif

			// measure the time copying data from PBO to texture object
			//t1.stop();
			//copyTime = t1.getElapsedTimeInMilliSec();
			///////////////////////////////////////////////////

			double startTime = getGlobalVideoTimer(side);//getPBOTimer(side);
			// start to modify pixel values ///////////////////
			//t1.start();

			// bind PBO to update pixel values
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][nextIndex]);

			// map the buffer object into client's memory
			// Note that glMapBufferARB() causes sync issue.
			// If GPU is working with this buffer, glMapBufferARB() will wait(stall)
			// for GPU to finish its job. To avoid waiting (stall), you can call
			// first glBufferDataARB() with NULL pointer before glMapBufferARB().
			// If you do that, the previous data in PBO will be discarded and
			// glMapBufferARB() returns a new allocated pointer immediately
			// even if GPU is still working with the previous data.
			glBufferData(GL_PIXEL_UNPACK_BUFFER, size[side], 0, GL_STREAM_DRAW);
			GLubyte* ptr = (GLubyte*)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
			if(ptr)
			{
				// update data directly on the mapped buffer
				//updatePixels(ptr, size[side]);
				//if(qindex[side] % 2 == 0)
				//	memset(ptr, 0, size[side]);
				//else
				//	memset(ptr, 255, size[side]);
				
				//double ret = getNextVideoFrameNext(side, (char *)ptr, qindex[side]);
				//	memset(ptr, 0, size[side]);
				double ret = 0;
				//do
				//{
					ret = getNextVideoFrameNext(side, (char *)ptr, nextIndex);
				//} while(ret == -1);
				//printf("qindex[%d]: %d\n", side, qindex[side]);

				glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer

				if(ret == -1)
				{
					//nextIndex = qindex[side] % 2;
					return;
				}
			}

			// measure the time modifying the mapped buffer
			//t1.stop();
			//updateTime = t1.getElapsedTimeInMilliSec();
			double endTime = getGlobalVideoTimer(side);//getPBOTimer(side);
			//printf("mod mapped buffer(ms): %f\n", (endTime-startTime));
			///////////////////////////////////////////////////

			// it is good idea to release PBOs with ID 0 after use.
			// Once bound with 0, all pixel operations behave normal ways.
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

#ifdef USE_ODBASE
			pboMutex.grab();
#else
			WaitForSingleObject(pboMutex, INFINITE);
#endif
			pbo_size[side]++;
#ifdef USE_ODBASE
			pboMutex.release();
#else
			ReleaseMutex(pboMutex);
#endif
			//if(pbo_size == PBO_QUEUE_SIZE)
			//	index;
			//if(++index == PBO_QUEUE_SIZE)
			//	index;
		}
	}
}
#endif
void drawPBO(int side)
{
	//printf("drawPBO...\n");
	//static int pboMode = 1;
	//static int pindex = 0;
	//pindex[side] = 0;
    int nextIndex = 0;                  // pbo index used for next frame

	//if(!(pbo_size[side] >= pboMode))
	//{
	//	printf("Pbo not yet uploaded...\n");
	//	return;
	//}

	
//#ifdef USE_ODBASE
//	pboMutex.grab();
//#else
//	WaitForSingleObject(pboMutex, INFINITE);
//#endif

	if(pbo_size[side] == 0)
	{
//#ifdef USE_ODBASE
//		pboMutex.release();
//#else
//		ReleaseMutex(pboMutex);
//#endif

		printf("Pbo not yet uploaded...\n");
		return;
	}
//#ifdef USE_ODBASE
//	pboMutex.release();
//#else
//	ReleaseMutex(pboMutex);
//#endif

    if(pboMode > 0)
    {
		 // "index" is used to copy pixels from a PBO to a texture object
         // "nextIndex" is used to update pixels in a PBO
#if 0
         if(pboMode == 1)
         {
             // In single PBO mode, the index and nextIndex are set to 0
             //pindex[side] = nextIndex = 0;

			 qindex[side] = nextIndex = 0;
         }
         else if(pboMode == 2)
         {
             // In dual PBO mode, increment current index first then get the next index
//             pindex[side] = (pindex[side] + 1) % 2;
//             nextIndex = (pindex[side] + 1) % 2;

			 qindex[side] = (qindex[side] + 1) % 2;
         }
		 else if(pboMode == 3)
         {
             // In dual PBO mode, increment current index first then get the next index
//             pindex[side] = (pindex[side] + 1) % 3;
//             nextIndex = (pindex[side] + 1) % 3;
				
			 qindex[side] = (qindex[side] + 1) % 3;
         }
		 else if(pboMode == 4)
         {
             // In dual PBO mode, increment current index first then get the next index
//             pindex[side] = (pindex[side] + 1) % 3;
//             nextIndex = (pindex[side] + 1) % 3;
				
			 qindex[side] = (qindex[side] + 1) % 4;
         }
		 else if(pboMode == 5)
         {
             // In dual PBO mode, increment current index first then get the next index
//             pindex[side] = (pindex[side] + 1) % 3;
//             nextIndex = (pindex[side] + 1) % 3;
				
			 //if(pboPTS[qindex[side]] < 9.5)
				qindex[side] = (qindex[side] + 1) % 5;
         }
#endif
		if(pboMode == 1)
        {
			 qindex[side] = nextIndex = 0;
		}
		else if(pboMode > 1)
		{
			qindex[side] = (qindex[side] + 1) % pboMode;
		}

		double startTime = getGlobalVideoTimer(side);//getPBOTimer(side);

		//glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][nextIndex]);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][qindex[side]]);
		//glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][pindex[side]]);


		// copy pixels from PBO to texture object
		// Use offset instead of ponter.

		static int lwidth[MAXSTREAMS] = {0};
		static int lheight[MAXSTREAMS] = {0};

		if(lwidth[side] != pbowidth[side] || lheight[side] != pboheight[side] )
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, pbowidth[side], pboheight[side], 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
			//glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 512, 512, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
		}
		else
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pbowidth[side], pboheight[side], GL_BGRA, GL_UNSIGNED_BYTE, 0);

		lwidth[side] = pbowidth[side];
		lheight[side] = pboheight[side];

//#ifdef USE_ODBASE
//		pboMutex.grab();
//#else
//		WaitForSingleObject(pboMutex, INFINITE);
//#endif
		pbo_size[side]--;	
//#ifdef USE_ODBASE
//		pboMutex.release();
//#else
//		ReleaseMutex(pboMutex);
//#endif

		 // measure the time copying data from PBO to texture object
		double endTime = getGlobalVideoTimer(side);//getPBOTimer(side);
		//printf("copy   pbo to tex(ms): %f\n", (endTime-startTime));
		//printf("index: %d\n", index);
		//printf("pindex[%d]: %d\n", side, pindex[side]);
		//printf("pindex[%d]: %d\n", side, qindex[side]);

		GLenum err = glGetError();
		if(err != GL_NO_ERROR)
		{
			printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
			printf("DrawPBO GL err.\n");
		}
	}
}