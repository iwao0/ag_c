/*
 * A cast may spell a qualified scalar type, but the resulting non-lvalue
 * value does not expose a top-level qualifier to generic selection.  Nested
 * pointer qualifiers still distinguish the converted type, and the generic
 * controlling expression remains unevaluated.
 */
#include <assert.h>

#define TYPE_IS(expression, type) \
  _Generic((expression), type: 1, default: 0)

typedef const signed char ConstSignedChar;
typedef volatile unsigned long long VolatileUnsignedLongLong;
typedef const float ConstFloat;
typedef volatile long double VolatileLongDouble;
typedef const float _Complex ConstFloatComplex;
typedef volatile double _Complex VolatileDoubleComplex;
typedef _Atomic(int) AtomicInt;

typedef int *const ConstIntPointer;
typedef const int *const ConstPointerToConstInt;
typedef int *volatile VolatileIntPointer;
typedef VolatileIntPointer *const ConstPointerToVolatileIntPointer;

typedef int (*Unary)(int);
typedef Unary const ConstUnary;

static int add_two(int value) { return value + 2; }

_Static_assert(TYPE_IS((ConstSignedChar)7, signed char),
               "const integer cast yields its unqualified value type");
_Static_assert(TYPE_IS((VolatileUnsignedLongLong)9,
                       unsigned long long),
               "volatile integer cast yields its unqualified value type");
_Static_assert(TYPE_IS((ConstFloat)1.5f, float),
               "const floating cast yields its unqualified value type");
_Static_assert(TYPE_IS((VolatileLongDouble)2.5L, long double),
               "volatile long double cast loses only its top qualifier");
_Static_assert(TYPE_IS((ConstFloatComplex)1, float _Complex),
               "const complex cast yields its unqualified value type");
_Static_assert(TYPE_IS((VolatileDoubleComplex)1, double _Complex),
               "volatile complex cast yields its unqualified value type");
_Static_assert(TYPE_IS((AtomicInt)11, AtomicInt),
               "an atomic cast retains its distinct atomic value type");
_Static_assert(TYPE_IS((ConstIntPointer)0, int *),
               "const pointer cast loses pointer-object qualification");
_Static_assert(TYPE_IS((ConstPointerToConstInt)0, const int *),
               "pointee const survives top-level qualification removal");
_Static_assert(TYPE_IS((ConstPointerToVolatileIntPointer)0,
                       int *volatile *),
               "nested pointer qualification survives");
_Static_assert(TYPE_IS((ConstUnary)add_two, Unary),
               "const function pointer cast yields a function pointer value");

int main(void) {
  int value = 17;
  int *pointer = &value;
  int **pointer_to_pointer = &pointer;
  int effects = 0;

  assert(_Generic((ConstSignedChar)(effects++, 7),
                  signed char: 21, default: 0) == 21);
  assert(effects == 0);
  assert(_Generic((ConstFloat)(effects++, 3.5f),
                  float: 22, default: 0) == 22);
  assert(effects == 0);
  assert(_Generic((ConstPointerToConstInt)(effects++, &value),
                  const int *: 23, int *: 0, default: 0) == 23);
  assert(effects == 0);
  assert(_Generic((ConstPointerToVolatileIntPointer)
                      (effects++, pointer_to_pointer),
                  int *volatile *: 24, int **: 0, default: 0) == 24);
  assert(effects == 0);
  assert(_Generic((ConstUnary)(effects++, add_two),
                  Unary: 25, default: 0) == 25);
  assert(effects == 0);
  assert(((ConstUnary)add_two)(40) == 42);
  assert(*(ConstPointerToConstInt)pointer == 17);
  return 0;
}
