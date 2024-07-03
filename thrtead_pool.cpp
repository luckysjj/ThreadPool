#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

#if ((defined(_MSVC_LANG) && _MSVC_LANG >= 201703L) || __cplusplus >= 201703L)
#define WITH_TPOOL17
#include <type_traits>
#endif

namespace ThreadUtils {
    class ThreadPool {
    public:
        ThreadPool(size_t);
#if defined(WITH_TPOOL17)
        template<class F, class... Args>
        auto enqueue(F&& f, Args&&... args)
            ->std::future<typename std::invoke_result<F, Args...>::type>;
#else
        template<class F, class... Args>
        auto enqueue(F&& f, Args&&... args)
            ->std::future<typename std::result_of<F(Args...)>::type>;
#endif
        ~ThreadPool();

        // Get number of tasks currently in the queue
        size_t numTasks() const;
    private:
        // need to keep track of threads so we can join them
        std::vector< std::thread > workers;
        // the task queue
        std::queue< std::packaged_task<void()> > tasks;

        // synchronization
        mutable std::mutex queue_mutex;
        std::condition_variable condition;
        bool stop{false};
    };

    // the constructor just launches some amount of workers
    inline ThreadPool::ThreadPool(size_t threads)
    {
        for (size_t i = 0; i < threads; ++i)
            workers.emplace_back(
                [this]
                {
                    for (;;)
                    {
                        std::packaged_task<void()> task;

                        {
                            std::unique_lock<std::mutex> lock(this->queue_mutex);
                            this->condition.wait(lock,
                                [this] { return this->stop || !this->tasks.empty(); });
                            if (this->stop && this->tasks.empty())
                                return;
                            task = std::move(this->tasks.front());
                            this->tasks.pop();
                        }

                        task();
                    }
                }
                );
    }

    // add new work item to the pool
#if defined(WITH_TPOOL17)
    template<class F, class... Args>
    auto ThreadPool::enqueue(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>
    {
        using return_type = typename std::invoke_result<F, Args...>::type;

#if defined(WIN32)
        // Note: we need to do this silly shared_ptr of the task + lambda it for windows because of 
        // https://developercommunity.visualstudio.com/t/unable-to-move-stdpackaged-task-into-any-stl-conta/108672
        auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // don't allow enqueueing after stopping the pool
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace([task]() { (*task)(); });
        }
#else
        std::packaged_task<return_type()> task(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );

        std::future<return_type> res = task.get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // don't allow enqueueing after stopping the pool
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace(std::move(task));
        }
#endif
        condition.notify_one();
        return res;
    }
#else
    template<class F, class... Args>
    auto ThreadPool::enqueue(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        // Note: we need to do this silly shared_ptr of the task + lambda it for windows because of 
        // https://developercommunity.visualstudio.com/t/unable-to-move-stdpackaged-task-into-any-stl-conta/108672
        auto task = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // don't allow enqueueing after stopping the pool
            if (stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }
#endif

    // the destructor joins all threads
    inline ThreadPool::~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    inline size_t ThreadPool::numTasks() const
    {
        std::lock_guard<std::mutex> lock(queue_mutex);
        return tasks.size();
    }
}

#endif
