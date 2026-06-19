// 整数 + ポインタ `*(2 + a)` の可換性
// 修正前: parser の ADD 構築は左辺だけ pointer 判定し scaling していたため、
// `2 + a` (左が int) では scaling せず整数加算扱い → 不正アドレスを deref して
// garbage 値。`a + 2` と等価のはず (C11 6.5.6p2)。
//
// 左が非ポインタで右がポインタなら swap して既存ロジックに乗せる
// (p75 で subscript で施した swap と同じパターン)。
#include <assert.h>
int main(void) {
  int a[5] = {10, 20, 30, 40, 50};
  assert(*(a + 2) == 30); assert(*(2 + a) == 30); assert(a[2] == 30); return 0; // 30 + 30 + 30 - 90 = 0
}
// 期待: 0
