#ifndef GRAPH_H_
#define GRAPH_H_ 1

#include <stdio.h>
#include <stdlib.h>

enum version
{
  ver_unknown,
  ver_1_2,
  ver_1_3
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

void
parse_datafile (char* in, const char *pathname);

void
process_graph (size_t graph_index);

#endif /* !GRAPH_H_ */
