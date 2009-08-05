/*  Drawing functions for munin-hardcore.
    Copyright (C) 2009  Morten Hustveit <morten@rashbox.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "draw.h"

#define ANTI_ALIASING 1

void
draw_vline(struct canvas* canvas, int x, int y0, int y1, uint32_t color)
{
  if(y0 > y1)
  {
    size_t tmp = y0;
    y0 = y1;
    y1 = tmp;
  }

  while(y0 <= y1)
  {
    size_t i = (y0 * canvas->width + x) * 3;

    canvas->data[i + 0] = color >> 16;
    canvas->data[i + 1] = color >> 8;
    canvas->data[i + 2] = color;

    ++y0;
  }
}

void
draw_line(struct canvas* canvas, size_t x0, size_t y0, size_t x1, size_t y1, uint32_t color)
{
  size_t y_count, x_count;
  unsigned char* pixel;
  unsigned char r, g, b;
  int step_x, step_y, x, y;

  r = color >> 16;
  g = color >> 8;
  b = color;

  y_count = abs(y1 - y0);
  x_count = abs(x1 - x0);

  if(y_count > x_count)
  {
    step_y = (y0 < y1) ? 1 : -1;
    step_x = (x1 - x0) * 65536 / y_count;

    x = x0 << 16;

    while(y0 != y1)
    {
      pixel = &canvas->data[(y0 * canvas->width + (x >> 16)) * 3];

#if ANTI_ALIASING
      unsigned int weight_a = (x & 0xffff) >> 8;
      unsigned int weight_b = 0xff - weight_a;

      pixel[0] = (weight_b * r + weight_a * pixel[0]) / 255;
      pixel[1] = (weight_b * g + weight_a * pixel[1]) / 255;
      pixel[2] = (weight_b * b + weight_a * pixel[2]) / 255;

      pixel += 3;

      pixel[0] = (weight_a * r + weight_b * pixel[0]) / 255;
      pixel[1] = (weight_a * g + weight_b * pixel[1]) / 255;
      pixel[2] = (weight_a * b + weight_b * pixel[2]) / 255;
#else
      pixel[0] = r;
      pixel[1] = g;
      pixel[2] = b;
#endif

      x += step_x;
      y0 += step_y;
    }
  }
  else
  {
    if(!x_count)
      return;

    step_x = (x0 < x1) ? 1 : -1;
    step_y = (y1 - y0) * 65536 / x_count;

    y = y0 << 16;

    while(x0 != x1)
    {
      pixel = &canvas->data[((y >> 16) * canvas->width + x0) * 3];

#if ANTI_ALIASING
      unsigned int weight_a = (y & 0xffff) >> 8;
      unsigned int weight_b = 0xff - weight_a;

      pixel[0] = (weight_b * r + weight_a * pixel[0]) / 255;
      pixel[1] = (weight_b * g + weight_a * pixel[1]) / 255;
      pixel[2] = (weight_b * b + weight_a * pixel[2]) / 255;

      pixel += canvas->width * 3;

      pixel[0] = (weight_a * r + weight_b * pixel[0]) / 255;
      pixel[1] = (weight_a * g + weight_b * pixel[1]) / 255;
      pixel[2] = (weight_a * b + weight_b * pixel[2]) / 255;
#else
      pixel[0] = r;
      pixel[1] = g;
      pixel[2] = b;
#endif

      x0 += step_x;
      y += step_y;
    }
  }
}

void
draw_rect(struct canvas* canvas, size_t x, size_t y, size_t width, size_t height, uint32_t color)
{
  unsigned char* out = &canvas->data[(y * canvas->width + x) * 3];
  unsigned char r, g, b;
  size_t yy, xx;

  r = color >> 16;
  g = color >> 8;
  b = color;

  for(yy = 0; yy < height; ++yy)
  {
    for(xx = 0; xx < width; ++xx)
    {
      out[0] = r;
      out[1] = g;
      out[2] = b;
      out += 3;
    }

    out += (canvas->width - width) * 3;
  }
}

void
draw_pixel(struct canvas* canvas, size_t x, size_t y, uint32_t color)
{
  size_t i = (y * canvas->width + x) * 3;

  canvas->data[i + 0] = color >> 16;
  canvas->data[i + 1] = color >> 8;
  canvas->data[i + 2] = color;
}
