#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <sysexits.h>
#include <unistd.h>

#include "font.h"
#include "graph.h"
#include "munin.h"

int
main (int argc, char **argv)
{
  struct graph *g;
  struct curve *c;
  size_t i, j, k;
  size_t iterations;

  font_init ();

  debug = 1;
  nolazy = 1;

  graph_alloc = 1;
  graphs = calloc (sizeof(*graphs), graph_alloc);
  graph_count = 1;

  for (iterations = 0; iterations < 100; ++iterations)
    {
      g = &graphs[0];

      g->domain = "test-domain";
      g->host = "test-host";
      g->name = "test-graph";

      g->title = "Test graph";
      g->args;
      g->category = "tests";
      g->info = "This is the test graph";
      g->order = "data02 data01 data00";
      g->period;
      g->noscale = rand() & 1;
      g->total = "Totals";;
      g->vlabel = "Vertical label";
      g->nograph = (rand() % 10) == 9;

      g->width = 400;
      g->height = 175;

      g->curve_alloc = 10;
      g->curves = calloc (sizeof(*g->curves), g->curve_alloc);

      g->curve_count = 10;

      for (i = 0; i < g->curve_count; ++i)
        {
          unsigned long rra_ptr = 0;

          c = &g->curves[i];

          memset (c, 0, sizeof (*c));

          if (-1 == asprintf ((char **) &c->name, "data%02d", i)
              || -1 == asprintf ((char **) &c->label, "Data source #%02d", i))
            err (EX_OSERR, "asprintf failed");

          switch (rand() % 3)
            {
            case 0: c->draw = "line2"; break;
            case 1: c->draw = "stack"; break;
            case 2: c->draw = "area"; break;
            }

          c->has_color = (rand() % 10) == 9;

          if (c->has_color)
            c->color = rand();

          if ((rand() % 5) == 4)
            {
              if (-1 == asprintf ((char **) &c->negative, "data%02d", rand() % 10))
                err (EX_OSERR, "asprintf failed");
            }

          c->data.data = c;
          c->data.file_size = 0;
          c->data.header.ds_count = 1;
          c->data.header.rra_count = 12;
          c->data.header.pdp_step = 300;

          c->data.live_header.last_up = 946681200;

          c->data.rra_defs = calloc (sizeof (struct ds_def), 12);

          c->data.rra_ptrs = calloc (sizeof (unsigned long), 12);
          c->data.values = calloc (sizeof (double), 576 * 12);

          for (j = 0; j < 12; ++j)
            {
              struct rra_def *rd;

              rd = &c->data.rra_defs[j];

              switch (j % 3)
                {
                case 0: strcpy (rd->cf_name, "AVERAGE"); break;
                case 1: strcpy (rd->cf_name, "MIN"); break;
                case 2: strcpy (rd->cf_name, "MAX"); break;
                }

              switch (j / 3)
                {
                case 0: rd->pdp_count = 1; break;
                case 1: rd->pdp_count = 6; break;
                case 2: rd->pdp_count = 24; break;
                case 3: rd->pdp_count = 288; break;
                }

              rd->row_count = 576;

              c->data.rra_ptrs[j] = rra_ptr;

              for (k = 0; k < 576; ++k)
                {
                  switch (i)
                    {
                    case 0: c->data.values[rra_ptr + k] = sin(k * 0.1 + i); break;
                    case 1: c->data.values[rra_ptr + k] = 0.1;  break;
                    case 2: c->data.values[rra_ptr + k] = (double) rand() / RAND_MAX - 0.5; break;
                    case 3: c->data.values[rra_ptr + k] = cos(k * 0.1 + i); break;
                    case 4: c->data.values[rra_ptr + k] = cos(k * 0.1 + i) * 0.9; break;
                    case 5: c->data.values[rra_ptr + k] = cos(k * 0.1 + i) * 0.8; break;
                    case 6: c->data.values[rra_ptr + k] = cos(k * 0.1 + i) * 0.7; break;
                    case 7: c->data.values[rra_ptr + k] = cos(k * 0.1 + i) * 0.6; break;
                    case 8: c->data.values[rra_ptr + k] = cos(k * 0.1 + i) * 0.5; break;
                    case 9: c->data.values[rra_ptr + k] = cos(k * 0.1 + i) * 0.4; break;
                    case 10: c->data.values[rra_ptr + k] = rand(); break;
                    case 11: c->data.values[rra_ptr + k] = NAN; break;
                    default: c->data.values[rra_ptr + k] = rand();  break;
                    }
                }

              rra_ptr += 576;
            }
        }

      htmldir = "./tests";

      process_graph (0);

      for (i = 0; i < g->curve_count; ++i)
        {
          c = &g->curves[i];

          free ((char *) c->name);
          free ((char *) c->label);

          free (c->data.rra_defs);
          free (c->data.rra_ptrs);
          free (c->data.values);
        }

      free (g->curves);
    }

  return EXIT_SUCCESS;
}
