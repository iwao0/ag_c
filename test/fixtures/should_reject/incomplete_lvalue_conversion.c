// 不完全なstruct型のlvalueを値へ変換するのは不正。
// `&*pointer`は正当だが、式文の`*pointer`はlvalue conversionを要求する。
// 期待: ag_cはE3064。
struct Incomplete;

int main(void) {
  struct Incomplete *pointer = 0;
  *pointer;
  return 0;
}
