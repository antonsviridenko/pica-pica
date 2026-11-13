#!/bin/bash
#
# Builds the pica-client .deb on this machine, for this machine's distribution
# and architecture. For the signed multi-distribution repository use
# build-pica-packages.sh instead.
#
# The package is built from a "make dist" tarball unpacked in a temporary
# directory, the same way container-build.sh does it, so the working tree's
# own build (and its uncommitted changes) are used but never cleaned.
#
# (c) Copyright  2012 - 2026 Anton Sviridenko
# https://picapica.im
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.

set -euo pipefail

SCRIPTDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRCTREE=$(dirname "$SCRIPTDIR")
OUTDIR="$SCRIPTDIR/out-local"
INSTALL_DEPS=0
progname=$(basename "$0")

usage()
{
	cat <<EOF
Usage: $progname [-o DIR] [--install-deps]

Build the pica-client Debian package for this machine.

  -o, --output DIR   Where to put the .deb. Default: $OUTDIR
      --install-deps Install missing build dependencies with apt (uses sudo).
  -h, --help         This message.
EOF
}

die()
{
	echo "$progname: error: $*" >&2
	exit 1
}

while [ $# -gt 0 ]; do
	case "$1" in
	-o|--output)    OUTDIR="${2:?--output needs a value}"; shift 2 ;;
	--install-deps) INSTALL_DEPS=1; shift ;;
	-h|--help)      usage; exit 0 ;;
	*)              die "unknown argument $1 (try --help)" ;;
	esac
done

for tool in dpkg-buildpackage dpkg-checkbuilddeps make tar; do
	command -v "$tool" >/dev/null || die "$tool is missing - sudo apt-get install build-essential dpkg-dev debhelper"
done

WORKDIR=$(mktemp -d /tmp/pica-client-deb.XXXXXX)
trap 'rm -rf "$WORKDIR"' EXIT

echo "=== source tarball"
cd "$SRCTREE"
[ -f Makefile ] || { [ -x configure ] || autoreconf -i -f; ./configure; }
make dist-gzip
tarball=$(ls -t pica-pica-*.tar.gz | head -1)
mv "$tarball" "$WORKDIR/"
tar -xzf "$WORKDIR/$tarball" -C "$WORKDIR"
cd "$WORKDIR/${tarball%.tar.gz}"

echo "=== build dependencies"
if ! dpkg-checkbuilddeps; then
	[ "$INSTALL_DEPS" -eq 1 ] || die "missing build dependencies (listed above). Re-run with --install-deps, or: sudo apt-get build-dep $PWD"
	sudo apt-get build-dep -y ./
fi

echo "=== building"
# debian/rules builds pica-node as well; only the client package is kept
dpkg-buildpackage -b -us -uc

mkdir -p "$OUTDIR"
compgen -G "$WORKDIR/pica-client_*.deb" >/dev/null || die "the build produced no pica-client package"
cp -v "$WORKDIR"/pica-client_*.deb "$OUTDIR/"

echo
echo "Done. Install with: sudo apt install $OUTDIR/$(basename "$WORKDIR"/pica-client_*.deb)"
