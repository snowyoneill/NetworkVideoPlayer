#include <windows.h>
#include <stdio.h>

#include "hrtime.h"

//// Dont need.
//#define min( a, b ) ( ((a) < (b)) ? (a) : (b) )

//LARGE_INTEGER initialTime;
//LARGE_INTEGER mFrequency;
//DWORD initialTicks;
//LONGLONG endTime;
//HANDLE timerThread;

// Initialise the timer
// Checks that QPC and QPF is supported
int initialiseClockTimer( stopWatch *timer, int processorID ) {

	//Telling timer what core to use for a timer read
	DWORD newTimerMask = processorID;

	// Get the current process core mask
	DWORD_PTR procMask;
	DWORD_PTR sysMask;
//#if _MSC_VER >= 1400 && defined (_M_X64)
//		GetProcessAffinityMask(GetCurrentProcess(), (PDWORD_PTR)&procMask, (PDWORD_PTR)&sysMask);
//#else
//		GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask);
//#endif
	GetProcessAffinityMask(GetCurrentProcess(), &procMask, &sysMask);

	// If new mask is 0, then set to default behavior, otherwise check
	// to make sure new timer core mask overlaps with process core mask
	// and that new timer core mask is a power of 2 (i.e. a single core)
	if( ( newTimerMask == 0 ) || ( ( ( newTimerMask & procMask ) != 0 )))// && Bitwise::isPO2( newTimerMask ) ) )
		timer->timerMask = newTimerMask;

	timer->timerMask = newTimerMask;
	timer->timerThread = GetCurrentThread();

	//processAffinityMask = (1<<timer->timerMask);
	// Set affinity
	DWORD_PTR threadAffMask = SetThreadAffinityMask(timer->timerThread, timer->timerMask);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");

#if 0
	DWORD_PTR p_processAffinityMask;
	DWORD_PTR processAffinityMask;
	DWORD_PTR systemAffinityMask;
	DWORD  AllowProcessAffinity;
	HANDLE process = GetCurrentProcess();

	if (GetProcessAffinityMask(process, &p_processAffinityMask, &systemAffinityMask) != 0)
	{
		//processAffinityMask = (1<<0);//(GetCurrentProcessorNumber() + 1);

		if(!SetProcessAffinityMask(process, timer->timerMask))
			printf("Couldn't set Process Affinity Mask\n\n");
	}
#endif

	if(!QueryPerformanceCounter(&timer->initialTime)) {
		printf("HiRes timer counter not supported %d\n", GetLastError());
		return 1;
	}
	if(!QueryPerformanceFrequency(&timer->mFrequency)) {
		printf("HiRes timer freq. not supported %d\n", GetLastError());
		return 1;
	}
#if 0
	if(!SetProcessAffinityMask(process, p_processAffinityMask))
			printf("Couldn't set Process Affinity Mask\n\n");
#endif
	// Reset affinity
	SetThreadAffinityMask(timer->timerThread, threadAffMask);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");

	timer->initialTicks = GetTickCount();
	
	timer->endTime = 0;
	timer->start = 0;
	timer->stop = 0;

	// Return 0 indicating successful initialisation.
	return 0;
}

void startClockTimer( stopWatch *timer) {
	LONGLONG newTime;
	double newTicks;
	unsigned long check;
	double milliSecsOff;
	LONGLONG adjustment;

	LARGE_INTEGER currentTime;
	// Set affinity to the first core
	/*
	 * On a multiprocessor machine, it should not matter which processor is called.
     * However, you can get different results on different processors due to bugs in the BIOS or the HAL.
	 */

	//processAffinityMask = (1<<0);
	// Set affinity
	DWORD_PTR threadAffMask = SetThreadAffinityMask(GetCurrentThread(), timer->timerMask);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");
#if 0
	DWORD_PTR p_processAffinityMask;
	DWORD_PTR processAffinityMask;
	DWORD_PTR systemAffinityMask;
	DWORD  AllowProcessAffinity;
	HANDLE process = GetCurrentProcess();

	if (GetProcessAffinityMask(process, &p_processAffinityMask, &systemAffinityMask) != 0)
	{
		//processAffinityMask = (1<<0);//(GetCurrentProcessorNumber() + 1);

		if(!SetProcessAffinityMask(process, timer->timerMask))
			printf("Couldn't set Process Affinity Mask\n\n");
	}
#endif
	// Query the timer
	QueryPerformanceCounter(&currentTime);
#if 0
	if(!SetProcessAffinityMask(process, p_processAffinityMask))
			printf("Couldn't set Process Affinity Mask\n\n");
#endif
	// Reset affinity
	SetThreadAffinityMask(GetCurrentThread(), threadAffMask);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");
	//// Resample the frequency
	//QueryPerformanceFrequency(&timer->mFrequency);

	newTime = currentTime.QuadPart - timer->initialTime.QuadPart;

	// scale by 1000 for milliseconds
	newTicks = (double) (1000 * newTime)/(double) timer->mFrequency.QuadPart;

	// detect and compensate for performance counter leaps
	// (surprisingly common, see Microsoft KB: Q274323)

	DWORD curTicks = GetTickCount();

	check = curTicks - timer->initialTicks;
	milliSecsOff = (double)(newTicks - check);
	if (milliSecsOff < -100 || milliSecsOff > 100) {
		// We must allow the timer continue
		adjustment = min(milliSecsOff * timer->mFrequency.QuadPart / 1000, newTime - timer->endTime);
		timer->initialTime.QuadPart += adjustment;
		newTime -= adjustment;

		printf("start - adjustment: %f\n", (double)adjustment);
	}
	// Record last time for adjustment
	timer->endTime = newTime;
	
	newTicks = (double)(newTime)/(double)timer->mFrequency.QuadPart;
	timer->start = newTicks;
	//printf("Start Time: %f\n", newTicks);

	//timer->start = curTicks;
}

