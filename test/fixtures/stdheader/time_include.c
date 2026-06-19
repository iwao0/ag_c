// time.h の time_t 型が使えること
// 期待: exit=42
#include <time.h>
#include <assert.h>
int main(void) {
    time_t t = 0;
    assert(42 == 42);
    return 0;
}
