#!/bin/bash
#
# Build a backported webrtc-audio-processing 1.3 for a distribution whose
# archive does not carry it.
#
# pica-client needs AEC3, which arrived in the 1.x series. Ubuntu 24.04 still
# ships 0.3.1, which predates it entirely and has a different API. Debian 13
# and Ubuntu 25.10 or newer already have 1.3, so on those this script does
# nothing unless forced.
#
# Debian's own packaging is taken as-is and given a two-part delta:
#
#   - The -dev binary package is renamed back to the name Debian itself used
#     for 1.3-1, libwebrtc-audio-processing-1-dev. Debian renamed it to the
#     unversioned libwebrtc-audio-processing-dev in 1.3-2, after dropping 0.3
#     from the archive - but on a distribution that still ships 0.3 under
#     that unversioned name, taking it over would replace a package other
#     things build against. Nothing else collides: the runtime package
#     (libwebrtc-audio-processing-1-3), the SONAME, the include directory and
#     the pkg-config name are all versioned by upstream already.
#
#   - The Breaks/Replaces relationships that record Debian's own rename are
#     dropped. They are meaningless once the package carries the old name
#     again, and on the audio-coding package they would otherwise declare a
#     Replaces on the distribution's 0.3 -dev package, which is the exact
#     thing this is trying not to disturb.
#
# Run this inside the target distribution - a container from
# build-pica-packages.sh, an sbuild chroot, or a machine running it. It uses
# apt, so it has to be somewhere that apt describes the target archive.
#
# (c) Copyright  2012 - 2026 Anton Sviridenko
# https://picapica.im
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.

set -euo pipefail

UPSTREAM_VERSION=1.3
DEBIAN_REVISION=3
DEBIAN_MIRROR=https://deb.debian.org/debian
POOL="$DEBIAN_MIRROR/pool/main/w/webrtc-audio-processing"

# The version below which the distribution's own package is no good to us.
# 0.3 has no AEC3; see the note at the top.
MINIMUM_USABLE_VERSION=1.3

ARCH=""
OUTDIR="$PWD/out"
FORCE=0
RENAME_DEV=1
KEEP_BUILD=0

progname=$(basename "$0")

usage()
{
	cat <<EOF
Usage: $progname [OPTIONS] DISTRO

Build webrtc-audio-processing $UPSTREAM_VERSION-$DEBIAN_REVISION as a backport for DISTRO.

DISTRO is the target distribution codename - noble, resolute, trixie. It is
used in the package version (${UPSTREAM_VERSION}-${DEBIAN_REVISION}~DISTRO1) and the changelog entry, so that
the distribution's own package supersedes this one if it ever gains a newer
version.

Options:
  -a, --arch ARCH     Architecture to build for. Accepts either dpkg names
                      (amd64, arm64, i386) or uname names (x86_64, aarch64,
                      i686), which are normalised. Defaults to the
                      architecture this is running on. Anything else is a
                      cross build and needs crossbuild-essential-ARCH, which
                      this will install.
  -o, --output DIR    Where to put the resulting .deb files.
                      Default: $OUTDIR
  -f, --force         Build even when the distribution already has a good
                      enough webrtc-audio-processing.
      --no-rename-dev Keep Debian's unversioned libwebrtc-audio-processing-dev
                      name. Only correct where the distribution has no 0.3 of
                      its own to collide with.
      --keep-build    Leave the build tree behind for inspection.
  -h, --help          This message.

Exit status:
  0  packages built, or the distribution already has a good enough one
  1  something went wrong
EOF
}

die()
{
	echo "$progname: error: $*" >&2
	exit 1
}

note()
{
	echo "==> $*"
}

# dpkg and uname disagree about architecture names, and people reach for
# whichever they saw last. Take both.
normalise_arch()
{
	case "$1" in
	x86_64|amd64)          echo amd64 ;;
	aarch64|arm64)         echo arm64 ;;
	i386|i486|i586|i686)   echo i386 ;;
	armv7l|armhf)          echo armhf ;;
	ppc64le|ppc64el)       echo ppc64el ;;
	*)                     echo "$1" ;;
	esac
}

while [ $# -gt 0 ]; do
	case "$1" in
	-a|--arch)        ARCH=$(normalise_arch "${2:?--arch needs a value}"); shift 2 ;;
	-o|--output)      OUTDIR="${2:?--output needs a value}"; shift 2 ;;
	-f|--force)       FORCE=1; shift ;;
	--no-rename-dev)  RENAME_DEV=0; shift ;;
	--keep-build)     KEEP_BUILD=1; shift ;;
	-h|--help)        usage; exit 0 ;;
	-*)               die "unknown option $1 (try --help)" ;;
	*)                break ;;
	esac
