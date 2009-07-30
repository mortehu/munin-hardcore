#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <unistd.h>

#include "types.h"

int
write_png(const char *file_name, size_t width, size_t height, unsigned char* data);

unsigned char canvas[1024][495][3];

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

  if(memcmp("0003", result->header.version, 4))
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

  if(1 != fread(&result->live_header, sizeof(result->live_header), 1, f))
    errx(EXIT_FAILURE, "Error reading live header from '%s': %s", filename, strerror(errno));

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

double*
rrd_get_data(struct rrd* data, const char* ds_name, const char* cf_name, size_t pdp_count, size_t* rra_index)
{
  size_t i, j = 0;
  size_t ds, rra, data_offset = 0;
  double* result;

  for(ds = 0; ds < data->header.ds_count; ++ds)
  {
    if(!strcmp(data->ds_defs[ds].ds_name, ds_name))
      break;
  }

  if(ds == data->header.ds_count)
    return 0;

  for(rra = 0; rra < data->header.rra_count; ++rra)
  {
    if(data->rra_defs[rra].pdp_count == pdp_count
    && !strcmp(data->rra_defs[rra].cf_name, cf_name))
    {
      result = malloc(sizeof(double) * data->rra_defs[rra].row_count);

      for(i = data->rra_ptrs[rra]; i < data->rra_defs[rra].row_count; ++i, ++j)
      {
        result[j] = data->values[data_offset + i * data->header.ds_count + ds];
      }

      for(i = 0; i < data->rra_ptrs[rra]; ++i, ++j)
      {
        result[j] = data->values[data_offset + i * data->header.ds_count + ds];
      }

      *rra_index = rra;

      return result;
    }

    data_offset += data->rra_defs[rra].row_count * data->header.ds_count;
  }

  return 0;
}

static struct graph* graphs;
static size_t graph_count;
static size_t graph_alloc;

static const char* tmpldir = "/etc/munin/templates";
static const char* htmldir = "/var/www/munin";
static const char* dbdir = "/var/lib/munin";
static const char* rundir = "/var/run/munin";
static const char* logdir = "/var/log/munin";

#define DATA_FILE "/var/lib/munin/datafile"
#define DATA_FILE_SIGNATURE "version 1.2.6\n"

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
main(int argc, char** argv)
{
  FILE* f;
  size_t data_size;
  char* data;
  char* in;
  size_t lineno = 2;
  size_t graph, curve;

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

  if(data_size < 14 || memcmp(data, "version 1.2.6\n", 14))
    errx(EXIT_FAILURE, "Unsupported version signature at start of '%s'", DATA_FILE);

  in = data + 14;

  while(*in)
  {
    char* key_start;
    char* line_end;
    char* key_end;
    char* value_start;

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
    else
    {
      char* domain_end;
      char* host_start;
      char* host_end;
      char* graph_start;
      char* graph_end;
      char* graph_key;

      domain_end = strchr(key_start, ';');

      if(!domain_end)
        errx(EXIT_FAILURE, "Parse error at line %zu in '%s'.  Did not find a ; character in key", lineno, DATA_FILE);

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
      else
      {
        graph_end = strchr(graph_start, '.');

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
        else if(!strcmp(graph_key, "graph_period"))
          graphs[graph].period = value_start;
        else if(!strcmp(graph_key, "graph_total"))
          graphs[graph].total = value_start;
        else
        {
          char* curve_start;
          char* curve_end;

          curve_start = graph_key;
          graph_key = strchr(curve_start, '.');
          *graph_key++ = 0;

          if(!graph_key)
            errx(EXIT_FAILURE, "Parse error at line %zu in '%s'.  Did not find a . character after curve name", lineno, DATA_FILE);

          curve = curve_index(&graphs[graph], curve_start);

          if(!strcmp(graph_key, "label"))
            graphs[graph].curves[curve].label = value_start;
          else if(!strcmp(graph_key, "draw"))
            graphs[graph].curves[curve].draw = value_start;
          else if(!strcmp(graph_key, "graph"))
            graphs[graph].curves[curve].graph = value_start;
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
          else if(!strcmp(graph_key, "warning"))
            graphs[graph].curves[curve].warning = strtod(value_start, 0);
          else if(!strcmp(graph_key, "critical"))
            graphs[graph].curves[curve].critical = strtod(value_start, 0);
          else
          {
            fprintf(stderr, "Mediator: %s  Host: %s  Graph: %s  Curve: %s  Key: %s  Value: %s\n", key_start, host_start, graph_start, curve_start, graph_key, value_start);
          }
        }
      }
    }

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
      char* path;

      if(!c->type || !strcasecmp(c->type, "gauge"))
        suffix = 'g';
      else if(!strcasecmp(c->type, "derive"))
        suffix = 'd';
      else if(!strcasecmp(c->type, "counter"))
        suffix = 'c';
      else
        errx(EXIT_FAILURE, "Unknown curve type '%s'", c->type);

      if(-1 == asprintf(&path, "%s/%s/%s-%s-%s-%c.rrd", dbdir, g->domain, g->host, g->name, c->name, suffix))
        errx(EXIT_FAILURE, "asprintf failed while building RRD path: %s", strerror(errno));

      rrd_parse(&c->data, path);

      free(path);
    }
  }
#if 0
  memset(canvas, 0xff, sizeof(canvas));

  struct rrd data;

  rrd_parse(&data, "wormwood-iostat-dev8_0_write-d.rrd");

  double* values;
  size_t i, rra;

  values = rrd_get_data(&data, "42", "AVERAGE", 1, &rra);

  if(!values)
    errx(EXIT_FAILURE, "Data source not found");

  for(i = 0; i < data.rra_defs[rra].row_count; ++i)
    printf("%6.2f\n", values[i]);

  free(values);

  rrd_free(&data);
#endif

  return EXIT_SUCCESS;
}
