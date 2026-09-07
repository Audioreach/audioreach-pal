pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = pal.pc
EXTRA_DIST = $(pkgconfig_DATA)


if BUILDSYSTEM_OPENWRT

AM_CPPFLAGS := -I $(top_srcdir)/stream/inc
AM_CPPFLAGS += -I $(top_srcdir)/device/inc
AM_CPPFLAGS += -I $(top_srcdir)/session/inc
AM_CPPFLAGS += -I $(top_srcdir)/session/SessionAR/inc
AM_CPPFLAGS += -I $(top_srcdir)/context_manager/inc
AM_CPPFLAGS += -I $(top_srcdir)/plugins/PluginManager/inc
AM_CPPFLAGS += -I $(top_srcdir)/resource_manager/inc
AM_CPPFLAGS += -I $(top_srcdir)/utils/inc
AM_CPPFLAGS += -I $(top_srcdir)/inc
AM_CPPFLAGS += -I $(top_srcdir)/

AM_CPPFLAGS += $(SPF_CFLAGS)/
AM_CPPFLAGS += $(ACDBDATA_CFLAGS)
AM_CPPFLAGS += -I $(TOPDIR)/src/audio/mm-audio-headers/capiv2_api/
AM_CPPFLAGS += -I $(TOPDIR)/system/media/audio_route/include/
AM_CPPFLAGS += -I $(top_srcdir)/plugins/vui_interface/api/vui-interface


pal_sources = $(top_srcdir)/stream/src/Stream.cpp \
              $(top_srcdir)/device/src/Device.cpp \
              $(top_srcdir)/session/src/Session.cpp \
              $(top_srcdir)/context_manager/src/ContextManager.cpp \
              $(top_srcdir)/resource_manager/src/SndCardMonitor.cpp \
              $(top_srcdir)/resource_manager/src/ResourceManager.cpp \
              $(top_srcdir)/Pal.cpp \
              $(top_srcdir)/utils/src/SoundTriggerPlatformInfo.cpp \
              $(top_srcdir)/utils/src/SignalHandler.cpp \
              $(top_srcdir)/utils/src/MetadataParser.cpp \
              $(top_srcdir)/utils/src/MemLogBuilder.cpp \
              $(top_srcdir)/utils/src/PerfLock.cpp \
              $(top_srcdir)/utils/src/VoiceUIPlatformInfo.cpp \
              $(top_srcdir)/utils/src/AudioHapticsInterface.cpp \
              $(top_srcdir)/utils/src/ACDPlatformInfo.cpp \
              $(top_srcdir)/utils/src/ASRPlatformInfo.cpp \
              $(top_srcdir)/utils/src/STUtils.cpp
else
AM_CPPFLAGS := -I $(top_srcdir)/stream/inc
AM_CPPFLAGS += -I $(top_srcdir)/device/inc
AM_CPPFLAGS += -I $(top_srcdir)/session/inc
AM_CPPFLAGS += -I $(top_srcdir)/session/SessionAR/inc
AM_CPPFLAGS += -I $(top_srcdir)/stream/StreamCommon/StreamUltrasSound/inc
AM_CPPFLAGS += -I $(top_srcdir)/stream/StreamCommon/inc/
AM_CPPFLAGS += -I $(top_srcdir)/plugins/PluginManager/inc
AM_CPPFLAGS += -I $(top_srcdir)/resource_manager/inc
AM_CPPFLAGS += -I $(top_srcdir)/utils/inc
AM_CPPFLAGS += -I $(top_srcdir)/context_manager/inc
AM_CPPFLAGS += -I $(top_srcdir)/inc
AM_CPPFLAGS += -I $(top_srcdir)/plugins/codecs
AM_CPPFLAGS += -I $(top_srcdir)/
AM_CPPFLAGS += -I $(top_srcdir)/plugins/vui_interface/api/vui-interface
AM_CPPFLAGS += @AGM_CFLAGS@
AM_CPPFLAGS += @SPF_CFLAGS@
AM_CPPFLAGS += @KVH2XML_CFLAGS@
AM_CPPFLAGS += @AROUTE_CFLAGS@
AM_CPPFLAGS += -Wno-sign-compare -Wno-unused-result -fPIC


