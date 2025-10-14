#include <vector>
#include <string>
#include <sstream>
#include <cstdio>

#include "common.hpp"
#include "osclink.hpp"
#include "ui.hpp"
#include "profile.hpp"
#include "topo.hpp"
#include "serio/serio.h"

void handle_message(OSCLink_Client &link, UI &ui, std::string &&message);

int main() {
    if (!isatty(STDOUT_FILENO)) {
        printf("output must be run in a terminal\n");
        return 1;
    }

    GLFWwindow *window = setup_GLFW_and_ImGui();
    UI         &ui     = UI::get();

    ui.set_window(window);

    auto &link = OSCLink_Client::get();
    link.start();

    std::vector<int> heatmap;

    while (!ui.window_should_close()) {
        while (auto m = link.try_pull()) {
            handle_message(link, ui, std::move(*m));
        }
        ui.frame(link);
    }

    link.finish();

    return 0;
}

void handle_message(OSCLink_Client &link, UI &ui, std::string &&message) {
    std::stringstream ss(message);
    std::string       s;

    if (!std::getline(ss, s, ';')) { return; }

    ui.log("server sends: " + message, true);

    if (s == "SERVER-CONNECT") {
        ui.set_connected(true);
        ui.log("The server has been connected.", true);
    } else if (s == "TOPOLOGY") {
        if (std::getline(ss, s, ';')) {
            Topology topo;
            size_t consumed = Serio::deserialize(s, topo);
            ui.log("Deserialized " + std::to_string(consumed) + " bytes of a topo struct", true);
        }
    } else if (s == "HEATMAP-DATA") {
        std::vector<float> data;
        while (std::getline(ss, s, ';')) {
            data.push_back(std::stof(s));
        }
        ui.add_heatmap(std::move(data));
    } else {
        ui.log("bad server response: " + s, true);
    }
}
