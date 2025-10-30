#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd ${DIR}

if ! [ -d libssh ]; then
    git clone https://github.com/canonical/libssh || exit $?
    cd libssh
    rm -rf build
    mkdir build
    rm -rf prefix
    mkdir prefix
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=$(realpath ../prefix) -DBUILD_SHARED_LIBS=OFF -DWITH_GSSAPI=OFF || exit $?
    make -j $(nproc) || exit $?
    make install -j $(nproc) || exit $?
fi

LIBSSH_INCLUDE="${DIR}/libssh/prefix/include"

if [ -d "${DIR}/libssh/prefix/lib" ]; then
    LIBSSH_LIB="${DIR}/libssh/prefix/lib/libssh.a"
elif [ -d "${DIR}/libssh/prefix/lib64" ]; then
    LIBSSH_LIB="${DIR}/libssh/prefix/lib64/libssh.a"
fi

cd ${DIR}/..

SRC=""
SRC+=" client/client.cpp"
SRC+=" client/imgui/imgui.cpp"
SRC+=" client/imgui/imgui_draw.cpp"
SRC+=" client/imgui/imgui_tables.cpp"
SRC+=" client/imgui/imgui_widgets.cpp"
SRC+=" client/imgui/backends/imgui_impl_glfw.cpp"
SRC+=" client/imgui/backends/imgui_impl_opengl3.cpp"
SRC+=" client/imgui/misc/cpp/imgui_stdlib.cpp"


if [ "${DEBUG}" == "yes" ]; then
    OPT="-g -O0"
else
    if [ "$(uname)" == "Darwin" ] && uname -a | grep "arm64" >/dev/null 2>&1; then
        OPT="-O3 -mcpu=native"
    else
        OPT="-O3 -march=native -mtune=native"
    fi
fi

CPP_FLAGS="--std=c++20 -Wall -Werror ${OPT} -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -Ishared -Iclient -Iclient/imgui -Iclient/imgui/backends -Iclient/imgui/misc/cpp -I${LIBSSH_INCLUDE}"

if [ $(uname) = "Darwin" ]; then
    CPP_FLAGS+=" -I/opt/homebrew/include"
fi

LD_FLAGS=" ${LIBSSH_LIB} -lssl -lcrypto -lz"
if [ $(uname) = "Darwin" ]; then
    LD_FLAGS+=" -L/opt/homebrew/lib -framework OpenGL"
else
    LD_FLAGS+=" -lGL"
fi
LD_FLAGS+=" -lglfw"

mkdir -p build/obj/client

pids=()
for f in ${SRC}; do
    g++ -c -o build/obj/client/$(basename ${f}).o ${f} ${CPP_FLAGS} &
    pids+=($!)
done

for pid in "${pids[@]}"; do
    wait ${pid} || exit $?
done


g++ -o build/client build/obj/client/*.o ${CPP_FLAGS} ${LD_FLAGS} || exit $?
