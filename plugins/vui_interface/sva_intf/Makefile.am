pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = sva_interface.pc
EXTRA_DIST = $(pkgconfig_DATA)

h_sources = $(top_srcdir)/sva_intf/inc/SVAInterface.h


AM_CPPFLAGS := -DVUI_USE_SYSLOG @GLIB_CFLAGS@ -Dstrlcpy=g_strlcpy -Dstrlcat=g_strlcat -include glib.h

library_include_HEADERS = $(h_sources)
library_includedir = $(includedir)/sva_interface

AM_CPPFLAGS += @SPFHEADER_CFLAGS@
AM_CPPFLAGS += @VUIINTERFACEHEADERS_CFLAGS@
AM_CPPFLAGS += @KVH2XML_CFLAGS@
AM_CPPFLAGS += @PALHEADERS_CFLAGS@
AM_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\)
AM_CPPFLAGS += -I $(top_srcdir)/sva_intf/src/
AM_CPPFLAGS += -I $(top_srcdir)/sva_intf/inc/
AM_CPPFLAGS += -I $(top_srcdir)/utils/inc/


c_sources := $(top_srcdir)/sva_intf/src/SVAInterface.cpp

lib_LTLIBRARIES =  libsva_intf.la
libsva_intf_la_SOURCES = $(c_sources)
libsva_intf_la_CPPFLAGS = $(AM_CPPFLAGS)
libsva_intf_la_LDFLAGS  = -shared -avoid-version
libsva_intf_la_CPPFLAGS += -Dstrlcpy=g_strlcpy -Dstrlcat=g_strlcat
libsva_intf_la_LIBADD = -lar_osal @GLIB_LIBS@ $(top_builddir)/utils/libvuiinterface_utils.la
