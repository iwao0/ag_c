// ネストした compound literal
struct Inner { int v; };
struct Outer { struct Inner inner; int extra; };
int main(void) {
  struct Outer o = (struct Outer){ (struct Inner){42}, 8 };
  return o.inner.v + o.extra;
}
// 期待: 50
