/*
 * C11 6.8.6.1 only forbids a goto from outside the scope of a variably
 * modified identifier. Arrays whose bounds are integer constant expressions
 * remain fixed-size objects, even when the ICE uses logical, conditional,
 * sizeof, floating-constant cast, or enumeration operands.
 */
#include <assert.h>

enum { ENUM_BOUND = 2 };

static int enter_fixed_array_scope(void) {
  goto entered;
  {
    int logical_and[1 && 1];
    int logical_or[0 || 2];
    int conditional[1 ? 2 : 3];
    int sizeof_bound[sizeof(int) == 4 ? 2 : 1];
    int floating_cast[(int)2.5];
    int enumeration[ENUM_BOUND];

  entered:
    logical_and[0] = 1;
    logical_or[0] = 2;
    conditional[0] = 3;
    conditional[1] = 4;
    sizeof_bound[0] = 5;
    sizeof_bound[1] = 6;
    floating_cast[0] = 7;
    floating_cast[1] = 8;
    enumeration[0] = 3;
    enumeration[1] = 3;

    _Static_assert(sizeof(logical_and) == sizeof(int),
                   "logical-and bound is an ICE");
    _Static_assert(sizeof(logical_or) == sizeof(int),
                   "logical-or bound is an ICE");
    _Static_assert(sizeof(conditional) == 2 * sizeof(int),
                   "conditional bound is an ICE");
    _Static_assert(sizeof(sizeof_bound) == 2 * sizeof(int),
                   "sizeof bound is an ICE");
    _Static_assert(sizeof(floating_cast) == 2 * sizeof(int),
                   "floating cast bound is an ICE");
    _Static_assert(sizeof(enumeration) == 2 * sizeof(int),
                   "enumeration bound is an ICE");

    return logical_and[0] + logical_or[0] + conditional[0] +
           conditional[1] + sizeof_bound[0] + sizeof_bound[1] +
           floating_cast[0] + floating_cast[1] + enumeration[0] +
           enumeration[1];
  }
}

int main(void) {
  assert(enter_fixed_array_scope() == 42);
  return 0;
}
