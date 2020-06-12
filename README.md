# LightDM GTK+ Greeter
**LightDM GTK+ Greeter** is a greeter that has moderate requirements (GTK+).

This project is one of many greeters for [LightDM](https://github.com/canonical/lightdm).

## About LightDM

LightDM is a cross-desktop display manager. A display manager is a daemon that:

- Runs display servers (e.g. X) where necessary.
- Runs greeters to allow users to pick which user account and session type to use.
- Allows greeters to perform authentication using PAM.
- Runs session processes once authentication is complete.
- Provides remote graphical login options.

Key features of LightDM are:
- Cross-desktop - supports different desktop technologies.
- Supports different display technologies (X, Mir, Wayland ...).
- Lightweight - low memory usage and fast performance.
- Guest sessions.
- Supports remote login (incoming - XDMCP, VNC, outgoing - XDMCP, pluggable).
- Comprehensive test suite.

## Configuration

LightDM configuration is provided by the following files:

```
/usr/share/lightdm/lightdm.conf.d/*.conf
/etc/lightdm/lightdm.conf.d/*.conf
/etc/lightdm/lightdm.conf
```

**LightDM GTK+ Greeter uses `lightdm-gtk-greeter.conf` for it's configuration.**

System provided configuration should be stored in `/usr/share/lightdm/lightdm.conf.d/`. System administrators can override this configuration by adding files to `/etc/lightdm/lightdm.conf.d/` and `/etc/lightdm/lightdm.conf`. Files are read in the above order and combined together to make the LightDM configuration.

For example, if a sysadmin wanted to override the system configured default session (provided in `/usr/share/lightdm/lightdm.conf.d`) they should make a file `/etc/lightdm/lightdm.conf.d/50-myconfig.conf` with the following:

```
[Seat:*]
user-session=mysession
```

Configuration is in keyfile format. For most installations you will want to change the keys in the `[Seat:*]` section as this applies to all seats on the system (normally just one). A configuration file showing all the possible keys is provided in [`data/lightdm.conf`](https://github.com/Canonical/lightdm/blob/master/data/lightdm.conf).

## Questions

Questions about LightDM and LightDM GTK+ Greeter should be asked on the [mailing list](http://lists.freedesktop.org/mailman/listinfo/lightdm). All questions are welcome.

[Stack Overflow](http://stackoverflow.com/search?q=lightdm) and [Ask Ubuntu](http://askubuntu.com/search?q=lightdm) are good sites for frequently asked questions.

## Links
 - [Homepage](https://github.com/xubuntu/lightdm-gtk-greeter)
 - [Releases](https://github.com/xubuntu/lightdm-gtk-greeter/releases)
 - [Bug Reports](https://github.com/xubuntu/lightdm-gtk-greeter/issues)
 - [Translations](https://www.transifex.com/xubuntu/lightdm-gtk-greeter)
 - [Wiki](https://github.com/xubuntu/lightdm-gtk-greeter/wiki)
