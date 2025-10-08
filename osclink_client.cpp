#include <vector>
#include <string>
#include <sstream>
#include <cstdio>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <signal.h>
#include <sys/types.h>
#include <poll.h>
#include <fcntl.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#include <utmp.h>
#endif
#include <pwd.h>

#include "osclink.hpp"
#include "ui.hpp"

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

    if (!std::getline(ss, s)) { return; }

    if (s == "SERVER-CONNECT") {
        ui.set_connected(true);
        ui.log("The server has been connected.", true);
    } else if (s == "HEATMAP-DATA") {
        std::vector<int> data;
        while (std::getline(ss, s)) {
            data.push_back(std::stoi(s));
        }
        ui.add_heatmap(std::move(data));
    }
}
