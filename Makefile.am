#
# Copyright (C) 2010 - 2011, Robert Ancell <robert.ancell@canonical.com>
# Copyright (C) 2012, Lionel Le Folgoc <mrpouit@ubuntu.com>
# Copyright (C) 2018, Sean Davis <smd.seandavis@gmail.com>
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. See http://www.gnu.org/copyleft/gpl.html the full text of the
# license.
#

ACLOCAL_AMFLAGS = -I m4

SUBDIRS = data po src

EXTRA_DIST = \
	autogen.sh \
	lightdm-gtk-greeter.doap

DISTCLEANFILES = \
	gtk-doc.make \
	omf.make \
	xmldocs.make

.PHONY: ChangeLog

ChangeLog:
	(cd $(top_srcdir) && bzr log --gnu-changelog > .changelog.tmp \
	&& mv .changelog.tmp ChangeLog; rm -f .changelog.tmp) \
	|| (touch ChangeLog; echo 'Bzr directory not found: installing possibly empty changelog.' >&2)

dist-hook: ChangeLog

-include $(top_srcdir)/git.mk
