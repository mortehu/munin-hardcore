/*  Graphing functions.
    Copyright (C) 2009  Morten Hustveit <morten@rashbox.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, version 2.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <err.h>
#include <sysexits.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "draw.h"
#include "font.h"
#include "graph.h"
#include "munin.h"
#include "rrd.h"

#define PLOT_NEGATIVE 0x0001
#define PLOT_WIDTH2   0x0002
#define PLOT_WIDTH3   0x0004

#define MAX_DIM 2048
#define LINE_HEIGHT 14

#define INTERVAL_MONTH -1

struct time_args
{
  const char* format;
  time_t bias;
  time_t label_interval;
  time_t bar_interval;
};

static const uint32_t colors[] =
{
  0x21fb21, 0x0022ff, 0xff0000, 0x00aaaa, 0xff00ff, 0xffa500, 0xcc0000,
  0x0000cc, 0x0080c0, 0x8080c0, 0xff0080, 0x800080, 0x688e23, 0x408080,
  0x808000, 0x000000
};

const struct time_args time_args[] =
{
    { "%a %H:%M", 0, 43200, 3600 },
    { "%d", 0, 86400, 21600 },
    { "Week %V", 345600, 86400 * 7, 86400 },
    { "%b", 0, INTERVAL_MONTH, 0 },
};

enum version cur_version = ver_unknown;

int debug = 0;
int nolazy = 0;

FILE* stats;
static int first_domain = 1;
static struct timeval domain_start, domain_end;

struct graph* graphs = 0;
size_t graph_count = 0;
size_t graph_alloc = 0;

const char* tmpldir = "/etc/munin/templates";
const char* htmldir = "/var/www/munin";
const char* dbdir = "/var/lib/munin";
const char* rundir = "/var/run/munin";
const char* logdir = "/var/log/munin";

static __thread const char* graph_order;

static void
do_graph (struct graph* g, size_t interval, const char* suffix);

ssize_t
find_graph (const char* domain, const char* host, const char* name, int create)
{
  size_t i;
  char *ch;

  for (i = graph_count; i-- > 0; )
    {
      if (!strcmp (graphs[i].domain, domain)
          && !strcmp (graphs[i].host, host)
          && !strcmp (graphs[i].name, name))
        return i;
    }

  if (!create)
    return -1;

  i = graph_count;

  if (graph_count == graph_alloc)
    {
      graph_alloc = graph_alloc * 3 / 2 + 32;
      graphs = realloc (graphs, sizeof (struct graph) * graph_alloc);

      if (!graphs)
        errx (EXIT_FAILURE, "Memory allocation failed: %s", strerror (errno));
    }

  memset (&graphs[i], 0, sizeof (struct graph));
  graphs[i].domain = domain;
  graphs[i].host = host;
  graphs[i].name = name;
  graphs[i].name_png_path = strdup (name);

  if (!graphs[i].name_png_path)
    errx (EXIT_FAILURE, "Memory allocation failed: %s", strerror (errno));

  graphs[i].name_rrd_path = strdup (name);

  if (!graphs[i].name_rrd_path)
    errx (EXIT_FAILURE, "Memory allocation failed: %s", strerror (errno));

  ++graph_count;

  for (ch = strchr (graphs[i].name_png_path, '.'); ch; ch = strchr (graphs[i].name_png_path, '.'))
    *ch = '/';

  for (ch = strchr (graphs[i].name_rrd_path, '.'); ch; ch = strchr (graphs[i].name_rrd_path, '.'))
    *ch = '-';

  return i;
}

size_t
curve_index (struct graph* graph, const char* name)
{
  size_t i;

  for (i = graph->curve_count; i-- > 0; )
    {
      if (!strcmp (graph->curves[i].name, name))
        return i;
    }

  i = graph->curve_count;

  if (graph->curve_count == graph->curve_alloc)
    {
      graph->curve_alloc = graph->curve_alloc * 3 / 2 + 16;
      graph->curves = realloc (graph->curves, sizeof (struct curve) * graph->curve_alloc);

      if (!graph->curves)
        errx (EXIT_FAILURE, "Memory allocation failed: %s", strerror (errno));
    }

  memset (&graph->curves[i], 0, sizeof (struct curve));
  graph->curves[i].name = name;
  ++graph->curve_count;

  return i;
}

const char*
strword (const char* haystack, const char* needle)
{
  const char* tmp;
  size_t needle_length = strlen (needle);

  tmp = strstr (haystack, needle);

  while (tmp)
    {
      if ((tmp == haystack || isspace (tmp[-1]))
         && (tmp[needle_length] == 0 || tmp[needle_length] == '=' || isspace (tmp[needle_length])))
        return tmp;

      tmp = strstr (tmp + 1, needle);
    }

  return 0;
}

int
curve_name_cmp (const void* plhs, const void* prhs)
{
  const struct curve* lhs = plhs;
  const struct curve* rhs = prhs;

  if (graph_order)
    {
      const char* a = strword (graph_order, lhs->name);
      const char* b = strword (graph_order, rhs->name);

      if (a)
        {
          if (!b)
            return -1;
          else
            return a - b;
        }
      else if (b)
        return 1;
    }

  return strcmp (lhs->name, rhs->name);
}

const char* key_strings[] =
{
  "cdef", "color", "colour", "critical", "dbdir", "draw", "graph",
  "graph_args", "graph_category", "graph_data_size", "graph_height",
  "graph_info", "graph_order", "graph_period", "graph_scale", "graph_title",
  "graph_total", "graph_vlabel", "graph_width", "htmldir", "info", "label",
  "logdir", "max", "min", "negative", "rundir", "skipdraw", "tmpldir", "type",
  "update_rate", "warn", "warning"
};

enum key
{
  key_cdef = 0, key_color, key_colour, key_critical, key_dbdir, key_draw,
  key_graph, key_graph_args, key_graph_category, key_graph_data_size,
  key_graph_height, key_graph_info, key_graph_order, key_graph_period,
  key_graph_scale, key_graph_title, key_graph_total, key_graph_vlabel,
  key_graph_width, key_htmldir, key_info, key_label, key_logdir, key_max,
  key_min, key_negative, key_rundir, key_skipdraw, key_tmpldir, key_type,
  key_update_rate, key_warn, key_warning
};

static int
lookup_key (const char* string)
{
  int first, last, len, half, middle, cmp;

  first = 0;
  len = last = sizeof (key_strings) / sizeof (key_strings[0]);

  while (len > 0)
    {
      half = len >> 1;
      middle = first + half;

      cmp = strcmp (string, key_strings[middle]);

      if (cmp == 0)
        return middle;

      if (cmp > 0)
        {
          first = middle + 1;
          len = len - half - 1;
        }
      else
        len = half;
    }

  return -1;
}

void
parse_datafile (char* in, const char *pathname)
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
  char* graph_name;
  char* curve_name;
  char* graph_key;

  int host_terminator;
  int graph_terminator;

  switch (cur_version)
    {
    case ver_1_2:

      host_terminator = ':';
      graph_terminator = '.';

      break;

    case ver_1_3:

      host_terminator = ';';
      graph_terminator = ';';

      break;

    case ver_1_4:
    case ver_2_0:

      htmldir = "/var/cache/munin/www";
      host_terminator = ':';
      graph_terminator = '.';

      break;

    default:

      errx (EXIT_FAILURE, "parse_datafile: Unknown datafile version");
    }

  while (*in)
    {
      key_start = in;

      while (isspace (*key_start))
        ++key_start;

      if (!*key_start)
        break;

      line_end = key_start + 1;

      while (*line_end && *line_end != '\n')
        ++line_end;

      *line_end = 0;

      key_end = strchr (key_start, ' ');

      if (!key_end)
        errx (EXIT_FAILURE, "Parse error at line %zu in '%s'.  Did not find a SPACE character", lineno, pathname);

      value_start = key_end + 1;
      *key_end = 0;

      while (isspace (*value_start))
        ++value_start;

      if (0 != (domain_end = strchr (key_start, ';')))
        {
          host_start = domain_end + 1;
          *domain_end = 0;

          if (0 == (host_end = strchr (host_start, host_terminator)))
            errx (EXIT_FAILURE, "Parse error at line %zu in '%s'.  Did not find a %c character after host name",
                  lineno, pathname, host_terminator);

          graph_name = host_end + 1;
          *host_end = 0;

          if (0 != (graph_key = strrchr (graph_name, graph_terminator)))
	    {
	      struct graph *g;

	      *graph_key++ = 0;

	      if (strncmp (graph_key, "graph_", 6)
		  && 0 != (curve_name = strrchr (graph_name, graph_terminator)))
		*curve_name++ = 0;
	      else
		curve_name = 0;

	      graph = find_graph (key_start, host_start, graph_name, 1);
	      g = &graphs[graph];

	      if (curve_name)
		{
		  struct curve* c;

		  curve = curve_index (g, curve_name);
		  c = &g->curves[curve];

		  switch (lookup_key (graph_key))
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
		      c->color = strtol (value_start, 0, 16);

		      break;

		    case key_graph:

		      c->nograph = !strcasecmp (value_start, "no");

		      break;

		    case key_skipdraw:

		      c->nograph = !!strtol (value_start, 0, 0);

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

		      c->max = strtod (value_start, 0);
		      c->has_max = 1;

		      break;

		    case key_min:

		      c->min = strtod (value_start, 0);
		      c->has_min = 1;

		      break;

		    case key_warning:
		    case key_warn:

		      c->warning = strtod (value_start, 0);

		      break;

		    case key_critical:

		      c->critical = strtod (value_start, 0);

		      break;

                    case key_update_rate:

                      break;

		    default:

		      if (debug)
			fprintf (stderr, "Skipping unknown data source key '%s' at line %zu\n", graph_key, lineno);
		    }
		}
	      else
		{
		  switch (lookup_key (graph_key))
		    {
		    case key_graph:

		      g->nograph = !strcasecmp (value_start, "no");

		      break;

		    case key_graph_args:

			{
			  char *tmp = strdup (value_start);
			  char *key, *value;

			  for (key = strtok (tmp, " "); key; key = strtok (0, " "))
			    {
			      if (!strcmp (key, "--base")
				  || !strcmp (key, "-l")
				  || !strcmp (key, "--lower-limit")
				  || !strcmp (key, "--upper-limit")
				  || !strcmp (key, "--vertical-label"))
				{
				  value = strtok (0, " ");

				  if (!value)
				    {
				      if (debug)
					fprintf (stderr,
						 "Missing argument for graph "
						 "arg '%s' at line %zu\n",
						 key, lineno);

				      continue;
				    }
				}
			      else
				value = 0;

			      if (!strcmp (key, "--base"))
				g->base = atoi (value);
			      else if (!strcmp (key, "-l"))
				g->precision = atoi (value);
			      else if (!strcmp (key, "--lower-limit"))
				{
				  g->has_lower_limit = 1;
				  g->lower_limit = strtod (value, 0);
				}
			      else if (!strcmp (key, "--upper-limit"))
				{
				  g->has_upper_limit = 1;
				  g->upper_limit = strtod (value, 0);
				}
			      else if (!strcmp (key, "--logarithmic"))
				g->logarithmic = 1;

			      /* XXX: Handle vertical-label without leaking memory */
			    }

			  free (tmp);
			}

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

		      g->noscale = !strcasecmp (value_start, "no");

		      break;

		    case key_graph_height:

		      g->height = strtol (value_start, 0, 0);

		      break;

		    case key_graph_width:

		      g->width = strtol (value_start, 0, 0);

		      break;

		    case key_graph_period:

		      g->period = value_start;

		      break;

		    case key_graph_total:

		      g->total = value_start;

		      break;

                    case key_graph_data_size:

                      break;

		    default:

		      if (debug)
			fprintf (stderr, "Skipping unknown graph key '%s' at line %zu\n", graph_key, lineno);
		    }
		}
	    }
          else if (!strcmp (graph_name, "use_node_name"))
            {
            }
          else if (!strcmp (graph_name, "address"))
            {
            }
          else if (debug)
            fprintf (stderr, "Skipping unknown host key '%s' at line %zu\n", graph_name, lineno);
        }
      else if (!strcmp (key_start, "tmpldir"))
        tmpldir = value_start;
      else if (!strcmp (key_start, "htmldir"))
        htmldir = value_start;
      else if (!strcmp (key_start, "dbdir"))
        dbdir = value_start;
      else if (!strcmp (key_start, "rundir"))
        rundir = value_start;
      else if (!strcmp (key_start, "logdir"))
        logdir = value_start;
      else if (debug)
        fprintf (stderr, "Skipping unknown global key '%s' at line %zu\n", key_start, lineno);

      in = line_end + 1;
      ++lineno;
    }
}

