#ifndef MUNIH_H_
#define MUNIH_H_ 1

#include "rrd.h"

enum cdef_token_type
{
  cdef_plus, cdef_minus, cdef_mul, cdef_div, cdef_IF, cdef_UN, cdef_TIME,
  cdef_LE, cdef_GE, cdef_constant, cdef_curve
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

    struct rrd_iterator iterator_average;
    struct rrd_iterator iterator_min;
    struct rrd_iterator iterator_max;
  } work;
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
  int nograph;

  size_t width, height;

  struct curve* curves;
  size_t curve_count;
  size_t curve_alloc;
};

#endif /* !MUNIH_H_ */
