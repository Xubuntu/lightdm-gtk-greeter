#!/bin/sh
set -eu
for tool in python3 xvfb-run xdotool dbus-run-session; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "SKIP: GTK smoke tests require $tool" >&2
        exit 77
    fi
done
exec xvfb-run -a -s "-screen 0 ${GREETER_TEST_SCREEN:-1280x800x24}" dbus-run-session -- sh -c \
    'export DBUS_SYSTEM_BUS_ADDRESS="$DBUS_SESSION_BUS_ADDRESS"; exec python3 "$@"' \
    sh "$(dirname "$0")/greeter-smoke.py" "$@"
