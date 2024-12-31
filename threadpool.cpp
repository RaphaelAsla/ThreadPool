#include <condition_variable>
#include <functional>
#include <iomanip>
#include <iostream>
#include <queue>
#include <thread>

class ThreadPool {
  public:
    ThreadPool(int num_threads = 1) : active_tasks(0), is_active(true) {
        while (num_threads-- > 0) {
            workers.emplace_back([this] {
                for (;;) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(guard);
                        guard_condition.wait(lock, [this] { return !is_active.load() || !tasks.empty(); });
                        if (!this->is_active.load() && this->tasks.empty()) {
                            return;
                        }
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                        active_tasks++;
                    }

                    task();

                    {
                        std::lock_guard<std::mutex> lock(guard);
                        if (--active_tasks == 0 && tasks.empty()) {
                            guard_condition.notify_all();
                        }
                    }
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(guard);
            is_active = false;
        }
        guard_condition.notify_all();
        for (auto& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void AddTask(std::function<void()>&& callback) {
        {
            std::lock_guard<std::mutex> lock(guard);
            tasks.emplace(std::move(callback));
        }
        guard_condition.notify_one();
    }

    void WaitUntilEmpty() {
        std::unique_lock<std::mutex> lock(guard);
        guard_condition.wait(lock, [this] { return active_tasks == 0 && tasks.empty(); });
    }

    size_t GetQueueSize() {
        std::lock_guard<std::mutex> lock(guard);
        return tasks.size();
    }

  private:
    size_t                            active_tasks;
    std::mutex                        guard;
    std::atomic<bool>                 is_active;
    std::condition_variable           guard_condition;
    std::vector<std::thread>          workers;
    std::queue<std::function<void()>> tasks;
};

double ComputePiSegment(int startTerm, int numTerms) {
    double localSum = 0.0;
    for (int i = startTerm; i < startTerm + numTerms; ++i) {
        double term = 1.0 / (2.0 * i + 1.0);
        if (i % 2 == 0) {
            localSum += term;
        } else {
            localSum -= term;
        }
    }
    return localSum;
}

int main() {
    ThreadPool pool(std::thread::hardware_concurrency());

    int totalTerms   = std::numeric_limits<int>::max();
    int numTasks     = 50;
    int termsPerTask = totalTerms / numTasks;

    std::atomic<double> piEstimate(0.0);

    auto start = std::chrono::high_resolution_clock::now();

    for (int k = 0; k < numTasks; ++k) {
        int startTerm = k * termsPerTask;
        pool.AddTask([startTerm, termsPerTask, &piEstimate]() {
            double segmentSum = ComputePiSegment(startTerm, termsPerTask);
            piEstimate += segmentSum;

            if (startTerm / termsPerTask % 10 == 0) {
                std::cout << "Task starting at term " << startTerm << ": partial sum = " << segmentSum << std::endl;
            }
        });
    }

    pool.WaitUntilEmpty();

    double                        finalPiEstimate = 4.0 * piEstimate.load();
    auto                          end             = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration        = end - start;

    std::cout << "Tasks left: " << pool.GetQueueSize() << std::endl;
    std::cout << "Total execution time: " << duration.count() << " seconds" << std::endl;
    std::cout << "Estimated value of pi: " << std::setprecision(10) << finalPiEstimate << std::endl;

    return 0;
}
