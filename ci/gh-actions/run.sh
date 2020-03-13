#!/bin/bash

export CI_ROOT_DIR="${GITHUB_WORKSPACE//\\//}/.."
export CI_SOURCE_DIR="${GITHUB_WORKSPACE//\\//}"
export CI_DEP_DIR="${CI_ROOT_DIR}/dependencies}"
export CI_BIN_DIR="${CI_ROOT_DIR}/build"

export PATH=${CI_DEP_DIR}/tools/bin:${PATH}
export CMAKE_PREFIX_PATH=${CI_DEP_DIR}/install

mkdir -p ${CI_BIN_DIR}
cd ${CI_BIN_DIR}

case "$1" in
  configure)
    cmake -GNinja -DCMAKE_INSTALL_PREFIX=${CI_ROOT_DIR}/install ${CI_SOURCE_DIR}
    ;;
  build)
    ninja
    ;;
  test)
    ctest
    ;;
  install)
    ninja install
    ;;
esac
