#!/bin/bash
#
# Builds the pica-pica Debian packages for every distribution we support, each
# in its own podman container, and assembles the results into a signed apt
# repository.
#
# Takes a source tarball as produced by "make dist" - a self contained
# autotools distribution, which already carries the generated configure, so
# the containers have nothing to bootstrap.
#
# Meant to be run on an Ubuntu 24.04 host, but the only thing that is host
# specific is installing podman; give it podman by hand and it will run
# anywhere.
#
# The build matrix is at the top of the script. Note what is NOT in it:
# Ubuntu i386. Ubuntu has had no i386 architecture since 19.10 - what remains
# is a small multiarch compatibility subset with no installable base system,
# so there is no i386 Ubuntu container image to build in (docker.io/i386/ubuntu
# stops at 18.04) and no i386 Qt to build against. Debian still has i386 as a
# full architecture, so the 32 bit build happens there.
#
# The GPG key never enters a container. Containers produce .deb files; the
# repository is assembled and signed here on the host.
#
# (c) Copyright  2012 - 2026 Anton Sviridenko
# https://picapica.im
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, version 3.

set -euo pipefail

# codename | container image | dpkg arch | podman platform
TARGETS=(
	"noble|docker.io/library/ubuntu:24.04|amd64|linux/amd64"
	"resolute|docker.io/library/ubuntu:26.04|amd64|linux/amd64"
	"trixie|docker.io/library/debian:trixie|amd64|linux/amd64"
	"trixie|docker.io/i386/debian:trixie|i386|linux/386"
)

SCRIPTDIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
TARBALL=""
OUTDIR="$SCRIPTDIR/out"
REPODIR="$SCRIPTDIR/repo"
GPGKEY=""
SIGN=1
ONLY=""
SKIP_BUILD=0

ORIGIN="Pica Pica"
LABEL="Pica Pica"

progname=$(basename "$0")

usage()
{
	cat <<EOF
Usage: $progname [OPTIONS] TARBALL

Build the pica-pica packages in containers and assemble a signed apt
repository from the results.

TARBALL is a source tarball as produced by "make dist" in the pica-pica tree,
e.g. pica-pica-0.9.0dev.tar.gz. Not needed with --skip-build.

Options:
  -k, --gpg-key KEYID   Key to sign the repository's Release files with. Any
                        form gpg accepts - key id, fingerprint, or the email
                        address on the uid. Required unless --no-sign.
      --no-sign         Build the repository but leave it unsigned. apt will
                        refuse it without extra configuration; for testing.
  -o, --output DIR      Where the containers leave their .deb files.
                        Default: $OUTDIR
  -r, --repo DIR        Where to assemble the apt repository.
                        Default: $REPODIR
      --only PATTERN    Only build targets whose "codename/arch" matches this
                        shell pattern, e.g. --only 'noble/*' or --only '*/i386'.
      --skip-build      Do not build anything; just assemble and sign the
                        repository from whatever is already in the output
                        directory.
  -h, --help            This message.

Targets:
EOF

	local t cn img arch plat
	for t in "${TARGETS[@]}"; do
		IFS='|' read -r cn img arch plat <<<"$t"
		printf '  %-16s %-34s %s\n' "$cn/$arch" "$img" "$plat"
	done

	cat <<'EOF'

There is no Ubuntu i386 target and there cannot be: Ubuntu dropped i386 as an
architecture after 18.04, so there is no i386 Ubuntu base image and no i386 Qt
to build against. The 32 bit build is done on Debian, which still carries i386
as a full architecture.
EOF
}

die()
{
	echo "$progname: error: $*" >&2
	exit 1
}

note()
{
	echo
	echo "=== $*"
}

while [ $# -gt 0 ]; do
	case "$1" in
	-k|--gpg-key)  GPGKEY="${2:?--gpg-key needs a value}"; shift 2 ;;
	--no-sign)     SIGN=0; shift ;;
	-o|--output)   OUTDIR="${2:?--output needs a value}"; shift 2 ;;
	-r|--repo)     REPODIR="${2:?--repo needs a value}"; shift 2 ;;
	--only)        ONLY="${2:?--only needs a value}"; shift 2 ;;
	--skip-build)  SKIP_BUILD=1; shift ;;
	-h|--help)     usage; exit 0 ;;
	-*)            die "unknown option $1 (try --help)" ;;
	*)             break ;;
	esac
done

