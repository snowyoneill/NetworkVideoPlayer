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

#define MAX_PBOS 3
int pboMode = 3;

//#define LOCLESS_QUEUES
//#undef USE_CRITICAL_SECTION
//#include "SimpleQueue.h"
//#ifdef LOCLESS_QUEUES
//	SimpleQueue<int> pboQueue;
//	SimpleQueue<int> drawQueue;
//	SimpleQueue<int> ipboQueue;
//#endif

#ifdef  USE_CRITICAL_SECTION
	CRITICAL_SECTION pboMutex[MAXSTREAMS];
	CRITICAL_SECTION drawPboMutex[MAXSTREAMS];
	CRITICAL_SECTION iPboMutex[MAXSTREAMS];
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	ODBase::Lock pboMutex[MAXSTREAMS];
	ODBase::Lock drawPboMutex[MAXSTREAMS];
	ODBase::Lock iPboMutex[MAXSTREAMS];
#else
	HANDLE pboMutex[MAXSTREAMS];
	HANDLE drawPboMutex[MAXSTREAMS];
	HANDLE iPboMutex[MAXSTREAMS];
#endif
#endif

GLuint	pboIds[MAXSTREAMS][MAX_PBOS]; // IDs of PBO
int		pbowidth[MAXSTREAMS];
int		pboheight[MAXSTREAMS];
bool	pboInit[MAXSTREAMS];
int		size[MAXSTREAMS];
int		pbo_size[MAXSTREAMS];
int		qindex[MAXSTREAMS];
int		pindex[MAXSTREAMS];

double pboPTS[MAX_PBOS];
void* pboRingBuff[MAX_PBOS];

int mindex[MAXSTREAMS];
int dindex[MAXSTREAMS];
int	draw_size[MAXSTREAMS];

#ifdef USE_MUTEX
#ifdef USE_ODBASE
	ODBase::Semaphore pictqSemaphore[MAXSTREAMS];
	ODBase::Semaphore pictPacketqSemaphore[MAXSTREAMS];
#endif
#else
	HANDLE pboSemaphore[MAXSTREAMS];
	HANDLE waitSemaphore[MAXSTREAMS];
#endif


//HANDLE pboSemaphore = CreateSemaphore( NULL, 0 , 1, NULL);
//HANDLE waitSemaphore = CreateSemaphore( NULL, 0 , 1, NULL);

//HANDLE pboSemaphore = CreateEvent(NULL, false, false, NULL);
//HANDLE pboSemaphore = CreateEvent(NULL, true, false, NULL);
//HANDLE waitSemaphore = CreateEvent(NULL, true, false, NULL);

bool bufferUnmapped[MAX_PBOS];
int uindex[MAXSTREAMS];
int	um_size[MAXSTREAMS];
bool quit[MAXSTREAMS];

int currFrameIdx[MAXSTREAMS];
////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////// PBO //////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers;

void initPBOs()
{
	for(int i=0; i<MAXSTREAMS; i++)
	{
		#ifdef  USE_CRITICAL_SECTION
			/* Initialize the critical section before entering multi-threaded context. */
			InitializeCriticalSection(&pboMutex[i]);
			InitializeCriticalSection(&drawPboMutex[i]);
			InitializeCriticalSection(&iPboMutex[i]);
		#endif

		#ifdef USE_MUTEX
		#ifndef USE_ODBASE
			pboMutex[i] = CreateMutex(NULL, false, NULL);
			drawPboMutex[i] = CreateMutex(NULL, false, NULL);
			iPboMutex[i] = CreateMutex(NULL, false, NULL);
		#endif
		#endif

		#ifndef USE_MUTEX
		#ifndef USE_ODBASE
			pboSemaphore[i] = CreateSemaphore( NULL, 0 , 1, NULL);
			waitSemaphore[i] = CreateSemaphore( NULL, 0 , 1, NULL);
		#endif
		#endif
		clearPBOs(i);
	}
}
void deletePBOs(int side)
{
	if(!quit[side])
	{
		if (enablePBOs)
		{
			glDeleteBuffers(MAX_PBOS, pboIds[side]);
			printf("Side: %d - Pbos deleted\n", side);
		}
		quit[side] = true;
		clearPBOs(side);
	}
}
void closePBOs()
{
	for(int i=0; i<MAXSTREAMS; i++)
	{
	#ifdef  USE_CRITICAL_SECTION
		/* Initialize the critical section before entering multi-threaded context. */
		DeleteCriticalSection(&pboMutex[i]);
		DeleteCriticalSection(&drawPboMutex[i]);
		DeleteCriticalSection(&iPboMutex[i]);
	#endif
	#ifndef USE_MUTEX
	#ifndef USE_ODBASE
		CloseHandle(pboSemaphore[i]);
		CloseHandle(waitSemaphore[i]);
	#endif
	#endif
	}
}

