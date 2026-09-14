#!/bin/bash
#
# Builds the pica-pica packages inside one container. Not meant to be run by
# hand - build-pica-packages.sh starts a container per target and runs this
# as its entry point.
#
# What it expects to find, all put there by the orchestrator:
#
#   /src/pica-source.tar     the pica-pica source tarball, as produced by
#                            "make dist" - bind mounted under that fixed name
#                            whatever it is really called, and whatever it is
#                            compressed with
#   /scripts/                this directory, so that the backport script is
#                            reachable
#   /out/                    writable; artifacts are left in
#                            /out/<codename>/<arch>/
#
# and in the environment: PICA_DISTRO (codename) and PICA_ARCH (dpkg name).
#
# (c) Copyright  2012 - 2026 Anton Sviridenko
# https://picapica.im
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.

set -euo pipefail

DISTRO="${PICA_DISTRO:?PICA_DISTRO is not set}"
ARCH="${PICA_ARCH:-$(dpkg --print-architecture)}"

export DEBIAN_FRONTEND=noninteractive

note()
{
	echo "==> [$DISTRO/$ARCH] $*"
}

die()
{
	echo "[$DISTRO/$ARCH] error: $*" >&2
	exit 1
}

note "starting, $(. /etc/os-release && echo "$PRETTY_NAME"), dpkg arch $(dpkg --print-architecture)"

note "refreshing the package lists"
apt-get update -qq

# autoconf/automake because the pica-pica tree does not carry a generated
# configure - it is not in git, so the tree has to be bootstrapped here.
note "installing the base build tooling"
apt-get install -qq -y --no-install-recommends \
	build-essential \
	ca-certificates \
	curl \
	dpkg-dev \
	debhelper \
	autoconf \
	automake \
	pkgconf

# webrtc-audio-processing, if this distribution has nothing new enough.
# The script decides that for itself and exits without doing anything when
# the archive already has 1.x, so it is safe to call unconditionally.
note "checking whether webrtc-audio-processing needs backporting"
WEBRTC_OUT=/tmp/webrtc-backport-out
mkdir -p "$WEBRTC_OUT"

bash /scripts/build-webrtc-audio-processing.sh --output "$WEBRTC_OUT" "$DISTRO"

if compgen -G "$WEBRTC_OUT/*.deb" >/dev/null; then
	note "installing the backported webrtc-audio-processing"
	apt-get install -qq -y --no-install-recommends "$WEBRTC_OUT"/*.deb

	# These go into the repository too - without them the pica-client
	# package would be uninstallable on this distribution.
	mkdir -p "/out/$DISTRO/$ARCH"
	cp -v "$WEBRTC_OUT"/*.deb "/out/$DISTRO/$ARCH/"
else
	note "the archive's own webrtc-audio-processing is good enough"
fi

# --- pica-pica ------------------------------------------------------------

note "unpacking the source"
rm -rf /build
mkdir -p /build
tar -xf /src/pica-source.tar -C /build

# "make dist" produces exactly one top level directory, pica-pica-<version>.
SRCDIR=$(find /build -maxdepth 1 -mindepth 1 -type d | head -1)
[ -n "$SRCDIR" ] || die "the source tarball did not contain a directory"
cd "$SRCDIR"
note "source is $(basename "$SRCDIR")"

[ -f debian/control ] ||
	die "no debian/control in the tarball - was it made by \"make dist\" in a tree that has debian/?"

note "installing pica-pica's build dependencies"
# Reading them straight out of debian/control means the alternative
# "libwebrtc-audio-processing-dev (>= 1.3) | libwebrtc-audio-processing-1-dev"
# is resolved by apt rather than second-guessed here.
apt-get build-dep -qq -y ./ ||
	die "could not satisfy the build dependencies from debian/control"

# A "make dist" tarball already carries the generated configure, so there is
# normally nothing to do here. The fallback is for a tarball made some other
# way; autoconf and automake are installed above either way, because if the
# tarball's timestamps make Makefile.am look newer than Makefile.in, automake's
# own rebuild rules fire during the build and want them.
if [ -x configure ]; then
	note "the tarball carries a generated configure - not bootstrapping"
else
	note "no configure in the tarball - bootstrapping the build system"
	autoreconf -i -f
fi

note "building the packages"
# Binary-only: dpkg-source is not involved, so this does not need an .orig
# tarball alongside a 3.0 (quilt) source format.
dpkg-buildpackage -b -us -uc

note "collecting artifacts"
mkdir -p "/out/$DISTRO/$ARCH"
cd /build
found=0
for f in *.deb *.changes *.buildinfo; do
	[ -e "$f" ] || continue
	cp -v "$f" "/out/$DISTRO/$ARCH/"
	case "$f" in *.deb) found=1 ;; esac
done

[ "$found" -eq 1 ] || die "the build produced no .deb files"

note "done"
