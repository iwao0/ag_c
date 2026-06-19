// struct ポインタ配列メンバ `struct N *arr[N]` を subscript した結果が
// struct ポインタと認識されず `h.arr[i]->v` が E3005 になっていた回帰テスト。
// build_member_deref_node がポインタ配列メンバに pql/base_deref_size を立てる
// ことで、build_subscript_deref が要素を単段ポインタと認識する。
#include <assert.h>
struct N { int v; struct N *nx; };
struct Holder { struct N *arr[3]; };

int main(void) {
  struct N a = {1, 0}, b = {2, 0}, c = {3, 0};
  a.nx = &b;
  struct Holder h;
  h.arr[0] = &a;
  h.arr[1] = &b;
  h.arr[2] = &c;

  int s = 0;
  for (int i = 0; i < 3; i++) s += h.arr[i]->v; // 1+2+3 = 6
  h.arr[0]->nx->v = 30;                          // チェーン -> 経由の書き込み (b.v=30)
  s += h.arr[0]->nx->v;                          // +30 = 36
  s += b.v;                                      // +30 = 66
  assert(s == 66); return 0;                                 // 42
}
