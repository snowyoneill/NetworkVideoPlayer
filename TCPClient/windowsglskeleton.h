#include <windows.h>

// Returns valid window handle, or 0 if error.
HWND CreateGLWindow(char* title, int width, int height, int bits, int zbuff,int alphabuff, bool fullscreenflag);

int WINAPI WinMain(	HINSTANCE	hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine,int nCmdShow);
LRESULT	CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void	KillGLWindow();
LRESULT CALLBACK OptsDlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
HDC		getHDC();
HWND	gethWnd();
HGLRC	getHRC();
void	setHRC(HGLRC context);

static bool windowed_fullscreen = false;
static bool fullscreen = false;
static bool keys[256];
extern int  mouse_x, mouse_y;
extern int  rmouse_x, rmouse_y;
extern bool fullScreenVideo;
//static int mouse_x = 0, mouse_y = 0;