// 冗長括弧付きローカル関数ポインタ宣言
// 期待: exit=0
#include <assert.h>
int main(void) {
    int (*(*pp))(int);
    return 0;
}