pal_sources = $(top_srcdir)/stream/src/Stream.cpp \
              $(top_srcdir)/device/src/Device.cpp \
              $(top_srcdir)/session/src/Session.cpp \
              $(top_srcdir)/context_manager/src/ContextManager.cpp \
              $(top_srcdir)/resource_manager/src/SndCardMonitor.cpp \
              $(top_srcdir)/resource_manager/src/ResourceManager.cpp \
              $(top_srcdir)/Pal.cpp \
              $(top_srcdir)/utils/src/SoundTriggerPlatformInfo.cpp \
              $(top_srcdir)/utils/src/MetadataParser.cpp \
              $(top_srcdir)/utils/src/VoiceUIPlatformInfo.cpp \
              $(top_srcdir)/utils/src/AudioHapticsInterface.cpp \
              $(top_srcdir)/utils/src/ACDPlatformInfo.cpp \
              $(top_srcdir)/utils/src/ASRPlatformInfo.cpp \
              $(top_srcdir)/utils/src/STUtils.cpp \
              $(top_srcdir)/utils/src/BTUtils.cpp

endif
AM_CPPFLAGS += -D__unused=__attribute__\(\(__unused__\)\) -DLINUX_ENABLED
AM_CPPFLAGS += -DCONFIG_GSL

library_includedir = $(includedir)/pal
lib_LTLIBRARIES     = libpal.la
libpal_la_SOURCES   = $(pal_sources)
if BUILDSYSTEM_OPENWRT
libpal_la_LIBADD    = $(GLIB_LIBS) -ltinyalsa -laudioroute -lar-osal -lexpat -ltinycompress -lagmclientwrapper
libpal_la_LIBADD   += -lar-gsl
libpal_la_CPPFLAGS  = -DFEATURE_IPQ_OPENWRT
else
libpal_la_LIBADD    = $(GLIB_LIBS) -ltinyalsa -laudioroute -lar-osal -lexpat -ltinycompress
libpal_la_CPPFLAGS  =  -DPAL_USE_SYSLOG -DLINUX_ENABLED -DATRACE_UNSUPPORTED -DPAL_MEMLOG_UNSUPPORTED -DCARD_STATE_UNSUPPORTED -DPAL_SIGNAL_HANDLER_UNSUPPORTED -DA2DP_SINK_SUPPORTED -DVUI_DMGR_AUDIO_UNSUPPORTED -DAUDIO_FEATURE_STATS_UNSUPPORTED
endif
if IS_SDXLEMUR
libpal_la_LIBADD    += -lagmclientwrapper
endif
if IS_SA525M
libpal_la_LIBADD    += -lagmclientwrapper
endif
libpal_la_CPPFLAGS += $(AM_CPPFLAGS)
libpal_la_CPPFLAGS += -std=c++14
libpal_la_LDFLAGS   = -shared -version-number @LT_VERSION_NUMBER@
libpal_la_CPPFLAGS += @GLIB_CFLAGS@ -include glib.h
libpal_la_CPPFLAGS += -DACD_SM_FILEPATH=\"/etc/models/acd/\"

if USE_SYSLOG
libpal_la_CPPFLAGS += -DPAL_USE_SYSLOG
endif

if USE_G_STR_FUNC
libpal_la_CPPFLAGS +=-Dstrlcpy=g_strlcpy -Dstrlcat=g_strlcat
endif

if BUILDSYSTEM_OPENWRT
libpal_la_LIBADD += -lglib-2.0 -llog
endif
if COMPILE_COMPRESS
libpal_la_CPPFLAGS += -DSND_COMPRESS_DEC_HDR
endif
libpal_la_LIBADD   += -L${abs_top_builddir}/plugins/vui_interface/.libs
libpal_la_LIBADD   += ${abs_top_builddir}/plugins/PluginManager/.libs/libplugin_manager.la

if CUTILS_SUPPORTED
libpal_la_CPPFLAGS += -DPAL_CUTILS_SUPPORTED
libpal_la_LIBADD += -lcutils
endif

if BUILDSYSTEM_OPENWRT
# config files for OPENWRT
else

