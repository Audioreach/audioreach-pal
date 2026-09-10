pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = stream_acdb.pc
EXTRA_DIST = $(pkgconfig_DATA)


AM_CPPFLAGS := -I ./inc
AM_CPPFLAGS += -I ../../inc
AM_CPPFLAGS += -I ../../
AM_CPPFLAGS += -I ../../utils/inc
AM_CPPFLAGS += -I ../../device/inc
AM_CPPFLAGS += -I ../../session/inc
AM_CPPFLAGS += -I ../../stream/inc
AM_CPPFLAGS += -I ../../session/SessionAR/inc
AM_CPPFLAGS += -I ../../plugins/PluginManager/inc
AM_CPPFLAGS += -I ../../resource_manager/inc
AM_CPPFLAGS += -I ../../context_manager/inc


stream_acdb_sources = ./src/StreamACDB.cpp

library_includedir = $(includedir)/stream_acdb

lib_LTLIBRARIES     = libstream_acdb.la
libstream_acdb_la_SOURCES   = $(stream_acdb_sources)
libstream_acdb_la_LIBADD    = @GLIB_LIBS@ -llog -lcutils -lexpat @GSL_LIBS@
libstream_acdb_la_LIBADD    += -L${abs_top_builddir}/session/SessionAR/.libs -lsession_ar
libstream_acdb_la_CPPFLAGS := $(AM_CPPFLAGS)
libstream_acdb_la_CPPFLAGS += -std=c++14
libstream_acdb_la_LDFLAGS   = -shared -avoid-version
libstream_acdb_la_CPPFLAGS += @GLIB_CFLAGS@ -include glib.h
libstream_acdb_la_CPPFLAGS += -DACD_SM_FILEPATH=\"/etc/models/acd/\"
libstream_acdb_la_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\) -DLINUX_ENABLED

if BUILDSYSTEM_OPENWRT
libstream_acdb_la_LIBADD += -lglib-2.0
endif