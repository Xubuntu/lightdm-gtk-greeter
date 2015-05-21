dnl Process this file with autoconf to produce a configure script.

AC_INIT(lightdm-gtk-greeter, 2.0.1)
AC_CONFIG_MACRO_DIR(m4)
AC_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE
LT_INIT
AM_PROG_CC_C_O
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES(yes)])

GOBJECT_INTROSPECTION_CHECK(0.9.5)

GNOME_COMPILE_WARNINGS(maximum)

dnl ###########################################################################
dnl Standard headers
dnl ###########################################################################

AC_CHECK_HEADERS([stdlib.h])

dnl ###########################################################################
dnl Dependencies
dnl ###########################################################################

PKG_CHECK_MODULES([GTK], [gtk+-3.0])
PKG_CHECK_MODULES([GMODULE], [gmodule-export-2.0])
PKG_CHECK_MODULES([LIGHTDMGOBJECT], [liblightdm-gobject-1 >= 1.3.5])
PKG_CHECK_MODULES([LIBX11], [x11])

dnl ###########################################################################
dnl Optional dependencies
dnl ###########################################################################

INDICATOR_REQUIRED_VERSION=0.3.92
INDICATOR_PKG=indicator3-0.4
IDO_PKG=libido3-0.1

dnl ###########################################################################

AC_ARG_ENABLE([libindicator],
    AC_HELP_STRING([--enable-libindicator], [Enable libindicator support])
    AC_HELP_STRING([--disable-libindicator], [Disable libindicator support]),
            [], [enable_libindicator=yes])

AS_IF([test "x$enable_libindicator" = "xyes"], [
    AS_IF([$PKG_CONFIG --exists "$INDICATOR_PKG >= $INDICATOR_REQUIRED_VERSION" >/dev/null 2>&1], [
        PKG_CHECK_MODULES([LIBINDICATOR], [$INDICATOR_PKG >= $INDICATOR_REQUIRED_VERSION], [
            have_libindicator=yes
            INDICATORDIR=`$PKG_CONFIG --variable=indicatordir $INDICATOR_PKG`
            AC_SUBST(INDICATORDIR)
            UNITY_INDICATORDIR="${prefix}/share/unity/indicators"
            AC_SUBST(UNITY_INDICATORDIR)
            AC_DEFINE([HAVE_LIBINDICATOR], [1], [Define if "$INDICATOR_PKG" is present])

            CPPFLAGS_OLD="$CPPFLAGS"
            CPPFLAGS=`$PKG_CONFIG --cflags ${INDICATOR_PKG}`
            AC_CHECK_HEADER([libindicator/indicator-ng.h],
                [AC_DEFINE([HAVE_LIBINDICATOR_NG], [], [Define if "libindicator/indicator-ng.h" is present])],
                [], [-])
            CPPFLAGS="$CPPFLAGS_OLD"
        ])
    ], [
        AC_MSG_CHECKING([for optional package $INDICATOR_PKG])
        AC_MSG_RESULT([not found])
    ])
], [
    AC_MSG_CHECKING([for optional package $INDICATOR_PKG])
    AC_MSG_RESULT([disabled])
])

dnl ###########################################################################

AC_ARG_ENABLE([libido],
    AC_HELP_STRING([--enable-libido], [Enable libido support])
    AC_HELP_STRING([--disable-libido], [Disable libido support]),
            [], [enable_libido=yes])

AS_IF([test "x$enable_libido" = "xyes" && test "x$have_libindicator" = "xyes"], [
    AS_IF([$PKG_CONFIG --exists "$IDO_PKG" >/dev/null 2>&1], [
        PKG_CHECK_MODULES([LIBIDO], [$IDO_PKG >= 13.10], [
            AC_DEFINE([HAVE_LIBIDO], [1], [Define if "$IDO_PKG" is present])
        ])
    ], [
        AC_MSG_CHECKING([for optional package $IDO_PKG])
        AC_MSG_RESULT([not found])
    ])
], [
    AC_MSG_CHECKING([for optional package $IDO_PKG])
    AC_MSG_RESULT([disabled])
])

dnl ###########################################################################

AC_ARG_WITH([libxklavier], AS_HELP_STRING([--with-libxklavier], [Use libxklavier to manage layouts (instead of LightDM API)]))

AS_IF([test "x$with_libxklavier" = "xyes"],
[
    PKG_CHECK_MODULES([LIBXKLAVIER], [libxklavier], [have_xklavier=yes])
    AC_DEFINE([HAVE_LIBXKLAVIER], [1], [Define if "libxklavier" is present])
],
[])

dnl ###########################################################################

AC_ARG_ENABLE([at-spi-command],
    AC_HELP_STRING([--enable-at-spi-command[=command]], [Try to start at-spi service]])
    AC_HELP_STRING([--disable-at-spi-command], [Do not start at-spi service]),
    [], [])

AS_IF([test "x$enable_at_spi_command" != "xno"],
[
    if test "x$enable_at_spi_command" = "xyes" || test "x$enable_at_spi_command" = "x"; then
        enable_at_spi_command="/usr/lib/at-spi2-core/at-spi-bus-launcher --launch-immediately"
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
        enable_indicator_services_command="upstart --user --startup-event indicator-services-start"
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

IT_PROG_INTLTOOL(0.35.0)
AC_SUBST(GETTEXT_PACKAGE, lightdm-gtk-greeter)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", Gettext package)

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
