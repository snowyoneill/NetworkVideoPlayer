#include "constants.h"
//#include <windows.h>
#include <process.h>

void deletePBOs(int side);

void initPBOs();
void intiPBOsSide(int w, int h, int side);

void updatePBOs(int side);
void updatePBOThreads(void* side);

void drawPBO(int side);
//void drawPBO(int side, int size);

bool pbosFull(int side);
bool pbosEmpty(int side);
int pbosSize(int side);

extern void startGlobalVideoTimer(int videounit);
extern double getGlobalVideoTimer(int videounit);

void mapPBOsRingBuffer(int side);
int updatePBOsRingBuffer(int side);
void drawPBOsRingBuffer(int side);

int getCurrPbo(int side);

void increamentPBORingBuffer(int side);
void drawPBOsRingBuffer2(int side);
void skipFrame(int side);

void waitOnBufFill(int side);
void clearPBOs(int side);
void closePBOs();

void unmapPBOsRingBuffer(int side);
void wakeUpTimerThread(int side);

int updatePBOsRingBufferDecoderCheckSize(int side);
int updatePBOsRingBufferDecoder(int side, void* pict, int linesize, double pts);