void intiPBOsSide(int w, int h, int side)
{
	clearPBOs(side);
	quit[side] = false;

	//#ifdef USE_GLEW
	//	if (glewIsSupported("GL_ARB_pixel_buffer_object")) 
	//#else
	//	if (true) 
	//#endif
	if (enablePBOs)
	{
		//glDeleteBuffersARB(2, pboIds[side]);
		//glDeleteBuffers(MAX_PBOS, pboIds[side]);

		pbowidth[side] = w;
		pboheight[side] = h;

		pboInit[side] = true;
		size[side] = (sizeof(unsigned char)) * pbowidth[side] * pboheight[side] * 4;
		if(pboInit[side])
		{
			// create 2 pixel buffer objects, you need to delete them when program exits.
			// glBufferDataARB with NULL pointer reserves only memory space.
			glGenBuffers(MAX_PBOS, pboIds[side]);

			
			for(int i=0; i<MAX_PBOS; i++)
			{
				//glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][i]);
				//////glBufferDataARB(GL_PIXEL_UNPACK_BUFFER, size[side], 0, GL_STREAM_DRAW);
				//glBufferData(GL_PIXEL_UNPACK_BUFFER, size[side], 0, GL_DYNAMIC_DRAW);
				//// Set pbos to black
				//{
				//		GLbyte* ptr = (GLbyte*)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
				//		assert(ptr);
				//		if(ptr)
				//		{
				//			memset(ptr, 0, size[side]);
				//			glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer
				//		}
				//		
				//}
				mapPBOsRingBuffer(side);
			}
			//glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}
		
	}
	//if(enableDebugOutput)
	{
		GLenum err = glGetError();
		if(err != GL_NO_ERROR)
		{
			printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
			printf("INIT  PBO GL err.\n");
		}
	}
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

        glBufferData(GL_PIXEL_UNPACK_BUFFER, size[side], 0, GL_STREAM_DRAW);
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
void unmapPBOsRingBuffer(int side)
{
	int nextIndex = 0;

	#ifdef  USE_CRITICAL_SECTION
		/* Enter the critical section -- other threads are locked out */
		EnterCriticalSection(&drawPboMutex[side]);
	#endif

	#ifdef USE_MUTEX
	#ifdef USE_ODBASE
			drawPboMutex[side].grab();
	#else
			WaitForSingleObject(drawPboMutex[side], INFINITE);
	#endif
	#endif
		
			int dbSize = draw_size[side];
			int umSize = um_size[side];

	#ifdef  USE_CRITICAL_SECTION
		/* Leave the critical section -- other threads can now EnterCriticalSection() */
		LeaveCriticalSection(&drawPboMutex[side]);
	#endif

	#ifdef USE_MUTEX				
	#ifdef USE_ODBASE
			drawPboMutex[side].release();
	#else
			ReleaseMutex(drawPboMutex[side]);
	#endif
	#endif

	if(umSize >= dbSize)
	//if(umSize <= 0)
	{
		//printf("Pbo already unmapped - size = %d.\n", umSize);
		return;
	}

	int totalBuffersToMap = dbSize-umSize;
	//int totalBuffersToMap = pboMode-umSize;
	//printf("pboMode: %d - dbSize: %d - umSize: %d - totalBuffersToMap -  = %d.\n", pboMode, dbSize, umSize, totalBuffersToMap);
	for(int k=0; k<totalBuffersToMap; k++)
	{
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
			nextIndex = (uindex[side] + 1) % pboMode;
		}

		//if(bufferUnmapped[nextIndex])
		//{
		//	uindex[side] = (uindex[side] + 1) % pboMode;
		//	return;
		//}

		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][ nextIndex ]);
		glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

		//printf("...unmSize =  %d - nextIndex: %d - dbSize: %d - pboPTS[nextIndex]: %f - bufferUnmapped: %d\n", umSize, nextIndex, dbSize, pboPTS[nextIndex], bufferUnmapped[nextIndex]);

#if 0
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].grab();
#else
		WaitForSingleObject(pboMutex[side], INFINITE);
#endif
#endif
			if(pbo_size[side] > 0)
				pbo_size[side]--;

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].release();
#else
		ReleaseMutex(pboMutex[side]);
#endif
#endif
#endif

#if 0
		#ifdef  USE_CRITICAL_SECTION
			/* Enter the critical section -- other threads are locked out */
			EnterCriticalSection(&drawPboMutex);
		#endif

		#ifdef USE_MUTEX
		#ifdef USE_ODBASE
				drawPboMutex.grab();
		#else
				WaitForSingleObject(drawPboMutex, INFINITE);
		#endif
		#endif

			um_size[side]--;

		#ifdef  USE_CRITICAL_SECTION
			/* Leave the critical section -- other threads can now EnterCriticalSection() */
			LeaveCriticalSection(&drawPboMutex);
		#endif

		#ifdef USE_MUTEX				
		#ifdef USE_ODBASE
				drawPboMutex.release();
		#else
				ReleaseMutex(drawPboMutex);
		#endif
		#endif
