// assert.h の assert(1) は何もしない (中断せず継続する)
#include <assert.h>
int main(void) {
    assert(1);
    return 0;
}
