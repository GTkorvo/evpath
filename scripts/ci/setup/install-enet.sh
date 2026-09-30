#!/bin/bash

echo
echo
echo "****************************************"
echo "  Installing ENet"
echo "****************************************"

# Determine architecture flag for Windows builds
arch_flag=""
if [[ "${GH_YML_JOBNAME}" == *"win32"* ]]; then
  arch_flag="-A Win32"
elif [[ "${GH_YML_JOBNAME}" == *"windows"* ]]; then
  arch_flag="-A x64"
fi
# Only the static jobs use ENet, to build EVPath's enet transport as ADIOS2 does
if [[ "${GH_YML_JOBNAME}" != *"static"* ]]; then
  exit 0
fi
shared_flag="-DBUILD_SHARED_LIBS=OFF"

mkdir enet
cd enet
git clone https://github.com/GTKorvo/enet.git source
mkdir build
cd build
cmake ${arch_flag} ${shared_flag} \
  -DCMAKE_BUILD_TYPE=$1 \
  -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX=${PWD}/../install \
  ../source
cmake --build . -j4 --config $1
cmake --install . --config $1
