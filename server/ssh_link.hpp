#pragma once

#include <string>
#include <optional>
#include <unistd.h>
#include <errno.h>

#include "ssh_link_inbox.hpp"
#include "base64.hpp"

namespace {

struct SSH_Link_Server {
private:
    SSH_Link_Inbox inbox;

    static constexpr const char *OSC_PATTERN = "\033]9999;";

public:
    static SSH_Link_Server& get() {
        static SSH_Link_Server server;
        return server;
    }

    void start() { }

    void send(std::string &&msg) {
        std::string payload = "\033]9998;";
        try {
            payload += base64::to_base64(msg);
        } catch (...) {}
        payload += "\007";

        int n = payload.size();
        int t = 0;
        int w = 0;
        while (t < n) {
            errno = 0;
            w = write(STDOUT_FILENO, payload.c_str() + t, n - t);

            if (w > 0) {
                t += w;
            } else if (w < 0 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            } else {
                break;
            }
        }
    }

    std::optional<std::string> pull_next() {

check:;
        if (auto msg = this->inbox.try_pop()) { return *msg; }

        static const char  *osc_state = OSC_PATTERN;
        static std::string  cur_msg;

        char buff[4096];

        int n = 0;
        while (this->inbox.size() == 0 && (n = read(STDIN_FILENO, buff, sizeof(buff))) > 0) {
            for (int i = 0; i < n; i += 1) {
                if (*osc_state == 0) {
                    if (buff[i] == '\x07') {
                        try {
                            this->inbox.push(base64::from_base64(cur_msg));
                        } catch (...) {}
                        cur_msg.clear();
                        osc_state = OSC_PATTERN;
                    } else {
                        cur_msg += buff[i];
                    }
                } else if (buff[i] == *osc_state) {
                    osc_state += 1;
                } else {
                    osc_state = OSC_PATTERN;
                }
            }
        }

        if (n > 0) { goto check; }

        return {};
    }
};

}
