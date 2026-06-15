// `struct T (*p)[N]` (配列へのポインタ) で要素 struct が >4 バイトのとき subscript が
// 壊れていた。
//   local : lvar_is_pointer 判定が `size > elem_size` (8 > struct サイズ) を使い、
//           8B struct では 8>8 が偽でポインタと認識されず E3064 (4B struct は 8>4 で動いた)。
//   param : struct ポインタ仮引数ブランチが `[N]` 配列次元を無視し `struct T *` 扱い
//           (stride = 1 要素) で行を跨げなかった。
// 局所は outer_stride>0&&size==8 でポインタ認識、引数は is_tag_pointer をクリアし
// outer_stride を 1 行に設定して修正。
struct Cell { int v; char tag; };   // 8 bytes (4B struct では元から動いた)

int sumrow(struct Cell (*row)[2], int rows) {
  int s = 0;
  for (int i = 0; i < rows; i++)
    for (int j = 0; j < 2; j++) s += row[i][j].v;
  return s;
}

int main(void) {
  struct Cell grid[3][2];
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 2; j++) { grid[i][j].v = i * 2 + j; grid[i][j].tag = 'A' + i; }

  int t = 0;

  // ローカル: 配列へのポインタ
  struct Cell (*row)[2] = grid;
  t += row[2][1].v;          // grid[2][1].v = 5
  t += row[0][0].v;          // 0
  row[1][1].v = 30;          // write through
  t += grid[1][1].v;         // 30
  t += (row[2][0].tag == 'C');

  // 関数引数: 配列へのポインタ
  t += sumrow(grid, 3);      // 0+1+2+30+4+5 = 42 (grid[1][1] was set to 30)

  return t - 36;  // 5+0+30+1+42-36 = 42
}
