class Timer
{
	

public:
	Timer();
	~Timer();

	bool setOption();
	void reset();
	unsigned long getMilliseconds();

private:
	DWORD mTimerMask;

	LONGLONG mLastTime;
    LARGE_INTEGER mStartTime;
    LARGE_INTEGER mFrequency;

	DWORD mStartTick;
};