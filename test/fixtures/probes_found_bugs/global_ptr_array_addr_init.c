// グローバルのポインタ配列/struct ポインタメンバを `&data[n]` / `data + n` で初期化。
// 旧実装は brace 初期化パーサが `&g` (ND_ADDR(ND_GVAR)) のみ解決し、`&data[n]`
// (ND_ADDR(ND_DEREF)、オフセット付き) を const int 評価で 0 にしていた。さらに codegen の
// 配列要素/配列メンバ出力がシンボル+オフセットを `_sym+off` で出さなかったため、
// ポインタ配列が NULL/不正値で埋まり deref で SIGSEGV していた。
// resolve_global_addr_init で (シンボル, バイトオフセット) を解決し、各 codegen 経路で
// `.quad _sym+off` を出力するよう修正。
int data[5] = {10, 20, 30, 40, 50};

// グローバルポインタ配列を要素アドレスで初期化
int *darr[3] = {&data[0], &data[2], &data[4]};
// decay / ポインタ算術形式
int *parr[3] = {data, data + 1, data + 4};

// struct のポインタメンバ
struct Two { int *p; int *q; };
struct Two two = {&data[1], &data[3]};

// struct のポインタ配列メンバ
struct Box { int *ptrs[2]; int tag; };
struct Box box = {{&data[1], &data[3]}, 7};

int main(void) {
  int r = 0;

  if (*darr[0] != 10 || *darr[1] != 30 || *darr[2] != 50) r |= 1;
  if (*parr[0] != 10 || *parr[1] != 20 || *parr[2] != 50) r |= 2;
  if (*two.p != 20 || *two.q != 40) r |= 4;
  if (*box.ptrs[0] != 20 || *box.ptrs[1] != 40 || box.tag != 7) r |= 8;

  // エイリアス確認: data を書き換えるとポインタ経由でも見える
  data[2] = 99;
  if (*darr[1] != 99) r |= 16;

  return r == 0 ? 42 : r;
}
