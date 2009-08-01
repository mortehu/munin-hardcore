#include <err.h>
#include <math.h>
#include <wchar.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_CACHE_H

#include "types.h"

FT_Library       ft_library;
FT_Face          ft_face;
FTC_Manager      ft_cache_mgr;
FTC_CMapCache    ft_cmap_cache;
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

  if(0 != (result = FTC_CMapCache_New(ft_cache_mgr, &ft_cmap_cache)))
    errx(EXIT_FAILURE, "Failed to create font character map cache: %d", result);

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

    idx = FTC_CMapCache_Lookup(ft_cmap_cache, 0, 0, ch);

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

    while(n-- && *text)
      ch <<= 6, ch |= (*text++ & 0x3F);

    idx = FTC_CMapCache_Lookup(ft_cmap_cache, 0, 0, ch);

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