// 不完全なstruct型へのポインタは`[]`の基底にできない。
// `&pointer[0]`で結果値を読まない場合もsubscript自体の制約違反。
// 期待: ag_cはE3064。
struct Incomplete;

int main(void) {
  struct Incomplete *pointer = 0;
  return &pointer[0] == pointer;
}
