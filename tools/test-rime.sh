#!/usr/bin/env bash
set -euo pipefail

test_binary="${1:?Pass the path to moyu-rime-smoke}"
shift
test_root="$(mktemp -d /tmp/moyu-rime-test-XXXXXX)"
cleanup() {
    case "$test_root" in
        /tmp/moyu-rime-test-*) rm -rf -- "$test_root" ;;
        *) return 1 ;;
    esac
}
trap cleanup EXIT

export MOYU_RIME_TEST_ROOT="$test_root"
export XDG_CONFIG_HOME="$test_root/config"
export XDG_CACHE_HOME="$test_root/cache"
export XDG_DATA_HOME="$test_root/data"
export XDG_RUNTIME_DIR="$test_root/runtime"
mkdir -m 700 "$XDG_RUNTIME_DIR"
export QT_IM_MODULE=""
dbus-run-session -- "$test_binary" "$@"
