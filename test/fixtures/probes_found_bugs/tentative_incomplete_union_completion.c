// structと同様に、unionの外部リンケージ暫定定義も後続定義で完成できる。
#include <assert.h>

union DeferredNumber;
union DeferredNumber deferred_number;

union DeferredNumber {
    int integer;
    unsigned int bits;
};

int main(void) {
    assert(deferred_number.integer == 0);
    deferred_number.integer = 42;
    assert(deferred_number.integer == 42);
    return 0;
}
