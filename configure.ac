#
# Copyright (C) 2010 - 2011, Robert Ancell <robert.ancell@canonical.com>
# Copyright (C) 2012 - 2013, Lionel Le Folgoc <mrpouit@ubuntu.com>
# Copyright (C) 2013 - 2024, Sean Davis <sean@bluesabre.org>
# Copyright (C) 2014, Andrew P. <pan.pav.7c5@gmail.com>
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. See http://www.gnu.org/copyleft/gpl.html the full text of the
# license.
#

dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.64])
AC_INIT([lightdm-gtk-greeter],[2.0.9],[https://github.com/xubuntu/lightdm-gtk-greeter])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_SRCDIR([src/lightdm-gtk-greeter.c])
AC_CONFIG_MACRO_DIR([m4])

LT_INIT
AM_INIT_AUTOMAKE([1.11 tar-ustar foreign -Wall -Wno-portability])
AM_MAINTAINER_MODE([enable])

# Use AM_SILENT_RULES if present
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)])

AC_PROG_CC

GOBJECT_INTROSPECTION_CHECK(0.9.5)

dnl ###########################################################################
dnl Standard headers
dnl ###########################################################################

AC_CHECK_HEADERS([stdlib.h])

dnl ###########################################################################
dnl Dependencies
dnl ###########################################################################

PKG_CHECK_MODULES([GTK], [gtk+-3.0])
PKG_CHECK_MODULES([GMODULE], [gmodule-export-2.0])
PKG_CHECK_MODULES([LIGHTDMGOBJECT], [liblightdm-gobject-1 >= 1.19.2],
  [AC_DEFINE([HAVE_LIBLIGHTDMGOBJECT_1_19_2], [1], [Building with liblightdmgobject 1.19.2])],
  [PKG_CHECK_MODULES([LIGHTDMGOBJECT], [liblightdm-gobject-1 >= 1.3.5])]
)
PKG_CHECK_MODULES([LIBX11], [x11])

dnl ###########################################################################
dnl Optional dependencies
dnl ###########################################################################

AYATANA_INDICATOR_REQUIRED_VERSION=0.6.0
AYATANA_INDICATOR_PKG=ayatana-indicator3-0.4
AYATANA_IDO_REQUIRED_VERSION=0.4.0
AYATANA_IDO_PKG=libayatana-ido3-0.4

UNITY_INDICATOR_REQUIRED_VERSION=0.3.92
UNITY_INDICATOR_PKG=indicator3-0.4
UNITY_IDO_REQUIRED_VERSION=13.10
UNITY_IDO_PKG=libido3-0.1

dnl ###########################################################################

AC_ARG_ENABLE([libindicator],
    AC_HELP_STRING([--enable-libindicator], [Enable libindicator support])
    AC_HELP_STRING([--disable-libindicator], [Disable libindicator support]),
            [], [enable_libindicator=yes])

AC_ARG_WITH([libindicator], AS_HELP_STRING([--with-libindicator=ayatana|unity], [Preferred indicator backend [default=ayatana]]))
have_indicator_ng=no

