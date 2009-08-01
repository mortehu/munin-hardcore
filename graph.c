#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#include "types.h"

#define ANTI_ALIASING 1

int
write_png(const char *file_name, size_t width, size_t height, unsigned char* data);

void
font_init();

size_t
font_width(const char* text);

void
font_draw(struct canvas* canvas, size_t x, size_t y, const char* text, int direction);

void
do_graph(struct graph* g, size_t interval, const char* suffix);

static const uint32_t colors[] =
{
  0x21fb21, 0x0022ff, 0xff0000, 0x00aaaa, 0xff00ff, 0xffa500, 0xcc0000,
  0x0000cc, 0x0080c0, 0x8080c0, 0xff0080, 0x800080, 0x688e23, 0x408080,
  0x808000, 0x000000
};

static struct graph* graphs;
static size_t graph_count;
static size_t graph_alloc;

static const char* tmpldir = "/etc/munin/templates";
static const char* htmldir = "/var/www/munin";
static const char* dbdir = "/var/lib/munin";
static const char* rundir = "/var/run/munin";
static const char* logdir = "/var/log/munin";

#define DATA_FILE "/var/lib/munin/datafile"

#define MAX_DIM 2048
#define LINE_HEIGHT 14

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

  result->rra_ptrs = malloc(sizeof(uint32_t) * result->header.rra_count);

  if(result->header.rra_count != fread(result->rra_ptrs, sizeof(uint32_t), result->header.rra_count, f))
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

size_t
graph_index(const char* domain, const char* host, const char* name)
{
  size_t i;

  for(i = 0; i < graph_count; ++i)
  {
    if(!strcmp(graphs[i].domain, domain)
    && !strcmp(graphs[i].host, host)
    && !strcmp(graphs[i].name, name))
      return i;
  }

  if(graph_count == graph_alloc)
  {
    graph_alloc = graph_alloc * 3 / 2 + 32;
    graphs = realloc(graphs, sizeof(struct graph) * graph_alloc);

    if(!graphs)
      errx(EXIT_FAILURE, "Memory allocation failed: %s", strerror(errno));
  }

  memset(&graphs[i], 0, sizeof(struct graph));
  graphs[i].domain = domain;
  graphs[i].host = host;
  graphs[i].name = name;
  ++graph_count;

  return i;
}

size_t
curve_index(struct graph* graph, const char* name)
{
  size_t i;

  for(i = 0; i < graph->curve_count; ++i)
  {
    if(!strcmp(graph->curves[i].name, name))
      return i;
  }

  if(graph->curve_count == graph->curve_alloc)
  {
    graph->curve_alloc = graph->curve_alloc * 3 / 2 + 16;
    graph->curves = realloc(graph->curves, sizeof(struct curve) * graph->curve_alloc);

    if(!graph->curves)
      errx(EXIT_FAILURE, "Memory allocation failed: %s", strerror(errno));
  }

  memset(&graph->curves[i], 0, sizeof(struct curve));
  graph->curves[i].name = name;
  ++graph->curve_count;

  return i;
}

static const char* graph_order;

const char*
strword(const char* haystack, const char* needle)
{
  const char* tmp;
  size_t needle_length = strlen(needle);

  tmp = strstr(haystack, needle);

  while(tmp)
  {
    if((tmp == haystack || isspace(tmp[-1]))
    && (tmp[needle_length] == 0 || isspace(tmp[needle_length])))
      return tmp;

    tmp = strstr(tmp + 1, needle);
  }

  return 0;
}

int
curve_name_cmp(const void* plhs, const void* prhs)
{
  const struct curve* lhs = plhs;
  const struct curve* rhs = prhs;

  const char* a = strword(graph_order, lhs->name);
  const char* b = strword(graph_order, rhs->name);

  if(a)
  {
    if(!b)
      return -1;
    else
      return a - b;
  }
  else if(b)
    return 1;

  return strcmp(lhs->name, rhs->name);
}

