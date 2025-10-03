#!/usr/bin/env bash

SRC=""
SRC+=" osclink_client.cpp"
SRC+=" imgui/imgui.cpp"
SRC+=" imgui/imgui_draw.cpp"
SRC+=" imgui/imgui_tables.cpp"
SRC+=" imgui/imgui_widgets.cpp"
SRC+=" imgui/backends/imgui_impl_glfw.cpp"
SRC+=" imgui/backends/imgui_impl_opengl3.cpp"

CPP_FLAGS="--std=c++17 -fno-rtti -Wall -Werror -g -O0 -Iimgui -Iimgui/backends -I/opt/homebrew/include"
LD_FLAGS="-L/opt/homebrew/lib -lglfw -framework OpenGL"

g++ -o osclink_client ${SRC} ${CPP_FLAGS} ${LD_FLAGS} || exit $?