AS_IF([test "x$enable_libindicator" = "xyes"], [
    AS_IF([test "x$with_libindicator" != "xunity" && $PKG_CONFIG --exists "$AYATANA_INDICATOR_PKG >= $AYATANA_INDICATOR_REQUIRED_VERSION" >/dev/null 2>&1], [
        PKG_CHECK_MODULES([AYATANA_LIBINDICATOR], [$AYATANA_INDICATOR_PKG >= $AYATANA_INDICATOR_REQUIRED_VERSION], [
            have_ayatana_libindicator=yes
            have_libindicator=yes
            INDICATORDIR=`$PKG_CONFIG --variable=indicatordir $AYATANA_INDICATOR_PKG`
            AC_SUBST(INDICATORDIR)
            AYATANA_INDICATORDIR="${prefix}/share/ayatana/indicators"
            AC_SUBST(AYATANA_INDICATORDIR)
            AC_DEFINE([HAVE_LIBINDICATOR], [1], [Define if "$AYATANA_INDICATOR_PKG" is present])
            AC_DEFINE([HAVE_AYATANA_LIBINDICATOR], [1], [Define if "$AYATANA_INDICATOR_PKG" is present])

            CPPFLAGS_OLD="$CPPFLAGS"
            CPPFLAGS=`$PKG_CONFIG --cflags ${AYATANA_INDICATOR_PKG}`
            AC_CHECK_HEADER([libayatana-indicator/indicator-ng.h], [
                have_indicator_ng=yes
                AC_DEFINE([HAVE_LIBINDICATOR_NG], [1], [Define if "libayatana-indicator/indicator-ng.h" is present])
                AC_DEFINE([HAVE_AYATANA_LIBINDICATOR_NG], [1], [Define if "libayatana-indicator/indicator-ng.h" is present])
            ], [], [-])
            CPPFLAGS="$CPPFLAGS_OLD"
        ])
    ], [
        AS_IF([test "x$with_libindicator" != "xunity"], [
            AC_MSG_CHECKING([for optional package $AYATANA_INDICATOR_PKG])
            AC_MSG_RESULT([not found])
        ], [])
        AS_IF([$PKG_CONFIG --exists "$UNITY_INDICATOR_PKG >= $UNITY_INDICATOR_REQUIRED_VERSION" >/dev/null 2>&1], [
            PKG_CHECK_MODULES([UNITY_LIBINDICATOR], [$UNITY_INDICATOR_PKG >= $UNITY_INDICATOR_REQUIRED_VERSION], [
                have_unity_libindicator=yes
                have_libindicator=yes
                INDICATORDIR=`$PKG_CONFIG --variable=indicatordir $UNITY_INDICATOR_PKG`
                AC_SUBST(INDICATORDIR)
                UNITY_INDICATORDIR="${prefix}/share/unity/indicators"
                AC_SUBST(UNITY_INDICATORDIR)
                AC_DEFINE([HAVE_LIBINDICATOR], [1], [Define if "$UNITY_INDICATOR_PKG" is present])
                AC_DEFINE([HAVE_UNITY_LIBINDICATOR], [1], [Define if "$UNITY_INDICATOR_PKG" is present])

                CPPFLAGS_OLD="$CPPFLAGS"
                CPPFLAGS=`$PKG_CONFIG --cflags ${UNITY_INDICATOR_PKG}`
                AC_CHECK_HEADER([libindicator/indicator-ng.h], [
                    have_indicator_ng=yes
                    AC_DEFINE([HAVE_LIBINDICATOR_NG], [1], [Define if "libindicator/indicator-ng.h" is present])
                    AC_DEFINE([HAVE_UNITY_LIBINDICATOR_NG], [1], [Define if "libindicator/indicator-ng.h" is present])
                ], [], [-])
                CPPFLAGS="$CPPFLAGS_OLD"
            ])
        ], [
            AC_MSG_CHECKING([for optional package $UNITY_INDICATOR_PKG])
            AC_MSG_RESULT([not found])
        ])
    ])
], [
    AC_MSG_CHECKING([for optional package $AYATANA_INDICATOR_PKG or $UNITY_INDICATOR_PKG])
    AC_MSG_RESULT([disabled])
])

AM_CONDITIONAL([HAVE_UNITY_LIBINDICATOR], [test "x$have_unity_libindicator" = "xyes"])

AC_ARG_WITH([systemd-service-dir], AS_HELP_STRING([--with-systemd-service-dir=path], [Path where systemd indicator service units are stored]))
AS_IF([test "x$with_systemd_service_dir" = "x" || test "x$with_systemd_service_dir" = "x"], [
    with_systemd_service_dir="/usr/lib/systemd/user"
], [])
SYSTEMD_SERVICE_DIR="$with_systemd_service_dir"
AC_SUBST(SYSTEMD_SERVICE_DIR)

dnl ###########################################################################

AC_ARG_ENABLE([libido],
    AC_HELP_STRING([--enable-libido], [Enable libido support])
    AC_HELP_STRING([--disable-libido], [Disable libido support]),
            [], [enable_libido=yes])

have_ido=no

AS_IF([test "x$enable_libido" = "xyes" && test "x$have_libindicator" = "xyes"], [
    AS_IF([test "x$have_ayatana_libindicator" = "xyes" && $PKG_CONFIG --exists "$AYATANA_IDO_PKG" >/dev/null 2>&1], [
        PKG_CHECK_MODULES([AYATANA_LIBIDO], [$AYATANA_IDO_PKG >= $AYATANA_IDO_REQUIRED_VERSION], [
            have_ido=yes
            AC_DEFINE([HAVE_LIBIDO], [1], [Define if "$AYATANA_IDO_PKG" is present])
            AC_DEFINE([HAVE_AYATANA_LIBIDO], [1], [Define if "$AYATANA_IDO_PKG" is present])
        ])
    ], [
        AS_IF([test "x$have_ayatana_libindicator" = "xyes"], [
            AC_MSG_CHECKING([for optional package $AYATANA_IDO_PKG])
            AC_MSG_RESULT([not found])
        ], [])
        AS_IF([$PKG_CONFIG --exists "$UNITY_IDO_PKG" >/dev/null 2>&1], [
            PKG_CHECK_MODULES([UNITY_LIBIDO], [$UNITY_IDO_PKG >= $UNITY_IDO_REQUIRED_VERSION], [
                have_ido=yes
                AC_DEFINE([HAVE_LIBIDO], [1], [Define if "$UNITY_IDO_PKG" is present])
                AC_DEFINE([HAVE_UNITY_LIBIDO], [1], [Define if "$UNITY_IDO_PKG" is present])
            ])
        ], [
            AC_MSG_CHECKING([for optional package $UNITY_IDO_PKG])
            AC_MSG_RESULT([not found])
        ])
    ])
], [
    AC_MSG_CHECKING([for optional package $AYATANA_IDO_PKG or $UNITY_IDO_PKG])
    AC_MSG_RESULT([disabled])
])

