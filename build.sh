#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd ${DIR}

if ! [ -d hwloc ]; then
    git clone https://github.com/open-mpi/hwloc || exit $?
    cd hwloc
    ./autogen.sh || exit $?
    ./configure --disable-io --disable-libxml2 --enable-shared=no --enable-static=yes || exit $?
    make -j4 || exit $?
    cd ${DIR}
fi

HWLOC_INCLUDE="${DIR}/hwloc/include"
HWLOC_LIB="${DIR}/hwloc/hwloc/.libs/libhwloc.a"

SRC=""
SRC+=" osclink_client.cpp"
SRC+=" imgui/imgui.cpp"
SRC+=" imgui/imgui_draw.cpp"
SRC+=" imgui/imgui_tables.cpp"
SRC+=" imgui/imgui_widgets.cpp"
SRC+=" imgui/backends/imgui_impl_glfw.cpp"
SRC+=" imgui/backends/imgui_impl_opengl3.cpp"

OPT="-g -O0"

# if [ "$(uname)" == "Darwin" ] && uname -a | grep "arm64" >/dev/null 2>&1; then
#     OPT="-O3 -mcpu=native"
# else
#     OPT="-O3 -march=native -mtune=native"
# fi

CPP_FLAGS="--std=c++17 -Wall -Werror ${OPT} -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -I. -Iimgui -Iimgui/backends -I/opt/homebrew/include"

CLIENT_LD_FLAGS=""
SERVER_LD_FLAGS=""
if [ $(uname) = "Darwin" ]; then
    CLIENT_LD_FLAGS+=" -L/opt/homebrew/lib -framework OpenGL"
    SERVER_LD_FLAGS+=" -framework Foundation -framework IOKit ${HWLOC_LIB}"
else
    CLIENT_LD_FLAGS+=" -lGL"
fi
CLIENT_LD_FLAGS+=" -lglfw"

g++ -o osclink_client ${SRC} ${CPP_FLAGS} ${CLIENT_LD_FLAGS} || exit $?
g++ -o osclink_server -I${HWLOC_INCLUDE} osclink_server.cpp ${CPP_FLAGS} ${SERVER_LD_FLAGS} || exit $?
