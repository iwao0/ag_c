// 複文 (ブロック) 内の文
// 期待: exit=3
#include <assert.h>
main() {
    int r = 0;
    { 1; 2; r = 3; }
    assert(r == 3);
    return 0;
}
