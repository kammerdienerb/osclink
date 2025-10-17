#pragma once

#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

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

#include "base64.hpp"


namespace {

struct OSCLink_Inbox {

private:
    std::queue<std::string> q;
    std::mutex              mtx;
    std::condition_variable cv;

public:
    void push(const std::string &&msg) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(std::move(msg));
        cv.notify_one();
    }

    std::optional<std::string> try_pop(void) {
        std::lock_guard<std::mutex> lock(mtx);

        if (q.empty()) return {};

        auto msg = std::move(q.front());
        q.pop();

        return msg;
    }

    std::string wait_and_pop(void) {
        std::unique_lock<std::mutex> lock(mtx);

        cv.wait(lock, [this]{ return !q.empty(); });
        auto msg = q.front();
        q.pop();

        return msg;
    }

    size_t size() { return this->q.size(); }
};

struct OSCLink_Client {
private:
    int             primary_fd;
    int             replica_fd;
    std::thread     pty_thr;
    int             pty_read_thread_should_stop = 0;
    struct termios  sav_term;
    OSCLink_Inbox   inbox;

    static constexpr const char *OSC_PATTERN = "\033]9998;";

public:
    static OSCLink_Client& get() {
        static OSCLink_Client client;
        return client;
    }

    void start() {
        this->setup_pty();
        this->pty_thr = std::thread(pty_read_thread);
    }

    void send(std::string &&msg) {
        std::string payload = "\033]9999;";
        try {
            payload += base64::to_base64(msg);
        } catch (...) {}
        payload += "\007\n";

        int n = payload.size();
        int t = 0;
        int w = 0;
        while (t < n) {
            errno = 0;
            w = write(this->primary_fd, payload.c_str() + t, n - t);

            if (w > 0) {
                t += w;
            } else if (w < 0 && (errno == EINTR || errno == EAGAIN)) {
                continue;
            } else {
                break;
            }
        }
    }

    std::optional<std::string> try_pull() {
        return this->inbox.try_pop();
    }

    void finish() {
        this->pty_read_thread_should_stop = 1;
        this->pty_thr.join();
        close(primary_fd);
    }

private:
    static void restore_term() {
        tcsetattr(0, TCSAFLUSH, &get().sav_term);
        printf("\nOSCLink closed.\n");
    }

