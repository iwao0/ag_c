// 非 bitfield メンバの直後の bitfield が、収まるのに同じ storage ユニットへ詰められず
// sizeof が過大になっていたバグ。`struct { char c; int x:20; }` が ag_c=8 / clang=4。
// 原因: 直前の非 bitfield メンバで bitfield 累積状態がリセットされ、次の bitfield が
//      ALIGN_UP(current_off, sizeof(T)) で新しい整列ユニットを確保していた。
// 修正: 新しい bitfield run 開始時、現在位置を含む sizeof(T) ユニット内に sizeof(T) 境界を
//      跨がず収まるなら、そのユニット (ALIGN_DOWN) へ詰める (AAPCS)。跨ぐ場合は整列。
// 修正前: sizeof 過大 (char;int:20 が 8)
// 期待: exit=42
struct C  { char c; int x:20; };           // clang sizeof=4 (char@bit0-7, x@bit8-27)
struct D  { short s; int x:20; };           // 16+20=36>32 -> 別ユニット, sizeof=8
struct E  { char c; char x:4; };            // sizeof=2
struct F  { char c; int x:20; unsigned y:8; }; // 28+8=36>32 -> y 別ユニット, sizeof=8

int main(void){
    struct C v; v.c = 'A'; v.x = -12345;
    int layout = (sizeof(struct C)==4) && (sizeof(struct D)==8)
              && (sizeof(struct E)==2) && (sizeof(struct F)==8);
    int values = (v.c=='A') && (v.x==-12345);   // 同一ユニットでも値が壊れない
    return (layout && values) ? 42 : 0;
}
