pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = sva_interface.pc
EXTRA_DIST = $(pkgconfig_DATA)

h_sources = $(top_srcdir)/sva_intf/inc/SVAInterface.h


AM_CPPFLAGS := -DPAL_USE_SYSLOG @GLIB_CFLAGS@

if WITH_GLIB
AM_CPPFLAGS += -include glib.h
endif

library_include_HEADERS = $(h_sources)
library_includedir = $(includedir)/sva_interface

AM_CPPFLAGS += @SPFHEADER_CFLAGS@
AM_CPPFLAGS += -I $(top_srcdir)/api/vui-interface/
AM_CPPFLAGS += @KVH2XML_CFLAGS@
AM_CPPFLAGS += -I $(top_srcdir)/../../inc
AM_CPPFLAGS += -I $(top_srcdir)/../../
AM_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
AM_CPPFLAGS += -I $(top_srcdir)/sva_intf/src/
AM_CPPFLAGS += -I $(top_srcdir)/sva_intf/inc/
AM_CPPFLAGS += -I $(top_srcdir)/utils/inc/


c_sources := $(top_srcdir)/sva_intf/src/SVAInterface.cpp

lib_LTLIBRARIES =  libsva_intf.la
libsva_intf_la_SOURCES = $(c_sources)
libsva_intf_la_CPPFLAGS = $(AM_CPPFLAGS)
libsva_intf_la_LDFLAGS  = -shared -version-number @LT_VERSION_NUMBER@
libsva_intf_la_LIBADD = -lar-osal @GLIB_LIBS@ $(top_builddir)/utils/libvuiinterface_utils.la
