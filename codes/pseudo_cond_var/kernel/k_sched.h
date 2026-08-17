// ============================================================================
// [Layer 1] kernel base types + scheduler primitives -- k_futex.h's single
//           dependency (kernel's "linux/kernel-sched.h" prelude)
//
//   RULE: a k_* file simulates kernel code, so it must NOT pull in anything
//   non-kernel: no <errno.h> / <stddef.h> / <stdint.h>, no libc, no userspace
//   pthread data structures. Every symbol k_futex.h needs is re-provided HERE
//   in kernel-native form:
//
//     - kernel integer types (kernel: linux/types.h)   -> u32, ulong
//     - kernel NULL / errno constants                  -> kernel linux/errno.h
//     - kernel atomics as k_* macros over compiler builtins (no userspace
//       atomic.h, no libc; a real kernel ships its own READ_ONCE /
//       smp_load_acquire / cmpxchg equivalents)
//     - scheduler lock+cond as k_* kernel primitives (the kernel's runqueue /
//       wait-queue, EMPTY implementations -- the real suspend/resume machinery
//       is out of scope for a teaching demo)
// ============================================================================
#pragma once

// ---- kernel integer types (kernel: linux/types.h) ----
typedef unsigned int u32;             // the futex word (kernel: __u32 / u32)

// Address-sized integer the kernel uses for virtual addresses (kernel just
// writes `unsigned long`). Teaching stand-in for userspace uintptr_t.
typedef unsigned long ulong;

// ---- kernel NULL (kernel: linux/stddef.h) ----
#define NULL ((void *)0)

// ---- kernel errno (kernel: linux/errno.h) ----
// Linux: EWOULDBLOCK == EAGAIN == 11 (the "try again, don't sleep" return from
// futex_wait when the value changed before the sleep).
#define EWOULDBLOCK 11

// ----------------------------------------------------------------------------
// Kernel atomics -- k_* replacements for the userspace nptl/atomic.h macros.
//
//   A real kernel implements memory-model atomics itself (READ_ONCE /
//   WRITE_ONCE / smp_load_acquire / smp_store_release / cmpxchg, built on
//   compiler builtins or arch asm). The teaching kernel does the same here:
//   straight macros over the compiler's __atomic_* builtins on plain integers
//   -- no shared header, no libc, no data structure.
// ----------------------------------------------------------------------------
#define k_load_relaxed(ptr) __atomic_load_n((ptr), __ATOMIC_RELAXED)
#define k_load_acquire(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#define k_store_release(ptr, val) __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)
#define k_fetch_add_relaxed(ptr, val) __atomic_fetch_add((ptr), (val), __ATOMIC_RELAXED)
#define k_fetch_sub_relaxed(ptr, val) __atomic_fetch_sub((ptr), (val), __ATOMIC_RELAXED)
#define k_compare_exchange_weak_acquire(ptr, expected, desired)                \
  __atomic_compare_exchange_n((ptr), (expected), (desired), 1,                 \
                              __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)

// ----------------------------------------------------------------------------
// Kernel scheduler lock+cond -- the k_* replacements for the userspace
// pthread_* the earlier sim used. In the real kernel these are the runqueue /
// wait-queue internals that really park a task; here they are EMPTY stand-ins
// (names kept so task_struct / schedule / wake_up_process read like the sim).
// ----------------------------------------------------------------------------
typedef struct { int _dummy; } k_mutex_t;
typedef struct { int _dummy; } k_cond_t;

static inline void k_mutex_init(k_mutex_t *m) { (void)m; }
static inline void k_mutex_lock(k_mutex_t *m) { (void)m; }
static inline void k_mutex_unlock(k_mutex_t *m) { (void)m; }
static inline void k_mutex_destroy(k_mutex_t *m) { (void)m; }

static inline void k_cond_init(k_cond_t *c) { (void)c; }
static inline void k_cond_wait(k_cond_t *c, k_mutex_t *m) {
  (void)c;
  (void)m;
}
static inline void k_cond_signal(k_cond_t *c) { (void)c; }
static inline void k_cond_destroy(k_cond_t *c) { (void)c; }