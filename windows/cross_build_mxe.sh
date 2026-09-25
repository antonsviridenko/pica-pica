#!/bin/bash

# Build script that cross-builds Pica Pica Messenger
# for Windows on a GNU/Linux or Mac host using MXE build environment

# make sure that MXE dependencies are installed on a host system first
# see https://mxe.cc for details

function build_for_arch {

arch=$1

PREFIX=$PWD/usr/$arch

mkdir -p build_pica-pica
cd build_pica-pica
../../../configure --host=$arch --prefix=$PREFIX
make -j$(nproc)
make install
make clean
cd ..

#copy exe & DLLs
mkdir -p build_installer_$arch
cp $PREFIX/bin/pica-client.exe build_installer_$arch/
cp $PREFIX/bin/pica-node.exe build_installer_$arch/

#run copydlldeps.sh from destination dir, otherwise it recursively walks all MXE tree and copies excess DLLs

cd build_installer_$arch
../tools/copydlldeps.sh --infile ./pica-client.exe --destdir ./ --recursivesrcdir $PREFIX --copy --enforcedir $PREFIX/qt5/plugins/platforms/ --enforcedir $PREFIX/qt5/plugins/sqldrivers/ --enforcedir $PREFIX/qt5/plugins/imageformats/  --objdump ../usr/bin/$arch-objdump

cd build_installer_$arch
../tools/copydlldeps.sh --infile ./pica-node.exe --destdir ./ --recursivesrcdir $PREFIX --copy  --objdump ../usr/bin/$arch-objdump

cd ..

#strip
usr/bin/$arch-strip build_installer_$arch/*.dll build_installer_$arch/*.exe build_installer_$arch/sqldrivers/*.dll build_installer_$arch/platforms/*.dll build_installer_$arch/imageformats/*.dll

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
fi

cd ..
}


git clone https://github.com/mxe/mxe.git

cd mxe
git checkout 4efbb3567463a6d0a4d8ad7b822af0881bb1d9f8
patch -p1 < ../mxe_qtbase_strip_excess_deps.patch || exit
patch -p1 < ../mxe_ffmpeg_strip_excess_deps.patch || exit

# TODO: libva for windows, enable vaapi in ffmpeg configuration

make MXE_TARGETS='i686-w64-mingw32.shared x86_64-w64-mingw32.shared' cc qtbase ffmpeg openssl speexdsp miniupnpc libevent sqlite webrtc-audio-processing nsis

export PATH=$PWD/usr/bin/:$PATH


for arch in i686-w64-mingw32.shared x86_64-w64-mingw32.shared
do
	build_for_arch $arch
done
