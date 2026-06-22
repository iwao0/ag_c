/* ローカルの 2 次元 (以上の) データポインタ配列 `int *t[2][2]` の `t[i][j]` が SIGSEGV
 * だった (グローバル版は 320e0ff で別途修正、非ポインタ `int t[2][2]` は元から動作)。
 * 原因: register_multidim_array_lvar は arr_elem_size=8 で outer_stride を立てるが、
 * 登録後に pointer_qual_levels=1 / base_deref_size=elem_size(4) を立てる。
 * build_subscript_deref の「要素はポインタ」分岐 (pql>=1 && bds>0) が **1 段目** `t[i]` で
 * 発火し、行 (まだ配列) でなく最終 int* 扱いになって deref_size を inner_ds(8) から bds(4) に
 * 上書き → 2 段目が +4/ldrsw (4B) に化けた (要素は 8B ポインタのはず)。
 * 修正: fp/unsigned と同じ中間行判定 (inner_ds>0 && es>inner_ds) で 1 段目を中間行と認識し、
 * pointer-element 化を最終次元まで遅延しつつ pql/bds を carry する。単段 `int *arr[N]`
 * (inner_ds==0) や genuine 多段ポインタ `int **pp` (es==inner_ds) は従来どおり。
 * funcptr ローカル `int(*t[2][2])(void)` は別問題 (paren array stride + ネスト brace init) で未対応。 */
#include <assert.h>

int main(void) {
  int w = 1, x = 2, y = 3, z = 4;
  int *t[2][2] = { {&w, &x}, {&y, &z} };
  /* 値読み出し */
  assert(*t[0][0] == 1 && *t[0][1] == 2 && *t[1][0] == 3 && *t[1][1] == 4);
  /* 要素経由の代入 */
  *t[1][0] = 99;
  assert(y == 99);

  /* char* 2D ローカル (文字列ポインタ) */
  char *names[2][2] = { {"ab", "cd"}, {"ef", "gh"} };
  assert(names[0][1][0] == 'c' && names[1][0][1] == 'f');

  /* 3D ローカル */
  int v0=10,v1=11,v2=12,v3=13,v4=14,v5=15,v6=16,v7=17;
  int *u[2][2][2] = { {{&v0,&v1},{&v2,&v3}}, {{&v4,&v5},{&v6,&v7}} };
  assert(*u[0][0][0] == 10 && *u[1][0][1] == 15 && *u[1][1][1] == 17);

  /* 回帰防止: 1D ローカルポインタ配列 / genuine 多段ポインタ */
  int *p1[2] = { &w, &x };
  assert(*p1[0] == 1 && *p1[1] == 2);
  int vv = 42; int *pp = &vv; int **ppp = &pp;
  assert(**ppp == 42 && *ppp[0] == 42);

  return 0;
}
