#!/bin/bash

yes | pkg upgrade -y # Upgrading Packages

pkg install -y git wget make python getconf zip apksigner clang binutils libglvnd-dev aapt which patchelf curl openxr # Installing Dependencies

pkg reinstall -y libglvnd

rm $PREFIX/lib/libGLESv2.so
rm $PREFIX/lib/libGLESv2.so.2

mv $PREFIX/lib/libGLESv2.so.2.1.0 $PREFIX/lib/libGLESv2.so
patchelf --set-soname libGLESv2.so $PREFIX/lib/libGLESv2.so
patchelf --set-soname libcurl.so $PREFIX/lib/libcurl.so


mkdir ${HOME}/include

wget https://github.com/KhronosGroup/OpenXR-SDK/archive/refs/tags/release-1.1.53.tar.gz

tar xr release-1.1.53.tar.gz
mv OpenXR-SDK-release-1.1.53/include/* ${HOME}/include

make OPENXR=1