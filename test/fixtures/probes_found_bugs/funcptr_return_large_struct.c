/* 1/2/4/8B 以外 (x8 ret_area 間接返し ABI) の struct/union を返す関数ポインタの
 * 間接呼び出し。以前は IR build 失敗 ("ir build/emit failed")。メンバアクセス以前に
 * `struct Big r = ob(100);` (代入のみ) でも落ちていた。原因は 3 箇所:
 *   (1) parse_call_postfix が間接 funcall ノードに ret_struct_size を設定せず 0 のまま
 *       → ir_builder が struct 戻りを scalar 扱い。callee funcptr の戻り tag (pql<=1 で
 *       値戻り) からサイズを導出して設定。
 *   (2) build_assign_struct が間接 struct 戻りを明示 fail。汎用 funcall 経路へ委譲し
 *       ret_area から dst へ memcpy する。
 *   (3) build_node_funcall の ret_area 確保が direct call 限定 (!fn->callee)。間接でも
 *       x8 ret_area ABI は同じなので両方で確保 (codegen は x8 設定と blr を独立に出す)。
 * 1/2/4/8B のレジスタ返しは funcptr_return_struct_member で別途確認済み。 */
#include <assert.h>

struct Big { int a, b, c, d, e; };               /* 20B */
struct Mid { int x, y, z; };                      /* 12B */
struct Mix { char tag; int val; double d; };      /* 16B (非 1/2/4/8 -> 間接) */
union  U   { struct Mid m; long long pad[2]; };   /* 16B union */

struct Big mkbig(int s){ struct Big r; r.a=s; r.b=s+1; r.c=s+2; r.d=s+3; r.e=s+4; return r; }
struct Mid mkmid(int s){ struct Mid r; r.x=s; r.y=s*2; r.z=s*3; return r; }
struct Mix mkmix(int s){ struct Mix r; r.tag=(char)s; r.val=s*10; r.d=s*1.5; return r; }
union  U   mku(int s)  { union U r; r.m.x=s; r.m.y=s+1; r.m.z=s+2; return r; }

struct Mid (*gop)(int) = mkmid;                   /* ファイルスコープ funcptr */

int take(struct Big b){ return b.a + b.e; }       /* >16B を値で受ける */

int main(void) {
  /* 変数で受けてからアクセス (代入経路) */
  struct Big (*ob)(int) = mkbig;
  struct Big bv = ob(10);
  assert(bv.a == 10 && bv.e == 14);

  /* 間接呼び出し結果への直接メンバアクセス (materialize 経路) */
  assert(ob(100).a == 100);
  assert(ob(100).e == 104);

  /* 12B */
  struct Mid (*om)(int) = mkmid;
  assert(om(5).x == 5 && om(5).y == 10 && om(5).z == 15);
  struct Mid mv = om(7);
  assert(mv.z == 21);

  /* deref 形 `(*om)(x).z` */
  assert((*om)(4).z == 12);

  /* グローバル funcptr */
  assert(gop(3).y == 6);

  /* 16B 混在メンバ (char/int/double) */
  struct Mix (*ox)(int) = mkmix;
  assert(ox(2).val == 20);
  assert(ox(2).d == 3.0);

  /* 16B union */
  union U (*ou)(int) = mku;
  assert(ou(8).m.z == 10);

  /* 間接 struct 戻り値を値引数として渡す (引数も間接渡し) */
  assert(take(ob(50)) == 50 + 54);

  return 0;
}
