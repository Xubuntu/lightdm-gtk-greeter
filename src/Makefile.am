#
# Copyright (C) 2010 - 2011, Robert Ancell <robert.ancell@canonical.com>
# Copyright (C) 2012 - 2013, Lionel Le Folgoc <mrpouit@ubuntu.com>
# Copyright (C) 2013 - 2020, Sean Davis <sean@bluesabre.org>
# Copyright (C) 2014, Andrew P. <pan.pav.7c5@gmail.com>
# Copyright (C) 2015, Simon Steinbeiß <ochosi@shimmerproject.org>
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. See http://www.gnu.org/copyleft/gpl.html the full text of the
# license.
#

sbin_PROGRAMS = lightdm-gtk-greeter

lightdm_gtk_greeter_built_sources = \
	lightdm-gtk-greeter-ui.h \
	lightdm-gtk-greeter-css-fallback.h \
	lightdm-gtk-greeter-css-application.h

lightdm_gtk_greeter_SOURCES = \
	$(lightdm_gtk_greeter_built_sources) \
	lightdm-gtk-greeter.c \
	greeterbackground.c \
	greeterbackground.h \
	greeterconfiguration.c \
	greeterconfiguration.h \
	greetermenubar.c \
	greetermenubar.h \
	greeterdeprecated.c \
	greeterdeprecated.h

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DHAVE_CONFIG_H \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DGREETER_DATA_DIR=\""$(datadir)/lightdm-gtk-greeter"\" \
	-DCONFIG_FILE=\""$(sysconfdir)/lightdm/lightdm-gtk-greeter.conf"\" \
	-DINDICATOR_DIR=\""$(INDICATORDIR)"\" \
	-DUNITY_INDICATOR_DIR=\""$(UNITY_INDICATORDIR)"\" \
	-DAYATANA_INDICATOR_DIR=\""$(AYATANA_INDICATORDIR)"\" \
	-DSYSTEMD_SERVICE_DIR=\""$(SYSTEMD_SERVICE_DIR)"\" \
	$(WARN_CFLAGS)

lightdm_gtk_greeter_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GMODULE_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(LIGHTDMGOBJECT_CFLAGS) \
	$(LIBX11_CFLAGS)

lightdm_gtk_greeter_LDADD = \
	$(GTK_LIBS) \
	$(GMODULE_LIBS) \
	$(GTHREAD_LIBS) \
	$(LIGHTDMGOBJECT_LIBS) \
	$(LIBX11_LIBS) \
	$(LIBXKLAVIER_LIBS)\
	-lm

if HAVE_UNITY_LIBINDICATOR

lightdm_gtk_greeter_CFLAGS += \
	$(UNITY_LIBINDICATOR_CFLAGS) \
	$(UNITY_LIBIDO_CFLAGS)

lightdm_gtk_greeter_LDADD += \
	$(UNITY_LIBINDICATOR_LIBS) \
	$(UNITY_LIBIDO_LIBS)

else

lightdm_gtk_greeter_CFLAGS += \
	$(AYATANA_LIBINDICATOR_CFLAGS) \
	$(AYATANA_LIBIDO_CFLAGS)

lightdm_gtk_greeter_LDADD += \
	$(AYATANA_LIBINDICATOR_LIBS) \
	$(AYATANA_LIBIDO_LIBS)

endif

if MAINTAINER_MODE

lightdm-gtk-greeter-ui.h: $(srcdir)/lightdm-gtk-greeter.glade Makefile
	$(AM_V_GEN) xdt-csource --static --strip-comments --strip-content --name=lightdm_gtk_greeter_ui $< >$@

lightdm-gtk-greeter-css-fallback.h: $(srcdir)/lightdm-gtk-greeter-fallback.css Makefile
	$(AM_V_GEN) xdt-csource --static --name=lightdm_gtk_greeter_css_fallback $< >$@

lightdm-gtk-greeter-css-application.h: $(srcdir)/lightdm-gtk-greeter-application.css Makefile
	$(AM_V_GEN) xdt-csource --static --name=lightdm_gtk_greeter_css_application $< >$@

DISTCLEANFILES = \
	$(lightdm_gtk_greeter_built_sources)

BUILT_SOURCES = \
	$(lightdm_gtk_greeter_built_sources)

endif

EXTRA_DIST = \
	lightdm-gtk-greeter.glade \
	lightdm-gtk-greeter-fallback.css \
	lightdm-gtk-greeter-application.css

-include $(top_srcdir)/git.mk
