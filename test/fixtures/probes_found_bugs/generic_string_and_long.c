// _Generic の制御式の型推定で 2 件の取りこぼし:
//  (1) 文字列リテラルが char* へ decay した型として扱われず (ptr_deref_size 未設定)、
//      `char*` association と一致せず default に落ちていた。
//  (2) long サフィックス付き整数リテラル (`42L`) が int 扱い (scalar_size=4) で、
//      `long:` association と一致しなかった。
// 文字列に ptr_deref_size=文字幅 を設定し、NUM の int_is_long で scalar_size=8 にして修正。
#include <assert.h>
int main(void) {
  int t = 0;

  // 文字列リテラル -> char*
  t += (_Generic("hello", char*: 1, default: 0) == 1);
  t += (_Generic("x", char*: 1, int: 2, default: 0) == 1);

  // 配列 decay
  int arr[5]; (void)arr;
  t += (_Generic(arr, int*: 1, default: 0) == 1);
  char buf[10]; (void)buf;
  t += (_Generic(buf, char*: 1, default: 0) == 1);

  // long / long long リテラル
  t += (_Generic(42L, long: 1, int: 2, default: 0) == 1);
  t += (_Generic(42LL, long long: 1, int: 2, default: 0) == 1);
  t += (_Generic(42, int: 1, long: 2, default: 0) == 1);   // int は int のまま

  // スカラ / ポインタ変数 (回帰確認)
  int i = 0; long l = 0; int *ip = 0; char *cp = 0;
  t += (_Generic(i, int: 1, default: 0) == 1);
  t += (_Generic(l, long: 1, default: 0) == 1);
  t += (_Generic(ip, int*: 1, default: 0) == 1);
  t += (_Generic(cp, char*: 1, default: 0) == 1);

  assert(t == 11); return 0;  // 11 checks -> 11+31 = 42
}
