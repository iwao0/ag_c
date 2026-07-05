#ifndef _STDATOMIC_H
#define _STDATOMIC_H

/* ag_c 同梱 <stdatomic.h> (C11 7.17)。
 *
 * Apple ARM64 の LSE アトミック命令とバリアで真にアトミックに実装する
 * (マルチスレッドで正しい)。操作は全て seq_cst 強度 (ldar/stlr/ld...al/swpal/
 * casal/dmb ish) で発行する。fetch 系は規格通り「演算前の旧値」を返す。
 *
 * 幅と符号はコンパイラが obj ポインタの指す型から決める (1/2/4/8 バイト)。
 * memory_order 引数は受け取るが、常に seq_cst 強度で実行する (規格上、要求より
 * 強い順序付けは常に安全)。 */

#include <stddef.h>
#include <stdint.h>

/* memory_order。 */
typedef enum {
  memory_order_relaxed = 0,
  memory_order_consume = 1,
  memory_order_acquire = 2,
  memory_order_release = 3,
  memory_order_acq_rel = 4,
  memory_order_seq_cst = 5
} memory_order;

/* ロックフリー性: 1/2/4/8 バイト整数は LSE で常にロックフリー (2)。 */
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

/* C11 の ATOMIC_VAR_INIT (C17 で非推奨だが互換のため提供)。 */
#define ATOMIC_VAR_INIT(value) (value)

/* アトミック型 (= _Atomic 修飾した基底型)。 */
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
typedef _Atomic int                atomic_char16_t; /* char16_t = int 相当 */
typedef _Atomic int                atomic_char32_t;
typedef _Atomic int                atomic_wchar_t;
typedef _Atomic long               atomic_intptr_t;
typedef _Atomic unsigned long      atomic_uintptr_t;
typedef _Atomic unsigned long      atomic_size_t;
typedef _Atomic long               atomic_ptrdiff_t;
typedef _Atomic long               atomic_intmax_t;
typedef _Atomic unsigned long      atomic_uintmax_t;

/* atomic_flag: テスト&セット用のフラグ。 */
typedef struct { _Bool __ag_val; } atomic_flag;
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

/* 初期化: オブジェクトはまだ共有されていないので非アトミックでよい (C11 7.17.2.2)。 */
#define atomic_init(obj, value) ((void)(*(obj) = (value)))

/* ロード / ストア (LDAR / STLR)。 */
#define atomic_load(obj)                         __ag_atomic_load(obj)
#define atomic_load_explicit(obj, order)         __ag_atomic_load(obj)
#define atomic_store(obj, value)                 ((void)__ag_atomic_store((obj), (value)))
#define atomic_store_explicit(obj, value, order) ((void)__ag_atomic_store((obj), (value)))

/* 交換 (SWPAL): 旧値を返す。 */
#define atomic_exchange(obj, value)                 __ag_atomic_exchange((obj), (value))
#define atomic_exchange_explicit(obj, value, order) __ag_atomic_exchange((obj), (value))

/* compare-and-swap (CASAL): *obj==*expected なら *obj=desired にして 1、
 * さもなくば *expected=*obj にして 0 を返す (C11 7.17.7.4)。単一の CAS 命令なので
 * weak も spurious 失敗なし = strong と同じ。 */
#define atomic_compare_exchange_strong(obj, expected, desired) \
  __ag_atomic_cas((obj), (expected), (desired))
#define atomic_compare_exchange_weak(obj, expected, desired) \
  __ag_atomic_cas((obj), (expected), (desired))
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail) \
  __ag_atomic_cas((obj), (expected), (desired))
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ, fail) \
  __ag_atomic_cas((obj), (expected), (desired))

/* 取得して演算 (LDADDAL/LDSETAL/LDCLRAL/LDEORAL): いずれも旧値を返す。 */
#define atomic_fetch_add(obj, arg)                 __ag_atomic_fetch_add((obj), (arg))
#define atomic_fetch_add_explicit(obj, arg, order) __ag_atomic_fetch_add((obj), (arg))
#define atomic_fetch_sub(obj, arg)                 __ag_atomic_fetch_sub((obj), (arg))
#define atomic_fetch_sub_explicit(obj, arg, order) __ag_atomic_fetch_sub((obj), (arg))
#define atomic_fetch_or(obj, arg)                  __ag_atomic_fetch_or((obj), (arg))
#define atomic_fetch_or_explicit(obj, arg, order)  __ag_atomic_fetch_or((obj), (arg))
#define atomic_fetch_xor(obj, arg)                 __ag_atomic_fetch_xor((obj), (arg))
#define atomic_fetch_xor_explicit(obj, arg, order) __ag_atomic_fetch_xor((obj), (arg))
#define atomic_fetch_and(obj, arg)                 __ag_atomic_fetch_and((obj), (arg))
#define atomic_fetch_and_explicit(obj, arg, order) __ag_atomic_fetch_and((obj), (arg))

/* atomic_flag 操作。test_and_set は旧値 (真偽) を返す。 */
#define atomic_flag_test_and_set(flag) \
  (__ag_atomic_exchange(&(flag)->__ag_val, 1) != 0)
#define atomic_flag_test_and_set_explicit(flag, order) \
  atomic_flag_test_and_set(flag)
#define atomic_flag_clear(flag)                 ((void)__ag_atomic_store(&(flag)->__ag_val, 0))
#define atomic_flag_clear_explicit(flag, order) ((void)__ag_atomic_store(&(flag)->__ag_val, 0))

/* フェンス (DMB ISH)。 */
#define atomic_thread_fence(order) __ag_atomic_fence()
#define atomic_signal_fence(order) __ag_atomic_fence()

/* その他。 */
#define atomic_is_lock_free(obj) 1
#define kill_dependency(y)       (y)

#endif /* _STDATOMIC_H */
