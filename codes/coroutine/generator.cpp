#include <chrono>
#include <mutex>
#include <string_view>
#include <thread>

#include "coro.hpp"
struct generator {
  struct promise_type {
    int val{};

    promise_type() { LOG_FUNCTION(); }
    ~promise_type() { LOG_FUNCTION(); }
    generator get_return_object() {
      // LOG_FUNCTION();
      auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
      return generator{handle};
    }
    std::suspend_always initial_suspend() {
      // LOG_FUNCTION();
      return {};
    }
    std::suspend_always final_suspend() noexcept {
      // LOG_FUNCTION();
      return {};
    }
    std::suspend_always yield_value(int value) {
      val = value;
      LOG_MSG("val {} is saved", val);
      return {};
    }
    void return_void() { LOG_FUNCTION(); }
    void unhandled_exception() {
      LOG_FUNCTION();
      std::terminate();
    }
  };

  std::coroutine_handle<promise_type> handle;

  generator(std::coroutine_handle<promise_type> h) : handle(h) {
    LOG_FUNCTION();
  }

  ~generator() {
    LOG_FUNCTION();
    if (handle)
      handle.destroy();
  }
};

generator make_generator() {
  // LOG_FUNCTION();
  for (int i = 1; i <= 4; ++i) {
    int val = i * 10;
    LOG_MSG("co_yield {}", val);
    co_yield val;
  }
}

int main() {
  LOG_FUNCTION();
  auto gen = make_generator();

  // for states and values in the coroutine frame
  std::mutex mtx_generator;
  auto consumer = [&](std::string_view func) {
    // LOG_F(func);
    for (int i = 0; i < 2; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));

      {
        std::lock_guard<std::mutex> resume_lock(mtx_generator);
        TRACK_RESUME(gen.handle.resume());

        LOG_MSG_F(func, "lookup val:{}", gen.handle.promise().val);
      }
    }
  };

  std::thread thrd2(consumer, "consumer thrd2");
  std::thread thrd3(consumer, "consumer thrd3");

  thrd2.join();
  thrd3.join();

  TRACK_RESUME(gen.handle.resume());
  return 0;
}
