#include "coro.hpp"

#ifdef SYMMETRIC_EXTRA
#include "worker.hpp"

// --- Shared state for the race demo ---------------------------------------
// Single tasks are resumed by a worker thread while the task runs on the
// main thread, so the two `done_.exchange(true)` calls below (awaiter on the
// main thread, final awaiter on the worker thread) genuinely race. If done_
// were a plain `bool` this would be a data race (UB).

// The thread that resumes the single tasks. Started by main() and stopped only
// after the task has finished.
static Worker g_worker;

// Set to true by main() only after its single `task.handle_.resume()` call has
// returned, which means the task is fully suspended at its first co_await
// (or already finished). The worker thread waits on this flag before resuming
// the task, so it never resumes a coroutine that is still in the middle
// of suspending.
static std::atomic<bool> g_task_suspended = false;

// Set to true by the task right before co_return so that main() knows the
// task has finished and the worker may be stopped (Worker::stop discards any
// queued tasks, so it must not be called while the task is still running on
// the worker thread).
std::atomic<bool> g_task_finished = false;
#endif

#ifdef SYMMETRIC
template <typename Promise> struct ReadyResumeAwaiter {
  bool await_ready() const noexcept {
    // LOG_FUNCTION();
    return false;
  }
  void await_suspend(std::coroutine_handle<Promise> single_task_h) noexcept {
    auto &single_task_promise = single_task_h.promise();
    // done, also means no one waiting
    // not done before, means someone waiting(in the await_suspend)
    bool done =
        single_task_promise.done_.exchange(true, std::memory_order_acq_rel);
    auto task_handle = single_task_promise.task_handle_;
    LOG_MSG_F("void SyncTask::final_awaiter::await_suspend",
              "[D2] single task's: {:#010x}, done_ before: {}, task waiting "
              "before, resume no task's: {:#010x}",
              short_addr(single_task_h), done, short_addr(task_handle));
    if (done && task_handle) {
#ifdef SYMMETRIC_EXTRA
      // done_ was already set by the awaiter (main thread): it is about to
      // suspend the waiting task. Wait until it is fully suspended, then hand
      // it back to the worker as a fresh task. We must NOT resume it directly
      // from inside this single task's resume() stack: the task would then
      // destroy this frame (via SyncTask::~SyncTask) while we are still
      // unwinding, which is a use-after-free.
      int i = 0;
      while (!g_task_suspended.load(std::memory_order_acquire)) {
        ++i;
        std::this_thread::yield();
      }
      LOG_MSG("[Worker] yield {} times, task suspended, back to worker to "
              "resume task",
              i);
      g_worker.submit([task_handle] { TRACK_RESUME(task_handle.resume()); });
#else
      // Dead code in the non-EXTRA SYMMETRIC build: the single task is resumed
      // synchronously by the awaiter (main thread), so its final awaiter always
      // exchanges done_ before the awaiter does and always sees done == false,
      // meaning this branch never runs.
      TRACK_RESUME(task_handle.resume());
#endif
    }
  }
  void await_resume() const noexcept { LOG_FUNCTION(); }
};
#endif

struct SyncTask {
  struct promise_type {
    std::coroutine_handle<> task_handle_ = nullptr;
#ifdef SYMMETRIC
    std::atomic<bool> done_ = false;
#ifdef SYMMETRIC_EXTRA
    // Set to true by the worker thread only after it has fully returned from
    // resuming this task. done_ is set by the final awaiter *while* resume() is
    // still on the worker's stack, so observing done_ alone is not enough to
    // know the worker is done with this frame.
    std::atomic<bool> worker_finished_ = false;
#endif
#endif

    promise_type() {
      // LOG_FUNCTION();
    }
    ~promise_type() {
      // LOG_FUNCTION();
    }

    SyncTask get_return_object() {
      // LOG_FUNCTION();
      auto handle = std::coroutine_handle<promise_type>::from_promise(*this);
      return SyncTask{handle};
    }
    std::suspend_always initial_suspend() noexcept {
      // LOG_FUNCTION();
      return {};
    }

#ifdef SYMMETRIC
    using final_awaiter = ReadyResumeAwaiter<promise_type>;
#else
    using final_awaiter = ResumeAwaiter<promise_type>;
#endif