libpal_la_list      = ./configs/qcom/IoT/qcm6490/usecaseKvManager.xml \
                      ./configs/qcom/IoT/qcs615/mixer_paths_TALOS_EVK.xml \
                      ./configs/qcom/IoT/qcs615/resourcemanager_TALOS_EVK.xml \
                      ./configs/qcom/IoT/qcs8300/mixer_paths_MONACO_EVK.xml \
                      ./configs/qcom/IoT/qcs8300/resourcemanager_MONACO_EVK.xml \
                      ./configs/qcom/IoT/qcs6490/mixer_paths_QCS6490_RB3Gen2.xml \
                      ./configs/qcom/IoT/qcs6490/resourcemanager_QCS6490_RB3Gen2.xml \
                      ./configs/qcom/IoT/qcs9075/mixer_paths_LEMANS_EVK.xml \
                      ./configs/qcom/IoT/qcs9075/resourcemanager_LEMANS_EVK.xml \
                      ./configs/qcom/IoT/shikra/mixer_paths_shikra_cqs_evk.xml \
                      ./configs/qcom/IoT/shikra/resourcemanager_shikra_cqs_evk.xml \
                      ./configs/qcom/IoT/shikra/mixer_paths_shikra_iqs_evk.xml \
                      ./configs/qcom/IoT/shikra/resourcemanager_shikra_iqs_evk.xml \
                      ./configs/qcom/IoT/shikra/plugin_manager.xml \
                      ./configs/qcom/IoT/x1e80100/mixer_paths_X1E80100_CRD.xml \
                      ./configs/qcom/IoT/x1e80100/resourcemanager_X1E80100_CRD.xml \
                      ./configs/qcom/IoT/x1e80100/mixer_paths_X1E80100_EVK.xml \
                      ./configs/qcom/IoT/x1e80100/resourcemanager_X1E80100_EVK.xml \
                      ./configs/qcom/IoT/sm8750/mixer_paths_SM8750_MTP.xml \
                      ./configs/qcom/IoT/sm8750/resourcemanager_SM8750_MTP.xml \
                      ./configs/qcom/IoT/GLYMUR/mixer_paths_GLYMUR_CRD.xml \
                      ./configs/qcom/IoT/GLYMUR/resourcemanager_GLYMUR_CRD.xml \
                      ./configs/qcom/IoT/MAHUA/mixer_paths_MAHUA_CRD.xml \
                      ./configs/qcom/IoT/MAHUA/resourcemanager_MAHUA_CRD.xml \
                      ./configs/qcom/IoT/Kaanapali/mixer_paths_Kaanapali_MTP.xml \
                      ./configs/qcom/IoT/Kaanapali/resourcemanager_Kaanapali_MTP.xml


# install essential xml files under /etc
root_etcdir      = "/etc"
root_etc_SCRIPTS = $(libpal_la_list)
install-data-hook:
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_MONACO_EVK.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_MONACO_EVK.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_TALOS_EVK.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_TALOS_EVK.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/usecaseKvManager.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_QCS6490_RB3Gen2.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_QCS6490_RB3Gen2.xml
	ln -sf mixer_paths_QCS6490_RB3Gen2.xml $(DESTDIR)$(root_etcdir)/mixer_paths_QCM6490_IDP.xml
	ln -sf resourcemanager_QCS6490_RB3Gen2.xml $(DESTDIR)$(root_etcdir)/resourcemanager_QCM6490_IDP.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_LEMANS_EVK.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_LEMANS_EVK.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_shikra_cqs_evk.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_shikra_cqs_evk.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_shikra_iqs_evk.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_shikra_iqs_evk.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/plugin_manager.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_X1E80100_CRD.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_X1E80100_CRD.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_X1E80100_EVK.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_X1E80100_EVK.xml 
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_SM8750_MTP.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_SM8750_MTP.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_GLYMUR_CRD.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_GLYMUR_CRD.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_MAHUA_CRD.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_MAHUA_CRD.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/mixer_paths_Kaanapali_MTP.xml
	chmod  go+r $(DESTDIR)$(root_etcdir)/resourcemanager_Kaanapali_MTP.xml
endif

if COMPILE_ADSPRPCD
adsprpc_sources = adsprpcd/adsprpcd.c

bin_PROGRAMS = audioadsprpcd
audioadsprpcd_SOURCES = $(adsprpc_sources)
audioadsprpcd_LDADD = -ldl
audioadsprpcd_la_CFLAGS = -fPIC
endif


SUBDIRS =  inc plugins/vui_interface/api/vui-interface plugins . session stream device plugins/configs