int
pmkdir (const char* path, int mode)
{
  char* p;
  char* t;

  p = alloca(strlen(path) + 1);
  strcpy(p, path);
  t = p + 1;

  while (0 != (t = strchr (t, '/')))
    {
      *t = 0;

      if (-1 == mkdir (p, mode) && errno != EEXIST)
        {
          fprintf (stderr, "Failed to create directory '%s': %s\n", p, strerror (errno));

          return -1;
        }

      *t++ = '/';
    }

  return 0;
}

void
process_graph (size_t graph_index)
{
  struct graph* g = &graphs[graph_index];
  size_t curve;
  struct timeval graph_start, graph_end;
  char *path;

  int curve_terminator;

  if (cur_version < ver_1_3)
    curve_terminator = '.';
  else if (cur_version < ver_2_0)
    curve_terminator = ';';
  else
    curve_terminator = ':';

  if (g->nograph)
      return;

  if (-1 == asprintf (&path, "%s/%s/", htmldir, g->domain))
    err (EXIT_FAILURE, "asprintf failed");

  if (-1 == pmkdir (path, 0775))
    {
      free (path);

      return;
    }

  free(path);

  gettimeofday (&graph_start, 0);

  if (first_domain)
    {
      domain_start = graph_start;
      first_domain = 0;
    }

  for (curve = 0; curve < g->curve_count; )
    {
      struct curve* c = &g->curves[curve];

      const struct graph* eff_g = g;
      const struct curve* eff_c = c;
      int suffix;

      if (g->order)
        {
          const char* ch;
          char* curve_name;

          ch = strword (g->order, c->name);

          if (ch && ch[strlen (c->name)] == '=')
            {
              ch += strlen(c->name) + 1;
              curve_name = alloca(strlen(ch) + 1);
              strcpy(curve_name, ch);

              if (strchr (curve_name, ' '))
                *strchr (curve_name, ' ') = 0;

              if (strchr (curve_name, curve_terminator))
                {
                  char* graph_name;
                  ssize_t eff_graph_index;

                  graph_name = curve_name;
                  curve_name = strchr (graph_name, curve_terminator);
                  *curve_name++ = 0;

                  eff_graph_index = find_graph (g->domain, g->host, graph_name, 0);

                  if (eff_graph_index == -1)
                    goto skip_data_source;

                  eff_g = &graphs[eff_graph_index];
                }

              if (0 == (eff_c = find_curve (eff_g, curve_name)))
                goto skip_data_source;
            }
        }

      if (!eff_c->type || !strcasecmp (eff_c->type, "gauge"))
        suffix = 'g';
      else if (!strcasecmp (eff_c->type, "derive"))
        suffix = 'd';
      else if (!strcasecmp (eff_c->type, "counter"))
        suffix = 'c';
      else if (!strcasecmp (eff_c->type, "absolute"))
        suffix = 'a';
      else
        errx (EXIT_FAILURE, "Unknown curve type '%s'", eff_c->type);

      if (-1 == asprintf (&c->path, "%s/%s/%s-%s-%s-%c.rrd", dbdir, eff_g->domain, eff_g->host, eff_g->name_rrd_path, eff_c->name, suffix))
        errx (EXIT_FAILURE, "asprintf failed while building RRD path: %s", strerror (errno));

      /* Data loaded by caller */
      if (c->data.data)
        {
          ++curve;

          continue;
        }

      if (0 == rrd_parse (&c->data, c->path) || c->cdef)
        {
          ++curve;

          continue;
        }

skip_data_source:

      if (debug)
        fprintf (stderr, "Skipping data source %s.%s.%s.%s (%s)\n", g->domain, g->host, g->name, c->name, c->path);

      free (c->path);

      --g->curve_count;
      memmove (&g->curves[curve], &g->curves[curve + 1], sizeof (struct curve) * (g->curve_count - curve));

      continue;
    }

  if (g->curve_count)
    {
      graph_order = g->order;
      qsort (g->curves, g->curve_count, sizeof (struct curve), curve_name_cmp);

      do_graph (g, 300, "day");
      do_graph (g, 1800, "week");
      do_graph (g, 7200, "month");
      do_graph (g, 86400, "year");

      if (stats)
        {
          gettimeofday (&graph_end, 0);

          fprintf (stats, "GS|%s|%s|%s|%.3f\n", g->domain, g->host, g->name,
                  graph_end.tv_sec - graph_start.tv_sec + (graph_end.tv_usec - graph_start.tv_usec) * 1.0e-6);

          if (graph_index + 1 == graph_count
             || strcmp (graphs[graph_index + 1].domain, graphs[graph_index].domain))
            {
              domain_end = graph_end;

              fprintf (stats, "GD|%s|%.3f\n", g->domain,
                      domain_end.tv_sec - domain_start.tv_sec + (domain_end.tv_usec - domain_start.tv_usec) * 1.0e-6);

              domain_start = domain_end;
            }
        }

      for (curve = 0; curve < g->curve_count; ++curve)
        {
          if (g->curves[curve].data.file_size)
            rrd_free (&g->curves[curve].data);

          free (g->curves[curve].work.script.tokens);
          free (g->curves[curve].path);
        }
    }
}

