// printf を可変引数で呼ぶ ("x=42\n" は 5 文字)
// 期待: exit=0
#include <stdio.h>
#include <assert.h>
int main(void) {
    assert(printf("x=%d\n", 42) == 5);
    return 0;
}
