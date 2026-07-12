struct S {
  char value;
};

int main(void) {
  typedef long LocalWord;
  if (sizeof(LocalWord) != 8) return 1;
  if (_Alignof(LocalWord) != 8) return 2;

  {
    struct S {
      int x;
      int y;
    } local = {1, 2};
    if (sizeof(struct S) != 8) return 3;
    if (_Alignof(struct S) != 4) return 4;
    if (_Generic(local, struct S: 1, default: 0) != 1) return 5;
  }

  if (sizeof(struct S) != 1) return 6;
  if (_Alignof(struct S) != 1) return 7;
  if (sizeof((struct Inline { int value; }){3}) != 4) return 8;
  return 0;
}