void
plot_gauge (struct canvas* canvas,
           struct rrd_iterator* iterator,
           size_t graph_x, size_t graph_y,
           size_t width, size_t height,
           double global_min, double global_max, size_t ds,
           uint32_t color, unsigned int flags)
{
  int x = 0, y, prev_y = -1;
  size_t i;

  for (i = 0; i < iterator->count && x < width; ++i, ++x)
    {
      double value = rrd_iterator_peek (iterator);
      rrd_iterator_advance (iterator);

      if (isnan (value))
        {
          prev_y = -1;

          continue;
        }

      if (flags & PLOT_NEGATIVE)
        value = -value;

      y = height
        - (value - global_min) * (height - 1) / (global_max - global_min)
        - 1;

      if (prev_y != -1)
        {
          if (flags & PLOT_WIDTH3)
            draw_line2 (canvas, graph_x + x - 1, graph_y + prev_y, graph_x + x, graph_y + y, color);
          else
            draw_line (canvas, graph_x + x - 1, graph_y + prev_y, graph_x + x, graph_y + y, color);
        }
      else
        {
          draw_pixel (canvas, graph_y + y, graph_x + x, color);

          if (flags & PLOT_WIDTH3)
            draw_pixel (canvas, graph_y + y, graph_x + x, color);
        }

      prev_y = y;
    }
}

void
plot_min_max (struct canvas* canvas,
             struct rrd_iterator* mins,
             struct rrd_iterator* maxs,
             size_t graph_x, size_t graph_y,
             size_t width, size_t height,
             double global_min, double global_max, size_t ds,
             uint32_t color, unsigned int flags)
{
  size_t i, x = 0;

  for (i = 0; i < mins->count && i < maxs->count && x < width; ++i, ++x)
    {
      double min_value = rrd_iterator_peek (mins);
      double max_value = rrd_iterator_peek (maxs);
      rrd_iterator_advance (mins);
      rrd_iterator_advance (maxs);

      if (isnan (min_value) || isnan (max_value))
        continue;

      if (flags & PLOT_NEGATIVE)
        {
          min_value = -min_value;
          max_value = -max_value;
        }

      size_t y0 = height - (min_value - global_min) * (height - 1) / (global_max - global_min) - 1;
      size_t y1 = height - (max_value - global_min) * (height - 1) / (global_max - global_min) - 1;

      draw_vline (canvas, graph_x + x, graph_y + y0, graph_y + y1, color);
    }
}

