#!/usr/bin/env bash
set -euo pipefail

test_binary="${1:?Pass the path to moyu-rime-smoke}"
app_pid="$(pidof harbour-moyu || true)"
case "$app_pid" in
    ''|*[!0-9]*)
        echo "Launch one instance of the installed harbour-moyu app first." >&2
        exit 1
        ;;
esac

if [[ "$(readlink "/proc/$app_pid/exe")" != "/usr/bin/harbour-moyu" ]]; then
    echo "Relaunch the installed app before checking its sandbox." >&2
    exit 1
fi

# Exercise the proxy belonging to the normally launched app.
bus_socket="/proc/$app_pid/root/run/firejail/mnt/dbus/user"
test -S "$bus_socket"
export DBUS_SESSION_BUS_ADDRESS="unix:path=$bus_socket"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-minimal}"
export QT_IM_MODULE=""
"$test_binary" --app-bus
