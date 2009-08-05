/*  Font functions for munin-hardcore.
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

#include <err.h>
#include <math.h>
#include <wchar.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "draw.h"

FT_Library       ft_library;
FT_Face          ft_face;
FTC_Manager      ft_cache_mgr;
FTC_SBitCache    ft_sbit_cache;
FTC_ImageTypeRec ft_image_type;

#define FONT_NAME "/usr/share/munin/VeraMono.ttf"
#define FONT_CACHE_SIZE (1024 * 1024)

static FT_Error
face_requester(FTC_FaceID face_id, FT_Library library, FT_Pointer request_data, FT_Face* aface)
{
  *aface = ft_face;

  return 0;
}

void
font_init()
{
  int result;

  result = FT_Init_FreeType(&ft_library);

  if(result)
    errx(EXIT_FAILURE, "Error initializing FreeType: %d", result);

  if(0 != (result = FT_New_Face(ft_library, FONT_NAME, 0, &ft_face)))
    errx(EXIT_FAILURE, "Error opening font '%s': %d", FONT_NAME, result);

  if(0 != (result = FT_Set_Pixel_Sizes(ft_face, 0, 10)))
    errx(EXIT_FAILURE, "Error setting size (%d pixels): %d", 10, result);

  if(0 != (result = FTC_Manager_New(ft_library, 1, 1, FONT_CACHE_SIZE, face_requester, NULL, &ft_cache_mgr)))
    errx(EXIT_FAILURE, "Failed to create font cache manager: %d", result);

  if(0 != (result = FTC_SBitCache_New(ft_cache_mgr, &ft_sbit_cache)))
    errx(EXIT_FAILURE, "Failed to create font image cache: %d", result);

  memset(&ft_image_type, 0, sizeof(ft_image_type));
  ft_image_type.width = 10;
  ft_image_type.height = 10;
  ft_image_type.flags = FT_LOAD_DEFAULT | FT_LOAD_RENDER;
}

size_t
font_width(const char* text)
{
  size_t width = 0;
  int result;
  FT_UInt idx;
  FTC_SBit sbit;

  while(*text)
  {
    unsigned int n = 0, ch = *text++;

    if(!(ch & 0x80))
      n = 0;
    else if((ch & 0xE0) == 0xC0)
      ch &= 0x1F, n = 1;
    else if((ch & 0xF0) == 0xE0)
      ch &= 0x0F, n = 2;
    else if((ch & 0xF8) == 0xF0)
      ch &= 0x07, n = 3;
    else if((ch & 0xFC) == 0xF8)
      ch &= 0x03, n = 4;
    else if((ch & 0xFE) == 0xFC)
      ch &= 0x01, n = 5;

    while(n-- && *text)
      ch <<= 6, ch |= (*text++ & 0x3F);

    idx = FT_Get_Char_Index(ft_face, ch);

    if(!idx)
      continue;

    if(0 != (result = FTC_SBitCache_Lookup(ft_sbit_cache, &ft_image_type, idx, &sbit, 0)))
      continue;

    width += sbit->xadvance;
  }

  return width;
}

void
font_draw(struct canvas* canvas, size_t x, size_t y, const char* text, int direction)
{
  int result;
  FT_UInt idx;
  FTC_SBit sbit;
  size_t yy, xx;

  if(direction == -1)
    x -= font_width(text);
  else if(direction == -2)
    x -= font_width(text) >> 1;

  while(*text)
  {
    unsigned int n = 0, ch = *text++;

    if(!(ch & 0x80))
      n = 0;
    else if((ch & 0xE0) == 0xC0)
      ch &= 0x1F, n = 1;
    else if((ch & 0xF0) == 0xE0)
      ch &= 0x0F, n = 2;
    else if((ch & 0xF8) == 0xF0)
      ch &= 0x07, n = 3;
    else if((ch & 0xFC) == 0xF8)
      ch &= 0x03, n = 4;
    else if((ch & 0xFE) == 0xFC)
      ch &= 0x01, n = 5;

    if(n)
      while(n-- && *text)
        ch <<= 6, ch |= (*text++ & 0x3F);

    idx = FT_Get_Char_Index(ft_face, ch);

    if(!idx)
      continue;

    if(0 != (result = FTC_SBitCache_Lookup(ft_sbit_cache, &ft_image_type, idx, &sbit, 0)))
      continue;

    int x_off = sbit->left;
    int y_off = -sbit->top + (ft_face->descender >> 8) - 1;

    switch(direction)
    {
    case -2:
    case -1:
    case 0:

      for(yy = 0; yy < sbit->height; ++yy)
      {
        int eff_y = (y + yy + y_off);

        if(eff_y < 0 || eff_y >= canvas->height)
          continue;

        for(xx = 0; xx < sbit->width; ++xx)
        {
          int eff_x = (x + xx + x_off);

          if(eff_x < 0 || eff_x >= canvas->width)
            continue;

          size_t i = (eff_y * canvas->width + eff_x) * 3;

          unsigned int alpha = sbit->buffer[yy * sbit->pitch + xx];
          unsigned int inv_alpha = 256 - alpha;

          canvas->data[i + 0] = (canvas->data[i + 0] * inv_alpha) >> 8;
          canvas->data[i + 1] = (canvas->data[i + 1] * inv_alpha) >> 8;
          canvas->data[i + 2] = (canvas->data[i + 2] * inv_alpha) >> 8;
        }
      }

      x += sbit->xadvance;

      break;

    case 1:

      for(yy = 0; yy < sbit->height; ++yy)
      {
        int eff_x = (x - yy - y_off);

        if(eff_x < 0 || eff_x >= canvas->width)
          continue;

        for(xx = 0; xx < sbit->width; ++xx)
        {
          int eff_y = (y + xx + x_off);

          if(eff_y < 0 || eff_y >= canvas->height)
            continue;


          size_t i = (eff_y * canvas->width + eff_x) * 3;

          unsigned int alpha = sbit->buffer[yy * sbit->pitch + xx];
          unsigned int inv_alpha = 256 - alpha;

          canvas->data[i + 0] = (canvas->data[i + 0] * inv_alpha) >> 8;
          canvas->data[i + 1] = (canvas->data[i + 1] * inv_alpha) >> 8;
          canvas->data[i + 2] = (canvas->data[i + 2] * inv_alpha) >> 8;
        }
      }

      y += sbit->xadvance;

      break;

    case 2:

      for(yy = 0; yy < sbit->height; ++yy)
      {
        int eff_x = (x + yy + y_off);

        if(eff_x < 0 || eff_x >= canvas->width)
          continue;

        for(xx = 0; xx < sbit->width; ++xx)
        {
          int eff_y = (y - xx - x_off);

          if(eff_y < 0 || eff_y >= canvas->height)
            continue;

          size_t i = (eff_y * canvas->width + eff_x) * 3;

          unsigned int alpha = sbit->buffer[yy * sbit->pitch + xx];
          unsigned int inv_alpha = 256 - alpha;

          canvas->data[i + 0] = (canvas->data[i + 0] * inv_alpha) >> 8;
          canvas->data[i + 1] = (canvas->data[i + 1] * inv_alpha) >> 8;
          canvas->data[i + 2] = (canvas->data[i + 2] * inv_alpha) >> 8;
        }
      }

      y -= sbit->xadvance;

      break;
    }
  }
}
