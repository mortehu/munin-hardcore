bin_PROGRAMS = munin-hardcore-graph
noinst_LIBRARIES = libmuningraph.a
check_PROGRAMS = graph-check

TESTS = graph-check

AM_CFLAGS = -g -O3 -Wall -std=c99
AM_CPPFLAGS = -I/usr/include/freetype2 -D_GNU_SOURCE

munin_hardcore_graph_SOURCES = munin-hardcore-graph.c
munin_hardcore_graph_LDFLAGS = -lpng -lfreetype -lm
munin_hardcore_graph_LDADD = libmuningraph.a

graph_check_SOURCES = graph-check.c
graph_check_LDFLAGS = -lpng -lfreetype -lm
graph_check_LDADD = libmuningraph.a

libmuningraph_a_SOURCES = graph.c png.c font.c font.h draw.c draw.h rrd.c rrd.h
