// _Alignof が struct/union/配列に対して「サイズ」を返していた (sizeof と同じ実装)。
// 基本型は size==align で偶然合うが、`struct{int,int}` (size 8/align 4) や
// `int[10]` (size 40/align 4)、`_Alignas(16) int` を含む struct で誤っていた。
// struct のアラインメント (agg_align) を tag テーブルへ保存し、_Alignof は配列で
// 要素数を掛けず、struct はその align を返すよう修正。
struct P { int x, y; };               // align 4, size 8
struct Q { char a; double b; };       // align 8, size 16
struct A { char c; _Alignas(16) int x; }; // align 16, size 32
struct Small { char a, b; };          // align 1, size 2
union U { char c; long l; };          // align 8

int main(void) {
  int t = 0;

  t += (_Alignof(struct P) == 4);
  t += (_Alignof(struct Q) == 8);
  t += (_Alignof(struct A) == 16);
  t += (_Alignof(struct Small) == 1);
  t += (_Alignof(union U) == 8);
  t += (_Alignof(int[10]) == 4);          // 配列 = 要素アラインメント
  t += (_Alignof(struct Q[3]) == 8);

  // 基本型は従来通り
  t += (_Alignof(char) == 1) + (_Alignof(int) == 4) + (_Alignof(double) == 8) + (_Alignof(int*) == 8);

  // sizeof は変わらない
  t += (sizeof(struct P) == 8) + (sizeof(struct A) == 32);

  return t + 29;  // 13 checks -> 13+29 = 42
}
