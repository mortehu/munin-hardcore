#ifndef RRD_H_
#define RRD_H_ 1

#include <stdint.h>

union unival
{
  unsigned long u_count;
  double        u_val;
};

enum cf_type
{
  cf_average = 0,
  cf_minimum,
  cf_maximum,
  cf_last
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

struct rrd_iterator
{
  const double* values;
  size_t ds;
  size_t offset;
  size_t first;
  size_t count;
  size_t step;

  size_t current_position;
};

#define rrd_iterator_pop(i) \
    ((i)->values[(i)->offset + (((i)->current_position++ + (i)->first) % (i)->count) * (i)->step + (i)->ds])

#define rrd_iterator_peek(i) \
    ((i)->values[(i)->offset + (((i)->current_position + (i)->first) % (i)->count) * (i)->step + (i)->ds])

#define rrd_iterator_last(i) \
    ((i)->values[(i)->offset + (((i)->current_position + (i)->first + ((i)->count - 1)) % (i)->count) * (i)->step + (i)->ds])

#define rrd_iterator_advance(i) \
    do { ++(i)->current_position; } while(0)

int
rrd_parse(struct rrd* result, const char* filename);

void
rrd_free(struct rrd* data);

int
rrd_iterator_create(struct rrd_iterator* result, const struct rrd* data,
                    const char* cf_name, size_t interval,
                    size_t max_count);

#endif /* !RRD_H_ */
