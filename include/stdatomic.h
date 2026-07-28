#ifndef _STDATOMIC_H
#define _STDATOMIC_H

/* ag_c bundled <stdatomic.h> (C11 7.17).
 *
 * Apple ARM64 LSE atomic instructions and barriers provide true atomicity
 * (including in multithreaded code).  Every operation is emitted with seq_cst
 * strength (ldar/stlr/ld...al/swpal/casal/dmb ish).  Fetch operations return
 * the previous value, as required by the standard.
 *
 * The compiler determines width and signedness from the type addressed by the
 * obj pointer.  Loads and stores support scalars and pointers as well as
 * aggregate and complex types whose widths the backend can treat as atomic
 * storage.  A memory_order argument is accepted, but operations always use
 * seq_cst strength (stronger ordering than requested is standards-compliant). */

#include <stddef.h>
#include <stdint.h>

/* memory_order. */
typedef enum {
  memory_order_relaxed = 0,
  memory_order_consume = 1,
  memory_order_acquire = 2,
  memory_order_release = 3,
  memory_order_acq_rel = 4,
  memory_order_seq_cst = 5
} memory_order;

/* Lock-free properties: 1/2/4/8-byte integers are always lock-free with LSE (2). */
#define ATOMIC_BOOL_LOCK_FREE     2
#define ATOMIC_CHAR_LOCK_FREE     2
#define ATOMIC_CHAR16_T_LOCK_FREE 2
#define ATOMIC_CHAR32_T_LOCK_FREE 2
#define ATOMIC_WCHAR_T_LOCK_FREE  2
#define ATOMIC_SHORT_LOCK_FREE    2
#define ATOMIC_INT_LOCK_FREE      2
#define ATOMIC_LONG_LOCK_FREE     2
#define ATOMIC_LLONG_LOCK_FREE    2
#define ATOMIC_POINTER_LOCK_FREE  2

/* C11 ATOMIC_VAR_INIT (deprecated in C17, retained for compatibility). */
#define ATOMIC_VAR_INIT(value) (value)

/* Atomic types (= base types qualified with _Atomic). */
typedef _Atomic _Bool              atomic_bool;
typedef _Atomic char               atomic_char;
typedef _Atomic signed char        atomic_schar;
typedef _Atomic unsigned char      atomic_uchar;
typedef _Atomic short              atomic_short;
typedef _Atomic unsigned short     atomic_ushort;
typedef _Atomic int                atomic_int;
typedef _Atomic unsigned int       atomic_uint;
typedef _Atomic long               atomic_long;
typedef _Atomic unsigned long      atomic_ulong;
typedef _Atomic long long          atomic_llong;
typedef _Atomic unsigned long long atomic_ullong;
typedef _Atomic uint_least16_t     atomic_char16_t;
typedef _Atomic uint_least32_t     atomic_char32_t;
typedef _Atomic wchar_t            atomic_wchar_t;
typedef _Atomic intptr_t           atomic_intptr_t;
typedef _Atomic uintptr_t          atomic_uintptr_t;
typedef _Atomic size_t             atomic_size_t;
typedef _Atomic ptrdiff_t          atomic_ptrdiff_t;
typedef _Atomic intmax_t           atomic_intmax_t;
typedef _Atomic uintmax_t          atomic_uintmax_t;

/* atomic_flag: flag used for test-and-set. */
typedef struct { _Atomic _Bool __ag_val; } atomic_flag;
#define ATOMIC_FLAG_INIT {0}

/* Internal compiler intrinsics used by the public macros below.  They are
 * lowered by the IR builder and never linked as ordinary runtime functions. */
long __ag_atomic_load(void *obj);
long __ag_atomic_store(void *obj, long value);
long __ag_atomic_exchange(void *obj, long value);
int  __ag_atomic_cas(void *obj, void *expected, long desired);
long __ag_atomic_fetch_add(void *obj, long value);
long __ag_atomic_fetch_sub(void *obj, long value);
long __ag_atomic_fetch_or(void *obj, long value);
long __ag_atomic_fetch_xor(void *obj, long value);
long __ag_atomic_fetch_and(void *obj, long value);
int  __ag_atomic_fence(void);

/* Apply the same assignment conversion as a memory_order parameter and reject
 * non-arithmetic types.  The argument is evaluated exactly once because this
 * is a compound-literal initializer. */
#define __ag_atomic_order(order) \
  ((void)((memory_order){(order)}))

/* Initialization.  Use a compiler intrinsic to preserve the same type
 * constraints as the public generic function.  A seq_cst store is stronger
 * than non-atomic initialization before publication, but remains conforming. */
#define atomic_init(obj, value) \
  ((void)__ag_atomic_store((obj), (value)))

