pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = vui_interface_utils.pc
EXTRA_DIST = $(pkgconfig_DATA)

h_sources = $(top_srcdir)/utils/inc/VoiceUIInterfaceUtils.h


AM_CPPFLAGS := -DVUI_USE_SYSLOG @GLIB_CFLAGS@ -Dstrlcpy=g_strlcpy -Dstrlcat=g_strlcat -include glib.h
AM_CPPFLAGS += -I $(top_srcdir)/utils/src/
AM_CPPFLAGS += -I $(top_srcdir)/utils/inc/
AM_CPPFLAGS += @VUIINTERFACEHEADERS_CFLAGS@
AM_CPPFLAGS += @PALHEADERS_CFLAGS@

library_include_HEADERS = $(h_sources)
library_includedir = $(includedir)/vui_interface_utils


AM_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\)

c_sources := $(top_srcdir)/utils/src/VoiceUIInterfaceUtils.cpp

lib_LTLIBRARIES = libvuiinterface_utils.la
libvuiinterface_utils_la_SOURCES = $(c_sources)
libvuiinterface_utils_la_CPPFLAGS = $(AM_CPPFLAGS)
libvuiinterface_utils_la_LDFLAGS  = -shared -avoid-version
libvuiinterface_utils_la_CPPFLAGS += -Dstrlcpy=g_strlcpy -Dstrlcat=g_strlcat

