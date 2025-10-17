#include <vector>
#include <string>
#include <sstream>
#include <cstdio>
#include <unistd.h>

#include "common.hpp"
#include "osclink.hpp"
#include "ui.hpp"
#include "profile.hpp"
#include "topo.hpp"


static Profile_Config config;
static Topology       topo;

void handle_message(OSCLink_Client &link, UI &ui, std::string &&message);

int main() {
    if (!isatty(STDOUT_FILENO)) {
        printf("output must be run in a terminal\n");
        return 1;
    }

    auto &link = OSCLink_Client::get();

    GLFWwindow *window = setup_GLFW_and_ImGui();
    UI         &ui     = UI::get(link, config, topo);

    ui.set_window(window);

    link.start();

    while (!ui.window_should_close()) {
        while (auto m = link.try_pull()) {
            handle_message(link, ui, std::move(*m));
        }
        ui.frame();
    }

    link.finish();

    return 0;
}

void handle_message(OSCLink_Client &link, UI &ui, std::string &&message) {
    std::stringstream ss(message);
    std::string       s;

    if (!std::getline(ss, s, ';')) { return; }

    ui.log("server sends: " + s);

    if (s == "SERVER-CONNECT") {
        ui.set_connected(true);
        ui.log("The server has been connected.");
        link.send("REQUEST/TOPOLOGY");
        link.send("REQUEST/CONFIG");
    } else if (s == "SERVER-WARNING") {
        if (std::getline(ss, s, ';')) {
            s = "SERVER WARNING: " + s;
            ui.log(std::move(s), true);
        }
    } else if (s == "CONFIG") {
        if (std::getline(ss, s, ';')) {
            config = Profile_Config::from_serialized(s);
        }
    } else if (s == "TOPOLOGY") {
        if (std::getline(ss, s, ';')) {
            topo = Topology::from_serialized(s);
            ui.focus_tab("Dashboard");
        }
    } else if (s == "HEATMAP-DATA") {
        std::vector<float> data;
        while (std::getline(ss, s, ';')) {
            data.push_back(std::stof(s));
        }
        ui.set_heatmap(std::move(data));
        ui.focus_tab("Profile");
    } else {
        ui.log("bad server response: " + s, true);
    }
}
