// 関数ポインタ配列を struct メンバに持ち、brace 初期化する dispatch table が
// 壊れていたバグ。`struct Calc{int(*ops[3])(int,int);}; c={{add,sub,mul}}` が
// E3064 (スカラ初期化子の波括弧内は1要素のみ) で拒否され、ガードを緩めると今度は
// 先頭要素が最後の値で上書きされ誤値になっていた。
// 原因: 関数ポインタ配列メンバは is_tag_pointer=1 (head.is_ptr 由来) で登録される
//   ため、parse_member_initializer / wrap_member_init_as_assign の
//   `array_len>0 && !is_tag_pointer` ガードが配列扱いをスキップしていた。後者では
//   init_chain (要素代入チェーン) の最終値を member スロットへ余分に代入し
//   ops[0] を破壊していた。
// 修正: 両ガードを `array_len>0` にする (is_tag_pointer は要素がポインタかどうかで
//   あって、配列であることとは独立)。
// 期待: exit=42
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int mul(int a, int b) { return a * b; }

struct Calc { int (*ops[3])(int, int); };

int main(void) {
    // brace 初期化した dispatch table を index で呼び出す
    struct Calc c = {{add, sub, mul}};
    if (c.ops[0](10, 3) != 13) return 1;   // add
    if (c.ops[1](10, 3) != 7) return 2;    // sub
    if (c.ops[2](10, 3) != 30) return 3;   // mul

    int r = 0;
    for (int i = 0; i < 3; i++) r += c.ops[i](6, 2);  // 8 + 4 + 12 = 24
    if (r != 24) return 4;

    // 代入形式も従来どおり
    struct Calc d;
    d.ops[0] = mul; d.ops[1] = add; d.ops[2] = sub;
    if (d.ops[0](5, 4) != 20) return 5;

    return c.ops[0](10, 3) + c.ops[2](5, 5) + r - 20;  // 13 + 25 + 24 - 20 = 42
}
