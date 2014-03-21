#include "glfont.h"
#include <gl\glew.h>

GLFont::GLFont(HDC hDC, char * fontname, int fontsize)
{
	HFONT	font;
	HFONT	oldfont;
	base = glGenLists(96);
    size = fontsize;

	font = CreateFont(	size,
						0,
						0,	
						0,
						FW_NORMAL,						// Font Weight
						FALSE,							// Italic
						FALSE,							// Underline
						FALSE,							// Strikeout
						ANSI_CHARSET,					// Character Set Identifier
						OUT_TT_PRECIS,					// Output Precision
						CLIP_DEFAULT_PRECIS,			// Clipping Precision
						ANTIALIASED_QUALITY,			// Output Quality
						FF_DONTCARE|DEFAULT_PITCH,		// Family And Pitch
						fontname);					// Font Name

	oldfont = (HFONT)SelectObject(hDC, font);
	if(!wglUseFontBitmaps(hDC, 32, 96, base))
	{
		//MessageBox(NULL, "Couldn't initialise font on first attempt, trying again.", "Error", MB_OK);
		if(!wglUseFontBitmaps(hDC, 32, 96, base))
			MessageBox(NULL, "Error loading font.", "Error", MB_OK);
	}
	SelectObject(hDC, oldfont);
	DeleteObject(font);
}

GLFont::~GLFont() 
{
	glDeleteLists(base, 96);
}

int GLFont::getFontSize() const
{
	return size;
}

void GLFont::removeFont()						// Delete The Font List
{
 	glDeleteLists(base, 96);				// Delete All 96 Characters ( NEW )
}

void GLFont::setposition(float x, float y)
{
	xpos = x;
	ypos = y;
}

void GLFont::displaytext(const char * message)
{
	GLboolean textureenabled;
	glGetBooleanv(GL_TEXTURE_2D, &textureenabled);
	if (textureenabled == GL_TRUE)
		glDisable(GL_TEXTURE_2D);
	glRasterPos2f(xpos, ypos);
	glPushAttrib(GL_LIST_BIT);
	glListBase(base - 32);
	glCallLists((int)strlen(message), GL_UNSIGNED_BYTE, message);
	glPopAttrib();
	if (textureenabled == GL_TRUE)
		glEnable(GL_TEXTURE_2D);
}