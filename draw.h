#include <stdint.h>
#include <stdlib.h>

struct canvas
{
  unsigned char* data;
  size_t width, height;
};

void
draw_vline (struct canvas* canvas, size_t x, size_t y0, size_t y1, uint32_t color);

void
draw_line (struct canvas* canvas, size_t x0, size_t y0, size_t x1, size_t y1, uint32_t color);

void
draw_rect (struct canvas* canvas, size_t x, size_t y, size_t width, size_t height, uint32_t color);

void
draw_pixel (struct canvas* canvas, size_t x, size_t y, uint32_t color);

void
draw_pixel_50 (struct canvas* canvas, size_t x, size_t y, uint32_t color);

int
write_png (const char *file_name, size_t width, size_t height, unsigned char* data);
