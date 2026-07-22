// for 初期化宣言の複数宣言子と、その宣言全体に対するスコープ。
// 後続宣言子は先行宣言子を参照でき、ループ終了後は外側の i が復元される。
#include <assert.h>

int main(void) {
    int i = 40;
    int total = 0;

    for (int i = 1, limit = i + 2; i <= limit; i++) {
        total += i;
    }

    assert(total == 6);
    assert(i == 40);
    return 0;
}
