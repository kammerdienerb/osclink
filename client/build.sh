#!/usr/bin/env bash

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"

cd ${DIR}/..

SRC=""
SRC+=" client/client.cpp"
SRC+=" client/imgui/imgui.cpp"
SRC+=" client/imgui/imgui_draw.cpp"
SRC+=" client/imgui/imgui_tables.cpp"
SRC+=" client/imgui/imgui_widgets.cpp"
SRC+=" client/imgui/backends/imgui_impl_glfw.cpp"
SRC+=" client/imgui/backends/imgui_impl_opengl3.cpp"


if [ "${DEBUG}" == "yes" ]; then
    OPT="-g -O0"
else
    if [ "$(uname)" == "Darwin" ] && uname -a | grep "arm64" >/dev/null 2>&1; then
        OPT="-O3 -mcpu=native"
    else
        OPT="-O3 -march=native -mtune=native"
    fi
fi

CPP_FLAGS="--std=c++17 -Wall -Werror ${OPT} -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer -Ishared -Iclient -Iclient/imgui -Iclient/imgui/backends"

if [ $(uname) = "Darwin" ]; then
    CPP_FLAGS+=" -I/opt/homebrew/include"
fi

LD_FLAGS=""
if [ $(uname) = "Darwin" ]; then
    LD_FLAGS+=" -L/opt/homebrew/lib -framework OpenGL"
else
    LD_FLAGS+=" -lGL"
fi
LD_FLAGS+=" -lglfw"

g++ -o build/client ${SRC} ${CPP_FLAGS} ${LD_FLAGS} || exit $?
