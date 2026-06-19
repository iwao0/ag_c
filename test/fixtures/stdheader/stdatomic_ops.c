// 同梱 <stdatomic.h> (Apple ARM64 LSE 命令による本物のアトミック) を検査する。
// fetch_add/sub/or/and/xor/exchange は規格通り旧値を返す。compare_exchange は
// 成功/失敗の両経路。各幅 (1/2/4/8 バイト) と符号、atomic_flag、fence。
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

    // fetch_or / fetch_and / exchange も旧値を返す
    atomic_store(&x, 0xF0);
    assert(atomic_fetch_or(&x, 0x0F) == 0xF0);   // 旧値
    assert(atomic_load(&x) == 0xFF);
    assert(atomic_fetch_and(&x, 0x3C) == 0xFF);  // 旧値
    assert(atomic_load(&x) == 0x3C);
    assert(atomic_exchange(&x, 555) == 0x3C);    // 旧値
    assert(atomic_load(&x) == 555);

    // long (8 バイト) の atomic
    atomic_long lx;
    atomic_init(&lx, 1000000000000L);
    assert(atomic_fetch_add(&lx, 234) == 1000000000000L);
    assert(atomic_load(&lx) == 1000000000234L);

    // short (2 バイト)
    atomic_short sx;
    atomic_init(&sx, 100);
    assert(atomic_fetch_add(&sx, 5) == 100);
    assert(atomic_load(&sx) == 105);

    // unsigned char (1 バイト, 符号なし)
    atomic_uchar cx;
    atomic_init(&cx, 200);
    assert(atomic_fetch_add(&cx, 50) == 200);
    assert(atomic_load(&cx) == 250);

    // signed char (1 バイト, 負値)
    atomic_schar scx;
    atomic_init(&scx, -5);
    assert(atomic_load(&scx) == -5);
    assert(atomic_fetch_sub(&scx, 3) == -5);
    assert(atomic_load(&scx) == -8);
    return 0;
}
