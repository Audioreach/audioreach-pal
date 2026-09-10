pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = vui_interface_utils.pc
EXTRA_DIST = $(pkgconfig_DATA)

h_sources = $(top_srcdir)/utils/inc/VUIInterfaceUtils.h


AM_CPPFLAGS := -DPAL_USE_SYSLOG @GLIB_CFLAGS@

if WITH_GLIB
AM_CPPFLAGS += -include glib.h
endif

AM_CPPFLAGS += -I $(top_srcdir)/utils/src/
AM_CPPFLAGS += -I $(top_srcdir)/utils/inc/
AM_CPPFLAGS += -I $(top_srcdir)/api/vui-interface/
AM_CPPFLAGS += -I $(top_srcdir)/../../

library_include_HEADERS = $(h_sources)
library_includedir = $(includedir)/vui_interface_utils


AM_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\)

c_sources := $(top_srcdir)/utils/src/VUIInterfaceUtils.cpp

lib_LTLIBRARIES = libvuiinterface_utils.la
libvuiinterface_utils_la_SOURCES = $(c_sources)
libvuiinterface_utils_la_CPPFLAGS = $(AM_CPPFLAGS)
libvuiinterface_utils_la_LDFLAGS  = -shared -version-number @LT_VERSION_NUMBER@

