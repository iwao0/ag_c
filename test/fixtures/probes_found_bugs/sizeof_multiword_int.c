// sizeof / _Alignof の型名に複数語の整数型 (`long long`, `unsigned long`,
// `unsigned int`, `short int`, `signed char` 等) を渡せる必要がある。
// parse_parenthesized_type_size が先頭 1 語しか消費せず、`sizeof(long long)` が
// 2 語目の `long` で E2006 になっていた。整数型指定子列をまとめて解釈して修正。
#include <assert.h>

int main(void) {
    assert(sizeof(long long) == 8);
    assert(sizeof(unsigned long long) == 8);
    assert(sizeof(unsigned long) == 8);
    assert(sizeof(long int) == 8);
    assert(sizeof(unsigned int) == 4);
    assert(sizeof(short int) == 2);
    assert(sizeof(signed char) == 1);
    assert(sizeof(unsigned char) == 1);

    assert(_Alignof(long long) == 8);
    assert(_Alignof(unsigned long long) == 8);

    // 単語型・long double・ポインタの退行確認
    assert(sizeof(long) == 8);
    assert(sizeof(int) == 4);
    assert(sizeof(long double) == 8);   // macOS/AArch64 では double と同じ
    assert(sizeof(long long *) == sizeof(void*));
    return 0;
}
