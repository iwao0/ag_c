/* 同一 TU 内の「extern 宣言 → 同名定義」(C11 6.9.2)。tag/typedef を基底とする object で
 * 壊れていた。builtin (`extern int v; int v=5;`) は元から動作。2 つの独立バグ:
 *   (1) E3064: storage class フラグ (g_*_is_extern) が宣言間でリセットされず、前の
 *       `extern struct S es;` の extern が次の bare-struct 定義 `struct S es={7};` に漏れ、
 *       finalize が extern 分岐 (consume_toplevel_extern_initializer_if_any) に入り `={7}` の
 *       brace を psx_expr_assign で食べて「数値が必要」。reset_toplevel_decl_spec_state と
 *       parse_toplevel_tag_decl で storage class フラグを宣言ごとに 0 クリアして修正。
 *   (2) ASSEMBLE_FAIL (.comm 二重定義): typedef object 経路 (apply_toplevel_typedef_prefix_flags)
 *       が extern を無条件に 0 にしており、`extern T et;` が tentative 定義扱いで `.comm _et` を
 *       出し、本定義の data 出力と重複。extern/static を伝播させて修正 (漏れは (1) の reset で防止)。
 * 定義→extern の逆順や別 TU は元から動作。static が次の宣言へ漏れないこと (内部リンケージ維持) も検証。 */
#include <assert.h>

struct S { int a, b; };
extern struct S es;              /* extern 宣言 (tag 基底) */
struct S es = { 7, 8 };          /* 同一 TU 定義 */

typedef struct { int x; } T;
extern T et;                     /* extern 宣言 (typedef 基底) */
T et = { 11 };

extern int arr[3];               /* extern 宣言 (配列) */
int arr[3] = { 1, 2, 3 };

struct S sx = { 5, 6 };
extern struct S *esp;            /* extern 宣言 (ポインタ) */
struct S *esp = &sx;

/* static が後続の bare-struct 宣言へ漏れないこと (漏れると別 TU と内部リンケージ衝突) */
static struct S sa = { 100, 0 };
struct S sb = { 200, 0 };        /* 外部リンケージのはず */

/* 定義なしの extern 宣言の後でも、別変数定義が誤って extern 化されないこと */
extern struct S edecl_only;
struct S after = { 42, 0 };

int main(void) {
  assert(es.a == 7 && es.b == 8);
  assert(et.x == 11);
  assert(arr[0] == 1 && arr[1] == 2 && arr[2] == 3);
  assert(esp->a == 5 && esp->b == 6);
  assert(sa.a == 100);
  assert(sb.a == 200);
  assert(after.a == 42);
  return 0;
}
