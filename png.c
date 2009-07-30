#include <alloca.h>
#include <png.h>

static void
png_error_fn(png_structp png, png_const_charp message)
{
}

static void
png_warning_fn(png_structp png, png_const_charp message)
{
}

int
write_png(const char *file_name, size_t width, size_t height, unsigned char* data)
{
  FILE *fp;
  size_t i;

  png_structp png_ptr;
  png_infop info_ptr;
  png_bytepp row_pointers;

  png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);

  if(!png_ptr)
    return - 1;

  info_ptr = png_create_info_struct(png_ptr);

  if(!info_ptr)
  {
    png_destroy_write_struct(&png_ptr,  png_infopp_NULL);

    return - 1;
  }

  fp = fopen(file_name, "wb");

  if(!fp)
  {
    png_destroy_write_struct(&png_ptr, &info_ptr);

    return - 1;
  }

  row_pointers = alloca(height * sizeof(png_bytep));

  for(i = 0; i < height; ++i)
    row_pointers[i] = data + i * width * 3;

  png_init_io(png_ptr, fp);

  png_set_IHDR(png_ptr, info_ptr, width, height,
      8, PNG_COLOR_TYPE_RGB, PNG_INTERLACE_NONE,
      PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

  png_set_compression_level(png_ptr, Z_BEST_SPEED);

  png_set_rows(png_ptr, info_ptr, (png_byte**) row_pointers);
  png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, row_pointers);
  png_write_end(png_ptr, info_ptr);

  png_destroy_write_struct(&png_ptr, &info_ptr);

  fclose(fp);

  return 0;
}