    static void sigwinch_handler(int sig) {
        struct winsize ws;

        ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
        ioctl(get().primary_fd, TIOCSWINSZ, &ws);
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

    void setup_pty() {
        /* Set up terminal. */
        struct termios raw_term;

        tcgetattr(0, &this->sav_term);
        raw_term = this->sav_term;

        raw_term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw_term.c_oflag &= ~(OPOST | ONLCR);
        raw_term.c_cflag |= (CS8);
        raw_term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw_term.c_cc[VMIN] = 1;
        raw_term.c_cc[VTIME] = 0;

        tcsetattr(0, TCSAFLUSH, &raw_term);

        atexit(restore_term);

        /* Catch signals to change terminal size and restore terminal on exit. */
        struct sigaction sa;

        sigemptyset(&sa.sa_mask);
        sa.sa_flags   = 0;
        sa.sa_handler = sigwinch_handler;
        sigaction(SIGWINCH, &sa, NULL);

        sigemptyset(&sa.sa_mask);
        sa.sa_flags   = 0;
        sa.sa_handler = sigterm_handler;
        sigaction(SIGTERM, &sa, NULL);


        /* Create a PTY and launch a shell connected to it. */
        struct winsize ws;

        ws.ws_row    = 24;
        ws.ws_col    = 80;
        ws.ws_xpixel = 0;
        ws.ws_ypixel = 0;

        openpty(&this->primary_fd, &this->replica_fd, NULL, NULL, &ws);

        const char *hello = "OSCLink activated. Run the server application locally or remotely in this terminal.\n\r";
        write(STDOUT_FILENO, hello, strlen(hello));

        sigwinch_handler(0);

        pid_t p = fork();

        if (p == 0) {
            close(this->primary_fd);
            login_tty(this->replica_fd);
            const char *shell = getenv("SHELL");
            if (shell == NULL) {
                shell = getpwuid(geteuid())->pw_shell;
            }
            if (shell == NULL) {
                shell = "/usr/bin/bash";
            }

            execlp(shell, shell, NULL);
            exit(123);
        }

        int flags = fcntl(this->primary_fd, F_GETFL);
        int err = fcntl(this->primary_fd, F_SETFL, flags | O_NONBLOCK);
        (void)err;
        flags = fcntl(STDIN_FILENO, F_GETFL);
        err = fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

        close(this->replica_fd);
    }

    static void pty_read_thread() {
        std::string cur_msg;

        /* Read from PTY and look for messages. Pass STDIN on to PTY. */
        struct pollfd pfds[2];

        pfds[0].fd      = STDIN_FILENO;
        pfds[0].events  = POLLIN | POLLHUP | POLLERR;
        pfds[0].revents = 0;
        pfds[1].fd      = get().primary_fd;
        pfds[1].events  = POLLIN | POLLHUP | POLLERR;
        pfds[1].revents = 0;

        char buff[4096];
        int  n;
        int  w;
        int  t;

        const char *osc_state = OSC_PATTERN;

        while (!get().pty_read_thread_should_stop) {
            errno = 0;

            if (poll(pfds, 2, 100) <= 0) {
                if (errno) {
                    if (errno != EINTR) {
                        goto out;
                    }

                    errno = 0;
                }
                continue;
            }

            if (pfds[0].revents & POLLHUP || pfds[0].revents & POLLERR) { goto out; }
            if (pfds[1].revents & POLLHUP || pfds[1].revents & POLLERR) { goto out; }

            if (pfds[0].revents & POLLIN) {
                while ((n = read(pfds[0].fd, buff, sizeof(buff))) > 0) {
                    t = 0;
                    while (t < n) {
                        w = write(get().primary_fd, buff + t, n - t);

                        if (w > 0) {
                            t += w;
                        } else if (w < 0 && (errno == EINTR || errno == EAGAIN)) {
                            continue;
                        } else {
                            goto out;
                        }
                    }
                }

                if (n < 0) {
                    if (errno != EWOULDBLOCK && errno != EINTR) {
                        goto out;
                    }
                }
            }

            if (pfds[1].revents & POLLIN) {
                while ((n = read(pfds[1].fd, buff, sizeof(buff))) > 0) {
                    for (int i = 0; i < n; i += 1) {
                        if (*osc_state == 0) {
                            if (buff[i] == '\x07') {
                                try {
                                    get().inbox.push(base64::from_base64(cur_msg));
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

                    t = 0;
                    while (t < n) {
                        w = write(STDOUT_FILENO, buff + t, n - t);

                        if (w > 0) {
                            t += w;
                        } else if (w < 0 && (errno == EINTR || errno == EAGAIN)) {
                            continue;
                        } else {
                            goto out;
                        }
                    }
                }

                if (n < 0) {
                    if (errno != EWOULDBLOCK && errno != EINTR && errno != EAGAIN) {
                        goto out;
                    }
                }
            }
        }

out:;
    }

    OSCLink_Client() {}

};


struct OSCLink_Server {
private:
    struct termios sav_term;
    OSCLink_Inbox  inbox;

    static constexpr const char *OSC_PATTERN = "\033]9999;";

public:
    static OSCLink_Server& get() {
        static OSCLink_Server server;
        return server;
    }

    void start() {
        this->setup_pty();
    }

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

    std::string pull_next() {

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

        goto check;

        __builtin_unreachable();
        return "";
    }

private:
    static void restore_term() {
        tcsetattr(0, TCSAFLUSH, &get().sav_term);
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

    void setup_pty() {
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
    }
};

}
