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
void* pboRingBuff[MAX_PBOS];

int mindex[MAXSTREAMS];
int dindex[MAXSTREAMS];
int	draw_size[MAXSTREAMS];

#ifdef USE_ODBASE
	ODBase::Lock drawPboMutex("drawPboMutex");
#else
	HANDLE drawPboMutex;
#endif

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
	startGlobalVideoTimer(side);

	//for(int i=0; i<MAX_PBOS; i++)
	//{
	//	qindex[i] = 0;
	//	pboPTS[i] = -1;
	//	mindex[i] = 0;
	//	dindex[i] = 0;
	//}

	for(int i=0; i<MAXSTREAMS; i++)
	{
		qindex[i] = 0;
		memset(pboPTS, 0, sizeof(double)*MAX_PBOS);
		mindex[i] = 0;
		dindex[i] = 0;
	}

	pbo_size[side] = 0;
	draw_size[side] = 0;
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

////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////// Synchronous PBOs //////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
bool pbosFull(int side)
{
	return (pbo_size[side] < pboMode);
}

bool pbosEmpty(int side)
{
	return pbo_size[side] == 0;
}

int pbosSize(int side)
{
	return pbo_size[side];
}
//bool endReached = false;
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
		if(pboMode == 1)
        {
            // In single PBO mode, the index and nextIndex are set to 0
            qindex[side] = nextIndex = 0;
        }
        else if(pboMode > 1)
        {
			nextIndex = (qindex[side] + pbo_size[side]) % pboMode;
		}
		//printf("Pbo uploaded - size = %d.\n", pbo_size[side]);

		double startTime = getGlobalVideoTimer(side);
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

		double ret = 0;
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

			double pbopts = -1.0;
			
			//do
			//{
				ret = getNextVideoFrameNext(side, (char *)ptr, nextIndex, pbopts);
			//} while(ret == -1);

				if(ret > 0)
					pboPTS[nextIndex] = pbopts;

				//printf("ret: %f - pbo_size[%d]: %d - qindex[%d]: %d - pbopts: %f\n", ret, side, pbo_size[side], side, nextIndex, pbopts);
			//printf("Pbo         uploaded...\n");

            glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer
        }

		double endTime = getGlobalVideoTimer(side);
		//printf("mod mapped buffer(ms): %f\n", (endTime-startTime));
		//printf("\t\t\t\tUPLOADTime: %f\n", (endTime-startTime));
        ///////////////////////////////////////////////////

        // it is good idea to release PBOs with ID 0 after use.
        // Once bound with 0, all pixel operations behave normal ways.
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

		if(ret >= 0)
		{
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

		}
		if(enableDebugOutput)
		{
			GLenum err = glGetError();
			if(err != GL_NO_ERROR)
			{
				printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
				printf("UpdatePBO GL err.\n");
			}
		}

		if(ret == -1)
		{
			//printf("|||Pbo pictq_size = 0.\n");
			return;
		}
		if(ret == -50)
		{
			//printf("|||Pbo queue has reached the end.\n");
			//endReached = false;
			pboPTS[nextIndex] = -50;
			return;
		}
    }
}

void drawPBO(int side)
//void drawPBO(int side, int size)
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


	//printf("------- pbo_size[%d]: %d - qindex[%d]: %d\n", side, pbo_size[side], side, qindex[side]);
	//if(pbo_size[side] == 0 && !endReached)// && size == 0)
	if(pbo_size[side] == 0 && pboPTS[qindex[side]] != -50)
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
		if(pboMode == 1)
        {
			 qindex[side] = nextIndex = 0;
		}
		//else if(pboMode > 1)// && !(endReached && pbo_size[side] == 1) )
		else if(pboMode > 1)// && pboPTS[qindex[side]] < 9.95) 
		{
			//if(endReached)
			if(pboPTS[qindex[side]] == -50)
			{
				printf("Reached end - Stop drawing pbos\n");
				//qindex[side] = (qindex[side] == 0) ? pboMode - 1 : qindex[side] - 1;
				nextIndex = (qindex[side] == 0) ? pboMode - 1 : qindex[side] - 1;
				//return;
			}
			else
			{
				qindex[side] = (qindex[side] + 1) % pboMode;
				nextIndex = qindex[side];
			}
		}

		double startTime = getGlobalVideoTimer(side);

		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][nextIndex]);
		//glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][qindex[side]]);
		//glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][pindex[side]]);


		// copy pixels from PBO to texture object
		// Use offset instead of pointer.
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

		//if(endReached)
		//{
			//qindex[side] = (qindex[side] + 1) % pboMode;
			//return;
		//}
		if(pboPTS[qindex[side]] != -50)
		{
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
		}

		 // measure the time copying data from PBO to texture object
		double endTime = getGlobalVideoTimer(side);
		//printf("copy   pbo to tex(ms): %f\n", (endTime-startTime));
		//printf("index: %d\n", index);
		//printf("pindex[%d]: %d\n", side, pindex[side]);
		//printf("pindex[%d]: %d\n", side, qindex[side]);

		GLenum err = glGetError();
		if(enableDebugOutput)
		{
			if(err != GL_NO_ERROR)
			{
				printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
				printf("DrawPBO GL err.\n");
			}
		}
	}
}
									
