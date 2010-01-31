void
font_init ();

size_t
font_width (const char* text);

void
font_draw (struct canvas* canvas, size_t x, size_t y, const char* text, int direction);
