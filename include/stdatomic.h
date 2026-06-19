#ifndef _STDATOMIC_H
#define _STDATOMIC_H

/* ag_c 同梱 <stdatomic.h> (C11 7.17)。
 *
 * ag_c は単一スレッドを対象とするため、アトミック操作は通常のロード/ストア/算術に
 * 退化させる (単一スレッドでは意味的に正しい)。statement expression / typeof / アトミック
 * 組込みが無いため、旧値を返す非可逆操作には次の制約がある:
 *   - atomic_exchange / atomic_fetch_or / atomic_fetch_and は「演算後の新しい値」を
 *     返す (厳密には旧値を返すべき。単一スレッドかつ戻り値未使用の用途では支障なし)。
 *   - atomic_fetch_add / atomic_fetch_sub / atomic_fetch_xor は可逆なので旧値を正しく返す。
 *   - atomic_flag_test_and_set は旧値を正しく返す。
 */

#include <stddef.h>
#include <stdint.h>

/* memory_order (単一スレッドでは全て同義)。 */
typedef enum {
  memory_order_relaxed = 0,
  memory_order_consume = 1,
  memory_order_acquire = 2,
  memory_order_release = 3,
  memory_order_acq_rel = 4,
  memory_order_seq_cst = 5
} memory_order;

/* ロックフリー性: 単一スレッドなので常にロックフリー (2)。 */
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

/* 初期化・ロード・ストア。 */
#define atomic_init(obj, value)                  ((void)(*(obj) = (value)))
#define atomic_store(obj, value)                 ((void)(*(obj) = (value)))
#define atomic_store_explicit(obj, value, order) ((void)(*(obj) = (value)))
#define atomic_load(obj)                         (*(obj))
#define atomic_load_explicit(obj, order)         (*(obj))

/* 交換 (新しい値を返す: 上記の制約を参照)。 */
#define atomic_exchange(obj, value)                  (*(obj) = (value))
#define atomic_exchange_explicit(obj, value, order)  (*(obj) = (value))

/* compare-and-swap: *obj==*expected なら *obj=desired にして 1、さもなくば
 * *expected=*obj にして 0 を返す (C11 7.17.7.4)。単一スレッドなので weak も spurious
 * 失敗なし = strong と同じ。 */
#define atomic_compare_exchange_strong(obj, expected, desired) \
  (*(obj) == *(expected) ? ((void)(*(obj) = (desired)), 1) \
                         : ((void)(*(expected) = *(obj)), 0))
#define atomic_compare_exchange_weak(obj, expected, desired) \
  atomic_compare_exchange_strong(obj, expected, desired)
#define atomic_compare_exchange_strong_explicit(obj, expected, desired, succ, fail) \
  atomic_compare_exchange_strong(obj, expected, desired)
#define atomic_compare_exchange_weak_explicit(obj, expected, desired, succ, fail) \
  atomic_compare_exchange_strong(obj, expected, desired)

/* 取得して演算 (fetch-and-op)。add/sub/xor は旧値を正しく返す (可逆)。
 * or/and は新しい値を返す (上記の制約を参照)。 */
#define atomic_fetch_add(obj, arg)                 (*(obj) += (arg), *(obj) - (arg))
#define atomic_fetch_add_explicit(obj, arg, order) (*(obj) += (arg), *(obj) - (arg))
#define atomic_fetch_sub(obj, arg)                 (*(obj) -= (arg), *(obj) + (arg))
#define atomic_fetch_sub_explicit(obj, arg, order) (*(obj) -= (arg), *(obj) + (arg))
#define atomic_fetch_xor(obj, arg)                 (*(obj) ^= (arg), *(obj) ^ (arg))
#define atomic_fetch_xor_explicit(obj, arg, order) (*(obj) ^= (arg), *(obj) ^ (arg))
#define atomic_fetch_or(obj, arg)                  (*(obj) |= (arg))
#define atomic_fetch_or_explicit(obj, arg, order)  (*(obj) |= (arg))
#define atomic_fetch_and(obj, arg)                 (*(obj) &= (arg))
#define atomic_fetch_and_explicit(obj, arg, order) (*(obj) &= (arg))

/* atomic_flag 操作。test_and_set は旧値を正しく返す。 */
#define atomic_flag_test_and_set(flag) \
  ((flag)->__ag_val == 0 ? ((flag)->__ag_val = 1, 0) : 1)
#define atomic_flag_test_and_set_explicit(flag, order) \
  atomic_flag_test_and_set(flag)
#define atomic_flag_clear(flag)                ((void)((flag)->__ag_val = 0))
#define atomic_flag_clear_explicit(flag, order) ((void)((flag)->__ag_val = 0))

/* フェンス (単一スレッドでは何もしない)。 */
#define atomic_thread_fence(order) ((void)0)
#define atomic_signal_fence(order) ((void)0)

/* その他。 */
#define atomic_is_lock_free(obj) 1
#define kill_dependency(y)       (y)

#endif /* _STDATOMIC_H */