#endif

		#ifdef  USE_CRITICAL_SECTION
			/* Enter the critical section -- other threads are locked out */
			EnterCriticalSection(&drawPboMutex[side]);
		#endif

		#ifdef USE_MUTEX
		#ifdef USE_ODBASE
				drawPboMutex[side].grab();
		#else
				WaitForSingleObject(drawPboMutex[side], INFINITE);
		#endif
		#endif

			um_size[side]++;

		#ifdef  USE_CRITICAL_SECTION
			/* Leave the critical section -- other threads can now EnterCriticalSection() */
			LeaveCriticalSection(&drawPboMutex[side]);
		#endif

		#ifdef USE_MUTEX				
		#ifdef USE_ODBASE
				drawPboMutex[side].release();
		#else
				ReleaseMutex(drawPboMutex[side]);
		#endif
		#endif

		uindex[side] = (uindex[side] + 1) % pboMode;

		bufferUnmapped[nextIndex] = true;
	}
	}

	if(enableDebugOutput)
	{
		GLenum err = glGetError();
		if(err != GL_NO_ERROR)
		{
			printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
			printf("Unmap PBO GL err.\n");
		}
	}
}

void mapPBOsRingBuffer(int side)
{
    int nextIndex = 0;                  // pbo index used for next frame

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].grab();
#else
		WaitForSingleObject(pboMutex[side], INFINITE);
#endif
#endif
		int bSize = pbo_size[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].release();
#else
		ReleaseMutex(pboMutex[side]);
#endif
#endif

#if 0
	#ifdef  USE_CRITICAL_SECTION
		/* Enter the critical section -- other threads are locked out */
		EnterCriticalSection(&drawPboMutex[side]);
	#endif

	#ifdef USE_MUTEX
	#ifdef USE_ODBASE
			drawPboMutex[side].grab();
	#else
			WaitForSingleObject(drawPboMutex[side], INFINITE);
	#endif
	#endif

	
			int dbSize = draw_size[side];
							

	#ifdef  USE_CRITICAL_SECTION
		/* Leave the critical section -- other threads can now EnterCriticalSection() */
		LeaveCriticalSection(&drawPboMutex[side]);
	#endif

	#ifdef USE_MUTEX				
	#ifdef USE_ODBASE
			drawPboMutex[side].release();
	#else
			ReleaseMutex(drawPboMutex[side]);
	#endif
	#endif
			if(dbSize > 0 && dbSize <= bSize)
			{
				int nextQIndex = getCurrPbo(side);

			printf("Unmapping buffer - dbSize: %d - pbosize: %d - nextQIndex: %d.\n", dbSize, bSize, nextQIndex);
				for(int m=0; m<dbSize; m++)
				{

					//int offset = mindex[side]-dbSize;
					//int sIdx = offset > 0 ? offset : pboMode + offset;

					int sIdx = nextQIndex;
					if(bufferUnmapped[ sIdx + m ])
						continue;

					glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][ sIdx + m ]);
					glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer
					glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
					bufferUnmapped[ sIdx + m ] = true;
				}
			}
#endif


	if(!(bSize < pboMode))
	{
		//printf("Pbo already created - size = %d.\n", bSize);
		return;
	}
	
	int totalBuffersToMap = pboMode-bSize;
	//printf("pboMode: %d - bSize: %d - totalBuffersToMap -  = %d.\n", pboMode, bSize, totalBuffersToMap);
	for(int k=0; k<totalBuffersToMap; k++)
	{
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
		//printf("___pboSize =  %d - nextIndex: %d - pboPTS[nextIndex]: %f\n", bSize, nextIndex, pboPTS[nextIndex]);

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

        glBufferData(GL_PIXEL_UNPACK_BUFFER, size[side], 0, GL_DYNAMIC_DRAW);
		//glBufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, size[side], 0);
        GLbyte* ptr = (GLbyte*)glMapBuffer(GL_PIXEL_UNPACK_BUFFER, GL_WRITE_ONLY);
		if(enableDebugOutput)
		{
			assert(ptr);
		}
		//updatePixels(side, ptr, size[side]);

        // measure the time modifying the mapped buffer
        //t1.stop();
        //updateTime = t1.getElapsedTimeInMilliSec();
		double endTime = getGlobalVideoTimer(side);
		//printf("mod mapped buffer(ms): %f\n", (endTime-startTime));
		//printf("\t\t\t\tMAPime: %f\n", (endTime-startTime));
        ///////////////////////////////////////////////////

		if(ptr)
		{
			pboRingBuff[nextIndex] = ptr;

			//pboPTS[nextIndex] = -1;
			
			#ifdef  USE_CRITICAL_SECTION
				/* Enter the critical section -- other threads are locked out */
				EnterCriticalSection(&pboMutex[side]);
			#endif
			
			#ifdef USE_MUTEX
			#ifdef USE_ODBASE
					pboMutex[side].grab();
			#else
					WaitForSingleObject(pboMutex[side], INFINITE);
			#endif
			#endif
					pbo_size[side]++;

			#ifdef  USE_CRITICAL_SECTION
				/* Leave the critical section -- other threads can now EnterCriticalSection() */
				LeaveCriticalSection(&pboMutex[side]);
			#endif

			#ifdef USE_MUTEX
			#ifdef USE_ODBASE
					pboMutex[side].release();
			#else
					ReleaseMutex(pboMutex[side]);
			#endif
			#endif

//			glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer

			mindex[side] = (mindex[side] + 1) % pboMode;

#if 0
			if(!bufferUnmapped[nextIndex])
			{

		#ifdef  USE_CRITICAL_SECTION
			/* Enter the critical section -- other threads are locked out */
			EnterCriticalSection(&drawPboMutex[side]);
		#endif

		#ifdef USE_MUTEX
		#ifdef USE_ODBASE
				drawPboMutex[side].grab();
		#else
				WaitForSingleObject(drawPboMutex[side], INFINITE);
		#endif
		#endif

		if(um_size[side] > 0)
			um_size[side]--;

		#ifdef  USE_CRITICAL_SECTION
			/* Leave the critical section -- other threads can now EnterCriticalSection() */
			LeaveCriticalSection(&drawPboMutex[side]);
		#endif

		#ifdef USE_MUTEX				
		#ifdef USE_ODBASE
				drawPboMutex[side].release();
		#else
				ReleaseMutex(drawPboMutex[side]);
		#endif
		#endif

			}
#endif
			bufferUnmapped[nextIndex] = false;
		}


		//// it is good idea to release PBOs with ID 0 after use.
		//// Once bound with 0, all pixel operations behave normal ways.
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

#if 1
		#ifdef USE_ODBASE
			pboSemaphore[side].signal();
		#else
			ReleaseSemaphore(pboSemaphore[side], 1, NULL );
		#endif
		//#ifdef USE_ODBASE
		//	waitSemaphore[side].signal();
		//#else
		//	ReleaseSemaphore(waitSemaphore[side], 1, NULL );
		//#endif
#endif
#if 0
		if(!SetEvent(pboSemaphore))
		{
			printf("SetEvent failed (%d)\n", GetLastError);
			return;
		}
		//printf("MAPEvent done\n");
		//if(!SetEvent(waitSemaphore))
		//{
		//	printf("SetEvent failed (%d)\n", GetLastError);
		//	return;
		//}
		//printf("WATEvent done\n");
#endif
    }
	}

	if(enableDebugOutput)
	{
		GLenum err = glGetError();
		if(err != GL_NO_ERROR)
		{
			printf("OpenGL Error: %s (0x%x), @\n", glGetString(err), err);
			printf("MAP   PBO GL err.\n");
		}
	}
}

