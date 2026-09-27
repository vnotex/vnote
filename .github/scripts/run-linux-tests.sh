#!/usr/bin/env bash
set -euo pipefail

# Use Ubuntu's executable, not the separately installed packaging OpenSSL.
export OPENSSL=/usr/bin/openssl
"$OPENSSL" version

umask 077
test_home=$(mktemp -d "${RUNNER_TEMP:-${TMPDIR:-/tmp}}/vnote-tests.XXXXXX")
trap 'rm -rf "$test_home" || true' EXIT
export XDG_DATA_HOME="$test_home/data"
export XDG_CONFIG_HOME="$test_home/config"
export XDG_CACHE_HOME="$test_home/cache"
export XDG_RUNTIME_DIR="$test_home/runtime"
mkdir -p "$XDG_DATA_HOME" "$XDG_CONFIG_HOME" "$XDG_CACHE_HOME" "$XDG_RUNTIME_DIR/keyring"
unset GNOME_KEYRING_CONTROL

# Both ordinary tests and diagnostic reruns need a real, unlocked Secret Service.
dbus-run-session -- bash -s -- "$@" <<'TEST_SESSION'
set -euo pipefail
gnome-keyring-daemon --foreground --unlock --components=secrets \
  --control-directory="$XDG_RUNTIME_DIR/keyring" <<<"vnote-ci-only" &
keyring_pid=$!
cleanup_keyring() {
  kill "$keyring_pid" 2>/dev/null || true
  wait "$keyring_pid" 2>/dev/null || true
}
trap cleanup_keyring EXIT

gdbus wait --session --timeout=15 org.freedesktop.secrets
printf %s vnote-ci-probe | timeout 15s secret-tool store \
  --label="VNote CI readiness" application vnote-ci-probe
test "$(timeout 15s secret-tool lookup application vnote-ci-probe)" = vnote-ci-probe
timeout 15s secret-tool clear application vnote-ci-probe

ctest "$@"
TEST_SESSION
