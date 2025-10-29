#pragma once

#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <optional>

namespace {

struct SSH_Link_Inbox {

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

}
