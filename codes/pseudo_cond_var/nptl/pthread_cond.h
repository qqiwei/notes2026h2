#pragma once

#include <limits.h>
#include <stdint.h>

#include "../kernel/syscall.h"
#include "pthread_mutex.h"
#include "three_states_lock_t.h"

// same as glibc struct __pthread_cond_s (48 bytes)
struct pthread_cond_t {
  uint64_t wseq;         // waiter sequence counter, LSB == current G2 index
  uint64_t g1_start;     // start position of G1 (inclusive), LSB == G2 index
  uint32_t g_refs[2];    // futex-waiter refs per group, LSB == wake-request
  uint32_t g_size[2];    // waiters to be waken, decreased by signal
  uint32_t g1_orig_size; // g1 original(before signal) size, LSBs == inner lock
  uint32_t wrefs;
  uint32_t g_signals[2]; // signals per group (unit 2), LSB == closed flag
};
typedef struct pthread_cond_t pthread_cond_t;

#define _GroupBit (1u)
#define _SeqStep (2u)
#define _GAnother(g) ((g) ^ _GroupBit)
#define _G2(wseq) ((uint32_t)((wseq) & _GroupBit))
#define _Seq(wseq) ((wseq) >> _GroupBit)

#define _ClosedBit (1u)   // g_signals LSB: group closed (fully signaled)
#define _SignalsStep (2u) // g_signals: one signal == 2 units

#define _GRefsStep (2u) // g_refs: one futex-waiter reference == 2 units
#define _GRefsWake (1u) // g_refs LSB: wake-request flag

#define _DestroyingBit (4u)
#define _WaitersRefStep (8u)
#define _WaitersRef(wrefs) ((wrefs) >> 3u)

// protect g1_size[2] and g1_orig_size
#define _OrigSizeStep (2u)
#define _InnerLock() (three_states_lock(&cond->g1_orig_size))
#define _InnerUnlock() (three_states_unlock(&cond->g1_orig_size))

static inline int pthread_cond_init(pthread_cond_t *cond) {
  cond->wseq = 0;
  cond->g1_start = 0;
  cond->g_refs[0] = 0;
  cond->g_refs[1] = 0;
  cond->g_size[0] = 0;
  cond->g_size[1] = 0;
  cond->g1_orig_size = 0;
  cond->wrefs = 0;
  cond->g_signals[0] = 0;
  cond->g_signals[1] = 0;
  return 0;
}

static inline int pthread_cond_destroy(pthread_cond_t *cond) {
  // set destroying
  uint32_t wrefs = atomic_fetch_or_acquire(&cond->wrefs, _DestroyingBit);
  wrefs |= _DestroyingBit;

  // wait waiters all gone
  while (_WaitersRef(wrefs) != 0) {
    futex_syscall(SYS_futex, &cond->wrefs, FUTEX_WAIT, wrefs);
    wrefs = atomic_load_acquire(&cond->wrefs);
  }

  return 0;
}

// a waiter leaving the blocking path: drop its group reference, and wake
// the signaler quiescing the group if this was the last reference
static inline void dec_grefs(pthread_cond_t *cond, uint32_t g) {
  if (atomic_fetch_add_release(&cond->g_refs[g], -_GRefsStep) ==
      (_GRefsStep + _GRefsWake)) {
    atomic_fetch_and_relaxed(&cond->g_refs[g], ~_GRefsWake);
    futex_syscall(SYS_futex, &cond->g_refs[g], FUTEX_WAKE, INT_MAX);
  }
}

