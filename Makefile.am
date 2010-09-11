bin_PROGRAMS = munin-hardcore-graph munin-hardcore-node munin-hardcore-correlate
noinst_LIBRARIES = libmuningraph.a
check_PROGRAMS = graph-check

TESTS = graph-check

AM_CFLAGS = -g -O3 -Wall -std=c99
AM_CPPFLAGS = -I/usr/include/freetype2 -D_GNU_SOURCE

munin_hardcore_node_SOURCES = node.c

munin_hardcore_graph_SOURCES = munin-hardcore-graph.c
munin_hardcore_graph_LDFLAGS = -lpng -lfreetype
munin_hardcore_graph_LDADD = libmuningraph.a

munin_hardcore_correlate_SOURCES = munin-hardcore-correlate.c correlate.c corrmap.cc
munin_hardcore_correlate_LDFLAGS = -lpng -lfreetype
munin_hardcore_correlate_LDADD = libmuningraph.a

graph_check_SOURCES = graph-check.c
graph_check_LDFLAGS = -lpng -lfreetype
graph_check_LDADD = libmuningraph.a

libmuningraph_a_SOURCES = graph.c png.c font.c font.h draw.c draw.h rrd.c rrd.h
