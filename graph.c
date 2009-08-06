#include <assert.h>
/*  Entry point for munin-hardcore-graph.
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

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <getopt.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "draw.h"
#include "font.h"
#include "munin.h"
#include "rrd.h"

static const struct option long_options[] =
{
  { "debug", 0, 0, 'd' },
  { "no-lazy", 0, 0, 'n' },
  { "help", 0, 0, 'h' },
  { "version", 0, 0, 'v' },
  { 0, 0, 0, 0 }
};

static int debug = 0;
static int nolazy = 0;

static void
help(const char* argv0)
{
  printf("Usage: %s [OPTION]...\n"
         "batch plotting of RRD data files\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short"
         " options too\n"
         "\n"
         " -d, --debug                print debug messages\n"
         " -n, --no-lazy              redraw every single graph\n"
         "     --help     display this help and exit\n"
         "     --version  display version information and exit\n"
         "\n"
         "Report bugs to <morten@rashbox.org>.\n", argv0);
}

static const uint32_t colors[] =
{
  0x21fb21, 0x0022ff, 0xff0000, 0x00aaaa, 0xff00ff, 0xffa500, 0xcc0000,
  0x0000cc, 0x0080c0, 0x8080c0, 0xff0080, 0x800080, 0x688e23, 0x408080,
  0x808000, 0x000000
};

void
do_graph(struct graph* g, size_t interval, const char* suffix);

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

size_t
graph_index(const char* domain, const char* host, const char* name)
{
  size_t i;

  for(i = graph_count; i-- > 0; )
  {
    if(!strcmp(graphs[i].domain, domain)
    && !strcmp(graphs[i].host, host)
    && !strcmp(graphs[i].name, name))
      return i;
  }

  i = graph_count;

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

  for(i = graph->curve_count; i-- > 0; )
  {
    if(!strcmp(graph->curves[i].name, name))
      return i;
  }

  i = graph->curve_count;

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

  if(graph_order)
  {
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
  }

  return strcmp(lhs->name, rhs->name);
}

int
graph_cmp(const void* plhs, const void* prhs)
{
  const struct graph* lhs = plhs;
  const struct graph* rhs = prhs;
  int result;

  if(0 != (result = strcmp(lhs->domain, rhs->domain)))
    return result;

  return strcmp(lhs->name, rhs->name);
}

const char* key_strings[] =
{
  "cdef", "color", "colour", "critical",
  "dbdir", "draw", "graph",
  "graph_args", "graph_category", "graph_height", "graph_info",
  "graph_order", "graph_period", "graph_scale", "graph_title",
  "graph_total", "graph_vlabel", "graph_width", "htmldir",
  "info", "label", "logdir", "max",
  "min", "negative", "rundir", "skipdraw",
  "tmpldir", "type", "warn", "warning"
};

enum key
{
  key_cdef = 0, key_color, key_colour, key_critical,
  key_dbdir, key_draw, key_graph,
  key_graph_args, key_graph_category, key_graph_height, key_graph_info,
  key_graph_order, key_graph_period, key_graph_scale, key_graph_title,
  key_graph_total, key_graph_vlabel, key_graph_width, key_htmldir,
  key_info, key_label, key_logdir, key_max,
  key_min, key_negative, key_rundir, key_skipdraw,
  key_tmpldir, key_type, key_warn, key_warning
};

static int
lookup_key(const char* string)
{
  int first, last, len, half, middle, cmp;

  first = 0;
  len = last = sizeof(key_strings) / sizeof(key_strings[0]);

  while(len > 0)
  {
    half = len >> 1;
    middle = first + half;

    cmp = strcmp(string, key_strings[middle]);

    if(cmp == 0)
      return middle;

    if(cmp > 0)
    {
      first = middle + 1;
      len = len - half - 1;
    }
    else
      len = half;
  }

  return -1;
}

void parse_datafile(char* in)
{
  size_t curve, graph;
  size_t lineno = 2;

  char *line_end;
  char* key_start;
  char* key_end;
  char* value_start;
  char* domain_end;
  char* host_start;
  char* host_end;
  char* graph_start;
  char* graph_end;
  char* graph_key;
  char* tmp;

  while(*in)
  {
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

    if(0 != (domain_end = strchr(key_start, ';')))
    {
      host_start = domain_end + 1;
      *domain_end = 0;

      host_end = strchr(host_start, ':');

      if(!host_end)
        errx(EXIT_FAILURE, "Parse error at line %zu in '%s'.  Did not find a : character after host name", lineno, DATA_FILE);

      graph_start = host_end + 1;
      *host_end = 0;

      if(0 != (graph_end = strchr(graph_start, '.')))
      {
        struct graph* g;

        if(!graph_end)
          errx(EXIT_FAILURE, "Parse error at line %zu in '%s'.  Did not find a . character after graph name", lineno, DATA_FILE);

        graph_key = graph_end + 1;
        *graph_end = 0;

        graph = graph_index(key_start, host_start, graph_start);
        g = &graphs[graph];

        if(0 != (tmp = strchr(graph_key, '.')))
        {
          char* curve_start;
          struct curve* c;

          curve_start = graph_key;
          graph_key = tmp;

          *graph_key++ = 0;

          curve = curve_index(g, curve_start);
          c = &g->curves[curve];

          switch(lookup_key(graph_key))
          {
          case key_label:

            c->label = value_start;

            break;

          case key_draw:

            c->draw = value_start;

            break;

          case key_color:
          case key_colour:

            c->has_color = 1;
            c->color = strtol(value_start, 0, 16);

            break;

          case key_graph:

            c->nograph = !strcasecmp(value_start, "no");

            break;

          case key_skipdraw:

            c->nograph = !!strtol(value_start, 0, 0);

            break;

          case key_type:

            c->type = value_start;

            break;

          case key_info:

            c->info = value_start;

            break;

          case key_cdef:

            c->cdef = value_start;

            break;

          case key_negative:

            c->negative = value_start;

            break;

          case key_max:

            c->max = strtod(value_start, 0);
            c->has_max = 1;

            break;

          case key_min:

            c->min = strtod(value_start, 0);
            c->has_min = 1;

            break;

          case key_warning:
          case key_warn:

            c->warning = strtod(value_start, 0);

            break;

          case key_critical:

            c->critical = strtod(value_start, 0);

            break;

          default:

            if(debug)
              fprintf(stderr, "Skipping unknown data source key '%s' at line %zu\n", graph_key, lineno);
          }
        }
        else
        {
          switch(lookup_key(graph_key))
          {
          case key_graph:

            g->nograph = !strcasecmp(value_start, "no");

            break;

          case key_graph_args:

            g->args = value_start;

            break;

          case key_graph_vlabel:

            g->vlabel = value_start;

            break;

          case key_graph_title:

            g->title = value_start;

            break;

          case key_graph_order:

            g->order = value_start;

            break;

          case key_graph_category:

            g->category = value_start;

            break;

          case key_graph_info:

            g->info = value_start;

            break;

          case key_graph_scale:

            g->scale = value_start;

            break;

          case key_graph_height:

            g->height = strtol(value_start, 0, 0);

            break;

          case key_graph_width:

            g->width = strtol(value_start, 0, 0);

            break;

          case key_graph_period:

            g->period = value_start;

            break;

          case key_graph_total:

            g->total = value_start;

            break;

          default:

            if(debug)
              fprintf(stderr, "Skipping unknown graph key '%s' at line %zu\n", graph_key, lineno);
          }
        }
      }
      else if(!strcmp(graph_start, "use_node_name"))
      {
      }
      else if(!strcmp(graph_start, "address"))
      {
      }
      else if(debug)
        fprintf(stderr, "Skipping unknown host key '%s' at line %zu\n", graph_start, lineno);
    }
    else if(!strcmp(key_start, "tmpldir"))
      tmpldir = value_start;
    else if(!strcmp(key_start, "htmldir"))
      htmldir = value_start;
    else if(!strcmp(key_start, "dbdir"))
      dbdir = value_start;
    else if(!strcmp(key_start, "rundir"))
      rundir = value_start;
    else if(!strcmp(key_start, "logdir"))
      logdir = value_start;
    else if(debug)
      fprintf(stderr, "Skipping unknown global key '%s' at line %zu\n", key_start, lineno);

    in = line_end + 1;
    ++lineno;
  }
}

int
main(int argc, char** argv)
{
  FILE* f;
  size_t data_size;
  char* data;
  char* in;
  char* line_end;
  size_t graph, curve;

  for(;;)
  {
    int optindex = 0;
    int c;

    c = getopt_long(argc, argv, "dn", long_options, &optindex);

    if(c == -1)
      break;

    switch(c)
    {
    case 'd':

      debug = 1;

      break;

    case 'n':

      nolazy = 1;

      break;

    case 'h':

      help(argv[0]);

      return EXIT_SUCCESS;

    case 'v':

      printf("%s-graph %s\n", PACKAGE_NAME, PACKAGE_VERSION);
      printf("Copyright © 2009 Morten Hustveit\n"
          "This is free software.  You may redistribute copies of it under the terms of\n"
          "the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n"
          "There is NO WARRANTY, to the extent permitted by law.\n"
          "\n"
          "Authors:\n"
          "  Morten Hustveit\n");

      return EXIT_SUCCESS;

    case '?':

      fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);

      return EXIT_FAILURE;
    }
  }

  if(optind != argc)
  {
    printf("Usage: %s [OPTION]...\n", argv[0]);
    fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);

    return EXIT_FAILURE;
  }

  FILE* stats = fopen("/var/lib/munin/munin-graph.stats", "w");

  if(!stats && debug)
    fprintf(stderr, "Failed to open /var/lib/munin/munin-graph.stats for writing: %s\n", strerror(errno));

  struct timeval total_start, total_end;

  gettimeofday(&total_start, 0);

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

  parse_datafile(in);

  qsort(graphs, graph_count, sizeof(struct graph), graph_cmp);

  struct timeval graph_start, graph_end;
  struct timeval domain_start, domain_end;
  gettimeofday(&graph_end, 0);
  domain_start = graph_end;

  for(graph = 0; graph < graph_count; ++graph)
  {
    struct graph* g = &graphs[graph];

    if(g->nograph)
    {
      free(g->curves);

      continue;
    }

    graph_start = graph_end;

    for(curve = 0; curve < g->curve_count; )
    {
      struct curve* c = &g->curves[curve];

      int suffix;

      if(!c->type || !strcasecmp(c->type, "gauge"))
        suffix = 'g';
      else if(!strcasecmp(c->type, "derive"))
        suffix = 'd';
      else if(!strcasecmp(c->type, "counter"))
        suffix = 'c';
      else if(!strcasecmp(c->type, "absolute"))
        suffix = 'a';
      else
        errx(EXIT_FAILURE, "Unknown curve type '%s'", c->type);

      if(-1 == asprintf(&c->path, "%s/%s/%s-%s-%s-%c.rrd", dbdir, g->domain, g->host, g->name, c->name, suffix))
        errx(EXIT_FAILURE, "asprintf failed while building RRD path: %s", strerror(errno));

      if(-1 == rrd_parse(&c->data, c->path))
      {
        if(debug)
          fprintf(stderr, "Skipping data source %s\n", c->path);

	free(g->curves[curve].path);

	--g->curve_count;
	memmove(&g->curves[curve], &g->curves[curve + 1], sizeof(struct curve) * (g->curve_count - curve));

	continue;
      }

      ++curve;
    }

    if(g->curve_count)
    {
      graph_order = g->order;
      qsort(g->curves, g->curve_count, sizeof(struct curve), curve_name_cmp);

      do_graph(g, 300, "day");
      do_graph(g, 1800, "week");
      do_graph(g, 7200, "month");
      do_graph(g, 86400, "year");

      gettimeofday(&graph_end, 0);

      if(stats)
      {
	fprintf(stats, "GS|%s|%s|%s|%.3f\n", g->domain, g->host, g->name,
		graph_end.tv_sec - graph_start.tv_sec + (graph_end.tv_usec - graph_start.tv_usec) * 1.0e-6);

	if(graph + 1 == graph_count
	|| strcmp(graphs[graph + 1].domain, graphs[graph].domain))
	{
	  domain_end = graph_end;

	  fprintf(stats, "GD|%s|%.3f\n", g->domain,
	      domain_end.tv_sec - domain_start.tv_sec + (domain_end.tv_usec - domain_start.tv_usec) * 1.0e-6);

	  domain_start = domain_end;
	}
      }

      for(curve = 0; curve < g->curve_count; ++curve)
      {
	rrd_free(&g->curves[curve].data);
	free(g->curves[curve].path);
      }
    }

    free(g->curves);
  }

  if(stats)
  {
    gettimeofday(&total_end, 0);

    fprintf(stats, "GT|total|%.3f\n",
            total_end.tv_sec - total_start.tv_sec + (total_end.tv_usec - total_start.tv_usec) * 1.0e-6);

    fclose(stats);
  }

  free(graphs);
  free(data);

  return EXIT_SUCCESS;
}

#define PLOT_NEGATIVE 0x0001

void
plot_gauge(struct canvas* canvas,
           struct rrd_iterator* iterator,
           size_t graph_x, size_t graph_y,
           size_t width, size_t height,
           double min, double max, size_t ds,
           uint32_t color, unsigned int flags)
{
  int x = 0, y, prev_y = -1;
  size_t i;

  for(i = 0; i < iterator->count && x < width; ++i, ++x)
  {
    double value = rrd_iterator_pop(iterator);

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
             struct rrd_iterator* mins,
             struct rrd_iterator* maxs,
             size_t graph_x, size_t graph_y,
             size_t width, size_t height,
             double min, double max, size_t ds,
             uint32_t color, unsigned int flags)
{
  size_t i, x = 0;

  for(i = 0; i < mins->count && i < maxs->count && x < width; ++i, ++x)
  {
    double min_value = rrd_iterator_pop(mins);
    double max_value = rrd_iterator_pop(maxs);

    if(isnan(min_value) || isnan(max_value))
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
          struct rrd_iterator* iterator, double* maxs,
          size_t graph_x, size_t graph_y,
          size_t width, size_t height,
          double min, double max, size_t ds,
          uint32_t color)
{
  int x = 0, y0, y1;
  size_t i;

  for(i = 0; i < iterator->count && x < width; ++i, ++x)
  {
    double value = rrd_iterator_pop(iterator);

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
  /* XXX: Not reentrant because of this buffer: */
  static char suffix_buf[32];

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
    {
      sprintf(suffix_buf, "E-%u", mag * 3);
      *suffix = suffix_buf;
    }
    else
      *suffix = suffixes[mag];

    *format = fmt[rad];
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
      {
        sprintf(suffix_buf, "E+%u", mag * 3);
        *suffix = suffix_buf;
      }
      else
        *suffix = suffixes[mag - 1];

      *format = fmt[rad];
      *scale = pow(1000.0, -mag);
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