int
main(int argc, char** argv)
{
  FILE* f;
  size_t data_size;
  char* data;
  char* in;
  char* line_end;
  size_t lineno = 2;
  size_t graph, curve;

  font_init();

  if(!(f = fopen(DATA_FILE, "r")))
    errx(EXIT_FAILURE, "Failed to open '%s' for reading: %s", DATA_FILE, strerror(errno));

  if(-1 == (fseek(f, 0, SEEK_END)))
    errx(EXIT_FAILURE, "Failed to seek to end of '%s': %s", DATA_FILE, strerror(errno));

  data_size = ftell(f);

  if(-1 == (fseek(f, 0, SEEK_SET)))
    errx(EXIT_FAILURE, "Failed to seek to start of '%s': %s", DATA_FILE, strerror(errno));

  data = malloc(data_size + 1);

  if(data_size != fread(data, 1, data_size, f))
    errx(EXIT_FAILURE, "Error reading %zu bytes from '%s': %s", (size_t) data_size, DATA_FILE, strerror(errno));

  fclose(f);

  data[data_size] = 0;

  in = data;
  line_end = strchr(in, '\n');

  if(!line_end)
    errx(EXIT_FAILURE, "No newlines in '%s'", DATA_FILE);

  unsigned int ver_major, ver_minor, ver_patch;

  if(3 != sscanf(in, "version %u.%u.%u\n", &ver_major, &ver_minor, &ver_patch))
    errx(EXIT_FAILURE, "Unsupported version signature at start of '%s'", DATA_FILE);

  if(ver_major != 1 || ver_minor != 2)
    errx(EXIT_FAILURE, "Unsupported version %u.%u.  I only support 1.2", ver_major, ver_minor);

  in = line_end + 1;

  while(*in)
  {
    char* key_start;
    char* key_end;
    char* value_start;
    char* domain_end;

    key_start = in;

    while(isspace(*key_start))
      ++key_start;

    if(!*key_start)
      break;

    line_end = key_start + 1;

    while(*line_end && *line_end != '\n')
      ++line_end;

    *line_end = 0;

    key_end = strchr(key_start, ' ');

    if(!key_end)
      errx(EXIT_FAILURE, "Parse error at line %zu in '%s'.  Did not find a SPACE character", lineno, DATA_FILE);

    value_start = key_end + 1;
    *key_end = 0;

    while(isspace(*value_start))
      ++value_start;

    if(!strcmp(key_start, "tmpldir"))
    {
      tmpldir = value_start;
    }
    else if(!strcmp(key_start, "htmldir"))
    {
      htmldir = value_start;
    }
    else if(!strcmp(key_start, "dbdir"))
    {
      dbdir = value_start;
    }
    else if(!strcmp(key_start, "rundir"))
    {
      rundir = value_start;
    }
    else if(!strcmp(key_start, "logdir"))
    {
      logdir = value_start;
    }
    else if(0 != (domain_end = strchr(key_start, ';')))
    {
      char* host_start;
      char* host_end;
      char* graph_start;
      char* graph_end;
      char* graph_key;

      host_start = domain_end + 1;
      *domain_end = 0;

      host_end = strchr(host_start, ':');

      if(!host_end)
        errx(EXIT_FAILURE, "Parse error at line %zu in '%s'.  Did not find a : character after host name", lineno, DATA_FILE);

      graph_start = host_end + 1;
      *host_end = 0;

      if(!strcmp(graph_start, "use_node_name"))
      {
      }
      else if(!strcmp(graph_start, "address"))
      {
      }
      else if(0 != (graph_end = strchr(graph_start, '.')))
      {
        if(!graph_end)
          errx(EXIT_FAILURE, "Parse error at line %zu in '%s'.  Did not find a . character after graph name", lineno, DATA_FILE);

        graph_key = graph_end + 1;
        *graph_end = 0;

        graph = graph_index(key_start, host_start, graph_start);

        if(!strcmp(graph_key, "graph_args"))
          graphs[graph].args = value_start;
        else if(!strcmp(graph_key, "graph_vlabel"))
          graphs[graph].vlabel = value_start;
        else if(!strcmp(graph_key, "graph_title"))
          graphs[graph].title = value_start;
        else if(!strcmp(graph_key, "graph_order"))
          graphs[graph].order = value_start;
        else if(!strcmp(graph_key, "graph_category"))
          graphs[graph].category = value_start;
        else if(!strcmp(graph_key, "graph_info"))
          graphs[graph].info = value_start;
        else if(!strcmp(graph_key, "graph_scale"))
          graphs[graph].scale = value_start;
        else if(!strcmp(graph_key, "graph_height"))
          graphs[graph].height = strtol(value_start, 0, 0);
        else if(!strcmp(graph_key, "graph_width"))
          graphs[graph].width = strtol(value_start, 0, 0);
        else if(!strcmp(graph_key, "graph_period"))
          graphs[graph].period = value_start;
        else if(!strcmp(graph_key, "graph_total"))
          graphs[graph].total = value_start;
        else if(strchr(graph_key, '.'))
        {
          char* curve_start;

          curve_start = graph_key;
          graph_key = strchr(curve_start, '.');

          *graph_key++ = 0;

          curve = curve_index(&graphs[graph], curve_start);

          if(!strcmp(graph_key, "label"))
            graphs[graph].curves[curve].label = value_start;
          else if(!strcmp(graph_key, "draw"))
            graphs[graph].curves[curve].draw = value_start;
          else if(!strcmp(graph_key, "graph"))
            graphs[graph].curves[curve].nograph = !strcasecmp(value_start, "no");
          else if(!strcmp(graph_key, "type"))
            graphs[graph].curves[curve].type = value_start;
          else if(!strcmp(graph_key, "info"))
            graphs[graph].curves[curve].info = value_start;
          else if(!strcmp(graph_key, "cdef"))
            graphs[graph].curves[curve].cdef = value_start;
          else if(!strcmp(graph_key, "negative"))
            graphs[graph].curves[curve].negative = value_start;
          else if(!strcmp(graph_key, "max"))
            graphs[graph].curves[curve].max = strtod(value_start, 0);
          else if(!strcmp(graph_key, "min"))
            graphs[graph].curves[curve].min = strtod(value_start, 0);
          else if(!strcmp(graph_key, "warning") || !strcmp(graph_key, "warn"))
            graphs[graph].curves[curve].warning = strtod(value_start, 0);
          else if(!strcmp(graph_key, "critical"))
            graphs[graph].curves[curve].critical = strtod(value_start, 0);
          else
          {
            fprintf(stderr, "Mediator: %s  Host: %s  Graph: %s  Curve: %s  Key: %s  Value: %s\n", key_start, host_start, graph_start, curve_start, graph_key, value_start);
          }
        }
        else
          fprintf(stderr, "Skipping unknown graph key '%s'\n", graph_key);
      }
    }
    else
      fprintf(stderr, "Skipping unknown key '%s'\n", key_start);

    in = line_end + 1;
    ++lineno;
  }

  for(graph = 0; graph < graph_count; ++graph)
  {
    struct graph* g = &graphs[graph];

    for(curve = 0; curve < g->curve_count; ++curve)
    {
      struct curve* c = &g->curves[curve];

      int suffix;

      if(!c->type || !strcasecmp(c->type, "gauge"))
        suffix = 'g';
      else if(!strcasecmp(c->type, "derive"))
        suffix = 'd';
      else if(!strcasecmp(c->type, "counter"))
        suffix = 'c';
      else
        errx(EXIT_FAILURE, "Unknown curve type '%s'", c->type);

      if(-1 == asprintf(&c->path, "%s/%s/%s-%s-%s-%c.rrd", dbdir, g->domain, g->host, g->name, c->name, suffix))
        errx(EXIT_FAILURE, "asprintf failed while building RRD path: %s", strerror(errno));

      if(-1 == access(c->path, R_OK))
      {
        fprintf(stderr, "Don't have %s\n", c->path);
        goto no_graph;
      }

      rrd_parse(&c->data, c->path);
    }

    graph_order = g->order;
    qsort(g->curves, g->curve_count, sizeof(struct curve), curve_name_cmp);

    do_graph(g, 300, "day");
    do_graph(g, 1800, "week");
    do_graph(g, 7200, "month");
    do_graph(g, 86400, "year");

no_graph:

    for(curve = 0; curve < g->curve_count; ++curve)
      rrd_free(&g->curves[curve].data);

    free(g->curves);
  }

  free(graphs);
  free(data);

  return EXIT_SUCCESS;
}