////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////// Asynchronous PBOs //////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
void mapPBOsRingBuffer(int side)
{
	//for(int k=0; k<pboMode; k++)
	{

    int nextIndex = 0;                  // pbo index used for next frame

#ifdef USE_ODBASE
		pboMutex.grab();
#else
		WaitForSingleObject(pboMutex, INFINITE);
#endif

		int bSize = pbo_size[side];

#ifdef USE_ODBASE
		pboMutex.release();
#else
		ReleaseMutex(pboMutex);
#endif

	if(!(bSize < pboMode))
	{
		//printf("Pbo already created - size = %d.\n", bSize);
		return;
	}

    if(pboMode > 0)
    {
		// "index" is used to copy pixels from a PBO to a texture object
        // "nextIndex" is used to update pixels in a PBO
		if(pboMode == 1)
        {
            // In single PBO mode, the index and nextIndex are set to 0
            nextIndex = 0;
        }
        else if(pboMode > 1)
        {
			//nextIndex = (qindex[side] + bSize) % pboMode;
			////qindex[side] = (qindex[side] + 1) % pboMode;		
			nextIndex = (mindex[side] + 1) % pboMode;
		}
		//printf("Pbo uploaded - size = %d.\n", pbo_size[side]);

        // measure the time copying data from PBO to texture object
        //t1.stop();
        //copyTime = t1.getElapsedTimeInMilliSec();
        ///////////////////////////////////////////////////

		double startTime = getGlobalVideoTimer(side);
        // start to modify pixel values ///////////////////
        //t1.start();

        // bind PBO to update pixel values
        //glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][qindex[side]]);
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

		//updatePixels(side, ptr, size[side]);

		//glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer

        // measure the time modifying the mapped buffer
        //t1.stop();
        //updateTime = t1.getElapsedTimeInMilliSec();
		double endTime = getGlobalVideoTimer(side);
		//printf("mod mapped buffer(ms): %f\n", (endTime-startTime));
		//printf("\t\t\t\tUPLOADTime: %f\n", (endTime-startTime));
        ///////////////////////////////////////////////////

        //// it is good idea to release PBOs with ID 0 after use.
		//// Once bound with 0, all pixel operations behave normal ways.
        //glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

		if(ptr)
		{
			//glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer

			pboRingBuff[nextIndex] = ptr;

			//pboPTS[nextIndex] = -1;
			
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

			mindex[side] = (mindex[side] + 1) % pboMode;

		}

#if 0
	#ifdef USE_ODBASE
			drawPboMutex.grab();
	#else
			WaitForSingleObject(drawPboMutex, INFINITE);
	#endif
	
			int dbSize = draw_size[side];
							
	#ifdef USE_ODBASE
			drawPboMutex.release();
	#else
			ReleaseMutex(drawPboMutex);
	#endif
			if(dbSize > 0 && dbSize <= bSize)
			{
				//printf("Unmapping buffer - dbSize: %d - pbosize: %d.\n", dbSize, bSize);
				glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer
			}
#endif
		// it is good idea to release PBOs with ID 0 after use.
		// Once bound with 0, all pixel operations behave normal ways.
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

		if(enableDebugOutput)
		{
			GLenum err = glGetError();
			if(err != GL_NO_ERROR)
			{
				printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
				printf("UpdatePBO GL err.\n");
			}
		}
    }
	}
}

