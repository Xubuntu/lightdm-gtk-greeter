sbin_PROGRAMS = lightdm-gtk-greeter

lightdm_gtk_greeter_built_sources = \
	lightdm-gtk-greeter-ui.h

lightdm_gtk_greeter_SOURCES = \
	$(lightdm_gtk_greeter_built_sources) \
	lightdm-gtk-greeter.c

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-DHAVE_CONFIG_H \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DGREETER_DATA_DIR=\""$(datadir)/lightdm-gtk-greeter"\" \
	-DCONFIG_FILE=\""$(sysconfdir)/lightdm/lightdm-gtk-greeter.conf"\" \
	-DINDICATOR_DIR=\""$(INDICATORDIR)"\" \
	-DUNITY_INDICATOR_DIR=\""$(UNITY_INDICATORDIR)"\" \
	$(WARN_CFLAGS)

lightdm_gtk_greeter_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GMODULE_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(LIGHTDMGOBJECT_CFLAGS) \
	$(LIBX11_CFLAGS) \
	$(LIBINDICATOR_CFLAGS) \
	$(LIBIDO_CFLAGS)

lightdm_gtk_greeter_LDADD = \
	$(GTK_LIBS) \
	$(GMODULE_LIBS) \
	$(GTHREAD_LIBS) \
	$(LIGHTDMGOBJECT_LIBS) \
	$(LIBX11_LIBS) \
	$(LIBINDICATOR_LIBS) \
	$(LIBIDO_LIBS)

if MAINTAINER_MODE

lightdm-gtk-greeter-ui.h: $(srcdir)/lightdm-gtk-greeter.glade Makefile
	$(AM_V_GEN) exo-csource --static --strip-comments --strip-content --name=lightdm_gtk_greeter_ui $< >$@

DISTCLEANFILES = \
	$(lightdm_gtk_greeter_built_sources)

BUILT_SOURCES = \
	$(lightdm_gtk_greeter_built_sources)

endif

EXTRA_DIST = \
	lightdm-gtk-greeter.glade