struct curve_args
{
  double cur, max, min, avg;
  double max_avg, min_avg;
  int rra_min, rra_max, rra_avg;
  size_t rra_min_offset, rra_max_offset, rra_avg_offset;
  size_t negative;
};

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

#define PLOT_NEGATIVE 0x0001

void
plot_gauge(struct canvas* canvas,
           struct curve* c, struct curve_args* ca,
           size_t graph_x, size_t graph_y,
           size_t width, size_t height,
           double min, double max, size_t ds,
           uint32_t color, unsigned int flags)
{
  int x = 0, y, prev_y = -1;

  size_t i;

  struct rrd* data;
  size_t data_offset, rra;
  size_t row_count, skip;

  data = &c->data;
  rra = ca->rra_avg;
  data_offset = ca->rra_avg_offset;
  row_count = data->rra_defs[rra].row_count;

  skip = row_count - width + 1;

  for(i = 0; i < row_count && x < width; ++i, ++x)
  {
    double value = data->values[data_offset + ((i + skip + data->rra_ptrs[rra]) % row_count) * data->header.ds_count + ds];

    if(isnan(value))
    {
      prev_y = -1;

      continue;
    }

    if(flags & PLOT_NEGATIVE)
      value = -value;

    y = height - (value - min) * (height - 1) / (max - min) - 1;

    if(prev_y != -1)
      draw_line(canvas, graph_x + x - 1, graph_y + prev_y, graph_x + x, graph_y + y, color);
    else
      canvas->data[((graph_y + y) * width + (graph_x + x)) * 3] = color;

    prev_y = y;
  }
}

