#pragma once

// atomic read, modify, and CAS, on the ADDRESS of a plain integer object.

#define atomic_load_relaxed(ptr) __atomic_load_n((ptr), __ATOMIC_RELAXED)
#define atomic_load_acquire(ptr) __atomic_load_n((ptr), __ATOMIC_ACQUIRE)

#define atomic_store_relaxed(ptr, val)                                         \
  __atomic_store_n((ptr), (val), __ATOMIC_RELAXED)
#define atomic_store_release(ptr, val)                                         \
  __atomic_store_n((ptr), (val), __ATOMIC_RELEASE)

// returns the OLD value
#define atomic_fetch_add_relaxed(ptr, val)                                     \
  __atomic_fetch_add((ptr), (val), __ATOMIC_RELAXED)
#define atomic_fetch_add_acquire(ptr, val)                                     \
  __atomic_fetch_add((ptr), (val), __ATOMIC_ACQUIRE)
#define atomic_fetch_add_release(ptr, val)                                     \
  __atomic_fetch_add((ptr), (val), __ATOMIC_RELEASE)

#define atomic_fetch_sub_relaxed(ptr, val)                                     \
  __atomic_fetch_sub((ptr), (val), __ATOMIC_RELAXED)
#define atomic_fetch_sub_acquire(ptr, val)                                     \
  __atomic_fetch_sub((ptr), (val), __ATOMIC_ACQUIRE)
#define atomic_fetch_sub_release(ptr, val)                                     \
  __atomic_fetch_sub((ptr), (val), __ATOMIC_RELEASE)

#define atomic_fetch_and_relaxed(ptr, val)                                     \
  __atomic_fetch_and((ptr), (val), __ATOMIC_RELAXED)
#define atomic_fetch_and_acquire(ptr, val)                                     \
  __atomic_fetch_and((ptr), (val), __ATOMIC_ACQUIRE)
#define atomic_fetch_and_release(ptr, val)                                     \
  __atomic_fetch_and((ptr), (val), __ATOMIC_RELEASE)

#define atomic_fetch_or_relaxed(ptr, val)                                      \
  __atomic_fetch_or((ptr), (val), __ATOMIC_RELAXED)
#define atomic_fetch_or_acquire(ptr, val)                                      \
  __atomic_fetch_or((ptr), (val), __ATOMIC_ACQUIRE)
#define atomic_fetch_or_release(ptr, val)                                      \
  __atomic_fetch_or((ptr), (val), __ATOMIC_RELEASE)

#define atomic_fetch_xor_relaxed(ptr, val)                                     \
  __atomic_fetch_xor((ptr), (val), __ATOMIC_RELAXED)
#define atomic_fetch_xor_release(ptr, val)                                     \
  __atomic_fetch_xor((ptr), (val), __ATOMIC_RELEASE)

// expected: the expected OLD value, lvalue, updated in place on failure.
// desired: the desired NEW, rvalue.
#define atomic_compare_exchange_weak_relaxed(ptr, expected, desired)           \
  __atomic_compare_exchange_n((ptr), (expected), (desired), true,              \
                              __ATOMIC_RELAXED, __ATOMIC_RELAXED)
#define atomic_compare_exchange_weak_acquire(ptr, expected, desired)           \
  __atomic_compare_exchange_n((ptr), (expected), (desired), true,              \
                              __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
#define atomic_compare_exchange_weak_release(ptr, expected, desired)           \
  __atomic_compare_exchange_n((ptr), (expected), (desired), true,              \
                              __ATOMIC_RELEASE, __ATOMIC_RELAXED)
#define atomic_compare_exchange_weak_acq_rel(ptr, expected, desired)           \
  __atomic_compare_exchange_n((ptr), (expected), (desired), true,              \
                              __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)

#define atomic_compare_exchange_strong_relaxed(ptr, expected, desired)         \
  __atomic_compare_exchange_n((ptr), (expected), (desired), false,             \
                              __ATOMIC_RELAXED, __ATOMIC_RELAXED)
#define atomic_compare_exchange_strong_acquire(ptr, expected, desired)         \
  __atomic_compare_exchange_n((ptr), (expected), (desired), false,             \
                              __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)
#define atomic_compare_exchange_strong_release(ptr, expected, desired)         \
  __atomic_compare_exchange_n((ptr), (expected), (desired), false,             \
                              __ATOMIC_RELEASE, __ATOMIC_RELAXED)
#define atomic_compare_exchange_strong_acq_rel(ptr, expected, desired)         \
  __atomic_compare_exchange_n((ptr), (expected), (desired), false,             \
                              __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)

#define atomic_thread_fence_acquire() __atomic_thread_fence(__ATOMIC_ACQUIRE)