#!/bin/bash

# Build script that cross-builds Pica Pica Messenger
# for Windows on a GNU/Linux or Mac host using MXE build environment

# make sure that MXE dependencies are installed on a host system first
# see https://mxe.cc for details

# The MXE tree (clone, patches, libraries) is set up only once and reused on
# every subsequent run, because building the MXE libraries takes hours.
# Pica Pica itself and the installers are always rebuilt from scratch.

# Environment variables understood by this script:
#   MXE_COMMIT                  build against another MXE commit than the pinned one
#   MXE_ALLOW_COMMIT_MISMATCH=1 only warn instead of failing when the existing MXE
#                               clone is checked out at a different commit
#   MXE_FORCE_REAPPLY_PATCHES=1 restore the MXE files touched by our patches from
#                               git and apply the patches again, in case a patch was
#                               changed after it had been applied

set -o errexit
set -o errtrace
set -o nounset
set -o pipefail

MXE_REPO_URL='https://github.com/mxe/mxe.git'
MXE_COMMIT=${MXE_COMMIT:-4efbb3567463a6d0a4d8ad7b822af0881bb1d9f8}
MXE_TARGETS='i686-w64-mingw32.shared x86_64-w64-mingw32.shared'
MXE_PACKAGES='cc qtbase ffmpeg openssl miniupnpc libevent sqlite webrtc-audio-processing nsis'
MXE_PATCHES='mxe_qtbase_strip_excess_deps.patch mxe_ffmpeg_strip_excess_deps.patch'

windows_dir=$(cd "$(dirname "$0")" && pwd)
mxe_dir=$windows_dir/mxe

function die {
	echo "ERROR: $*" >&2
	exit 1
}

function on_error {
	echo "ERROR: $0 failed at line $1 (exit code $2), build aborted" >&2
}

trap 'on_error $LINENO $?' ERR

# step 1: clone the MXE repository unless it is already there
function clone_mxe {
	if test -d "$mxe_dir/.git"
	then
		echo "*** MXE is already cloned in $mxe_dir, skipping clone"
		return
	fi

	test -e "$mxe_dir" && die "$mxe_dir exists but is not a git repository, remove it and run this script again"

	echo "*** cloning MXE from $MXE_REPO_URL"
	git clone "$MXE_REPO_URL" "$mxe_dir"
}

# step 2: make sure the MXE clone is checked out at the commit we build against
function checkout_mxe_commit {
	local head

	head=$(git -C "$mxe_dir" rev-parse HEAD)

	if test "$head" = "$MXE_COMMIT"
	then
		echo "*** MXE is already checked out at $MXE_COMMIT, skipping checkout"
		return
	fi

	if test "${MXE_ALLOW_COMMIT_MISMATCH:-0}" = '1'
	then
		echo "*** WARNING: MXE is checked out at $head instead of $MXE_COMMIT, continuing anyway" >&2
		return
	fi

	if ! git -C "$mxe_dir" cat-file -e "$MXE_COMMIT^{commit}" 2>/dev/null
	then
		echo "*** commit $MXE_COMMIT is not present in the local MXE clone, fetching"
		git -C "$mxe_dir" fetch origin "$MXE_COMMIT" || git -C "$mxe_dir" fetch origin
		git -C "$mxe_dir" cat-file -e "$MXE_COMMIT^{commit}" 2>/dev/null \
			|| die "commit $MXE_COMMIT does not exist in $MXE_REPO_URL, fix MXE_COMMIT in $0"
	fi

	if test -n "$(git -C "$mxe_dir" status --porcelain --untracked-files=no)"
	then
		die "$mxe_dir is checked out at $head instead of $MXE_COMMIT and has local modifications.
Checking out $MXE_COMMIT would discard them, so it is not done automatically. Either
  - check out $MXE_COMMIT in $mxe_dir manually, or
  - set MXE_COMMIT to the commit you want to build against, or
  - run with MXE_ALLOW_COMMIT_MISMATCH=1 to keep the current checkout, or
  - remove $mxe_dir to get a fresh clone (this rebuilds all MXE libraries)"
	fi

	echo "*** checking out MXE commit $MXE_COMMIT"
	git -C "$mxe_dir" checkout --detach "$MXE_COMMIT"
}

# files a patch touches, as paths relative to the MXE tree
function patch_target_files {
	awk '/^\+\+\+ /{sub(/^\+\+\+ /, ""); sub(/\t.*$/, ""); sub(/^b\//, ""); print}' "$1"
}