dnl ###########################################################################

AC_ARG_WITH([libxklavier], AS_HELP_STRING([--with-libxklavier], [Use libxklavier to manage layouts (instead of LightDM API)]))

AS_IF([test "x$with_libxklavier" = "xyes"],
[
    PKG_CHECK_MODULES([LIBXKLAVIER], [libxklavier], [have_xklavier=yes])
    AC_DEFINE([HAVE_LIBXKLAVIER], [1], [Define if "libxklavier" is present])
],
[
    with_libxklavier=no
])

dnl ###########################################################################

AC_ARG_ENABLE([at-spi-command],
    AC_HELP_STRING([--enable-at-spi-command[=command]], [Try to start at-spi service]])
    AC_HELP_STRING([--disable-at-spi-command], [Do not start at-spi service]),
    [], [])

AS_IF([test "x$enable_at_spi_command" != "xno"],
[
    if test "x$enable_at_spi_command" = "xyes" || test "x$enable_at_spi_command" = "x"; then
        enable_at_spi_command="/usr/libexec/at-spi-bus-launcher --launch-immediately"
    fi
    AC_DEFINE_UNQUOTED([AT_SPI_COMMAND], ["$enable_at_spi_command"], [Command to start at-spi service])
])

dnl ###########################################################################

AC_ARG_ENABLE([indicator-services-command],
    AC_HELP_STRING([--enable-indicator-services-command[=command]], [Try to start indicators service]])
    AC_HELP_STRING([--disable-indicator-services-command], [Do not start indicators service]),
    [], [])

AS_IF([test "x$enable_indicator_services_command" != "xno"],
[
    if test "x$enable_indicator_services_command" = "xyes" || test "x$enable_indicator_services_command" = "x"; then
        enable_indicator_services_command="systemd --user indicator-services-start"
    fi
    AC_DEFINE_UNQUOTED([INDICATOR_SERVICES_COMMAND], ["$enable_indicator_services_command"], [Command to start indicators service])
])

dnl ###########################################################################

AC_ARG_ENABLE([kill-on-sigterm],
    AC_HELP_STRING([--enable-kill-on-sigterm], [Kill greeter instance on SIGTERM, see LP1445461])
    AC_HELP_STRING([--disable-kill-on-sigterm], [Don't kill greeter on SIGTERM, see LP1445461']),
            [], [enable_kill_on_sigterm=no])

AS_IF([test "x$enable_kill_on_sigterm" = "xyes"],
[
    AC_DEFINE([KILL_ON_SIGTERM], [1], [Kill greeter in SIGTERM handler, see LP1445461])
],
[])

dnl ###########################################################################
dnl Internationalization
dnl ###########################################################################

GETTEXT_PACKAGE="lightdm-gtk-greeter"
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [GETTEXT package name])
AM_GNU_GETTEXT_VERSION([0.19.7])
AM_GNU_GETTEXT_REQUIRE_VERSION([0.19.7])
AM_GNU_GETTEXT([external])

dnl ###########################################################################
dnl Files to generate
dnl ###########################################################################

AC_CONFIG_FILES([
Makefile
data/Makefile
data/badges/Makefile
po/Makefile.in
src/Makefile
])
AC_OUTPUT

AS_IF([test "x$have_libindicator" = "xyes"], [
    AS_IF([test "x$have_ayatana_libindicator" = "xyes"], [
        indicator_backend="ayatana"
    ], [
        indicator_backend="unity"
    ])
], [
    indicator_backend="disabled"
])

echo "
------------------------------------------------------
            LightDM GTK Greeter $VERSION
        ====================================

        Installation:
        =============
        Prefix:                         $prefix
        AT-SPI Service:                 $enable_at_spi_command
        Use libxklavier:                $with_libxklavier
        Enable SIGTERM Handler:         $enable_kill_on_sigterm

        Indicators:
        ===========
        Indicator Support:              $indicator_backend
        Indicator Services Command:     $enable_indicator_services_command
        systemd Services:               $with_systemd_service_dir
        Indicator NG:                   $have_indicator_ng
        IDO:                            $have_ido

------------------------------------------------------

Configuration finished, type make to compile"
