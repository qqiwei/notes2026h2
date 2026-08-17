// ============================================================================
// [Layer 1] the syscall boundary - the USERSIDE (glibc) view of the kernel
//
//   Userspace (three_states_lock_t.h, pthread_cond.h, ...) knows the kernel only through
//   this file: a syscall NUMBER plus the futex op codes. It never sees the
//   kernel simulation (k_futex.h).
//
//   [Full kernel/user isolation]
//     k_futex.h is deliberately NOT included here (see the commented-out line
//     below). The kernel simulation is a separate world of its own, never
//     reachable from userspace: the two handlers userspace can "call" are only
//     empty stubs standing in for the real kernel routines. A syscall from the
//     userspace side is nothing more than a NUMBER handed across the boundary.
//
//   Inside futex_syscall(), the switch on `nr` is the syscall-table lookup
//   (kernel: arch/x86/entry/syscalls sys_call_table) that routes the number to
//   the real handler; the switch on `futex_op` mirrors FUTEX_WAIT / FUTEX_WAKE
//   dispatch in kernel/futex/waitwake.c.
// ============================================================================
#pragma once

#include <errno.h>
#include <stdint.h>

// #include "k_futex.h" // [isolated] the kernel simulation is NOT visible here

// The futex syscall number (Linux x86-64: __NR_futex == 202). The number is
// the only contract between userspace and the kernel simulation below.
enum { SYS_futex = 202 };

// Futex operation codes -- the first argument of the futex syscall
// (Linux <linux/futex.h>: FUTEX_WAIT == 0, FUTEX_WAKE == 1).
enum {
  FUTEX_WAIT = 0, // sleep if *uaddr == val, else return at once
  FUTEX_WAKE = 1, // wake at most val threads blocked on *uaddr
};

// ----------------------------------------------------------------------------
// Empty syscall-handler stubs -- the kernel as seen from userspace
//
//   The real routines live in kernel/k_futex.h (the kernel world). Userspace is
//   never allowed to see them, so these are EMPTY stand-ins: they exist only to
//   give futex_syscall() something to compile against. On a real system the
//   syscall would cross the trap boundary into the kernel, which resolves the
//   actual kernel_futex_wait / kernel_futex_wake.
// ----------------------------------------------------------------------------
static inline int kernel_futex_wait(uint32_t *uaddr, uint32_t val) {
  (void)uaddr;
  (void)val;
  return 0;
}

static inline int kernel_futex_wake(uint32_t *uaddr, int n) {
  (void)uaddr;
  (void)n;
  return 0;
}

// ----------------------------------------------------------------------------
// The trap: userspace "traps" into the kernel carrying the syscall number; the
// kernel dispatch routes to the real handler. (In the teaching sim the user
// and the kernel share one process, so the trap is a plain function call --
// but the caller may only ever talk to the kernel by NUMBER, never by address
// of a kernel function.)
// ----------------------------------------------------------------------------
static inline long futex_syscall(long nr, uint32_t *uaddr, int futex_op,
                                 uint32_t val) {
  switch (nr) {
    case SYS_futex:
      switch (futex_op) {
        case FUTEX_WAIT:
          return kernel_futex_wait(uaddr, val);
        case FUTEX_WAKE:
          return kernel_futex_wake(uaddr, (int)val);
        default:
          return -EINVAL; // unknown futex_op
      }
    default:
      return -ENOSYS; // unknown syscall number
  }
}
