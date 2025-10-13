#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd ${DIR}

if ! [ -d hwloc ]; then
    git clone https://github.com/open-mpi/hwloc || exit $?
    cd hwloc
    ./autogen.sh || exit $?
    ./configure --disable-libxml2 --enable-shared=no --enable-static=yes || exit $?
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

CPP_FLAGS="--std=c++17 -Wall -Werror ${OPT} -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -Iimgui -Iimgui/backends -I/opt/homebrew/include"

LD_FLAGS="-L/opt/homebrew/lib -lglfw"
if [ $(uname) = "Darwin" ]; then
    LD_FLAGS+=" -framework OpenGL"
else
    LD_FLAGS+=" -lGL"
fi

g++ -o osclink_client ${SRC} ${CPP_FLAGS} ${LD_FLAGS} || exit $?
g++ -o osclink_server -I${HWLOC_INCLUDE} osclink_server.cpp ${CPP_FLAGS} ${HWLOC_LIB} -lpciaccess -ludev || exit $?
