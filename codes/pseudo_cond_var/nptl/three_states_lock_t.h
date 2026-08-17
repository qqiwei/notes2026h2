#pragma once

#include <stdint.h>

#include "../kernel/syscall.h"
#include "atomic.h"

enum {
  three_states_unlocked = 0u,
  three_states_locked_no_waiter = 1u,
  three_states_locked_waiters = 2u,
  three_states_mask = 3u,
};

#define _State(v) ((v) & three_states_mask)
#define _ClearState(v) ((v) & ~three_states_mask)

static inline void three_states_lock(uint32_t *word) {
  uint32_t expected = atomic_load_relaxed(word);

  // fast path
  while (_State(expected) == three_states_unlocked) {
    if (atomic_compare_exchange_weak_acquire(
            word, &expected, expected | three_states_locked_no_waiter)) {
      return;
    }
  }

  // slow path
  while (1) {
    if (_State(expected) != three_states_locked_waiters) {
      uint32_t desired = _ClearState(expected) | three_states_locked_waiters;
      uint32_t old_state = _State(expected);
      if (atomic_compare_exchange_weak_acquire(word, &expected, desired)) {
        if (old_state == three_states_unlocked) {
          return;
        }
        expected = desired;
      } else {
        if (_State(expected) == three_states_unlocked) {
          continue;
        }
      }
    }

    futex_syscall(SYS_futex, word, FUTEX_WAIT, expected);

    // wakeup, or spurious wakeup, or timeout
    expected = atomic_load_relaxed(word);
  }
}

static inline void three_states_unlock(uint32_t *word) {
  uint32_t old = atomic_fetch_and_release(word, ~three_states_mask);
  if (_State(old) == three_states_locked_waiters) {
    futex_syscall(SYS_futex, word, FUTEX_WAKE, 1);
  }
}
