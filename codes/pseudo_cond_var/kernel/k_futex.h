// ============================================================================
// [Layer 1] futex KERNEL simulation -- isolated, see syscall.h for the boundary
//
//   std::condition_variable -> pthread_cond_t -> futex : bottom of the chain.
//   This file implements the kernel side only, one operation per futex word:
//     kernel_futex_wait() / kernel_futex_wake() <- kernel (local teaching sim)
//
//   No userspace layer includes this file, and nothing reaches into it from
//   the userspace build: kernel/k_futex.h is a fully isolated world. The
//   syscall boundary (kernel/syscall.h) deliberately does NOT include this
//   header and only defines EMPTY stubs for kernel_futex_wait/kernel_futex_wake
//   -- a real futex operation would hand the syscall NUMBER across the trap
//   boundary and only the kernel (this file) knows what to do with it.
//
//   [dependencies] k_futex.h itself carries NO external includes: everything it
//   needs (kernel types u32/ulong, NULL, EWOULDBLOCK, atomics, and the EMPTY
//   k_* scheduler primitives) comes from the single sibling header k_sched.h.
//   The kernel world must not depend on userspace <pthread.h> -- its scheduler
//   is its own.
//
//   [futex core semantics] (atomicity guaranteed by the kernel)
//     futex_wait(addr, val) -> sleep if *addr == val; otherwise return at once
//     futex_wake(addr, n)   -> wake at most n threads blocked on addr
//
//   [teaching trade-off]
//     Only PRIVATE semantics kept: the futex word is a virtual address within
//     the same process. Cross-process futex_key/inode translation is removed,
//     so match_futex degenerates to "address equality".
// ============================================================================
#pragma once

#include "k_sched.h"

// ============================================================================
// Kernel scheduler simulation (stands in for the runqueue / wake_up_process)
//
//   In the real kernel the scheduler lives strictly BELOW futex: a waiter
//   blocks by calling schedule(), a waker reschedules by calling
//   wake_up_process(). The futex code never touches a "condition variable" --
//   that is a userspace concept, invisible to the kernel.
//
//   A teaching sim must nevertheless actually suspend an OS thread, so every
//   simulated task parks a private lock+cond inside itself as its "runqueue".
//   Those primitives are scheduler internals only: futex_q carries a
//   task_struct and hands it to the two scheduler entry points, and never
//   references pthread primitives directly. The k_* names used below are the
//   EMPTY stand-ins supplied by k_sched.h (the kernel owns its scheduler --
//   it never calls the real userspace pthread).
// ============================================================================

// Per-task state (kernel: <linux/sched.h> TASK_*)
enum { TASK_RUNNING = 0, TASK_INTERRUPTIBLE = 1 };

// Minimal struct task_struct -- teaching stand-in for the kernel's task
// descriptor. wake_lock / wake_cv are scheduler internals: the primitive that
// really blocks / resumes the OS thread behind this task (the simulated
// runqueue). Futex sees only state + wake_pending through the API below.
struct task_struct {
  int state;              // TASK_* : per-task state (kernel: task->__state)
  int wake_pending;       // wake_up_process() delivered (kernel: runnable)
  k_mutex_t wake_lock;    // scheduler-internal "runqueue": a private
  k_cond_t wake_cv;       // lock+cond that really suspends this thread
};
typedef struct task_struct task_struct;

// The hash-bucket spinlock, named as the kernel calls it. The real kernel
// spinlock_t is a raw arch word grabbed with a lock-prefixed xchg/CAS; ours is
// the plain-C equivalent: one u32 won with a weak CAS, released with a store.
// Only suitable for very short critical sections (the bucket lock).
struct spinlock {
  u32 lock; // 0 = free, 1 = held
};
typedef struct spinlock spinlock_t;

static inline void spin_lock(spinlock_t *l) {
  for (;;) {
    u32 expected = 0u; // free == exactly 0
    if (k_compare_exchange_weak_acquire(&l->lock, &expected, 1u))
      return;
  }
}

static inline void spin_unlock(spinlock_t *l) {
  k_store_release(&l->lock, 0u);
}

// set_current_state: record the task's new state (kernel: smp_store_mb).
static inline void set_current_state(task_struct *t, int state) {
  t->state = state;
}