# step 3: apply our MXE patches, but only those that are not applied yet
function apply_mxe_patches {
	local patch_name patch_file

	for patch_name in $MXE_PATCHES
	do
		patch_file=$windows_dir/$patch_name

		test -f "$patch_file" || die "patch $patch_file not found"

		if patch -p1 -d "$mxe_dir" --dry-run --reverse --silent < "$patch_file" > /dev/null 2>&1
		then
			echo "*** $patch_name is already applied, skipping"
			continue
		fi

		if patch -p1 -d "$mxe_dir" --dry-run --forward --silent < "$patch_file" > /dev/null 2>&1
		then
			echo "*** applying $patch_name"
			patch -p1 -d "$mxe_dir" --forward < "$patch_file"
			continue
		fi

		if test "${MXE_FORCE_REAPPLY_PATCHES:-0}" = '1'
		then
			echo "*** restoring $(patch_target_files "$patch_file" | tr '\n' ' ')from git and applying $patch_name again"
			# shellcheck disable=SC2046
			git -C "$mxe_dir" checkout -- $(patch_target_files "$patch_file")
			patch -p1 -d "$mxe_dir" --forward < "$patch_file"
			continue
		fi

		die "$patch_name neither applies to nor is already applied in $mxe_dir.
This usually means the patch was changed after it had been applied. Run
  patch -p1 -d $mxe_dir --dry-run --forward < $patch_file
to see the details, then either resolve it by hand or run this script with
MXE_FORCE_REAPPLY_PATCHES=1 to restore the patched MXE files from git and
apply the patch again (the affected libraries will be rebuilt by MXE)."
	done
}

# MXE packages from $MXE_PACKAGES that are not built yet, as 'package (target)'
function missing_mxe_packages {
	local target package

	for target in $MXE_TARGETS
	do
		for package in $MXE_PACKAGES
		do
			test -f "$mxe_dir/usr/$target/installed/$package" || echo "$package ($target)"
		done
	done
}

# step 4: build the libraries provided by MXE, unless they are all built already
function build_mxe_packages {
	local missing

	make -C "$mxe_dir" MXE_TARGETS="$MXE_TARGETS" check-requirements

	missing=$(missing_mxe_packages)

	if test -z "$missing"
	then
		echo "*** all MXE packages are already built, skipping MXE build"
		return
	fi

	echo "*** MXE packages that are not built yet:"
	echo "$missing" | sed 's,^,      ,'

	# TODO: libva for windows, enable vaapi in ffmpeg configuration
	# make itself does nothing for the packages that are built already
	make -C "$mxe_dir" MXE_TARGETS="$MXE_TARGETS" $MXE_PACKAGES

	missing=$(missing_mxe_packages)
	test -z "$missing" || die "MXE build finished but these packages are still missing:
$(echo "$missing" | sed 's,^,      ,')"
}

# step 5: build Pica Pica itself and the installer, always from scratch
function build_for_arch {

arch=$1

PREFIX=$PWD/usr/$arch

test -x ../../configure || die "../../configure not found, run ./autogen.sh in the Pica Pica source tree first"

rm -rf build_pica-pica
mkdir -p build_pica-pica
cd build_pica-pica
../../../configure --host=$arch --prefix=$PREFIX
make -j$(nproc)
make install
make clean
cd ..

#copy exe & DLLs
rm -rf build_installer_$arch
mkdir -p build_installer_$arch
cp $PREFIX/bin/pica-client.exe build_installer_$arch/
cp $PREFIX/bin/pica-node.exe build_installer_$arch/

#run copydlldeps.sh from destination dir, otherwise it recursively walks all MXE tree and copies excess DLLs

cd build_installer_$arch
../tools/copydlldeps.sh --infile ./pica-client.exe --destdir ./ --recursivesrcdir $PREFIX --copy --enforcedir $PREFIX/qt5/plugins/platforms/ --enforcedir $PREFIX/qt5/plugins/sqldrivers/ --enforcedir $PREFIX/qt5/plugins/imageformats/  --objdump ../usr/bin/$arch-objdump

../tools/copydlldeps.sh --infile ./pica-node.exe --destdir ./ --recursivesrcdir $PREFIX --copy  --objdump ../usr/bin/$arch-objdump

cd ..

#strip everything that was copied, including the Qt plugin subdirectories.
#find is used instead of globs because a glob for a plugin directory that
#copydlldeps.sh did not create would abort the whole script now that it runs
#with errexit - makensis reports a missing plugin later anyway.
find build_installer_$arch -type f \( -name '*.dll' -o -name '*.exe' \) -print0 \
	| xargs -0 -r usr/bin/$arch-strip

#build installer
cd build_installer_$arch

for dll in *.dll
do
	echo "File $dll" >> dll_list.nsh
done

cp ../../../COPYING ./
cp ../../../README ./
cp ../../../windows/picapica.ico ./

mkdir -p share

cp $PREFIX/share/pica-client/dhparam4096.pem share/
cp $PREFIX/share/pica-client/picapica-icon-fly.png share/
cp $PREFIX/share/pica-client/picapica-icon-sit.png share/
cp $PREFIX/share/pica-client/picapica-snd-newmessage.wav share/

if test $arch = 'i686-w64-mingw32.shared'
then
	cp ../build_pica-pica/win/installer32.nsi ./
	makensis installer32.nsi
elif test $arch = 'x86_64-w64-mingw32.shared'
then
	cp ../build_pica-pica/win/installer64.nsi ./
	makensis installer64.nsi
else
	die "unsupported architecture $arch"
fi

cd ..
}


clone_mxe
checkout_mxe_commit
apply_mxe_patches
build_mxe_packages

cd "$mxe_dir"

export PATH=$PWD/usr/bin/:$PATH

command -v makensis > /dev/null || die "makensis not found in PATH, the MXE nsis package is not built"

for arch in $MXE_TARGETS
do
	build_for_arch $arch
done

echo "*** done, installers are in:"
for arch in $MXE_TARGETS
do
	echo "      $mxe_dir/build_installer_$arch"
done
