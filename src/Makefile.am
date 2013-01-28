sbin_PROGRAMS = lightdm-gtk-greeter

lightdm_gtk_greeter_SOURCES = \
	lightdm-gtk-greeter.c

lightdm_gtk_greeter_CFLAGS = \
	-DHAVE_CONFIG_H \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DGREETER_DATA_DIR=\""$(datadir)/lightdm-gtk-greeter"\" \
	-DCONFIG_FILE=\""$(sysconfdir)/lightdm/lightdm-gtk-greeter.conf"\" \
	-DINDICATOR_DIR=\""$(INDICATORDIR)"\" \
	$(GREETER_CFLAGS) \
	$(LIBINDICATOR_CFLAGS) \
	$(WARN_CFLAGS)

lightdm_gtk_greeter_LDADD = \
	$(GREETER_LIBS) \
	$(LIBINDICATOR_LIBS)
