#pragma once

#include <atomic>
#include <chrono>
#include <coroutine>
#include <exception>
#include <thread>

#include "log.hpp"

#ifdef SYMMETRIC
extern std::atomic<bool> g_task_finished;
#endif

inline uint32_t short_addr(const std::coroutine_handle<> &handle) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(handle.address()) &
                               0xFFFFFFFFULL);
}

struct GetHandleAwaiter {
  std::coroutine_handle<> handle_;
  bool await_ready() const noexcept {
    // LOG_FUNCTION();
    return false;
  }
  bool await_suspend(std::coroutine_handle<> h) noexcept {
    // LOG_FUNCTION();
    handle_ = h;
    return false;
  }
  std::coroutine_handle<> await_resume() const noexcept {
    // LOG_FUNCTION();
    return handle_;
  }
};

template <typename Promise> struct ResumeAwaiter {
  bool await_ready() const noexcept {
    // LOG_FUNCTION();
    return false;
  }
  void await_suspend(std::coroutine_handle<Promise> single_task_h) noexcept {
    auto task_h = single_task_h.promise().task_handle_;

    LOG_MSG_F("void SyncTask::final_awaiter::await_suspend",
              "[D2] single task's: {:#010x}, resume task's: {:#010x}",
              short_addr(single_task_h), short_addr(task_h));
    // LOG_MSG("[D2] single task's: {:#010x}, resume task's: {:#010x}",
    //         short_addr(single_task_h), short_addr(task_h));
    if (task_h) {
      TRACK_RESUME(task_h.resume());
    }
  }
  void await_resume() const noexcept { LOG_FUNCTION(); }
};

struct Task {
  struct promise_type {
    promise_type() {
      // LOG_FUNCTION();
    }
    ~promise_type() {
      // LOG_FUNCTION();
    }
    Task get_return_object() {
      // LOG_FUNCTION();
      auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
      return Task{handle};
    }
    std::suspend_always initial_suspend() noexcept {
      // LOG_FUNCTION();
      return {};
    }
    std::suspend_always final_suspend() noexcept {
      // LOG_FUNCTION();
      return {};
    }
    void return_void() noexcept {
      // LOG_FUNCTION();
    }
    void unhandled_exception() {
      LOG_FUNCTION();
      std::terminate();
    }
  };

  std::coroutine_handle<promise_type> handle_;
  Task(std::coroutine_handle<promise_type> h) : handle_(h) {
    // LOG_FUNCTION();
  }
  ~Task() {
    LOG_FUNCTION();
    if (handle_)
      handle_.destroy();
  }
};

template <typename SingleTaskT>
SingleTaskT make_single_task_impl(const char *func) {
  auto handle = co_await GetHandleAwaiter{};
  LOG_MSG_F(func, "single task's: {:#010x}", short_addr(handle));
  co_return;
}

template <typename SingleTaskT, typename MakeSingleTaskFn>
Task make_task_impl(const char *func, int count, MakeSingleTaskFn make_single_task) {
  for (int i = 1; i <= count; ++i) {
    std::println();
    LOG_MSG_F(func, "loop [i:{}] make single task", i);
    auto single_task = make_single_task();

    auto handle = co_await GetHandleAwaiter{};

    LOG_MSG_F(func, "loop [i:{}] co_await single task, task's: {:#010x}", i,
              short_addr(handle));
    co_await single_task;

    LOG_MSG_F(func, "loop [i:{}] resumed, end", i);
  }

#ifdef SYMMETRIC
#ifdef SYMMETRIC_EXTRA
  g_task_finished.store(true, std::memory_order_release);
#endif
#endif
  co_return;
}
