/* `unsigned char*` / `unsigned short*` を介した pointee アクセスが符号拡張 (ldrsb/ldrsh)
 * されて値が化けていた (例: 200 → -56)。スカラ unsigned や unsigned 配列要素は元から
 * zero-extend されていたが、ポインタ経由の以下が漏れていた:
 *   (1) local 変数の subscript `p[i]`: build_subscript_deref の最終要素判定が
 *       `pql==0 && inner_ds==0` で、単段ポインタ (pql=1 / inner_ds=elem) を最終要素と
 *       認識できなかった。fp の中間行判定と対称な `!is_pointer && !(inner_ds>0 && es>inner_ds)`
 *       に変更。
 *   (2) 仮引数 `unsigned char* p` の deref/subscript: register_param_lvar が pointee の
 *       unsigned 性を lvar に設定していなかった。param_decl_spec_t に is_unsigned を捕捉して
 *       var->is_unsigned に伝播 (build_lvar_or_vla_node が pointee_is_unsigned へ運ぶ)。
 *   (3) ポインタ算術 deref `*(p+i)`: build_unary_deref が ND_ADD/SUB operand の pointee
 *       unsigned を辿らなかった。node_pointee_is_unsigned ヘルパで辿る (pointee_fp と対称)。
 * signed char / short ポインタは従来どおり符号拡張を保つ。 */
#include <assert.h>

int  sub_param(unsigned char *p)   { return p[1]; }
int  deref_param(unsigned char *p) { return *p; }
int  arith_param(unsigned char *p) { return *(p + 2); }
int  ushort_param(unsigned short *p){ return p[1]; }
int  schar_param(signed char *p)   { return p[0]; }   /* 符号維持 */

int main(void) {
  unsigned char a[3] = { 10, 200, 250 };

  /* local 変数経由 */
  unsigned char *lp = a;
  assert(lp[1] == 200);          /* subscript: zero-extend */
  assert(*lp == 10);             /* deref */
  assert(*(lp + 2) == 250);      /* arith deref */

  /* 仮引数経由 */
  assert(sub_param(a) == 200);
  assert(deref_param(a) == 10);
  assert(arith_param(a) == 250);

  /* unsigned short */
  unsigned short s[2] = { 1, 60000 };
  unsigned short *sp = s;
  assert(sp[1] == 60000);
  assert(ushort_param(s) == 60000);

  /* 多段・2D も最内 pointee の unsigned を保つ */
  unsigned char **pp = &lp;
  assert(pp[0][1] == 200);
  unsigned char m[2][3] = { {1,2,3}, {4,5,255} };
  unsigned char (*mp)[3] = m;
  assert(mp[1][2] == 255);

  /* signed は符号拡張のまま (回帰防止) */
  signed char neg[1] = { -56 };
  assert(schar_param(neg) == -56);

  return 0;
}
