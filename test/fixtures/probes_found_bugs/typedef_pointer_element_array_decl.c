/* `typedef IP IPA[3]; IPA arr = {...}` (typedef 配列で要素がポインタ + 配列宣言):
 *   typedef int *IP;
 *   typedef IP IPA[3];
 *   IPA arr = { &x, &x, &x };    /* 旧: E3064 (スカラ初期化子 1 要素のみ) *\/
 *
 * 以前は base が pointer typedef のとき is_pointer=1 が宣言子側まで継承され、
 * declarator に `*` が無くても `IPA arr` がポインタ宣言として処理されていた。3522 経路
 * (配列ポインタ) に流れ、3 要素の brace 初期化子が「スカラ初期化子は 1 要素のみ」と E3064。
 *
 * 修正: declarator に `*` 追加なし + 基底が pointer typedef + 配列 typedef のとき
 * (`ptr_levels == 0 && td_array_dim_count > 0 && td_array_elem_size > 0`)、is_pointer を 0 に
 * リセットして配列宣言経路 (3616: register_typedef_array_lvar) に流す。要素サイズも
 * td_array_elem_size (= ポインタサイズ 8) で登録し、pointer_qual_levels=1 と
 * base_deref_size=pointee サイズを立てて `*arr[i]` が pointee で deref されるようにする。
 *
 * 多次元 typedef (`typedef int M[2][3]; M m`) は td_array_dim_count >= 2 で除外され既存挙動
 * (elem_size=4, 多次元 stride) のまま。pointer-to-array typedef (`typedef int (*PA)[3]; PA p`)
 * は declarator に `*` 無いが is_pointer は元から立てる (base が `(*PA)[3]` = pointer)、
 * td_array_dim_count > 0 + td_array_elem_size=0 (is_array=0 のため) で除外される。 */
#include <assert.h>

typedef int *IP;
typedef IP IPA[3];

typedef int (*BinOp)(int, int);
typedef BinOp OpArr3[3];

static int add(int a, int b) { return a + b; }
static int sub_(int a, int b) { return a - b; }
static int mul_(int a, int b) { return a * b; }

int main(void) {
  /* (1) IPA: int* 配列 */
  int x = 100, y = 200, z = 300;
  IPA arr = { &x, &y, &z };
  assert(*arr[0] == 100);
  assert(*arr[1] == 200);
  assert(*arr[2] == 300);

  /* (2) OpArr3: 関数ポインタ配列 */
  OpArr3 ops = { add, sub_, mul_ };
  assert(ops[0](7, 2) == 9);
  assert(ops[1](7, 2) == 5);
  assert(ops[2](7, 2) == 14);

  /* (3) 部分初期化 (残り NULL) */
  IPA arr2 = { &x };
  assert(*arr2[0] == 100);
  assert(arr2[1] == 0);
  assert(arr2[2] == 0);

  /* (4) 多重 typedef ネスト */
  typedef int Int;
  typedef Int MyInt;
  typedef MyInt Score;
  typedef Score *ScorePtr;
  typedef ScorePtr ScorePtrArr[3];
  Score s = 42;
  ScorePtrArr scs = { &s, &s, &s };
  assert(*scs[0] == 42 && *scs[1] == 42 && *scs[2] == 42);

  return 0;
}
