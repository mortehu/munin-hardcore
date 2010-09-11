/*  Entry point for munin-hardcore-graph.
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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <err.h>
#include <getopt.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>

#include "font.h"
#include "graph.h"
#include "munin.h"

static int cpu_count = 1;

static const struct option long_options[] =
{
    { "data-file", required_argument, 0, 'd' },
    { "debug",   no_argument, &debug, 1 },
    { "no-lazy", no_argument, &nolazy, 1 },
    { "help",    no_argument, 0, 'h' },
    { "version", no_argument, 0, 'v' },
    { 0, 0, 0, 0 }
};

static const char* datafile = "/var/lib/munin/datafile";
static struct graph *last_graph;

static void
help (const char* argv0)
{
  printf ("Usage: %s [OPTION]...\n"
         "batch plotting of RRD data files\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short"
         " options too\n"
         "\n"
         "     --data-file=FILE       load graph information from FILE\n"
         " -d, --debug                print debug messages\n"
         " -n, --no-lazy              redraw every single graph\n"
         "     --help     display this help and exit\n"
         "     --version  display version information and exit\n"
         "\n"
         "Report bugs to <morten@rashbox.org>.\n", argv0);
}

int
graph_cmp (const void* plhs, const void* prhs)
{
  const struct graph* lhs = plhs;
  const struct graph* rhs = prhs;
  int result;

  if (0 != (result = strcmp (lhs->domain, rhs->domain)))
    return result;

  return strcmp (lhs->name, rhs->name);
}

void
sigsegvhandler(int signal)
{
  static int first = 1;
  size_t i;

  if (!first)
    exit (EX_SOFTWARE);

  first = 0;

  if (last_graph)
    {
      fprintf (stderr, "Segmentation fault while processing graph. domain=%s host=%s graph=%s\n",
               last_graph->domain, last_graph->host, last_graph->name);

      for (i = 0; i < last_graph->curve_count; ++i)
        {
          struct curve *c;

          c = &last_graph->curves[i];

          fprintf(stderr, "Data source #%zu: %s\n", i, c->path);

          if (c->draw)     fprintf (stderr, "  draw=%s\n", c->draw);
          if (c->negative) fprintf (stderr, "  negative=%s\n", c->negative);
          if (c->cdef)     fprintf (stderr, "  cdef=%s\n", c->cdef);
        }
    }
  else
    fprintf (stderr, "Segmentation fault while not processing graph\n");

  exit (EX_SOFTWARE);
}

int
main (int argc, char** argv)
{
  unsigned int ver_major, ver_minor, ver_patch;
  FILE* f;
  size_t i, data_size;
  char* data;
  char* in;
  char* line_end;
  pid_t *children;

  cpu_count = sysconf (_SC_NPROCESSORS_ONLN);

  signal (SIGSEGV, sigsegvhandler);

  for (;;)
    {
      int optindex = 0;
      int c;

      c = getopt_long (argc, argv, "dn", long_options, &optindex);

      if (c == -1)
        break;

      switch (c)
        {
        case 'd':

          datafile = optarg;

          break;

        case 'h':

          help (argv[0]);

          return EXIT_SUCCESS;

        case 'v':

          printf ("%s-graph %s\n", PACKAGE_NAME, PACKAGE_VERSION);
          printf ("Copyright © 2009 Morten Hustveit\n"
                 "This is free software.  You may redistribute copies of it under the terms of\n"
                 "the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n"
                 "There is NO WARRANTY, to the extent permitted by law.\n"
                 "\n"
                 "Authors:\n"
                 "  Morten Hustveit\n");

          return EXIT_SUCCESS;

        case '?':

          fprintf (stderr, "Try `%s --help' for more information.\n", argv[0]);

          return EXIT_FAILURE;
        }
    }

  if (optind != argc)
    {
      printf ("Usage: %s [OPTION]...\n", argv[0]);
      fprintf (stderr, "Try `%s --help' for more information.\n", argv[0]);

      return EXIT_FAILURE;
    }

  if (cpu_count < 1)
    cpu_count = 1;

  stats = fopen ("/var/lib/munin/munin-graph.stats", "w");

  if (!stats && debug)
    fprintf (stderr, "Failed to open /var/lib/munin/munin-graph.stats for writing: %s\n", strerror (errno));

  struct timeval total_start, total_end;

  gettimeofday (&total_start, 0);

  if (!(f = fopen (datafile, "r")))
    errx (EXIT_FAILURE, "Failed to open '%s' for reading: %s", datafile, strerror (errno));

  if (-1 == (fseek (f, 0, SEEK_END)))
    errx (EXIT_FAILURE, "Failed to seek to end of '%s': %s", datafile, strerror (errno));

  data_size = ftell (f);

  if (-1 == (fseek (f, 0, SEEK_SET)))
    errx (EXIT_FAILURE, "Failed to seek to start of '%s': %s", datafile, strerror (errno));

  data = malloc (data_size + 1);

  if (data_size != fread (data, 1, data_size, f))
    errx (EXIT_FAILURE, "Error reading %zu bytes from '%s': %s", (size_t) data_size, datafile, strerror (errno));

  fclose (f);

  data[data_size] = 0;

  in = data;
  line_end = strchr (in, '\n');

  if (!line_end)
    errx (EXIT_FAILURE, "No newlines in '%s'", datafile);

  if (3 != sscanf (in, "version %u.%u.%u\n", &ver_major, &ver_minor, &ver_patch))
    errx (EXIT_FAILURE, "Unsupported version signature at start of '%s'", datafile);

  if (ver_major == 1 && ver_minor == 2)
    cur_version = ver_1_2;
  else if (ver_major == 1 && ver_minor == 3)
    cur_version = ver_1_3;

  if (cur_version == ver_unknown)
    errx (EXIT_FAILURE, "Unsupported version %u.%u.  I only support 1.2 to 1.3", ver_major, ver_minor);

  in = line_end + 1;

  parse_datafile (in, datafile);

  qsort (graphs, graph_count, sizeof (struct graph), graph_cmp);

  for (i = 0; i < graph_count; ++i)
    process_correlations (i);

  if (stats)
    {
      gettimeofday (&total_end, 0);

      fprintf (stats, "GT|total|%.3f\n",
              total_end.tv_sec - total_start.tv_sec + (total_end.tv_usec - total_start.tv_usec) * 1.0e-6);

      fclose (stats);
    }

  free (graphs);
  free (data);

  corrmap_process ();

  return EXIT_SUCCESS;
}
