// voidへのcastでも、operandの不完全型lvalueは値への変換を要求するため不正。
// 期待: ag_cはE3064。
struct Incomplete;

int main(void) {
  struct Incomplete *pointer = 0;
  (void)*pointer;
  return 0;
}
