// インラインの `*(T*)ptr` (ポインタを別要素サイズの型へキャストして即 deref/添字)
// が、キャストで pointee サイズを更新せず元ポインタの要素サイズでロードしていた。
// `*(int*)(charptr + 4)` が char サイズ (1 バイト) で読まれて化けていた
// (一旦 `int *p = (int*)...;` と変数に入れると変数の型で正しく動いていた)。
// スカラ整数型への単段ポインタキャストを ND_PTR_CAST で deref_size 更新して修正。
#include <assert.h>
int main(void) {
  int data[4] = {0x01020304, 0x05060708, 0x090A0B0C, 0x0D0E0F10};
  char *cp = (char *)data;
  int t = 0;

  // char* + offset を int* にキャストして即 deref
  t += (*(int *)(cp + 4) == 0x05060708);   // data[1]
  t += (*(int *)(cp + 8) == 0x090A0B0C);   // data[2]

  // long* で 8 バイト読み
  t += (*(long *)(cp + 0) == 0x0506070801020304L);

  // short* で添字
  short *sp = (short *)data;
  t += (sp[2] == 0x0708);

  // long 配列を char* 経由で再解釈
  long arr[3] = {10, 20, 30};
  char *lc = (char *)arr;
  t += (*(long *)(lc + 8) == 20);
  t += (*(long *)(lc + 16) == 30);

  // null ポインタ定数キャストは定数のまま (ラップしない) — 比較が成立する
  t += ((int *)0 == 0);

  assert(t == 7); return 0;  // 7 checks -> 7+35 = 42
}