// same as glibc __pthread_cond_wait_common (4 stages)
static inline void pthread_cond_wait(pthread_cond_t *cond,
                                     pthread_mutex_t *mutex) {
  // stage 1: ++seq, g
  uint64_t wseq = atomic_fetch_add_acquire(&cond->wseq, _SeqStep);
  uint64_t seq = _Seq(wseq);
  uint32_t g = _G2(wseq);

  // stage 2: ++wrefs, for destroying check and fast path check of
  // signal/broadcast
  atomic_fetch_add_release(&cond->wrefs, _WaitersRefStep);

  // stage 3
  pthread_mutex_unlock(mutex);

  // stage 4: consume signals or block and later waken in kernel
  uint32_t signals = atomic_load_acquire(&cond->g_signals[g]);
  do {
    while (1) {
      // in a closed (fully signaled) group
      if ((signals & _ClosedBit) != 0u)
        goto done;

      // a signal is available
      if (signals != 0u)
        break;

      // no signals: take a group reference and block on the group futex
      atomic_fetch_add_acquire(&cond->g_refs[g], _GRefsStep);
      if (((atomic_load_acquire(&cond->g_signals[g]) & _ClosedBit) != 0u) ||
          (seq < (atomic_load_relaxed(&cond->g1_start) >> 1))) {
        dec_grefs(cond, g);
        goto done;
      }

      futex_syscall(SYS_futex, &cond->g_signals[g], FUTEX_WAIT,
                    0u); // suspend and later waken
      dec_grefs(cond, g);

      signals = atomic_load_acquire(&cond->g_signals[g]);
    }
  } while (!atomic_compare_exchange_weak_acquire(&cond->g_signals[g], &signals,
                                                 signals - _SignalsStep));

  // we consumed a signal, but we may have stolen it from a newer group that
  // reuses our group slot; repair conservatively
  if (uint64_t g1_start = atomic_load_relaxed(&cond->g1_start); // cpp
      seq < (g1_start >> 1)) {
    // our group is closed; if the current G1 uses our slot, we may have
    // stolen from it
    if (_GAnother(_G2(g1_start)) == g) {
      uint32_t s = atomic_load_relaxed(&cond->g_signals[g]);
      while (atomic_load_relaxed(&cond->g1_start) == g1_start) {
        // add a signal back; or, if the group is being closed, just add a
        // wake-up (a futex waiter may still wait for the signal we stole)
        if (((s & _ClosedBit) != 0u) ||
            atomic_compare_exchange_weak_relaxed(&cond->g_signals[g], &s,
                                                 s + _SignalsStep)) {
          futex_syscall(SYS_futex, &cond->g_signals[g], FUTEX_WAKE, 1);
          break;
        }
      }
    }
  }

done:
  // stage 5: --wrefs
  {
    uint32_t old = atomic_fetch_sub_release(&cond->wrefs, _WaitersRefStep);
    old -= _WaitersRefStep;

    // destroying and last waiter gone, so destroy
    if ((old & _DestroyingBit) != 0u && _WaitersRef(old) == 0u)
      futex_syscall(SYS_futex, &cond->wrefs, FUTEX_WAKE, INT_MAX);
  }

  pthread_mutex_lock(mutex);
}

