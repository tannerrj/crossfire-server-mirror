dist_noinst_SCRIPTS = mktable.script player_dl.html crossloop.web metaserver
dist_bin_SCRIPTS = cfdb_convert crossloop player_dl.pl

noinst_PROGRAMS = gridarta-types-convert
gridarta_types_convert_SOURCES = gridarta-types-convert.cpp

EXTRA_DIST =
AM_CPPFLAGS = -I$(top_srcdir)/include

CLEANFILES = crossloop

SUBDIRS = mapper

noinst_PROGRAMS = test_weather
test_weather_SOURCES = test_weather.cpp
test_weather_LDADD = ../server/libserver.a ../common/libcross.a ../random_maps/librandom_map.a \
		../socket/libsocket.a ../types/libtypes.a
