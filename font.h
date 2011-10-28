#ifndef FONT_H_
#define FONT_H_ 1

#include "draw.h"

void
font_init ();

size_t
font_width (const char* text);

void
font_draw (struct canvas* canvas, size_t x, size_t y, const char* text, int direction,
	   unsigned int blackness);

#endif /* !FONT_H_ */