int
pmkdir(const char* path, int mode)
{
  char* p = strdupa(path);
  char* t = p + 1;

  while(0 != (t = strchr(t, '/')))
  {
    *t = 0;

    if(-1 == mkdir(p, mode) && errno != EEXIST)
    {
      fprintf(stderr, "Failed to create directory '%s': %s\n", p, strerror(errno));

      return -1;
    }

    *t++ = '/';
  }

  return 0;
}

const struct curve*
find_curve(const struct graph* g, const char* name)
{
  size_t i;

  for(i = 0; i < g->curve_count; ++i)
    if(!strcmp(g->curves[i].name, name))
      return &g->curves[i];

  return 0;
}

void
draw_grid(struct graph* g, struct canvas* canvas,
          time_t last_update, size_t interval, double min, double max,
          size_t graph_x, size_t graph_y, size_t graph_width, size_t graph_height)
{
  char buf[64];
  int i, j;
  int x, y;

  double step_size;

  const struct time_args* ta;
  struct tm tm_last_update;
  time_t t, prev_t;

  const char* format;
  const char* suffix;
  double scale;

  for(i = 0; i + 1 < sizeof(time_args) / sizeof(time_args[0]); ++i)
  {
    if(time_args[i].bar_interval > interval * 10)
      break;
  }

  ta = &time_args[i];

  step_size = calc_step_size(max - min, graph_height);

  localtime_r(&last_update, &tm_last_update);
  t = last_update + tm_last_update.tm_gmtoff;
  prev_t = t + interval;

  number_format_args((fabs(max) > fabs(min)) ? fabs(max) : fabs(min), &format, &suffix, &scale);

  for(j = min / step_size; j <= max / step_size; ++j)
  {
    y = graph_height - (j * step_size - min) * (graph_height - 1) / (max - min) - 1;

    sprintf(buf, format, (j * step_size) * scale, suffix);

    font_draw(canvas, graph_x - 5, graph_y + y + 7, buf, -1);

    if(!j)
      continue;

    for(x = 0; x < graph_width; x += 2)
      draw_pixel_50(canvas, x + graph_x, y + graph_y, 0xaaaaaa);
  }

  for(j = 0; j < graph_width; ++j)
  {
    if(ta->label_interval > 0)
    {
      if((prev_t - ta->bias) / ta->label_interval != (t - ta->bias) / ta->label_interval)
      {
        struct tm tm_tmp;

        gmtime_r(&prev_t, &tm_tmp);

        strftime(buf, sizeof(buf), ta->format, &tm_tmp);

        for(y = 0; y < graph_height; ++y)
          draw_pixel_50(canvas, graph_x + graph_width - j, y + graph_y, 0xaa8888);

        font_draw(canvas, graph_x + graph_width - j, graph_y + graph_height + LINE_HEIGHT, buf, -2);
      }
      else if(ta->bar_interval && (prev_t - ta->bias) / ta->bar_interval != (t - ta->bias) / ta->bar_interval)
      {
        for(y = 0; y < graph_height; y += 2)
          draw_pixel_50(canvas, graph_x + graph_width - j, y + graph_y, 0xaaaaaa);
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

        for(y = 0; y < graph_height; ++y)
          draw_pixel_50(canvas, graph_x + graph_width - j, y + graph_y, 0xaa8888);

        font_draw(canvas, graph_x + graph_width - j, graph_y + graph_height + LINE_HEIGHT, buf, -2);
      }
    }

    prev_t = t;
    t -= interval;
  }

  strftime(buf, sizeof(buf), "Last update: %Y-%m-%d %H:%M:%S %Z", &tm_last_update);
  font_draw(canvas, canvas->width - 5, canvas->height - 3, buf, -1);
}

