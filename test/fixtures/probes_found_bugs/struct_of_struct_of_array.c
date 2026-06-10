// 3階層 struct
struct Inner { int arr[3]; };
struct Mid { struct Inner ins[2]; };
struct Outer { struct Mid m; int x; };
int main(void) {
  struct Outer o;
  o.m.ins[0].arr[0] = 1;
  o.m.ins[0].arr[1] = 2;
  o.m.ins[1].arr[2] = 3;
  o.x = 36;
  return o.m.ins[0].arr[0] + o.m.ins[0].arr[1] + o.m.ins[1].arr[2] + o.x;
}
// 期待: 42
