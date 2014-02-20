#include <windows.h>

#ifndef hrtimer
#define hrtimer

typedef struct {
    double start;
    double stop;

	LARGE_INTEGER initialTime;
	LARGE_INTEGER mFrequency;
	DWORD initialTicks;
	LONGLONG endTime;
	HANDLE timerThread;

	DWORD_PTR timerMask; 

} stopWatch;

int initialiseClockTimer( stopWatch *timer,  int processorID );
void startClockTimer( stopWatch *timer);
void stopClockTimer( stopWatch *timer);
double getElapsedTime( stopWatch *timer);
double getElapsedTimeInMilli( stopWatch *timer);
double getElapsedTimeInMicro( stopWatch *timer);
double getElapsedTimeInNano( stopWatch *timer);

//double getMilliseconds();

#endif