// close G1 (set its closed flag), wait for all futex waiters to leave G1,
// then switch group roles: the former G2 becomes the new G1 and the old G1
// becomes a fresh G2. Returns 1 iff groups were switched and the new G1 has
// waiters. SEQ is a recent observation of wseq >> 1 made by the signaler.
static inline int quiesce_and_switch_g1(pthread_cond_t *cond, uint64_t seq,
                                        uint32_t *g1) {
  uint32_t g = *g1;

  // if G2 is empty, do not switch (g_size may hold a negative value)
  uint32_t old_orig_size =
      atomic_load_relaxed(&cond->g1_orig_size) >> _OrigSizeStep;
  uint64_t old_g1_start = atomic_load_relaxed(&cond->g1_start) >> 1;
  if (((uint32_t)(seq - old_g1_start - old_orig_size) +
       cond->g_size[g ^ _GroupBit]) == 0u)
    return 0;

  // close G1: set the closed flag on its g_signals as advance notice of the
  // upcoming g1_start change; waiters about to block leave instead
  atomic_fetch_or_relaxed(&cond->g_signals[g], _ClosedBit);

  // quiesce: wait until no waiter holds a group reference anymore
  uint32_t r = atomic_fetch_or_release(&cond->g_refs[g], 0u);
  while ((r >> 1) != 0u) {
    r = atomic_fetch_or_relaxed(&cond->g_refs[g], _GRefsWake) | _GRefsWake;
    if ((r >> 1) != 0u)
      futex_syscall(SYS_futex, &cond->g_refs[g], FUTEX_WAIT, r);
    r = atomic_load_relaxed(&cond->g_refs[g]);
  }
  atomic_thread_fence_acquire(); // sync with the last dec_grefs

  // advance g1_start, which finishes closing G1. The LSB of g1_start tracks
  // the G2 index, so it is adjusted by ±1. Relaxed MO is fine.
  atomic_fetch_add_relaxed(&cond->g1_start,
                           (old_orig_size << 1) + (g == 1u ? 1 : -1));

  // reopen the slot as a fresh G2, enabling waiters to block on it again
  atomic_store_release(&cond->g_signals[g], 0u);

  // publish the group switch by flipping the G2 index bit in wseq
  seq = atomic_fetch_xor_release(&cond->wseq, _GroupBit) >> 1;
  *g1 = g ^ _GroupBit;

  uint32_t orig_size = (uint32_t)(seq - old_g1_start - old_orig_size);
  cond->g_size[*g1] += orig_size;

  old_orig_size = atomic_load_relaxed(&cond->g1_orig_size);
  orig_size <<= _OrigSizeStep;
  while (!atomic_compare_exchange_weak_relaxed(
      &cond->g1_orig_size, &old_orig_size,
      orig_size | (old_orig_size & three_states_mask))) {
  }

  return cond->g_size[*g1] != 0u;
}

static inline void pthread_cond_signal(pthread_cond_t *cond) {
  // fast path
  if (_WaitersRef(atomic_load_acquire(&cond->wrefs)) == 0)
    return;

  _InnerLock();

  uint64_t wseq = atomic_load_acquire(&cond->wseq);
  uint32_t g1 = _GAnother(_G2(wseq));
  wseq >>= 1;

  int has_waiters = 0;
  // G1 still has waiters, or G2 has waiters(close G1 and switch first)
  if (cond->g_size[g1] != 0u || quiesce_and_switch_g1(cond, wseq, &g1)) {
    // signals once
    atomic_fetch_add_relaxed(&cond->g_signals[g1], _SignalsStep);
    cond->g_size[g1]--;
    has_waiters = 1;
  }

  _InnerUnlock();

  if (has_waiters)
    futex_syscall(SYS_futex, &cond->g_signals[g1], FUTEX_WAKE, 1);
}

static inline void pthread_cond_broadcast(pthread_cond_t *cond) {
  // fast path
  if (_WaitersRef(atomic_load_acquire(&cond->wrefs)) == 0)
    return;

  _InnerLock();

  uint64_t wseq = atomic_load_acquire(&cond->wseq);
  uint32_t g2 = _G2(wseq);
  uint32_t g1 = _GAnother(g2);
  wseq >>= 1;

  int wake_g2 = 0;

  // signal all waiters remaining in G1, and wake them before closing G1
  if (cond->g_size[g1] != 0u) {
    atomic_fetch_add_relaxed(&cond->g_signals[g1], cond->g_size[g1] << 1);
    cond->g_size[g1] = 0u;
    futex_syscall(SYS_futex, &cond->g_signals[g1], FUTEX_WAKE, INT_MAX);
  }

  // close G1, switch groups, and signal all waiters in the new G1
  if (quiesce_and_switch_g1(cond, wseq, &g1)) {
    atomic_fetch_add_relaxed(&cond->g_signals[g1], cond->g_size[g1] << 1);
    cond->g_size[g1] = 0u;
    wake_g2 = 1;
  }

  _InnerUnlock();

  if (wake_g2)
    futex_syscall(SYS_futex, &cond->g_signals[g1], FUTEX_WAKE, INT_MAX);
}
