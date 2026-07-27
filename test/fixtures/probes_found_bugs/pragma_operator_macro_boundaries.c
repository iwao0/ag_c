// C99/C11 の `_Pragma` は、通常の識別子ではなくプリプロセッサ演算子。
// 修正前は parser まで `_Pragma` が残り、有効な標準Cが E3005 で拒否されていた。
#define PRAGMA_TEXT "pack(push, 1)"
#define DO_PRAGMA_IMPL(x) _Pragma(#x)
#define DO_PRAGMA(x) DO_PRAGMA_IMPL(x)

// operand 自体をマクロ展開してから文字列リテラルとして解釈する。
_Pragma(PRAGMA_TEXT)
struct Pack1 {
  char lead;
  int value;
};
DO_PRAGMA(pack(pop))

// stringize で生成された文字列を再走査し、pack markerを同じ位置へ出力する。
DO_PRAGMA(pack(push, 2))
struct Pack2 {
  char lead;
  int value;
};
_Pragma("pack(pop)")

// 未実装の標準pragmaは未知の #pragma と同様に無視する。encoding-prefixも標準で合法。
_Pragma(L"STDC FP_CONTRACT OFF")
struct Natural {
  char lead;
  int value;
};

_Static_assert(sizeof(struct Pack1) == 5, "_Pragma pack(1)");
_Static_assert(sizeof(struct Pack2) == 6, "macro-generated _Pragma pack(2)");
_Static_assert(sizeof(struct Natural) == 8, "_Pragma pack(pop)");

int main(void) {
  struct Pack1 one = {'a', 17};
  struct Pack2 two = {'b', 19};
  struct Natural natural = {'c', 23};

  if (one.lead != 'a' || one.value != 17) return 1;
  if (two.lead != 'b' || two.value != 19) return 2;
  if (natural.lead != 'c' || natural.value != 23) return 3;
  return 0;
}
