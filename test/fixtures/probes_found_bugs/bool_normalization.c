// _Bool conversion
int main(void) {
  _Bool b1 = 42;   // → 1
  _Bool b2 = 0;    // → 0
  _Bool b3 = -1;   // → 1
  return b1 + b2 + b3;  // 2
}
// 期待: 2
