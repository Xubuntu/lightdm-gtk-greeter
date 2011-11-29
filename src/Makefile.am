sbin_PROGRAMS = lightdm-gtk-greeter

lightdm_gtk_greeter_SOURCES = \
	lightdm-gtk-greeter.c

lightdm_gtk_greeter_CFLAGS = \
	-DGETTEXT_PACKAGE=\"$(GETTEXT_PACKAGE)\" \
	-DLOCALEDIR=\""$(localedir)"\" \
	-DGREETER_DATA_DIR=\""$(greeterdatadir)"\" \
	-DCONFIG_FILE=\""$(configdir)/lightdm-gtk-greeter.conf"\" \
	$(GREETER_CFLAGS) \
	$(WARN_CFLAGS)

lightdm_gtk_greeter_LDADD = \
	$(GREETER_LIBS)

DISTCLEANFILES = \
	Makefile.in