int updatePBOsRingBuffer(int side)
{
    int nextIndex = 0;                  // pbo index used for next frame

	double initialTime = getGlobalVideoTimer(side);

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].grab();
#else
		WaitForSingleObject(pboMutex[side], INFINITE);
#endif
#endif

		int pbSize = pbo_size[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].release();
#else
		ReleaseMutex(pboMutex[side]);
#endif
#endif

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif

		int dbSize = draw_size[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
#endif

	//if((dbSize >= pbSize))// < pboMode))
	//{
	//	//printf("Draw queue >= pbo queue - drawSize = %d - pboSize = %d.\n", dbSize, pbSize);
	//	return 1;
	//}

	double waitTime = getGlobalVideoTimer(side);
	while(dbSize >= pbSize && !quit[side])
	{
		//printf("dbSize: %d >= pbSize: %d - WAIT\n", dbSize, pbSize);
#ifdef USE_ODBASE
		pboSemaphore[side].wait();
#else
		WaitForSingleObject(pboSemaphore[side],INFINITE);
#endif
		//ResetEvent(pboSemaphore);
		//Sleep(100);

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].grab();
#else
		WaitForSingleObject(pboMutex[side], INFINITE);
#endif
#endif

		pbSize = pbo_size[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].release();
#else
		ReleaseMutex(pboMutex[side]);
#endif
#endif

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif

		dbSize = draw_size[side];
						
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
#endif
	}
	double endwaitTime = getGlobalVideoTimer(side);
	//printf("wat mapped buffer(ms): %f\n", (endwaitTime-waitTime));

	//for(int k=0; k<pbSize-dbSize; k++)
	{
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

		if(enableDebugOutput)
		{
			//assert(data);
			assert(pboRingBuff[nextIndex]);
		}
		//pboRingBuff[qindex[side]] = (char *)data;

		double pbopts = -1.0;
		int ret = getNextVideoFrameNext(side, (char *)pboRingBuff[nextIndex], nextIndex, pbopts);
		//static char *rawData1 = new char[1920*1080*4];
		//int ret = getNextVideoFrameNext(side, (char *)rawData1, nextIndex, pbopts);

		//printf("^^^drawSize = %d - pboSize =   %d - nextIndex: %d - pts: %f - pboPTS[nextIndex]: %f - ret: %d\n", dbSize, pbSize, nextIndex, pbopts, pboPTS[nextIndex], ret);
		if(ret > 0)
		{
			pboPTS[nextIndex] = pbopts;

			#ifdef  USE_CRITICAL_SECTION
				/* Enter the critical section -- other threads are locked out */
				EnterCriticalSection(&drawPboMutex[side]);
			#endif

			#ifdef USE_MUTEX
			#ifdef USE_ODBASE
					drawPboMutex[side].grab();
			#else
					WaitForSingleObject(drawPboMutex[side], INFINITE);
			#endif
			#endif

						draw_size[side]++;

						//if(um_size[side] < pboMode)
						//	um_size[side]++;
							
				#ifdef  USE_CRITICAL_SECTION
					/* Leave the critical section -- other threads can now EnterCriticalSection() */
					LeaveCriticalSection(&drawPboMutex[side]);
				#endif

				#ifdef USE_MUTEX				
				#ifdef USE_ODBASE
						drawPboMutex[side].release();
				#else
						ReleaseMutex(drawPboMutex[side]);
				#endif
				#endif

			dindex[side] = (dindex[side] + 1) % pboMode;

			wakeUpTimerThread(side);
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

			#ifdef  USE_CRITICAL_SECTION
				/* Enter the critical section -- other threads are locked out */
				EnterCriticalSection(&drawPboMutex[side]);
			#endif

			#ifdef USE_MUTEX
			#ifdef USE_ODBASE
					drawPboMutex[side].grab();
			#else
					WaitForSingleObject(drawPboMutex[side], INFINITE);
			#endif
			#endif

						draw_size[side]++;
							
				#ifdef  USE_CRITICAL_SECTION
					/* Leave the critical section -- other threads can now EnterCriticalSection() */
					LeaveCriticalSection(&drawPboMutex[side]);
				#endif

				#ifdef USE_MUTEX				
				#ifdef USE_ODBASE
						drawPboMutex[side].release();
				#else
						ReleaseMutex(drawPboMutex[side]);
				#endif
				#endif

			dindex[side] = (dindex[side] + 1) % pboMode;

			wakeUpTimerThread(side);

			return -50;
		}

        // measure the time modifying the mapped buffer
        //t1.stop();
        //updateTime = t1.getElapsedTimeInMilliSec();
		double endTime = getGlobalVideoTimer(side);
		//printf("udp mapped buffer(ms): %f\n", (endTime-startTime));
		//printf("\t\t\t\tUPLOADTime: %f\n", (endTime-startTime));
        ///////////////////////////////////////////////////
    }
	}
	double endTime = getGlobalVideoTimer(side);
	//printf("mod mapped buffer(ms): %f\n", (endTime-initialTime));

	return 0;
}

