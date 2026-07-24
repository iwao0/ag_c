// 関数型は完全なオブジェクト型ではないため、関数ポインタの`[]`は不正。
// 期待: ag_cはE3064。
static int identity(int value) {
  return value;
}

int main(void) {
  int (*function)(int) = identity;
  return function[0](7);
}