void
plot_area (struct canvas* canvas,
          struct rrd_iterator* iterator, double* maxs,
          size_t graph_x, size_t graph_y,
          size_t width, size_t height,
          double global_min, double global_max, size_t ds,
          uint32_t color)
{
  int x = 0, y0, y1;
  size_t i;

  for (i = 0; i < iterator->count && x < width; ++i, ++x)
    {
      double value = rrd_iterator_peek (iterator);
      rrd_iterator_advance (iterator);

      if (isnan (value) || value <= 0)
        continue;

      y0 = height - (maxs[x] - global_min) * (height - 1) / (global_max - global_min) - 1;
      y1 = height - ((value + maxs[x]) - global_min) * (height - 1) / (global_max - global_min) - 1;

      draw_vline (canvas, graph_x + x, graph_y + y0, graph_y + y1, color);

      maxs[x] += value;
    }
}

double
calc_step_size (double range, size_t graph_height)
{
  const unsigned int factors[] = { 1, 2, 5 };
  unsigned int i;

  double min_step = range / (graph_height / 14.0);
  double mag = pow (10.0, floor (log (min_step) / log (10.0)));

  for (i = 0; i < 3; ++i)
    if (mag * factors[i] >= min_step)
      return mag * factors[i];

  return mag * 10;
}

void
number_format_args (double number, const char** format, const char** suffix, double* scale, double step_size)
{
  static const char* fmt[] = { "%.2f%s", "%.1f%s", "%.0f%s" };
  /* XXX: Not reentrant because of this buffer: */
  static char suffix_buf[32];

  int mag, rad;

  if (!number)
    {
      *format = "%.0f";
      *suffix = "";
      *scale = 1.0;

      return;
    }

  if (fabs (number) < 1)
    {
      static const char* suffixes[] = { "m", "µ", "n", "p", "f", "a", "z", "y" };

      mag = -floor (log (fabs (number)) / log (10) + 1);
      rad = 2 - (mag % 3);
      mag /= 3;

      if (mag > sizeof (suffixes) / sizeof (suffixes[0]) - 1)
        {
          sprintf (suffix_buf, "E-%u", mag * 3);
          *suffix = suffix_buf;
        }
      else
        *suffix = suffixes[mag];
      *scale = pow (1000, mag + 1);
    }
  else
    {
      mag = floor (log (fabs (number)) / log (10));
      rad = mag % 3;
      mag /= 3;

      if (!mag)
        {
          *format = fmt[rad];
          *suffix = "";
          *scale = 1.0;
        }
      else
        {
          static const char* suffixes[] = { "k", "M", "G", "T", "P", "E", "Z", "Y" };

          if (mag > sizeof (suffixes) / sizeof (suffixes[0]))
            {
              sprintf (suffix_buf, "E+%u", mag * 3);
              *suffix = suffix_buf;
            }
          else
            *suffix = suffixes[mag - 1];
          *scale = pow (1000.0, -mag);
        }
    }

  if (step_size)
    {
      step_size *= *scale;

      if (step_size < 0.01)
        *format = "%.3f%s";
      else if (step_size < 0.1)
        *format = "%.2f%s";
      else if (step_size < 1.0)
        *format = "%.1f%s";
      else
        *format = "%.f%s";
    }
  else
    *format = fmt[rad];
}

void
format_number (char* target, double number, double scale_reference)
{
  const char* format;
  const char* suffix;
  double scale;

  if (isnan (number))
    strcpy (target, "nan");
  else
    {
      number_format_args (scale_reference, &format, &suffix, &scale, 0);

      sprintf (target, format, number * scale, suffix);
    }
}

void
print_number (struct canvas* canvas, size_t x, size_t y, double val, double scale_reference)
{
  char buf[64];

  format_number (buf, val, scale_reference);
  font_draw (canvas, x, y, buf, -1, 0x00);
}

void
print_numbers (struct canvas* canvas, size_t x, size_t y, double neg, double pos)
{
  char buf[64];

  format_number (buf, neg, neg);
  strcat (buf, "/");
  format_number (strchr (buf, 0), pos, pos);
  font_draw (canvas, x, y, buf, -1, 0x00);
}

int
find_curve_global (const struct graph** g, const struct curve** c, const char* graph_name, const char* curve_name)
{
  size_t i, j;

  for (j = 0; j < graph_count; ++j)
    {
      if (strcmp (graphs[j].name, graph_name))
        continue;

      for (i = 0; i < graphs[j].curve_count; ++i)
        {
          if (!strcmp (graphs[j].curves[i].name, curve_name))
            {
              *g = &graphs[j];
              *c = &graphs[j].curves[i];

              return 1;
            }
        }
    }

  return 0;
}

const struct curve*
find_curve (const struct graph* g, const char* name)
{
  const char *basename, *ch;
  size_t i;

  for (i = 0; i < g->curve_count; ++i)
    {
      basename = g->curves[i].name;

      while (0 != (ch = strchr (basename, '.')))
	basename = ch + 1;

      if (!strcmp (basename, name))
	return &g->curves[i];
    }

  return 0;
}

void
draw_grid (struct graph* g, struct canvas* canvas,
          time_t last_update, size_t interval, double global_min, double global_max,
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

  for (i = 0; i + 1 < sizeof (time_args) / sizeof (time_args[0]); ++i)
    {
      if (time_args[i].bar_interval > interval * 10)
        break;
    }

  ta = &time_args[i];

  step_size = calc_step_size (global_max - global_min, graph_height);

  localtime_r (&last_update, &tm_last_update);
  t = last_update + tm_last_update.tm_gmtoff;
  prev_t = t + interval;

  if (g->noscale)
    {
      if (step_size < 0.01)
        format = "%.3f";
      else if (step_size < 0.1)
        format = "%.2f";
      else if (step_size < 1.0)
        format = "%.1f";
      else
        format = "%.f";

      suffix = "";
      scale = 1.0;
    }
  else
    number_format_args ((fabs (global_max) > fabs (global_min)) ? fabs (global_max) : fabs (global_min), &format, &suffix, &scale, step_size);

  for (j = global_min / step_size; j <= global_max / step_size; ++j)
    {
      y = graph_height - (j * step_size - global_min) * (graph_height - 1) / (global_max - global_min) - 1;

      sprintf (buf, format, (j * step_size) * scale, suffix);

      font_draw (canvas, graph_x - 5, graph_y + y + 7, buf, -1, 0x00);

      if (!j)
        continue;

      for (x = 0; x < graph_width; x += 2)
        draw_pixel_50 (canvas, x + graph_x, y + graph_y, 0xaaaaaa);
    }

  for (j = 0; j < graph_width; ++j)
    {
      if (ta->label_interval > 0)
        {
          if ((prev_t - ta->bias) / ta->label_interval != (t - ta->bias) / ta->label_interval)
            {
              struct tm tm_tmp;

              gmtime_r (&prev_t, &tm_tmp);

              strftime (buf, sizeof (buf), ta->format, &tm_tmp);

              for (y = 0; y < graph_height; ++y)
                draw_pixel_50 (canvas, graph_x + graph_width - j, y + graph_y, 0xaa8888);

              font_draw (canvas, graph_x + graph_width - j, graph_y + graph_height + LINE_HEIGHT, buf, -2, 0x00);
            }
          else if (ta->bar_interval && (prev_t - ta->bias) / ta->bar_interval != (t - ta->bias) / ta->bar_interval)
            {
              for (y = 0; y < graph_height; y += 2)
                draw_pixel_50 (canvas, graph_x + graph_width - j, y + graph_y, 0xaaaaaa);
            }
        }
      else if (ta->label_interval == INTERVAL_MONTH)
        {
          struct tm a, b;

          gmtime_r (&prev_t, &a);
          gmtime_r (&t, &b);

          if (a.tm_mon != b.tm_mon)
            {
              strftime (buf, sizeof (buf), ta->format, &a);

              for (y = 0; y < graph_height; ++y)
                draw_pixel_50 (canvas, graph_x + graph_width - j, y + graph_y, 0xaa8888);

              font_draw (canvas, graph_x + graph_width - j, graph_y + graph_height + LINE_HEIGHT, buf, -2, 0x00);
            }
        }

      prev_t = t;
      t -= interval;
    }

  strftime (buf, sizeof (buf), "Last update: %Y-%m-%d %H:%M:%S %Z", &tm_last_update);
  font_draw (canvas, canvas->width - 5, canvas->height - 3, buf, -1, 0x00);
}

