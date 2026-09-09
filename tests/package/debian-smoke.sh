#!/bin/bash
set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <bookworm|trixie> </path/to/package.deb>" >&2
    exit 2
fi

SUITE="$1"
PACKAGE="$(readlink -f "$2")"
[ -f "$PACKAGE" ] || { echo "Package not found: $PACKAGE" >&2; exit 2; }
command -v mmdebstrap >/dev/null || { echo "mmdebstrap is required" >&2; exit 2; }

if [ "$(id -u)" -eq 0 ]; then
    SUDO=()
else
    sudo -n true
    SUDO=(sudo -n)
fi

ROOT="$(mktemp -d /tmp/testo-deb-smoke.XXXXXX)"
DEV_MOUNTED=false
PROC_MOUNTED=false

cleanup() {
    set +e
    if $PROC_MOUNTED; then "${SUDO[@]}" umount "$ROOT/proc"; fi
    if $DEV_MOUNTED; then "${SUDO[@]}" umount "$ROOT/dev"; fi
    if mountpoint -q "$ROOT/proc" || mountpoint -q "$ROOT/dev"; then
        echo "Refusing to remove smoke root while a mount is still active: $ROOT" >&2
        return
    fi
    "${SUDO[@]}" rm -rf "$ROOT"
}
trap cleanup EXIT

"${SUDO[@]}" mmdebstrap --mode=root --variant=minbase \
    --include=ca-certificates,apt,gpgv,systemd-sysv \
    "$SUITE" "$ROOT" http://deb.debian.org/debian >/dev/null

"${SUDO[@]}" mkdir -p "$ROOT/dev" "$ROOT/proc" "$ROOT/tmp"
"${SUDO[@]}" mount -t tmpfs -o mode=755,nosuid,dev tmpfs "$ROOT/dev"
DEV_MOUNTED=true
"${SUDO[@]}" mknod -m 666 "$ROOT/dev/null" c 1 3
"${SUDO[@]}" mknod -m 666 "$ROOT/dev/zero" c 1 5
"${SUDO[@]}" mknod -m 666 "$ROOT/dev/full" c 1 7
"${SUDO[@]}" mknod -m 666 "$ROOT/dev/random" c 1 8
"${SUDO[@]}" mknod -m 666 "$ROOT/dev/urandom" c 1 9
"${SUDO[@]}" mknod -m 666 "$ROOT/dev/tty" c 5 0
"${SUDO[@]}" mount -t proc proc "$ROOT/proc"
PROC_MOUNTED=true
"${SUDO[@]}" cp "$PACKAGE" "$ROOT/tmp/package.deb"

run_chroot() {
    "${SUDO[@]}" /usr/sbin/chroot "$ROOT" "$@"
}

run_chroot apt-get update -qq
NAME="$(dpkg-deb -f "$PACKAGE" Package)"
VERSION="$(dpkg-deb -f "$PACKAGE" Version)"
echo "Testing $NAME $VERSION on Debian $SUITE"

if ! run_chroot /bin/sh -c \
    'DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends /tmp/package.deb >/tmp/package-install.log 2>&1'; then
    run_chroot tail -80 /tmp/package-install.log || true
    exit 1
fi

case "$NAME" in
    testo)
        run_chroot /usr/bin/testo --version
        ;;
    testo-guest-additions)
        run_chroot /usr/sbin/testo-guest-additions --version
        run_chroot /usr/sbin/testo-guest-additions-cli --version
        run_chroot test -x /etc/init.d/testo-guest-additions
        run_chroot test -f /lib/systemd/system/testo-guest-additions.service
        ;;
    testo-nn-server)
        run_chroot /usr/sbin/testo-nn-server --version
        run_chroot test -x /etc/init.d/testo-nn-server
        run_chroot test -f /lib/systemd/system/testo-nn-server.service
        run_chroot test -f /lib/systemd/system/testo-nn-server@.service
        ;;
    *)
        echo "Unsupported package for smoke test: $NAME" >&2
        exit 2
        ;;
esac

if ! run_chroot /bin/sh -c \
    "DEBIAN_FRONTEND=noninteractive apt-get purge -y '$NAME' >/tmp/package-purge.log 2>&1"; then
    run_chroot tail -80 /tmp/package-purge.log || true
    exit 1
fi
if run_chroot dpkg-query -W "$NAME" >/dev/null 2>&1; then
    echo "Package remains installed after purge: $NAME" >&2
    exit 1
fi

echo "$NAME package smoke passed on Debian $SUITE"