void
plot_min_max(struct canvas* canvas,
             struct curve* c, struct curve_args* ca,
             size_t graph_x, size_t graph_y,
             size_t width, size_t height,
             double min, double max, size_t ds,
             uint32_t color, unsigned int flags)
{
  size_t i, x = 0;

  struct rrd* data;

  size_t min_rra, max_rra;
  size_t min_row_count, max_row_count;
  size_t min_skip, max_skip;

  data = &c->data;

  min_rra = ca->rra_min;
  max_rra = ca->rra_max;

  min_row_count = data->rra_defs[min_rra].row_count;
  max_row_count = data->rra_defs[max_rra].row_count;

  min_skip = min_row_count - width + 1;
  max_skip = max_row_count - width + 1;

  for(i = 0; i < min_row_count && i < max_row_count && x < width; ++i, ++x)
  {
    double min_value = data->values[ca->rra_min_offset + ((i + min_skip + data->rra_ptrs[min_rra]) % min_row_count) * data->header.ds_count + ds];
    double max_value = data->values[ca->rra_max_offset + ((i + max_skip + data->rra_ptrs[max_rra]) % max_row_count) * data->header.ds_count + ds];

    if(isnan(min) || isnan(max))
      continue;

    if(flags & PLOT_NEGATIVE)
    {
      min_value = -min_value;
      max_value = -max_value;
    }

    size_t y0 = height - (min_value - min) * (height - 1) / (max - min) - 1;
    size_t y1 = height - (max_value - min) * (height - 1) / (max - min) - 1;

    draw_vline(canvas, graph_x + x, graph_y + y0, graph_y + y1, color);
  }
}

void
plot_area(struct canvas* canvas,
          struct curve* c, struct curve_args* ca,
          double* maxs,
          size_t graph_x, size_t graph_y,
          size_t width, size_t height,
          double min, double max, size_t ds,
          uint32_t color)
{
  int x = 0, y0, y1;

  size_t i;

  struct rrd* data;
  size_t data_offset, rra;
  size_t row_count, skip;

  data = &c->data;
  rra = ca->rra_avg;
  data_offset = ca->rra_avg_offset;
  row_count = data->rra_defs[rra].row_count;

  skip = row_count - width + 1;

  for(i = 0; i < row_count && x < width; ++i, ++x)
  {
    double value = data->values[data_offset + ((i + skip + data->rra_ptrs[rra]) % row_count) * data->header.ds_count + ds];

    if(isnan(value))
      continue;

    y0 = height - (maxs[x] - min) * (height - 1) / (max - min) - 1;
    y1 = height - ((value + maxs[x]) - min) * (height - 1) / (max - min) - 1;

    draw_vline(canvas, graph_x + x, graph_y + y0, graph_y + y1, color);

    maxs[x] += value;
  }
}

double
calc_step_size(double range, size_t graph_height)
{
  const unsigned int factors[] = { 1, 2, 5 };
  unsigned int i;

  double min_step = range / (graph_height / 14.0);
  double mag = pow(10.0, floor(log(min_step) / log(10.0)));

  for(i = 0; i < 3; ++i)
    if(mag * factors[i] >= min_step)
      return mag * factors[i];

  return mag * 10;
}

void
number_format_args(double number, const char** format, const char** suffix, double* scale)
{
  static const char* fmt[] = { "%.2f%s", "%.1f%s", "%.0f%s" };

  int mag, rad;

  if(!number)
  {
    *format = "%.0f";
    *suffix = "";
    *scale = 1.0;

    return;
  }

  if(fabs(number) < 1)
  {
    static const char* suffixes[] = { "m", "µ", "n", "p", "f", "a", "z", "y" };

    mag = -floor(log(fabs(number)) / log(10) + 1);
    rad = 2 - (mag % 3);
    mag /= 3;

    if(mag > sizeof(suffixes) / sizeof(suffixes[0]) - 1)
      mag = sizeof(suffixes) / sizeof(suffixes[0]) - 1;

    *format = fmt[rad];
    *suffix = suffixes[mag];
    *scale = pow(1000, mag + 1);
  }
  else
  {
    mag = floor(log(fabs(number)) / log(10));
    rad = mag % 3;
    mag /= 3;

    if(!mag)
    {
      *format = fmt[rad];
      *suffix = "";
      *scale = 1.0;
    }
    else
    {
      static const char* suffixes[] = { "k", "M", "G", "T", "P", "E", "Z", "Y" };

      if(mag > sizeof(suffixes) / sizeof(suffixes[0]))
        mag = sizeof(suffixes) / sizeof(suffixes[0]);

      *format = fmt[rad];
      *scale = pow(1000.0, -mag);
      *suffix = suffixes[mag - 1];
    }
  }
}

void
format_number(char* target, double number)
{
  const char* format;
  const char* suffix;
  double scale;

  number_format_args(number, &format, &suffix, &scale);

  sprintf(target, format, number * scale, suffix);
}

