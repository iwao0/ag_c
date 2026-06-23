// c-testsuite 00210: GNU __attribute__((...)) を宣言・キャストで読み飛ばす。
// 修正前: `} __attribute__((packed)) T` や `int ATTR f()` で E2006。
#define ATTR __attribute__((__noinline__))
typedef union U { unsigned short u; unsigned char b[2]; } __attribute__((packed)) U;
typedef union __attribute__((packed)) V { unsigned short u; unsigned char b[2]; } V;
extern void bar(void) __attribute__((stdcall));
void __attribute__((stdcall)) bar(void) {}
ATTR int pick(void) { return 42; }
#include <assert.h>
int main(void) {
  void *fp = &pick;
  assert(((ATTR int (*)(void))fp)() == 42);
  assert(((int(ATTR *)(void))fp)() == 42);
  return 0;
}