/* Load/store.  The compiler builtin preserves the object type and returns the
 * corresponding non-atomic type, including aggregate and complex types. */
#define atomic_load(obj)                         __ag_atomic_load(obj)
#define atomic_load_explicit(obj, order) \
  (__ag_atomic_order(order), __ag_atomic_load(obj))
#define atomic_store(obj, value) \
  ((void)__ag_atomic_store((obj), (value)))
#define atomic_store_explicit(obj, value, order) \
  (__ag_atomic_order(order), (void)__ag_atomic_store((obj), (value)))

/* Exchange: integers, pointers, and 1/2/4/8-byte objects use a single-word
 * exchange; 16-byte Apple ARM64 objects use a CASPAL retry loop.  Return the
 * previous value. */
#define atomic_exchange(obj, value)                 __ag_atomic_exchange((obj), (value))
#define atomic_exchange_explicit(obj, value, order) \
  (__ag_atomic_order(order), __ag_atomic_exchange((obj), (value)))

/* Compare-and-swap: if *obj == *expected, assign desired to *obj and return 1;
 * otherwise assign *obj to *expected and return 0 (C11 7.17.7.4).
 * Apple ARM64 uses CASAL/CASPAL; weak has no spurious failures and therefore
 * behaves like strong. */
#define atomic_compare_exchange_strong(obj, expected, desired) \
  ((_Bool)__ag_atomic_cas((obj), (expected), (desired)))
#define atomic_compare_exchange_weak(obj, expected, desired) \
  ((_Bool)__ag_atomic_cas((obj), (expected), (desired)))
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail) \
  (__ag_atomic_order(succ), __ag_atomic_order(fail), \
   (_Bool)__ag_atomic_cas((obj), (expected), (desired)))
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ, fail) \
  (__ag_atomic_order(succ), __ag_atomic_order(fail), \
   (_Bool)__ag_atomic_cas((obj), (expected), (desired)))

/* Fetch-and-operate (LDADDAL/LDSETAL/LDCLRAL/LDEORAL): all return the old value. */
#define atomic_fetch_add(obj, arg)                 __ag_atomic_fetch_add((obj), (arg))
#define atomic_fetch_add_explicit(obj, arg, order) \
  (__ag_atomic_order(order), __ag_atomic_fetch_add((obj), (arg)))
#define atomic_fetch_sub(obj, arg)                 __ag_atomic_fetch_sub((obj), (arg))
#define atomic_fetch_sub_explicit(obj, arg, order) \
  (__ag_atomic_order(order), __ag_atomic_fetch_sub((obj), (arg)))
#define atomic_fetch_or(obj, arg)                  __ag_atomic_fetch_or((obj), (arg))
#define atomic_fetch_or_explicit(obj, arg, order) \
  (__ag_atomic_order(order), __ag_atomic_fetch_or((obj), (arg)))
#define atomic_fetch_xor(obj, arg)                 __ag_atomic_fetch_xor((obj), (arg))
#define atomic_fetch_xor_explicit(obj, arg, order) \
  (__ag_atomic_order(order), __ag_atomic_fetch_xor((obj), (arg)))
#define atomic_fetch_and(obj, arg)                 __ag_atomic_fetch_and((obj), (arg))
#define atomic_fetch_and_explicit(obj, arg, order) \
  (__ag_atomic_order(order), __ag_atomic_fetch_and((obj), (arg)))

/* atomic_flag operations.  test_and_set returns the previous Boolean value. */
#define atomic_flag_test_and_set(flag) \
  ((_Bool)(__ag_atomic_exchange(&(flag)->__ag_val, 1) != 0))
#define atomic_flag_test_and_set_explicit(flag, order) \
  (__ag_atomic_order(order), atomic_flag_test_and_set(flag))
#define atomic_flag_clear(flag)                 ((void)__ag_atomic_store(&(flag)->__ag_val, 0))
#define atomic_flag_clear_explicit(flag, order) \
  (__ag_atomic_order(order), \
   (void)__ag_atomic_store(&(flag)->__ag_val, 0))

/* Fences (DMB ISH). */
#define atomic_thread_fence(order) \
  (__ag_atomic_order(order), (void)__ag_atomic_fence())
#define atomic_signal_fence(order) \
  (__ag_atomic_order(order), (void)__ag_atomic_fence())

/* Miscellaneous operations. */
#define atomic_is_lock_free(obj) \
  ((void)(obj), (void)sizeof(__ag_atomic_load(obj)), \
   (_Bool)(sizeof(*(obj)) <= 16))
#define kill_dependency(y) \
  ((void)sizeof((y) ? 1 : 0), (y))

#endif /* _STDATOMIC_H */
