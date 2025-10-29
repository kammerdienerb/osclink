#pragma once

#include <string>
#include <optional>
#include <memory>
#include <thread>
#include <functional>

#include "common.hpp"
#include "log.hpp"
#include "ssh_link_inbox.hpp"
#include "base64.hpp"

#define LIBSSH_STATIC 1
#include "libssh/libsshpp.hpp"
#include "libssh/sftp.h"

namespace {

struct SSH_Link_Client {
    enum class State {
        INIT,
        CONNECTED,
        AUTHENTICATED,
        ATTACHED,
    };

    using enum State;

private:
    SSH_Link_Inbox                inbox;
    State                         state = INIT;
    std::unique_ptr<ssh::Session> session;
    std::unique_ptr<ssh::Channel> sftp_channel;
    sftp_session                  sftp = NULL;
    std::unique_ptr<ssh::Channel> server_channel;
    bool                          known_host = false;
    std::string                   error_string;
    std::thread                   thr;
    int                           read_thread_should_stop = 0;
public:
    std::string                   user;
    std::string                   hostname;
    std::string                   pass;

public:
    void default_fill_user_and_host() {
        const char *user = getenv("USER");
        if (user != NULL) {
            this->user = user;
        }
        this->hostname = "localhost";
    }

    SSH_Link_Client() : session(new ssh::Session) {
        default_fill_user_and_host();
    }

    ~SSH_Link_Client() {
        this->disconnect();
    }

    void connect() {
        this->error_string.clear();

        this->session->setOption(SSH_OPTIONS_USER, this->user.c_str());
        this->session->setOption(SSH_OPTIONS_HOST, this->hostname.c_str());

        try {
            this->session->connect();
            this->state = CONNECTED;

            if (this->session->isServerKnown() == SSH_SERVER_KNOWN_OK) {
                this->known_host = true;
            }

        } catch (ssh::SshException &e) {
            this->error_string = e.getError();
        }
    }

    void try_authenticate_with_publickey() {
        if (this->session->userauthPublickeyAuto() == SSH_AUTH_SUCCESS) {
            this->state = AUTHENTICATED;
        }
    }

    void authenticate_with_password() {
        this->error_string.clear();

        try {
            if (this->session->userauthPassword(this->pass.c_str()) == SSH_AUTH_SUCCESS) {
                this->state = AUTHENTICATED;
            } else {
                this->error_string = this->session->getError();
            }
        } catch (ssh::SshException &e) {
            this->error_string = e.getError();
        }
    }

private:
    void cleanup_channels() {
        if (this->sftp != NULL) {
            sftp_free(this->sftp);
            this->sftp = NULL;
        }
        if (this->sftp_channel) {
            if (this->sftp_channel->isOpen()) {
                this->sftp_channel->sendEof();
                this->sftp_channel->close();
            }
            this->sftp_channel.reset();
        }
        if (this->server_channel) {
            if (this->server_channel->isOpen()) {
                this->server_channel->sendEof();
                this->server_channel->close();
            }
            this->server_channel.reset();
        }
    }

    static void read_thread(SSH_Link_Client &self) {
        static constexpr const char *OSC_PATTERN = "\033]9998;";

        char         buff[4096];
        const char  *osc_state = OSC_PATTERN;
        std::string  cur_msg;

        while (!self.read_thread_should_stop && !self.server_channel->isEof()) {
            int n = 0;

            try {
                n = self.server_channel->read(buff, sizeof(buff), 100);
            } catch (...) {
                break;
            }

            for (int i = 0; i < n; i += 1) {
                if (*osc_state == 0) {
                    if (buff[i] == '\x07') {
                        try {
                            self.inbox.push(base64::from_base64(cur_msg));
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
    }

public:
    void disconnect() {
        if (this->thr.joinable()) {
            this->read_thread_should_stop = 1;
            this->thr.join();
        }

        this->cleanup_channels();

        this->session->disconnect();
        this->session.reset(new ssh::Session);

        this->known_host = false;
        this->error_string = "";
        default_fill_user_and_host();
        this->state = INIT;
    }

    void start() {
        if (this->state != AUTHENTICATED) { return; }

        std::string pass = std::move(this->pass);

        DEFER {
            pass.resize(pass.capacity(), 0);
            typedef void* (*memset_t)(void*, int, size_t);
            static volatile memset_t memset_func = memset;
            memset_func(pass.data(), 0, pass.size());
            pass.clear();
        };

        this->sftp_channel.reset(new ssh::Channel(*this->session));
        try {
            this->sftp_channel->openSession();
            this->sftp_channel->requestSubsystem("sftp");
        } catch (ssh::SshException &e) {
            this->error_string = e.getError();
            this->cleanup_channels();
            return;
        }

        this->sftp = sftp_new_channel(this->session->getCSession(), this->sftp_channel->getCChannel());
        if (this->sftp == NULL) {
            this->error_string = "Error creating SFTP session: code " + std::to_string(sftp_get_error(this->sftp));
            this->cleanup_channels();
            return;
        }

        int rc = sftp_init(this->sftp);
        if (rc != SSH_OK) {
            this->error_string = "Error initializing SFTP session: code " + std::to_string(sftp_get_error(this->sftp));
            this->cleanup_channels();
            return;
        }

        sftp_attributes attr = sftp_stat(this->sftp, "osclink/build/server");
        if (attr == NULL) {
            int err = sftp_get_error(this->sftp);

            this->error_string = "osclink/build/server: ";

            if (err == SSH_FX_NO_SUCH_FILE) {
                this->error_string += "no such file";
            } else if (err == SSH_FX_PERMISSION_DENIED) {
                this->error_string += "permission denied";
            } else {
                this->error_string += "unknown error " + std::to_string(err);
            }

            this->cleanup_channels();
            return;
        }

        sftp_attributes_free(attr);

        this->server_channel.reset(new ssh::Channel(*this->session));
        try {
            this->server_channel->openSession();
        } catch (ssh::SshException &e) {
            this->error_string = e.getError();
            this->cleanup_channels();
            return;
        }

        try {
            this->server_channel->requestExec("sudo osclink/build/server");
        } catch (ssh::SshException &e) {
            this->error_string = e.getError();
            this->cleanup_channels();
            return;
        }

        this->thr = std::thread(read_thread, std::ref(*this));

        this->state = ATTACHED;
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
            try {
                w = this->server_channel->write(payload.c_str() + t, n - t);
            } catch (...) {
                break;
            }

            if (w > 0) {
                t += w;
            } else if (w < 0) {
                break;
            }
        }
    }

    std::optional<std::string> try_pull() {
        return this->inbox.try_pop();
    }

    void finish() {
        this->disconnect();
    }

    State get_state() const { return this->state; }

    const std::string &get_error_string() const { return this->error_string; }

    bool is_host_known() const { return this->known_host; }
    void ignore_unknown_host() { this->known_host = true; }
    void write_known_hosts() {
        try {
            this->session->writeKnownhost();
            this->known_host = true;
        } catch (ssh::SshException &e) {
            this->error_string = e.getError();
        }
    }
};

}
