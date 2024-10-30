#                                               -*- Autoconf -*-
# configure.ac -- Autoconf script for cnn_sva
#

# Process this file with autoconf to produce a configure script

# Requires autoconf tool later than 2.61
AC_PREREQ(2.61)
# Initialize the audio-effects package version 1.0.0
AC_INIT([vui_interface],1.0.0)
# Does not strictly follow GNU Coding standards
AM_INIT_AUTOMAKE([foreign])
# Disables auto rebuilding of configure, Makefile.ins
AM_MAINTAINER_MODE
# defines some macros variable to be included by source
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

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
AC_PROG_RANLIB
AM_PROG_AR

AC_ARG_WITH([glib],
      AS_HELP_STRING([--with-glib],
         [enable glib, Build against glib. Use this when building for HLOS systems which use glib]))

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

PKG_CHECK_MODULES([PALHEADERS],[pal-headers])
AC_SUBST([PALHEADERS_CFLAGS])

PKG_CHECK_MODULES([SPFHEADER],[spf])
AC_SUBST([SPFHEADER_CFLAGS])

PKG_CHECK_MODULES([KVH2XML],[kvh2xml])
AC_SUBST([KVH2XML_CFLAGS])

AC_CONFIG_FILES([
        Makefile
        sva_intf/Makefile
        sva_intf/sva_interface.pc
        uitls/Makefile
        utils/vui_interface_utils.pc
        ])

AC_OUTPUT