done

[ $# -eq 1 ] || { usage >&2; exit 1; }
DISTRO="$1"

[ "$(id -u)" -eq 0 ] || die "this installs build dependencies, so it needs to run as root"

command -v apt-get >/dev/null || die "no apt-get - this has to run inside the target distribution"

HOST_ARCH=$(dpkg --print-architecture)
BUILD_ARCH="${ARCH:-$HOST_ARCH}"
CROSS=0
[ "$BUILD_ARCH" = "$HOST_ARCH" ] || CROSS=1

VERSION="${UPSTREAM_VERSION}-${DEBIAN_REVISION}~${DISTRO}1"

export DEBIAN_FRONTEND=noninteractive

# Is it needed at all?
#
# apt-cache policy reports the candidate the target archive would install.
# Anything from 1.x on is fine and there is no reason to ship our own.
if [ "$FORCE" -eq 0 ]; then
	apt-get update -qq

	candidate=$(apt-cache policy libwebrtc-audio-processing-dev 2>/dev/null |
	            sed -n 's/^  Candidate: //p')

	if [ -n "$candidate" ] && [ "$candidate" != "(none)" ] &&
	   dpkg --compare-versions "$candidate" ge "$MINIMUM_USABLE_VERSION"; then
		note "$DISTRO already has libwebrtc-audio-processing-dev $candidate - nothing to build"
		note "(pass --force to build the backport anyway)"
		exit 0
	fi

	if [ -n "$candidate" ] && [ "$candidate" != "(none)" ]; then
		note "$DISTRO has libwebrtc-audio-processing-dev $candidate, which predates AEC3 - backporting $VERSION"
	else
		note "$DISTRO has no libwebrtc-audio-processing-dev at all - backporting $VERSION"
	fi
fi

note "building webrtc-audio-processing $VERSION for $BUILD_ARCH"

# Just enough to fetch and unpack a Debian source package. The build
# dependencies proper come from the extracted debian/control further down,
# via apt-get build-dep, so that this does not have to be kept in step with
# Debian's packaging by hand.
#
# devscripts is deliberately not installed: the changelog entry below is
# written directly, so this works without DEBEMAIL or DEBFULLNAME being set.
note "installing the tools needed to fetch the source"
apt-get install -qq -y --no-install-recommends \
	build-essential \
	ca-certificates \
	curl \
	dpkg-dev \
	xz-utils

if [ "$CROSS" -eq 1 ]; then
	note "cross building for $BUILD_ARCH from $HOST_ARCH"
	dpkg --add-architecture "$BUILD_ARCH"
	apt-get update -qq
	apt-get install -qq -y --no-install-recommends \
		"crossbuild-essential-$BUILD_ARCH"
fi

BUILDROOT=$(mktemp -d /tmp/webrtc-backport.XXXXXX)
cleanup()
{
	[ "$KEEP_BUILD" -eq 1 ] && { note "build tree left at $BUILDROOT"; return; }
	rm -rf "$BUILDROOT"
}
trap cleanup EXIT

cd "$BUILDROOT"

DSC="webrtc-audio-processing_${UPSTREAM_VERSION}-${DEBIAN_REVISION}.dsc"

note "fetching Debian's source package"
for f in "$DSC" \
         "webrtc-audio-processing_${UPSTREAM_VERSION}.orig.tar.gz" \
         "webrtc-audio-processing_${UPSTREAM_VERSION}-${DEBIAN_REVISION}.debian.tar.xz"; do
	curl -fsSL --retry 3 -o "$f" "$POOL/$f" || die "could not fetch $f from $POOL"
done

# dpkg-source checks the tarballs against the checksums in the .dsc as it
# extracts, so a corrupted or substituted tarball fails here. It does not
# check the .dsc's own OpenPGP signature; dscverify from devscripts does that
# and needs the Debian keyring, so it is done only when both are present.
if command -v dscverify >/dev/null && [ -d /usr/share/keyrings ]; then
	if dscverify --keyring /usr/share/keyrings/debian-keyring.gpg "$DSC" >/dev/null 2>&1; then
		note "the .dsc signature verifies against the Debian keyring"
	else
		note "WARNING: could not verify the .dsc signature (keyring missing or untrusted signer)"
	fi
fi

note "extracting"
dpkg-source -x "$DSC" src >/dev/null
cd src

# Straight out of the extracted debian/control, so Debian's own list is the
# source of truth rather than a copy of it kept here.
note "installing Debian's build dependencies for it"
if [ "$CROSS" -eq 1 ]; then
	apt-get build-dep -qq -y -a "$BUILD_ARCH" ./ ||
		die "could not satisfy the build dependencies for $BUILD_ARCH"
else
	apt-get build-dep -qq -y ./ ||
		die "could not satisfy the build dependencies from debian/control"
fi

# --- the delta ------------------------------------------------------------

if [ "$RENAME_DEV" -eq 1 ]; then
	note "renaming libwebrtc-audio-processing-dev to libwebrtc-audio-processing-1-dev"

	# Only the -dev package is renamed. The runtime package keeps Debian's
	# name (libwebrtc-audio-processing-1-3), which does not collide with
	# 0.3's libwebrtc-audio-processing1 and is what pica-client will depend
	# on through shlibs.
	sed -i 's/^Package: libwebrtc-audio-processing-dev$/Package: libwebrtc-audio-processing-1-dev/' \
		debian/control

	grep -q '^Package: libwebrtc-audio-processing-1-dev$' debian/control ||
		die "the -dev package rename did not apply - has Debian's control changed?"
fi

# Drop the Breaks/Replaces that record Debian's 1.3-1 -> 1.3-2 rename.
#
# With the -dev package carrying its old name again these say nothing useful,
# and the pair on libwebrtc-audio-coding-dev is actively wrong here: it
# declares Breaks/Replaces on libwebrtc-audio-processing-dev (<< 1.3-2~),
# which on a distribution still shipping 0.3 under that name matches the
# distribution's own package.
note "dropping Debian's rename-era Breaks/Replaces"
# perl rather than python3: a minimal Debian or Ubuntu container has no
# python3, but debhelper depends on perl so it is always here.
#
# A deb822 field runs from its name to the next line starting in column zero,
# continuation lines being those that start with whitespace - hence the
# (?:\n[ \t][^\n]*)* . Only the relationships naming the -dev packages are
# dropped; anything else Debian declares is left alone.
perl -0777 -i -pe '
	s{^((?:Breaks|Replaces):[^\n]*(?:\n[ \t][^\n]*)*\n)}{
		$1 =~ /libwebrtc-audio-(?:processing|coding)(?:-1)?-dev/ ? q{} : $1
	}gme;
' debian/control

# A changelog entry written directly rather than through dch, so that this
# does not depend on DEBEMAIL/DEBFULLNAME being set in the container.
#
# The ~DISTRO1 suffix sorts BELOW the plain Debian version, so if the
# distribution ever ships 1.3-3 itself, apt upgrades away from this backport
# rather than being held back by it.
note "adding the changelog entry for $VERSION"
{
	cat <<EOF
webrtc-audio-processing ($VERSION) $DISTRO; urgency=medium

  * Rebuild of Debian's ${UPSTREAM_VERSION}-${DEBIAN_REVISION} for $DISTRO, which has no
    webrtc-audio-processing 1.x of its own. pica-client needs it for AEC3.
EOF

	if [ "$RENAME_DEV" -eq 1 ]; then
		cat <<'EOF'
  * Keep the versioned libwebrtc-audio-processing-1-dev name that Debian used
    for 1.3-1, so that this coexists with the 0.3 that the distribution ships
    as libwebrtc-audio-processing-dev instead of replacing it
  * Drop the Breaks/Replaces recording Debian's rename of that package; with
    the old name back they say nothing, and on libwebrtc-audio-coding-dev the
    pair would match the distribution's own 0.3 -dev package
EOF
	fi

	cat <<EOF

 -- Pica Pica packaging <anton@picapica.im>  $(date -R)

EOF
	cat debian/changelog
} > debian/changelog.new
mv debian/changelog.new debian/changelog

note "the delta applied to Debian's packaging:"
sed -n '1,20p' debian/changelog | sed 's/^/    /'

# --- build ----------------------------------------------------------------

note "building"
if [ "$CROSS" -eq 1 ]; then
	dpkg-buildpackage --host-arch "$BUILD_ARCH" -b -us -uc
else
	dpkg-buildpackage -b -us -uc
fi

cd "$BUILDROOT"

mkdir -p "$OUTDIR"
found=0
for deb in *.deb; do
	[ -e "$deb" ] || continue
	cp -v "$deb" "$OUTDIR/"
	found=1
done
for changes in *.changes; do
	[ -e "$changes" ] || continue
	cp "$changes" "$OUTDIR/"
done

[ "$found" -eq 1 ] || die "the build produced no .deb files"

note "done - packages are in $OUTDIR"
