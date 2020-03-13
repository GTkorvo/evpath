#!/bin/bash

export CI_ROOT_DIR="${GITHUB_WORKSPACE//\\//}/.."
export CI_SOURCE_DIR="${GITHUB_WORKSPACE//\\//}"
export CI_DEP_DIR="${CI_ROOT_DIR}/dependencies}"

# Install ninja
mkdir -p ${CI_DEP_DIR}/tools/bin
cd ${CI_DEP_DIR}/tools/bin
case "$(uname -s)" in
  Linux)
    curl -O -L https://github.com/ninja-build/ninja/releases/download/v1.10.0/ninja-linux.zip
    unzip ninja-linux.zip
    rm ninja-linux.zip
    ;;
  Darwin)
    curl -O -L https://github.com/ninja-build/ninja/releases/download/v1.10.0/ninja-mac.zip
    unzip ninja-mac.zip
    rm ninja-mac.zip
    ;;
esac

# Install cmake
cd ${CI_DEP_DIR}/tools
case "$(uname -s)" in
  Linux)
    curl -L https://github.com/Kitware/CMake/releases/download/v3.2.3/cmake-3.2.3-Linux-x86_64.tar.gz | tar --strip-components=1 -xzv
    ;;
  Darwin)
    curl -L https://github.com/Kitware/CMake/releases/download/v3.2.3/cmake-3.2.3-Darwin-x86_64.tar.gz | tar --strip-components=3 -xzv
    ;;
esac

export PATH=${CI_DEP_DIR}/tools/bin:${PATH}
export CMAKE_PREFIX_PATH=${CI_DEP_DIR}/install

# Install atl
mkdir -p ${CI_DEP_DIR}/atl
cd ${CI_DEP_DIR}/atl
git clone https://github.com/GTKorvo/atl.git source
mkdir build
cd build
cmake -GNinja -DCMAKE_INSTALL_PREFIX=${CI_DEP_DIR}/install ../source
ninja install

# Install dill
mkdir -p ${CI_DEP_DIR}/dill
cd ${CI_DEP_DIR}/dill
git clone https://github.com/GTKorvo/dill.git source
mkdir build
cd build
cmake -GNinja -DCMAKE_INSTALL_PREFIX=${CI_DEP_DIR}/install ../source
ninja install

# Install ffs
mkdir -p ${CI_DEP_DIR}/ffs
cd ${CI_DEP_DIR}/ffs
git clone https://github.com/GTKorvo/ffs.git source
mkdir build
cd build
cmake -GNinja -DCMAKE_INSTALL_PREFIX=${CI_DEP_DIR}/install ../source
ninja install

# Install enet
mkdir -p ${CI_DEP_DIR}/enet
cd ${CI_DEP_DIR}/enet
git clone https://github.com/GTKorvo/enet.git source
mkdir build
cd build
cmake -GNinja -DCMAKE_INSTALL_PREFIX=${CI_DEP_DIR}/install ../source
ninja install
