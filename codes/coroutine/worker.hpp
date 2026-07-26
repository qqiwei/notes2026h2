#pragma once

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

struct Worker {
  Worker() : Worker(1) {}
  explicit Worker(size_t thread_count)
      : thread_count_(std::max<size_t>(1, thread_count)) {}

  // A pool sized to the machine's CPU count (1 if hardware_concurrency()
  // reports 0, e.g. a detached/invalidator environment).
  static Worker with_hardware_parallelism() {
    unsigned hw = std::thread::hardware_concurrency();
    return Worker(hw == 0 ? 1 : hw);
  }

  void start() {
    threads_.reserve(thread_count_);
    for (size_t i = 0; i < thread_count_; ++i)
      threads_.emplace_back([this](std::stop_token st) { run(st); });
  }

  void submit(std::function<void()> fn) {
    {
      std::lock_guard lock(tasks_mtx_);
      tasks_.push(std::move(fn));
    }
    cv_.notify_one();
  }

  // Signals every worker thread to stop and discards the tasks still queued at
  // the moment of the call; tasks already running are not interrupted.
  void stop() {
    for (auto &t : threads_)
      t.request_stop();
    cv_.notify_all();
  }

  void join() {
    for (auto &t : threads_)
      if (t.joinable())
        t.join();
    threads_.clear();
  }

  size_t thread_count() const noexcept { return thread_count_; }

private:
  void run(std::stop_token st) {
    while (!st.stop_requested()) {
      std::function<void()> task;
      {
        std::unique_lock lock(tasks_mtx_);
        cv_.wait(lock, st, [&] { return st.stop_requested() || !tasks_.empty(); });
        if (st.stop_requested())
          return;
        task = std::move(tasks_.front());
        tasks_.pop();
      }
      task();
    }
  }

  size_t thread_count_;
  std::vector<std::jthread> threads_;
  std::mutex tasks_mtx_;
  std::condition_variable_any cv_;
  std::queue<std::function<void()>> tasks_;
};