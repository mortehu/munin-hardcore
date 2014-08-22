#ifndef GRAPH_H_
#define GRAPH_H_ 1

#include <stdio.h>
#include <stdlib.h>

enum version
{
  ver_unknown,
  ver_1_2,
  ver_1_3,
  ver_1_4,
  ver_2_0
};

extern enum version cur_version;

extern int debug;
extern int nolazy;

extern FILE* stats;

extern struct graph* graphs;
extern size_t graph_count;
extern size_t graph_alloc;

extern const char* tmpldir;
extern const char* htmldir;
extern const char* dbdir;
extern const char* rundir;
extern const char* logdir;

struct cdef_script;
struct rrd_iterator;
struct curve;
enum iterator_name;

void
parse_datafile (char* in, const char *pathname);

int
cdef_compile (struct cdef_script* target, struct graph* g, const char* string);

double
cdef_eval (const struct rrd_iterator* iterator, size_t index, void* vargs);

void
cdef_create_iterator (struct rrd_iterator* result, struct graph* g, struct curve* c, enum iterator_name name, size_t max_count);

ssize_t
find_graph (const char* domain, const char* host, const char* name, int create);

const struct curve*
find_curve (const struct graph* g, const char* name);

const char*
strword (const char* haystack, const char* needle);

void
process_graph (size_t graph_index);

void
process_correlations (size_t graph_index);

void
corrmap_add (const char *host, const char *subject, const char *source, time_t t, double v);

void
corrmap_process ();

#endif /* !GRAPH_H_ */
