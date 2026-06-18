// `(struct V*)<expr>` キャスト結果に対する `->` でのメンバアクセス
// 修正前: apply_cast はキャストに含まれる tag 情報 (cast_tag_kind/name/len) を
// 受け取らず、`(struct V*)0)->b` 形式の古典的 offsetof パターンで
// `'->' の左辺は構造体/共用体ポインタである必要があります` (E3005) を出していた。
//
// 修正: apply_cast のシグネチャに cast_tag_kind/name/len を追加し、
// is_pointer + cast_tag_kind が TK_STRUCT/TK_UNION のとき ND_PTR_CAST で
// 結果をラップして tag 情報と is_tag_pointer=1 を伝播する。
// (struct V*)void_ptr のようなパターンも動く。
#include <assert.h>
struct V { char a; int b; char c; long d; };
int main(void) {
  // offsetof 風: ヌルポインタを struct V * にキャストして member offset を取る
  long off_b = (long)&((struct V*)0)->b;
  long off_d = (long)&((struct V*)0)->d;
  assert(off_b == 4);    // char a(0) + pad -> int b at 4
  assert(off_d == 16);   // long d at 16
  return 0;
}
// 期待: 20
