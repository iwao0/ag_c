// typedef した unsigned 型のグローバル変数が、符号拡張 load され比較が化けていた
// バグ。`typedef unsigned u32; u32 g=0xFFFFFFFF; g==0xFFFFFFFFu` が false に。
// ローカル変数や直接 `unsigned g` は is_unsigned が伝播していたが、typedef 経由の
// グローバル宣言経路 (apply_toplevel_typedef_decl_spec) が typedef の is_unsigned を
// g_toplevel_decl_is_unsigned に伝えていなかった。
// 修正: psx_ctx_find_typedef_name_ex3 の out_is_unsigned を捕捉し、
//   apply_toplevel_typedef_decl_spec に渡して g_toplevel_decl_is_unsigned に設定する。
// 期待: exit=42
#include <assert.h>
typedef unsigned       u32;
typedef unsigned long  u64;
typedef unsigned char  u8;

u32 g32 = 0xFFFFFFFFu;
u64 g64 = 0xFFFFFFFFFFFFFFFFUL;
u8  g8  = 250;

int main(void) {
    if (!(g32 == 0xFFFFFFFFu)) return 1;
    if (g32 <= 0x7FFFFFFFu) return 2;          // unsigned 比較
    if (g64 <= 0x7FFFFFFFFFFFFFFFUL) return 3; // unsigned long 比較
    if (g8 <= 200) return 4;

    return 0;
}