typedef unsigned __int8   uint8_t;
int updatePBOsRingBufferDecoderCheckSize(int side)
{
    int nextIndex = 0;                  // pbo index used for next frame

	double initialTime = getGlobalVideoTimer(side);

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].grab();
#else
		WaitForSingleObject(pboMutex[side], INFINITE);
#endif
#endif

		int pbSize = pbo_size[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].release();
#else
		ReleaseMutex(pboMutex[side]);
#endif
#endif

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif

		int dbSize = draw_size[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
#endif

	//if((dbSize >= pbSize))// < pboMode))
	//{
	//	//printf("Draw queue >= pbo queue - drawSize = %d - pboSize = %d.\n", dbSize, pbSize);
	//	return 1;
	//}

	double waitTime = getGlobalVideoTimer(side);
	while(dbSize >= pbSize && !quit[side])
	{
		//printf("dbSize: %d >= pbSize: %d - WAIT\n", dbSize, pbSize);
#ifdef USE_ODBASE
		pboSemaphore[side].wait();
#else
		WaitForSingleObject(pboSemaphore[side],INFINITE);
#endif
		//ResetEvent(pboSemaphore);
		//Sleep(100);

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].grab();
#else
		WaitForSingleObject(pboMutex[side], INFINITE);
#endif
#endif

		pbSize = pbo_size[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].release();
#else
		ReleaseMutex(pboMutex[side]);
#endif
#endif

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif

		dbSize = draw_size[side];
						
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
#endif
	}
	double endwaitTime = getGlobalVideoTimer(side);
	//printf("wat mapped buffer(ms): %f\n", (endwaitTime-waitTime));

	return 0;
}

