#include "constants.h"
//#include <windows.h>
#include <process.h>

void intiPBOs(int width, int height, int side);
void updatePBOs(int side);
void updatePBOThreads(void* side);
void drawPBO(int side);

bool pbosFull(int side);
bool pbosEmpty(int side);

extern void startGlobalVideoTimer(int videounit);
extern double getGlobalVideoTimer(int videounit);