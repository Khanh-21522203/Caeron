#pragma once

#include "caeron/common/types.h"

#include <pthread.h>
#include <functional>
#include <string>

namespace caeron::platform {

/// Thread wrapper with CPU affinity support.
class Thread
{
public:
    explicit Thread(std::function<void()> func, const std::string& name = "");
    ~Thread();

    Thread(Thread&& other) noexcept;
    Thread& operator=(Thread&& other) noexcept;
    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    void join();
    void set_affinity(i32 cpu_index);
    void set_name(const std::string& name);

    [[nodiscard]] bool joinable() const noexcept { return joinable_; }

private:
    static void* thread_func(void* arg);

    pthread_t thread_{};
    std::function<void()> func_;
    bool joinable_ = false;
};

} // namespace caeron::platform
