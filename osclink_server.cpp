#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#include "base64.hpp"

static struct termios sav_term;

static void restore_term(void) {
    tcsetattr(0, TCSAFLUSH, &sav_term);
}

static void sigterm_handler(int sig) {
    struct sigaction sa;

    sa.sa_handler = SIG_DFL;
    sa.sa_flags = 0;
    sigemptyset (&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);

    restore_term();

    kill(0, SIGTERM);
}

static void send_to_client(std::string &&msg) {
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

int main(void) {
    /* Set up terminal. */
    struct termios raw_term;
    tcgetattr(0, &sav_term);
    raw_term = sav_term;
    raw_term.c_lflag &= ~(ECHO | ICANON);
    tcsetattr(0, TCSAFLUSH, &raw_term);
    atexit(restore_term);

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags   = 0;
    sa.sa_handler = sigterm_handler;
    sigaction(SIGTERM, &sa, NULL);


    printf("Server started. Reaching out to client.\n");

    send_to_client("SERVER-CONNECT");


    const char *osc_pattern = "\033]9999;";
    const char *osc_state   = osc_pattern;

    char buff[4096];
    std::string cur_msg;

    int n = 0;
    while ((n = read(STDIN_FILENO, buff, sizeof(buff))) > 0) {
        for (int i = 0; i < n; i += 1) {
            if (*osc_state == 0) {
                if (buff[i] == '\x07') {
                    try {
                        printf("got a request\n");
                        auto content = base64::from_base64(cur_msg);

                        std::string out = "HEATMAP-DATA";
                        for (int i = 0; i < 500; i += 1) {
                            out += "\n";
                            int n = random() % 100000;
                            out += std::to_string(n);
                        }
                        send_to_client(std::move(out));
                    } catch (...) {}
                    cur_msg.clear();
                    osc_state = osc_pattern;
                } else {
                    cur_msg += buff[i];
                }
            } else if (buff[i] == *osc_state) {
                osc_state += 1;
            } else {
                osc_state = osc_pattern;
            }
        }
    }

    return 0;
}
