// ローカル typedef + union 前方宣言
// 期待: exit=0
#include <assert.h>
int main(void) {
    typedef union L L;
    return 0;
}
