#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace jarvis::core {

/// Fixed-size worker pool for work that must not run on the UI thread.
///
/// JARVIS keeps the Qt GUI thread free at all times (60 FPS target), so model
/// loading, audio processing, file scanning and tool execution are dispatched
/// here. Results travel back to the UI through queued Qt signals, never by
/// touching UI objects from a worker.
///
/// The pool is thread-safe: submit(), post(), pendingTasks() and waitIdle() may
/// be called concurrently from any thread, including from inside a task.
class ThreadPool {
public:
    /// \param threadCount Number of workers. 0 selects hardware_concurrency()-1
    ///        (at least 1), leaving a core for the UI thread.
    explicit ThreadPool(std::size_t threadCount = 0);

    /// Drains queued work, then joins every worker.
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /// Queue a callable and get a future for its result. Exceptions thrown by
    /// the task are captured in the future, as with std::async.
    ///
    /// Submitting after shutdown() returns a future holding a broken-promise
    /// exception rather than silently dropping the work.
    template <typename F, typename... Args>
    [[nodiscard]] auto submit(F&& fn, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>;

    /// Queue fire-and-forget work. Exceptions are routed to the handler
    /// installed by setExceptionHandler(); without one they are swallowed so a
    /// single bad task cannot terminate the process.
    ///
    /// Takes a move-only function so a task may own something that must not be
    /// duplicated. A confirmation grant is the motivating case: copying one
    /// would mean two callers each holding permission the user gave once.
    void post(std::move_only_function<void()> task);

    /// Install a handler for exceptions escaping post()ed tasks.
    /// Called on the worker thread; must be thread-safe.
    void setExceptionHandler(std::function<void(std::exception_ptr)> handler);

    [[nodiscard]] std::size_t threadCount() const noexcept { return m_workers.size(); }

    /// Tasks queued but not yet started.
    [[nodiscard]] std::size_t pendingTasks() const;

    /// Block until the queue is empty and no task is running.
    void waitIdle();

    /// Stop accepting work, drain the queue, join the workers. Idempotent.
    void shutdown();

    [[nodiscard]] bool isShutdown() const;

private:
    using Job = std::move_only_function<void()>;

    void workerLoop();
    void enqueue(Job job);

    mutable std::mutex m_mutex;
    std::condition_variable m_jobAvailable;
    std::condition_variable m_idle;

    std::deque<Job> m_jobs;
    std::vector<std::thread> m_workers;
    std::function<void(std::exception_ptr)> m_exceptionHandler;

    std::size_t m_activeJobs{0};
    bool m_stopping{false};
};

// --- implementation -------------------------------------------------------

template <typename F, typename... Args>
auto ThreadPool::submit(F&& fn, Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
    using ReturnType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

    // std::packaged_task is move-only while std::function requires a copyable
    // target, so the task is owned through a shared_ptr.
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        [fn = std::forward<F>(fn),
         argsTuple = std::make_tuple(std::forward<Args>(args)...)]() mutable -> ReturnType {
            return std::apply(std::move(fn), std::move(argsTuple));
        });

    std::future<ReturnType> future = task->get_future();
    enqueue([task]() { (*task)(); });
    return future;
}

} // namespace jarvis::core
