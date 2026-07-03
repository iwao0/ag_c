// time.h minimal runtime calls
// Expected: exit=0
#include <assert.h>
#include <time.h>

int main(void) {
    time_t stored = -1;
    time_t now = time(&stored);
    assert(stored == now);
    assert(clock() >= 0);
    assert(difftime((time_t)10, (time_t)3) == 7.0);
    return 0;
}