void
print_number(struct canvas* canvas, size_t x, size_t y, double val)
{
  char buf[64];

  format_number(buf, val);
  font_draw(canvas, x, y, buf, -1);
}

void
print_numbers(struct canvas* canvas, size_t x, size_t y, double neg, double pos)
{
  char buf[64];

  format_number(buf, neg);
  strcat(buf, "/");
  format_number(strchr(buf, 0), pos);
  font_draw(canvas, x, y, buf, -1);
}

struct time_args
{
  const char* format;
  time_t bias;
  time_t label_interval;
  time_t bar_interval;
};

#define INTERVAL_MONTH -1

const struct time_args time_args[] =
{
  { "%a %H:%M", 0, 43200, 3600 },
  { "%d", 0, 86400, 21600 },
  { "Week %V", 345600, 86400 * 7, 86400 },
  { "%b", 0, INTERVAL_MONTH, 0 },
};

void
do_graph(struct graph* g, size_t interval, const char* suffix)
{
  const struct time_args* ta;
  size_t x, y, width;
  time_t last_update = 0;
  size_t i, curve, ds = 0;
  int j;

  int has_negative = 0, draw_min_max = 0;

  fprintf(stderr, "%s %s %s\n", g->host, g->name, suffix);

  for(i = 0; i + 1 < sizeof(time_args) / sizeof(time_args[0]); ++i)
  {
    if(time_args[i].bar_interval > interval * 10)
      break;
  }

  ta = &time_args[i];

  size_t graph_width, graph_height;
  size_t graph_x = 60, graph_y = 30;

  graph_width = g->width ? g->width : 400;
  graph_height = g->height ? g->height : 175;

  double min = 0, max = 0;
  double* maxs = alloca(sizeof(double) * graph_width);
  memset(maxs, 0, sizeof(double) * graph_width);

  size_t visible_graph_count = 0;

  struct curve_args* cas = alloca(sizeof(struct curve_args) * g->curve_count);

  for(curve = 0; curve < g->curve_count; ++curve)
  {
    struct curve* c = &g->curves[curve];
    struct curve_args* ca = &cas[curve];
    size_t rra, offset = 0;
    int area = 0;

    if(c->data.live_header.last_up > last_update)
      last_update = c->data.live_header.last_up;

    ca->rra_min = ca->rra_max = ca->rra_avg = -1;

    for(rra = 0; rra < c->data.header.rra_count; ++rra)
    {
      if(c->data.rra_defs[rra].pdp_count != interval / c->data.header.pdp_step)
      {
        offset += c->data.rra_defs[rra].row_count * c->data.header.ds_count;

        continue;
      }

      if(!strcmp(c->data.rra_defs[rra].cf_name, "MAX"))
      {
        ca->rra_max = rra;
        ca->rra_max_offset = offset;
      }
      else if(!strcmp(c->data.rra_defs[rra].cf_name, "MIN"))
      {
        ca->rra_min = rra;
        ca->rra_min_offset = offset;
      }
      else if(!strcmp(c->data.rra_defs[rra].cf_name, "AVERAGE"))
      {
        ca->rra_avg = rra;
        ca->rra_avg_offset = offset;
      }

      offset += c->data.rra_defs[rra].row_count * c->data.header.ds_count;
    }

    if(ca->rra_min == -1 || ca->rra_max == -1 || ca->rra_avg == -1)
      errx(EXIT_FAILURE, "Did not find all required rr-archivess in '%s'", c->path);


    if(!c->nograph && c->draw)
    {
      if(!strcasecmp(c->draw, "area"))
      {
        memset(maxs, 0, sizeof(double) * graph_width);
        area = 1;
      }
      else if(!strcasecmp(c->draw, "stack"))
        area = 1;
    }

    ca->cur = c->data.values[ca->rra_avg_offset + c->data.rra_ptrs[ca->rra_avg] * c->data.header.ds_count];
    ca->max_avg = ca->cur;
    ca->min_avg = ca->cur;
    ca->min = ca->cur;
    ca->max = ca->cur;
    ca->avg = 0.0;

    size_t avg_skip = c->data.rra_defs[ca->rra_avg].row_count - graph_width;
    size_t min_skip = c->data.rra_defs[ca->rra_min].row_count - graph_width;
    size_t max_skip = c->data.rra_defs[ca->rra_max].row_count - graph_width;

    for(i = 0, x = 0; i < c->data.rra_defs[ca->rra_avg].row_count && x < graph_width; ++i, ++x)
    {
      double avg_value = c->data.values[ca->rra_avg_offset + ((i + avg_skip + c->data.rra_ptrs[ca->rra_avg]) % c->data.rra_defs[ca->rra_avg].row_count) * c->data.header.ds_count + ds];
      double min_value = c->data.values[ca->rra_min_offset + ((i + min_skip + c->data.rra_ptrs[ca->rra_min]) % c->data.rra_defs[ca->rra_min].row_count) * c->data.header.ds_count + ds];
      double max_value = c->data.values[ca->rra_max_offset + ((i + max_skip + c->data.rra_ptrs[ca->rra_max]) % c->data.rra_defs[ca->rra_max].row_count) * c->data.header.ds_count + ds];

      if(isnan(avg_value))
        continue;

      if(area)
      {
        maxs[x] += avg_value;

        if(maxs[x] > max)
          max = maxs[x];
      }

      ca->avg += avg_value;

      if(avg_value > ca->max_avg)
        ca->max_avg = avg_value;
      else if(avg_value < ca->min_avg)
        ca->min_avg = avg_value;

      if(max_value > ca->max)
        ca->max = max_value;

      if(min_value < ca->min)
        ca->min = min_value;
    }

    ca->avg /= c->data.rra_defs[ca->rra_avg].row_count;

    if(!c->nograph)
      ++visible_graph_count;

    if(c->negative)
    {
      has_negative = 1;

      for(i = 0; i < g->curve_count; ++i)
      {
        if(!strcmp(g->curves[i].name, c->negative))
          break;
      }

      if(i == g->curve_count)
        errx(EXIT_FAILURE, "Negative '%s' for '%s' not found", c->name, c->negative);

      ca->negative = i;
    }
    else
      ca->negative = (size_t) -1;
  }

  if(visible_graph_count == 1
  && (!g->curves[0].draw || !strcasecmp(g->curves[0].draw, "line2")))
    draw_min_max = 1;

  for(curve = 0; curve < g->curve_count; ++curve)
  {
    struct curve* c = &g->curves[curve];
    struct curve_args* ca = &cas[curve];

    if(c->nograph)
      continue;

    if(draw_min_max)
    {
      if(ca->max > max)
        max = ca->max;

      if(ca->min < min)
        min = ca->min;

      if(c->negative)
      {
        if(-cas[ca->negative].max < min)
          min = -cas[ca->negative].max;

        if(-cas[ca->negative].min > max)
          max = -cas[ca->negative].min;
      }
    }
    else
    {
      if(ca->max_avg > max)
        max = ca->max_avg;

      if(ca->min_avg < min)
        min = ca->min_avg;

      if(c->negative)
      {
        if(-cas[ca->negative].max_avg < min)
          min = -cas[ca->negative].max_avg;

        if(-cas[ca->negative].min_avg > max)
          max = -cas[ca->negative].min_avg;
      }
    }
  }

  struct canvas canvas;

  char buf[256];

  if(graph_width > MAX_DIM || graph_height > MAX_DIM)
    errx(EXIT_FAILURE, "Graph dimensions %zux%zu are too big", graph_width, graph_height);

  canvas.width = graph_width + 95;
  canvas.height = graph_height + 75 + visible_graph_count * LINE_HEIGHT;

  if(g->total)
    canvas.height += LINE_HEIGHT;

  canvas.data = malloc(3 * canvas.width * canvas.height);
  memset(canvas.data, 0xcc, 3 * canvas.width);
  memset(canvas.data + 3 * canvas.width, 0xf5, 3 * canvas.width * (canvas.height - 2));
  memset(canvas.data + (canvas.height - 1) * canvas.width * 3, 0x77, 3 * canvas.width);
  draw_vline(&canvas, 0, 0, canvas.height - 1, 0xcccccc);
  draw_vline(&canvas, canvas.width - 1, 0, canvas.height - 1, 0x777777);

  draw_rect(&canvas, graph_x, graph_y, graph_width, graph_height, 0xffffff);

  double step_size = calc_step_size(max - min, graph_height);
  size_t graph_index = 0;

  if(g->title)
  {
    snprintf(buf, sizeof(buf), "%s - by %s", g->title, suffix);
    buf[sizeof(buf) - 1] = 0;

    width = font_width(buf);
    font_draw(&canvas, (canvas.width - width) / 2, 20, buf, 0);
  }

  {
    const char* format;
    const char* suffix;
    double scale;

    number_format_args((fabs(max) > fabs(min)) ? fabs(max) : fabs(min), &format, &suffix, &scale);

    for(j = min / step_size; j <= max / step_size; ++j)
    {
      y = graph_height - (j * step_size - min) * (graph_height - 1) / (max - min) - 1;

      sprintf(buf, format, (j * step_size) * scale, suffix);

      font_draw(&canvas, graph_x - 5, graph_y + y + 7, buf, -1);

      if(!j)
        continue;

      for(x = 0; x < graph_width; x += 2)
        draw_pixel(&canvas, x + graph_x, y + graph_y, 0xc0c0c0);
    }
  }

  struct tm tm_last_update;
  localtime_r(&last_update, &tm_last_update);

  time_t t = last_update + tm_last_update.tm_gmtoff;
  time_t prev_t = t + interval;

  for(j = 0; j < graph_width; ++j)
  {
    if(ta->label_interval > 0)
    {
      if((prev_t - ta->bias) / ta->label_interval != (t - ta->bias) / ta->label_interval)
      {
        struct tm tm_tmp;

        gmtime_r(&prev_t, &tm_tmp);

        strftime(buf, sizeof(buf), ta->format, &tm_tmp);

        draw_vline(&canvas, graph_x + graph_width - j, graph_y, graph_y + graph_height, 0xffcccc);
        font_draw(&canvas, graph_x + graph_width - j, graph_y + graph_height + LINE_HEIGHT, buf, -2);
      }
      else if(ta->bar_interval && (prev_t - ta->bias) / ta->bar_interval != (t - ta->bias) / ta->bar_interval)
      {
        draw_vline(&canvas, graph_x + graph_width - j, graph_y, graph_y + graph_height, 0xeeeeee);
      }
    }
    else if(ta->label_interval == INTERVAL_MONTH)
    {
      struct tm a, b;

      gmtime_r(&prev_t, &a);
      gmtime_r(&t, &b);

      if(a.tm_mon != b.tm_mon)
      {
        strftime(buf, sizeof(buf), ta->format, &a);

        draw_vline(&canvas, graph_x + graph_width - j, graph_y, graph_y + graph_height, 0xffcccc);
        font_draw(&canvas, graph_x + graph_width - j, graph_y + graph_height + LINE_HEIGHT, buf, -2);
      }
    }

    prev_t = t;
    t -= interval;
  }

  strftime(buf, sizeof(buf), "Last update: %Y-%m-%d %H:%M:%S %Z", &tm_last_update);
  font_draw(&canvas, canvas.width - 5, canvas.height - 3, buf, -1);

  font_draw(&canvas, canvas.width - 15, 5, "Munin Hardcore/Morten Hustveit", 1);

  if(g->vlabel)
  {
    const char* i = g->vlabel;
    char* o = buf;
    char* o_end = buf + sizeof(buf) - 1;

    while(*i && o != o_end)
    {
      if(*i == '$')
      {
        if(!strncmp(i, "${graph_period}", 15))
        {
          if(o + 6 < o_end)
          {
            memcpy(o, "second", 7);
            o += 6;
          }
          i += 15;
        }
        else
          *o++ = *i++;
      }
      else
        *o++ = *i++;
    }

    *o = 0;

    width = font_width(buf);
    font_draw(&canvas, 14, graph_y + graph_height / 2 + width / 2, buf, 2);
  }

  size_t max_label_width = 0;

  y = graph_y + graph_height + 20 + LINE_HEIGHT;
  memset(maxs, 0, sizeof(double) * graph_width);

  if(min != max)
  {
    for(curve = 0; curve < g->curve_count; ++curve)
    {
      struct curve* c = &g->curves[curve];
      struct curve_args* ca = &cas[curve];
      uint32_t color;

      if(c->nograph)
        continue;

      const char* label = c->label ? c->label : c->name;
      size_t label_width = font_width(label);

      if(label_width > max_label_width)
        max_label_width = label_width;

      color = colors[graph_index % (sizeof(colors) / sizeof(colors[0]))];

      if(!c->draw || !strcasecmp(c->draw, "line2"))
      {
        if(draw_min_max)
        {
          plot_min_max(&canvas, c, ca, graph_x, graph_y, graph_width, graph_height, min, max, ds, color, 0);
          plot_gauge(&canvas, c, ca, graph_x, graph_y, graph_width, graph_height, min, max, ds, (color >> 1) & 0x7f7f7f, 0);

          if(c->negative)
          {
            plot_min_max(&canvas, &g->curves[ca->negative], &cas[ca->negative], graph_x, graph_y, graph_width, graph_height, min, max, ds, color, PLOT_NEGATIVE);
            plot_gauge(&canvas, &g->curves[ca->negative], &cas[ca->negative], graph_x, graph_y, graph_width, graph_height, min, max, ds, (color >> 1) & 0x7f7f7f, PLOT_NEGATIVE);
          }
        }
        else
        {
          plot_gauge(&canvas, c, ca, graph_x, graph_y, graph_width, graph_height, min, max, ds, color, 0);

          if(c->negative)
            plot_gauge(&canvas, &g->curves[ca->negative], &cas[ca->negative], graph_x, graph_y, graph_width, graph_height, min, max, ds, color, PLOT_NEGATIVE);
        }

      }
      else if(!strcasecmp(c->draw, "area"))
      {
        memset(maxs, 0, sizeof(double) * graph_width);
        plot_area(&canvas, c, ca, maxs, graph_x, graph_y, graph_width, graph_height, min, max, ds, color);
      }
      else if(!strcasecmp(c->draw, "stack"))
      {
        plot_area(&canvas, c, ca, maxs, graph_x, graph_y, graph_width, graph_height, min, max, ds, color);
      }

      draw_rect(&canvas, 10, y,  6, 6, draw_min_max ? ((color >> 1) & 0x7f7f7f) : color);
      draw_line(&canvas,  9, y - 1, 17, y - 1, 0);
      draw_line(&canvas,  9, y + 6, 17, y + 6, 0);
      draw_vline(&canvas,  9, y, y + 6, 0);
      draw_vline(&canvas, 16, y, y + 6, 0);

      font_draw(&canvas, 22, y + 9, c->label ? c->label : c->name, 0);

      ++graph_index;
      y += LINE_HEIGHT;
    }
  }

  if(g->total)
  {
    size_t label_width = font_width(g->total);

    if(label_width > max_label_width)
      max_label_width = label_width;

    font_draw(&canvas, 22, y + 9, g->total, 0);
  }

  y = graph_y + graph_height + 20;
  x = 22 + max_label_width + 10;

  size_t column_width = (canvas.width - x - 20) / 4;

  font_draw(&canvas, x + column_width * 1, y + 9, has_negative ? "Cur (-/+)" : "Cur", -1);
  font_draw(&canvas, x + column_width * 2, y + 9, has_negative ? "Min (-/+)" : "Min", -1);
  font_draw(&canvas, x + column_width * 3, y + 9, has_negative ? "Avg (-/+)" : "Avg", -1);
  font_draw(&canvas, x + column_width * 4, y + 9, has_negative ? "Max (-/+)" : "Max", -1);
  y += LINE_HEIGHT;

  struct curve_args totals[2];
  memset(&totals, 0, sizeof(totals));

  for(curve = 0; curve < g->curve_count; ++curve)
  {
    struct curve* c = &g->curves[curve];
    struct curve_args* ca = &cas[curve];

    if(c->nograph)
      continue;

    if(c->negative)
    {
      struct curve_args* nca = &cas[ca->negative];

      print_numbers(&canvas, x + column_width * 1, y + 9, nca->cur, ca->cur);
      print_numbers(&canvas, x + column_width * 2, y + 9, nca->min, ca->min);
      print_numbers(&canvas, x + column_width * 3, y + 9, nca->avg, ca->avg);
      print_numbers(&canvas, x + column_width * 4, y + 9, nca->max, ca->max);

      totals[1].cur += nca->cur;
      totals[1].min += nca->min;
      totals[1].avg += nca->avg;
      totals[1].max += nca->max;
    }
    else
    {
      print_number(&canvas, x + column_width * 1, y + 9, ca->cur);
      print_number(&canvas, x + column_width * 2, y + 9, ca->min);
      print_number(&canvas, x + column_width * 3, y + 9, ca->avg);
      print_number(&canvas, x + column_width * 4, y + 9, ca->max);
    }

    totals[0].cur += ca->cur;
    totals[0].min += ca->min;
    totals[0].avg += ca->avg;
    totals[0].max += ca->max;

    y += LINE_HEIGHT;
  }

  if(g->total)
  {
    if(has_negative)
    {
      print_numbers(&canvas, x + column_width * 1, y + 9, totals[1].cur, totals[0].cur);
      print_numbers(&canvas, x + column_width * 2, y + 9, totals[1].min, totals[0].min);
      print_numbers(&canvas, x + column_width * 3, y + 9, totals[1].avg, totals[0].avg);
      print_numbers(&canvas, x + column_width * 4, y + 9, totals[1].max, totals[0].max);
    }
    else
    {
      print_number(&canvas, x + column_width * 1, y + 9, totals[0].cur);
      print_number(&canvas, x + column_width * 2, y + 9, totals[0].min);
      print_number(&canvas, x + column_width * 3, y + 9, totals[0].avg);
      print_number(&canvas, x + column_width * 4, y + 9, totals[0].max);
    }
  }

  y = graph_height + min * (graph_height - 1) / (max - min) - 1;

  draw_line(&canvas, graph_x, y + graph_y, graph_x + graph_width - 1, y + graph_y, 0);

  char* png_path;

  asprintf(&png_path, "%s/%s/%s-%s-%s.png", htmldir, g->domain, g->host, g->name, suffix);

  write_png(png_path, canvas.width, canvas.height, canvas.data);

  free(png_path);
  free(canvas.data);
}
