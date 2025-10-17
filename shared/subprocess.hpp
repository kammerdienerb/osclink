#pragma once

#include <string>
#include <vector>
#include <optional>
#include <thread>
#include <chrono>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

namespace {

using namespace std::chrono_literals;

struct Subprocess {
    enum class Error {
        NONE = 0,
        PIPE,
        FORK,
        FCNTL,
        WAITPID,
        READ,
        TIMEOUT,
    };

private:
    enum class State {
        ERROR,
        INIT,
        RUNNING,
        COMPLETED,
    };

    using Duration = std::chrono::milliseconds;

    State                    _state = State::INIT;
    Error                    _error = Error::NONE;
    std::vector<std::string> args;
    std::vector<const char*> cargs;
    std::optional<Duration>  timeout;
    Duration                 poll_period;
    pid_t                    pid = -1;
    int                      pipe_fds[2];
    int                      _exit_status = 0;
    std::thread              thr;
    std::string              _output;

    static void thread_fn(Subprocess *proc) {
        auto elapsed    = Duration(0);
        int finished    = 0;
        int exited      = 0;
        int exit_status = -1;

        std::string output;

        while (!finished) {
            if (!exited) {
                int wait_status;

                if (waitpid(proc->pid, &wait_status, WNOHANG) == -1) {
                    proc->_state = State::ERROR;
                    proc->_error = Error::WAITPID;
                    kill(proc->pid, 9);
                    errno = 0;
                    goto out;
                }

                exited = WIFEXITED(wait_status);

                if (exited) {
                    exit_status = WEXITSTATUS(wait_status);
                }
            }

            if (exited) {
                finished = 1;
            }

            char buff[1024];
            int  n = 0;

            while ((n = read(proc->pipe_fds[0], buff, sizeof(buff) - 1)) > 0) {
                buff[n] = 0;
                output += buff;
            }

            if (n < 0) {
                if (errno == EAGAIN || errno == EINTR) {
                    errno = 0;
                    finished = 0;
                } else {
                    proc->_state = State::ERROR;
                    proc->_error = Error::READ;
                    kill(proc->pid, 9);
                    errno = 0;
                    goto out;
                }
            }

            std::this_thread::sleep_for(proc->poll_period);
            elapsed += proc->poll_period;

            if (auto timeout = proc->timeout) {
                if (elapsed >= *timeout) {
                    proc->_state = State::ERROR;
                    proc->_error = Error::TIMEOUT;
                    kill(proc->pid, 9);
                    errno = 0;
                    goto out;
                }
            }
        }
out:;
        close(proc->pipe_fds[0]);
        if (proc->_state != State::ERROR) {
            proc->_output      = std::move(output);
            proc->_exit_status = exit_status;
        }
    }

public:
    Subprocess() = delete;
    Subprocess(const Subprocess&) = delete;

    Subprocess(std::vector<std::string> args, std::optional<Duration> timeout = {}, Duration poll_period = 50ms)
            : args(args), timeout(timeout), poll_period(poll_period) {

        for (auto &arg : this->args) { this->cargs.push_back(arg.c_str()); }
        this->cargs.push_back(NULL);

        if (pipe(this->pipe_fds) == -1) {
            this->_state = State::ERROR;
            this->_error = Error::PIPE;
            errno = 0;
            goto out;
        }

        this->pid = fork();

        if (this->pid == -1) {
            this->_state = State::ERROR;
            this->_error = Error::FORK;
            errno = 0;
            close(this->pipe_fds[0]);
            close(this->pipe_fds[1]);
            goto out;
        }

        if (this->pid == 0) {
            while ((dup2(this->pipe_fds[1], 1) == -1) && (errno == EINTR)) {}
            close(this->pipe_fds[1]);
            close(this->pipe_fds[0]);
            execvp(this->cargs[0], (char* const*)this->cargs.data());
            exit(99);
        }

        close(this->pipe_fds[1]);

        if (fcntl(this->pipe_fds[0], F_SETFL, O_NONBLOCK) == -1) {
            this->_state = State::ERROR;
            this->_error = Error::FCNTL;
            errno = 0;
            close(this->pipe_fds[0]);
            goto out;
        }

        this->thr = std::thread(thread_fn, this);

        this->_state = State::RUNNING;
out:;
    }

    ~Subprocess() {
        if (this->_state == State::RUNNING) {
            this->terminate();
        }
    }

    Error error() { return this->_error; }

    void join() {
        if (this->_state != State::RUNNING) { return; }
        this->thr.join();
        this->_state = State::COMPLETED;
    }

    void terminate() {
        if (this->_state != State::RUNNING) { return; }
        kill(this->pid, 9);
        this->join();
    }

    std::optional<std::string> output() {
        if (this->_state != State::COMPLETED) { return {}; }
        return this->_output;
    }

    std::optional<int> exit_status() {
        if (this->_state != State::COMPLETED) { return {}; }
        return this->_exit_status;
    }
};

}
