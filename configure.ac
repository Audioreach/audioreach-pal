#                                               -*- Autoconf -*-
# configure.ac -- Autoconf script for pal
#

# Process this file with autoconf to produce a configure script

# Requires autoconf tool later than 2.61
AC_PREREQ(2.69)
# Initialize the pal package version 1.0.0
AC_INIT([pal],1.0.0)
# Does not strictly follow GNU Coding standards
AM_INIT_AUTOMAKE([foreign subdir-objects])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_LIBTOOL
AC_PROG_AWK
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
PKG_PROG_PKG_CONFIG

m4_define([LT_MAJOR], [1])
m4_define([LT_MINOR], [0])
m4_define([LT_PATCH], [0])

AC_SUBST([LT_VERSION], LT_MAJOR.LT_MINOR.LT_PATCH)
AC_SUBST([LT_VERSION_NUMBER], LT_MAJOR:LT_MINOR:LT_PATCH)

#basemachine
AC_MSG_CHECKING([which base machine to use])
AC_ARG_WITH([basemachine],
            [AS_HELP_STRING([--with-basemachine],
            [Specify the base machine])],
            [machine_selected="$withval"],
            [machine_selected=none])

AC_MSG_RESULT([$machine_selected])
AC_SUBST([MACHINE_ENABLED],[$machine_selected])

AC_ARG_WITH([openwrt],
    AS_HELP_STRING([use openwrt (default is no)]),
    [with_openwrt=$withval],
    [with_openwrt=no])
AM_CONDITIONAL([BUILDSYSTEM_OPENWRT], [test "x${with_openwrt}" = "xyes"])

AC_ARG_WITH([glib],
      AS_HELP_STRING([--with-glib],
         [enable glib, Build against glib. Use this when building for HLOS systems which use glib]))


AC_ARG_WITH([syslog],
    AS_HELP_STRING([use syslog (default is no)]),
        [with_syslog=$withval],
            [with_syslog=no])
            AM_CONDITIONAL([USE_SYSLOG], [test "x${with_syslog}" = "xyes"])

if (test "x${with_glib}" = "xyes"); then
        PKG_CHECK_MODULES(GTHREAD, gthread-2.0 >= 2.16, dummy=yes,
                                AC_MSG_ERROR(GThread >= 2.16 is required))
        PKG_CHECK_MODULES(GLIB, glib-2.0 >= 2.16, dummy=yes,
                                AC_MSG_ERROR(GLib >= 2.16 is required))
        GLIB_CFLAGS="$GLIB_CFLAGS $GTHREAD_CFLAGS"
        GLIB_LIBS="$GLIB_LIBS $GTHREAD_LIBS"

        AC_SUBST(GLIB_CFLAGS)
        AC_SUBST(GLIB_LIBS)
fi

AM_CONDITIONAL(USE_GLIB, test "x${with_glib}" = "xyes")
AC_ARG_WITH([cutils],
    AS_HELP_STRING([Use cutils (default is no)]),
     [with_cutils=$withval],
     [with_cutils=no])
AM_CONDITIONAL([CUTILS_SUPPORTED], [test "x${with_cutils}" = "xyes"])
AM_CONDITIONAL([IS_SDXLEMUR], [test x$IS_SDXLEMUR_TARGET = xtrue])
AM_CONDITIONAL([IS_SA525M], [test x$IS_SA525M_TARGET = xtrue])

# Checks for libraries
PKG_CHECK_MODULES([AGM], [agm])
AC_SUBST([AGM_CFLAGS])

PKG_CHECK_MODULES([SPF], [spf])
AC_SUBST([SPF_CFLAGS])

AC_CHECK_FUNCS([strlcpy])
AM_CONDITIONAL(USE_G_STR_FUNC, ! test "x$ac_cv_func_strlcpy" = "xyes")

AC_ARG_WITH([acdbdata],
      AC_HELP_STRING([--with-acdbdata],
         [enable acdbdata, Build against acdbdata. Use this when building for HLOS systems which use acdbdata]))

ACDBDATA_CFLAGS=
AS_IF([test "x$with_acdbdata" != "xno"], [ACDBDATA_CFLAGS="-I$with_acdbdata"])

