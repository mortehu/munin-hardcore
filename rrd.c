/*  RRD parser for munin-hardcore.
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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "rrd.h"

int
rrd_parse(struct rrd* result, const char* filename)
{
  off_t file_size;
  size_t i, data_size = 0;
  void* data;
  unsigned char* input;
  unsigned char* end;
  int fd;

  /* To facilitate free-ing of incompletely loaded RRDs */
  memset(result, 0, sizeof(struct rrd));

  if(-1 == (fd = open(filename, O_RDONLY)))
  {
    /* Silently ignore ENOENT like munin-graph does */
    if(errno == ENOENT)
      return -1;

    fprintf(stderr, "Failed to open '%s': %s\n", filename, strerror(errno));

    return -1;
  }

  if(-1 == (file_size = lseek(fd, 0, SEEK_END)))
  {
    fprintf(stderr, "Seek failed on '%s': %s\n", filename, strerror(errno));

    close(fd);

    return -1;
  }

  data = mmap(0, file_size,  PROT_READ, MAP_SHARED, fd, 0);
  madvise(data, file_size, MADV_WILLNEED);

  close(fd);

  if(data == MAP_FAILED)
  {
    fprintf(stderr, "Memory map failed on '%s': %s\n", filename, strerror(errno));

    return -1;
  }

  input = (unsigned char*) data;
  end = input + file_size;

#define test_end(offset, label) \
  if((offset) > end)            \
  {                             \
    fprintf(stderr, "Unexpected end-of-file in %s in RRD file '%s'\n", label, filename); \
    goto fail;                  \
  }

  test_end(input + sizeof(result->header), "header");

  memcpy(&result->header, data, sizeof(result->header));
  input += sizeof(result->header);

  if(memcmp("RRD", result->header.cookie, 4))
  {
    fprintf(stderr, "Incorrect cookie in '%s'.  Expected \"RRD\" at offset 0\n", filename);

    goto fail;
  }

  int version = (result->header.version[0] - '0') * 1000
              + (result->header.version[1] - '0') * 100
              + (result->header.version[2] - '0') * 10
              + (result->header.version[3] - '0');

  if(version < 1 || version > 3)
  {
    fprintf(stderr, "Unsupported RRD version %d in '%s'.  Version 1, 2 and 3 supported.\n", version, filename);

    goto fail;
  }

  if(result->header.float_cookie != 8.642135E130)
  {
    fprintf(stderr, "Floating point sanity test failed for '%s'\n", filename);

    goto fail;
  }

  result->ds_defs = (void*) input;
  input += sizeof(struct ds_def) * result->header.ds_count;
  test_end(input, "data source definitions");

  for(i = 0; i < result->header.ds_count; ++i)
  {
    if(result->ds_defs[i].ds_name[19] || result->ds_defs[i].dst[19])
    {
      fprintf(stderr, "Missing NUL-termination in data source definition strings in '%s'\n", filename);

      goto fail;
    }
  }

  result->rra_defs = (void*) input;
  input += sizeof(struct rra_def) * result->header.rra_count;
  test_end(input, "RRA definitions");

  for(i = 0; i < result->header.rra_count; ++i)
  {
    if(result->rra_defs[i].cf_name[19])
    {
      fprintf(stderr, "Missing NUL-termination in rr-archive definition string in '%s'\n", filename);

      goto fail;
    }
  }

  if(version >= 3)
  {
    test_end(input + sizeof(result->live_header), "live header");

    memcpy(&result->live_header, input, sizeof(result->live_header));
    input += sizeof(result->live_header);
  }
  else
  {
    time_t val;

    test_end(input + sizeof(val), "live header");

    memcpy(&val, input, sizeof(val));
    input += sizeof(val);

    result->live_header.last_up = val;
    result->live_header.last_up_usec = 0;
  }

  result->pdp_preps = (void*) input;
  input += sizeof(*result->pdp_preps) * result->header.ds_count;
  test_end(input, "PDP prepares");

  i = result->header.ds_count * result->header.rra_count;

  result->cdp_preps = (void*) input;
  input += sizeof(*result->cdp_preps) * i;
  test_end(input, "CDP prepares");

  result->rra_ptrs = (void*) input;
  input += sizeof(*result->rra_ptrs) * result->header.rra_count;
  test_end(input, "RRA pointers");

  for(i = 0; i < result->header.rra_count; ++i)
    data_size += result->rra_defs[i].row_count * result->header.ds_count;

  result->values = (void*) input;
  input += data_size * sizeof(*result->values);

  test_end(input, "value list");

  if(input != end)
  {
    fprintf(stderr, "Unexpected file size for '%s': Got %zu, but expected %zu\n",
            filename, (end - (unsigned char*) data), (input - (unsigned char*) data));

    goto fail;
  }

  result->data = data;
  result->file_size = file_size;

  return 0;

fail:

  munmap(data, file_size);
  memset(result, 0, sizeof(struct rrd));

  return -1;
}

void
rrd_free(struct rrd* data)
{
  if(data->file_size)
    munmap(data->data, data->file_size);

  memset(data, 0, sizeof(struct rrd));
}

int
rrd_iterator_create(struct rrd_iterator* result, const struct rrd* data,
                    const char* cf_name, size_t interval,
                    size_t max_count)
{
  size_t rra;
  size_t offset = 0;

  interval /= data->header.pdp_step;

  for(rra = 0; rra < data->header.rra_count; ++rra)
  {
    if(data->rra_defs[rra].pdp_count == interval
    && !strcmp(data->rra_defs[rra].cf_name, cf_name))
    {
      result->ds = 0;
      result->values = data->values;
      result->offset = offset;
      result->count = data->rra_defs[rra].row_count;
      result->first = (data->rra_ptrs[rra] + 1) % result->count;
      result->step = data->header.ds_count;

      if(result->count > max_count)
        result->current_position = result->count - max_count;
      else
        result->current_position = 0;

      return 0;
    }

    offset += data->rra_defs[rra].row_count * data->header.ds_count;
  }

  return -1;
}
