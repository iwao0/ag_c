// グローバル struct with func ptr
int sq(int x) { return x * x; }
struct Op { int (*f)(int); };
struct Op gop = {sq};
int main(void) {
  return gop.f(7);  // 49
}
// 期待: 49
