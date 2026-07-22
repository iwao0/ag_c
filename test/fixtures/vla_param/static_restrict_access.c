// 配列仮引数の static/restrict は関数本体ではポインタへ調整され、
// 保証された最小要素数の範囲を通常の配列添字で読み書きできる。
#include <assert.h>

static int update_and_sum(int values[restrict static 4]) {
    values[2] += 10;
    return values[0] + values[1] + values[2] + values[3];
}

int main(void) {
    int values[4] = {1, 2, 3, 4};
    assert(update_and_sum(values) == 20);
    assert(values[2] == 13);
    return 0;
}