void
do_graph(struct graph* g, size_t interval, const char* suffix)
{
  size_t x, y, width;
  time_t last_update = 0;
  size_t i, curve, ds = 0;

  char* png_path;

  asprintf(&png_path, "%s/%s/%s-%s-%s.png", htmldir, g->domain, g->host, g->name, suffix);

  struct stat png_stat;

  if(!nolazy && interval > 300 && 0 == stat(png_path, &png_stat))
  {
    for(curve = 0; curve < g->curve_count; ++curve)
    {
      if(g->curves[curve].data.live_header.last_up / interval != png_stat.st_mtime / interval)
        break;
    }

    if(curve == g->curve_count)
    {
      free(png_path);

      return;
    }
  }

  int has_negative = 0, draw_min_max = 0;

  size_t graph_width, graph_height;
  size_t graph_x = 60, graph_y = 30;

  graph_width = g->width ? g->width : 400;
  graph_height = g->height ? g->height : 175;

  double min = 0, max = 0;
  double* maxs = alloca(sizeof(double) * graph_width);
  memset(maxs, 0, sizeof(double) * graph_width);

  size_t visible_graph_count = 0;

  for(curve = 0; curve < g->curve_count; ++curve)
  {
    struct curve* c = &g->curves[curve];
    int area = 0;

    struct rrd_iterator iterator_average;
    struct rrd_iterator iterator_min;
    struct rrd_iterator iterator_max;
    size_t avg_count = 0;

    if(c->data.live_header.last_up > last_update)
      last_update = c->data.live_header.last_up;

    if(-1 == rrd_iterator_create(&iterator_average, &c->data, "AVERAGE", interval, graph_width)
    || -1 == rrd_iterator_create(&iterator_min,     &c->data, "MIN", interval, graph_width)
    || -1 == rrd_iterator_create(&iterator_max,     &c->data, "MAX", interval, graph_width))
      errx(EXIT_FAILURE, "Did not find all required round robin archives in '%s'", c->path);

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

    c->work.cur = rrd_iterator_last(&iterator_average);
    c->work.max_avg = 0.0;
    c->work.min_avg = 0.0;
    c->work.min = 0.0;
    c->work.max = 0.0;
    c->work.avg = 0.0;

    for(i = 0, x = 0; i < iterator_average.count && x < graph_width; ++i, ++x)
    {
      double avg_value = rrd_iterator_pop(&iterator_average);
      double min_value = rrd_iterator_pop(&iterator_min);
      double max_value = rrd_iterator_pop(&iterator_max);

      if(!isnan(avg_value))
      {
        if(area)
        {
          maxs[x] += avg_value;

          if(maxs[x] > max)
            max = maxs[x];
        }

        c->work.avg += avg_value;
        ++avg_count;

        if(avg_value > c->work.max_avg)
          c->work.max_avg = avg_value;
        else if(avg_value < c->work.min_avg)
          c->work.min_avg = avg_value;
      }

      if(!isnan(max_value) && max_value > c->work.max)
        c->work.max = max_value;

      if(!isnan(min_value) && min_value < c->work.min)
        c->work.min = min_value;
    }

    if(avg_count)
      c->work.avg /= avg_count;

    if(!c->nograph)
      ++visible_graph_count;

    if(c->negative)
    {
      has_negative = 1;

      c->work.negative = find_curve(g, c->negative);

      if(!c->work.negative)
        errx(EXIT_FAILURE, "Negative '%s' for '%s' not found", c->name, c->negative);
    }
    else
      c->work.negative = 0;
  }

  if(visible_graph_count == 1
  && (!g->curves[0].draw || !strcasecmp(g->curves[0].draw, "line2")))
    draw_min_max = 1;

  for(curve = 0; curve < g->curve_count; ++curve)
  {
    struct curve* c = &g->curves[curve];

    if(c->nograph)
      continue;

    if(c->has_max && c->max > max)
      c->max = max;

    if(draw_min_max)
    {
      if(c->work.max > max)
        max = c->work.max;

      if(c->work.min < min)
        min = c->work.min;

      if(c->work.negative)
      {
        if(-c->work.negative->work.max < min)
          min = -c->work.negative->work.max;

        if(-c->work.negative->work.min > max)
          max = -c->work.negative->work.min;
      }
    }
    else
    {
      if(c->work.max_avg > max)
        max = c->work.max_avg;

      if(c->work.min_avg < min)
        min = c->work.min_avg;

      if(c->negative)
      {
        if(-c->work.negative->work.max_avg < min)
          min = -c->work.negative->work.max_avg;

        if(-c->work.negative->work.min_avg > max)
          max = -c->work.negative->work.min_avg;
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

  if(g->title)
  {
    snprintf(buf, sizeof(buf), "%s - by %s", g->title, suffix);
    buf[sizeof(buf) - 1] = 0;

    width = font_width(buf);
    font_draw(&canvas, (canvas.width - width) / 2, 20, buf, 0);
  }

  font_draw(&canvas, canvas.width - 15, 5, "Munin Hardcore/Morten Hustveit (test)", 1);

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

  if(min != max)
  {
    int pass;

    memset(maxs, 0, sizeof(double) * graph_width);

    for(pass = 0; pass < 2; ++pass)
    {
      size_t graph_index = 0;

      y = graph_y + graph_height + 20 + LINE_HEIGHT;

      if(pass == 1)
        draw_grid(g, &canvas, last_update, interval, min, max, graph_x, graph_y, graph_width, graph_height);

      for(curve = 0; curve < g->curve_count; ++curve)
      {
        struct rrd_iterator iterator_average;
        struct rrd_iterator iterator_min;
        struct rrd_iterator iterator_max;

        struct curve* c = &g->curves[curve];
        uint32_t color;

        if(c->nograph)
          continue;

        const char* label = c->label ? c->label : c->name;
        size_t label_width = font_width(label);

        if(label_width > max_label_width)
          max_label_width = label_width;

        if(c->has_color)
          color = c->color;
        else
          color = colors[graph_index % (sizeof(colors) / sizeof(colors[0]))];

        rrd_iterator_create(&iterator_average, &c->data, "AVERAGE", interval, graph_width);

        if(!c->draw || !strcasecmp(c->draw, "line2"))
        {
          if(draw_min_max)
          {
            if(pass == 0)
            {
              rrd_iterator_create(&iterator_min, &c->data, "MIN", interval, graph_width);
              rrd_iterator_create(&iterator_max, &c->data, "MAX", interval, graph_width);

              plot_min_max(&canvas, &iterator_min, &iterator_max, graph_x, graph_y, graph_width, graph_height, min, max, ds, color, 0);
            }
            else
              plot_gauge(&canvas, &iterator_average, graph_x, graph_y, graph_width, graph_height, min, max, ds, (color >> 1) & 0x7f7f7f, 0);

            if(c->negative)
            {
              const struct curve* cn;

              if(0 != (cn = find_curve(g, c->negative)))
              {
                if(pass == 0)
                {
                  rrd_iterator_create(&iterator_min, &cn->data, "MIN", interval, graph_width);
                  rrd_iterator_create(&iterator_max, &cn->data, "MAX", interval, graph_width);

                  plot_min_max(&canvas, &iterator_min, &iterator_max, graph_x, graph_y, graph_width, graph_height, min, max, ds, color, PLOT_NEGATIVE);
                }
                else
                {
                  rrd_iterator_create(&iterator_average, &cn->data, "AVERAGE", interval, graph_width);

                  plot_gauge(&canvas, &iterator_average, graph_x, graph_y, graph_width, graph_height, min, max, ds, (color >> 1) & 0x7f7f7f, PLOT_NEGATIVE);
                }
              }
            }
          }
          else if(pass == 1)
          {
            plot_gauge(&canvas, &iterator_average, graph_x, graph_y, graph_width, graph_height, min, max, ds, color, 0);

            if(c->negative)
            {
              const struct curve* cn;

              if(0 != (cn = find_curve(g, c->negative)))
              {
                rrd_iterator_create(&iterator_average, &cn->data, "AVERAGE", interval, graph_width);

                plot_gauge(&canvas, &iterator_average, graph_x, graph_y, graph_width, graph_height, min, max, ds, color, PLOT_NEGATIVE);
              }
            }
          }
        }
        else if(!strcasecmp(c->draw, "area"))
        {
          if(pass == 0)
          {
            memset(maxs, 0, sizeof(double) * graph_width);

            plot_area(&canvas, &iterator_average, maxs, graph_x, graph_y, graph_width, graph_height, min, max, ds, color);
          }
        }
        else if(!strcasecmp(c->draw, "stack") && (pass == 0))
        {
          if(pass == 0)
            plot_area(&canvas, &iterator_average, maxs, graph_x, graph_y, graph_width, graph_height, min, max, ds, color);
        }

        if(pass == 0)
        {
          draw_rect(&canvas, 10, y,  6, 6, draw_min_max ? ((color >> 1) & 0x7f7f7f) : color);
          draw_line(&canvas,  9, y - 1, 17, y - 1, 0);
          draw_line(&canvas,  9, y + 6, 17, y + 6, 0);
          draw_vline(&canvas,  9, y, y + 6, 0);
          draw_vline(&canvas, 16, y, y + 6, 0);

          font_draw(&canvas, 22, y + 9, c->label ? c->label : c->name, 0);
        }

        ++graph_index;
        y += LINE_HEIGHT;
      }
    }
  }
  else
  {
    min = 0.0;
    max = 1.0;
    y = graph_y + graph_height + 20 + LINE_HEIGHT;
    draw_grid(g, &canvas, last_update, interval, min, max, graph_x, graph_y, graph_width, graph_height);
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

  double totals[4][2];
  memset(&totals, 0, sizeof(totals));

  for(curve = 0; curve < g->curve_count; ++curve)
  {
    struct curve* c = &g->curves[curve];

    if(c->nograph)
      continue;

    if(c->work.negative)
    {
      print_numbers(&canvas, x + column_width * 1, y + 9, c->work.negative->work.cur, c->work.cur);
      print_numbers(&canvas, x + column_width * 2, y + 9, c->work.negative->work.min, c->work.min);
      print_numbers(&canvas, x + column_width * 3, y + 9, c->work.negative->work.avg, c->work.avg);
      print_numbers(&canvas, x + column_width * 4, y + 9, c->work.negative->work.max, c->work.max);

      totals[0][1] += c->work.negative->work.cur;
      totals[1][1] += c->work.negative->work.min;
      totals[2][1] += c->work.negative->work.avg;
      totals[3][1] += c->work.negative->work.max;
    }
    else
    {
      if(c->critical && c->work.cur > c->critical)
        draw_rect(&canvas, x, y - 4, column_width + 2, LINE_HEIGHT, 0xff7777);
      else if(c->warning && c->work.cur > c->warning)
        draw_rect(&canvas, x, y - 4, column_width + 2, LINE_HEIGHT, 0xffff77);

      print_number(&canvas, x + column_width * 1, y + 9, c->work.cur);
      print_number(&canvas, x + column_width * 2, y + 9, c->work.min);
      print_number(&canvas, x + column_width * 3, y + 9, c->work.avg);
      print_number(&canvas, x + column_width * 4, y + 9, c->work.max);
    }

    totals[0][0] += c->work.cur;
    totals[1][0] += c->work.min;
    totals[2][0] += c->work.avg;
    totals[3][0] += c->work.max;

    y += LINE_HEIGHT;
  }

  if(g->total)
  {
    if(has_negative)
    {
      print_numbers(&canvas, x + column_width * 1, y + 9, totals[0][1], totals[0][0]);
      print_numbers(&canvas, x + column_width * 2, y + 9, totals[1][1], totals[1][0]);
      print_numbers(&canvas, x + column_width * 3, y + 9, totals[2][1], totals[2][0]);
      print_numbers(&canvas, x + column_width * 4, y + 9, totals[3][1], totals[3][0]);
    }
    else
    {
      print_number(&canvas, x + column_width * 1, y + 9, totals[0][0]);
      print_number(&canvas, x + column_width * 2, y + 9, totals[1][0]);
      print_number(&canvas, x + column_width * 3, y + 9, totals[2][0]);
      print_number(&canvas, x + column_width * 4, y + 9, totals[3][0]);
    }
  }

  y = graph_height + min * (graph_height - 1) / (max - min) - 1;

  draw_line(&canvas, graph_x, y + graph_y, graph_x + graph_width - 1, y + graph_y, 0);

  if(-1 != pmkdir(png_path, 0775))
    write_png(png_path, canvas.width, canvas.height, canvas.data);

  free(png_path);
  free(canvas.data);
}