if [ $# -gt 1 ]; then
	die "expected at most one tarball, got: $*"
elif [ $# -eq 1 ]; then
	TARBALL=$(readlink -f "$1")
fi

if [ "$SIGN" -eq 1 ] && [ -z "$GPGKEY" ]; then
	die "no signing key given. Pass --gpg-key KEYID, or --no-sign to skip signing"
fi

if [ "$SKIP_BUILD" -eq 0 ]; then
	[ -n "$TARBALL" ] ||
		die "no source tarball given. Run \"make dist\" in the pica-pica tree and pass the result (try --help)"
	[ -f "$TARBALL" ] ||
		die "$TARBALL does not exist"
	tar -tf "$TARBALL" >/dev/null 2>&1 ||
		die "$TARBALL is not a readable tar archive"
fi

if [ "$(id -u)" -eq 0 ]; then
	SUDO=""
elif command -v sudo >/dev/null; then
	SUDO="sudo"
else
	# Not fatal on its own. Nothing may need installing, which is the normal
	# case on a signing-only machine; ensure_tool() complains if something
	# actually is missing.
	SUDO=""
fi

# Installs a package, but only if the tool it provides is not already there -
# and says plainly why it cannot when it has no way to become root, rather
# than letting apt-get fail with something less obvious.
ensure_tool()
{
	local tool="$1" package="$2"

	command -v "$tool" >/dev/null && return 0

	if [ "$(id -u)" -ne 0 ] && [ -z "$SUDO" ]; then
		die "$tool is missing and would come from the $package package, but this is not root and there is no sudo"
	fi

	note "installing $package, for $tool"
	$SUDO apt-get install -y "$package"
}

# --- tools ----------------------------------------------------------------

# podman only when there is something to build. --skip-build exists so that
# the repository can be assembled and signed on a machine that holds the key
# and does nothing else; installing a container runtime on that machine would
# be exactly backwards.
if [ "$SKIP_BUILD" -eq 0 ]; then
	if command -v podman >/dev/null; then
		note "podman is already installed: $(podman --version)"
	else
		if [ "$(id -u)" -ne 0 ] && [ -z "$SUDO" ]; then
			die "podman is missing and this is not root and there is no sudo - install podman yourself and re-run"
		fi
		note "installing podman"
		$SUDO apt-get update
		$SUDO apt-get install -y podman
	fi
fi

# apt-ftparchive builds the repository indices, so it is needed in both
# modes. gpg only matters when there is something to sign.
ensure_tool apt-ftparchive apt-utils

if [ "$SIGN" -eq 1 ]; then
	ensure_tool gpg gnupg
fi

if [ "$SIGN" -eq 1 ]; then
	gpg --list-keys "$GPGKEY" >/dev/null 2>&1 ||
		die "gpg does not know a key called '$GPGKEY'"
	gpg --list-secret-keys "$GPGKEY" >/dev/null 2>&1 ||
		die "no SECRET key for '$GPGKEY' - the repository cannot be signed with it"
fi

# --- work area ------------------------------------------------------------

WORKDIR=$(mktemp -d /tmp/pica-packaging.XXXXXX)
trap 'rm -rf "$WORKDIR"' EXIT

if [ "$SKIP_BUILD" -eq 0 ]; then
	note "source tarball"
	echo "    $TARBALL"
	echo "    $(du -h "$TARBALL" | cut -f1), top level: $(tar -tf "$TARBALL" | head -1)"
fi

# --- build ----------------------------------------------------------------

mkdir -p "$OUTDIR"

BUILT=()
FAILED=()

if [ "$SKIP_BUILD" -eq 0 ]; then
	for target in "${TARGETS[@]}"; do
		IFS='|' read -r codename image arch platform <<<"$target"

		if [ -n "$ONLY" ]; then
			# shellcheck disable=SC2254
			case "$codename/$arch" in
			$ONLY) ;;
			*) echo "    skipping $codename/$arch (does not match --only '$ONLY')"; continue ;;
			esac
		fi

		note "$codename/$arch  ($image)"

		if ! podman pull --platform "$platform" "$image"; then
			FAILED+=("$codename/$arch: could not pull $image")
			continue
		fi

		# --userns=keep-id is deliberately not used: the build wants to be
		# root inside the container to install packages, and in rootless
		# podman that still maps back to this user on the way out.
		# The tarball is bind mounted as a single file under a fixed name, so
		# container-build.sh does not have to be told what it is called. tar
		# works out the compression from the content, so the missing .gz or
		# .xz on that name costs nothing.
		if podman run --rm \
			--platform "$platform" \
			-v "$TARBALL:/src/pica-source.tar:ro" \
			-v "$SCRIPTDIR:/scripts:ro" \
			-v "$OUTDIR:/out" \
			-e PICA_DISTRO="$codename" \
			-e PICA_ARCH="$arch" \
			-w / \
			"$image" \
			/bin/bash /scripts/container-build.sh
		then
			BUILT+=("$codename/$arch")
		else
			FAILED+=("$codename/$arch: build failed")
		fi
	done

	note "build summary"
	for b in "${BUILT[@]:-}";  do [ -n "$b" ] && echo "    ok      $b"; done
	for f in "${FAILED[@]:-}"; do [ -n "$f" ] && echo "    FAILED  $f"; done

	[ ${#BUILT[@]} -gt 0 ] || die "nothing built - not assembling a repository"
fi

# --- repository -----------------------------------------------------------
#
# Laid out with a pool per codename rather than one shared pool. The indices
# are per suite, and a shared pool would let noble's Packages list trixie's
# binaries - apt would happily offer them and they would not install.

note "assembling the repository in $REPODIR"

rm -rf "$REPODIR"
mkdir -p "$REPODIR"

CODENAMES=()

for distdir in "$OUTDIR"/*/; do
	[ -d "$distdir" ] || continue
	codename=$(basename "$distdir")

	arches=()

	for archdir in "$distdir"*/; do
		[ -d "$archdir" ] || continue
		arch=$(basename "$archdir")

		compgen -G "$archdir*.deb" >/dev/null || continue
		arches+=("$arch")

		for deb in "$archdir"*.deb; do
			# Pool path by source package, the way Debian lays it out, so
			# that the binaries of one source sit together.
			src=$(dpkg-deb -f "$deb" Source 2>/dev/null | awk '{print $1}')
			[ -n "$src" ] || src=$(dpkg-deb -f "$deb" Package)
			case "$src" in
			lib*) letter="${src:0:4}" ;;
			*)    letter="${src:0:1}" ;;
			esac

			pool="$REPODIR/pool/$codename/main/$letter/$src"
			mkdir -p "$pool"
			cp "$deb" "$pool/"
		done
	done

	[ ${#arches[@]} -gt 0 ] || continue
	CODENAMES+=("$codename")

	# Deduplicate: a codename built for two architectures appears twice above.
	mapfile -t arches < <(printf '%s\n' "${arches[@]}" | sort -u)

	echo "    $codename: ${arches[*]}"

	for arch in "${arches[@]}"; do
		bindir="$REPODIR/dists/$codename/main/binary-$arch"
		mkdir -p "$bindir"

		# Run from the repository root so that the Filename fields come out
		# relative to it, which is what apt expects.
		( cd "$REPODIR" &&
		  apt-ftparchive --arch "$arch" packages "pool/$codename" \
			> "dists/$codename/main/binary-$arch/Packages" )

		gzip -9 -k -f "$bindir/Packages"

		cat > "$bindir/Release" <<EOF
Archive: $codename
Suite: $codename
Component: main
Origin: $ORIGIN
Label: $LABEL
Architecture: $arch
EOF
	done

	# The suite Release, carrying the checksums of everything above. Written
	# to a temporary file first - apt-ftparchive walks the directory it is
	# indexing, and would otherwise hash a half-written Release.
	( cd "$REPODIR" &&
	  apt-ftparchive \
		-o APT::FTPArchive::Release::Origin="$ORIGIN" \
		-o APT::FTPArchive::Release::Label="$LABEL" \
		-o APT::FTPArchive::Release::Suite="$codename" \
		-o APT::FTPArchive::Release::Codename="$codename" \
		-o APT::FTPArchive::Release::Components="main" \
		-o APT::FTPArchive::Release::Architectures="${arches[*]}" \
		-o APT::FTPArchive::Release::Description="Pica Pica Messenger packages" \
		release "dists/$codename" > "$WORKDIR/Release.$codename" )

	mv "$WORKDIR/Release.$codename" "$REPODIR/dists/$codename/Release"

	if [ "$SIGN" -eq 1 ]; then
		echo "    signing $codename with $GPGKEY"
		rm -f "$REPODIR/dists/$codename/Release.gpg" \
		      "$REPODIR/dists/$codename/InRelease"

		# Both forms: InRelease is what modern apt fetches, Release.gpg is
		# the detached signature older clients look for.
		gpg --batch --yes --default-key "$GPGKEY" \
			--armor --detach-sign \
			-o "$REPODIR/dists/$codename/Release.gpg" \
			"$REPODIR/dists/$codename/Release"

		gpg --batch --yes --default-key "$GPGKEY" \
			--clearsign \
			-o "$REPODIR/dists/$codename/InRelease" \
			"$REPODIR/dists/$codename/Release"
	fi
done

[ ${#CODENAMES[@]} -gt 0 ] || die "no packages found under $OUTDIR"

if [ "$SIGN" -eq 1 ]; then
	gpg --armor --export "$GPGKEY" > "$REPODIR/pica-pica-archive-keyring.asc"
fi

# --- how to use it --------------------------------------------------------

note "done"
cat <<EOF

Repository: $REPODIR
Suites:     ${CODENAMES[*]}

Serve that directory over HTTP, then on a client:

  sudo install -d /usr/share/keyrings
  sudo curl -fsSL https://YOUR.HOST/pica-pica-archive-keyring.asc \\
      -o /usr/share/keyrings/pica-pica-archive-keyring.asc

  echo "deb [signed-by=/usr/share/keyrings/pica-pica-archive-keyring.asc] \\
https://YOUR.HOST \$(lsb_release -cs) main" \\
      | sudo tee /etc/apt/sources.list.d/pica-pica.list

  sudo apt update && sudo apt install pica-client
EOF

if [ "$SIGN" -eq 0 ]; then
	cat <<'EOF'

The repository is NOT signed. apt will reject it unless clients add
[trusted=yes] to the sources.list entry, which disables authentication -
do that for local testing only.
EOF
fi