int updatePBOsRingBuffer(int side)
{
	//for(int k=0; k<pboMode; k++)
	{
    int nextIndex = 0;                  // pbo index used for next frame

#ifdef USE_ODBASE
		pboMutex.grab();
#else
		WaitForSingleObject(pboMutex, INFINITE);
#endif

		int pbSize = pbo_size[side];

#ifdef USE_ODBASE
		pboMutex.release();
#else
		ReleaseMutex(pboMutex);
#endif

#ifdef USE_ODBASE
		drawPboMutex.grab();
#else
		WaitForSingleObject(drawPboMutex, INFINITE);
#endif

		int dbSize = draw_size[side];
						
#ifdef USE_ODBASE
		drawPboMutex.release();
#else
		ReleaseMutex(drawPboMutex);
#endif

	if((dbSize >= pbSize))// < pboMode))
	{
		//printf("\t\t\t\t\t\t\t\t\t\t\tDraw queue >= pbo queue - drawSize = %d - pboSize = %d.\n", dbSize, pbSize);
		return 1;
	}

    if(pboMode > 0)
    {
		// "index" is used to copy pixels from a PBO to a texture object
        // "nextIndex" is used to update pixels in a PBO
		if(pboMode == 1)
        {
            // In single PBO mode, the index and nextIndex are set to 0
            nextIndex = 0;
        }
        else if(pboMode > 1)
        {
			////qindex[side] = (qindex[side] + 1) % pboMode;
			//nextIndex = (qindex[side] + dbSize) % pboMode;

			nextIndex = (dindex[side] + 1) % pboMode;
		}
		//printf("Pbo uploaded - size = %d.\n", pbo_size[side]);

        // measure the time copying data from PBO to texture object
        //t1.stop();
        //copyTime = t1.getElapsedTimeInMilliSec();
        ///////////////////////////////////////////////////

		double startTime = getGlobalVideoTimer(side);
        // start to modify pixel values ///////////////////
        //t1.start();

        //assert(data);
		assert(pboRingBuff[nextIndex]);
		//pboRingBuff[qindex[side]] = (char *)data;

		

		double pbopts = -1.0;
		int ret = getNextVideoFrameNext(side, (char *)pboRingBuff[nextIndex], nextIndex, pbopts);
		//static char *rawData1 = new char[1920*1080*4];
		//int ret = getNextVideoFrameNext(side, (char *)rawData1, nextIndex, pbopts);

		//printf("===drawSize = %d - pboSize =   %d - nextIndex: %d - pts: %f - pboPTS[nextIndex]: %f - ret: %d\n", dbSize, pbSize, nextIndex, pbopts, pboPTS[nextIndex], ret);
		if(ret > 0)
		{
			pboPTS[nextIndex] = pbopts;

				#ifdef USE_ODBASE
						drawPboMutex.grab();
				#else
						WaitForSingleObject(drawPboMutex, INFINITE);
				#endif

						draw_size[side]++;
							
				#ifdef USE_ODBASE
						drawPboMutex.release();
				#else
						ReleaseMutex(drawPboMutex);
				#endif

			dindex[side] = (dindex[side] + 1) % pboMode;


		}
		else if(ret == -1)
		{
			//printf("|||Pbo pictq_size = 0.\n");
		}
		else if(ret == -50)
		{
			

			printf("|||Pbo queue has reached the end.\n");
			printf("===drawSize = %d - pboSize =   %d - nextIndex: %d - pts: %f - pboPTS[nextIndex]: %f\n", dbSize, pbSize, nextIndex, pbopts, pboPTS[nextIndex]);
			
			pboPTS[nextIndex] = -50;

				#ifdef USE_ODBASE
						drawPboMutex.grab();
				#else
						WaitForSingleObject(drawPboMutex, INFINITE);
				#endif

						draw_size[side]++;
							
				#ifdef USE_ODBASE
						drawPboMutex.release();
				#else
						ReleaseMutex(drawPboMutex);
				#endif

			dindex[side] = (dindex[side] + 1) % pboMode;
			return -50;
		}

        // measure the time modifying the mapped buffer
        //t1.stop();
        //updateTime = t1.getElapsedTimeInMilliSec();
		double endTime = getGlobalVideoTimer(side);
		//printf("mod mapped buffer(ms): %f\n", (endTime-startTime));
		//printf("\t\t\t\tUPLOADTime: %f\n", (endTime-startTime));
        ///////////////////////////////////////////////////
    }
	}

	return 0;
}

