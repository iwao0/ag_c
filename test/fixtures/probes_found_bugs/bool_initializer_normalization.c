// `_Bool` 初期化子は (value != 0) に正規化して 0/1 を格納する必要がある
// (C11 6.3.1.2)。スカラ変数は正規化されていたが、配列・struct メンバ・グローバルの
// 各初期化経路では生値が格納され `_Bool f[]={5}` が 5 のままになっていた。
// ローカル配列 / ローカル struct スカラ・配列メンバ / グローバル配列 /
// グローバル struct スカラ・配列メンバ の全経路を網羅する。
#include <assert.h>
_Bool gscalar = 7;            // -> 1
_Bool garr[4] = {0, 5, 0, -3}; // -> 0,1,0,1
struct GS { _Bool b; _Bool arr[3]; int n; };
struct GS gs = {9, {0, 100, 200}, 4}; // b->1, arr->0,1,1

int main(void) {
  // グローバル: 各 _Bool が 0/1 に正規化されていることを要素ごとに検査
  assert(gscalar == 1);          // 7 -> 1
  assert(garr[0] == 0);
  assert(garr[1] == 1);          // 5 -> 1
  assert(garr[2] == 0);
  assert(garr[3] == 1);          // -3 -> 1
  assert(gs.b == 1);             // 9 -> 1
  assert(gs.arr[0] == 0);
  assert(gs.arr[1] == 1);        // 100 -> 1
  assert(gs.arr[2] == 1);        // 200 -> 1
  assert(gs.n == 4);

  // ローカル配列
  _Bool la[4] = {0, 5, 0, -3};
  assert(la[0] == 0);
  assert(la[1] == 1);
  assert(la[2] == 0);
  assert(la[3] == 1);

  // ローカル struct (スカラ + 配列メンバ)
  struct GS ls = {42, {7, 0, 9}, 6};
  assert(ls.b == 1);             // 42 -> 1
  assert(ls.arr[0] == 1);        // 7 -> 1
  assert(ls.arr[1] == 0);
  assert(ls.arr[2] == 1);        // 9 -> 1
  assert(ls.n == 6);
  return 0;
}
