#pragma once

#include <atomic>
#include <print>
#include <source_location>

inline int get_tid() {
  static std::atomic<int> g_thread_counter{0};
  thread_local int my_tid = ++g_thread_counter;
  return my_tid;
}

inline thread_local int g_stack_depth = 0;

#define FUNCTION std::source_location::current().function_name()

#define TID_INDENT_PH "{:02} | {:>{}}"
#define TID_INDENT get_tid(), "", g_stack_depth * 2
#define FUNC_PH "{}"
#define MSG_INTERVAL_PH " --> "
#define LINE_PH " {}"
#define LINE "-----------------------------------------------------------------"

#define LOG_MSG_F(func, fmt, ...)                                              \
  do {                                                                         \
    constexpr auto &_log_fmt = fmt;                                            \
    static_assert(sizeof(_log_fmt) > 1, "LOG_MSG fmt must not be empty");      \
    std::println(TID_INDENT_PH FUNC_PH MSG_INTERVAL_PH fmt, TID_INDENT,        \
                 func __VA_OPT__(, ) __VA_ARGS__);                             \
  } while (0)
#define LOG_MSG(...) LOG_MSG_F(FUNCTION, __VA_ARGS__)

#define LOG_F(func) std::println(TID_INDENT_PH FUNC_PH, TID_INDENT, func)
#define LOG_FUNCTION() LOG_F(FUNCTION)

#define LOG_LINE()                                                             \
  std::println(TID_INDENT_PH LINE LINE_PH, TID_INDENT, g_stack_depth)

struct LineTracker {
  LineTracker() { LOG_LINE(); }
  ~LineTracker() { LOG_LINE(); }
};

struct StackFrameTracker {
  StackFrameTracker() { ++g_stack_depth; }
  ~StackFrameTracker() { --g_stack_depth; }
};

#define TRACK_RESUME(resume_expr)                                              \
  do {                                                                         \
    StackFrameTracker _tracker;                                                \
    LineTracker _line_tracker;                                                 \
    resume_expr;                                                               \
  } while (0)
