#!/usr/bin/env bash
# Build the hub deb (orca-server-linux-amd64.deb family) with plain dpkg-deb —
# no debhelper, so the whole recipe stays visible in this file.
#
#   ORCA_VERSION=0.0.1 bash packaging/build-deb.sh [output-dir]
#
# Layout follows docs/NAMING.md:
#   /opt/orca-web/dist-server/orca-server   executable
#   /etc/orca-web/server.json               config (conffile)
#   /lib/systemd/system/orca-web.service    unit
#   journalctl -u orca-web                  logs
# Size gate: the deb must stay <= 10 MiB (docs/DEPENDENCIES.md).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="${ORCA_VERSION:-0.0.1}"
OUT_DIR="${1:-$ROOT/dist}"
DEB_MAX_BYTES=$((10 * 1024 * 1024))

BIN="$ROOT/build/server/orca-server"
if [ ! -x "$BIN" ]; then
    echo "[orca-deb] binary missing; running scripts/build.sh (configure + build + ctest + size gate)"
    ORCA_VERSION="$VERSION" BUILD_TYPE=Release bash "$ROOT/scripts/build.sh"
fi
test -x "$BIN"

STAGING="$(mktemp -d /tmp/orca-deb.XXXXXX)"
trap 'rm -rf "$STAGING"' EXIT

install -D -m 755 "$BIN" "$STAGING/opt/orca-web/dist-server/orca-server"
install -D -m 644 "$ROOT/packaging/deb/server.json" "$STAGING/etc/orca-web/server.json"
install -D -m 644 "$ROOT/packaging/deb/orca-web.service" "$STAGING/lib/systemd/system/orca-web.service"

install -D -m 644 "$ROOT/packaging/deb/conffiles" "$STAGING/DEBIAN/conffiles"
install -D -m 755 "$ROOT/packaging/deb/postinst" "$STAGING/DEBIAN/postinst"
install -D -m 755 "$ROOT/packaging/deb/prerm" "$STAGING/DEBIAN/prerm"
sed "s/__ORCA_VERSION__/$VERSION/" "$ROOT/packaging/deb/control" > "$STAGING/DEBIAN/control"

mkdir -p "$OUT_DIR"
DEB="$OUT_DIR/orca-server-$VERSION-linux-amd64.deb"
dpkg-deb --build --root-owner-group "$STAGING" "$DEB"

BYTES=$(stat -c %s "$DEB")
KILO=$(awk "BEGIN{printf \"%.1f\", $BYTES/1024}")
echo "[orca-deb] size    : $BYTES bytes (${KILO} KiB)"
if [ "$BYTES" -gt "$DEB_MAX_BYTES" ]; then
    echo "[orca-deb] SIZE GATE FAILED: $BYTES > $DEB_MAX_BYTES" >&2
    exit 1
fi
echo "[orca-deb] verdict : OK (budget 10485760 bytes)"

# Release convention: every artifact ships with a .sha256 sidecar (NAMING.md).
( cd "$OUT_DIR" && sha256sum "$(basename "$DEB")" > "$(basename "$DEB").sha256" )
echo "[orca-deb] wrote   : $DEB"