int updatePBOsRingBufferDecoder(int side, void* pict, int linesize, double pts)
{
	int nextIndex = 0;
	//for(int k=0; k<pbSize-dbSize; k++)
	{
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

		if(enableDebugOutput)
		{
			//assert(data);
			assert(pboRingBuff[nextIndex]);
		}
		//pboRingBuff[qindex[side]] = (char *)data;

		double pbopts = pts;
		//double pbopts = -1.0;
		//int ret = getNextVideoFrameNext(side, (char *)pboRingBuff[nextIndex], nextIndex, pbopts);
		//static char *rawData1 = new char[1920*1080*4];
		//int ret = getNextVideoFrameNext(side, (char *)rawData1, nextIndex, pbopts);

		//printf("^^^drawSize = %d - pboSize =   %d - nextIndex: %d - pts: %f - pboPTS[nextIndex]: %f - ret: %d\n", dbSize, pbSize, nextIndex, pbopts, pboPTS[nextIndex], ret);
		//if(ret > 0)
		if(pts >= 0)
		{
			pboPTS[nextIndex] = pbopts;
			memcpy((unsigned char *) pboRingBuff[nextIndex], pict, sizeof(uint8_t) * linesize * pboheight[side]);

			#ifdef  USE_CRITICAL_SECTION
				/* Enter the critical section -- other threads are locked out */
				EnterCriticalSection(&drawPboMutex[side]);
			#endif

			#ifdef USE_MUTEX
			#ifdef USE_ODBASE
					drawPboMutex[side].grab();
			#else
					WaitForSingleObject(drawPboMutex[side], INFINITE);
			#endif
			#endif

						draw_size[side]++;

						//if(um_size[side] < pboMode)
						//	um_size[side]++;
							
				#ifdef  USE_CRITICAL_SECTION
					/* Leave the critical section -- other threads can now EnterCriticalSection() */
					LeaveCriticalSection(&drawPboMutex[side]);
				#endif

				#ifdef USE_MUTEX				
				#ifdef USE_ODBASE
						drawPboMutex[side].release();
				#else
						ReleaseMutex(drawPboMutex[side]);
				#endif
				#endif

			dindex[side] = (dindex[side] + 1) % pboMode;

			wakeUpTimerThread(side);
		}
		//else if(pts == -50)
		//{
		//	printf("|||Pbo queue has reached the end.\n");
		//	//printf("===drawSize = %d - pboSize =   %d - nextIndex: %d - pts: %f - pboPTS[nextIndex]: %f\n", dbSize, pbSize, nextIndex, pbopts, pboPTS[nextIndex]);
		//	
		//	pboPTS[nextIndex] = -50;

		//	#ifdef  USE_CRITICAL_SECTION
		//		/* Enter the critical section -- other threads are locked out */
		//		EnterCriticalSection(&drawPboMutex[side]);
		//	#endif

		//	#ifdef USE_MUTEX
		//	#ifdef USE_ODBASE
		//			drawPboMutex[side].grab();
		//	#else
		//			WaitForSingleObject(drawPboMutex[side], INFINITE);
		//	#endif
		//	#endif

		//				draw_size[side]++;
		//					
		//		#ifdef  USE_CRITICAL_SECTION
		//			/* Leave the critical section -- other threads can now EnterCriticalSection() */
		//			LeaveCriticalSection(&drawPboMutex[side]);
		//		#endif

		//		#ifdef USE_MUTEX				
		//		#ifdef USE_ODBASE
		//				drawPboMutex[side].release();
		//		#else
		//				ReleaseMutex(drawPboMutex[side]);
		//		#endif
		//		#endif

		//	dindex[side] = (dindex[side] + 1) % pboMode;

		//	wakeUpTimerThread(side);

		//	return -50;
		//}

        // measure the time modifying the mapped buffer
        //t1.stop();
        //updateTime = t1.getElapsedTimeInMilliSec();
		double endTime = getGlobalVideoTimer(side);
		//printf("udp mapped buffer(ms): %f\n", (endTime-startTime));
		//printf("\t\t\t\tUPLOADTime: %f\n", (endTime-startTime));
        ///////////////////////////////////////////////////
    }
	}
	double endTime = getGlobalVideoTimer(side);
	//printf("mod mapped buffer(ms): %f\n", (endTime-initialTime));

	return 0;
}


void wakeUpTimerThread(int side)
{
	//printf("rel waitSemaphore\n");
#if 1
		#ifdef USE_ODBASE
			waitSemaphore[side].signal();
		#else
			ReleaseSemaphore(waitSemaphore[side], 1, NULL );
		#endif
#endif

#if 0
	if(!SetEvent(waitSemaphore))
	{
		printf("SetEvent failed (%d)\n", GetLastError);
		return;
	}
	//printf("WATEvent done\n");
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////////
void drawPBOsRingBuffer(int side)
{
    int nextIndex = 0;                  // pbo index used for next frame

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif

		int dSize = draw_size[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
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

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif

				draw_size[side] = 0;
					
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
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

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].grab();
#else
		WaitForSingleObject(pboMutex[side], INFINITE);
#endif
#endif
						pbo_size[side]--;

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].release();
#else
		ReleaseMutex(pboMutex[side]);
#endif
#endif

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif

						draw_size[side]--;
							
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
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
	if(pboMode > 1)
	{
//#ifdef  USE_CRITICAL_SECTION
//	/* Enter the critical section -- other threads are locked out */
//	EnterCriticalSection(&iPboMutex);
//#endif
//
//#ifdef USE_MUTEX
//#ifdef USE_ODBASE
//		iPboMutex.grab();
//#else
//		WaitForSingleObject(iPboMutex, INFINITE);
//#endif
//#endif
//			int nextIndex = ((qindex[side] + 1) % pboMode);
//
//#ifdef  USE_CRITICAL_SECTION
//	/* Leave the critical section -- other threads can now EnterCriticalSection() */
//	LeaveCriticalSection(&iPboMutex);
//#endif
//
//#ifdef USE_MUTEX				
//#ifdef USE_ODBASE
//		iPboMutex.release();
//#else
//		ReleaseMutex(iPboMutex);
//#endif
//#endif
//
//	return nextIndex;

	return ((qindex[side] + 1) % pboMode);
	}
	else
		return qindex[side];
	
}


////////////////////////////////////////////////////////////////////////////////////////////////////////
void clearPBOs(int side)
{
	currFrameIdx[side]	  = 0;

	for(int i=0; i<MAXSTREAMS; i++)
	{
		qindex[i] = 0;
		//memset(pboPTS, -1.0, sizeof(double)*MAX_PBOS);
		mindex[i] = 0;
		dindex[i] = 0;
		uindex[i] = 0;
	}

	for(int i=0; i<MAX_PBOS; i++)
	{
		pboPTS[i] = -1.0;
		bufferUnmapped[i] = false;
	}

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].grab();
#else
		WaitForSingleObject(pboMutex[side], INFINITE);
