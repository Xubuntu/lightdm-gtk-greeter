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
