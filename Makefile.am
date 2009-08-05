bin_PROGRAMS = munin-hardcore-graph munin-hardcore-node

AM_CFLAGS = -g -Wall -O3 -std=c99
AM_CPPFLAGS = -I/usr/include/freetype2 -D_GNU_SOURCE

munin_hardcore_node_SOURCES = node.c

munin_hardcore_graph_SOURCES = \
  graph.c \
  png.c \
  font.c font.h \
  draw.c draw.h \
  rrd.c rrd.h
munin_hardcore_graph_LDFLAGS = -lpng -lfreetype
