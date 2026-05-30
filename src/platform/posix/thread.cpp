#include "thread.h"

#include <pthread.h>
#include <stdexcept>

namespace caeron::platform {

Thread::Thread(std::function<void()> func, const std::string& name)
    : func_{std::move(func)}
{
    if (pthread_create(&thread_, nullptr, thread_func, this) != 0)
        throw std::runtime_error("Failed to create thread");
    joinable_ = true;

    if (!name.empty())
        set_name(name);
}

Thread::~Thread()
{
    if (joinable_)
        pthread_detach(thread_);
}

Thread::Thread(Thread&& other) noexcept
    : thread_{other.thread_}, func_{std::move(other.func_)}, joinable_{other.joinable_}
{
    other.joinable_ = false;
}

Thread& Thread::operator=(Thread&& other) noexcept
{
    if (this != &other)
    {
        if (joinable_)
            pthread_detach(thread_);
        thread_ = other.thread_;
        func_ = std::move(other.func_);
        joinable_ = other.joinable_;
        other.joinable_ = false;
    }
    return *this;
}

void Thread::join()
{
    if (!joinable_)
        throw std::runtime_error("Thread not joinable");
    pthread_join(thread_, nullptr);
    joinable_ = false;
}

void Thread::set_affinity(i32 cpu_index)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(static_cast<size_t>(cpu_index), &cpuset);
    pthread_setaffinity_np(thread_, sizeof(cpu_set_t), &cpuset);
}

void Thread::set_name(const std::string& name)
{
    pthread_setname_np(thread_, name.c_str());
}

void* Thread::thread_func(void* arg)
{
    auto* self = static_cast<Thread*>(arg);
    self->func_();
    return nullptr;
}

} // namespace caeron::platform
