#include <err.h>
#include <math.h>
#include <wchar.h>

#include <X11/extensions/Xrender.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "types.h"

FT_Library ft_library;
FT_Face ft_face;

#define FONT_NAME "/usr/share/munin/VeraMono.ttf"

void
font_init()
{
  int result;

  result = FT_Init_FreeType(&ft_library);

  if(result)
    errx(EXIT_FAILURE, "Error initializing FreeType: %d", result);

  result = FT_New_Face(ft_library, FONT_NAME, 0, &ft_face);

  if(result)
    errx(EXIT_FAILURE, "Error opening font '%s': %d", FONT_NAME, result);

  result = FT_Set_Pixel_Sizes(ft_face, 0, 10);

  if(result)
    errx(EXIT_FAILURE, "Error setting size (%d pixels): %d", 10, result);
}

size_t
font_width(const char* text)
{
  size_t width = 0;
  int result;
  FT_UInt idx;
  FT_GlyphSlot slot;

  while(*text)
  {
    unsigned int n, ch = *text++;

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

    result = FT_Load_Glyph(ft_face, idx, FT_LOAD_NO_BITMAP);

    if(result)
      continue;

    slot = ft_face->glyph;

    width += slot->advance.x >> 6;
  }

  return width;
}

void
font_draw(struct canvas* canvas, size_t x, size_t y, const char* text, int direction)
{
  int result;
  FT_UInt idx;
  FT_GlyphSlot slot;
  size_t yy, xx;

  if(direction == -1)
    x -= font_width(text);
  else if(direction == -2)
    x -= font_width(text) >> 1;

  while(*text)
  {
    unsigned int n, ch = *text++;

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

    result = FT_Load_Glyph(ft_face, idx, FT_LOAD_RENDER);

    if(result)
      continue;

    slot = ft_face->glyph;

    int x_off = slot->metrics.horiBearingX >> 6;
    int y_off = -(slot->metrics.horiBearingY >> 6) + (ft_face->descender >> 8) - 1;

    switch(direction)
    {
    case -2:
    case -1:
    case 0:

      for(yy = 0; yy < slot->bitmap.rows; ++yy)
      {
        int eff_y = (y + yy + y_off);

        if(eff_y < 0 || eff_y >= canvas->height)
          continue;

        for(xx = 0; xx < slot->bitmap.width; ++xx)
        {
          int eff_x = (x + xx + x_off);

          if(eff_x < 0 || eff_x >= canvas->width)
            continue;

          size_t i = (eff_y * canvas->width + eff_x) * 3;

          unsigned int alpha = slot->bitmap.buffer[yy * slot->bitmap.width + xx];
          unsigned int inv_alpha = 256 - alpha;

          canvas->data[i + 0] = (canvas->data[i + 0] * inv_alpha) >> 8;
          canvas->data[i + 1] = (canvas->data[i + 1] * inv_alpha) >> 8;
          canvas->data[i + 2] = (canvas->data[i + 2] * inv_alpha) >> 8;
        }
      }

      x += slot->advance.x >> 6;

      break;

    case 1:

      for(yy = 0; yy < slot->bitmap.rows; ++yy)
      {
        int eff_x = (x - yy - y_off);

        if(eff_x < 0 || eff_x >= canvas->width)
          continue;

        for(xx = 0; xx < slot->bitmap.width; ++xx)
        {
          int eff_y = (y + xx + x_off);

          if(eff_y < 0 || eff_y >= canvas->height)
            continue;


          size_t i = (eff_y * canvas->width + eff_x) * 3;

          unsigned int alpha = slot->bitmap.buffer[yy * slot->bitmap.width + xx];
          unsigned int inv_alpha = 256 - alpha;

          canvas->data[i + 0] = (canvas->data[i + 0] * inv_alpha) >> 8;
          canvas->data[i + 1] = (canvas->data[i + 1] * inv_alpha) >> 8;
          canvas->data[i + 2] = (canvas->data[i + 2] * inv_alpha) >> 8;
        }
      }

      y += slot->advance.x >> 6;

      break;

    case 2:

      for(yy = 0; yy < slot->bitmap.rows; ++yy)
      {
        int eff_x = (x + yy + y_off);

        if(eff_x < 0 || eff_x >= canvas->width)
          continue;

        for(xx = 0; xx < slot->bitmap.width; ++xx)
        {
          int eff_y = (y - xx - x_off);

          if(eff_y < 0 || eff_y >= canvas->height)
            continue;

          size_t i = (eff_y * canvas->width + eff_x) * 3;

          unsigned int alpha = slot->bitmap.buffer[yy * slot->bitmap.width + xx];
          unsigned int inv_alpha = 256 - alpha;

          canvas->data[i + 0] = (canvas->data[i + 0] * inv_alpha) >> 8;
          canvas->data[i + 1] = (canvas->data[i + 1] * inv_alpha) >> 8;
          canvas->data[i + 2] = (canvas->data[i + 2] * inv_alpha) >> 8;
        }
      }

      y -= slot->advance.x >> 6;

      break;
    }
  }
}
