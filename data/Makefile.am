xgreetersdir = $(datadir)/xgreeters
dist_xgreeters_DATA = lightdm-gtk-greeter.desktop

configdir = $(sysconfdir)/lightdm
dist_config_DATA = lightdm-gtk-greeter.conf

EXTRA_DIST =				\
	$(Data_DATA)

Datadir = $(datadir)/icons/hicolor/scalable/places

Data_DATA =				\
	gnome_badge-symbolic.svg	\
	gnome-classic_badge-symbolic.svg	\
	gnome-fallback_badge-symbolic.svg	\
	gnome-fallback-compiz_badge-symbolic.svg	\
	gnome-flashback_badge-symbolic.svg	\
	gnome-flashback-compiz_badge-symbolic.svg	\
	gnome-shell_badge-symbolic.svg	\
	kde_badge-symbolic.svg	\
	kde-plasma_badge-symbolic.svg	\
	lubuntu_badge-symbolic.svg	\
	lxde_badge-symbolic.svg	\
	ubuntu-2d_badge-symbolic.svg	\
	ubuntu_badge-symbolic.svg	\
	ubuntustudio_badge-symbolic.svg	\
	xfce_badge-symbolic.svg	\
	xterm_badge-symbolic.svg	\
	xubuntu_badge-symbolic.svg

gtk_update_icon_cache = gtk-update-icon-cache -f -t $(datadir)/icons/hicolor

install-data-hook: update-icon-cache
uninstall-hook: update-icon-cache
update-icon-cache:
	@-if test -z "$(DESTDIR)"; then \
		echo "Updating Gtk icon cache."; \
		$(gtk_update_icon_cache); \
	else \
		echo "*** Icon cache not updated.  After (un)install, run this:"; \
		echo "***   $(gtk_update_icon_cache)"; \
	fi
