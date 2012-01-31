dnl Process this file with autoconf to produce a configure script.

AC_INIT(lightdm-gtk-greeter, 1.1.3)
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
dnl Dependencies
dnl ###########################################################################

PKG_CHECK_MODULES(GREETER, [
    gtk+-3.0
    x11
    liblightdm-gobject-1
    gmodule-export-2.0
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
