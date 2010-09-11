#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <sysexits.h>

#include "munin.h"
#include "graph.h"

static void
do_process (struct graph* g, size_t interval, const char* suffix)
{
  size_t x;
  size_t i, curve;
  size_t graph_width;

  graph_width = g->width ? g->width : 400;

  for (curve = 0; curve < g->curve_count; ++curve)
    {
      struct curve* c = &g->curves[curve];
      struct rrd_iterator iterator_average;
      time_t t;
      
      t = c->data.live_header.last_up - graph_width * interval;

      if (!c->data.header.ds_count)
        continue;

      if (-1 == rrd_iterator_create (&iterator_average, &c->data, "AVERAGE", interval, graph_width))
        errx (EXIT_FAILURE, "Did not find all required round robin archives in '%s'", c->path);

      if (c->cdef)
        {
          if (-1 == cdef_compile (&c->work.script, g, c->cdef))
              return;

          for (i = 0; i < 3; ++i)
            cdef_create_iterator (&iterator_average, g, c, i, graph_width);
        }

      for (i = 0, x = 0; i < iterator_average.count && x < graph_width; ++i, ++x, t += interval)
        {
          double avg_value = rrd_iterator_peek (&iterator_average);

          rrd_iterator_advance (&iterator_average);

          if (!isnan (avg_value))
            corrmap_add (g->host, c->label ? c->label : c->name, g->name, t, avg_value);
        }
    }
}

void
process_correlations (size_t graph_index)
{
  struct graph* g = &graphs[graph_index];
  size_t curve;

  int curve_terminator;

  curve_terminator = (cur_version < ver_1_3) ? '.' : ';';

  if (g->nograph)
      return;

  for (curve = 0; curve < g->curve_count; )
    {
      struct curve* c = &g->curves[curve];

      const struct graph* eff_g = g;
      const struct curve* eff_c = c;

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
                *strchr (curve_name, ' ' ) = 0;

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

      int suffix;

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

      if (-1 == asprintf (&c->path, "%s/%s/%s-%s-%s-%c.rrd", dbdir, eff_g->domain, eff_g->host, eff_g->name, eff_c->name, suffix))
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
      do_process (g, 300, "day");
      /*
      do_process (g, 1800, "week");
      do_process (g, 7200, "month");
      do_process (g, 86400, "year");
      */

      for (curve = 0; curve < g->curve_count; ++curve)
        {
          if (g->curves[curve].data.file_size)
            rrd_free (&g->curves[curve].data);

          free (g->curves[curve].work.script.tokens);
          free (g->curves[curve].path);
        }
    }
}
