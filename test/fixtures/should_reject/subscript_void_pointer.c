// `[]`の基底は完全なオブジェクト型へのポインタでなければならない。
// voidはオブジェクト型ではないため`void *`のsubscriptは不正。
// 期待: ag_cはE3064。
int main(void) {
  void *pointer = 0;
  pointer[0];
  return 0;
}
