#pragma once

#include <stdint.h>

#include "three_states_lock_t.h"

struct pthread_mutex_t {
  uint32_t lock_; // three_states_lock_t
  // unsigned int count_;  // recursion depth (kind == RECURSIVE)
  // int owner_;           // TID of the current owner
  // unsigned int nusers_; // references; destroy() must see 0 first
  // int kind_;            // NORMAL/RECURSIVE/ERRORCHECK/ROBUST/PI...
  // int spins_;           // adaptive spin count before hitting the kernel
  // struct {
  //   void *prev_;
  //   void *next_;
  // } list_; // wait queue (futex requeue / wait-morphing)
};
typedef struct pthread_mutex_t pthread_mutex_t;

static inline void pthread_mutex_init(pthread_mutex_t *m) {
  m->lock_ = three_states_unlocked;
}

static inline void pthread_mutex_lock(pthread_mutex_t *m) {
  three_states_lock(&m->lock_);
}

static inline void pthread_mutex_unlock(pthread_mutex_t *m) {
  three_states_unlock(&m->lock_);
}

static inline int pthread_mutex_trylock(pthread_mutex_t *m) {
  uint32_t s = three_states_unlocked;
  return atomic_compare_exchange_strong_acquire(&m->lock_, &s, three_states_locked_no_waiter);
}
