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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rrd.h"

void
rrd_parse(struct rrd* result, const char* filename)
{
  FILE* f;
  size_t i, data_size = 0;

  f = fopen(filename, "r");

  if(!f)
    errx(EXIT_FAILURE, "Failed to open '%s': %s", filename, strerror(errno));

  if(1 != fread(&result->header, sizeof(result->header), 1, f))
    errx(EXIT_FAILURE, "Error reading header from '%s': %s", filename, strerror(errno));

  if(memcmp("RRD", result->header.cookie, 4))
    errx(EXIT_FAILURE, "Incorrect cookie in '%s'", filename);

  int version = (result->header.version[0] - '0') * 1000
              + (result->header.version[1] - '0') * 100
              + (result->header.version[2] - '0') * 10
              + (result->header.version[3] - '0');

  if(version < 1 || version > 3)
    errx(EXIT_FAILURE, "Unsupported RRD version in '%s'", filename);

  if(result->header.float_cookie != 8.642135E130)
    errx(EXIT_FAILURE, "Floating point sanity test failed for '%s'", filename);

  result->ds_defs = malloc(sizeof(struct ds_def) * result->header.ds_count);

  if(result->header.ds_count != fread(result->ds_defs, sizeof(struct ds_def), result->header.ds_count, f))
    errx(EXIT_FAILURE, "Error reading data source definitions from '%s': %s", filename, strerror(errno));

  for(i = 0; i < result->header.ds_count; ++i)
  {
    if(result->ds_defs[i].ds_name[19] || result->ds_defs[i].dst[19])
      errx(EXIT_FAILURE, "Missing NUL-termination in data source definition strings in '%s'", filename);
  }

  result->rra_defs = malloc(sizeof(struct rra_def) * result->header.rra_count);

  if(result->header.rra_count != fread(result->rra_defs, sizeof(struct rra_def), result->header.rra_count, f))
    errx(EXIT_FAILURE, "Error reading rr-archive definitions from '%s': %s", filename, strerror(errno));

  for(i = 0; i < result->header.rra_count; ++i)
  {
    if(result->rra_defs[i].cf_name[19])
      errx(EXIT_FAILURE, "Missing NUL-termination in rr-archive definition string in '%s'", filename);
  }

  if(version >= 3)
  {
    if(1 != fread(&result->live_header, sizeof(result->live_header), 1, f))
      errx(EXIT_FAILURE, "Error reading live header from '%s': %s", filename, strerror(errno));
  }
  else
  {
    time_t val;

    if(1 != fread(&val, sizeof(val), 1, f))
      errx(EXIT_FAILURE, "Error reading live header from '%s': %s", filename, strerror(errno));

    result->live_header.last_up = val;
    result->live_header.last_up_usec = 0;
  }

  result->pdp_preps = malloc(sizeof(struct pdp_prepare) * result->header.ds_count);

  if(result->header.ds_count != fread(result->pdp_preps, sizeof(struct pdp_prepare), result->header.ds_count, f))
    errx(EXIT_FAILURE, "Error reading PDP preps from '%s': %s", filename, strerror(errno));

  i = result->header.ds_count * result->header.rra_count;
  result->cdp_preps = malloc(sizeof(struct cdp_prepare) * i);

  if(i != fread(result->cdp_preps, sizeof(struct cdp_prepare), i, f))
    errx(EXIT_FAILURE, "Error reading CDP preps from '%s': %s", filename, strerror(errno));

  result->rra_ptrs = malloc(sizeof(*result->rra_ptrs) * result->header.rra_count);

  if(result->header.rra_count != fread(result->rra_ptrs, sizeof(*result->rra_ptrs), result->header.rra_count, f))
    errx(EXIT_FAILURE, "Error reading RRA pointers from '%s': %s", filename, strerror(errno));

  for(i = 0; i < result->header.rra_count; ++i)
    data_size += result->rra_defs[i].row_count * result->header.ds_count;

  result->values = malloc(sizeof(double) * data_size);

  if(data_size != fread(result->values, sizeof(double), data_size, f))
    errx(EXIT_FAILURE, "Error reading %zu values from '%s': %s", data_size, filename, strerror(errno));

  fclose(f);
}

void
rrd_free(struct rrd* data)
{
  free(data->ds_defs);
  free(data->rra_defs);
  free(data->pdp_preps);
  free(data->cdp_preps);
  free(data->rra_ptrs);
  free(data->values);
}