AC_SUBST(ACDBDATA_CFLAGS)

PKG_CHECK_MODULES([CAPIV2HEADERS], [capiv2-api-headers], [have_capiv2_headers=yes], [have_capiv2_headers=no])
AM_CONDITIONAL([HAVE_CAPIV2HEADERS], [test "x$have_capiv2_headers" = "xyes"])
AC_SUBST([CAPIV2HEADERS_CFLAGS])

if (test "x${with_openwrt}" = "xno"); then

PKG_CHECK_MODULES([KVH2XML], [kvh2xml])
AC_SUBST([KVH2XML_CFLAGS])

PKG_CHECK_MODULES([AROUTE], [audioroute])
AC_SUBST([AROUTE_CFLAGS])

fi


AC_ARG_WITH([qli],
    AS_HELP_STRING([use qli (default is no)]),
    [with_qli=$withval],
    [with_qli=no])
AM_CONDITIONAL([BUILDSYSTEM_QLI], [test "x${with_qli}" = "xyes"])


AC_ARG_WITH([compress],
    AS_HELP_STRING([compile compress (default is no)]),
    [with_compress=$withval],
    [with_compress=no])
AM_CONDITIONAL([COMPILE_COMPRESS], [test "x${with_compress}" = "xyes"])

AC_ARG_WITH([adsprpcd],
    AS_HELP_STRING([compile adsprpcd (default is no)]),
    [with_adsprpcd=$withval],
    [with_adsprpcd=no])
AM_CONDITIONAL([COMPILE_ADSPRPCD], [test "x${with_adsprpcd}" = "xyes"])

AC_ARG_WITH([streamSoundTrigger],
    AS_HELP_STRING([compile streamSoundTrigger (default is no)]),
    [with_streamSoundTrigger=$withval],
    [with_streamSoundTrigger=no])
AM_CONDITIONAL([COMPILE_STREAMSOUNDTRIGGER], [test "x${with_streamSoundTrigger}" = "xyes"])

AC_CONFIG_SUBDIRS([
        inc
        plugins/vui_interface/api/vui-interface
])

