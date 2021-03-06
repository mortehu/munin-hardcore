#ifndef MUNIH_H_
#define MUNIH_H_ 1

#include "rrd.h"

enum cdef_token_type
{
  cdef_plus, cdef_minus, cdef_mul, cdef_div, cdef_mod, cdef_IF, cdef_UN,
  cdef_TIME, cdef_LE, cdef_GE, cdef_constant, cdef_curve
};

enum iterator_name
{
  average = 0, min = 1, max = 2
};

struct cdef_token
{
  enum cdef_token_type type;

  union
    {
      double constant;
      const struct curve* curve;
    } v;
};

struct cdef_script
{
  struct cdef_token* tokens;
  size_t token_count;
  size_t max_stack_size;
};

struct cdef_run_args
{
  struct cdef_script* script;
  struct curve* c;
  enum iterator_name name;
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

  uint32_t color;
  int has_color;

  double max, min;
  int has_min, has_max;

  double warning, critical;

  struct
    {
      struct cdef_script script;

      double cur, max, min, avg;
      double max_avg, min_avg;
      const struct curve* negative;

      struct rrd_iterator iterator[3];
      struct rrd_iterator eff_iterator[3];
      struct cdef_run_args script_args[3];
    } work;
};

struct graph
{
  const char* domain;
  const char* host;
  const char* name;

  char *name_png_path;
  char *name_rrd_path;

  const char* title;
  const char* category;
  const char* info;
  const char* order;
  const char* period;
  int noscale;
  const char* total;
  const char* vlabel;
  int nograph;

  int base;
  int precision;
  int has_lower_limit;
  double lower_limit;
  int has_upper_limit;
  double upper_limit;
  int logarithmic;

  size_t width, height;

  struct curve* curves;
  size_t curve_count;
  size_t curve_alloc;
};

#endif /* !MUNIH_H_ */
