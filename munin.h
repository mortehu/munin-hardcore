#ifndef MUNIH_H_
#define MUNIH_H_ 1

#include "rrd.h"


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

#endif /* !MUNIH_H_ */