AC_CONFIG_FILES([ Makefile \
pal.pc \
stream/Makefile \
session/Makefile \
device/Makefile \
stream/StreamSoundTrigger/Makefile \
stream/StreamSoundTrigger/stream_soundtrigger.pc \
stream/StreamPCM/Makefile \
stream/StreamPCM/stream_pcm.pc \
stream/StreamNonTunnel/Makefile \
stream/StreamNonTunnel/stream_nontunnel.pc \
stream/StreamInCall/Makefile \
stream/StreamInCall/stream_incall.pc \
stream/StreamHaptic/Makefile \
stream/StreamHaptic/stream_haptics.pc \
stream/StreamCompress/Makefile \
stream/StreamCompress/stream_compress.pc \
stream/StreamCommon/Makefile \
stream/StreamCommon/stream_common.pc \
stream/StreamCommon/StreamUltrasSound/Makefile \
stream/StreamCommon/StreamUltrasSound/stream_ultrasound.pc \
stream/StreamCommon/StreamSensorRenderer/Makefile \
stream/StreamCommon/StreamSensorRenderer/stream_sensorrenderer.pc \
stream/StreamCommon/StreamSensorPCMData/Makefile \
stream/StreamCommon/StreamSensorPCMData/streamsensorpcmdata_headers.pc \
stream/StreamCommon/StreamContextProxy/Makefile \
stream/StreamCommon/StreamContextProxy/stream_contextproxy.pc \
stream/StreamCommon/StreamCommonProxy/Makefile \
stream/StreamCommon/StreamCommonProxy/stream_commonproxy.pc \
stream/StreamCallTranslation/Makefile \
stream/StreamCallTranslation/stream_calltranslation.pc \
stream/StreamASR/Makefile \
stream/StreamASR/stream_asr.pc \
stream/StreamACDB/Makefile \
stream/StreamACDB/stream_acdb.pc \
stream/StreamACD/Makefile \
stream/StreamACD/stream_acd.pc \
session/SessionAlsaPcm/Makefile \
session/SessionAlsaPcm/session_pcm.pc \
session/SessionAlsaVoice/Makefile \
session/SessionAlsaVoice/session_voice.pc \
session/SessionAlsaCompress/Makefile \
session/SessionAlsaCompress/session_compress.pc \
session/SessionAR/Makefile \
session/SessionAR/session_ar.pc \
session/SessionAgm/Makefile \
session/SessionAgm/session_agm.pc \
device/USBAudio/Makefile \
device/USBAudio/dev_usb.pc \
device/UltrasoundDevice/Makefile \
device/UltrasoundDevice/dev_ultrasound.pc \
device/SpeakerMic/Makefile \
device/SpeakerMic/dev_speaker_mic.pc \
device/Speaker/Makefile \
device/Speaker/dev_speaker.pc \
device/HeadsetVaMic/Makefile \
device/HeadsetVaMic/dev_headset_va.pc \
device/HeadsetMic/Makefile \
device/HeadsetMic/dev_headset_mic.pc \
device/Headphone/Makefile \
device/Headphone/dev_headphone.pc \
device/HapticsDev/Makefile \
device/HapticsDev/dev_haptics.pc \
device/HandsetVaMic/Makefile \
device/HandsetVaMic/dev_handset_va.pc \
device/HandsetMic/Makefile \
device/HandsetMic/dev_handset_mic.pc \
device/Handset/Makefile \
device/Handset/dev_handset.pc \
device/FMDevice/Makefile \
device/FMDevice/dev_fm.pc \
device/ExtEC/Makefile \
device/ExtEC/dev_ext_ec.pc \
device/ECRefDevice/Makefile \
device/ECRefDevice/dev_ec_ref.pc \
device/DummyDev/Makefile \
device/DummyDev/dev_dummy.pc \
device/DisplayPort/Makefile \
device/DisplayPort/dev_display.pc \
device/Bluetooth/Makefile \
device/Bluetooth/dev_bt.pc \
device/RTProxy/Makefile \
device/RTProxy/dev_proxy.pc \
plugins/Makefile plugins/plugins.pc \
plugins/configs/Makefile \
plugins/configs/session_configs.pc \
plugins/configs/qcom/mobile/default/ConfigSessionUtils/Makefile \
plugins/configs/qcom/mobile/default/ConfigSessionUtils/session_utils_config.pc \
plugins/configs/qcom/mobile/default/ConfigSessionAlsaPcm/Makefile \
plugins/configs/qcom/mobile/default/ConfigSessionAlsaPcm/session_pcm_config.pc \
plugins/configs/qcom/mobile/default/ConfigSessionAlsaCompress/Makefile \
plugins/configs/qcom/mobile/default/ConfigSessionAlsaCompress/session_compress_config.pc \
plugins/configs/qcom/mobile/default/ConfigSessionAlsaVoice/Makefile \
plugins/configs/qcom/mobile/default/ConfigSessionAlsaVoice/session_voice_config.pc \
plugins/configs/qcom/mbb/default/ConfigSessionUtils/Makefile \
plugins/configs/qcom/mbb/default/ConfigSessionUtils/session_utils_config.pc \
plugins/configs/qcom/mbb/default/ConfigSessionAlsaPcm/Makefile \
plugins/configs/qcom/mbb/default/ConfigSessionAlsaPcm/session_pcm_config.pc \
plugins/configs/qcom/mbb/default/ConfigSessionAlsaCompress/Makefile \
plugins/configs/qcom/mbb/default/ConfigSessionAlsaCompress/session_compress_config.pc \
plugins/configs/qcom/mbb/default/ConfigSessionAlsaVoice/Makefile \
plugins/configs/qcom/mbb/default/ConfigSessionAlsaVoice/session_voice_config.pc \
plugins/PluginManager/Makefile \
plugins/PluginManager/plugin_manager.pc \
plugins/codecs/Makefile \
plugins/codecs/codecs.pc ])

AC_ARG_WITH([adsprpcd],
    AS_HELP_STRING([compile adsprpcd (default is no)]),
    [with_adsprpcd=$withval],
    [with_adsprpcd=no])
AM_CONDITIONAL([COMPILE_ADSPRPCD], [test "x${with_adsprpcd}" = "xyes"])

AC_OUTPUT
