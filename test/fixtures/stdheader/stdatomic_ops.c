// 同梱 <stdatomic.h> (単一スレッド版) の基本操作を検査する。
// load/store/init/fetch_add/sub/xor は旧値を正しく返す。compare_exchange は
// 成功/失敗の両経路。atomic_flag の test_and_set/clear。fence は no-op。
#include <stdatomic.h>
#include <assert.h>

int main(void) {
    atomic_int x;
    atomic_init(&x, 10);
    assert(atomic_load(&x) == 10);

    atomic_store(&x, 42);
    assert(atomic_load(&x) == 42);

    // fetch_add は旧値を返す
    int old = atomic_fetch_add(&x, 5);
    assert(old == 42);
    assert(atomic_load(&x) == 47);

    // fetch_sub は旧値を返す
    old = atomic_fetch_sub(&x, 7);
    assert(old == 47);
    assert(atomic_load(&x) == 40);

    // fetch_xor は旧値を返す
    atomic_store(&x, 0xF0);
    old = atomic_fetch_xor(&x, 0x0F);
    assert(old == 0xF0);
    assert(atomic_load(&x) == 0xFF);

    // compare_exchange: 成功
    atomic_store(&x, 100);
    int expected = 100;
    int ok = atomic_compare_exchange_strong(&x, &expected, 200);
    assert(ok == 1);
    assert(atomic_load(&x) == 200);

    // compare_exchange: 失敗 (expected が現在値に更新される)
    int expected2 = 999;
    int ng = atomic_compare_exchange_strong(&x, &expected2, 0);
    assert(ng == 0);
    assert(expected2 == 200);
    assert(atomic_load(&x) == 200);

    // memory_order と _explicit バリアント
    atomic_store_explicit(&x, 7, memory_order_seq_cst);
    assert(atomic_load_explicit(&x, memory_order_acquire) == 7);

    // atomic_flag
    atomic_flag f = ATOMIC_FLAG_INIT;
    assert(atomic_flag_test_and_set(&f) == 0);  // 旧値 0
    assert(atomic_flag_test_and_set(&f) == 1);  // 旧値 1
    atomic_flag_clear(&f);
    assert(atomic_flag_test_and_set(&f) == 0);

    // fence (no-op) と lock-free
    atomic_thread_fence(memory_order_seq_cst);
    assert(atomic_is_lock_free(&x) == 1);
    assert(ATOMIC_INT_LOCK_FREE == 2);

    // long の atomic
    atomic_long lx;
    atomic_init(&lx, 1000);
    assert(atomic_fetch_add(&lx, 234) == 1000);
    assert(atomic_load(&lx) == 1234);
    return 0;
}
