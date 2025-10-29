#include <vector>
#include <string>
#include <sstream>
#include <cstdio>
#include <unistd.h>

#include "common.hpp"
#include "ssh_link.hpp"
#include "ui.hpp"
#include "profile.hpp"
#include "topo.hpp"


static SSH_Link_Client ssh_link;
static Profile_Config  config;
static Topology        topo;

void handle_message(UI &ui, std::string &&message);

int main() {
    GLFWwindow *window = setup_GLFW_and_ImGui();
    UI         &ui     = UI::get(ssh_link, config, topo);

    ui.set_window(window);

    while (!ui.window_should_close()) {
        while (auto m = ssh_link.try_pull()) {
            handle_message(ui, std::move(*m));
        }
        ui.frame();
    }

    ssh_link.finish();

    return 0;
}

void handle_message(UI &ui, std::string &&message) {
    std::stringstream ss(message);
    std::string       s;

    if (!std::getline(ss, s, ';')) { return; }

    ui.log("server sends: " + s);

    if (s == "SERVER-CONNECT") {
        ui.set_connected(true);
        ui.log("The server has been connected.");
        ssh_link.send("REQUEST/TOPOLOGY");
        ssh_link.send("REQUEST/CONFIG");
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
