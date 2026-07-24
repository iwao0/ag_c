// comma演算子の左operandでも、不完全型lvalueから値への変換は不正。
// 期待: ag_cはE3064。
struct Incomplete;

int main(void) {
  struct Incomplete *pointer = 0;
  return (*pointer, 0);
}