static void
do_graph (struct graph* g, size_t interval, const char* suffix)
{
  size_t x, y, width;
  time_t last_update = 0;
  size_t i, curve, ds = 0;

  const char *png_path_format;
  char* png_path;

  struct stat png_stat;

  if (cur_version < ver_1_3)
    png_path_format = "%s/%s/%s-%s-%s.png";
  else
    png_path_format = "%s/%s/%s/%s-%s.png";

  if (-1 == asprintf (&png_path, png_path_format, htmldir, g->domain, g->host, g->name_png_path, suffix))
    err (EX_OSERR, "asprintf failed");

  if (!nolazy && interval > 300 && 0 == stat (png_path, &png_stat))
    {
      for (curve = 0; curve < g->curve_count; ++curve)
        {
          if (g->curves[curve].data.live_header.last_up / interval != png_stat.st_mtime / interval)
            break;
        }

      if (curve == g->curve_count)
        {
          free (png_path);

          return;
        }
    }


  int has_negative = 0, draw_min_max = 0;

  size_t graph_width, graph_height;
  size_t graph_x = 60, graph_y = 30;

  graph_width = g->width ? g->width : 400;
  graph_height = g->height ? g->height : 175;

  double global_min = 0, global_max = 0;
  double* maxs = alloca (sizeof (double) * graph_width);
  memset (maxs, 0, sizeof (double) * graph_width);

  size_t visible_graph_count = 0;

  for (curve = 0; curve < g->curve_count; ++curve)
    {
      struct curve* c = &g->curves[curve];

      memset (&c->work, 0, sizeof (c->work));

      if (c->data.header.ds_count)
        {
          if (-1 == rrd_iterator_create (&c->work.iterator[average], &c->data, "AVERAGE", interval, graph_width)
             || -1 == rrd_iterator_create (&c->work.iterator[min],   &c->data, "MIN",     interval, graph_width)
             || -1 == rrd_iterator_create (&c->work.iterator[max],   &c->data, "MAX",     interval, graph_width))
            errx (EXIT_FAILURE, "Did not find all required round robin archives in '%s'", c->path);
        }
    }

  for (curve = 0; curve < g->curve_count; ++curve)
    {
      struct curve* c = &g->curves[curve];
      int area = 0, first = 1;

      struct rrd_iterator iterator_average;
      struct rrd_iterator iterator_min;
      struct rrd_iterator iterator_max;
      size_t avg_count = 0;

      if (c->data.live_header.last_up > last_update)
        last_update = c->data.live_header.last_up;

      if (c->cdef)
        {
          if (-1 == cdef_compile (&c->work.script, g, c->cdef))
            {
              free (png_path);

              return;
            }

          for (i = 0; i < 3; ++i)
            cdef_create_iterator (&c->work.eff_iterator[i], g, c, i, graph_width);
        }
      else
        {
          for (i = 0; i < 3; ++i)
            c->work.eff_iterator[i] = c->work.iterator[i];
        }

      if (!c->nograph && c->draw)
        {
          if (!strcasecmp (c->draw, "area"))
            {
              memset (maxs, 0, sizeof (double) * graph_width);
              area = 1;
            }
          else if (!strcasecmp (c->draw, "stack") || !strcasecmp (c->draw, "areastack"))
            area = 1;
        }

      iterator_average = c->work.eff_iterator[average];
      iterator_min = c->work.eff_iterator[min];
      iterator_max = c->work.eff_iterator[max];

      c->work.cur = rrd_iterator_last (&iterator_average);
      c->work.max_avg = 0.0;
      c->work.min_avg = 0.0;
      c->work.min = 0.0;
      c->work.max = 0.0;
      c->work.avg = 0.0;

      for (i = 0, x = 0; i < iterator_average.count && x < graph_width; ++i, ++x)
        {
          double avg_value = rrd_iterator_peek (&iterator_average);
          double min_value = rrd_iterator_peek (&iterator_min);
          double max_value = rrd_iterator_peek (&iterator_max);

          rrd_iterator_advance (&iterator_average);
          rrd_iterator_advance (&iterator_min);
          rrd_iterator_advance (&iterator_max);

          if (!isnan (avg_value))
            {
              if (area)
                {
                  if (avg_value > 0)
                    maxs[x] += avg_value;

                  if (maxs[x] > global_max)
                    global_max = maxs[x];
                }

              c->work.avg += avg_value;
              ++avg_count;

              if (first)
                {
                  c->work.max_avg = avg_value;
                  c->work.min_avg = avg_value;
                  c->work.min = avg_value;
                  first = 0;
                }
              else
                {
                  if (avg_value > c->work.max_avg)
                    c->work.max_avg = avg_value;
                  else if (avg_value < c->work.min_avg)
                    c->work.min_avg = avg_value;
                }
            }

          if (!isnan (max_value) && max_value > c->work.max)
            c->work.max = max_value;

          if (!isnan (min_value) && min_value < c->work.min)
            c->work.min = min_value;
        }

      if (avg_count)
        c->work.avg /= avg_count;

      if (!c->nograph)
        ++visible_graph_count;

      if (c->negative)
        {
          has_negative = 1;

          c->work.negative = find_curve (g, c->negative);

          if (!c->work.negative)
            errx (EXIT_FAILURE, "Negative '%s' for '%s' not found", c->negative, c->name);
        }
      else
        c->work.negative = 0;
    }

  if (visible_graph_count == 1
     && (!g->curves[0].draw
         || !strcasecmp (g->curves[0].draw, "line1")
         || !strcasecmp (g->curves[0].draw, "line2")
         || !strcasecmp (g->curves[0].draw, "line3")))
    draw_min_max = 1;

  for (curve = 0; curve < g->curve_count; ++curve)
    {
      struct curve* c = &g->curves[curve];

      if (c->nograph)
        continue;

      if (draw_min_max)
        {
          if (c->work.max > global_max)
            global_max = c->work.max;

          if (c->work.min < global_min)
            global_min = c->work.min;

          if (c->work.negative)
            {
              if (-c->work.negative->work.max < global_min)
                global_min = -c->work.negative->work.max;

              if (-c->work.negative->work.min > global_max)
                global_max = -c->work.negative->work.min;
            }
        }
      else
        {
          if (c->work.max_avg > global_max)
            global_max = c->work.max_avg;

          if (c->work.min_avg < global_min)
            global_min = c->work.min_avg;

          if (c->negative)
            {
              if (-c->work.negative->work.max_avg < global_min)
                global_min = -c->work.negative->work.max_avg;

              if (-c->work.negative->work.min_avg > global_max)
                global_max = -c->work.negative->work.min_avg;
            }
        }
    }

  if (g->has_upper_limit && global_max < g->upper_limit)
    global_max = g->upper_limit;

  struct canvas canvas;

  char buf[256];

  if (graph_width > MAX_DIM || graph_height > MAX_DIM)
    errx (EXIT_FAILURE, "Graph dimensions %zux%zu are too big", graph_width, graph_height);

  canvas.width = graph_width + 95;
  canvas.height = graph_height + 75 + visible_graph_count * LINE_HEIGHT;

  if (g->total)
    canvas.height += LINE_HEIGHT;

  canvas.data = malloc (3 * canvas.width * canvas.height);
  memset (canvas.data, 0xcc, 3 * canvas.width);
  memset (canvas.data + 3 * canvas.width, 0xf5, 3 * canvas.width * (canvas.height - 2));
  memset (canvas.data + (canvas.height - 1) * canvas.width * 3, 0x77, 3 * canvas.width);
  draw_vline (&canvas, 0, 0, canvas.height - 1, 0xcccccc);
  draw_vline (&canvas, canvas.width - 1, 0, canvas.height - 1, 0x777777);

  draw_rect (&canvas, graph_x, graph_y, graph_width, graph_height, 0xffffff);

  if (g->title)
    {
      snprintf (buf, sizeof (buf), "%s - by %s", g->title, suffix);
      buf[sizeof (buf) - 1] = 0;

      width = font_width (buf);
      font_draw (&canvas, (canvas.width - width) / 2, 20, buf, 0, 0x00);
    }

  font_draw (&canvas, canvas.width - 15, 5, "Munin Hardcore/Morten Hustveit", 1, 0xc0);

  if (g->vlabel)
    {
      const char* i = g->vlabel;
      char* o = buf;
      char* o_end = buf + sizeof (buf) - 1;

      while (*i && o != o_end)
        {
          if (*i == '$')
            {
              if (!strncmp (i, "${graph_period}", 15))
                {
                  if (o + 6 < o_end)
                    {
                      memcpy (o, "second", 7);
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

      width = font_width (buf);
      font_draw (&canvas, 14, graph_y + graph_height / 2 + width / 2, buf, 2, 0x00);
    }

  size_t max_label_width = 0;

  if (global_min != global_max)
    {
      int pass;

      memset (maxs, 0, sizeof (double) * graph_width);

      for (pass = 0; pass < 2; ++pass)
        {
          size_t graph_index = 0;

          y = graph_y + graph_height + 20 + LINE_HEIGHT;

          if (pass == 1)
            draw_grid (g, &canvas, last_update, interval, global_min, global_max, graph_x, graph_y, graph_width, graph_height);

          for (curve = 0; curve < g->curve_count; ++curve)
            {
              struct rrd_iterator iterator_average;
              struct rrd_iterator iterator_min;
              struct rrd_iterator iterator_max;

              struct curve* c = &g->curves[curve];
              uint32_t color;

              if (c->nograph)
                continue;

              const char* label = c->label ? c->label : c->name;
              size_t label_width = font_width (label);

              if (label_width > max_label_width)
                max_label_width = label_width;

              if (c->has_color)
                color = c->color;
              else
                color = colors[graph_index % (sizeof (colors) / sizeof (colors[0]))];

              iterator_average = c->work.eff_iterator[average];

              if (!c->draw
                  || !strcasecmp (c->draw, "line1")
                  || !strcasecmp (c->draw, "line2")
                  || !strcasecmp (c->draw, "line3"))
                {
                  int flags = 0;

                  if (!c->draw || !strcasecmp (c->draw, "line2"))
                    flags |= PLOT_WIDTH2;
                  else if (!strcasecmp (c->draw, "line3"))
                    flags |= PLOT_WIDTH3;

                  if (draw_min_max)
                    {
                      if (pass == 0)
                        {
                          iterator_min = c->work.eff_iterator[min];
                          iterator_max = c->work.eff_iterator[max];

                          plot_min_max (&canvas, &iterator_min, &iterator_max, graph_x, graph_y, graph_width, graph_height, global_min, global_max, ds, color, flags);
                        }
                      else
                        plot_gauge (&canvas, &iterator_average, graph_x, graph_y, graph_width, graph_height, global_min, global_max, ds, (color >> 1) & 0x7f7f7f, flags);

                      if (c->work.negative)
                        {
                          if (pass == 0)
                            {
                              iterator_min = c->work.negative->work.eff_iterator[min];
                              iterator_max = c->work.negative->work.eff_iterator[max];

                              plot_min_max (&canvas, &iterator_min, &iterator_max, graph_x, graph_y, graph_width, graph_height, global_min, global_max, ds, color, PLOT_NEGATIVE | flags);
                            }
                          else
                            {
                              iterator_average = c->work.negative->work.eff_iterator[average];

                              plot_gauge (&canvas, &iterator_average, graph_x, graph_y, graph_width, graph_height, global_min, global_max, ds, (color >> 1) & 0x7f7f7f, PLOT_NEGATIVE | flags);
                            }
                        }
                    }
                  else if (pass == 1)
                    {
                      plot_gauge (&canvas, &iterator_average, graph_x, graph_y, graph_width, graph_height, global_min, global_max, ds, color, 0);

                      if (c->work.negative)
                        {
                          iterator_average = c->work.negative->work.eff_iterator[average];

                          plot_gauge (&canvas, &iterator_average, graph_x, graph_y, graph_width, graph_height, global_min, global_max, ds, color, PLOT_NEGATIVE);
                        }
                    }
                }
              else if (!strcasecmp (c->draw, "area"))
                {
                  if (pass == 0)
                    {
                      memset (maxs, 0, sizeof (double) * graph_width);

                      plot_area (&canvas, &iterator_average, maxs, graph_x, graph_y, graph_width, graph_height, global_min, global_max, ds, color);
                    }
                }
              else if ((!strcasecmp (c->draw, "stack") || !strcasecmp (c->draw, "areastack")) && (pass == 0))
                {
                  if (pass == 0)
                    plot_area (&canvas, &iterator_average, maxs, graph_x, graph_y, graph_width, graph_height, global_min, global_max, ds, color);
                }

              if (pass == 0)
                {
                  draw_rect (&canvas, 10, y,  6, 6, draw_min_max ? ((color >> 1) & 0x7f7f7f) : color);
                  draw_line (&canvas,  9, y - 1, 17, y - 1, 0);
                  draw_line (&canvas,  9, y + 6, 17, y + 6, 0);
                  draw_vline (&canvas,  9, y, y + 6, 0);
                  draw_vline (&canvas, 16, y, y + 6, 0);

                  font_draw (&canvas, 22, y + 9, c->label ? c->label : c->name, 0, 0x00);
                }

              ++graph_index;
              y += LINE_HEIGHT;
            }
        }
    }
  else
    {
      global_min = 0.0;
      global_max = 1.0;
      y = graph_y + graph_height + 20 + LINE_HEIGHT;
      draw_grid (g, &canvas, last_update, interval, global_min, global_max, graph_x, graph_y, graph_width, graph_height);
    }

  if (g->total)
    {
      size_t label_width = font_width (g->total);

      if (label_width > max_label_width)
        max_label_width = label_width;

      font_draw (&canvas, 22, y + 9, g->total, 0, 0x00);
    }

  y = graph_y + graph_height + 20;
  x = 22 + max_label_width + 10;

  size_t column_width = (canvas.width - x - 20) / 4;

  font_draw (&canvas, x + column_width * 1, y + 9, has_negative ? "Cur (-/+)" : "Cur", -1, 0x00);
  font_draw (&canvas, x + column_width * 2, y + 9, has_negative ? "Min (-/+)" : "Min", -1, 0x00);
  font_draw (&canvas, x + column_width * 3, y + 9, has_negative ? "Avg (-/+)" : "Avg", -1, 0x00);
  font_draw (&canvas, x + column_width * 4, y + 9, has_negative ? "Max (-/+)" : "Max", -1, 0x00);
  y += LINE_HEIGHT;

  double totals[4][2];
  memset (&totals, 0, sizeof (totals));

  for (curve = 0; curve < g->curve_count; ++curve)
    {
      struct curve* c = &g->curves[curve];

      if (c->nograph)
        continue;

      if (c->work.negative)
        {
          print_numbers (&canvas, x + column_width * 1, y + 9, c->work.negative->work.cur, c->work.cur);
          print_numbers (&canvas, x + column_width * 2, y + 9, c->work.negative->work.min, c->work.min);
          print_numbers (&canvas, x + column_width * 3, y + 9, c->work.negative->work.avg, c->work.avg);
          print_numbers (&canvas, x + column_width * 4, y + 9, c->work.negative->work.max, c->work.max);

          totals[0][1] += c->work.negative->work.cur;
          totals[1][1] += c->work.negative->work.min;
          totals[2][1] += c->work.negative->work.avg;
          totals[3][1] += c->work.negative->work.max;
        }
      else
        {
          double smallest, biggest;

          if (c->critical && c->work.cur > c->critical)
            draw_rect (&canvas, x, y - 4, column_width + 2, LINE_HEIGHT, 0xff7777);
          else if (c->warning && c->work.cur > c->warning)
            draw_rect (&canvas, x, y - 4, column_width + 2, LINE_HEIGHT, 0xffff77);

          smallest = biggest = c->work.cur;

          if (c->work.min < smallest) smallest = c->work.min;
          if (c->work.avg < smallest) smallest = c->work.avg;
          if (c->work.max < smallest) smallest = c->work.max;

          if (c->work.min > biggest) biggest = c->work.min;
          if (c->work.avg > biggest) biggest = c->work.avg;
          if (c->work.max > biggest) biggest = c->work.max;

          if (biggest / smallest < 100.0)
            {
              print_number (&canvas, x + column_width * 1, y + 9, c->work.cur, smallest);
              print_number (&canvas, x + column_width * 2, y + 9, c->work.min, smallest);
              print_number (&canvas, x + column_width * 3, y + 9, c->work.avg, smallest);
              print_number (&canvas, x + column_width * 4, y + 9, c->work.max, smallest);
            }
          else
            {
              print_number (&canvas, x + column_width * 1, y + 9, c->work.cur, c->work.cur);
              print_number (&canvas, x + column_width * 2, y + 9, c->work.min, c->work.min);
              print_number (&canvas, x + column_width * 3, y + 9, c->work.avg, c->work.avg);
              print_number (&canvas, x + column_width * 4, y + 9, c->work.max, c->work.max);
            }
        }

      totals[0][0] += c->work.cur;
      totals[1][0] += c->work.min;
      totals[2][0] += c->work.avg;
      totals[3][0] += c->work.max;

      y += LINE_HEIGHT;
    }

  if (g->total)
    {
      if (has_negative)
        {
          print_numbers (&canvas, x + column_width * 1, y + 9, totals[0][1], totals[0][0]);
          print_numbers (&canvas, x + column_width * 2, y + 9, totals[1][1], totals[1][0]);
          print_numbers (&canvas, x + column_width * 3, y + 9, totals[2][1], totals[2][0]);
          print_numbers (&canvas, x + column_width * 4, y + 9, totals[3][1], totals[3][0]);
        }
      else
        {
          print_number (&canvas, x + column_width * 1, y + 9, totals[0][0], totals[0][0]);
          print_number (&canvas, x + column_width * 2, y + 9, totals[1][0], totals[1][0]);
          print_number (&canvas, x + column_width * 3, y + 9, totals[2][0], totals[2][0]);
          print_number (&canvas, x + column_width * 4, y + 9, totals[3][0], totals[3][0]);
        }
    }

  y = graph_height + global_min * (graph_height - 1) / (global_max - global_min) - 1;

  draw_line (&canvas, graph_x, y + graph_y, graph_x + graph_width - 1, y + graph_y, 0);

  write_png (png_path, canvas.width, canvas.height, canvas.data);

  free (png_path);
  free (canvas.data);
}

double
cdef_eval (const struct rrd_iterator* iterator, size_t index, void* vargs)
{
  struct cdef_run_args* args = vargs;

  int sp = 0;

  struct cdef_script* script = args->script;
  double* stack = alloca (sizeof (double) * script->max_stack_size);

  size_t i;
  for (i = 0; i < script->token_count; ++i)
    {
      switch (script->tokens[i].type)
        {
        case cdef_plus:

          --sp;
          assert(sp > 0);
          stack[sp - 1] += stack[sp];

          break;

        case cdef_minus:

          --sp;
          assert(sp > 0);
          stack[sp - 1] += stack[sp];

          break;

        case cdef_mul:

          --sp;
          assert(sp > 0);
          stack[sp - 1] *= stack[sp];

          break;

        case cdef_div:

          --sp;
          assert(sp > 0);
          if(!isfinite(stack[sp - 1]) || !isfinite(stack[sp]))
            stack[sp - 1] = NAN;
          else
            stack[sp - 1] /= stack[sp];

          break;

        case cdef_mod:

          --sp;
          assert(sp > 0);
          if(!isfinite(stack[sp - 1]) || !isfinite(stack[sp]))
            stack[sp - 1] = NAN;
          else
            stack[sp - 1] = fmod(stack[sp - 1], stack[sp]);

          break;

        case cdef_IF:

          sp -= 2;
          assert(sp > 0);

          if (stack[sp - 1])
            stack[sp - 1] = stack[sp];
          else
            stack[sp - 1] = stack[sp + 1];

          break;

        case cdef_UN:

          assert(sp > 0);
          if (isnan (stack[sp - 1]))
            stack[sp - 1] = 1;
          else
            stack[sp - 1] = 0;

          break;

        case cdef_TIME:

          return NAN;

        case cdef_LE:

          --sp;
          assert(sp > 0);
          stack[sp - 1] = (stack[sp - 1] <= stack[sp]);

          break;

        case cdef_GE:

          --sp;
          assert(sp > 0);
          stack[sp - 1] = (stack[sp - 1] >= stack[sp]);

          break;

        case cdef_constant:

          assert(sp < script->max_stack_size);
          stack[sp++] = script->tokens[i].v.constant;

          break;

        case cdef_curve:

            {
              const struct curve* ref_c;
              const struct rrd_iterator *ref_iterator;

              ref_c = script->tokens[i].v.curve;

              /* Avoid calling self-referencing CDEFs recursively */
              ref_iterator
                = (ref_c == args->c) ? &ref_c->work.iterator[args->name]
                : &ref_c->work.eff_iterator[args->name];

              if(!ref_iterator->count)
                {
                  static int first = 1;

                  if(first)
                    {
                      first = 0;

                      fprintf(stderr, "Software error: Iterator %p has zero length\n", ref_iterator);

                      if(ref_c == args->c)
                        fprintf(stderr, "CDEF token is self-referencing\n");

                      fprintf(stderr, "Referenced curve is '%s'\n", ref_c->path);
                    }

                  return NAN;
                }

              assert(sp < script->max_stack_size);
              stack[sp++] = rrd_iterator_peek_index(ref_iterator, index);
            }

          break;
        }
    }

  if (sp > 0)
    return stack[sp - 1];

  return NAN;
}

void
cdef_create_iterator (struct rrd_iterator* result, struct graph* g, struct curve* c, enum iterator_name name, size_t max_count)
{
  size_t min_count = 1000, i;
  struct cdef_run_args* args;
  struct cdef_script* script;

  memset (result, 0, sizeof (*result));
  script = &c->work.script;

  args = &c->work.script_args[name];
  args->script = script;
  args->name = name;
  args->c = c;

  for (i = 0; i < script->token_count; ++i)
    {
      if (script->tokens[i].type == cdef_curve)
        {
          const struct curve* ref_c;

          ref_c = script->tokens[i].v.curve;

          if (ref_c->work.iterator[name].count < min_count)
            min_count = ref_c->work.iterator[name].count;
        }
    }

  if (min_count > max_count)
    result->first = min_count - max_count;

  result->count = min_count;
  result->generator = cdef_eval;
  result->generator_arg = args;
}

int
cdef_compile (struct cdef_script* target, struct graph* g, const char* string)
{
  char* buf;
  char* token;
  char* saveptr;
  size_t i = 0;
  size_t stack_size = 0;

  target->token_count = 0;
  target->max_stack_size = 0;

  if (!*string)
    return 0;

  ++target->token_count;

  for (token = (char*) string; *token; ++token)
    {
      if (*token == ',')
        ++target->token_count;
    }

  target->tokens = calloc (sizeof (struct cdef_token), target->token_count);

  buf = alloca (strlen (string) + 1);
  strcpy (buf, string);

  token = strtok_r (buf, ",", &saveptr);

  while (token)
    {
      struct cdef_token* ct;
      const struct curve* c;
      double value;
      size_t argc = 0;
      char* endptr;

      ct = &target->tokens[i++];

      if (!strcmp (token, "+"))
        {
          ct->type = cdef_plus;
          argc = 2;
        }
      else if (!strcmp (token, "-"))
        {
          ct->type = cdef_minus;
          argc = 2;
        }
      else if (!strcmp (token, "*"))
        {
          ct->type = cdef_mul;
          argc = 2;
        }
      else if (!strcmp (token, "/"))
        {
          ct->type = cdef_div;
          argc = 2;
        }
      else if (!strcmp (token, "%"))
        {
          ct->type = cdef_mod;
          argc = 2;
        }
      else if (!strcmp (token, "IF"))
        {
          ct->type = cdef_IF;
          argc = 3;
        }
      else if (!strcmp (token, "UN"))
        {
          ct->type = cdef_UN;
          argc = 1;
        }
      else if (!strcmp (token, "UNKN"))
        {
          ct->type = cdef_constant;
          ct->v.constant = NAN;
        }
      else if (!strcmp (token, "INF"))
        {
          ct->type = cdef_constant;
          ct->v.constant = INFINITY;
        }
      else if (!strcmp (token, "TIME"))
        {
          ct->type = cdef_TIME;
        }
      else if (!strcmp (token, "LE"))
        {
          ct->type = cdef_LE;
          argc = 2;
        }
      else if (!strcmp (token, "GE"))
        {
          ct->type = cdef_GE;
          argc = 2;
        }
      else if (value = strtod (token, &endptr), !*endptr) /* Observe use of ',' */
        {
          ct->type = cdef_constant;
          ct->v.constant = value;
        }
      else if (0 != (c = find_curve (g, token)))
        {
          ct->type = cdef_curve;
          ct->v.curve = c;
        }
      else
        {
          fprintf (stderr, "Parse error in CDEF '%s' for '%s': Unknown token '%s'\n", string, g->name, token);

          return -1;
        }

      if (stack_size < argc)
        {
          fprintf (stderr, "Parse error in CDEF '%s' for '%s': %s called with less than %zu parameters\n", string, g->name, token, argc);

          return -1;
        }

      stack_size -= argc;
      ++stack_size;

      if (stack_size > target->max_stack_size)
        target->max_stack_size = stack_size;

      token = strtok_r (0, ",", &saveptr);
    }

  return 0;
}
