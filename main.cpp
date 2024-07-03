#include <iostream>
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool {
public:
    // 构造函数，初始化线程池并启动指定数量的线程
    ThreadPool(size_t threads);
    // 销毁线程池，等待所有任务完成
    ~ThreadPool();

    // 添加新任务到任务队列
    void enqueue(std::function<void()> task);

private:
    // 线程工作函数
    void worker();

    // 线程池中的工作线程
    std::vector<std::thread> workers;
    // 任务队列
    std::queue<std::function<void()>> tasks;
    // 同步任务队列的互斥锁
    std::mutex queue_mutex;
    // 条件变量，用于通知工作线程
    std::condition_variable condition;
    // 线程池是否停止
    bool stop;
};

ThreadPool::ThreadPool(size_t threads) : stop(false) {
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back(&ThreadPool::worker, this);
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers) {
        worker.join();
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (!stop) {
            tasks.emplace(task);
        }
    }
    condition.notify_one();
}

void ThreadPool::worker() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            condition.wait(lock, [this] { return stop || !tasks.empty(); });
            if (stop && tasks.empty()) {
                return;
            }
            task = std::move(tasks.front());
            tasks.pop();
        }
        task();
    }
}

int main() {
    // 创建线程池，包含4个线程
    ThreadPool pool(4);

    // 添加任务到线程池
    pool.enqueue([] {
        std::cout << "任务1正在执行\n";
    });
    pool.enqueue([] {
        std::cout << "任务2正在执行\n";
    });
    pool.enqueue([] {
        std::cout << "任务3正在执行\n";
    });

    // 主线程等待一段时间，让所有任务执行完成
    std::this_thread::sleep_for(std::chrono::seconds(2));

    return 0;
}
