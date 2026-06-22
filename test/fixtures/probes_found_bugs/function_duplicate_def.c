// 同名関数の本体重複定義を E3064 で弾く (C11 6.9p3)。修正前は重複定義を素通しし、
// 後段でアセンブラ/リンカが duplicate symbol を出すまで気づけなかった。
//
// 修正: func_name_t に is_defined フラグを追加。funcdef の本体パース直前
// (proto `;` を弾いた後) で psx_ctx_track_function_defined を呼び、初回は記録、
// 2 度目なら E3064。本 fixture は「合法な再宣言 + 1 度の定義」が false-positive で
// 弾かれないことの回帰確認。
#include <assert.h>

/* (a) 同一シグネチャの重複プロトタイプ + 1 つの定義 */
int repeat_proto(int);
int repeat_proto(int);
int repeat_proto(int);
int repeat_proto(int x) { return x + 1; }

/* (b) static 関数の 1 つの定義 (内部リンケージ) */
static int internal(int x) { return x * 2; }

/* (c) プロトタイプ + 定義 (典型) */
int normal(int);
int normal(int x) { return x; }

int main(void) {
    assert(repeat_proto(10) == 11);
    assert(internal(5) == 10);
    assert(normal(7) == 7);
    return 0;
}
