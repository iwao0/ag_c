#include <assert.h>

const struct const_tag;
volatile union volatile_tag;
_Atomic struct atomic_tag;
_Alignas(3) struct aligned_tag;

static enum { FILE_STATIC_ENUM = 1 };
const enum { FILE_CONST_ENUM = 2 };
volatile enum NamedEnum { FILE_VOLATILE_ENUM = 3 };
_Atomic enum { FILE_ATOMIC_ENUM = 4 };
_Alignas(3) enum { FILE_ALIGNED_ENUM = 5 };

struct const_tag { int value; };
union volatile_tag { int value; };
struct atomic_tag { int value; };
struct aligned_tag { int value; };

int main(void) {
  static struct local_static_tag;
  volatile union local_volatile_tag;
  _Atomic struct local_atomic_tag;
  _Alignas(3) struct local_aligned_tag;

  extern enum { LOCAL_EXTERN_ENUM = 6 };
  const enum { LOCAL_CONST_ENUM = 7 };
  _Atomic enum { LOCAL_ATOMIC_ENUM = 8 };
  _Alignas(3) enum { LOCAL_ALIGNED_ENUM = 9 };

  struct local_static_tag { int value; };
  union local_volatile_tag { int value; };
  struct local_atomic_tag { int value; };
  struct local_aligned_tag { int value; };

  struct const_tag a = {1};
  union volatile_tag b = {2};
  struct atomic_tag c = {3};
  struct aligned_tag d = {4};
  struct local_static_tag e = {5};
  union local_volatile_tag f = {6};
  struct local_atomic_tag g = {7};
  struct local_aligned_tag h = {8};

  assert(a.value + b.value + c.value + d.value == 10);
  assert(e.value + f.value + g.value + h.value == 26);
  assert(FILE_STATIC_ENUM + FILE_CONST_ENUM + FILE_VOLATILE_ENUM +
             FILE_ATOMIC_ENUM + FILE_ALIGNED_ENUM ==
         15);
  assert(LOCAL_EXTERN_ENUM + LOCAL_CONST_ENUM + LOCAL_ATOMIC_ENUM +
             LOCAL_ALIGNED_ENUM ==
         30);
  assert(sizeof(struct aligned_tag) == sizeof(int));
  assert(sizeof(struct local_aligned_tag) == sizeof(int));
  return 0;
}