// schedule: block until wake_up_process() marks the task runnable again.
//   Real kernel: pick the next task from the runqueue (context switch).
//   Teaching sim: wait on this task's private cond -- the simulated runqueue.
static inline void schedule(task_struct *t) {
  k_mutex_lock(&t->wake_lock);
  while (!t->wake_pending)
    k_cond_wait(&t->wake_cv, &t->wake_lock);
  t->wake_pending = 0;
  t->state = TASK_RUNNING;
  k_mutex_unlock(&t->wake_lock);
}

// wake_up_process: mark a sleeping task runnable so its schedule() returns.
static inline void wake_up_process(task_struct *t) {
  k_mutex_lock(&t->wake_lock);
  t->wake_pending = 1;
  k_cond_signal(&t->wake_cv);
  k_mutex_unlock(&t->wake_lock);
}

// ============================================================================
// The userspace teaching simulation of the kernel futex
//   (corresponds to Linux kernel/futex/waitwake.c)
//
//   Kernel principle: each futex word maps to a hash bucket holding a
//   spinlock + wait queue. The wait queue is a list of futex_q nodes; each
//   node references the sleeping task (q->task), never an userspace condvar.
//   All kernel data structures here are simulated with plain C; the only
//   k_* scheduler primitives anywhere are hidden inside the scheduler above.
// ============================================================================

// ---- Wait node: corresponds to kernel struct futex_q ----
struct futex_q {
  futex_q *next;          // plist node in the hash bucket wait queue
  const u32 *uaddr;       // teaching: union futex_key (address == key)
  spinlock_t *lock_ptr;   // bucket lock while queued; NULL once dequeued
  task_struct task;       // the waiting task (kernel: q->task = current)
};
typedef struct futex_q futex_q;

// ---- Hash bucket: corresponds to kernel struct futex_hash_bucket ----
struct futex_hash_bucket {
  spinlock_t lock;  // kernel spinlock_t (guards the queue)
  futex_q *chain;   // wait queue (corresponds to kernel plist_head)
  int waiters; // waiter count in the bucket (kernel atomic_t); accessed via
               // the k_* atomic macros in k_sched.h -- never std::atomic
};
typedef struct futex_hash_bucket futex_hash_bucket;

// Fixed bucket count, globally statically allocated (kernel hashes physical
// addresses; teaching demo hashes virtual addresses)
enum { N_BUCKETS = 16 };
static futex_hash_bucket buckets[N_BUCKETS];

// Find the bucket: the kernel does get_futex_key -> hash_futex; under PRIVATE
// semantics just hash the address directly
static futex_hash_bucket *hash_futex(const u32 *uaddr) {
  ulong a = (ulong)uaddr;
  return &buckets[(a >> 2) % N_BUCKETS];
}

// ============================================================================
// Kernel futex_wait: compare + atomically sleep
//
// [Why must the kernel provide the "atomicity"?]
//   No matter how userspace is arranged, between "check *addr == val" and
//   "initiate sleep" there is always a gap where the scheduler can preempt
//   (TOCTOU). The kernel fuses "check + enqueue" into one critical section
//   guarded by the hash bucket lock -- before the lock is acquired, no
//   futex_wake can get in, so once the check passes and we enqueue, a later
//   wakeup can never be missed.
// ============================================================================
static inline int kernel_futex_wait(u32 *uaddr, u32 val) {
  futex_q q;
  q.uaddr = uaddr;
  q.next = NULL;
  q.lock_ptr = NULL;
  q.task.state = TASK_RUNNING;
  q.task.wake_pending = 0;
  k_mutex_init(&q.task.wake_lock);
  k_cond_init(&q.task.wake_cv);

  futex_hash_bucket *hb = hash_futex(uaddr);

  // 1. Take the hash bucket lock (kernel: spin_lock)
  spin_lock(&hb->lock);

  // 2. [Key] Re-check *uaddr == val under the lock
  //    Not equal -> someone already signaled after userspace released the
  //    user lock but before entering the kernel; must NOT sleep! Release the
  //    lock, return EWOULDBLOCK, and userspace re-tests the condition.
  if (k_load_acquire(uaddr) != val) {
    spin_unlock(&hb->lock);
    // Never enqueued: release the per-node resources initialized above
    k_cond_destroy(&q.task.wake_cv);
    k_mutex_destroy(&q.task.wake_lock);
    return -EWOULDBLOCK;
  }

  // 3. Enqueue + mark interruptible (kernel: plist_add + waiters++,
  //    set_current_state(TASK_INTERRUPTIBLE)); lock_ptr records which bucket
  //    we are queued on, NULL once the waker dequeues us.
  q.lock_ptr = &hb->lock;
  q.task.state = TASK_INTERRUPTIBLE;
  q.next = hb->chain;
  hb->chain = &q;
  k_fetch_add_relaxed(&hb->waiters, 1);

  // 4. Only sleep after releasing the lock (kernel: spin_unlock then schedule)
  spin_unlock(&hb->lock);
  schedule(&q.task); // suspend until wake_up_process() -- not a condvar

  // 5. Woken: the waker already dequeued us (lock_ptr == NULL) and marked us
  //    runnable; clean up and return
  k_cond_destroy(&q.task.wake_cv);
  k_mutex_destroy(&q.task.wake_lock);
  return 0;
}

