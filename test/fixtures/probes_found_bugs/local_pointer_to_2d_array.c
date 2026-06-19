// 局所の「2 次元配列へのポインタ」`T (*p)[N][M]` が mid_stride 未設定で、p[i][j] が
// 誤スケール（p[i][j][k] が壊れる）していた。局所宣言経路は paren の `[N][M]` を
// 積 (outer_stride) としてのみ扱い、第2サブスクリプト用の mid_stride を立てていなかった。
// (引数版は inner dims から mid_stride を設定するので元から動いた。)
// paren-array の先頭次元と次元数を捕捉し mid_stride = (積/先頭次元)*elem を設定して修正。
#include <assert.h>
struct P { int a, b; };

int main(void) {
  int t = 0;

  // int 3D 配列へのポインタ (pointee = 2D 配列)
  int cube[2][2][3];
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++)
      for (int k = 0; k < 3; k++) cube[i][j][k] = i * 6 + j * 3 + k;
  int (*pc)[2][3] = cube;
  t += pc[1][1][2];      // cube[1][1][2] = 11
  t += pc[0][1][0];      // 3
  pc[1][0][1] = 100;     // 書き込み
  t += cube[1][0][1];    // 100

  // struct 版
  struct P blk[2][2][2];
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++)
      for (int k = 0; k < 2; k++) blk[i][j][k].a = i * 4 + j * 2 + k;
  struct P (*pb)[2][2] = blk;
  t += pb[1][1][0].a;    // blk[1][1][0].a = 6
  t += pb[0][1][1].a;    // 3

  assert(t == 123); return 0;  // 11+3+100+6+3 = 123 ; 123-81 = 42
}
