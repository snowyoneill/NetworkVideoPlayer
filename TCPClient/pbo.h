#include "constants.h"
//#include <windows.h>
#include <process.h>

void intiPBOs(int width, int height, int side);
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