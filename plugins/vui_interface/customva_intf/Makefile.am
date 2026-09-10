pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = customva_intf.pc
EXTRA_DIST = $(pkgconfig_DATA)

AM_CPPFLAGS := -I$(top_srcdir)/customva_intf/inc
AM_CPPFLAGS += @SPFHEADER_CFLAGS@
AM_CPPFLAGS += -I $(top_srcdir)/api/vui-interface/
AM_CPPFLAGS += @KVH2XML_CFLAGS@
AM_CPPFLAGS += -I $(top_srcdir)/../../inc
AM_CPPFLAGS += -I $(top_srcdir)/../../
AM_CPPFLAGS += -I $(top_srcdir)/utils/inc/

customva_intf_sources = $(top_srcdir)/customva_intf/src/CustomVA.cpp

library_includedir = $(includedir)/customva_intf

lib_LTLIBRARIES     = libcustomva_intf.la
libcustomva_intf_la_SOURCES   = $(customva_intf_sources)
libcustomva_intf_la_LIBADD    = @GLIB_LIBS@ -lexpat
libcustomva_intf_la_CPPFLAGS := $(AM_CPPFLAGS)
libcustomva_intf_la_CPPFLAGS += -std=c++14
libcustomva_intf_la_LDFLAGS  = -shared -version-number @LT_VERSION_NUMBER@
libcustomva_intf_la_CPPFLAGS += @GLIB_CFLAGS@
if WITH_GLIB
libcustomva_intf_la_CPPFLAGS += -include glib.h
endif
libcustomva_intf_la_CPPFLAGS += -DACD_SM_FILEPATH=\"/etc/models/acd/\"
libcustomva_intf_la_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\) -DLINUX_ENABLED -DPAL_USE_SYSLOG

if BUILDSYSTEM_OPENWRT
libcustomva_intf_la_LIBADD += -lglib-2.0
endif

if CUTILS_SUPPORTED
libcustomva_intf_la_CPPFLAGS += -DPAL_CUTILS_SUPPORTED
libcustomva_intf_la_LIBADD += -lcutils
endif