    final_awaiter final_suspend() noexcept {
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
  SyncTask(std::coroutine_handle<promise_type> h) : handle_(h) {
    // LOG_FUNCTION();
  }
  ~SyncTask() {
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

#ifdef SYMMETRIC
    bool
#else
    void
#endif
    await_suspend(std::coroutine_handle<> task_handle) noexcept {
      // LOG_FUNCTION();
      auto &promise = handle_.promise();
      promise.task_handle_ = task_handle;
#ifdef SYMMETRIC_EXTRA
      LOG_MSG("[D1] save task's: {:#010x}, submit single task's: {:#010x} to "
              "worker",
              short_addr(task_handle), short_addr(handle_));
      // Instead of resuming the single task synchronously on this thread, hand
      // it to the worker thread. The single task's final awaiter now runs on
      // the worker thread and races with this awaiter (main thread) over done_:
      // the single task may finish before or after our exchange below, on a
      // different thread.
      g_worker.submit([single_task_handle = handle_] {
        LOG_MSG("[Worker] resume single task");
        TRACK_RESUME(single_task_handle.resume());
        // Publish that the worker is done touching this frame; the awaiter
        // needs this before the task may destroy the frame.
        single_task_handle.promise().worker_finished_.store(
            true, std::memory_order_release);
        LOG_MSG("[Worker] set worker finished");
      });

      // Widen the race window: briefly poll done_ to give the worker thread a
      // chance to finish the single task (and run its final awaiter's
      // done_.exchange) while we are still here. Whichever of the two
      // exchanges below happens first is a genuine race between the main
      // thread and the worker thread. The 50 is tuned so that, on this
      // machine, both orderings actually show up in the output.
      int i = 0;
      for (; i < 50 && !promise.done_.load(std::memory_order_acquire); ++i) {
        std::this_thread::yield();
      }

      bool done = promise.done_.exchange(true, std::memory_order_acq_rel);
      LOG_MSG("yield {} times, already done: {}, no task suspend, and vice "
              "versa, end",
              i, done);
      if (!done) {
        // We exchanged first: the single task is still running on the worker
        // thread, so suspend; the final awaiter will hand us back to the worker
        // as a fresh task once the single task is done.
        return true;
      }
      // The worker's final awaiter exchanged first: the single task is already
      // done. Wait until the worker has fully returned from resume() before
      // continuing, so that destroying the frame at the end of this iteration
      // is safe.
      while (!promise.worker_finished_.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      return false;
#else
      LOG_MSG("[D1] save task's: {:#010x}, resume single task's: {:#010x} "
              "immediately",
              short_addr(task_handle), short_addr(handle_));
      TRACK_RESUME(handle_.resume());
#ifdef SYMMETRIC
      bool done = promise.done_.exchange(true, std::memory_order_acq_rel);
      LOG_MSG("already done: {}, no task suspend, and vice versa, end", done);
      return !done;
#else
      LOG_MSG("task suspend, end");
#endif
#endif
    }
    void await_resume() const noexcept { LOG_FUNCTION(); }
  };

  awaiter operator co_await() const noexcept { return awaiter{handle_}; }
};

SyncTask make_single_task() {
  return make_single_task_impl<SyncTask>(FUNCTION);
}

Task make_task(int count) {
  return make_task_impl<SyncTask>(FUNCTION, count, &make_single_task);
}

int main() {

#ifdef SYMMETRIC_EXTRA
  g_worker.start();
#endif

  LOG_MSG("make task");
  auto task = make_task(3);

  LOG_MSG("resume task");
  TRACK_RESUME(task.handle_.resume());

#ifdef SYMMETRIC_EXTRA
  // The task is now either suspended at its first co_await or already
  // finished; from now on the worker thread may safely resume it.
  g_task_suspended.store(true, std::memory_order_release);

  // The task may keep running on the worker thread (it is resumed there
  // by the single tasks' final awaiter), so wait until it has finished before
  // stopping the worker, otherwise Worker::stop() would discard the remaining
  // queued tasks and the task would never complete.
  while (!g_task_finished.load(std::memory_order_acquire)) {
    std::this_thread::yield();
  }
  g_worker.stop();
  // Worker::stop() only signals the worker thread; it does not wait for it.
  // The worker may still be unwinding `resume()` of the task (submitted by
  // the single task's final awaiter) after the task has finished, so join the
  // worker thread before returning from main: destroying the task's frame
  // (via `task`'s destructor) while the worker is still inside that resume()
  // would be a use-after-free.
  g_worker.join();
#endif

  return 0;
}

// clang-format off
/*
symmetric_resume
    main
        make task
            promise
            task
        resume task
            make single task
                single tasks's primise
                single task
            await single task
                awaiter suspend single task
                resume single task immediately
                    final awaiter suspend single task
                        resume task
                            destroy the single task(await suspend finished later)
                            make another single task ...

symmetric_resume_ready
    main
        make task
        resume task
            make single task
            await single task
                awaiter suspend single task
                resume single task
                    final awaiter suspend single task
                        done here(actually no chance done before, but only find done later)
                done, so not suspend
            destroy the single tasks
            make another single task ...

symmetric_resume_ready_atomic
    main
        make task
        resume task
            make single task
            await single task
                awaiter suspend single task
                [worker]
                    resume single task
                        final awaiter suspend single task
                            A if not done in [main], done here, later find done in [main]
                            B else done before in [main]
                                wait g_task_suspended
                                [worker's another] resume the task  -->
                    worker_finished_ set
                [main]
                    A if done already in [worker]
                        wait worker_finished_, awaid the worker in a destroyed coroutine
                        false suspend  -->
                    B else not done before in [worker], done here, later find done in [worker]
                        true suspend
            --> await resume point of the single task
            destroy the single tasks
            make another single task ...

            g_task_finished set
        g_task_suspended set(for B: true suspend in [main] and resume in [worker])
        wait g_task_finished(for g_worker stop and join)
 */
