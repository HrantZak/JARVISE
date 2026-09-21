#include "jarvis/core/ThreadPool.h"

#include <algorithm>

namespace jarvis::core {
namespace {

std::size_t defaultThreadCount() {
    const unsigned hardware = std::thread::hardware_concurrency();
    if (hardware <= 1) {
        return 1;
    }
    // Leave one core for the Qt GUI thread.
    return static_cast<std::size_t>(hardware - 1);
}

} // namespace

ThreadPool::ThreadPool(std::size_t threadCount) {
    const std::size_t count = threadCount > 0 ? threadCount : defaultThreadCount();
    m_workers.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        m_workers.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::enqueue(Job job) {
    {
        std::lock_guard lock{m_mutex};
        if (m_stopping) {
            // Dropping the job destroys the packaged_task, which breaks its
            // promise; the caller's future reports broken_promise instead of
            // blocking forever.
            return;
        }
        m_jobs.push_back(std::move(job));
    }
    m_jobAvailable.notify_one();
}

void ThreadPool::post(std::move_only_function<void()> task) {
    if (!task) {
        return;
    }
    enqueue([this, task = std::move(task)]() mutable {
        try {
            task();
        } catch (...) {
            std::function<void(std::exception_ptr)> handler;
            {
                std::lock_guard lock{m_mutex};
                handler = m_exceptionHandler;
            }
            if (handler) {
                handler(std::current_exception());
            }
        }
    });
}

void ThreadPool::setExceptionHandler(std::function<void(std::exception_ptr)> handler) {
    std::lock_guard lock{m_mutex};
    m_exceptionHandler = std::move(handler);
}

std::size_t ThreadPool::pendingTasks() const {
    std::lock_guard lock{m_mutex};
    return m_jobs.size();
}

bool ThreadPool::isShutdown() const {
    std::lock_guard lock{m_mutex};
    return m_stopping;
}

void ThreadPool::waitIdle() {
    std::unique_lock lock{m_mutex};
    m_idle.wait(lock, [this] { return m_jobs.empty() && m_activeJobs == 0; });
}

void ThreadPool::shutdown() {
    {
        std::lock_guard lock{m_mutex};
        if (m_stopping) {
            return;
        }
        m_stopping = true;
    }
    m_jobAvailable.notify_all();

    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();
}

void ThreadPool::workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock lock{m_mutex};
            m_jobAvailable.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });

            // Drain remaining work before exiting, so shutdown() does not
            // discard jobs that were already accepted.
            if (m_jobs.empty()) {
                return;
            }

            job = std::move(m_jobs.front());
            m_jobs.pop_front();
            ++m_activeJobs;
        }

        // packaged_task and post()'s wrapper both absorb exceptions, but a
        // throwing destructor in a captured object must not escape a worker.
        try {
            job();
        } catch (...) {
        }

        {
            std::lock_guard lock{m_mutex};
            --m_activeJobs;
        }
        m_idle.notify_all();
    }
}

} // namespace jarvis::core