void stopClockTimer( stopWatch *timer) {
	LONGLONG newTime;
	double newTicks;
	unsigned long check;
	double milliSecsOff;
	LONGLONG adjustment;

	LARGE_INTEGER currentTime;

	// Set affinity to the first core
	//DWORD_PTR threadAffMask = SetThreadAffinityMask(timer->timerThread, (DWORD)processorID);
	DWORD_PTR threadAffMask = SetThreadAffinityMask(GetCurrentThread()/*timer->timerThread*/, timer->timerMask);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");

#if 0
	DWORD_PTR p_processAffinityMask;
	DWORD_PTR processAffinityMask;
	DWORD_PTR systemAffinityMask;
	DWORD  AllowProcessAffinity;
	HANDLE process = GetCurrentProcess();

	if (GetProcessAffinityMask(process, &p_processAffinityMask, &systemAffinityMask) != 0)
	{
		//processAffinityMask = 1<<0;//(GetCurrentProcessorNumber() + 1);

		if(!SetProcessAffinityMask(process, timer->timerMask))
			printf("Couldn't set Process Affinity Mask\n\n");
	}
#endif
	// Query the timer
	QueryPerformanceCounter(&currentTime);

	//// Resample the frequency
	//QueryPerformanceFrequency(&timer->mFrequency);
#if 0
	if(!SetProcessAffinityMask(process, p_processAffinityMask))
			printf("Couldn't set Process Affinity Mask\n\n");
#endif
	// Reset affinity
	SetThreadAffinityMask(GetCurrentThread()/*timer->timerThread*/, threadAffMask);
	if(threadAffMask == 0)
		printf("===Setting threadAffMask failed!!!\n");

	//DWORD core = GetCurrentProcessorNumber();
	//printf("^^^^STOP Timer  - Process Affinity: %d\n", core);

	newTime = currentTime.QuadPart - timer->initialTime.QuadPart;

	// scale by 1000 for milliseconds
	newTicks = (double)(1000 * newTime)/(double)timer->mFrequency.QuadPart;

	// detect and compensate for performance counter leaps
	// (surprisingly common, see Microsoft KB: Q274323)

	DWORD curTicks = GetTickCount();

	check = curTicks - timer->initialTicks;
  	milliSecsOff = (double)(newTicks - check);
	//printf("Millisecs between clocks: %f\n", milliSecsOff);
	if (milliSecsOff < -100 || milliSecsOff > 100) {
		// We must allow the timer continue
		adjustment = min(milliSecsOff * timer->mFrequency.QuadPart / 1000, newTime - timer->endTime);
		timer->initialTime.QuadPart += adjustment;
		newTime -= adjustment;

		printf("stop - adjustment: %f\n", (double)adjustment);
	}
	// Record last time for adjustment
	timer->endTime = newTime;

	newTicks = (double)(newTime)/(double)timer->mFrequency.QuadPart;
	timer->stop = newTicks;
	//fprintf(stdout, "Stop Time : %f\n", newTicks);

	//timer->stop = curTicks;
}
#if 0
#define FREQUENCY_RESAMPLE_RATE 200
//DWORD mStartTick;
//LONGLONG mLastTime = 0;
//LARGE_INTEGER mStartTime;
//LARGE_INTEGER mFrequency;

//unsigned long getMilliseconds()
double getMilliseconds()
{
    LARGE_INTEGER currentTime;

    // Set affinity to the first core
    DWORD_PTR threadAffMask = SetThreadAffinityMask(timerThread, (DWORD)1);

    // Query the timer
    QueryPerformanceCounter(&currentTime);

    // Reset affinity
    SetThreadAffinityMask(timerThread, threadAffMask);

	// Resample the frequency
    //mQueryCount++;
    //if(mQueryCount == FREQUENCY_RESAMPLE_RATE)
    //{
    //    mQueryCount = 0;
    //    QueryPerformanceFrequency(&mFrequency);
    //}

    LONGLONG newTime = currentTime.QuadPart - initialTime.QuadPart;
    
    // scale by 1000 for milliseconds
    unsigned long newTicks = (double)(1000 * newTime)/(double)mFrequency.QuadPart;

    // detect and compensate for performance counter leaps
    // (surprisingly common, see Microsoft KB: Q274323)
    unsigned long check = GetTickCount() - initialTicks;
    signed long msecOff = (signed long)(newTicks - check);
    if (msecOff < -100 || msecOff > 100)
    {
        // We must keep the timer running forward :)
        LONGLONG adjust = min(msecOff * mFrequency.QuadPart / 1000, newTime - endTime);
        initialTime.QuadPart += adjust;
        newTime -= adjust;

        // Re-calculate milliseconds
        newTicks = (unsigned long) (1000 * newTime / mFrequency.QuadPart);
    }

    // Record last time for adjust
    endTime = newTime;

    return newTicks;
}

#endif
// This is the elapsed time in seconds
double getElapsedTime( stopWatch *timer) {
	double duration = (timer->stop) - (timer->start);
	return duration;
}

// This is the elapsed time in milliseconds
double getElapsedTimeInMilli( stopWatch *timer) {
	double duration = ((timer->stop) - (timer->start)) * 1000;
	return duration;
}

// This is the elapsed time in microseconds
double getElapsedTimeInMicro( stopWatch *timer) {
	double duration = ((timer->stop) - (timer->start)) * 1000000;
	return duration;
}

// This is the elapsed time in nanoseconds
double getElapsedTimeInNano( stopWatch *timer) {
	double duration = ((timer->stop) - (timer->start)) * 1000000000;
	return duration;
}