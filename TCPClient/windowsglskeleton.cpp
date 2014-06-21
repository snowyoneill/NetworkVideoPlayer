#include "main.h"
#include "windowsglskeleton.h"

HDC			hDC=NULL;		// Private GDI Device Context
HGLRC		hRC=NULL;		// Permanent Rendering Context
HWND		hWnd=NULL;		// Holds Our Window Handle
HINSTANCE	hInstance;		// Holds The Instance Of The Application

void KillGLWindow()
{
	if(fullscreen)
		ChangeDisplaySettings(NULL,0);					// If So Switch Back To The Desktop
	ShowCursor(true);									// Show Mouse Pointer

	if (hRC)											// Do We Have A Rendering Context?
	{
		if (!wglMakeCurrent(NULL,NULL))					// Are We Able To Release The DC And RC Contexts?
		{
			MessageBox(NULL,"Release Of DC And RC Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}

		if (!wglDeleteContext(hRC))						// Are We Able To Delete The RC?
		{
			MessageBox(NULL,"Release Rendering Context Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}
		hRC=NULL;										// Set RC To NULL
	}

	if (hDC && !ReleaseDC(hWnd,hDC))					// Are We Able To Release The DC
	{
		MessageBox(NULL,"Release Device Context Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		hDC=NULL;										// Set DC To NULL
	}

	if (hWnd && !DestroyWindow(hWnd))					// Are We Able To Destroy The Window?
	{
		MessageBox(NULL,"Could Not Release hWnd.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		hWnd=NULL;										// Set hWnd To NULL
	}

	if (!UnregisterClass("OpenGL",hInstance))			// Are We Able To Unregister Class
	{
		MessageBox(NULL,"Could Not Unregister Class.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		hInstance=NULL;									// Set hInstance To NULL
	}

}

HWND CreateGLWindow(char* title, int width, int height, int bits, int zbuff,int alphabuff, bool fullscreenflag)
{
	GLuint		PixelFormat;			// Holds The Results After Searching For A Match
	WNDCLASS	wc;						// Windows Class Structure
	DWORD		dwExStyle;				// Window Extended Style
	DWORD		dwStyle;				// Window Style
	RECT		WindowRect;				// Grabs Rectangle Upper Left / Lower Right Values
	WindowRect.left=(long)0;			// Set Left Value To 0
	WindowRect.right=(long)width;		// Set Right Value To Requested Width
	WindowRect.top=(long)0;				// Set Top Value To 0
	WindowRect.bottom=(long)height;		// Set Bottom Value To Requested Height

	fullscreen = fullscreenflag;			// Set The Global Fullscreen Flag

	hInstance			= GetModuleHandle(NULL);				// Grab An Instance For Our Window
	wc.style			= CS_HREDRAW | CS_VREDRAW | CS_OWNDC | CS_DBLCLKS;	// Redraw On Size, And Own DC For Window.
	wc.lpfnWndProc		= (WNDPROC) WndProc;					// WndProc Handles Messages
	wc.cbClsExtra		= 0;									// No Extra Window Data
	wc.cbWndExtra		= 0;									// No Extra Window Data
	wc.hInstance		= hInstance;							// Set The Instance
	wc.hIcon			= NULL;					// Load The Default Icon
	wc.hCursor			= LoadCursor(NULL, IDC_ARROW);			// Load The Arrow Pointer
	wc.hbrBackground	= NULL;									// No Background Required For GL
	wc.lpszMenuName		= NULL;									// We Don't Want A Menu
	wc.lpszClassName	= "OpenGL";								// Set The Class Name

	if (!RegisterClass(&wc))									// Attempt To Register The Window Class
	{
		MessageBox(NULL,"Failed To Register The Window Class.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return 0;	     										// Return false
	}
	
	if (fullscreenflag)											// Attempt Fullscreen Mode?
	{
        LONG lStyles = GetWindowLong(hWnd, GWL_STYLE);
        lStyles ^= (WS_CAPTION|WS_SIZEBOX|WS_SYSMENU|WS_MAXIMIZEBOX|WS_MINIMIZEBOX);
        SetWindowLong(hWnd, GWL_STYLE, lStyles);

        // Change the size and position.
#if !defined(SM_CXVIRTUALSCREEN)
        int width = GetSystemMetrics(SM_CXSCREEN);
        int height = GetSystemMetrics(SM_CYSCREEN);
#else
        int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
#endif
        SetWindowPos(hWnd, NULL, 0, 0, width, height, SWP_FRAMECHANGED|SWP_NOZORDER);

		DEVMODE dmScreenSettings;								// Device Mode
		memset(&dmScreenSettings,0,sizeof(dmScreenSettings));	// Makes Sure Memory's Cleared
		dmScreenSettings.dmSize=sizeof(dmScreenSettings);		// Size Of The Devmode Structure
		dmScreenSettings.dmPelsWidth	= width;				// Selected Screen Width
		dmScreenSettings.dmPelsHeight	= height;				// Selected Screen Height
		dmScreenSettings.dmBitsPerPel	= bits;					// Selected Bits Per Pixel
		dmScreenSettings.dmFields=DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT;

		// Try To Set Selected Mode And Get Results.  NOTE: CDS_FULLSCREEN Gets Rid Of Start Bar.
		if (ChangeDisplaySettings(&dmScreenSettings,CDS_FULLSCREEN)!=DISP_CHANGE_SUCCESSFUL)
		{
			// If The Mode Fails, Offer Two Options.  Quit Or Use Windowed Mode.
			if (MessageBox(NULL,"The Requested Fullscreen Mode Is Not Supported By\nYour Video Card. Use Windowed Mode Instead?","NeHe GL",MB_YESNO|MB_ICONEXCLAMATION)==IDNO)
				return false;
		}
	}

	if (fullscreenflag)												// Are We Still In Fullscreen Mode?
	{
		dwExStyle=WS_EX_APPWINDOW;								// Window Extended Style
		dwStyle=WS_POPUP;										// Windows Style
        ShowCursor(false);										// Hide Mouse Pointer
	}
	else
	{
		dwExStyle=WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;			// Window Extended Style
		dwStyle=WS_OVERLAPPEDWINDOW;							// Windows Style
	}

	AdjustWindowRectEx(&WindowRect, dwStyle, false, dwExStyle);		// Adjust Window To True Requested Size

	if (!(hWnd=CreateWindowEx(	dwExStyle,							// Extended Style For The Window
								"OpenGL",							// Class Name
								title,								// Window Title
								dwStyle |							// Defined Window Style
								WS_CLIPSIBLINGS |					// Required Window Style
								WS_CLIPCHILDREN,					// Required Window Style
								0, 0,								// Window Position
								WindowRect.right-WindowRect.left,	// Calculate Window Width
								WindowRect.bottom-WindowRect.top,	// Calculate Window Height
								NULL,								// No Parent Window
								NULL,								// No Menu
								hInstance,							// Instance
								NULL)))								// Dont Pass Anything To WM_CREATE
	{
		KillGLWindow();								// Reset The Display
		MessageBox(NULL,"Window Creation Error.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return 0;	    							// Return false
	}

	static PIXELFORMATDESCRIPTOR pfd=				// pfd Tells Windows How We Want Things To Be
	{
		sizeof(PIXELFORMATDESCRIPTOR),				// Size Of This Pixel Format Descriptor
		1,											// Version Number
		PFD_DRAW_TO_WINDOW |						// Format Must Support Window
		PFD_SUPPORT_OPENGL |						// Format Must Support OpenGL
		PFD_DOUBLEBUFFER,							// Must Support Double Buffering
		PFD_TYPE_RGBA,								// Request An RGBA Format
		bits,										// Select Our Color Depth
		0, 0, 0, 0, 0, 0,							// Color Bits Ignored
		alphabuff,											// No Alpha Buffer
		0,											// Shift Bit Ignored
		0,											// No Accumulation Buffer
		0, 0, 0, 0,									// Accumulation Bits Ignored
		zbuff,											// 16Bit Z-Buffer (Depth Buffer)  
		8,											// No Stencil Buffer
		0,											// No Auxiliary Buffer
		PFD_MAIN_PLANE,								// Main Drawing Layer
		3,											// Reserved
		0, 0, 0										// Layer Masks Ignored
	};
	
	if (!(hDC=GetDC(hWnd)))							// Did We Get A Device Context?
	{
		KillGLWindow();								// Reset The Display
		MessageBox(NULL,"Can't Create A GL Device Context.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return 0;   								// Return false
	}

	if (!(PixelFormat=ChoosePixelFormat(hDC,&pfd)))	// Did Windows Find A Matching Pixel Format?
	{
		KillGLWindow();								// Reset The Display
		MessageBox(NULL,"Can't Find A Suitable PixelFormat.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return 0;   								// Return false
	}

	if(!SetPixelFormat(hDC,PixelFormat,&pfd))		// Are We Able To Set The Pixel Format?
	{
		KillGLWindow();								// Reset The Display
		MessageBox(NULL,"Can't Set The PixelFormat.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return 0;    								// Return false
	}

	if (!(hRC=wglCreateContext(hDC)))				// Are We Able To Get A Rendering Context?
	{
		KillGLWindow();								// Reset The Display
		MessageBox(NULL,"Can't Create A GL Rendering Context.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return 0;	    							// Return false
	}

	if(!wglMakeCurrent(hDC,hRC))					// Try To Activate The Rendering Context
	{
		KillGLWindow();								// Reset The Display
		MessageBox(NULL,"Can't Activate The GL Rendering Context.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return 0;	    							// Return false
	}

	if (!InitGL())									// Initialize Our Newly Created GL Window
	{
		KillGLWindow();								// Reset The Display
		MessageBox(NULL,"Initialization Failed.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return 0;   								// Return false
	}

	ShowWindow(hWnd,SW_SHOW);						// Show The Window
	SetForegroundWindow(hWnd);						// Slightly Higher Priority
	SetFocus(hWnd);									// Sets Keyboard Focus To The Window

	//UpdateWindow( hWnd );        // Sends WM_PAINT message
	ReSizeGLScene(width, height);					// Set Up Our Perspective GL Screen

	return hWnd;									// Success
}

LRESULT CALLBACK WndProc(	HWND	hWnd,			// Handle For This Window
							UINT	uMsg,			// Message For This Window
							WPARAM	wParam,			// Additional Message Information
							LPARAM	lParam)			// Additional Message Information
{
	//PAINTSTRUCT ps;

	switch (uMsg)									// Check For Windows Messages
	{
		case WM_SYSCOMMAND:							// Intercept System Commands
		{
			switch (wParam)							// Check System Calls
			{
				case SC_SCREENSAVE:					// Screensaver Trying To Start?
				case SC_MONITORPOWER:				// Monitor Trying To Enter Powersave?
				return 0;							// Prevent From Happening
			}
			break;									// Exit
		}

		case WM_CLOSE:								// Did We Receive A Close Message?
		{
			//shutdownPlayer();
			PostQuitMessage(0);						// Send A Quit Message
			return 0;								// Jump Back
		}

		case WM_SIZE:								// Resize The OpenGL Window
		{
			ReSizeGLScene(LOWORD(lParam),HIWORD(lParam));  // LoWord=Width, HiWord=Height
			return 0;								// Jump Back
		}
		case WM_KEYDOWN:			
		{
			keyHandle((char)wParam);
			keys[wParam] = true;		
			return 0;						
		}

		case WM_KEYUP:						
		{
			upKeyHandle((char)wParam);
			keys[wParam] = false;
			return 0;
		}

		case WM_LBUTTONDOWN:
		{
			//mouse_x = LOWORD(lParam);          
			//mouse_y = HIWORD(lParam);
			return 0;
		}
		case WM_RBUTTONDBLCLK:
		{
			//rmouse_x = LOWORD(lParam);          
			//rmouse_y = HIWORD(lParam);
			fullScreenVideo = !fullScreenVideo;
		}
		//case WM_RBUTTONDOWN:
		//{
		//	rmouse_x = LOWORD(lParam);          
		//	rmouse_y = HIWORD(lParam);
		//	return 0;
		//}
		//case WM_RBUTTONUP:
		//{
		//	rmouse_x = -1;       
		//	rmouse_y = -1;
		//	return 0;
		//}
#ifdef WINDOWS7
		case WM_TOUCH:
        {
            //OutputDebugString("Got a multi-touch event!\n");
            if (touchSourceType == "native")
                return touchSource->handleTouchEvents(hWnd, wParam, lParam);
            // else, use DefWindowProc
		}
        case WM_GESTURE:
        {
            //OutputDebugString("Got a gesture event!\n");
            // use DefWindowProc
        }
#endif
		case WM_LBUTTONUP:
		{
            //OutputDebugString("Mouse button up\n");
			mouse_x = LOWORD(lParam);          
			mouse_y = HIWORD(lParam);
			return 0;
		}
		case WM_MOUSEMOVE:
		{
            //OutputDebugString("Mouse move\n");
			return 0;
		}
		case WM_MOUSEWHEEL:
		{
			#ifndef GET_X_LPARAM
			#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
			#endif
			#ifndef GET_Y_LPARAM
			#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
			#endif

			int xPos = GET_X_LPARAM(lParam); 
			int yPos = GET_Y_LPARAM(lParam);
			float zDelta = GET_WHEEL_DELTA_WPARAM(wParam);
			zDelta /= WHEEL_DELTA;
			printf("MOUSE_WHEEL: x: %d - y: %d - zDelta: %f\n", xPos, yPos, zDelta);

            //OutputDebugString("Mouse move\n");
			return 0;
		}
		case WM_LBUTTONDBLCLK:
		{
			//SetCapture(hWnd);
			if(!windowed_fullscreen)
				ShowWindow(hWnd, SW_MAXIMIZE);
			else
				ShowWindow(hWnd, SW_SHOWNORMAL);

			windowed_fullscreen = !windowed_fullscreen;

			return 0;
		}
/*
		case WM_PAINT:
		{
			// Draw the scene.

            // Get a DC, then make the RC current and
            // associate with this DC
            hDC = BeginPaint( hWnd, &ps );
            //wglMakeCurrent( hDC, hRC );

            DrawGLScene();

            // we're done with the RC, so
            // deselect it
            // (note: This technique is not recommended!)
            //wglMakeCurrent( NULL, NULL );

            EndPaint( hWnd, &ps );
            return 0;
		}
*/
	}
	return DefWindowProc(hWnd,uMsg,wParam,lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow)
{
	MSG	msg;									        // Windows Message Structure

	//LPWSTR *szArgList;
	//int argCount;
	//szArgList = CommandLineToArgvW(GetCommandLineW(), &argCount);
	//if (szArgList == NULL)
	//{
	//	MessageBox(NULL, "Unable to parse command line", "Error", MB_OK);
	//	return 0;
	//}
	//LocalFree(szArgList);
	//int argCount;
	//CommandLineToArgvW(GetCommandLineW(), &argCount);
	//argCount -= 1;

    try 
    {
        //if (!Init(args, argCount))
		if (!Init(lpCmdLine))
            return -1;

        bool done = false;
        while (!done)									// Loop That Runs While done=false
        {
            if (PeekMessage(&msg,NULL,0,0,PM_REMOVE))	// Is There A Message Waiting?
            {
                if (keys[VK_ESCAPE] || msg.message == WM_APP+1)
				{
					//shutdownPlayer();
                    done=true;							// ESC or DrawGLScene Signalled A Quit
				}
                if (msg.message==WM_QUIT)				// Have We Received A Quit Message?
                    done=true;							// If So done=true
                else									// If Not, Deal With Window Messages
                {
                    TranslateMessage(&msg);				// Translate The Message
                    DispatchMessage(&msg);				// Dispatch The Message
                }
            }
            else										// If There Are No Messages
            {
				//if(!wglMakeCurrent(getHDC(), getHRC()))
				//	printf("Couldn't make current.\n");
                DrawGLScene();
				//SwapBuffers(hDC);
				//UpdateWindow( hWnd );        // Sends WM_PAINT message
				//wglMakeCurrent(NULL, NULL);
            }
        }

		shutdownPlayer();
        KillGLWindow();
    }
	catch (...)
	{
		OutputDebugString("\n");
		MessageBox(NULL, "Caught exception.", "ERROR", MB_OK|MB_ICONEXCLAMATION);
	}

	return ((int)msg.wParam);
}

HGLRC getHRC()
{
	return hRC;
}

HDC getHDC()
{
	return hDC;
}

HWND gethWnd()
{
	return hWnd;
}

void setHRC(HGLRC context)
{
	hRC = context;
}