#endif
#endif
			pbo_size[side] = 0;

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].release();
#else
		ReleaseMutex(pboMutex[side]);
#endif
#endif

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif
		draw_size[side] = 0;
		um_size[side] = 0;
				
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
#endif

#if 1
		#ifdef USE_ODBASE
			pboSemaphore[side].signal();
		#else
			ReleaseSemaphore(pboSemaphore[side], 1, NULL );
		#endif

		#ifdef USE_ODBASE
			waitSemaphore[side].signal();
		#else
			ReleaseSemaphore(waitSemaphore[side], 1, NULL );
		#endif
#endif

#if 0
	if(!SetEvent(pboSemaphore))
	{
		printf("SetEvent failed (%d)\n", GetLastError);
		return;
	}
	printf("SetEvent clear\n");
	if(!SetEvent(waitSemaphore))
	{
		printf("SetEvent failed (%d)\n", GetLastError);
		return;
	}
	printf("SetEvent clear\n");
#endif
}

void waitOnBufFill(int side)
{
	//printf("Wait on buffer...\n");
	#ifdef USE_ODBASE
			waitSemaphorep[side].wait();
	#else
			WaitForSingleObject(waitSemaphore[side],INFINITE);
	#endif
	//ResetEvent(waitSemaphore);

	//#ifdef USE_ODBASE
	//		pboSemaphore[side].wait();
	//#else
	//		WaitForSingleObject(pboSemaphore[side],INFINITE);
	//#endif
	//ResetEvent(pboSemaphore);

#if 1
	#ifdef USE_ODBASE
		pboSemaphore[side].signal();
	#else
		ReleaseSemaphore(pboSemaphore[side], 1, NULL );
	#endif
#endif

#if 0
	if(!SetEvent(pboSemaphore))
	{
		printf("SetEvent failed (%d)\n", GetLastError);
		return;
	}
	//printf("SetEvent clear\n");
#endif
}

void doneWithFrame(int side)
{
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
    EnterCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].grab();
#else
		WaitForSingleObject(pboMutex[side], INFINITE);
#endif
#endif
			if(pbo_size[side] > 0)
				pbo_size[side]--;

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
    LeaveCriticalSection(&pboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		pboMutex[side].release();
#else
		ReleaseMutex(pboMutex[side]);
#endif
#endif

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif
			if(draw_size[side] > 0)
				draw_size[side]--;

			if(um_size[side] > 0)
				um_size[side]--;
				
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
#endif
}

void skipFrame(int side)
{
//#ifdef  USE_CRITICAL_SECTION
//	/* Enter the critical section -- other threads are locked out */
//	EnterCriticalSection(&drawPboMutex);
//#endif
//
//#ifdef USE_MUTEX
//#ifdef USE_ODBASE
//		drawPboMutex.grab();
//#else
//		WaitForSingleObject(drawPboMutex, INFINITE);
//#endif
//#endif
//				int umSize = um_size[side];
//				
//#ifdef  USE_CRITICAL_SECTION
//	/* Leave the critical section -- other threads can now EnterCriticalSection() */
//	LeaveCriticalSection(&drawPboMutex);
//#endif
//
//#ifdef USE_MUTEX				
//#ifdef USE_ODBASE
//		drawPboMutex.release();
//#else
//		ReleaseMutex(drawPboMutex);
//#endif
//#endif
//	if(umSize > 0)
//	{

#if 1
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&iPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		iPboMutex[side].grab();
#else
		WaitForSingleObject(iPboMutex[side], INFINITE);
#endif
#endif		
			if(pboMode == 1)
				qindex[side] = 0;
			else if(pboMode > 1)
				qindex[side] = (qindex[side] + 1) % pboMode;
			int nextIndex = qindex[side];
				
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&iPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		iPboMutex[side].release();
#else
		ReleaseMutex(iPboMutex[side]);
#endif
#endif
#endif

		pboPTS[nextIndex] = -1;

		doneWithFrame(side);
	//}

	#ifdef USE_ODBASE
		pboSemaphore[side].signal();
	#else
		ReleaseSemaphore(pboSemaphore[side], 1, NULL );
	#endif
}



