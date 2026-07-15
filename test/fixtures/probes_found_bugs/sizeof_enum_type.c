// sizeof / _Alignof に enum 型名を渡すと E3064 でコンパイル失敗していた。
// (sizeof(struct/union T)・sizeof(enum 変数) は通っていた。enum 型名のみ未対応。)
// parse_parenthesized_type_size に TK_ENUM 分岐を追加 (enum は int 相当 4 バイト)。
#include <assert.h>

enum E { X = 1, Y, Z };
enum Color { R, G, B };

int main(void) {
    assert(sizeof(enum E) == 4);
    assert(_Alignof(enum E) == 4);
    assert(sizeof(enum Color) == 4);
    assert(sizeof(enum E *) == sizeof(void*)); // enum へのポインタ
    enum Color c = G;
    assert((int)sizeof(enum Color) + c == 5);
    return 0;
}
