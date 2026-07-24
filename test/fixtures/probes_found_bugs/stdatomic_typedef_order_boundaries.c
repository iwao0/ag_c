// stdatomic.h typedef identity and generic-function argument evaluation.
// Expected: exit=0
#include <stdatomic.h>

static atomic_char16_t char16_value = ATOMIC_VAR_INIT(0x1234U);
static atomic_char32_t char32_value = ATOMIC_VAR_INIT(0x12345678U);
static atomic_intmax_t intmax_value = ATOMIC_VAR_INIT(-7);
static atomic_uintmax_t uintmax_value = ATOMIC_VAR_INIT(9);

static memory_order next_order(int *count, memory_order order) {
    ++*count;
    return order;
}

static int next_dependency(int *count, int value) {
    ++*count;
    return value;
}

int main(void) {
    atomic_int value = ATOMIC_VAR_INIT(0);
    atomic_flag flag = ATOMIC_FLAG_INIT;
    int order_count = 0;
    int dependency_count = 0;
    int expected;
    int result;
    long old_value;
    uint_least16_t loaded_char16;
    uint_least32_t loaded_char32;
    intmax_t loaded_intmax;
    uintmax_t loaded_uintmax;

    if (!_Generic(&char16_value, _Atomic uint_least16_t *: 1, default: 0) ||
        !_Generic(&char32_value, _Atomic uint_least32_t *: 1, default: 0))
        return 1;
#ifdef __wasm32__
    if (!_Generic(&intmax_value, _Atomic long long *: 1, default: 0) ||
        !_Generic(&uintmax_value, _Atomic unsigned long long *: 1, default: 0))
        return 2;
#else
    if (!_Generic(&intmax_value, _Atomic long *: 1, default: 0) ||
        !_Generic(&uintmax_value, _Atomic unsigned long *: 1, default: 0))
        return 2;
#endif
    loaded_char16 = (uint_least16_t)atomic_load(&char16_value);
    loaded_char32 = (uint_least32_t)atomic_load(&char32_value);
    loaded_intmax = (intmax_t)atomic_load(&intmax_value);
    loaded_uintmax = (uintmax_t)atomic_load(&uintmax_value);
    if (loaded_char16 != (uint_least16_t)0x1234U ||
        loaded_char32 != (uint_least32_t)0x12345678U ||
        loaded_intmax != (intmax_t)-7 ||
        loaded_uintmax != (uintmax_t)9)
        return 3;

    atomic_store_explicit(
        &value, 10, next_order(&order_count, memory_order_release));
    old_value = atomic_load_explicit(
        &value, next_order(&order_count, memory_order_acquire));
    if (old_value != 10)
        return 4;
    old_value = atomic_exchange_explicit(
        &value, 20, next_order(&order_count, memory_order_acq_rel));
    if (old_value != 10)
        return 5;

    expected = 20;
    result = atomic_compare_exchange_strong_explicit(
        &value, &expected, 30,
        next_order(&order_count, memory_order_acq_rel),
        next_order(&order_count, memory_order_acquire));
    if (!result)
        return 6;
    expected = 99;
    result = atomic_compare_exchange_weak_explicit(
        &value, &expected, 40,
        next_order(&order_count, memory_order_acq_rel),
        next_order(&order_count, memory_order_acquire));
    if (result || expected != 30)
        return 7;

    old_value = atomic_fetch_add_explicit(
        &value, 2, next_order(&order_count, memory_order_relaxed));
    if (old_value != 30)
        return 8;
    old_value = atomic_fetch_sub_explicit(
        &value, 1, next_order(&order_count, memory_order_relaxed));
    if (old_value != 32)
        return 8;
    old_value = atomic_fetch_or_explicit(
        &value, 32, next_order(&order_count, memory_order_relaxed));
    if (old_value != 31)
        return 8;
    old_value = atomic_fetch_xor_explicit(
        &value, 3, next_order(&order_count, memory_order_relaxed));
    if (old_value != 63)
        return 8;
    old_value = atomic_fetch_and_explicit(
        &value, 15, next_order(&order_count, memory_order_relaxed));
    if (old_value != 60 || atomic_load(&value) != 12)
        return 8;

    result = atomic_flag_test_and_set_explicit(
        &flag, next_order(&order_count, memory_order_acquire));
    if (result)
        return 9;
    atomic_flag_clear_explicit(
        &flag, next_order(&order_count, memory_order_release));
    atomic_thread_fence(next_order(&order_count, memory_order_seq_cst));
    atomic_signal_fence(next_order(&order_count, memory_order_seq_cst));
    if (order_count != 16)
        return 10;

    if (!atomic_is_lock_free(&value))
        return 11;
    if (kill_dependency(next_dependency(
            &dependency_count, 42)) != 42 || dependency_count != 1)
        return 12;
    return 0;
}