void increamentPBORingBuffer(int side)
{
    int nextIndex = 0;                  // pbo index used for next frame

	double startTime = getGlobalVideoTimer(side);

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif

		int dSize = draw_size[side];
		int umSize = um_size[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
#endif

	if(umSize<=0)
	//if(!(dSize < pboMode))
	//if(dSize <= 0)
	{
		//printf("Unmap queue empty - Draw size = %d - qindex[side]: %d\n", dSize, qindex[side]);
//		skipFrame(side);
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
			{

				nextIndex = (qindex[side] + 1) % pboMode;
				//qindex[side] = (qindex[side] + 1) % pboMode;
				//nextIndex = qindex[side];
			}
			
			if(pboPTS[nextIndex] == -50)
			{
				//printf("END!!!!!!! dSize[%d]: %d - qindex[%d]: %d - pbopts: %f\n", side, dSize, side, nextIndex, pboPTS[nextIndex]);

#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		drawPboMutex[side].grab();
#else
		WaitForSingleObject(drawPboMutex[side], INFINITE);
#endif
#endif

				draw_size[side] = 0;
				um_size[side] = 0;
					

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&drawPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		drawPboMutex[side].release();
#else
		ReleaseMutex(drawPboMutex[side]);
#endif
#endif

				return;
			}

		}
		//printf("~~~dSize[%d]:  %d - qindex[%d]: %d - pbopts: %f - \n", side, dSize, side, nextIndex, pboPTS[nextIndex] );

		if(pboPTS[nextIndex] != -50)
		{		
		
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&iPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		iPboMutex[side].grab();
#else
		WaitForSingleObject(iPboMutex[side], INFINITE);
#endif
#endif

				qindex[side] = (qindex[side] + 1) % pboMode;
					
#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&iPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		iPboMutex[side].release();
#else
		ReleaseMutex(iPboMutex[side]);
#endif
#endif	
		
			pboPTS[nextIndex] = -1;
			//skipFrame(side);
			doneWithFrame(side);

#if 1
		#ifdef USE_ODBASE
			pboSemaphore[side].signal();
		#else
			ReleaseSemaphore(pboSemaphore[side], 1, NULL );
		#endif
		//#ifdef USE_ODBASE
		//	waitSemaphore[side].signal();
		//#else
		//	ReleaseSemaphore(waitSemaphore[side], 1, NULL );
		//#endif
#endif
#if 0
		if(!SetEvent(pboSemaphore))
		{
			printf("SetEvent failed (%d)\n", GetLastError);
			return;
		}
		//printf("PBOEvent done\n");
		//if(!SetEvent(waitSemaphore))
		//{
		//	printf("SetEvent failed (%d)\n", GetLastError);
		//	return;
		//}
		//printf("WATEvent done\n");
#endif
		}

    }
	double endTime = getGlobalVideoTimer(side);
	//printf("anc mapped buffer(ms): %f\n", (endTime-startTime));
}

void drawPBOsRingBuffer2(int side)
{
	int nextIndex = 0;                  // pbo index used for next frame

	double startTime = getGlobalVideoTimer(side);
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&iPboMutex[side]);
#endif

#ifdef USE_MUTEX
#ifdef USE_ODBASE
		iPboMutex[side].grab();
#else
		WaitForSingleObject(iPboMutex[side], INFINITE);
#endif
#endif

			nextIndex = qindex[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&iPboMutex[side]);
#endif

#ifdef USE_MUTEX				
#ifdef USE_ODBASE
		iPboMutex[side].release();
#else
		ReleaseMutex(iPboMutex[side]);
#endif
#endif

		//if(!lchanged)
		//	return;
#if 1
		if(pboMode > 1)
		{
			if(currFrameIdx[side] == nextIndex)
				return;
			//if((currFrameIdx[side] + 1) % pboMode != nextIndex)
			//	printf("===qindex[%d]:  %d - currFrameIdx: %d -  pbopts: %f - MISSED FRAME: nextFrameDelay[i]: %09.6f\n",  side, nextIndex, currFrameIdx[side], pboPTS[nextIndex], nextFrameDelay[side]);
			currFrameIdx[side] = nextIndex;
		}
#endif
		//if(!bufferUnmapped[nextIndex])
		//{
		//	printf("===qindex[%d]:  %d - pbopts: %f - bufferUnmapped: %d\n",  side, nextIndex, pboPTS[nextIndex], bufferUnmapped[nextIndex]);
		//	return;
		//}
		//bufferUnmapped[nextIndex] = false;

#if 0
#ifdef  USE_CRITICAL_SECTION
	/* Enter the critical section -- other threads are locked out */
	EnterCriticalSection(&drawPboMutex);
#endif

#ifdef USE_MUTEX
	#ifdef USE_ODBASE
			drawPboMutex.grab();
	#else
			WaitForSingleObject(drawPboMutex, INFINITE);
	#endif
#endif
			int dSize = draw_size[side];

#ifdef  USE_CRITICAL_SECTION
	/* Leave the critical section -- other threads can now EnterCriticalSection() */
	LeaveCriticalSection(&drawPboMutex);
#endif

#ifdef USE_MUTEX
	#ifdef USE_ODBASE
			drawPboMutex.release();
	#else
			ReleaseMutex(drawPboMutex);
	#endif
#endif
#endif

		//printf("===dSize[%d]:  %d - qindex[%d]:  %d - pbopts:    %f - bufferUnmapped: %d\n", side, dSize, side, nextIndex, pboPTS[nextIndex], bufferUnmapped[nextIndex]);
		//printf("===qindex[%d]: %d - pbopts: %f\n", side, nextIndex, pboPTS[nextIndex]);
		//printf("Pbo uploaded - size = %d.\n", pbo_size[side]);
		glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pboIds[side][nextIndex]);
		//glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer
		//GLboolean res = glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER); // release pointer to mapping buffer
		//if(GL_FALSE)
		//	printf("BAD :(\n");

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

		{
			//skipFrame(side);
			//doneWithFrame(side);

			//// it is good idea to release PBOs with ID 0 after use.
			//// Once bound with 0, all pixel operations are back to normal ways.
			glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		}
		double endTime = getGlobalVideoTimer(side);
		//printf("bnd mapped buffer(ms): %f\n", (endTime-startTime));
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