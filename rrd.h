#ifndef RRD_H_
#define RRD_H_ 1

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
  void* data;
  off_t file_size;

  struct rrd_header header;
  struct ds_def* ds_defs;
  struct rra_def* rra_defs;
  struct live_header live_header;
  struct pdp_prepare* pdp_preps;
  struct cdp_prepare* cdp_preps;
  unsigned long* rra_ptrs;
  double* values;
};

int
rrd_parse(struct rrd* result, const char* filename);

void
rrd_free(struct rrd* data);

#endif /* !RRD_H_ */