// ============================================================================
// Kernel futex_wake: wake at most n threads blocked on uaddr
// ============================================================================
static inline int kernel_futex_wake(u32 *uaddr, int n) {
  futex_hash_bucket *hb = hash_futex(uaddr);
  int ret = 0;

  // 1. Fast path: nobody waiting in the bucket -> no need to take the lock
  //    The waiter count is incremented before enqueue (with barriers), so a
  //    stale miscount only costs an extra lock traversal.
  if (k_load_relaxed(&hb->waiters) == 0)
    return 0;

  // 2. Take the lock, walk the wait queue (kernel: spin_lock + plist_for_each)
  spin_lock(&hb->lock);
  futex_q **pp = &hb->chain;
  while (*pp != NULL && ret < n) {
    futex_q *q = *pp;

    // 3. match_futex: only wake nodes sleeping on the SAME futex word
    //    Under PRIVATE semantics this is virtual-address equality (only
    //    cross-process mode needs inode translation).
    if (q->uaddr != uaddr) {
      pp = &q->next;
      continue;
    }

    // 4. Dequeue (kernel: plist_del + waiters--) and clear lock_ptr so the
    //    waiter knows it is no longer queued
    *pp = q->next;
    q->lock_ptr = NULL;
    k_fetch_sub_relaxed(&hb->waiters, 1);

    // 5. Wake (kernel: wake_up_process puts it on the runnable queue)
    //    The task's schedule() then returns and the waiter re-runs.
    wake_up_process(&q->task);

    ret++;
  }
  // 6. Release the lock
  spin_unlock(&hb->lock);
  return ret;
}

// ============================================================================
// [Advanced: FUTEX_CMP_REQUEUE / Wait Morphing (not implemented, explained)]
//   The ultimate broadcast: wake 1 and cut-paste the rest directly into the
//   mutex wait queue inside the kernel, avoiding thundering herd. The version
//   number (val3) must be compared before transferring to prevent queue
//   jumping -- that is the purpose of CMP; on failure fall back to the
//   FUTEX_WAKE thundering herd.
// ============================================================================

// ----------------------------------------------------------------------------
// [Removed] The glibc-side syscall entry used to live here (futex_wait /
// futex_wake). Userspace must never reach these kernel handlers directly: the
// syscall boundary in syscall.h now owns the syscall NUMBER (SYS_futex) and
// dispatches to kernel_futex_wait / kernel_futex_wake. On the userspace side
// those two routines are EMPTY stubs (see syscall.h) -- this file's real
// implementations are unreachable from any userspace build.
// ----------------------------------------------------------------------------

// ----------------------------------------------------------------------------
// [Removed] This file used to include <errno.h> <pthread.h> <stddef.h>
// <stdint.h> and ../atomic.h for the symbols above. Those dependencies moved
// into the sibling header k_sched.h, which provides them in kernel-native form
// (u32/ulong types, kernel NULL/EWOULDBLOCK, k_* atomic macros over compiler
// builtins, and the k_* scheduler primitives as EMPTY stand-ins) -- so the
// kernel simulation itself carries no libc and no userspace data structures.
// ----------------------------------------------------------------------------