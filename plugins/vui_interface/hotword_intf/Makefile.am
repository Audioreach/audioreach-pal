pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = hotword_intf.pc
EXTRA_DIST = $(pkgconfig_DATA)

AM_CPPFLAGS := @SPFHEADER_CFLAGS@
AM_CPPFLAGS += -I $(top_srcdir)/api/vui-interface/
AM_CPPFLAGS += @KVH2XML_CFLAGS@
AM_CPPFLAGS += -I $(top_srcdir)/../../inc
AM_CPPFLAGS += -I $(top_srcdir)/../../
AM_CPPFLAGS += -I$(top_srcdir)/hotword_intf/inc

hotword_intf_sources = $(top_srcdir)/hotword_intf/src/HotwordVA.cpp

library_includedir = $(includedir)/hotword_intf

lib_LTLIBRARIES     = libhotword_intf.la
libhotword_intf_la_SOURCES   = $(hotword_intf_sources)
libhotword_intf_la_LIBADD    = @GLIB_LIBS@ -lexpat
libhotword_intf_la_CPPFLAGS := $(AM_CPPFLAGS)
libhotword_intf_la_CPPFLAGS += -std=c++14
libhotword_intf_la_LDFLAGS  = -shared -version-number @LT_VERSION_NUMBER@
libhotword_intf_la_CPPFLAGS += @GLIB_CFLAGS@
if WITH_GLIB
libhotword_intf_la_CPPFLAGS += -include glib.h
endif
libhotword_intf_la_CPPFLAGS += -DACD_SM_FILEPATH=\"/etc/models/acd/\"
libhotword_intf_la_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\) -DLINUX_ENABLED -DPAL_USE_SYSLOG

if BUILDSYSTEM_OPENWRT
libhotword_intf_la_LIBADD += -lglib-2.0
endif