void drawPBOsRingBuffer(int side)
{
    int nextIndex = 0;                  // pbo index used for next frame

#ifdef USE_ODBASE
		drawPboMutex.grab();
#else
		WaitForSingleObject(drawPboMutex, INFINITE);
#endif

		int dSize = draw_size[side];

#ifdef USE_ODBASE
		drawPboMutex.release();
#else
		ReleaseMutex(drawPboMutex);
#endif

	//if(!(dSize < pboMode))
	if(dSize <= 0)
	{
		//printf("\t\t\t\t\t\tDraw queue empty - size = %d - qindex[side]: %d\n", dSize, qindex[side]);
		return;
	}

    if(pboMode > 0)
    {
		// "index" is used to copy pixels from a PBO to a texture object
        // "nextIndex" is used to update pixels in a PBO
		if(pboMode == 1)
        {
            // In single PBO mode, the index and nextIndex are set to 0
            qindex[side] = nextIndex = 0;
        }
        else if(pboMode > 1)
        {
			//qindex[side] = (qindex[side] + 1) % pboMode;

			//if(endReached)
			//if(pboPTS[(qindex[side] + 1) % pboMode] == -50)
			//if(pboPTS[(qindex[side]) % pboMode] == -50)
			//{
			//	printf("```Reached end - Stop drawing pbos\n");
			//	//qindex[side] = (qindex[side] == 0) ? pboMode - 1 : qindex[side] - 1;
			//	//nextIndex = (qindex[side] == 0) ? pboMode - 1 : qindex[side] - 1;
			//	return;
			//}
			//else
			//if(pboPTS[(qindex[side]) % pboMode] != -50)
			{
				qindex[side] = (qindex[side] + 1) % pboMode;
				nextIndex = qindex[side];
			}
			
			if(pboPTS[nextIndex] == -50)
			{
				printf("END!!!!!!! dSize[%d]: %d - qindex[%d]: %d - pbopts: %f\n", side, dSize, side, nextIndex, pboPTS[nextIndex]);

		#ifdef USE_ODBASE
				drawPboMutex.grab();
		#else
				WaitForSingleObject(drawPboMutex, INFINITE);
		#endif

				draw_size[side] = 0;
					
		#ifdef USE_ODBASE
				drawPboMutex.release();
		#else
				ReleaseMutex(drawPboMutex);
		#endif

				return;
			}

		}
		//printf("~~~dSize[%d]:  %d - qindex[%d]:  %d - pbopts:    %f\n", side, dSize, side, nextIndex, pboPTS[nextIndex]);

		//printf("Pbo uploaded - size = %d.\n", pbo_size[side]);

        // measure the time copying data from PBO to texture object
        //t1.stop();
        //copyTime = t1.getElapsedTimeInMilliSec();
        ///////////////////////////////////////////////////

		double startTime = getGlobalVideoTimer(side);
        // start to modify pixel values ///////////////////
        //t1.start();

		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][nextIndex]);
		glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer

		//glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][qindex[side]]);

		//glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][pindex[side]]);

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

		//if(endReached)
		//{
			//qindex[side] = (qindex[side] + 1) % pboMode;
			//return;
		//}
		if(pboPTS[nextIndex] != -50)
		{

			pboPTS[nextIndex] = -1;

				#ifdef USE_ODBASE
						pboMutex.grab();
				#else
						WaitForSingleObject(pboMutex, INFINITE);
				#endif
						pbo_size[side]--;

				#ifdef USE_ODBASE
						pboMutex.release();
				#else
						ReleaseMutex(pboMutex);
				#endif

				#ifdef USE_ODBASE
						drawPboMutex.grab();
				#else
						WaitForSingleObject(drawPboMutex, INFINITE);
				#endif

						draw_size[side]--;
							
				#ifdef USE_ODBASE
						drawPboMutex.release();
				#else
						ReleaseMutex(drawPboMutex);
				#endif
		}

        // measure the time modifying the mapped buffer
        //t1.stop();
        //updateTime = t1.getElapsedTimeInMilliSec();
		double endTime = getGlobalVideoTimer(side);
		//printf("mod mapped buffer(ms): %f\n", (endTime-startTime));
		//printf("\t\t\t\tUPLOADTime: %f\n", (endTime-startTime));
        ///////////////////////////////////////////////////
		if(enableDebugOutput)
		{
			GLenum err = glGetError();
			if(err != GL_NO_ERROR)
			{
				printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
				printf("DrawPBO GL err.\n");
			}
		}
    }
}

int getCurrPbo(int side)
{
	return ((qindex[side] + 1) % pboMode);
}