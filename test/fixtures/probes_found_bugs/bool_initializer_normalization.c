// `_Bool` 初期化子は (value != 0) に正規化して 0/1 を格納する必要がある
// (C11 6.3.1.2)。スカラ変数は正規化されていたが、配列・struct メンバ・グローバルの
// 各初期化経路では生値が格納され `_Bool f[]={5}` が 5 のままになっていた。
// ローカル配列 / ローカル struct スカラ・配列メンバ / グローバル配列 /
// グローバル struct スカラ・配列メンバ の全経路を網羅する。
_Bool gscalar = 7;            // -> 1
_Bool garr[4] = {0, 5, 0, -3}; // -> 0,1,0,1
struct GS { _Bool b; _Bool arr[3]; int n; };
struct GS gs = {9, {0, 100, 200}, 4}; // b->1, arr->0,1,1

int main(void) {
  int t = 0;

  // グローバル
  t += gscalar;                                  // 1
  t += garr[0] + garr[1] + garr[2] + garr[3];    // 0+1+0+1 = 2
  t += gs.b;                                     // 1
  t += gs.arr[0] + gs.arr[1] + gs.arr[2];        // 0+1+1 = 2
  t += gs.n;                                     // 4

  // ローカル配列
  _Bool la[4] = {0, 5, 0, -3};                   // 0,1,0,1
  t += la[0] + la[1] + la[2] + la[3];            // 2

  // ローカル struct (スカラ + 配列メンバ)
  struct GS ls = {42, {7, 0, 9}, 6};             // b->1, arr->1,0,1
  t += ls.b;                                     // 1
  t += ls.arr[0] + ls.arr[1] + ls.arr[2];        // 1+0+1 = 2
  t += ls.n;                                     // 6

  // すべて 0/1 に正規化されていれば各 _Bool は 1 を超えない
  int over = 0;
  if (garr[1] > 1 || gs.arr[2] > 1 || la[3] > 1 || ls.arr[0] > 1) over = 1;
  t += (over == 0) * 21;                         // +21

  return t;  // globals 10 + local arr 2 + local struct 9 + 21 = 42
}
