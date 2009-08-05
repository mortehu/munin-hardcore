#ifndef TYPES_H_
#define TYPES_H_ 1

#include <stdint.h>

union unival
{
  unsigned long u_count;
  double        u_val;
};

struct rrd_header
{
  char          cookie[4];
  char          version[5];
  double        float_cookie;
  unsigned long ds_count;
  unsigned long rra_count;
  unsigned long pdp_step;
  union unival  par[10];
};

struct ds_def
{
  char         ds_name[20];
  char         dst[20];
  union unival par[10];
};

struct rra_def
{
  char          cf_name[20];
  unsigned long row_count;
  unsigned long pdp_count;
  union unival  par[10];
};

struct live_header
{
  time_t        last_up;
  unsigned long last_up_usec;
};

struct pdp_prepare
{
  char         last_ds[30];
  union unival scratch[10];
};

struct cdp_prepare
{
  union unival scratch[10];
};

struct rrd
{
  struct rrd_header header;
  struct ds_def* ds_defs;
  struct rra_def* rra_defs;
  struct live_header live_header;
  struct pdp_prepare* pdp_preps;
  struct cdp_prepare* cdp_preps;
  unsigned long* rra_ptrs;
  double* values;
};

struct curve
{
  char* path;
  struct rrd data;

  const char* name;
  const char* label;
  const char* draw;
  const char* type;
  const char* info;
  const char* cdef;
  const char* negative;
  int nograph;

  double max, min, warning, critical;
};

struct graph
{
  const char* domain;
  const char* host;
  const char* name;

  const char* title;
  const char* args;
  const char* category;
  const char* info;
  const char* order;
  const char* period;
  const char* scale;
  const char* total;
  const char* vlabel;

  size_t width, height;

  struct curve* curves;
  size_t curve_count;
  size_t curve_alloc;
};

struct canvas
{
  unsigned char* data;
  size_t width, height;
};

#endif /* !TYPES_H_ */
