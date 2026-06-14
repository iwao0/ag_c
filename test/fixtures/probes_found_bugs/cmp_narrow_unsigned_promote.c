// unsigned char / unsigned short は比較前に符号付き int へ整数昇格される
// (int が全値を表現できるため)。よって (unsigned char)200 > (int)-1 は
// 200 > -1 で真。
// 修正前: オペランドが unsigned なら符号なし比較し、-1 を巨大値に変換して
//         偽を返していた。
// 期待: exit=53
int main(void) {
  unsigned char c = 200;
  unsigned short h = 65000;
  int i = -1;
  int r = 50;
  if (c > i) r += 1;   // 200   > -1 (signed) => true
  if (h > i) r += 2;   // 65000 > -1 (signed) => true
  return r;            // 53
}
