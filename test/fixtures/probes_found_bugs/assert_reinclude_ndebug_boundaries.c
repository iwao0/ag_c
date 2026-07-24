// assert.h must reevaluate NDEBUG on every inclusion; static_assert is C11.
// Expected: exit=0
#define NDEBUG
#include <assert.h>

static void disabled_first(int *value) {
    assert(++*value == 100);
}

#undef NDEBUG
#include <assert.h>

static void enabled_first(int *value) {
    assert(++*value == 1);
}

#define NDEBUG
#include <assert.h>

static void disabled_second(int *value) {
    assert(++*value == 100);
}

#undef NDEBUG
#include <assert.h>

static_assert(sizeof(int) >= 2, "assert.h must expose static_assert in C11");

static void enabled_second(int *value) {
    assert(++*value == 2);
}

int main(void) {
    int value = 0;

    disabled_first(&value);
    if (value != 0)
        return 1;
    enabled_first(&value);
    if (value != 1)
        return 2;
    disabled_second(&value);
    if (value != 1)
        return 3;
    enabled_second(&value);
    if (value != 2)
        return 4;
    return 0;
}
