#ifndef GLFONT_H
#define GLFONT_H

#include <windows.h>

class GLFont {
public:
	GLFont(HDC hDC, char * fontname, int fontsize);
	~GLFont();
    int getFontSize() const;
	void setposition(float x, float y);
	void displaytext(const char * message);
	void removeFont();
private:
	float xpos, ypos;
	int	base;
    int size;
	HDC	hDC;
};

#endif