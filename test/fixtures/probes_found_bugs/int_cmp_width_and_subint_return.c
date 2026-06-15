// 整数比較の幅と sub-int 戻り値の切り詰め。
// codegen は整数を 64bit レジスタで保持するが、計算結果や funcall 戻り値の i32 は
// 上位 32bit が未定義 (TRUNC は `mov w` でゼロ化、char/short 戻りは widening I32)。
// 旧実装は (1) char/short 戻り値を宣言幅へ切り詰めず、(2) i32 比較を 64bit レジスタで
// 行っていたため、`int f(int x){return x-1;} f(0)!=-1` や `char g(int x){return x;}
// g(300)!=44` がインライン比較で化けていた (変数へ格納すると ldrsw/strb で偶然正しく
// なる)。修正: 戻り型 char/short の callee 切り詰め + i32 比較を 32bit (w レジスタ) で
// 行う + 戻り型 unsigned を funcall ノードへ伝播 (ULT/ULE 選択)。
int  neg(int x){ return x - 1; }          // 計算結果の負値
char trunc_char(int x){ return x; }       // sub-int 戻り
short trunc_short(int x){ return x; }
unsigned uret(void){ return 0xFFFFFFFFu; }
int iret(void){ return -42; }
unsigned char  uc_ret(int x){ return x; }   // unsigned sub-int 戻りはゼロ拡張
unsigned short us_ret(int x){ return x; }

int main(void) {
  int r = 0;

  // int 計算結果の負値をインライン比較
  if (neg(0) != -1) r |= 1;
  if (neg(0) == 0) r |= 2;
  if (!(neg(-5) == -6)) r |= 4;
  if (neg(0) >= 0) r |= 8;

  // char/short 戻り値の切り詰め (正の結果)
  if (trunc_char(300) != 44) r |= 16;       // 300 & 0xff = 44
  if (trunc_short(70000) != 4464) r |= 32;  // 70000 - 65536

  // char/short 戻り値の負値 (算術・比較とも)
  if (trunc_char(255) != -1) r |= 64;       // (signed char)255 = -1
  if (trunc_char(200) + 100 != 44) r |= 128;// -56 + 100 = 44
  if (trunc_short(60000) != -5536) r |= 256;

  // unsigned 戻り値の比較は符号なし (ULE)
  if (uret() <= 100) r |= 512;              // 0xFFFFFFFF <=u 100 → false
  if (!(uret() > 1000)) r |= 1024;
  if (uret() != 0xFFFFFFFFu) r |= 2048;

  // (int)/(unsigned) キャストと funcall の相互作用 (符号ラベルの取り扱い)
  if ((int)uret() != -1) r |= 4096;         // (int)0xFFFFFFFF = -1
  if ((unsigned)iret() != 4294967254u) r |= 8192; // (unsigned)(-42)

  // unsigned char/short 戻りはゼロ拡張 (符号拡張で負に化けない)
  if (uc_ret(200) != 200) r |= 16384;       // (unsigned char)200 = 200
  if (uc_ret(300) != 44) r |= 32768;        // 300 & 0xff
  if (us_ret(40000) != 40000) r |= 65536;   // bit15 立っても正値
  if (us_ret(70000) != 4464) r |= 131072;   // 70000 & 0xffff

  return r == 0 ? 42 : r;
}
