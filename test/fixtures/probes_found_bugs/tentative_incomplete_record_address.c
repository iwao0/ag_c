// record完成前でも、不完全型objectのアドレス取得とpointer返却は有効。
#include <assert.h>

struct DeferredValue;
struct DeferredValue deferred_value;

static struct DeferredValue *deferred_address(void) {
    return &deferred_value;
}

struct DeferredValue {
    int value;
};

int main(void) {
    deferred_address()->value = 42;
    assert(deferred_value.value == 42);
    return 0;
}
