// 多次元配列/配列ポインタの「行」を値文脈でポインタ算術に使うケース。
// `int m[3][4]` の `m[i]` や `int(*p)[4]` の `*(p+k)`/`p[i]` は行 (= int[4]) で、
// 値文脈ではポインタ (= 行先頭アドレス) へ decay する。旧実装はこの decay 結果を
// add() がポインタと認識せず、`m[i] + k` / `*(p+k) + j` が要素サイズ (4) でスケール
// されず byte 加算になり不正アドレスを deref していた (一旦 `int *r=m[i];` のように
// 変数へ入れると正しく動いていた)。スカラ要素 `int *q` の `q[i]` 算術は不変。
#include <assert.h>
struct Vec { int data[4]; };
int g[3][4];
int sum_row(int (*p)[4], int rows){
  int s = 0;
  for (int i=0;i<rows;i++){
    for (int j=0;j<4;j++) s += *(p[i] + j);   // 行 decay + inline 算術
  }
  return s;
}
int main(void){
  int m[3][4];
  for (int i=0;i<3;i++) for(int j=0;j<4;j++) m[i][j] = i*10+j;

  // 2D 配列の行 inline 算術
  assert(*(m[1] + 2) == 12);
  assert(*(m[2] + 3) == 23);
  assert(*(m[0] + 0) == 0);

  // ポインタ-to-1D-array
  int (*p)[4] = m;
  assert(*(p[1] + 1) == 11);
  assert(*(*(p+1) + 1) == 11);     // unary deref 経由
  assert((*(p+2))[3] == 23);

  // char 2D 行 (要素サイズ 1、行全体 3B ≤ 8B: IR の崩壊判定 type_size>8 から漏れていた)
  char c[2][3] = {{1,2,3},{4,5,6}};
  assert(*(c[1] + 2) == 6);

  // 行全体が 8 バイト以下の小さい行も崩壊させる
  int m22[2][2]; m22[0][0]=1; m22[0][1]=2; m22[1][0]=3; m22[1][1]=4;
  assert(*(m22[1] + 1) == 4);    // 行 = int[2] = 8B
  short sh[2][3]; sh[1][2] = 7;
  assert(*(sh[1] + 2) == 7);     // 行 = short[3] = 6B

  // struct の配列メンバ decay + 算術
  struct Vec v = {{10,20,30,40}};
  assert(*(v.data + 2) == 30);

  // global 2D の行算術
  for (int i=0;i<3;i++) for(int j=0;j<4;j++) g[i][j] = i*4+j;
  assert(*(g[2] + 1) == 9);

  // pointer-to-array 仮引数
  for (int i=0;i<3;i++) for(int j=0;j<4;j++) m[i][j] = 1;
  assert(sum_row(m, 3) == 12);

  // スカラ p[i] の算術は誤スケールしない (回帰防止)
  int arr[5] = {0,1,2,3,4};
  int *q = arr;
  assert(q[1] + q[2] == 3);
  struct S { int a, b; } s = {10, 20};
  int *pp = &s.a;
  assert(pp[0] + pp[1] == 30);   // 行 decay 誤判定で 4 倍されないこと

  return 0;
}
