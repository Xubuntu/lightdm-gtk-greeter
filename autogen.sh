#!/bin/sh
# Run this to generate all the initial makefiles, etc.
#
# Copyright (C) 2010 - 2011, Robert Ancell <robert.ancell@canonical.com>
# Copyright (C) 2017 - 2020, Sean Davis <sean@bluesabre.org>
#
# This program is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version. See http://www.gnu.org/copyleft/gpl.html the full text of the
# license.
#

(type xdt-csource) >/dev/null 2>&1 || {
  cat >&2 <<EOF
autogen.sh: You don't seem to have the Xfce development tools installed on
            your system, which are required to build this software.
            Please install the xfce4-dev-tools package first, it is available
            from your distribution or https://www.xfce.org/.
EOF
  exit 1
}

: ${srcdir:="$(dirname "$0")"}
: ${srcdir=.}

olddir=$(pwd)

cd $srcdir

(test -f configure.ac) || {
        echo "*** ERROR: Directory '$srcdir' does not look like the top-level project directory ***"
        exit 1
}

# shellcheck disable=SC2016
PKG_NAME=$(autoconf --trace 'AC_INIT:$1' configure.ac)

case "$# $NOCONFIGURE" in ('0 ')
        echo "*** WARNING: I am going to run 'configure' with no arguments." >&2
        echo "*** If you wish to pass any to it, please specify them on the" >&2
        echo "*** '$0' command line." >&2
        echo "" >&2
;;esac

aclocal --install || exit 1
autoreconf --verbose --force --install || exit 1

cd "$olddir"
case "$NOCONFIGURE" in ('')
        $srcdir/configure "$@" || exit 1

        case "$1" in ("--help") exit 0 ;;(*)
                echo "Now type 'make' to compile $PKG_NAME" || exit 1
        ;;esac
;;(*)
        echo "Skipping configure process."
;;esac
