#include "coro.hpp"

template <typename Promise> struct SymmetricTransFinalAwaiter {
  bool await_ready() const noexcept { return false; }
  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<Promise> single_task_h) noexcept {
    auto task_handle = single_task_h.promise().task_handle_;
    LOG_MSG_F("void SyncTransTask::final_awaiter::await_suspend",
              "[D2] single task's: {:#010x}, trans to task's: {:#010x}",
              short_addr(single_task_h), short_addr(task_handle));
    return task_handle;
  }
  void await_resume() const noexcept { LOG_FUNCTION(); }
};

struct SyncTransTask {
  struct promise_type {
    std::coroutine_handle<> task_handle_ = nullptr;

    promise_type() {
      // LOG_FUNCTION();
    }
    ~promise_type() {
      // LOG_FUNCTION();
    }

    SyncTransTask get_return_object() {
      // LOG_FUNCTION();
      return SyncTransTask{
          std::coroutine_handle<promise_type>::from_promise(*this)};
    }
    std::suspend_always initial_suspend() noexcept {
      // LOG_FUNCTION();
      return {};
    }

    using final_awaiter = SymmetricTransFinalAwaiter<promise_type>;

    final_awaiter final_suspend() noexcept {
      // LOG_FUNCTION();
      return {};
    }
    void return_void() noexcept {
      //  LOG_FUNCTION();
    }
    void unhandled_exception() {
      LOG_FUNCTION();
      std::terminate();
    }
  };

  std::coroutine_handle<promise_type> handle_;
  SyncTransTask(std::coroutine_handle<promise_type> h) : handle_(h) {
    // LOG_FUNCTION();
  }
  ~SyncTransTask() {
    LOG_FUNCTION();
    if (handle_)
      handle_.destroy();
  }

  struct awaiter {
    std::coroutine_handle<promise_type> handle_;

    bool await_ready() const noexcept {
      // LOG_FUNCTION();
      return false;
    }
    std::coroutine_handle<>
    await_suspend(std::coroutine_handle<> task_handle) noexcept {
      // LOG_FUNCTION();
      auto &promise = handle_.promise();
      promise.task_handle_ = task_handle;
      LOG_MSG("[D1] save task's: {:#010x}, trans to single task's: {:#010x}",
              short_addr(task_handle), short_addr(handle_));
      return handle_;
    }
    void await_resume() const noexcept { LOG_FUNCTION(); }
  };

  awaiter operator co_await() const noexcept { return awaiter{handle_}; }
};

SyncTransTask make_single_task() {
  return make_single_task_impl<SyncTransTask>(FUNCTION);
}

Task make_task(int count) {
  return make_task_impl<SyncTransTask>(FUNCTION, count, &make_single_task);
}

int main() {
  LOG_MSG("make task");
  auto task = make_task(3);

  TRACK_RESUME(task.handle_.resume());

  return 0;
}
