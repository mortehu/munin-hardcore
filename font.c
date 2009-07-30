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

void
font_draw(struct canvas* canvas, size_t x, size_t y, const char* text, int direction)
{
  unsigned char alpha_image[1024];
  unsigned int stride;
  int result;
  FT_UInt idx;
  FT_GlyphSlot slot = 0;
  size_t yy, xx;

  while(*text)
  {
    idx = FT_Get_Char_Index(ft_face, *text);

    if(!idx)
      return;

    result = FT_Load_Glyph(ft_face, idx, FT_LOAD_RENDER);

    if(result)
      return;

    slot = ft_face->glyph;

    int x_off = slot->metrics.horiBearingX >> 6;
    int y_off = -(slot->metrics.horiBearingY >> 6) + (ft_face->descender >> 8) - 1;

    switch(direction)
    {
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

    ++text;
  }
}
