#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd ${DIR}

if ! [ -d hwloc ]; then
    git clone https://github.com/open-mpi/hwloc || exit $?
    cd hwloc
    ./autogen.sh || exit $?
    ./configure --disable-io --disable-libxml2 --enable-shared=no --enable-static=yes || exit $?
    make -j$(nproc) || exit $?
fi

HWLOC_INCLUDE="${DIR}/hwloc/include"
HWLOC_LIB="${DIR}/hwloc/hwloc/.libs/libhwloc.a"

cd ${DIR}/..

SRC=""
SRC+=" server/server.cpp"

if [ "${DEBUG}" == "yes" ]; then
    OPT="-g -O0"
else
    if [ "$(uname)" == "Darwin" ] && uname -a | grep "arm64" >/dev/null 2>&1; then
        OPT="-O3 -mcpu=native"
    else
        OPT="-O3 -march=native -mtune=native"
    fi
fi

CPP_FLAGS="--std=c++20 -Wall -Werror ${OPT} -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -Ishared -Iserver -I${HWLOC_INCLUDE}"

if [ $(uname) = "Darwin" ]; then
    CPP_FLAGS+=" -I/opt/homebrew/include"
fi

LD_FLAGS=" ${HWLOC_LIB}"
if [ $(uname) = "Darwin" ]; then
    LD_FLAGS+=" -framework Foundation -framework IOKit"
else
    LD_FLAGS+=" -ludev"
fi

g++ -o build/server ${SRC} ${CPP_FLAGS} ${LD_FLAGS} || exit $?
