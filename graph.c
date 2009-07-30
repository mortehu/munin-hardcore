#include <assert.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <unistd.h>

#include "types.h"

#define ANTI_ALIASING 1

int
write_png(const char *file_name, size_t width, size_t height, unsigned char* data);

void
font_draw(struct canvas* canvas, size_t x, size_t y, const char* text, int direction);

void
do_graph(struct graph* g, size_t interval, const char* suffix);

static const uint32_t colors[] =
{
  0x21fb21, 0x0022ff, 0xff0000, 0x00aaaa, 0xff00ff, 0xffa500, 0xcc0000,
  0x0000cc, 0x0080c0, 0x8080c0, 0xff0080, 0x800080, 0x688e23
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

int
curve_name_cmp(const void* plhs, const void* prhs)
{
  const struct curve* lhs = plhs;
  const struct curve* rhs = prhs;

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
    value_start;

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
          char* curve_end;

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

      size_t rra, offset = 0;
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

    qsort(g->curves, g->curve_count, sizeof(struct curve), curve_name_cmp);

    do_graph(g, 1, "day");
    do_graph(g, 6, "week");
    do_graph(g, 24, "month");
    do_graph(g, 288, "year");

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
  double max;
  int rra_min, rra_max, rra_avg;
  size_t rra_min_offset, rra_max_offset, rra_avg_offset;
  size_t negative;
};

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
  size_t i;

  struct rrd* data;
  size_t data_offset, rra;
  size_t row_count, skip;

  int x = 0, y, prev_y = -1, n;

  data = &c->data;
  rra = ca->rra_avg;
  data_offset = ca->rra_avg_offset;
  row_count = data->rra_defs[rra].row_count;

  skip = row_count - width;

  for(i = 0; i < i < row_count && x < width; ++i, ++x)
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

double
calc_step_size(double range, size_t graph_height)
{
  const unsigned int factors[] = { 1, 2, 5 };
  unsigned int i;

  double min_step = range / (graph_height / 15.0);
  double mag = pow(10.0, floor(log(min_step) / log(10.0)));

  for(i = 0; i < 3; ++i)
    if(mag * i >= min_step)
      return mag * i;

  return mag * 10;
}

void
do_graph(struct graph* g, size_t interval, const char* suffix)
{
  size_t i, curve, ds = 0;
  int j;

  fprintf(stderr, "%s %s %s\n", g->host, g->name, suffix);

  size_t visible_graph_count = 0;

  struct curve_args* cas = alloca(sizeof(struct curve_args) * g->curve_count);

  for(curve = 0; curve < g->curve_count; ++curve)
  {
    struct curve* c = &g->curves[curve];
    struct curve_args* ca = &cas[curve];
    size_t rra, offset = 0;

    ca->max = 0.0;
    ca->rra_min = ca->rra_max = ca->rra_avg = -1;

    for(rra = 0; rra < c->data.header.rra_count; ++rra)
    {
      if(c->data.rra_defs[rra].pdp_count != interval)
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

    for(i = 0; i < c->data.rra_defs[ca->rra_avg].row_count; ++i)
    {
      double value = c->data.values[ca->rra_avg_offset + i * c->data.header.ds_count];

      if(value > ca->max)
        ca->max = value;
    }

    if(!c->nograph)
      ++visible_graph_count;

    if(c->negative)
    {
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

  double min = 0, max = 0;

  for(curve = 0; curve < g->curve_count; ++curve)
  {
    struct curve* c = &g->curves[curve];
    struct curve_args* ca = &cas[curve];

    if(c->nograph)
      continue;

    if(ca->max > max)
      max = ca->max;

    if(c->negative)
    {
      if(-cas[ca->negative].max < min)
        min = -cas[ca->negative].max;
    }
  }

  char* png_path;
  struct canvas canvas;

  size_t graph_width, graph_height;
  size_t graph_x = 60, graph_y = 30;

  size_t x, y;

  graph_width = g->width ? g->width : 400;
  graph_height = g->height ? g->height : 175;

  if(graph_width > MAX_DIM || graph_height > MAX_DIM)
    errx(EXIT_FAILURE, "Graph dimensions %zux%zu are too big", graph_width, graph_height);

  canvas.width = graph_width + 100;
  canvas.height = graph_height + 100 + visible_graph_count * 15;

  canvas.data = malloc(3 * canvas.width * canvas.height);
  memset(canvas.data, 0xee, 3 * canvas.width * canvas.height);

  draw_rect(&canvas, graph_x, graph_y, graph_width, graph_height, 0xffffff);

  double step_size = calc_step_size(max - min, graph_height);
  size_t graph_index = 0;

  for(j = min / step_size; j <= max / step_size; ++j)
  {
    char buf[32];

    y = graph_height - (j * step_size - min) * (graph_height - 1) / (max - min) - 1;

    sprintf(buf, "%.0f", j * step_size);

    font_draw(&canvas, 5, graph_y + y, buf, 0);

    if(!j)
      continue;

    for(x = 0; x < graph_width; x += 2)
      draw_pixel(&canvas, x + graph_x, y + graph_y, 0xc0c0c0);
  }

  y = graph_y + graph_height + 20;

  font_draw(&canvas, canvas.width - 14, 5, "Munin Hardcore / Morten Hustveit", 1);

  if(g->vlabel)
    font_draw(&canvas, 14, graph_y + graph_height, g->vlabel, 2);

  for(curve = 0; curve < g->curve_count; ++curve)
  {
    struct curve* c = &g->curves[curve];
    struct curve_args* ca = &cas[curve];
    uint32_t color;

    if(c->nograph)
      continue;

    color = colors[graph_index % sizeof(colors)];

    plot_gauge(&canvas, c, ca, graph_x, graph_y, graph_width, graph_height, min, max, ds, color, 0);

    if(c->negative)
      plot_gauge(&canvas, &g->curves[ca->negative], &cas[ca->negative], graph_x, graph_y, graph_width, graph_height, min, max, ds, color, PLOT_NEGATIVE);

    draw_rect(&canvas, 10, y,  6, 6, color);
    draw_line(&canvas,  9, y - 1, 17, y - 1, 0);
    draw_line(&canvas,  9, y + 6, 17, y + 6, 0);
    draw_line(&canvas,  9, y,  9, y + 6, 0);
    draw_line(&canvas, 16, y, 16, y + 6, 0);

    font_draw(&canvas, 22, y + 9, c->label ? c->label : c->name, 0);

    ++graph_index;
    y += 15;
  }

  y = graph_height + min * (graph_height - 1) / (max - min) - 1;

  draw_line(&canvas, graph_x, y + graph_y, graph_x + graph_width - 1, y + graph_y, 0);

  asprintf(&png_path, "graphs/%s-%s-%s.png", g->host, g->name, suffix);

  write_png(png_path, canvas.width, canvas.height, canvas.data);

  free(png_path);
  free(canvas.data);
}
