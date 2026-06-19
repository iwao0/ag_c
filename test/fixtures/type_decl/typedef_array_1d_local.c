// 1 次元 typedef 配列をローカル変数として宣言・読み書きする。
// 修正前: `row r;` で elem_size=4 (int 1 個分) としか確保されず、
// `r[3]` のアクセスで SEGV していた。
// 期待: exit=0
#include <assert.h>
typedef int row[4];
int main(void) {
    row r;
    int i;
    for (i = 0; i < 4; i++) r[i] = i * 10;
    assert(r[3] == 30);
    return 0;
}
