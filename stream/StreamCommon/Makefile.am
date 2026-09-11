pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = stream_common.pc
EXTRA_DIST = $(pkgconfig_DATA)

if BUILDSYSTEM_OPENWRT
AM_CPPFLAGS := -I $(top_srcdir)/stream/StreamCommon/inc
AM_CPPFLAGS += -I $(top_srcdir)/inc
AM_CPPFLAGS += -I $(top_srcdir)/
AM_CPPFLAGS += -I $(top_srcdir)/utils/inc
AM_CPPFLAGS += -I $(top_srcdir)/device/inc
AM_CPPFLAGS += -I $(top_srcdir)/session/inc
AM_CPPFLAGS += -I $(top_srcdir)/stream/inc
AM_CPPFLAGS += -I $(top_srcdir)/session/SessionAR/inc
AM_CPPFLAGS += -I $(top_srcdir)/plugins/PluginManager/inc
AM_CPPFLAGS += -I $(top_srcdir)/resource_manager/inc
AM_CPPFLAGS += -I $(top_srcdir)/context_manager/inc
AM_CPPFLAGS += -I $(top_srcdir)/plugins/vui_interface/api/vui-interface

stream_common_sources = ./src/StreamCommon.cpp
else
AM_CPPFLAGS := -I  $(top_srcdir)/stream/StreamCommon/inc
AM_CPPFLAGS += -I  $(top_srcdir)/inc
AM_CPPFLAGS += -I  $(top_srcdir)/
AM_CPPFLAGS += -I  $(top_srcdir)/utils/inc
AM_CPPFLAGS += -I  $(top_srcdir)/device/inc
AM_CPPFLAGS += -I  $(top_srcdir)/session/inc
AM_CPPFLAGS += -I  $(top_srcdir)/stream/inc
AM_CPPFLAGS += -I  $(top_srcdir)/session/SessionAR/inc
AM_CPPFLAGS += -I  $(top_srcdir)/plugins/PluginManager/inc
AM_CPPFLAGS += -I  $(top_srcdir)/resource_manager/inc
AM_CPPFLAGS += -I  $(top_srcdir)/plugins/vui_interface/api/vui-interface
AM_CPPFLAGS += -I $(top_srcdir)/context_manager/inc
AM_CPPFLAGS += -I $(top_srcdir)/stream/StreamCallTranslation/inc
AM_CPPFLAGS += @SPF_CFLAGS@
AM_CPPFLAGS += @AROUTE_CFLAGS@
AM_CPPFLAGS += @KVH2XML_CFLAGS@

stream_common_sources = $(top_srcdir)/stream/StreamCommon/src/StreamCommon.cpp
endif


library_includedir = $(includedir)/stream_common

lib_LTLIBRARIES     = libstream_common.la
libstream_common_la_SOURCES   = $(stream_common_sources)
libstream_common_la_LIBADD    = @GLIB_LIBS@ -lexpat @GSL_LIBS@
libstream_common_la_CPPFLAGS := $(AM_CPPFLAGS)
libstream_common_la_CPPFLAGS += -std=c++14
libstream_common_la_LIBADD    += -L${abs_top_builddir}/.libs -lpal
libstream_common_la_LDFLAGS   = -shared -avoid-version
libstream_common_la_CPPFLAGS += @GLIB_CFLAGS@ -include glib.h
libstream_common_la_CPPFLAGS += -DACD_SM_FILEPATH=\"/etc/models/acd/\"
libstream_common_la_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\) -DLINUX_ENABLED

if BUILDSYSTEM_OPENWRT
libstream_common_la_LIBADD += -lglib-2.0 -llog
else
libstream_common_la_CPPFLAGS += -DPAL_USE_SYSLOG -DPAL_MEMLOG_UNSUPPORTED -DATRACE_UNSUPPORTED -DCARD_STATE_UNSUPPORTED -DPAL_SIGNAL_HANDLER_UNSUPPORTED -DA2DP_SINK_SUPPORTED -DVUI_DMGR_AUDIO_UNSUPPORTED -DAUDIO_FEATURE_STATS_UNSUPPORTED
endif

if CUTILS_SUPPORTED
libstream_common_la_CPPFLAGS += -DPAL_CUTILS_SUPPORTED
libstream_common_la_LIBADD += -lcutils
endif

if USE_G_STR_FUNC
libstream_common_la_CPPFLAGS +=-Dstrlcpy=g_strlcpy -Dstrlcat=g_strlcat
endif

SUBDIRS = . StreamUltrasSound StreamSensorRenderer StreamSensorPCMData StreamContextProxy StreamCommonProxy
