dnl Process this file with autoconf to produce a configure script.

AC_INIT(lightdm-gtk-greeter, 1.6.0)
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

AC_ARG_WITH([gtk2], AS_HELP_STRING([--with-gtk2], [Use gtk+-2.0 instead of gtk+-3.0]))

AS_IF([test "x$with_gtk2" = "xyes"],
      [PKG_CHECK_MODULES([GTK], [gtk+-2.0], [have_gtk2=yes])],
      [PKG_CHECK_MODULES([GTK], [gtk+-3.0])])

PKG_CHECK_MODULES([GMODULE], [gmodule-export-2.0])
PKG_CHECK_MODULES([LIGHTDMGOBJECT], [liblightdm-gobject-1 >= 1.3.5])
PKG_CHECK_MODULES([LIBX11], [x11])

dnl ###########################################################################
dnl Optional dependencies
dnl ###########################################################################

INDICATOR_REQUIRED_VERSION=0.3.92

AS_IF([test "x$have_gtk2" = "xyes"],
      [INDICATOR_PKG=indicator-0.4],
      [INDICATOR_PKG=indicator3-0.4])

AC_ARG_ENABLE([libindicator],
    AC_HELP_STRING([--enable-libindicator], [Enable libindicator support])
    AC_HELP_STRING([--disable-libindicator], [Disable libindicator support]),
            [], [enable_libindicator=yes])

AS_IF([test "x$enable_libindicator" = "xyes"], [
    AS_IF([$PKG_CONFIG --exists "$INDICATOR_PKG >= $INDICATOR_REQUIRED_VERSION" >/dev/null 2>&1], [
        PKG_CHECK_MODULES([LIBINDICATOR], [$INDICATOR_PKG >= $INDICATOR_REQUIRED_VERSION], [
            INDICATORDIR=`$PKG_CONFIG --variable=indicatordir $INDICATOR_PKG`
            AC_SUBST(INDICATORDIR)
            AC_DEFINE([HAVE_LIBINDICATOR], [1], [Define if "$INDICATOR_PKG" is present])
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
po/Makefile.in
src/Makefile
])
AC_OUTPUT
